#include "FileServer.h"
#include "Messages.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <qnamespace.h>

void FileServer::setFileStorageImpl(std::unique_ptr<FileStorage> storage) {
  this->fileStorage = std::move(storage);
}

void FileServer::listenOn(const QString &addr) {
  QLocalServer::removeServer(addr);
  server.listen(addr);
  QObject::connect(&server, &QLocalServer::newConnection, [&]() {
    qDebug() << "New connection received";
    QLocalSocket *socket = server.nextPendingConnection();
    handleConnection(socket);
  });
}

QString FileServer::serverName() { return server.serverName(); }

bool FileServer::isListening() { return server.isListening(); }

void FileServer::handleConnection(QLocalSocket *socket) {
  qDebug() << "Handling new connection.";
  QObject::connect(socket, &QLocalSocket::disconnected, socket,
                   &QLocalSocket::deleteLater);
  QObject::connect(socket, &QLocalSocket::disconnected, this,
                   [this, socket]() { buffers.remove(socket); });
  QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
    qDebug() << "Ready read event fired.";
    MessageProtocol::processBuffer(
        socket, buffers[socket], [this, socket](Message *msg) {
          if (!msg) {
            qDebug() << "Failed to deserialize message";
            return;
          }
          qDebug() << "Dispatching message to handler.";
          switch (msg->type()) {
          case MessageType::ClientAuth: {
            handleAuth(socket, static_cast<AuthMessage *>(msg));
            break;
          }
          case MessageType::SyncRequest: {
            handleSyncRequest(socket, static_cast<SyncRequestMessage *>(msg));
            break;
          }
          default: {
            handleUnrecognized(socket, msg);
            break;
          }
          }
        });
  });
}


void FileServer::handleSyncRequest(QLocalSocket *socket,
                                   SyncRequestMessage *msg) {
  qDebug() << "Handling sync request message\n";

  QString qMtime = QString::fromStdString(msg->mtime);
  QString storageKey = msg->username + "/" + QString::fromStdString(msg->path);
  auto storedMtime = database.readMtime(storageKey);

  SyncRequestMessage response;
  response.path = msg->path;
  response.contents = {};
  response.operationType = msg->operationType;

  if (msg->operationType == FileOperationType::Delete) {
    if (!storedMtime.has_value()) {
      response.operationStatus = FileOperationStatus::Done;
    } else {
      QDateTime clientMtime = QDateTime::fromString(qMtime, Qt::ISODate);
      QDateTime serverMtime = storedMtime.value();

      if (serverMtime > clientMtime) {
        auto contents = fileStorage->readFile(
            msg->username, QString::fromStdString(msg->path));
        if (!contents.has_value()) {
          qDebug() << "Failed to read file from storage";
          response.operationStatus = FileOperationStatus::Rejected;
          MessageProtocol::sendMessage(socket, response);
          return;
        }
        response.contents = contents.value();
        response.mtime = serverMtime.toString(Qt::ISODate).toStdString();
        response.operationStatus = FileOperationStatus::Rejected;
      } else {
        if (!fileStorage->deleteFile(msg->username,
                                     QString::fromStdString(msg->path))) {
          qDebug() << "Failed to delete file from storage";
          response.operationStatus = FileOperationStatus::Rejected;
        } else {
          database.removeFileMtime(storageKey);
          response.operationStatus = FileOperationStatus::Done;
        }
      }
    }
  } else {
    if (!storedMtime.has_value()) {
      database.updateFileMtime(storageKey,
                               QDateTime::fromString(qMtime, Qt::ISODate));
      if (!fileStorage->writeFile(msg->username,
                                  QString::fromStdString(msg->path),
                                  msg->contents)) {
        qDebug() << "Failed to write file to storage";
        response.operationStatus = FileOperationStatus::Rejected;
        MessageProtocol::sendMessage(socket, response);
        return;
      }
      response.operationStatus = FileOperationStatus::Done;
    } else {
      QDateTime clientMtime = QDateTime::fromString(qMtime, Qt::ISODate);
      QDateTime serverMtime = storedMtime.value();

      if (serverMtime > clientMtime) {
        auto contents = fileStorage->readFile(
            msg->username, QString::fromStdString(msg->path));
        if (!contents.has_value()) {
          qDebug() << "Failed to read file from storage";
          response.operationStatus = FileOperationStatus::Rejected;
          MessageProtocol::sendMessage(socket, response);
          return;
        }
        response.contents = contents.value();
        response.mtime = serverMtime.toString(Qt::ISODate).toStdString();
        response.operationStatus = FileOperationStatus::Rejected;
      } else {
        database.updateFileMtime(storageKey, clientMtime);
        if (!fileStorage->writeFile(msg->username,
                                    QString::fromStdString(msg->path),
                                    msg->contents)) {
          qDebug() << "Failed to write file to storage";
          response.operationStatus = FileOperationStatus::Rejected;
          MessageProtocol::sendMessage(socket, response);
          return;
        }
        response.operationStatus = FileOperationStatus::Done;
      }
    }
  }

  MessageProtocol::sendMessage(socket, response);

  // qDebug() << "Handling sync request message\n";
  // auto rootDir = database.readUserDirectory(msg->username, msg->password);
  // if (!rootDir.has_value()) {
  //   QString userDir = getUserRootDirectory(msg->username);
  //   database.storeUser(msg->username, msg->password, userDir);
  //   QDir().mkpath(userDir);
  //   rootDir = userDir;
  // }
  //
  // QString fullPath = rootDir.value() + "/" +
  // QString::fromStdString(msg->path); QString qMtime =
  // QString::fromStdString(msg->mtime); auto storedMtime =
  // database.readMtime(fullPath);
  //
  // SyncRequestMessage response;
  // response.path = msg->path;
  // response.contents = {};
  // response.operationType = msg->operationType;
  //
  // if (msg->operationType == FileOperationType::Delete) {
  //   if (!storedMtime.has_value()) {
  //     // file doesn't exist, nothing to delete
  //     response.operationStatus = FileOperationStatus::Done;
  //   } else {
  //     QDateTime clientMtime = QDateTime::fromString(qMtime, Qt::ISODate);
  //     QDateTime serverMtime = storedMtime.value();
  //
  //     if (serverMtime > clientMtime) {
  //       // server has newer version, reject deletion
  //       QFile file(fullPath);
  //       if (!file.open(QIODevice::ReadOnly)) {
  //         qDebug() << "Failed to open file for reading:" << fullPath
  //                  << file.errorString();
  //         response.operationStatus = FileOperationStatus::Rejected;
  //         socket->write(response.serialize());
  //         return;
  //       }
  //       response.contents = file.readAll();
  //       file.close();
  //       response.mtime = serverMtime.toString(Qt::ISODate).toStdString();
  //       response.operationStatus = FileOperationStatus::Rejected;
  //     } else {
  //       // client mtime is same or newer, proceed with deletion
  //       if (!QFile::remove(fullPath)) {
  //         qDebug() << "Failed to delete file:" << fullPath;
  //         response.operationStatus = FileOperationStatus::Rejected;
  //       } else {
  //         database.removeFileMtime(fullPath);
  //         response.operationStatus = FileOperationStatus::Done;
  //       }
  //     }
  //   }
  // } else {
  //   // Write operation
  //   if (!storedMtime.has_value()) {
  //     database.updateFileMtime(fullPath,
  //                              QDateTime::fromString(qMtime, Qt::ISODate));
  //     QFile file(fullPath);
  //     QFileInfo(file).dir().mkpath(".");
  //     if (!file.open(QIODevice::WriteOnly)) {
  //       qDebug() << "Failed to open file for writing:" << fullPath
  //                << file.errorString();
  //       response.operationStatus = FileOperationStatus::Rejected;
  //       socket->write(response.serialize());
  //       return;
  //     }
  //     file.write(msg->contents);
  //     file.close();
  //     response.operationStatus = FileOperationStatus::Done;
  //   } else {
  //     QDateTime clientMtime = QDateTime::fromString(qMtime, Qt::ISODate);
  //     QDateTime serverMtime = storedMtime.value();
  //
  //     if (serverMtime > clientMtime) {
  //       QFile file(fullPath);
  //       if (!file.open(QIODevice::ReadOnly)) {
  //         qDebug() << "Failed to open file for reading:" << fullPath
  //                  << file.errorString();
  //         response.operationStatus = FileOperationStatus::Rejected;
  //         socket->write(response.serialize());
  //         return;
  //       }
  //       response.contents = file.readAll();
  //       file.close();
  //       response.mtime = serverMtime.toString(Qt::ISODate).toStdString();
  //       response.operationStatus = FileOperationStatus::Rejected;
  //     } else {
  //       database.updateFileMtime(fullPath, clientMtime);
  //       QFile file(fullPath);
  //       if (!file.open(QIODevice::WriteOnly)) {
  //         qDebug() << "Failed to open file for writing:" << fullPath
  //                  << file.errorString();
  //         response.operationStatus = FileOperationStatus::Rejected;
  //         socket->write(response.serialize());
  //         return;
  //       }
  //       file.write(msg->contents);
  //       file.close();
  //       response.operationStatus = FileOperationStatus::Done;
  //     }
  //   }
  // }
  //
  // socket->write(response.serialize());
}

FileStorage* FileServer::getStorage() {
  return fileStorage.get();
}
QString FileServer::getUserRootDirectory(const QString &username) {
  auto path = serverRootDir + "/" + username;
  QDir().mkpath(path);
  return path;
}

void FileServer::handleAuth(QLocalSocket *socket, AuthMessage *msg) {
  qDebug() << "User: " << msg->username << "Password: " << msg->password;

  AuthResponseMessage response;
  response.success = true;
  socket->write(response.serialize());
}

void FileServer::handleUnrecognized(QLocalSocket *socket, Message *msg) {
  qDebug() << "Unrecognized message type received";
}
