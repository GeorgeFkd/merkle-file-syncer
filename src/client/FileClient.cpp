#include "FileClient.h"
#include "Messages.h"
#include <QCoreApplication>
#include <QDirIterator>
FileClient::FileClient() {}
void FileClient::connectToServer(const QString &serverName) {
  socket.connectToServer(serverName);
}
void FileClient::init() {
  QObject::connect(&socket, &QLocalSocket::connected, [this]() {
    QObject::connect(&socket, &QLocalSocket::readyRead, [this]() {
      auto bytes = socket.readAll();
      auto msg = Message::deserialize(bytes);
      switch (msg->type()) {
      case MessageType::ClientAuth: {
        handleAuthResponse(static_cast<AuthResponseMessage *>(msg.get()));
        break;
      }
        case MessageType::SyncRequest: {
          handleSyncResponse(static_cast<SyncRequestMessage *>(msg.get()));
          break;
        }
      default: {
        handleUnrecognized(msg.get());
        break;
      }
      }
    });
  });
  QObject::connect(&timer, &QTimer::timeout, [this]() { clientTick(); });
  timer.start(3000);
}

void FileClient::handleSyncResponse(SyncRequestMessage *msg) {
    pendingMessages--;
    if (msg->operationStatus == FileOperationStatus::Rejected) {
        // server has newer version, write it to local filesystem
        // TODO: handle rejected case
        qDebug() << "Sync rejected for:" << QString::fromStdString(msg->path);
    }
    
    if (pendingMessages == 0) {
        qDebug() << "All of current sync items have been synced.";
        Q_EMIT syncCompleted();
        timer.start(3000);
    }
}

QString FileClient::getUserRootDirectory(const QString &username) {
  auto path =
      QCoreApplication::applicationDirPath() + "/client_root/" + username;
  QDir().mkpath(path);
  return path;
}

QList<QString> FileClient::discoverNewFiles(const QString &rootDir) {
  QList<QString> newFiles;
  QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString fullPath = it.next();
    QFileInfo info(fullPath);
    QDateTime mtime = info.lastModified();
    auto storedMtime = database.readMtime(fullPath);
    if (storedMtime.has_value() && storedMtime.value() == mtime) {
      // already stored
      continue;
    }
    database.updateFileMtime(fullPath, mtime);
    qDebug() << "Discovered new/modified file:" << fullPath
             << "mtime:" << mtime;
    newFiles.append(fullPath);
  }
  return newFiles;
}

QList<QString>
FileClient::discoverDeletedFiles(const QString &rootDir,
                                 const QSet<QString> &trackedFiles) {
  QList<QString> deletedFiles;
  QSet<QString> currentFiles;
  QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    currentFiles.insert(it.next());
  }
  for (const auto &trackedFile : trackedFiles) {
    if (!currentFiles.contains(trackedFile)) {
      qDebug() << "Discovered deleted file: " << trackedFile;
      // database.removeFileMtime(trackedFile);
      // when deleting we cant remove the fileMtime here cos its sent to the server
      deletedFiles.append(trackedFile);
    }
  }

  return deletedFiles;
}

void FileClient::clientTick() {
  if (currentlyDoingSyncOps)
    return;
  currentlyDoingSyncOps = true;
  qDebug() << "Client syncing stuff" << "\n";
  timer.stop();

  QString username = "foo";
  QString password = "bar";
  QString rootDir = getUserRootDirectory(username);

  database.storeUser(username, password, rootDir);
  auto trackedFiles = database.allTrackedFiles();
  auto newFiles = discoverNewFiles(rootDir);
  for (const auto &path : newFiles) {
    QFileInfo info(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      qDebug() << "Failed to open file:" << path << file.errorString();
      continue;
    }
    SyncRequestMessage msg;
    msg.username = username;
    msg.password = password;
    msg.path = QDir(rootDir).relativeFilePath(path).toStdString();
    msg.contents = file.readAll();
    msg.mtime = info.lastModified().toString(Qt::ISODate).toStdString();
    msg.operationType = FileOperationType::Write;
    msg.operationStatus = FileOperationStatus::DoIt;
    file.close();
    MessageProtocol::sendMessage(&socket,msg);
  }

  auto deletedFiles = discoverDeletedFiles(rootDir, trackedFiles);
  for (const auto &path : deletedFiles) {
    auto mtime = database.readMtime(path);
    database.removeFileMtime(path);
    SyncRequestMessage msg;
    msg.username = username;
    msg.password = password;
    msg.path = QDir(rootDir).relativeFilePath(path).toStdString();
    msg.contents = {};
    msg.mtime = mtime.value().toString(Qt::ISODate).toStdString();
    msg.operationType = FileOperationType::Delete;
    msg.operationStatus = FileOperationStatus::DoIt;
    MessageProtocol::sendMessage(&socket,msg);
  }
  pendingMessages = newFiles.size() + deletedFiles.size();
  currentlyDoingSyncOps = false;
  timer.start(3000);
}
void FileClient::handleAuthResponse(AuthResponseMessage *msg) {
  if (msg->success) {
    qDebug() << "Auth successful, starting sync timer";
  } else {
    qDebug() << "Auth failed";
  }
}

void FileClient::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received";
}
