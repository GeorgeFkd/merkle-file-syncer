#include "FileClient.h"
#include "Hasher.h"
#include "LocalClientTransport.h"
#include "LocalFileStorage.h"
#include "MerkleProtocolMessages.h"
#include "Messages.h"

#include "TcpClientTransport.h"
#include <QCoreApplication>
#include <QDir>
#include <memory>
#include <qnamespace.h>

FileClient::FileClient() { fileStorage = std::make_unique<LocalFileStorage>(); }

const NegotiationState *FileClient::getNegotiationState() const {
  return merkleSyncClient.getNegotiationState();
}

void FileClient::configure(const FileClientConfig &config) {
  username = config.username;
  password = config.password;
  shouldUseTimer = !config.manualTick;
  syncStrategy = config.syncStrategy;
  fileStorage->setRoot(QDir(config.rootDir).absolutePath());
  serverName = config.serverName;
  tickIntervalMs = config.tickIntervalMs;
  usersDb.storeUser(username, password, fileStorage->rootPath(username));
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
  fileTransferClient =
      std::make_unique<FileTransferClient>(fileStorage.get(), username);
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
  setupFileTransferConnections();
  setupNegotiationConnections();

  startTimer();
}

void FileClient::onDownloadCompleted(QString path) {
  qDebug() << "Download completed for:" << path;
  auto meta = pendingDownloadMetadata.take(path);
  recordFile(username, path, meta.second /*mtime*/, meta.first /*hash*/);
  transferDone();
}

void FileClient::setupNegotiationConnections() {
  QObject::connect(&merkleSyncClient, &MerkleSyncClient::messageSendRequest,
                   this,
                   [this](std::shared_ptr<MerkleProtocolMessage> protoMsg) {
                     auto msg = toWireMessage(protoMsg.get());
                     msg->token = token;
                     transport->send(msg);
                   });
  QObject::connect(&naiveSyncClient, &NaiveSyncClient::sendMessage, this,
                   [this](std::shared_ptr<ListRequestMessage> msg) {
                     awaitingListResponse = true;
                     msg->token = token;
                     msg->useMerkle = false;
                     transport->send(msg);
                   });

  QObject::connect(&naiveSyncClient, &NaiveSyncClient::negotiationCompleted,
                   this, &FileClient::handleNegotiationCompleted);
  QObject::connect(&merkleSyncClient, &MerkleSyncClient::negotiationCompleted,
                   this, &FileClient::handleNegotiationCompleted);
}

void FileClient::setupFileTransferConnections() {
  QObject::connect(this, &FileClient::outboundFileCommandsReady, this,
                   &FileClient::flushOutboundCommands);

  QObject::connect(this, &FileClient::downloadRequested, this,
                   &FileClient::stageDownloadFor);
  QObject::connect(this, &FileClient::uploadRequested, this,
                   &FileClient::stageUploadFor);

  // transfer emits messages -> transport (stamp token like the other senders)
  QObject::connect(
      fileTransferClient.get(), &FileTransferClient::sendMessage, this,
      [this](std::shared_ptr<Message> msg) {
        msg->token = token;
        if (msg->type() == MessageType::RequestChunkSizeUpload) {
          auto spec = std::static_pointer_cast<RequestChunkSizeForUpload>(msg);
          auto mtime = database.readMtime(username, spec->path);
          auto hash = database.readHash(username, spec->path);
          if (mtime) {
            spec->mtime = *mtime;
            qDebug() << "Mtime is: " << mtime;
          } else {
            qWarning() << "No mtime for path: " << spec->path;
          }
          if (hash) {
            spec->hash = *hash;
          } else {
            qWarning() << "No hash for path: " << spec->path;
          }
          qDebug() << "Hash is: " << hash;
        }
        transport->send(msg);
      });

  // inbound messages -> transfer (it filters by type internally)
  QObject::connect(transport.get(), &ClientTransport::messageReady,
                   fileTransferClient.get(), &FileTransferClient::onMessage);

  QObject::connect(fileTransferClient.get(),
                   &FileTransferClient::downloadCompleted, this,
                   &FileClient::onDownloadCompleted);

  // upload completion
  QObject::connect(fileTransferClient.get(),
                   &FileTransferClient::uploadCompleted, this,
                   &FileClient::onUploadCompleted);
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

void FileClient::dispatch(std::shared_ptr<Message> msg) {
  if (!msg) {
    qDebug() << "Failed to deserialize message";
    return;
  }
  switch (msg->type()) {
  case MessageType::ServerAuthResponse: {
    handleAuthResponse(static_cast<AuthResponseMessage *>(msg.get()));
    break;
  }
  case MessageType::DeleteRequest: {
    handleDeleteResponse(static_cast<DeleteRequestMessage *>(msg.get()));
    break;
  }
  case MessageType::MerkleSync: {
    handleMerkleSyncResponse(static_cast<MerkleSyncMessage *>(msg.get()));
    break;
  }
  case MessageType::ListResponse: {
    handleListResponse(std::static_pointer_cast<ListResponseMessage>(msg));
    break;
  }
  case MessageType::SpecifyChunkSizeDownload: {
    auto *m = static_cast<SpecifyChunkSizeDownload *>(msg.get());
    pendingDownloadMetadata.insert(m->path, {m->hash, m->mtime});
    break;
  }
  default: {
    handleUnrecognized(msg.get());
    break;
  }
  }
}

FileClient::~FileClient() {}

void FileClient::handleMerkleDirectoryListing(
    std::shared_ptr<ListResponseMessage> msg) {
  for (const auto &entry : msg->entries) {
    if (!entry.deleted) {
      Q_EMIT downloadRequested(entry.path);
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

void FileClient::handleListResponse(std::shared_ptr<ListResponseMessage> msg) {
  awaitingListResponse = false;
  qDebug() << "Handling list response from server with" << msg->entries.size()
           << "entries";

  if (inMerkleApply) {
    handleMerkleDirectoryListing(msg);
    return;
  }
  naiveSyncClient.onMessage(msg, &database, username);
}

void FileClient::checkSyncCompletionAndUnlock() {
  bool allZero = pendingMessages == 0 && outstandingTransfers == 0 &&
                 pendingDirectoryRequests == 0;
  qDebug() << "checkSync pending=" << pendingMessages
           << " transfers=" << outstandingTransfers
           << " dirReqs=" << pendingDirectoryRequests
           << (allZero ? " COMPLETE" : " WAITING");
  if (pendingMessages == 0 && outstandingTransfers == 0) {
    currentlyDoingSyncOps = false;
    qDebug() << "All of current sync items have been synced.";
    Q_EMIT syncCompleted();
    if (shouldUseTimer) {
      timer.start(tickIntervalMs);
    }
  }
}

void FileClient::handleDeleteResponse(DeleteRequestMessage *msg) {
  pendingMessages--;
  switch (msg->operationStatus) {
  case FileOperationStatus::Done: {
    qDebug() << "Delete acked for:" << msg->path;
    recordDeletion(username, msg->path, msg->operationTime);
    break;
  }
  case FileOperationStatus::ServerHasNewer: {
    qDebug() << "Server rejected deletion, restoring:" << msg->path;
    qDebug() << "Whole message in client:: handleDeleteResponse is: " << msg;
    assert(false); // it is still hit
    applyServerVersion(msg->path, msg->contents);
    break;
  }
  default: {
    qDebug() << "handleDeleteResponse: unhandled status for" << msg->path;
    break;
  }
  }
  checkSyncCompletionAndUnlock();
}

void FileClient::applyServerVersion(const QString &path,
                                    const QByteArray &contents) {
  qDebug() << "Applying server version for path: " << path;
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
    recordFile(username, path, localMtime.value(), hashContents(contents));
  } else {
    qDebug() << "applyServerVersion: no local mtime after write for" << path;
  }
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
  outstandingTransfers++;
  fileTransferClient->startUpload(path);
}

void FileClient::onUploadCompleted(QString path) {
  qDebug() << "Upload completed for:" << path;
  // client already has this file recorded (from the scan); nothing to record.
  transferDone();
}

void FileClient::transferDone() {
  outstandingTransfers--;
  checkSyncCompletionAndUnlock();
}

void FileClient::stageDownloadFor(const QString &path) {
  outstandingTransfers++;
  quint64 ignoredSize = 5 * 1024 * 1000;
  fileTransferClient->startDownload(path, ignoredSize);
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
  naiveSyncClient.startNegotiation();
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
    auto contents = fileStorage->readFile(username, path);
    if (contents.has_value()) {
      recordFile(username, path, mtime, hashContents(contents.value()));
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
    recordDeletion(username, path, deletedAt);
  }
}

void FileClient::recordFile(const QString &username, const QString &path,
                            const QDateTime &mtime, const QByteArray &hash) {
  database.recordFile(username, path, mtime, hash);
}

void FileClient::recordDeletion(const QString &username, const QString &path,
                                const QDateTime &deletedAt) {
  database.recordDeletion(username, path, deletedAt);
}

MerkleTree *FileClient::getMerkleTree() {
  return database.getUserTree(username);
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
  merkleSyncClient.startNegotiation(getMerkleTree());
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

  recordFile(user, path, mtime.value(), hashContents(contents));
  return mtime.value();
}

QByteArray FileClient::hashContents(const QByteArray &contents) {
  return Hasher::hash(contents);
}

void FileClient::handleMerkleSyncResponse(MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle response at client";
  assert(msg->phase != 0 && "Client always initiates the negotiation server "
                            "should respond with phase 1 or 2.");
  merkleSyncClient.onMessage(toProtocolMessage(msg), getMerkleTree());
}

void FileClient::stageDirectoryUpload(const QString &dirPath) {
  // Walk all local files whose path is under dirPath
  auto files = fileStorage->listFiles(username);
  for (const auto &path : files) {
    if (path == dirPath || path.startsWith(dirPath + "/")) {
      Q_EMIT uploadRequested(path);
    }
  }
}

void FileClient::stageDirectoryDownload(const QString &dirPath) {
  auto req = std::make_shared<ListRequestMessage>();
  qDebug() << "Directory requested is: " << dirPath;
  req->useMerkle = syncStrategy == SyncStrategy::Merkle;
  req->token = token;
  req->directory = dirPath;
  pendingDirectoryRequests++;
  transport->send(req);
}

void FileClient::stageDeleteFor(const QString &path,
                                const QDateTime &deletedAt) {
  auto msg = std::make_shared<DeleteRequestMessage>();
  msg->token = token;
  msg->path = path;
  msg->contents = {};
  msg->operationTime = deletedAt;
  msg->operationStatus = FileOperationStatus::DoIt;
  commandsToSend.insert(path, msg);
  MerkleTree *merkleTree = getMerkleTree();
  if (merkleTree) {
    merkleTree->deleteFile(path, deletedAt);
  }
}

void FileClient::handleNegotiationCompleted(
    const NegotiationState &negotiationState) {
  qDebug() << "Now starting to sync files";
  currentlyNegotiatingFileDiffs = false;
  qDebug() << "Tree Diff:";
  qDebug() << "(L) " << negotiationState.diffEntries.onlyInLeft;
  qDebug() << "(R) " << negotiationState.diffEntries.onlyInRight;
  qDebug() << "(ML) " << negotiationState.diffEntries.modifiedWinsLeft;
  qDebug() << "(MR) " << negotiationState.diffEntries.modifiedWinsRight;
  qDebug() << "(DL) " << negotiationState.diffEntries.deletionWinsLeft;
  qDebug() << "(DR) " << negotiationState.diffEntries.deletionWinsRight;

  if (syncStrategy == SyncStrategy::Merkle) {
    inMerkleApply = true;
  }
  pendingDirectoryRequests = 0;

  // File-level cases first; directories deferred for next iteration
  for (const auto &[isFile, path] : negotiationState.diffEntries.onlyInLeft) {
    if (isFile) {
      Q_EMIT uploadRequested(path);
    } else {
      stageDirectoryUpload(path);
    }
  }
  for (const auto &[isFile, path] : negotiationState.diffEntries.onlyInRight) {
    if (isFile) {
      Q_EMIT downloadRequested(path);
    } else {
      stageDirectoryDownload(path);
    }
  }

  // client's version is newer -> upload it (overwrites the server's stale copy)
  for (const auto &path : negotiationState.diffEntries.modifiedWinsLeft) {
    Q_EMIT uploadRequested(path);
  }

  // server's version is newer -> download it (overwrites our stale copy)
  for (const auto &path : negotiationState.diffEntries.modifiedWinsRight) {
    Q_EMIT downloadRequested(path);
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
    recordDeletion(username, path, mtime);
  }
}

QString FileClient::getDeviceName() { return deviceName; }

void FileClient::sendAuthRequest() {
  auto msg = std::make_shared<AuthMessage>();
  msg->username = username;
  msg->password = password;
  msg->deviceName = getDeviceName();
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
  qDebug() << "received message at client that cannot be handled internally, "
              "should probably send to a different subsystem";
}
