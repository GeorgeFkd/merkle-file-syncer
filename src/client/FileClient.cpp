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

NegotiationState *FileClient::getNegotiationState() {
  return &negotiationState;
}

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
  QObject::connect(socket, &QLocalSocket::connected, this, [this]() {
    qDebug() << "Connected event fired.";
    state = ClientState::Authenticating;
    sendAuthRequest();
  });
  QObject::connect(socket,&QLocalSocket::disconnected,this,[this](){
    state = ClientState::Disconnected;
    token.clear();
  });

  QObject::connect(this, &FileClient::authenticated, this, [this]() {
    if (pendingTick) {
      pendingTick = false;
      clientTick();
    }
  });

  QObject::connect(this, &FileClient::negotiationCompleted, this,
                   &FileClient::handleNegotiationCompleted);
  QObject::connect(socket, &QLocalSocket::readyRead, this, [this]() {
    MessageProtocol::processBuffer(socket, buffer, [this](Message *msg) {
      if (!msg) {
        qDebug() << "Failed to deserialize message";
        return;
      }
      switch (msg->type()) {
      case MessageType::ServerAuthResponse:
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
    if (!writeFile(username, QString::fromStdString(msg->path),
                   msg->contents)) {
      qDebug() << "Failed to write server version to client";
      return;
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
    if (!writeFile(username, QString::fromStdString(msg->path),
                   msg->contents)) {
      qDebug() << "Failed to restore file from server";
      return;
    }
  }
}

QList<QString> FileClient::discoverNewFiles() {
  if (syncStrategy == SyncStrategy::Naive) {
    QList<QString> newFiles;
    auto files = fileStorage->listFiles(username);
    for (const auto &relativePath : files) {
      auto storedMtime = database.readMtime(relativePath);
      auto localFileMtime = fileStorage->getMtime(username, relativePath);
      if (!localFileMtime.has_value())
        continue;
      if (storedMtime.has_value() &&
          storedMtime.value() == localFileMtime.value())
        continue;
      database.updateFileMtime(relativePath, localFileMtime.value());
      qDebug() << "Discovered new/modified file:" << relativePath
               << "mtime:" << localFileMtime.value();
      newFiles.append(relativePath);
    }
    return newFiles;
  }
  return {};
}

QList<QString> FileClient::discoverDeletedFiles() {
  auto trackedFiles = database.allTrackedFiles();
  qDebug() << "Tracked files: " << trackedFiles;
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
    // msg.username = username;
    // msg.password = password;
    qDebug() << "Token of client is: " << token;
    msg.token = token;
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
    // msg.username = username;
    // msg.password = password;
    qDebug() << "Token of client is: " << token;
    msg.token = token;
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
  if(state != ClientState::Authenticated){
    qDebug() << "Client is not yet authenticated";
    pendingTick = true;
    return;
  }
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

  auto newFiles = discoverNewFiles();
  auto deletedFiles = discoverDeletedFiles();

  qDebug() << "New files: " << newFiles;

  pendingMessages = 0;

  sendNewFiles(newFiles);
  sendDeletedFiles(deletedFiles);
}

bool FileClient::writeFile(const QString &user, const QString &path,
                           const QByteArray &contents) {
  assert(user == username && "WriteFile for client should pass the username "
                             "that it was configured with");
  if (!fileStorage->writeFile(username, path, contents)) {
    qDebug() << "Failed to write file on client:" << path;
    return false;
  }
  // auto mtime = fileStorage->getMtime(username, path);
  // we should not update the database, let the client discover it.
  // database.updateFileMtime(path,
  // mtime.value_or(QDateTime::currentDateTime()));
  if (merkleTree) {
    merkleTree->addFile(path.toStdString());
  }
  return true;
}

bool FileClient::deleteFile(const QString &user, const QString &path) {
  assert(user == username && "DeleteFile for client should pass the username "
                             "that it was configured with");
  if (!fileStorage->deleteFile(username, path)) {
    qDebug() << "Failed to delete file on client: " << path;
    return false;
  }
  // we only remove the entry when it has been deleted in the server
  // also note: tombstones for when we get to multidevice setups
  // database.removeFileMtime(path);
  if (merkleTree) {
    merkleTree->deleteFile(path.toStdString());
  }

  return true;
}

void FileClient::handleMerkleSyncResponse(MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle response at client";
  assert(msg->phase != 0 && "Client always initiates the negotiation server "
                            "should respond with phase 1 or 2.");
  if (msg->phase == 2) {
    // the server sets phase two when the file entries are empty so we dont need
    // to do anymore work
    qDebug() << "The negotiation is now complete";
    Q_EMIT negotiationCompleted();
    return;
  }
  qDebug() << "message from server to client: " << &msg;
  for (const auto &[parentPath, fileEntries] : msg->fileEntriesPerChild) {
    QList<QPair<QString, QByteArray>> clientHashesOfNode;

    if (parentPath.isEmpty()) {
      // root level — use depth 1
      for (const auto &[path, hash] : merkleTree->getHashesAtDepth(1)) {
        clientHashesOfNode.append({path, hash});
      }
    } else {
      auto parent = merkleTree->find(parentPath.toStdString());
      assert(
          parent.has_value() &&
          "The server should not send a parentPath back that the client does "
          "not have already, cause the client should not ask for nodes he "
          "doesnt have, it just notes them for the sync stage");
      clientHashesOfNode = merkleTree->getChildHashes(parentPath);
      qDebug() << "Parent path is: " << parent.value()->path;
    }

    QList<QPair<QString, QByteArray>> serverHashesOfNode;
    for (const auto &entry : fileEntries) {
      serverHashesOfNode.append({entry.path, entry.hash});
    }

    auto diff =
        MerkleTree::symmetricHashDiff(clientHashesOfNode, serverHashesOfNode);
    for (const auto &entry : diff.onlyInLeft) {
      auto node = merkleTree->find(entry.toStdString());
      assert(node.has_value());
      negotiationState.diffEntries.onlyInLeft.append(
          {node.value()->type == FileType::File, entry});
    }

    for (const auto &entry : diff.onlyInRight) {
      // just look up the entry in the server sent msg
      FileType type = FileType::File;
      for (const auto &serverEntry : fileEntries) {
        if (serverEntry.path == entry) {
          type = serverEntry.filetype;
          break;
        }
      }
      negotiationState.diffEntries.onlyInRight.append(
          {type == FileType::File, entry});
    }

    for (const auto &entry : diff.modified) {
      auto node = merkleTree->find(entry.toStdString());
      assert(node.has_value());
      if (node.value()->type == FileType::Directory) {
        negotiationState.directoriesToCheckWithServer.append(entry);
      } else {
        negotiationState.diffEntries.modified.append({true, entry});
      }
    }
  }

  if (!negotiationState.directoriesToCheckWithServer.isEmpty()) {
    MerkleSyncMessage nextMsg;
    nextMsg.token = token;
    nextMsg.phase = 1;
    nextMsg.depth = msg->depth + 1;

    for (const auto &dirPath : negotiationState.directoriesToCheckWithServer) {
      nextMsg.fileEntriesPerChild.append({dirPath, {}});
    }

    negotiationState.directoriesToCheckWithServer.clear();
    MessageProtocol::sendMessage(socket, nextMsg);
    return;
  }
  Q_EMIT negotiationCompleted();
}

void FileClient::handleNegotiationCompleted() {
  qDebug() << "Now starting to sync files";
  currentlyNegotiatingFileDiffs = false;
  qDebug() << "Tree Diff:";
  qDebug() << "(L) " << negotiationState.diffEntries.onlyInLeft;
  qDebug() << "(R) " << negotiationState.diffEntries.onlyInRight;
  qDebug() << "(M) " << negotiationState.diffEntries.modified;
}

void FileClient::merkleTick() {
  qDebug() << "Merkle tick";
  if (currentlyNegotiatingFileDiffs) {
    qDebug() << "There is an existing negotiation going on";
    return;
  }
  currentlyNegotiatingFileDiffs = true;
  negotiationState = NegotiationState{};
  // toDescend.clear();
  MerkleSyncMessage msg;
  msg.token = token;
  msg.phase = 0;
  msg.rootHash = merkleTree->rootHash();
  msg.depth = 0;
  qDebug() << "Initiating negotiation with msg: " << msg;
  MessageProtocol::sendMessage(socket, msg);
}

QString FileClient::getDeviceName(){
  return "placeholderdevicename";
}

void FileClient::sendAuthRequest(){
  AuthMessage msg;
  msg.username = username;
  msg.password = password;
  msg.deviceName = getDeviceName();
  MessageProtocol::sendMessage(socket, msg);
}

void FileClient::handleAuthResponse(AuthResponseMessage *msg) {
  if (msg->success) {
    qDebug() << "Auth successful";
    assert(!msg->token.isEmpty() &&
           "On auth success the token field should be populated");
    token = msg->token;
    qDebug() << "User's token is: " << token;
    state = ClientState::Authenticated;
    Q_EMIT authenticated();
  } else {
    qDebug() << "Auth failed: " << msg->error;
    state = ClientState::Disconnected;
    socket->disconnectFromServer();

  }
}

void FileClient::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received from client";
}
