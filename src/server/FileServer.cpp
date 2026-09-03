#include "FileServer.h"
#include "Hasher.h"
#include "LocalServerTransport.h"
#include "Messages.h"

#include "TcpServerTransport.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <memory>
#include <qnamespace.h>

void FileServer::configure(FileServerConfig config) {
  this->fileStorage = std::move(config.storage);
  // AuthServer holds raw pointers to both, so they have to outlive it and be
  // in place first. It also loads the signing keys in its constructor (fatal
  // if absent), which is why it is built here and not as a value member.
  this->usersDb =
      config.users ? std::move(config.users) : std::make_unique<UsersDb>();
  this->authServer =
      std::make_unique<AuthServer>(&sessionStore, usersDb.get(), this);
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

void FileServer::sendToClient(const ConnectionId &conn,
                              std::shared_ptr<Message> msg) {
  // QHash::key is a linear scan, so this is O(connections) per send. Fine at
  // current scale; the fix when it stops being fine is a second map.
  auto *socket = socketToConn.key(conn, nullptr);
  if (!socket) {
    qWarning() << "no socket for connection" << conn;
    return;
  }
  transport->send(socket, msg);
}

FileServer::~FileServer() {}

void FileServer::setupConnections() {
  setupSocketConnections();
  setupAuthConnections();
  setupNegotiationConnections();
  setupFileTransferConnections();
  QObject::connect(this, &FileServer::sendMessage, this,
                   [this](std::shared_ptr<Message> msg, ConnectionId conn) {
                     sendToClient(conn, msg);
                   });
}

void FileServer::setupAuthConnections() {
  QObject::connect(authServer.get(), &AuthServer::sendMessage, this,
                   [this](std::shared_ptr<Message> msg,
                          AuthServerOutMsgCtx out) {
                     sendToClient(out.connection, msg);
                   });
  QObject::connect(authServer.get(), &AuthServer::sessionEstablished, this,
                   &FileServer::bindSession);
  QObject::connect(authServer.get(), &AuthServer::sessionEnded, this,
                   [this](ConnectionId conn, QString) { unbindSession(conn); });
}

void FileServer::bindSession(ConnectionId conn, QString sessionId,
                             QString username) {
  connToSession.insert(conn, sessionId);
  qDebug() << "bound connection" << conn << "to session for" << username;
}

void FileServer::unbindSession(const ConnectionId &conn) {
  connToSession.remove(conn);
}

std::optional<QString> FileServer::authorize(const ConnectionId &conn,
                                             const QString &token) {
  auto bound = connToSession.constFind(conn);
  if (bound == connToSession.constEnd())
    return std::nullopt; // connection has not logged in
  auto presented = authServer->resolveSessionId(token);
  if (!presented || *presented != *bound)
    return std::nullopt; // absent, invalid, revoked, or another connection's
  sessionStore.touchSession(*bound);
  return *bound;
}

std::optional<QString>
FileServer::usernameFor(const ConnectionId &conn) const {
  auto sessionId = connToSession.value(conn);
  if (sessionId.isEmpty())
    return std::nullopt;
  return sessionStore.getUsername(sessionId);
}

void FileServer::setupNegotiationConnections() {
  QObject::connect(
      &merkleSyncServer, &MerkleSyncServer::messageSendRequest, this,
      [this](ConnectionId conn, std::shared_ptr<MerkleProtocolMessage> proto) {
        sendToClient(conn, toWireMessage(proto.get()));
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
        auto user = usernameFor(out.clientId);
        if (!user) {
          qDebug() << "User for connection: " << out.clientId << "not found";
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
        auto user = usernameFor(conn);
        if (!user)
          return;
        auto fileMetadata =
            pendingTransfersMetadata.take(transferMetadataKey(conn, path));
        fileTransferServer->commitUpload(conn, path);
        recordFile(*user, path, fileMetadata.second, fileMetadata.first);
      });
  // FileTransferServer self-filters by message type, so it sees the whole
  // inbound stream — but only once the connection has proven its session.
  QObject::connect(transport.get(), &ServerTransport::messageReady, this,
                   [this](QIODevice *socket, std::shared_ptr<Message> msg) {
                     ClientId conn = socketToConn.value(socket);
                     if (conn.isEmpty() || !authorize(conn, msg->token))
                       return;
                     auto user = usernameFor(conn);
                     if (!user)
                       return;
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

void FileServer::onNewConnection(QIODevice *socket) {
  // Identity is minted here, before any auth: register responses and failed
  // logins have to be routable too.
  ConnectionId conn = QUuid::createUuid().toString(QUuid::WithoutBraces);
  socketToConn.insert(socket, conn);
  qDebug() << "New connection received:" << conn;
}

QString FileServer::serverName() { return transport->endpoint(); }

bool FileServer::isListening() { return transport->isListening(); }

void FileServer::onSocketDisconnected(QIODevice *socket) {
  auto conn = socketToConn.take(socket);
  if (conn.isEmpty())
    return;
  // Sessions currently die with the connection, so the token a client holds is
  // useless after a drop and it re-logs in on reconnect.
  auto sessionId = connToSession.take(conn);
  if (!sessionId.isEmpty())
    sessionStore.revokeSession(sessionId);
}

void FileServer::dispatch(QIODevice *socket, std::shared_ptr<Message> msg) {
  if (!msg) {
    qDebug() << "Failed to deserialize message";
    return;
  }
  qDebug() << "Dispatching server message to handler." << (int)msg->type();
  const ConnectionId conn = socketToConn.value(socket);
  if (conn.isEmpty()) {
    qWarning() << "message from a socket with no connection id; dropping";
    return;
  }

  switch (msg->type()) {
  case MessageType::Register:
  case MessageType::ClientAuth:
  case MessageType::Logout:
  case MessageType::DeleteAccount:
    authServer->onMessage(msg, AuthServerInMsgCtx{conn, msg->token});
    return;
  default:
    break;
  }

  // Everything past this point needs a session. Rejecting is deliberate: this
  // used to be an assert in getUserFrom, which a client could trip remotely.
  if (!authorize(conn, msg->token)) {
    qWarning() << "unauthenticated message of type" << (int)msg->type()
               << "on connection" << conn << "- dropping";
    return;
  }

  switch (msg->type()) {
  case MessageType::DeleteRequest: {
    auto resp = handleDeleteRequest(
        std::static_pointer_cast<DeleteRequestMessage>(msg), conn);
    transport->send(socket, resp);
    break;
  }
  case MessageType::MerkleSync: {
    handleMerkleSyncRequest(std::static_pointer_cast<MerkleSyncMessage>(msg),
                            conn);
    break;
  }
  case MessageType::ListRequest: {
    auto actualMsg = std::static_pointer_cast<ListRequestMessage>(msg);
    handleListRequest(actualMsg, conn);
    break;
  }
  case MessageType::RequestChunkSizeUpload: {
    auto *m = static_cast<RequestChunkSizeForUpload *>(msg.get());
    storeUploadMetadata(m, conn);
    break;
  }
  default: {
    handleUnrecognized(msg.get());
    break;
  }
  }
}

void FileServer::storeUploadMetadata(RequestChunkSizeForUpload *msg,
                                    const ConnectionId &conn) {
  QPair<QByteArray, QDateTime> metadata = {msg->hash, msg->mtime};
  pendingTransfersMetadata.insert(transferMetadataKey(conn, msg->path),
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

std::shared_ptr<DeleteRequestMessage>
FileServer::handleDeleteRequest(std::shared_ptr<DeleteRequestMessage> msg,
                                const ConnectionId &conn) {
  auto response = std::make_shared<DeleteRequestMessage>();
  response->path = msg->path;

  auto username = usernameFor(conn).value();
  auto storedMtime = database.readMtime(username, msg->path);

  qDebug() << "Delete request for user:" << username << "at device:"
           << sessionStore.getDeviceName(connToSession.value(conn))
                  .value_or(QStringLiteral("<unknown>"));

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
    std::shared_ptr<MerkleSyncMessage> msg, const ConnectionId &conn) {
  qDebug() << "Handling merkle sync message at server";
  auto username = usernameFor(conn).value();

  auto serverTree = getUserTree(username);
  merkleSyncServer.onMessage(toProtocolMessage(msg.get()), serverTree, conn);
}

void FileServer::handleListRequest(std::shared_ptr<ListRequestMessage> msg,
                                  const ConnectionId &conn) {
  auto username = usernameFor(conn).value();
  auto response = std::make_shared<ListResponseMessage>();
  if (!msg->useMerkle) {
    naiveSyncServer.onMessage(msg, conn, &database, username);
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

  Q_EMIT(sendMessage(response, conn));
  return;
}

FileStorage *FileServer::getStorage() { return fileStorage.get(); }

void FileServer::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received from server";
}
