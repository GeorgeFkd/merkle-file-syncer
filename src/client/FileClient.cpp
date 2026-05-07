#include "FileClient.h"
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
      auto *msg = Message::deserialize(bytes);
      switch (msg->type()) {
      case MessageType::ClientAuth: {
        handleAuthResponse(static_cast<AuthResponseMessage *>(msg));
        break;
      }
      default: {
        handleUnrecognized(msg);
        break;
      }
      }
    });
  });
  QObject::connect(&timer, &QTimer::timeout, [this]() { clientTick(); });
  timer.start(3000);
}

QString FileClient::getUserRootDirectory(const QString &username) {
  auto path =
      QCoreApplication::applicationDirPath() + "/client_root/" + username;
  QDir().mkpath(path);
  return path;
}
void FileClient::clientTick() {
  if(currentlyDoingSyncOps) return;
  currentlyDoingSyncOps = true;
  qDebug() << "Client syncing stuff" << "\n";
  timer.stop();

  QString username = "foo";
  QString password = "bar";
  QString rootDir = getUserRootDirectory(username);

  database.storeUser(username, password, rootDir);
  auto trackedFiles = database.allTrackedFiles();

  QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
  // discover new files
  while (it.hasNext()) {
    QString fullPath = it.next();
    QFileInfo info(fullPath);
    QDateTime mtime = info.lastModified();

    database.updateFileMtime(fullPath, mtime);
    qDebug() << "Discovered file:" << fullPath << "mtime:" << mtime;

    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
      qDebug() << "Failed to open file:" << fullPath << file.errorString();
      continue;
    }

    SyncRequestMessage msg;
    msg.username = username;
    msg.password = password;
    msg.path = QDir(rootDir).relativeFilePath(fullPath).toStdString();
    msg.contents = file.readAll();
    msg.mtime = mtime.toString(Qt::ISODate).toStdString();
    msg.operationType = FileOperationType::Write;
    msg.operationStatus = FileOperationStatus::DoIt;

    file.close();
    socket.write(msg.serialize());
  }

  // find deleted files
  QSet<QString> currentFiles;
  QDirIterator check(rootDir, QDir::Files, QDirIterator::Subdirectories);
  while (check.hasNext()) {
    currentFiles.insert(check.next());
  }
  for (const auto &trackedFile : trackedFiles) {
    if (!currentFiles.contains(trackedFile)) {
      // file was deleted
      SyncRequestMessage msg;
      msg.username = username;
      msg.password = password;
      msg.path = QDir(rootDir).relativeFilePath(trackedFile).toStdString();
      msg.contents = {};
      msg.mtime = database.readMtime(trackedFile)
                      .value()
                      .toString(Qt::ISODate)
                      .toStdString();
      msg.operationType = FileOperationType::Delete;
      msg.operationStatus = FileOperationStatus::DoIt;
      database.removeFileMtime(trackedFile);
      socket.write(msg.serialize());
    }
  }
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
