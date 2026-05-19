#include "FileClient.h"
#include "LocalFileStorage.h"
#include "Messages.h"
#include <QCoreApplication>
#include <QDir>
#include <memory>
#include <qnamespace.h>

FileClient::FileClient() {
  socket = new QLocalSocket(this);
  fileStorage = std::make_unique<LocalFileStorage>();
}

TreeDiff *FileClient::getNegotiationState() { return &negotiationState; }

void FileClient::configure(const FileClientConfig &config) {
  username = config.username;
  password = config.password;
  shouldUseTimer = !config.manualTick;
  syncStrategy = config.syncStrategy;
  fileStorage->setRoot(QDir(config.rootDir).absolutePath());
  merkleTree = std::make_unique<MerkleTree>(
      fileStorage->rootPath(username).toStdString());
  merkleTree->setHasher(FileHasher(fileStorage.get(), username));
  merkleTree->buildFromStorage(fileStorage.get(), username);
  serverName = config.serverName;
  tickIntervalMs = config.tickIntervalMs;
  database.storeUser(username, password, fileStorage->rootPath(username));
}

LocalFileStorage *FileClient::getStorage() { return fileStorage.get(); }

void FileClient::connectToServer() { socket->connectToServer(serverName); }

void FileClient::start() {
  setupConnections();
  connectToServer();
}

void FileClient::setupConnections() {
  QObject::connect(socket, &QLocalSocket::connected, this,
                   [this]() { qDebug() << "Connected event fired."; });
  QObject::connect(this, &FileClient::negotiationCompleted, this,
                   &FileClient::handleNegotiationCompleted);
  QObject::connect(socket, &QLocalSocket::readyRead, this, [this]() {
    MessageProtocol::processBuffer(socket, buffer, [this](Message *msg) {
      if (!msg) {
        qDebug() << "Failed to deserialize message";
        return;
      }
      switch (msg->type()) {
      case MessageType::ClientAuth:
        handleAuthResponse(static_cast<AuthResponseMessage *>(msg));
        break;
      case MessageType::SyncRequest:
        handleSyncResponse(static_cast<SyncRequestMessage *>(msg));
        break;
      case MessageType::MerkleSync:
        handleMerkleSyncResponse(static_cast<MerkleSyncMessage *>(msg));
        break;
      default:
        handleUnrecognized(msg);
        break;
      }
    });
  });
  if (shouldUseTimer) {
    QObject::connect(&timer, &QTimer::timeout, this,
                     [this]() { clientTick(); });
    timer.start(tickIntervalMs);
  }
}

FileClient::~FileClient() {
  socket->blockSignals(true);
  QObject::disconnect(socket, nullptr, nullptr, nullptr);
  socket->abort();
}

void FileClient::handleSyncResponse(SyncRequestMessage *msg) {
  pendingMessages--;
  if (msg->operationType == FileOperationType::Write) {
    handleWriteResponse(msg);
  } else if (msg->operationType == FileOperationType::Delete) {
    handleDeleteResponse(msg);
  }
  checkSyncCompletionAndUnlock();
}

void FileClient::checkSyncCompletionAndUnlock() {
  if (pendingMessages == 0) {
    currentlyDoingSyncOps = false;
    qDebug() << "All of current sync items have been synced.";
    Q_EMIT syncCompleted();
    if (shouldUseTimer) {
      timer.start(tickIntervalMs);
    }
  }
}
void FileClient::handleWriteResponse(SyncRequestMessage *msg) {
  if (msg->operationStatus == FileOperationStatus::Done)
    return;
  if (msg->operationStatus == FileOperationStatus::ServerHasNewer) {
    qDebug() << "Server has newer version of:"
             << QString::fromStdString(msg->path);
    if (msg->contents.isEmpty())
      return;
    if (!fileStorage->writeFile(username, QString::fromStdString(msg->path),
                                msg->contents)) {
      qDebug() << "Failed to write server version to client";
      return;
    }
    qDebug() << "Written server version to client:"
             << QString::fromStdString(msg->path);
    if (merkleTree) {
      merkleTree->addFile(msg->path);
    }
  }
}

void FileClient::handleDeleteResponse(SyncRequestMessage *msg) {
  if (msg->operationStatus == FileOperationStatus::Done) {
    qDebug() << "For file: " << msg->path << " received status: " << "Done";
  }
  if (msg->operationStatus == FileOperationStatus::ServerHasNewer) {
    qDebug() << "Server rejected deletion, restoring:"
             << QString::fromStdString(msg->path);
    if (msg->contents.isEmpty())
      return;
    if (!fileStorage->writeFile(username, QString::fromStdString(msg->path),
                                msg->contents)) {
      qDebug() << "Failed to restore file from server";
      return;
    }
    if (merkleTree) {
      merkleTree->addFile(msg->path);
    }
  }
}

QList<QString> FileClient::discoverNewFiles() {
  if (syncStrategy == SyncStrategy::Naive) {
    QList<QString> newFiles;
    auto files = fileStorage->listFiles(username);
    for (const auto &relativePath : files) {
      auto storedMtime = database.readMtime(relativePath);
      auto serverMtime = fileStorage->getMtime(username, relativePath);
      if (!serverMtime.has_value())
        continue;
      if (storedMtime.has_value() && storedMtime.value() == serverMtime.value())
        continue;
      database.updateFileMtime(relativePath, serverMtime.value());
      qDebug() << "Discovered new/modified file:" << relativePath
               << "mtime:" << serverMtime.value();
      newFiles.append(relativePath);
    }
    return newFiles;
  }
  return {};
}

QList<QString>
FileClient::discoverDeletedFiles(const QSet<QString> &trackedFiles) {
  if (syncStrategy == SyncStrategy::Naive) {
    auto fileList = fileStorage->listFiles(username);
    QList<QString> deletedFiles;
    auto currentFiles = QSet<QString>(fileList.begin(), fileList.end());
    for (const auto &trackedFile : trackedFiles) {
      if (!currentFiles.contains(trackedFile)) {
        qDebug() << "Discovered deleted file:" << trackedFile;
        deletedFiles.append(trackedFile);
      }
    }
    return deletedFiles;
  }
  return {};
}

void FileClient::sendNewFiles(const QList<QString> &newFiles) {
  for (const auto &relativePath : newFiles) {
    auto contents = fileStorage->readFile(username, relativePath);
    if (!contents.has_value()) {
      qDebug() << "Failed to read file:" << relativePath;
      continue;
    }
    auto mtime = fileStorage->getMtime(username, relativePath);
    SyncRequestMessage msg;
    msg.username = username;
    msg.password = password;
    msg.path = relativePath.toStdString();
    msg.contents = contents.value();
    msg.mtime = mtime.has_value()
                    ? mtime.value().toString(Qt::ISODate).toStdString()
                    : "";
    msg.operationType = FileOperationType::Write;
    msg.operationStatus = FileOperationStatus::DoIt;
    MessageProtocol::sendMessage(socket, msg);
    pendingMessages++;
  }
}

void FileClient::sendDeletedFiles(const QList<QString> &deletedFiles) {
  for (const auto &trackedPath : deletedFiles) {
    auto mtime = database.readMtime(trackedPath);
    database.removeFileMtime(trackedPath);
    SyncRequestMessage msg;
    msg.username = username;
    msg.password = password;
    msg.path = trackedPath.toStdString();
    msg.contents = {};
    msg.mtime = mtime.has_value()
                    ? mtime.value().toString(Qt::ISODate).toStdString()
                    : "";
    msg.operationType = FileOperationType::Delete;
    msg.operationStatus = FileOperationStatus::DoIt;
    MessageProtocol::sendMessage(socket, msg);
    pendingMessages++;
  }
}

void FileClient::clientTick() {
  if (currentlyDoingSyncOps)
    return;
  currentlyDoingSyncOps = true;
  qDebug() << "Client syncing stuff\n";
  if (shouldUseTimer)
    timer.stop();

  if (syncStrategy == SyncStrategy::Naive) {
    naiveTick();
  } else {
    merkleTick();
  }

  checkSyncCompletionAndUnlock();
}

void FileClient::naiveTick() {
  auto trackedFiles = database.allTrackedFiles();
  auto newFiles = discoverNewFiles();
  auto deletedFiles = discoverDeletedFiles(trackedFiles);

  qDebug() << "Tracked files: " << trackedFiles;
  qDebug() << "New files: " << newFiles;

  pendingMessages = 0;

  sendNewFiles(newFiles);
  sendDeletedFiles(deletedFiles);
}

bool FileClient::writeFile(const QString &path, const QByteArray &contents) {
  if (!fileStorage->writeFile(username, path, contents)) {
    qDebug() << "Failed to write file:" << path;
    return false;
  }
  auto mtime = fileStorage->getMtime(username, path);
  database.updateFileMtime(path, mtime.value_or(QDateTime::currentDateTime()));
  if (merkleTree) {
    merkleTree->addFile(path.toStdString());
  }
  return true;
}

void FileClient::handleMerkleSyncResponse(MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle response at client";
  QList<QPair<QString, QByteArray>> serverHashes;
  for (const auto &entry : msg->fileEntries) {
    serverHashes.append({entry.path, entry.hash});
  }

  QList<QPair<QString, QByteArray>> clientHashes;
  if (toDescend.isEmpty()) {
    for (const auto &[path, hash] : merkleTree->getHashesAtDepth(0)) {
      clientHashes.append({path, hash});
    }
  } else {
    for (const auto &path : toDescend) {
      clientHashes.append(merkleTree->getChildHashes(path));
    }
  }

  auto diff = MerkleTree::symmetricHashDiff(clientHashes, serverHashes);
  toDescend.clear();

  auto processPaths = [&](const QList<QString> &paths, auto addToResult) {
    for (const auto &path : paths) {
      auto node = merkleTree->find(path.toStdString());
      if (node.has_value() && (*node)->type == FileType::Directory) {
        toDescend.append(path);
      } else {
        addToResult(path);
      }
    }
  };

  processPaths(diff.onlyInLeft, [&](const QString &p) {
    if (!negotiationState.onlyInLeft.contains(p))
      negotiationState.onlyInLeft.append(p);
  });
  processPaths(diff.onlyInRight, [&](const QString &p) {
    if (!negotiationState.onlyInRight.contains(p))
      negotiationState.onlyInRight.append(p);
  });
  processPaths(diff.modified, [&](const QString &p) {
    if (!negotiationState.modified.contains(p))
      negotiationState.modified.append(p);
  });

  if (!toDescend.isEmpty()) {
    MerkleSyncMessage nextMsg;
    nextMsg.username = username;
    for (const auto &path : toDescend) {
      for (const auto &[childPath, childHash] :
           merkleTree->getChildHashes(path)) {
        auto mtime = fileStorage->getMtime(username, childPath);
        nextMsg.fileEntries.append(
            {childPath, childHash, mtime.value_or(QDateTime())});
      }
    }
    qDebug() << "Client sending: " << nextMsg;
    MessageProtocol::sendMessage(socket, nextMsg);
    return;
  }
  // we are out of things to descend into, means we finished negotiation
  Q_EMIT negotiationCompleted();
}

void FileClient::handleNegotiationCompleted() {
  qDebug() << "Now starting to sync files";
  currentlyNegotiatingFileDiffs = false;
  qDebug() << "Tree Diff:";
  qDebug() << "(L) " << negotiationState.onlyInLeft;
  qDebug() << "(R) " << negotiationState.onlyInRight;
  qDebug() << "(M) " << negotiationState.modified;
}

void FileClient::merkleTick() {
  qDebug() << "Merkle tick";
  if (currentlyNegotiatingFileDiffs) {
    qDebug() << "There is an existing negotiation going on";
    return;
  }
  currentlyNegotiatingFileDiffs = true;
  negotiationState = TreeDiff{};
  toDescend.clear();

  auto hashes = merkleTree->getHashesAtDepth(1);
  MerkleSyncMessage msg;
  msg.username = username;
  for (const auto &[path, hash] : hashes) {
    auto mtime = fileStorage->getMtime(username, path);
    msg.fileEntries.append({path, hash, mtime.value_or(QDateTime())});
  }
  qDebug() << "Initiating negotiation with msg: " << msg;
  MessageProtocol::sendMessage(socket, msg);
}

void FileClient::handleAuthResponse(AuthResponseMessage *msg) {
  if (msg->success) {
    qDebug() << "Auth successful";
  } else {
    qDebug() << "Auth failed";
  }
}

void FileClient::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received from client";
}
