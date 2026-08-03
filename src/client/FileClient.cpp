#include "FileClient.h"
#include "LocalClientTransport.h"
#include "LocalFileStorage.h"
#include "MerkleProtocolMessages.h"
#include "Messages.h"
#include "TcpClientTransport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <memory>
#include <qnamespace.h>
#include <tuple>

FileClient::FileClient() { fileStorage = std::make_unique<LocalFileStorage>(); }

const NegotiationState *FileClient::getNegotiationState() const {
  return merkleSyncClient.getNegotiationState();
}

void FileClient::buildMerkleTree(const std::string &rootDir,
                                 const QString &username) {
  merkleTree = std::make_unique<MerkleTree>(username);
  // TODO: build from DB and not storage
  auto files = fileStorage->listFiles(username);
  for (const auto &path : files) {
    auto contents = fileStorage->readFile(username, path);
    if (!contents.has_value())
      continue;
    auto mtime = fileStorage->getMtime(username, path);
    if (!mtime.has_value())
      continue;
    merkleTree->addFile(path, mtime.value(), hashContents(contents.value()));
  }
  assert(merkleTree->verifyHashes());
}

void FileClient::configure(const FileClientConfig &config) {
  username = config.username;
  password = config.password;
  shouldUseTimer = !config.manualTick;
  syncStrategy = config.syncStrategy;
  fileStorage->setRoot(QDir(config.rootDir).absolutePath());
  buildMerkleTree(fileStorage->rootPath(username).toStdString(), username);
  serverName = config.serverName;
  tickIntervalMs = config.tickIntervalMs;
  database.storeUser(username, password, fileStorage->rootPath(username));
  deviceName = config.deviceName;

  switch (config.protocol) {
  case TransportProtocol::LocalSocket:
    transport = std::make_unique<LocalClientTransport>();
    break;
  case TransportProtocol::Tcp:
    transport = std::make_unique<TcpClientTransport>();
    break;
  }

  transport->configure(serverName);
}

LocalFileStorage *FileClient::getStorage() { return fileStorage.get(); }

void FileClient::connectToServer() { transport->connectToServer(); }

void FileClient::start() {
  setupConnections();
  connectToServer();
}

void FileClient::setupConnections() {

  setupSocketConnections();

  QObject::connect(this, &FileClient::authenticated, this,
                   &FileClient::onAuthenticated);
  QObject::connect(this, &FileClient::outboundFileCommandsReady, this,
                   &FileClient::flushOutboundCommands);
  QObject::connect(&merkleSyncClient, &MerkleSyncClient::messageSendRequest,
                   this, [this](MerkleProtocolMessage protoMsg) {
                     MerkleSyncMessage msg = toWireMessage(protoMsg);
                     msg.token = token;
                     transport->send(msg);
                   });
  QObject::connect(&merkleSyncClient, &MerkleSyncClient::negotiationCompleted,
                   this, &FileClient::handleNegotiationCompleted);
  QObject::connect(this, &FileClient::negotiationCompleted, this,
                   &FileClient::handleNegotiationCompleted);
  QObject::connect(&merkleSyncClient, &MerkleSyncClient::negotiationCompleted,
                   this, &FileClient::negotiationCompleted);
  startTimer();
}

void FileClient::setupSocketConnections() {
  QObject::connect(transport.get(), &ClientTransport::connected, this,
                   &FileClient::onConnected);
  QObject::connect(transport.get(), &ClientTransport::disconnected, this,
                   &FileClient::onDisconnected);
  QObject::connect(transport.get(), &ClientTransport::messageReady, this,
                   &FileClient::dispatch);
}

void FileClient::onConnected() {
  qDebug() << "Connected event fired.";
  state = ClientState::Authenticating;
  sendAuthRequest();
}

void FileClient::onDisconnected() {
  state = ClientState::Disconnected;
  token.clear();
}

void FileClient::onAuthenticated() {
  if (pendingTick) {
    pendingTick = false;
    clientTick();
  }
}

void FileClient::startTimer() {
  if (shouldUseTimer) {
    QObject::connect(&timer, &QTimer::timeout, this,
                     [this]() { clientTick(); });
    timer.start(tickIntervalMs);
  }
}

void FileClient::dispatch(Message *msg) {
  if (!msg) {
    qDebug() << "Failed to deserialize message";
    return;
  }
  switch (msg->type()) {
  case MessageType::ServerAuthResponse: {
    handleAuthResponse(static_cast<AuthResponseMessage *>(msg));
    break;
  }
  case MessageType::SyncRequest: {
    handleSyncResponse(static_cast<SyncRequestMessage *>(msg));
    break;
  }
  case MessageType::MerkleSync: {
    handleMerkleSyncResponse(static_cast<MerkleSyncMessage *>(msg));
    break;
  }
  case MessageType::ListResponse: {
    handleListResponse(static_cast<ListResponseMessage *>(msg));
    break;
  }
  case MessageType::ChunkTransfer: {
    handleChunkDownload(static_cast<ChunkTransferMessage *>(msg));
    break;
  }
  case MessageType::AckChunk: {
    handleChunkAck(static_cast<AckChunkMessage *>(msg));
    break;
  }
  default: {
    handleUnrecognized(msg);
    break;
  }
  }
}

FileClient::~FileClient() {}

void FileClient::handleMerkleDirectoryListing(ListResponseMessage *msg) {
  for (const auto &entry : msg->entries) {
    if (!entry.deleted) {
      qDebug() << "Hello";
      resolveServerHasFileClientDoesnt(entry.path);
      continue;
    }
    applyTombstone(entry.path, entry.mtime);
  }

  pendingDirectoryRequests--;
  if (pendingDirectoryRequests == 0) {
    inMerkleApply = false;
    Q_EMIT outboundFileCommandsReady();
    checkSyncCompletionAndUnlock();
  }
  return;
}

void FileClient::handleNaiveListing(ListResponseMessage *msg) {
  NegotiationState state;

  QSet<QString> serverPaths;
  for (const auto &entry : msg->entries) {
    if (!entry.deleted)
      serverPaths.insert(entry.path);
  }

  // --- server-reported entries: compare against reconciled local DB ---
  for (const auto &entry : msg->entries) {
    if (entry.deleted) {
      // server deletion vs local
      auto localMtime = database.readMtime(username, entry.path);
      if (localMtime.has_value() && localMtime.value() > entry.mtime) {
        state.diffEntries.onlyInLeft.append(
            {true, entry.path}); // client newer -> upload
      } else {
        state.diffEntries.deletionWinsRight.append({entry.path, entry.mtime});
      }
    } else {
      auto localMtime = database.readMtime(username, entry.path);
      if (!localMtime.has_value()) {
        state.diffEntries.onlyInRight.append(
            {true, entry.path}); // server-only -> download
      } else if (entry.mtime > localMtime.value()) {
        state.diffEntries.modified.append(
            entry.path); // server newer -> download
      } else if (localMtime.value() > entry.mtime) {
        state.diffEntries.onlyInLeft.append(
            {true, entry.path}); // client newer -> upload
      }
    }
  }

  // --- local entries the server didn't report ---
  // reconciled DB already reflects the filesystem (scan+apply ran in naiveTick)
  for (const auto &path : database.allTrackedFiles(username)) {
    if (!serverPaths.contains(path)) {
      state.diffEntries.onlyInLeft.append({true, path}); // local-only -> upload
    }
  }

  // --- locally-deleted (tombstones) the server still has ---
  auto tombstones = database.allTombstones(username);
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    const QString &path = it.key();
    const QDateTime &deletedAt = it.value();
    if (serverPaths.contains(path)) {
      state.diffEntries.deletionWinsLeft.append({path, deletedAt});
    }
  }

  Q_EMIT negotiationCompleted(state);
}

void FileClient::handleListResponse(ListResponseMessage *msg) {
  awaitingListResponse = false;
  qDebug() << "Handling list response from server with" << msg->entries.size()
           << "entries";

  if (inMerkleApply) {
    handleMerkleDirectoryListing(msg);
    return;
  }
  handleNaiveListing(msg);
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

void FileClient::handleChunkAck(AckChunkMessage *msg) {
  qDebug() << "received ack chunk message on client";
}

void FileClient::handleChunkDownload(ChunkTransferMessage *msg) {
  qDebug() << "received chunk transfer message on client";
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

  switch (msg->operationStatus) {
  case FileOperationStatus::Done: {
    qDebug() << "Write acked for:" << msg->path;
    auto localMtime = fileStorage->getMtime(username, msg->path);
    if (localMtime.has_value()) {
      database.updateFileMtime(username, msg->path, localMtime.value());
      if (merkleTree) {
        merkleTree->addFile(msg->path, localMtime.value(),
                            hashContents(msg->contents));
      }
    } else {
      qDebug() << "handleWriteResponse: no local mtime after write for"
               << msg->path;
    }
    break;
  }
  case FileOperationStatus::ServerHasNewer: {
    qDebug() << "Server has newer version of:" << msg->path;
    applyServerVersion(msg->path, msg->contents);
    break;
  }
  default: {
    qDebug() << "handleWriteResponse: unhandled status for" << msg->path;
    break;
  }
  }
}

void FileClient::handleDeleteResponse(SyncRequestMessage *msg) {
  switch (msg->operationStatus) {
  case FileOperationStatus::Done: {
    qDebug() << "Delete acked for:" << msg->path;
    database.removeFileMtime(username, msg->path);
    if(merkleTree) {
        merkleTree->deleteFile(msg->path,msg->operationTime);
      }
    break;
  }
  case FileOperationStatus::ServerHasNewer: {
    qDebug() << "Server rejected deletion, restoring:" << msg->path;
    applyServerVersion(msg->path, msg->contents);
    break;
  }
  default: {
    qDebug() << "handleDeleteResponse: unhandled status for" << msg->path;
    break;
  }
  }
}

void FileClient::applyServerVersion(const QString &path,
                                    const QByteArray &contents) {
  if (contents.isEmpty()) {
    qDebug() << "applyServerVersion: empty contents for" << path;
    return;
  }
  if (!writeFile(username, path, contents)) {
    qDebug() << "applyServerVersion: failed to write" << path;
    return;
  }
  auto localMtime = fileStorage->getMtime(username, path);
  if (localMtime.has_value()) {
    database.updateFileMtime(username, path, localMtime.value());
  } else {
    qDebug() << "applyServerVersion: no local mtime after write for" << path;
  }
}

QList<QString> FileClient::discoverNewFilesAndUpdateMtimes() {
  QList<QString> newFiles;
  auto files = fileStorage->listFiles(username);
  for (const auto &relativePath : files) {
    auto localFileMtime = fileStorage->getMtime(username, relativePath);
    if (!localFileMtime.has_value())
      continue;
    auto storedMtime = database.readMtime(username, relativePath);
    bool latestAlreadyInDb = storedMtime.has_value() &&
                             storedMtime.value() == localFileMtime.value();
    if (latestAlreadyInDb)
      continue;
    database.updateFileMtime(username, relativePath, localFileMtime.value());
    qDebug() << "Discovered new/modified file:" << relativePath
             << "mtime:" << localFileMtime.value();
    if (merkleTree) {
      auto contents = fileStorage->readFile(username, relativePath);
      if (contents.has_value()) {
        merkleTree->addFile(relativePath, localFileMtime.value(),
                            hashContents(contents.value()));
      }
    }
    newFiles.append(relativePath);
  }
  return newFiles;
}

QSet<QString> FileClient::discoverDeletedFiles() {
  auto tracked = database.allTrackedFiles(username);
  auto fileList = fileStorage->listFiles(username);
  QSet<QString> current(fileList.begin(), fileList.end());
  qDebug() << "tracked:" << tracked << "current:" << current
           << "deleted:" << (tracked - current);
  return tracked - current;
}

void FileClient::flushOutboundCommands() {
  qDebug() << "Flushing sync requests accumulated from client";
  for (auto it = commandsToSend.begin(); it != commandsToSend.end(); ++it) {
    transport->send(it.value());
    pendingMessages++;
  }
  commandsToSend.clear();
}

void FileClient::stageUploadFor(const QString &path) {
  auto contents = fileStorage->readFile(username, path);
  if (!contents.has_value()) {
    qDebug() << "Could not read file: " << path;
    return;
  }
  auto mtime = fileStorage->getMtime(username, path);
  commandsToSend.insert(path, buildSyncRequest(path, FileOperationType::Write,
                                               contents.value(), mtime));
}

void FileClient::stageDownloadFor(const QString &path) {
  auto mtime = database.readMtime(username, path);
  commandsToSend.insert(
      path, buildSyncRequest(path, FileOperationType::Write, {}, mtime));
}

void FileClient::stageConflictResolution(const QString &path) {
  stageUploadFor(path);
}

void FileClient::resolveServerHasFileClientDoesnt(const QString &path) {
  auto found = merkleTree->find(path);
  bool clientHasLiveNode = found.has_value() && !std::get<1>(*found);
  if (database.readMtime(username, path).has_value() && !clientHasLiveNode) {
    stageDeleteFor(path, QDateTime::currentDateTime());
  } else {
    stageDownloadFor(path);
  }
}

void FileClient::stageNewFilesForSending(const QList<QString> &newFiles) {
  for (const auto &p : newFiles) {
    stageUploadFor(p);
  }
}

void FileClient::stageDeletedFilesForSending(
    const QSet<QString> &deletedFiles) {
  for (const auto &p : deletedFiles) {
    stageDeleteFor(p, QDateTime::currentDateTime());
  }
}

void FileClient::clientTick() {
  if (state != ClientState::Authenticated) {
    qDebug() << "Client is not yet authenticated";
    pendingTick = true;
    return;
  }
  if (currentlyDoingSyncOps)
    return;
  if (awaitingListResponse)
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
}

void FileClient::naiveTick() {

  auto result = scanFilesystemForChanges();
  applyChangesToDb(result);
  ListRequestMessage req;
  req.token = token;
  awaitingListResponse = true;
  transport->send(req);
}

LocalChangeSet FileClient::scanFilesystemForChanges() const {
  LocalChangeSet changes;

  auto fileList = fileStorage->listFiles(username);
  QSet<QString> currentFsState(fileList.begin(), fileList.end());

  QSet<QString> tracked = database.allTrackedFiles(username);
  qDebug() << "Current is: " << currentFsState;
  qDebug() << "Tracked is: " << tracked;

  for (const auto &path : currentFsState) {
    auto fsMtime = fileStorage->getMtime(username, path);
    assert(fsMtime.has_value() &&
           "a listed file could not have its mtime read.");

    auto dbMtime = database.readMtime(username, path);
    if (!dbMtime.has_value()) {
      changes.newFiles.append({path, fsMtime.value()});
    } else if (fsMtime.value() != dbMtime.value()) {
      changes.modifiedFiles.append({path, fsMtime.value()});
    }
  }

  for (const auto &path : tracked) {
    if (!currentFsState.contains(path)) {
      // deletion detected now; use the recorded tombstone time if we have one,
      // otherwise stamp detection time.
      auto deletedAt = database.deletedAt(username, path);
      changes.deletedFiles.append(
          {path, deletedAt.value_or(QDateTime::currentDateTime())});
    }
  }
  qDebug() << "Result of scan is: " << changes;
  return changes;
}

void FileClient::applyChangesToDb(const LocalChangeSet &changes) {
  // New and modified: same DB+tree update (both write the current mtime/hash).
  // Kept as one loop since the handling is identical.
  auto applyPresent = [this](const QPair<QString, QDateTime> &entry) {
    auto &path = entry.first;
    auto &mtime = entry.second;
    database.updateFileMtime(username, path, mtime);
    if (merkleTree) {
      auto contents = fileStorage->readFile(username, path);
      if (contents.has_value()) {
        merkleTree->addFile(path, mtime, hashContents(contents.value()));
      }
    }
  };

  for (const auto &entry : changes.newFiles) {
    applyPresent(entry);
  }
  for (const auto &entry : changes.modifiedFiles) {
    applyPresent(entry);
  }

  // Deletions: remove from DB tracking and tombstone the tree, together.
  for (const auto &entry : changes.deletedFiles) {
    const QString &path = entry.first;
    const QDateTime &deletedAt = entry.second;
    // database.removeFileMtime(username, path);
    database.markDeleted(username, path, deletedAt, /*untrack=*/true);
    if (merkleTree) {
      merkleTree->deleteFile(path, deletedAt);
    }
  }
}

void FileClient::scanFilesystemAndApplyChangesToDb() {
  auto changes = scanFilesystemForChanges();
  applyChangesToDb(changes);
}

void FileClient::merkleTick() {
  qDebug() << "Merkle tick";
  if (currentlyNegotiatingFileDiffs) {
    qDebug() << "There is an existing negotiation going on";
    return;
  }
  currentlyNegotiatingFileDiffs = true;
  auto result = scanFilesystemForChanges();
  applyChangesToDb(result);
  merkleSyncClient.startNegotiation(merkleTree.get());
}

std::optional<QDateTime> FileClient::writeFile(const QString &user,
                                               const QString &path,
                                               const QByteArray &contents) {
  assert(user == username && "WriteFile for client should pass the username "
                             "that it was configured with");
  if (!fileStorage->writeFile(username, path, contents)) {
    qDebug() << "Failed to write file on client:" << path;
    return {};
  }
  auto mtime = fileStorage->getMtime(username, path);
  assert(mtime.has_value());

  // we should not update the database, let the client discover it.
  if (merkleTree) {
    merkleTree->addFile(path, mtime.value(), hashContents(contents));
  }
  return mtime.value();
}

QByteArray FileClient::hashContents(const QByteArray &contents) {
  QByteArray hash =
      QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
  return hash;
}

std::optional<QDateTime> FileClient::deleteFile(const QString &user,
                                                const QString &path) {
  assert(user == username && "DeleteFile for client should pass the username "
                             "that it was configured with");
  if (!fileStorage->deleteFile(username, path)) {
    qDebug() << "Failed to delete file on client: " << path;
    return {};
  }
  QDateTime deletedAt = QDateTime::currentDateTime();
  if (merkleTree) {
    merkleTree->deleteFile(path, deletedAt);
  }
  database.markDeleted(user, path, deletedAt, false);
  return deletedAt;
}

void FileClient::handleMerkleSyncResponse(MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle response at client";
  assert(msg->phase != 0 && "Client always initiates the negotiation server "
                            "should respond with phase 1 or 2.");
  merkleSyncClient.handleResponse(toProtocolMessage(*msg), merkleTree.get());
}

void FileClient::stageDirectoryUpload(const QString &dirPath) {
  // Walk all local files whose path is under dirPath
  auto files = fileStorage->listFiles(username);
  for (const auto &path : files) {
    if (path == dirPath || path.startsWith(dirPath + "/")) {
      stageUploadFor(path);
    }
  }
}

void FileClient::requestDirectoryList(const QString &dirPath) {
  ListRequestMessage req;
  req.token = token;
  req.directory = dirPath;
  pendingDirectoryRequests++;
  transport->send(req);
}

void FileClient::stageDeleteFor(const QString &path,
                                const QDateTime &deletedAt) {
  SyncRequestMessage msg;
  msg.token = token;
  msg.path = path;
  msg.contents = {};
  msg.operationTime = deletedAt;
  msg.operationType = FileOperationType::Delete;
  msg.operationStatus = FileOperationStatus::DoIt;
  commandsToSend.insert(path, msg);
  if (merkleTree) {
    merkleTree->deleteFile(path, deletedAt);
  }
}

SyncRequestMessage
FileClient::buildSyncRequest(const QString &path, FileOperationType op,
                             const QByteArray &contents,
                             const std::optional<QDateTime> &mtime) {
  SyncRequestMessage msg;
  msg.token = token;
  msg.path = path;
  msg.contents = contents;
  msg.operationTime = mtime.has_value() ? mtime.value() : QDateTime();
  msg.operationType = op;
  msg.operationStatus = FileOperationStatus::DoIt;
  return msg;
}

void FileClient::handleNegotiationCompleted(
    const NegotiationState &negotiationState) {
  qDebug() << "Now starting to sync files";
  currentlyNegotiatingFileDiffs = false;
  qDebug() << "Tree Diff:";
  qDebug() << "(L) " << negotiationState.diffEntries.onlyInLeft;
  qDebug() << "(R) " << negotiationState.diffEntries.onlyInRight;
  qDebug() << "(M) " << negotiationState.diffEntries.modified;
  qDebug() << "(DL) " << negotiationState.diffEntries.deletionWinsLeft;
  qDebug() << "(DR) " << negotiationState.diffEntries.deletionWinsRight;

  if (syncStrategy == SyncStrategy::Merkle) {
    inMerkleApply = true;
  }
  pendingDirectoryRequests = 0;

  // File-level cases first; directories deferred for next iteration
  for (const auto &[isFile, path] : negotiationState.diffEntries.onlyInLeft) {
    if (isFile)
      stageUploadFor(path);
    else
      stageDirectoryUpload(path);
  }
  for (const auto &[isFile, path] : negotiationState.diffEntries.onlyInRight) {
    if (!isFile) {
      requestDirectoryList(path);
      continue;
    }
    resolveServerHasFileClientDoesnt(path);
  }
  for (const auto &path : negotiationState.diffEntries.modified) {
    // modified should only contain files at this point — directories
    // were resolved during negotiation via directoriesToCheckWithServer
    {
      auto found = merkleTree->find(path);
      assert(found.has_value());
      auto &[node, isTombstoned] = *found;
      assert(node->type == FileType::File);
    }
    stageConflictResolution(path);
  }

  for (const auto &[path, deletedAt] :
       negotiationState.diffEntries.deletionWinsLeft) {
    // Client has tombstoned, server doesn't know — stage a Delete request
    // to server
    stageDeleteFor(path, deletedAt);
  }

  for (const auto &[path, deletedAt] :
       negotiationState.diffEntries.deletionWinsRight) {
    applyTombstone(path, deletedAt);
    qDebug() << "Applied server tombstone via merkle negotiation:" << path;
  }

  if (pendingDirectoryRequests == 0) {
    if (syncStrategy == SyncStrategy::Merkle) {
      inMerkleApply = false;
    }
    Q_EMIT outboundFileCommandsReady();
    checkSyncCompletionAndUnlock();
  }
}

void FileClient::applyTombstone(const QString &path, const QDateTime &mtime) {
  auto local = fileStorage->readFile(username, path);
  if (local.has_value()) {
    fileStorage->deleteFile(username, path);
    qDebug() << "Applied server tombstone for:" << path;
  }
  if (merkleTree) {
    merkleTree->deleteFile(path, mtime);
  }
  database.removeFileMtime(username, path);
}

QString FileClient::getDeviceName() { return deviceName; }

void FileClient::sendAuthRequest() {
  AuthMessage msg;
  msg.username = username;
  msg.password = password;
  msg.deviceName = getDeviceName();
  transport->send(msg);
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
    // if we want explicit disconnect on auth failure should add a
    // disconnect method to the clienttransport abstraction
  }
}

void FileClient::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received from client";
}
