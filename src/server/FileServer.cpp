#include "FileServer.h"
#include "FileHasher.h"
#include "LocalServerTransport.h"
#include "Messages.h"
#include "TcpServerTransport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
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

void FileServer::setupConnections() { setupSocketConnections(); }

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
    auto resp = handleMerkleSyncRequest(static_cast<MerkleSyncMessage *>(msg));
    transport->send(socket, resp);
    break;
  }
  case MessageType::ListRequest: {
    auto resp = handleListRequest(static_cast<ListRequestMessage *>(msg));
    transport->send(socket, resp);
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
  database.updateFileMtime(user, file, mtime);
  getUserTree(user)->addFile(file.toStdString(), mtime, hashContents(contents));
  return true;
}

QString FileServer::getUserFrom(Message *msg) {
  auto username = getUsername(msg->token);
  if (!username.has_value()) {
    qDebug() << "No username for token: " << msg->token;
    return "";
  }
  return username.value();
}

SyncRequestMessage
FileServer::handleDeleteRequest(SyncRequestMessage *msg, const QString &path,
                                const std::optional<QDateTime> &storedMtime) {
  SyncRequestMessage response;
  response.path = msg->path;
  response.operationType = FileOperationType::Delete;

  auto username = getUserFrom(msg);
  assert(username != "" &&
         "Token should always be set so we can fetch username on server");
  qDebug() << "Delete request for user:" << username
           << "at device:" << sessionStore.getDeviceName(msg->token).value();

  if (!storedMtime.has_value()) {
    qDebug() << "handleDeleteRequest: no stored mtime, marking Done";
    response.operationStatus = FileOperationStatus::Done;
    return response;
  }

  QDateTime clientMtime = msg->operationTime;
  QDateTime serverMtime = storedMtime.value();
  if (serverMtime > clientMtime) {
    qDebug() << "handleDeleteRequest: server mtime ahead, sending newer file";
    return trySendNewerFile(username, QString::fromStdString(msg->path),
                            serverMtime, FileOperationType::Delete);
  }

  if (!fileStorage->deleteFile(username, QString::fromStdString(msg->path))) {
    qDebug() << "handleDeleteRequest: failed to delete file from storage";
    response.operationStatus = FileOperationStatus::Error;
    return response;
  }

  QDateTime clientDeletedAt = msg->operationTime;
  database.markDeleted(username, path, clientDeletedAt);
  getUserTree(username)->deleteFile(msg->path, clientDeletedAt);
  response.operationStatus = FileOperationStatus::Done;
  return response;
}

SyncRequestMessage FileServer::trySendNewerFile(const QString &username,
                                                const QString &path,
                                                const QDateTime &serverMtime,
                                                FileOperationType origOp) {
  SyncRequestMessage response;
  response.path = path.toStdString();
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
  assert(username != "" &&
         "Token should always be set so we can fetch username on server");

  QDateTime clientMtime = msg->operationTime;

  if (storedMtime.has_value() && storedMtime.value() > clientMtime) {
    qDebug() << "handleWriteRequest: server mtime ahead, sending newer file";
    return trySendNewerFile(username, QString::fromStdString(msg->path),
                            storedMtime.value(), FileOperationType::Write);
  }

  if (!fileStorage->writeFile(username, QString::fromStdString(msg->path),
                              msg->contents)) {
    qDebug() << "handleWriteRequest: failed to write file to storage";
    response.operationStatus = FileOperationStatus::Error;
    return response;
  }

  getUserTree(username)->addFile(path.toStdString(), clientMtime,
                                 hashContents(msg->contents));
  database.updateFileMtime(username, path, clientMtime);
  response.operationStatus = FileOperationStatus::Done;
  return response;
}
QByteArray FileServer::hashContents(const QByteArray &contents) {
  QByteArray hash =
      QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  return hash;
}

std::unique_ptr<MerkleTree>
FileServer::buildMerkleTree(const QString &username) {
  auto tree = std::make_unique<MerkleTree>(username);

  auto files = fileStorage->listFiles(username);
  for (const auto &path : files) {
    auto contents = fileStorage->readFile(username, path);
    if (!contents.has_value()) {
      qDebug() << "buildMerkleTree: missing storage for tracked path" << path;
      continue;
    }
    auto mtime = database.readMtime(username, path);
    if (!mtime.has_value()) {
      qDebug() << "buildMerkleTree: no mtime for path" << path;
      continue;
    }

    tree->addFile(path.toStdString(), mtime.value(),
                  hashContents(contents.value()));
  }

  // Apply DB tombstones to the tree
  auto tombstones = database.allTombstones(username);
  QString prefix = username + "/";
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    tree->deleteFile(it.key().toStdString(), it.value());
  }

  assert(tree->verifyHashes());
  return tree;
}

MerkleTree *FileServer::getUserTree(const QString &username) {
  auto it = userTrees.find(username);
  if (it != userTrees.end()) {
    return it->second.get();
  }

  auto tree = buildMerkleTree(username);

  auto *raw = tree.get();
  userTrees.emplace(username, std::move(tree));
  return raw;
}

MerkleSyncMessage FileServer::handleMerkleSyncRequest(MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle sync message at server";
  auto username = getUserFrom(msg);
  assert(username != "" &&
         "Token should always be set so we can fetch username on server");

  auto serverTree = getUserTree(username);
  qDebug() << "Message from client to server: " << msg;

  MerkleSyncMessage response;

  if (msg->phase == 0) {
    if (serverTree->rootHash() == msg->rootHash) {
      qDebug() << "handleMerkleSyncRequest: roots match, negotiation complete";
      response.phase = 2;
      response.fileEntriesPerChild = {};
      return response;
    }
    response.phase = 1;
    response.depth = 1;
    auto rootChildren = serverTree->getHashesAtDepth(1);
    QList<MerkleEntry> childEntries;
    for (const auto &[path, hash] : rootChildren) {
      auto foundNode = serverTree->find(path.toStdString());
      if (foundNode.has_value()) {
        auto &[node, isTombstoned] = *foundNode;
        MerkleEntry entry;
        entry.path = path;
        entry.hash = hash;
        entry.mtime = node->mtime;
        entry.filetype = node->type;
        entry.isTombstone = isTombstoned;
        entry.deletedAt = node->deletedAt;
        childEntries.append(entry);
        qDebug() << "Server building entry path(from getHashesAtDepth(1))="
                 << node->path << "isTombstone=" << isTombstoned
                 << "deletedAt=" << node->deletedAt;
      }
    }
    response.fileEntriesPerChild.append({"", childEntries});
    return response;
  }

  response.phase = 1;
  response.depth = msg->depth;

  for (const auto &[parentPath, _] : msg->fileEntriesPerChild) {
    auto foundNode = serverTree->find(parentPath.toStdString());
    assert(foundNode.has_value());
    auto &[node, isTombstoned] = *foundNode;
    assert(node->type == FileType::Directory);

    auto childHashes = serverTree->getChildHashes(parentPath);
    QList<MerkleEntry> childEntries;
    for (const auto &[path, hash] : childHashes) {
      auto childFound = serverTree->find(path.toStdString());
      if (childFound.has_value()) {
        auto &[childNode, childIsTombstoned] = *childFound;
        MerkleEntry entry;
        entry.path = path;
        entry.hash = hash;
        entry.mtime = childNode->mtime;
        entry.filetype = childNode->type;
        entry.isTombstone = childIsTombstoned;
        entry.deletedAt = childNode->deletedAt;
        childEntries.append(entry);
        qDebug() << "Server building entry path=" << childNode->path
                 << "isTombstone=" << childIsTombstoned
                 << "deletedAt=" << childNode->deletedAt;
      }
    }
    response.fileEntriesPerChild.append({parentPath, childEntries});
  }

  if (response.fileEntriesPerChild.isEmpty()) {
    response.phase = 2;
  }

  return response;
}

ListResponseMessage FileServer::handleListRequest(ListRequestMessage *msg) {
  auto username = getUserFrom(msg);
  assert(username != "" &&
         "Token should always be set so we can fetch username on server");
  ListResponseMessage response;

  bool clientWantsDirectory = !msg->directory.isEmpty();
  auto files = fileStorage->listFiles(username);
  for (const auto &path : files) {
    bool pathIsDir = path.startsWith(msg->directory + "/");
    if (clientWantsDirectory && !pathIsDir) {
      continue;
    }
    auto mtime = database.readMtime(username, path);
    assert(mtime.has_value() && "File in storage must have a DB mtime entry");
    response.entries.append({path, mtime.value(), false});
  }

  auto tombstones = database.allTombstones(username);
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    const QString &path = it.key();
    bool pathIsDir = path.startsWith(msg->directory + "/");
    if (clientWantsDirectory && !pathIsDir) {
      continue;
    }
    response.entries.append({path, it.value(), true});
  }

  return response;
}

SyncRequestMessage FileServer::handleSyncRequest(SyncRequestMessage *msg) {
  qDebug() << "Handling sync request message";
  Q_ASSERT_X(fileStorage != nullptr, "FileServer::handleSyncRequest",
             "fileStorage is not set");

  auto username = getUserFrom(msg);
  assert(username != "" &&
         "Token should always be set so we can fetch username on server");

  auto path = QString::fromStdString(msg->path);
  auto storedMtime = database.readMtime(username, path);

  if (msg->operationType == FileOperationType::Delete) {
    return handleDeleteRequest(msg, path, storedMtime);
  } else if (msg->operationType == FileOperationType::Write) {
    return handleWriteRequest(msg, path, storedMtime);
  }
  assert(false);
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

  if (!verifyUserCredentials(msg->username, msg->password)) {
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
