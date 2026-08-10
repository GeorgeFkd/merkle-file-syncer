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
}

void FileServer::start() {
  setupConnections();
  transport->start();
}

FileServer::~FileServer() {}

void FileServer::setupConnections() {
  setupSocketConnections();

  QObject::connect(
      this, &FileServer::sendMessage, this, [this](const Message &msg) {
        auto socket = socketToTokenMap.key(msg.token, nullptr);
        if (!socket) {
          qDebug() << "generic message send request could not be sent.";
          return;
        }
        transport->send(socket, msg);
      });

  QObject::connect(&merkleSyncServer, &MerkleSyncServer::messageSendRequest,
                   this,
                   [this](ConnectionId conn, MerkleProtocolMessage proto) {
                     auto socket = socketToTokenMap.key(conn, nullptr);
                     if (!socket) {
                       qDebug() << "messageSend request could not be sent.";
                       return;
                     }
                     auto wire = toWireMessage(proto);
                     wire.token = conn;
                     transport->send(socket, wire);
                   });
  QObject::connect(&naiveSyncServer, &NaiveSyncServer::sendMessage, this,
                   [this](ListResponseMessage msg, ConnectionId conn) {
                     auto socket = socketToTokenMap.key(conn, nullptr);
                     if (!socket) {
                       qDebug()
                           << "could not send message of Naive Sync Server";
                       return;
                     }
                     transport->send(socket, msg);
                   });
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
  buffers.remove(socket);
}

void FileServer::dispatch(QIODevice *socket, Message *msg) {
  if (!msg) {
    qDebug() << "Failed to deserialize message";
    return;
  }
  qDebug() << "Dispatching message to handler.";
  switch (msg->type()) {
  case MessageType::ClientAuth: {
    auto resp = handleAuth(static_cast<AuthMessage *>(msg));
    if (resp.success) {
      socketToTokenMap.insert(socket, resp.token);
      qDebug() << "Inserted token " << resp.token
               << " in store and created session";
    }
    transport->send(socket, resp);
    break;
  }
  case MessageType::SyncRequest: {
    auto resp = handleSyncRequest(static_cast<SyncRequestMessage *>(msg));
    transport->send(socket, resp);
    break;
  }
  case MessageType::MerkleSync: {
    handleMerkleSyncRequest(static_cast<MerkleSyncMessage *>(msg));
    break;
  }
  case MessageType::ListRequest: {
    auto actualMsg = static_cast<ListRequestMessage *>(msg);
    handleListRequest(actualMsg);
    break;
  }
  case MessageType::ChunkTransfer: {
    auto resp = handleChunkUpload(static_cast<ChunkTransferMessage *>(msg));
    transport->send(socket, resp);
    break;
  }
  case MessageType::AckChunk: {
    handleAckChunk(static_cast<AckChunkMessage *>(msg));
    break;
  }
  default: {
    handleUnrecognized(msg);
    break;
  }
  }
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
  auto username = getUsername(msg->token);
  Q_ASSERT_X(username.has_value(), "getUserFrom",
             "token must resolve to a user for all non-auth handlers");
  return username.value();
}

SyncRequestMessage
FileServer::handleDeleteRequest(SyncRequestMessage *msg, const QString &path,
                                const std::optional<QDateTime> &storedMtime) {
  SyncRequestMessage response;
  response.path = msg->path;
  response.operationType = FileOperationType::Delete;

  auto username = getUserFrom(msg);
  qDebug() << "Delete request for user:" << username
           << "at device:" << sessionStore.getDeviceName(msg->token).value();

  if (!storedMtime.has_value()) {
    qDebug() << "handleDeleteRequest: no stored mtime, marking Done";
    response.operationStatus = FileOperationStatus::Done;
    return response;
  }

  QDateTime clientMtime = msg->operationTime;
  QDateTime serverMtime = storedMtime.value();
  bool serverHasNewer = serverMtime > clientMtime;
  if (serverHasNewer) {
    // assert(false && "server has newer was not reached previously"); // this
    // path is not exercised at all we never send deletes it rejects, probs
    // should write a test.
    qDebug() << "handleDeleteRequest: server mtime ahead, sending newer file";
    return trySendNewerFile(username, msg->path, serverMtime,
                            FileOperationType::Delete);
  }

  if (!fileStorage->deleteFile(username, msg->path)) {
    qDebug() << "handleDeleteRequest: failed to delete file from storage";
    response.operationStatus = FileOperationStatus::Error;
    return response;
  }

  QDateTime clientDeletedAt = msg->operationTime;
  recordDeletion(username, path, clientDeletedAt);
  response.operationStatus = FileOperationStatus::Done;
  return response;
}

SyncRequestMessage FileServer::trySendNewerFile(const QString &username,
                                                const QString &path,
                                                const QDateTime &serverMtime,
                                                FileOperationType origOp) {
  SyncRequestMessage response;
  response.path = path;
  response.operationType = origOp;
  auto contents = fileStorage->readFile(username, path);
  if (!contents.has_value()) {
    qDebug() << "trySendNewerFile: failed to read file from storage" << path;
    response.operationStatus = FileOperationStatus::Error;
    return response;
  }
  response.contents = contents.value();
  response.operationTime = serverMtime;
  response.operationStatus = FileOperationStatus::ServerHasNewer;
  return response;
}

SyncRequestMessage
FileServer::handleWriteRequest(SyncRequestMessage *msg, const QString &path,
                               const std::optional<QDateTime> &storedMtime) {
  SyncRequestMessage response;
  response.path = msg->path;
  response.operationType = FileOperationType::Write;

  auto username = getUserFrom(msg);

  QDateTime clientMtime = msg->operationTime;

  auto tombstone = database.deletedAt(username, msg->path);
  bool serverTombstoneIsLatest =
      tombstone.has_value() && tombstone.value() > clientMtime;
  if (serverTombstoneIsLatest) {
    qDebug() << "handleWriteRequest: tombstone newer than write, rejecting"
             << msg->path;
    // the file was deleted more recently than this write -> deletion wins.
    // tell the client to delete it (mirror of ServerHasNewer for writes).
    response.operationType = FileOperationType::Delete;
    response.operationStatus = FileOperationStatus::ServerHasNewer;
    response.operationTime = tombstone.value();
    return response;
  }

  auto serverTimeIsLatest =
      storedMtime.has_value() && (storedMtime.value() > clientMtime);
  if (serverTimeIsLatest) {
    // this path is still taken, so we are sending to the server writes that it
    // rejects
    qDebug() << "handleWriteRequest: server mtime ahead, sending newer file";
    return trySendNewerFile(username, msg->path, storedMtime.value(),
                            FileOperationType::Write);
  }

  if (!fileStorage->writeFile(username, msg->path, msg->contents)) {
    qDebug() << "handleWriteRequest: failed to write file to storage";
    response.operationStatus = FileOperationStatus::Error;
    return response;
  }
  qDebug() << "Updating db mtime: " << username << "-> " << path
           << " at: " << clientMtime;
  recordFile(username, path, clientMtime, hashContents(msg->contents));
  response.operationStatus = FileOperationStatus::Done;
  return response;
}
QByteArray FileServer::hashContents(const QByteArray &contents) {
  // QByteArray hash =
  //     QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
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

void FileServer::handleMerkleSyncRequest(MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle sync message at server";
  auto username = getUserFrom(msg);

  auto serverTree = getUserTree(username);
  merkleSyncServer.handleRequest(toProtocolMessage(*msg), serverTree,
                                 msg->token);
}

void FileServer::handleListRequest(ListRequestMessage *msg) {
  auto username = getUserFrom(msg);
  ListResponseMessage response;
  response.token = msg->token;
  if (!msg->useMerkle) {
    naiveSyncServer.handleRequest(msg, msg->token, &database, username);
    return ;
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
    response.entries.append({path, mtime.value(), false});
  }

  auto tombstones = database.allTombstones(username);
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    const QString &path = it.key();
    bool pathIsInRequestedDir = path.startsWith(msg->directory + "/");
    if (!clientWantsEverything && !pathIsInRequestedDir) {
      continue;
    }
    response.entries.append({path, it.value(), true});
  }

  Q_EMIT(sendMessage(response));
  return;
}

SyncRequestMessage FileServer::handleSyncRequest(SyncRequestMessage *msg) {
  qDebug() << "Handling sync request message";
  Q_ASSERT_X(fileStorage != nullptr, "FileServer::handleSyncRequest",
             "fileStorage is not set");

  auto username = getUserFrom(msg);

  auto storedMtime = database.readMtime(username, msg->path);

  if (msg->operationType == FileOperationType::Delete) {
    qDebug() << "Delete request from device: "
             << sessionStore.getDeviceName(msg->token)
             << " receives the following msg: ";
    qDebug() << *msg;
    return handleDeleteRequest(msg, msg->path, storedMtime);
  } else if (msg->operationType == FileOperationType::Write) {
    qDebug() << "Write request from device: "
             << sessionStore.getDeviceName(msg->token)
             << " receives the following msg: ";
    qDebug() << *msg;

    return handleWriteRequest(msg, msg->path, storedMtime);
  }
  assert(false);
}

void FileServer::handleAckChunk(AckChunkMessage *msg) {
  qDebug() << "received ack chunk message on server";
}

AckChunkMessage FileServer::handleChunkUpload(ChunkTransferMessage *msg) {
  AckChunkMessage response;
  qDebug() << "received chunk transfer message on server";
  return response;
}

FileStorage *FileServer::getStorage() { return fileStorage.get(); }

AuthResponseMessage FileServer::handleAuth(AuthMessage *msg) {
  qDebug() << "User: " << msg->username << "Password: " << msg->password;
  AuthResponseMessage response;

  if (msg->username.isEmpty() || msg->deviceName.isEmpty()) {
    qDebug() << "handleAuth: username or deviceName empty";
    response.success = false;
    response.error = "username and deviceName required";
    return response;
  }

  if (sessionStore.hasSession(msg->username, msg->deviceName)) {
    qDebug() << "handleAuth: device already connected for" << msg->username
             << msg->deviceName;
    response.success = false;
    response.error = "device already connected";
    return response;
  }

  if (!usersDb.userExists(msg->username, msg->password)) {
    qDebug() << "handleAuth: invalid credentials for" << msg->username;
    response.success = false;
    response.error = "invalid credentials";
    return response;
  }

  QString token = sessionStore.createSession(msg->username, msg->deviceName);
  response.success = true;
  response.token = token;
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

std::optional<QString> FileServer::getUsername(const QString &token) {
  return sessionStore.getUsername(token);
}

void FileServer::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received from server";
}
