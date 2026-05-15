#include "FileClient.h"
#include "LocalFileStorage.h"
#include "Messages.h"
#include <QCoreApplication>
#include <QDir>
#include <qnamespace.h>

FileClient::FileClient() {
  socket = new QLocalSocket(this);
  fileStorage = std::make_unique<LocalFileStorage>();
}

LocalFileStorage *FileClient::getStorage() { return fileStorage.get(); }

void FileClient::connectToServer(const QString &serverName) {
  socket->connectToServer(serverName);
}

void FileClient::setManualTick() { shouldUseTimer = false; }

void FileClient::init() {
  QObject::connect(socket, &QLocalSocket::connected, this,
                   [this]() { qDebug() << "Connected event fired."; });
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
      default:
        handleUnrecognized(msg);
        break;
      }
    });
  });
  if (shouldUseTimer) {
    QObject::connect(&timer, &QTimer::timeout, this,
                     [this]() { clientTick(); });
    timer.start(3000);
  }
}

FileClient::~FileClient() {
  socket->blockSignals(true);
  QObject::disconnect(socket, nullptr, nullptr, nullptr);
  socket->abort();
}

void FileClient::handleSyncResponse(SyncRequestMessage *msg) {
  pendingMessages--;
  if (msg->operationStatus == FileOperationStatus::Rejected) {
    qDebug() << "Sync rejected for:" << QString::fromStdString(msg->path);
    if (msg->operationType == FileOperationType::Write &&
        !msg->contents.isEmpty()) {
      if (!fileStorage->writeFile(username, QString::fromStdString(msg->path),
                                  msg->contents)) {
        qDebug() << "Failed to write server version to client";
      } else {
        qDebug() << "Written server version to client:"
                 << QString::fromStdString(msg->path);
      }
    } else if (msg->operationType == FileOperationType::Delete) {
      qDebug() << "Server rejected deletion of:"
               << QString::fromStdString(msg->path);
    }
  }
  if (pendingMessages == 0) {
    currentlyDoingSyncOps = false;
    qDebug() << "All of current sync items have been synced.";
    Q_EMIT syncCompleted();
    if (shouldUseTimer) {
      timer.start(3000);
    }
  }
}

QString FileClient::getUserRootDirectory(const QString &username) {
  return fileStorage->rootPath(username);
}

QList<QString> FileClient::discoverNewFiles() {
  QList<QString> newFiles;
  auto files = fileStorage->listFiles(username);
  for (const auto &relativePath : files) {
    auto storedMtime = database.readMtime(relativePath);
    auto serverMtime = fileStorage->getMtime(username, relativePath);
    if (!serverMtime.has_value())
      continue;
    if (storedMtime.has_value() && storedMtime.value() == serverMtime.value())
      continue;
    database.updateFileMtime(relativePath,
                             serverMtime.value());
    qDebug() << "Discovered new/modified file:" << relativePath
             << "mtime:" << serverMtime.value();
    newFiles.append(relativePath);
  }
  return newFiles;
}

QList<QString>
FileClient::discoverDeletedFiles(const QSet<QString> &trackedFiles) {
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

void FileClient::setUsername(const QString &username) {
  this->username = username;
}

void FileClient::setPassword(const QString &password) {
  this->password = password;
}

void FileClient::setRootDir(const QString &rootDir) {
  fileStorage->setRoot(QDir(rootDir).absolutePath());
  qDebug() << "Created client root dir at:" << rootDir;
}

void FileClient::clientTick() {
  if (currentlyDoingSyncOps)
    return;
  currentlyDoingSyncOps = true;
  qDebug() << "Client syncing stuff\n";
  if (shouldUseTimer)
    timer.stop();

  database.storeUser(username, password, fileStorage->rootPath(username));
  auto trackedFiles = database.allTrackedFiles();
  auto newFiles = discoverNewFiles();
  qDebug() << "Tracked files: " << trackedFiles;
  qDebug() << "New files: " << newFiles;

  pendingMessages = 0;

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

  auto deletedFiles = discoverDeletedFiles(trackedFiles);
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

  if (pendingMessages == 0) {
    currentlyDoingSyncOps = false;
    Q_EMIT syncCompleted();
    if (shouldUseTimer)
      timer.start(3000);
  }
}

void FileClient::handleAuthResponse(AuthResponseMessage *msg) {
  if (msg->success) {
    qDebug() << "Auth successful";
  } else {
    qDebug() << "Auth failed";
  }
}

void FileClient::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received";
}
