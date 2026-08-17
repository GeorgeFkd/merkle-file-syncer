#include "FileServer.h"
#include "Hasher.h"
#include "LocalServerTransport.h"
#include "Messages.h"

#include "TcpServerTransport.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <memory>
#include <qnamespace.h>

void FileServer::configure(FileServerConfig config) {
  this->fileStorage = std::move(config.storage);
  switch (config.protocol) {
  case TransportProtocol::LocalSocket:
    transport = std::make_unique<LocalServerTransport>();
    break;
  case TransportProtocol::Tcp:
    transport = std::make_unique<TcpServerTransport>();
    break;
  }
  transport->configure(config.serverName);
  fileTransferServer = std::make_unique<FileTransferServer>(fileStorage.get());
}

void FileServer::start() {
  setupConnections();
  transport->start();
}

void FileServer::sendToClient(const QString &token,
                              std::shared_ptr<Message> msg) {
  auto *socket = getSocketFromToken(token);
  if (!socket) {
    qWarning() << "no socket for token" << token;
    return;
  }
  transport->send(socket, msg);
}

FileServer::~FileServer() {}

QIODevice *FileServer::getSocketFromToken(const QString &token) {
  auto socket = socketToTokenMap.key(token, nullptr);
  if (!socket) {
    qDebug() << "could not find socket of token: " << token;
    return nullptr;
  }
  return socket;
}

void FileServer::setupConnections() {
  setupSocketConnections();
  setupNegotiationConnections();
  setupFileTransferConnections();
  QObject::connect(
      this, &FileServer::sendMessage, this,
      [this](std::shared_ptr<Message> msg) { sendToClient(msg->token, msg); });
}

void FileServer::setupNegotiationConnections() {
  QObject::connect(
      &merkleSyncServer, &MerkleSyncServer::messageSendRequest, this,
      [this](ConnectionId conn, std::shared_ptr<MerkleProtocolMessage> proto) {
        auto wire = toWireMessage(proto.get());
        wire->token = conn;
        sendToClient(conn, wire);
      });
  QObject::connect(&naiveSyncServer, &NaiveSyncServer::sendMessage, this,
                   [this](std::shared_ptr<Message> msg, ConnectionId conn) {
                     sendToClient(conn, msg);
                   });
}

void FileServer::setupFileTransferConnections() {
  QObject::connect(
      fileTransferServer.get(), &FileTransferServer::sendMessage, this,
      [this](std::shared_ptr<Message> msg, FileTransferServerOutMsgCtx out) {
        auto user = getUsernameFromToken(out.clientId);
        if (!user) {
          qDebug() << "User for token: " << out.clientId << "not found";
          return;
        }
        if (msg->type() == MessageType::SpecifyChunkSizeDownload) {
          auto *m = static_cast<SpecifyChunkSizeDownload *>(msg.get());
          fillDownloadMetadata(m, user.value());
        }
        sendToClient(out.clientId, msg);
      });

  QObject::connect(
      fileTransferServer.get(), &FileTransferServer::uploadCompleted, this,
      [this](ClientId conn, QString path) {
        auto user = getUsernameFromToken(conn); // conn is the token
        if (!user)
          return;
        auto fileMetadata =
            pendingTransfersMetadata.take(transferMetadataKey(conn, path));
        recordFile(*user, path, fileMetadata.second, fileMetadata.first);
      });
  QObject::connect(transport.get(), &ServerTransport::messageReady, this,
                   [this](QIODevice *socket, std::shared_ptr<Message> msg) {
                     ClientId conn = socketToTokenMap.value(socket);
                     auto user = getUsernameFromToken(conn);
                     if (!user) {
                       qDebug() << "Could not find user from token: " << conn;
                       return;
                     }
                     FileTransferServerInMsgCtx ctx{conn, user.value()};
                     fileTransferServer->onMessage(msg, ctx);
                     return;
                   });
  // cleanup on fail/cancel
  QObject::connect(fileTransferServer.get(), &FileTransferServer::uploadFailed,
                   this, [this](ClientId conn, QString path) {
                     pendingTransfersMetadata.remove(
                         transferMetadataKey(conn, path));
                   });
  QObject::connect(
      fileTransferServer.get(), &FileTransferServer::uploadCancelled, this,
      [this](ClientId conn, QString path) {
        pendingTransfersMetadata.remove(transferMetadataKey(conn, path));
      });
}

void FileServer::fillDownloadMetadata(SpecifyChunkSizeDownload *msg,
                                      const QString &user) {
  auto mtime = database.readMtime(user, msg->path);
  auto hash = database.readHash(user, msg->path);
  assert(mtime.has_value() && hash.has_value() &&
         "Server should have hash and mtime when sending "
         "SpecifyChunkSizeDownload");
  if (mtime)
    msg->mtime = *mtime; // server HAS these now
  if (hash)
    msg->hash = *hash;
}

void FileServer::setupSocketConnections() {
  QObject::connect(transport.get(), &ServerTransport::newConnection, this,
                   &FileServer::onNewConnection);

  QObject::connect(transport.get(), &ServerTransport::messageReady, this,
                   &FileServer::dispatch);
  QObject::connect(transport.get(), &ServerTransport::disconnected, this,
                   &FileServer::onSocketDisconnected);
}

void FileServer::onNewConnection() { qDebug() << "New connection received"; }

QString FileServer::serverName() { return transport->endpoint(); }

bool FileServer::isListening() { return transport->isListening(); }

void FileServer::onSocketDisconnected(QIODevice *socket) {
  auto it = socketToTokenMap.find(socket);
  if (it != socketToTokenMap.end()) {
    sessionStore.revokeSession(it.value());
    socketToTokenMap.erase(it);
  }
}

void FileServer::dispatch(QIODevice *socket, std::shared_ptr<Message> msg) {
  if (!msg) {
    qDebug() << "Failed to deserialize message";
    return;
  }
  qDebug() << "Dispatching server message to handler." << (int)msg->type();
  switch (msg->type()) {
  case MessageType::ClientAuth: {
    auto resp = handleAuth(std::static_pointer_cast<AuthMessage>(msg));
    if (resp->success) {
      socketToTokenMap.insert(socket, resp->token);
      qDebug() << "Inserted token " << resp->token
               << " in store and created session";
    }
    transport->send(socket, resp);
    break;
  }
  case MessageType::DeleteRequest: {
    auto resp = handleDeleteRequest(
        std::static_pointer_cast<DeleteRequestMessage>(msg));
    transport->send(socket, resp);
    break;
  }
  case MessageType::MerkleSync: {
    handleMerkleSyncRequest(std::static_pointer_cast<MerkleSyncMessage>(msg));
    break;
  }
  case MessageType::ListRequest: {
    auto actualMsg = std::static_pointer_cast<ListRequestMessage>(msg);
    handleListRequest(actualMsg);
    break;
  }
  case MessageType::RequestChunkSizeUpload: {
    auto *m = static_cast<RequestChunkSizeForUpload *>(msg.get());
    storeUploadMetadata(m);
    break;
  }
  default: {
    handleUnrecognized(msg.get());
    break;
  }
  }
}

void FileServer::storeUploadMetadata(RequestChunkSizeForUpload *msg) {
  QPair<QByteArray, QDateTime> metadata = {msg->hash, msg->mtime};
  pendingTransfersMetadata.insert(transferMetadataKey(msg->token, msg->path),
                                  metadata);
  qDebug() << "Inserted at: " << msg->path << "  " << metadata.second << "  "
           << metadata.first;
}

bool FileServer::writeFile(const QString &user, const QString &file,
                           const QByteArray &contents, const QDateTime &mtime) {
  if (!fileStorage->writeFile(user, file, contents)) {
    qDebug() << "writeFile: storage write failed for" << file;
    return false;
  }
  recordFile(user, file, mtime, hashContents(contents));
  return true;
}

QString FileServer::getUserFrom(Message *msg) {
  auto username = getUsernameFromToken(msg->token);
  Q_ASSERT_X(username.has_value(), "getUserFrom",
             "token must resolve to a user for all non-auth handlers");
  return username.value();
}

std::shared_ptr<DeleteRequestMessage>
FileServer::handleDeleteRequest(std::shared_ptr<DeleteRequestMessage> msg) {
  auto response = std::make_shared<DeleteRequestMessage>();
  response->path = msg->path;

  auto username = getUserFrom(msg.get());
  auto storedMtime = database.readMtime(username, msg->path);

  qDebug() << "Delete request for user:" << username
           << "at device:" << sessionStore.getDeviceName(msg->token).value();

  if (!storedMtime.has_value()) {
    qDebug() << "handleDeleteRequest: no stored mtime, marking Done";
    response->operationStatus = FileOperationStatus::Done;
    return response;
  }

  if (!fileStorage->deleteFile(username, msg->path)) {
    qDebug() << "handleDeleteRequest: failed to delete file from storage";
    response->operationStatus = FileOperationStatus::Error;
    return response;
  }

  QDateTime clientDeletedAt = msg->operationTime;
  recordDeletion(username, msg->path, clientDeletedAt);
  response->operationStatus = FileOperationStatus::Done;
  return response;
}

QByteArray FileServer::hashContents(const QByteArray &contents) {
  return Hasher::hash(contents);
}

void FileServer::recordFile(const QString &username, const QString &path,
                            const QDateTime &mtime, const QByteArray &hash) {
  database.recordFile(username, path, mtime, hash);
}

void FileServer::recordDeletion(const QString &username, const QString &path,
                                const QDateTime &deletedAt) {
  database.recordDeletion(username, path, deletedAt);
}

MerkleTree *FileServer::getUserTree(const QString &username) {
  return database.getUserTree(username);
}

void FileServer::handleMerkleSyncRequest(
    std::shared_ptr<MerkleSyncMessage> msg) {
  qDebug() << "Handling merkle sync message at server";
  auto username = getUserFrom(msg.get());

  auto serverTree = getUserTree(username);
  merkleSyncServer.onMessage(toProtocolMessage(msg.get()), serverTree,
                             msg->token);
}

void FileServer::handleListRequest(std::shared_ptr<ListRequestMessage> msg) {
  auto username = getUserFrom(msg.get());
  auto response = std::make_shared<ListResponseMessage>();
  response->token = msg->token;
  if (!msg->useMerkle) {
    naiveSyncServer.onMessage(msg, msg->token, &database, username);
    return;
  }

  bool clientWantsEverything = msg->directory.isEmpty();
  auto files = database.allTrackedFiles(username);
  for (const auto &path : files) {
    bool pathIsInRequestedDir = path.startsWith(msg->directory + "/");
    if (!clientWantsEverything && !pathIsInRequestedDir) {
      continue;
    }
    auto mtime = database.readMtime(username, path);
    assert(mtime.has_value() && "File in storage must have a DB mtime entry");
    response->entries.append({path, mtime.value(), false});
  }

  auto tombstones = database.allTombstones(username);
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    const QString &path = it.key();
    bool pathIsInRequestedDir = path.startsWith(msg->directory + "/");
    if (!clientWantsEverything && !pathIsInRequestedDir) {
      continue;
    }
    response->entries.append({path, it.value(), true});
  }

  Q_EMIT(sendMessage(response));
  return;
}

FileStorage *FileServer::getStorage() { return fileStorage.get(); }

std::shared_ptr<AuthResponseMessage>
FileServer::handleAuth(std::shared_ptr<AuthMessage> msg) {
  qDebug() << "User: " << msg->username << "Password: " << msg->password;
  auto response = std::make_shared<AuthResponseMessage>();

  if (msg->username.isEmpty() || msg->deviceName.isEmpty()) {
    qDebug() << "handleAuth: username or deviceName empty";
    response->success = false;
    response->error = "username and deviceName required";
    return response;
  }

  if (sessionStore.hasSession(msg->username, msg->deviceName)) {
    qDebug() << "handleAuth: device already connected for" << msg->username
             << msg->deviceName;
    response->success = false;
    response->error = "device already connected";
    return response;
  }

  if (!usersDb.userExists(msg->username, msg->password)) {
    qDebug() << "handleAuth: invalid credentials for" << msg->username;
    response->success = false;
    response->error = "invalid credentials";
    return response;
  }

  QString token = sessionStore.createSession(msg->username, msg->deviceName);
  response->success = true;
  response->token = token;
  return response;
}

// TODO: Add proper backend for credentials handling
bool FileServer::verifyUserCredentials(const QString &username,
                                       const QString &password) {
  return true;
}

std::optional<Session> FileServer::resolveSession(const QString &token) {
  return sessionStore.getSession(token);
}

std::optional<QString> FileServer::getUsernameFromToken(const QString &token) {
  return sessionStore.getUsername(token);
}

void FileServer::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received from server";
}
