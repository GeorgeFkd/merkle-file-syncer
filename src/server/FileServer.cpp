#include "FileServer.h"
#include "FileHasher.h"
#include "Messages.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <qnamespace.h>

void FileServer::configure(FileServerConfig config) {
  this->fileStorage = std::move(config.storage);
  serverUrl = std::move(config.serverName);
}

void FileServer::start() {
  QLocalServer::removeServer(serverUrl);
  server.listen(serverUrl);
  setupConnections();
}

void FileServer::setupConnections() {
  QObject::connect(&server, &QLocalServer::newConnection, [&]() {
    qDebug() << "New connection received";
    QLocalSocket *socket = server.nextPendingConnection();
    setupNewSocketConnection(socket);
  });

  QObject::connect(this, &FileServer::authMessageReceived, this,
                   [this](QLocalSocket *socket, AuthMessage *msg) {
                     handleAuth(socket, msg);
                   });

  QObject::connect(this, &FileServer::syncRequestReceived, this,
                   [this](QLocalSocket *socket, SyncRequestMessage *msg) {
                     handleSyncRequest(socket, msg);
                   });
  QObject::connect(this, &FileServer::unrecognizedMessageReceived, this,
                   [this](QLocalSocket *socket, Message *msg) {
                     handleUnrecognized(socket, msg);
                   });
  QObject::connect(this, &FileServer::merkleSyncRequestReceived, this,
                   [this](QLocalSocket *socket, MerkleSyncMessage *msg) {
                     handleMerkleSyncRequest(socket, msg);
                   });
}

QString FileServer::serverName() { return server.serverName(); }

bool FileServer::isListening() { return server.isListening(); }

void FileServer::setupNewSocketConnection(QLocalSocket *socket) {
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
          // TODO: emit events instead of just handler calls
          switch (msg->type()) {
          case MessageType::ClientAuth: {
            Q_EMIT handleAuth(socket, static_cast<AuthMessage *>(msg));
            break;
          }
          case MessageType::SyncRequest: {
            Q_EMIT handleSyncRequest(socket,
                                     static_cast<SyncRequestMessage *>(msg));
            break;
          }
          case MessageType::MerkleSync: {
            Q_EMIT handleMerkleSyncRequest(
                socket, static_cast<MerkleSyncMessage *>(msg));
            break;
          }
          default: {
            Q_EMIT handleUnrecognized(socket, msg);
            break;
          }
          }
        });
  });
}

// this method is only used in testing, should not be used for the server logic
// bool FileServer::writeFile(const QString &user, const QString &file,
//                            const QByteArray &contents, const QDateTime
//                            &mtime) {
//   database.updateFileMtime(user + "/" + file, mtime);
//   return fileStorage->writeFile(user, file, contents);
// }

bool FileServer::writeFile(const QString &user, const QString &file,
                           const QByteArray &contents, const QDateTime &mtime) {
  database.updateFileMtime(user + "/" + file, mtime);
  if (!fileStorage->writeFile(user, file, contents))
    return false;
  getUserTree(user)->addFile(file.toStdString());
  return true;
}

void FileServer::handleDeleteRequest(
    SyncRequestMessage *msg, SyncRequestMessage &response,
    const QString &storageKey, const std::optional<QDateTime> &storedMtime) {
  if (!storedMtime.has_value()) {
    response.operationStatus = FileOperationStatus::Done;
    return;
  }
  QDateTime clientMtime =
      QDateTime::fromString(QString::fromStdString(msg->mtime), Qt::ISODate);
  QDateTime serverMtime = storedMtime.value();
  bool serverMtimeIsAhead = serverMtime > clientMtime;
  if (serverMtimeIsAhead) {
    trySendNewerFile(response, msg->username, QString::fromStdString(msg->path),
                     serverMtime);
    return;
  }

  assert(!serverMtimeIsAhead);
  if (!fileStorage->deleteFile(msg->username,
                               QString::fromStdString(msg->path))) {
    qDebug() << "Failed to delete file from storage";
    response.operationStatus = FileOperationStatus::Error;
    return;
  } else {
    database.removeFileMtime(storageKey);
    response.operationStatus = FileOperationStatus::Done;
    return;
  }
}

void FileServer::trySendNewerFile(SyncRequestMessage &response,
                                  const QString &user, const QString &path,
                                  const QDateTime &serverMtime) {
  auto contents = fileStorage->readFile(user, path);
  if (!contents.has_value()) {
    qDebug() << "Failed to read file from storage";
    response.operationStatus = FileOperationStatus::Error;
    return;
  }
  response.contents = contents.value();
  response.mtime = serverMtime.toString(Qt::ISODate).toStdString();
  response.operationStatus = FileOperationStatus::ServerHasNewer;
  return;
}

void FileServer::handleWriteRequest(
    SyncRequestMessage *msg, SyncRequestMessage &response,
    const QString &storageKey, const std::optional<QDateTime> &storedMtime) {
  QDateTime clientMtime =
      QDateTime::fromString(QString::fromStdString(msg->mtime), Qt::ISODate);

  if (storedMtime.has_value() && storedMtime.value() > clientMtime) {
    trySendNewerFile(response, msg->username, QString::fromStdString(msg->path),
                     storedMtime.value());
    return;
  }

  if (!fileStorage->writeFile(msg->username, QString::fromStdString(msg->path),
                              msg->contents)) {
    qDebug() << "Failed to write file to storage";
    response.operationStatus = FileOperationStatus::Error;
    return;
  }

  database.updateFileMtime(storageKey, clientMtime);
  response.operationStatus = FileOperationStatus::Done;
  return;
}

MerkleTree *FileServer::getUserTree(const QString &username) {
  if (userTrees.find(username) == userTrees.end()) {
    auto tree = std::make_unique<MerkleTree>();
    tree->setHasher(FileHasher(fileStorage.get(), username));
    tree->buildFromStorage(fileStorage.get(), username);
    userTrees.emplace(username, std::move(tree));
  }
  return userTrees.at(username).get();
}

void FileServer::handleMerkleSyncRequest(QLocalSocket *socket,
                                         MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle sync message at server";
  auto serverTree = getUserTree(msg->username);

  // group client entries by parent to find siblings server has
  QSet<QString> clientPaths;
  QSet<QString> parents;
  for (const auto &entry : msg->fileEntries) {
    clientPaths.insert(entry.path);
    QString parent = entry.path.contains('/')
                         ? entry.path.left(entry.path.lastIndexOf('/'))
                         : "";
    parents.insert(parent);
  }

  MerkleSyncMessage response;
  response.username = msg->username;

  // respond with server's hash for each client path
  for (const auto &entry : msg->fileEntries) {
    auto node = serverTree->find(entry.path.toStdString());
    QByteArray hash = node.has_value() ? (*node)->hash : QByteArray{};
    FileType type = node.has_value() ? (*node)->type : entry.filetype;
    response.fileEntries.append({entry.path, hash, QDateTime{}, type});
  }

  // add paths server has at same level that client didn't send
  for (const auto &parent : parents) {
    auto siblings = parent.isEmpty() ? serverTree->getHashesAtDepth(1)
                                     : serverTree->getChildHashes(parent);
    for (const auto &[path, hash] : siblings) {
      if (!clientPaths.contains(path)) {
        auto node = serverTree->find(path.toStdString());
        FileType type = node.has_value() ? (*node)->type : FileType::File;
        response.fileEntries.append({path, hash, QDateTime{}, type});
      }
    }
  }

  qDebug() << "Sending response:" << response;
  MessageProtocol::sendMessage(socket, response);
}

void FileServer::handleSyncRequest(QLocalSocket *socket,
                                   SyncRequestMessage *msg) {
  qDebug() << "Handling sync request message";
  Q_ASSERT_X(fileStorage != nullptr, "FileServer::handleSyncRequest",
             "fileStorage is not set");

  // we need the username prefix to namespace records per user
  QString storageKey = msg->username + "/" + QString::fromStdString(msg->path);
  auto storedMtime = database.readMtime(storageKey);

  SyncRequestMessage response;
  response.path = msg->path;
  response.contents = {};
  response.operationType = msg->operationType;

  if (msg->operationType == FileOperationType::Delete) {
    handleDeleteRequest(msg, response, storageKey, storedMtime);
  } else if (msg->operationType == FileOperationType::Write) {
    handleWriteRequest(msg, response, storageKey, storedMtime);
  }

  MessageProtocol::sendMessage(socket, response);
}

FileStorage *FileServer::getStorage() { return fileStorage.get(); }

void FileServer::handleAuth(QLocalSocket *socket, AuthMessage *msg) {
  qDebug() << "User: " << msg->username << "Password: " << msg->password;

  AuthResponseMessage response;
  response.success = true;
  MessageProtocol::sendMessage(socket, response);
}

void FileServer::handleUnrecognized(QLocalSocket *socket, Message *msg) {
  qDebug() << "Unrecognized message type received from server";
}
