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

FileClient::FileClient() {
  fileStorage = std::make_unique<LocalFileStorage>();
}

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
    merkleTree->addFile(path.toStdString(), mtime.value(),
                        hashContents(contents.value()));
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

void FileClient::handleListResponse(ListResponseMessage *msg) {
  awaitingListResponse = false;
  qDebug() << "Handling list response from server with" << msg->entries.size()
           << "entries";

  if (inMerkleApply) {
    for (const auto &entry : msg->entries) {
      if (!entry.deleted) {
        qDebug() << "Hello";
        resolveServerHasFileClientDoesnt(entry.path);
        continue;
      }

      // server says this file is deleted — apply locally
      auto local = fileStorage->readFile(username, entry.path);
      if (local.has_value()) {
        fileStorage->deleteFile(username, entry.path);
        qDebug() << "Applied server tombstone for:" << entry.path;
      }
      if (merkleTree) {
        merkleTree->deleteFile(entry.path.toStdString(), entry.mtime);
      }
      database.removeFileMtime(username, entry.path);
    }

    pendingDirectoryRequests--;
    if (pendingDirectoryRequests == 0) {
      inMerkleApply = false;
      Q_EMIT outboundFileCommandsReady();
      checkSyncCompletionAndUnlock();
    }
    return;
  }

  // Apply tombstones — server says these files are deleted
  for (const auto &entry : msg->entries) {
    if (!entry.deleted)
      continue;
    auto local = fileStorage->readFile(username, entry.path);
    if (local.has_value()) {
      fileStorage->deleteFile(username, entry.path);
      qDebug() << "Applied server tombstone for:" << entry.path;
    }
    database.removeFileMtime(username, entry.path);
  }
  // Build a set of server paths and a lookup for mtimes
  QHash<QString, QDateTime> serverFiles;
  for (const auto &entry : msg->entries) {
    if (entry.deleted)
      continue;
    serverFiles.insert(entry.path, entry.mtime);
  }

  // Find server-only and server-newer files -> request download
  for (auto it = serverFiles.cbegin(); it != serverFiles.cend(); ++it) {
    const QString &path = it.key();
    const QDateTime &serverMtime = it.value();

    auto localMtime = database.readMtime(username, path);
    bool needsDownload = false;

    if (!localMtime.has_value()) {
      // server has it, we don't know about it
      needsDownload = true;
    } else if (serverMtime > localMtime.value()) {
      // server is newer
      needsDownload = true;
    }

    if (needsDownload) {
      qDebug() << "Requesting download of:" << path;
      SyncRequestMessage req;
      req.token = token;
      req.path = path.toStdString();
      req.contents = {};
      req.operationTime =
          localMtime.has_value() ? localMtime.value() : QDateTime();
      req.operationType = FileOperationType::Write;
      req.operationStatus = FileOperationStatus::DoIt;
      commandsToSend.insert(path, req);
    }
  }

  // Run existing local discovery for uploads and deletes
  auto newFiles = discoverNewFiles();
  auto deletedFiles = discoverDeletedFiles();
  stageNewFilesForSending(newFiles);
  stageDeletedFilesForSending(deletedFiles);

  // Send everything
  Q_EMIT outboundFileCommandsReady();
  checkSyncCompletionAndUnlock();
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
  QString path = QString::fromStdString(msg->path);
  switch (msg->operationStatus) {
  case FileOperationStatus::Done: {
    qDebug() << "Write acked for:" << path;
    auto localMtime = fileStorage->getMtime(username, path);
    if (localMtime.has_value()) {
      database.updateFileMtime(username, path, localMtime.value());
    } else {
      qDebug() << "handleWriteResponse: no local mtime after write for" << path;
    }
    break;
  }
  case FileOperationStatus::ServerHasNewer: {
    qDebug() << "Server has newer version of:" << path;
    applyServerVersion(path, msg->contents);
    break;
  }
  default: {
    qDebug() << "handleWriteResponse: unhandled status for" << path;
    break;
  }
  }
}

void FileClient::handleDeleteResponse(SyncRequestMessage *msg) {
  QString path = QString::fromStdString(msg->path);
  switch (msg->operationStatus) {
  case FileOperationStatus::Done: {
    qDebug() << "Delete acked for:" << path;
    database.removeFileMtime(username, path);
    break;
  }
  case FileOperationStatus::ServerHasNewer: {
    qDebug() << "Server rejected deletion, restoring:" << path;
    applyServerVersion(path, msg->contents);
    break;
  }
  default: {
    qDebug() << "handleDeleteResponse: unhandled status for" << path;
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

QList<QString> FileClient::discoverNewFiles() {
  if (syncStrategy != SyncStrategy::Naive) {
    // TODO: Merkle Tree discover new files
    return {};
  }
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
    newFiles.append(relativePath);
  }
  return newFiles;
}

QSet<QString> FileClient::discoverDeletedFiles() {
  if (syncStrategy != SyncStrategy::Naive) {
    // TODO: Merkle Tree path
    return {};
  }
  auto tracked = database.allTrackedFiles(username);
  auto fileList = fileStorage->listFiles(username);
  QSet<QString> current(fileList.begin(), fileList.end());
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
  auto found = merkleTree->find(path.toStdString());
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

  ListRequestMessage req;
  req.token = token;
  awaitingListResponse = true;
  transport->send(req);
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
    merkleTree->addFile(path.toStdString(), mtime.value(),
                        hashContents(contents));
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
    merkleTree->deleteFile(path.toStdString(), QDateTime::currentDateTime());
  }

  return deletedAt;
}

void FileClient::handleMerkleSyncResponse(MerkleSyncMessage *msg) {
  qDebug() << "Handling merkle response at client";
  assert(msg->phase != 0 && "Client always initiates the negotiation server "
                            "should respond with phase 1 or 2.");
  merkleSyncClient.handleResponse(toProtocolMessage(*msg), merkleTree.get());
  // if (msg->phase == 2) {
  //   // the server sets phase two when the file entries are empty so we dont
  //   need
  //   // to do anymore work
  //   qDebug() << "The negotiation is now complete";
  //   Q_EMIT negotiationCompleted();
  //   return;
  // }
  // qDebug() << "message from server to client: " << &msg;
  // for (const auto &[parentPath, fileEntries] : msg->fileEntriesPerChild) {
  //   QList<QPair<QString, QByteArray>> clientHashesOfNode;
  //   if (parentPath.isEmpty()) {
  //     // root level, use depth 1
  //     clientHashesOfNode = merkleTree->getHashesAtDepth(1);
  //   } else {
  //     {
  //       auto parent = merkleTree->find(parentPath.toStdString());
  //       assert(
  //           parent.has_value() &&
  //           "The server should not send a parentPath back that the client
  //           does " "not have already, cause the client should not ask for
  //           nodes he " "doesnt have, it just notes them for the sync stage");
  //       auto &[parentNode, isTombstoned] = *parent;
  //       qDebug() << "Parent path is: " << parentNode->path;
  //     }
  //     clientHashesOfNode = merkleTree->getChildHashes(parentPath);
  //   }
  //
  //   QList<QPair<QString, QByteArray>> serverHashesOfNode;
  //   for (const auto &entry : fileEntries) {
  //     serverHashesOfNode.append({entry.path, entry.hash});
  //   }
  //
  //   qDebug() << "Client hashes at this level (count="
  //            << clientHashesOfNode.size() << "):";
  //   for (const auto &[p, h] : clientHashesOfNode) {
  //     qDebug() << "  " << p << "->" << h.toHex().left(16);
  //   }
  //   qDebug() << "Server hashes at this level (count="
  //            << serverHashesOfNode.size() << "):";
  //   for (const auto &[p, h] : serverHashesOfNode) {
  //     qDebug() << "  " << p << "->" << h.toHex().left(16);
  //   }
  //
  //   auto diff =
  //       MerkleTree::symmetricHashDiff(clientHashesOfNode,
  //       serverHashesOfNode);
  //
  //   auto findServerEntry =
  //       [&fileEntries](const QString &path) -> std::optional<MerkleEntry> {
  //     for (const auto &entry : fileEntries) {
  //       if (entry.path == path) {
  //         return entry;
  //       }
  //     }
  //     return {};
  //   };
  //   auto addFilesFromClientInNegotiation = [&](const QList<QString> &paths) {
  //     for (const auto &entry : paths) {
  //       auto foundNode = merkleTree->find(entry.toStdString());
  //       assert(foundNode.has_value());
  //       auto &[node, isTombstoned] = *foundNode;
  //       if (isTombstoned) {
  //         negotiationState.diffEntries.deletionWinsLeft.append(
  //             {entry, node->deletedAt});
  //       } else {
  //         negotiationState.diffEntries.onlyInLeft.append(
  //             {node->type == FileType::File, entry});
  //       }
  //     }
  //   };
  //   auto addFilesFromServerInNegotiation =
  //       [&](const QList<QString> &paths,
  //           const QList<MerkleEntry> &fileEntries) {
  //         for (const auto &entry : paths) {
  //           auto serverEntry = findServerEntry(entry);
  //           if (!serverEntry.has_value()) {
  //             qDebug() << "Server reported diff for path not in entries:"
  //                      << entry;
  //             continue;
  //           }
  //
  //           if (serverEntry->isTombstone) {
  //             // Server has tombstoned, client doesn't know about path at all
  //             negotiationState.diffEntries.deletionWinsRight.append(
  //                 {entry, serverEntry->deletedAt});
  //           } else {
  //             negotiationState.diffEntries.onlyInRight.append(
  //                 {serverEntry->filetype == FileType::File, entry});
  //           }
  //         }
  //       };
  //   auto addModifiedFromNegotiation =
  //       [&](const QList<QString> &paths,
  //           const QList<MerkleEntry> &fileEntries) {
  //         for (const auto &entry : paths) {
  //           auto foundNode = merkleTree->find(entry.toStdString());
  //           assert(foundNode.has_value());
  //           auto &[node, clientHasTombstone] = *foundNode;
  //
  //           auto serverEntry = findServerEntry(entry);
  //           if (!serverEntry.has_value()) {
  //             qDebug() << "Server reported modified path not in entries:"
  //                      << entry;
  //             continue;
  //           }
  //
  //           qDebug() << "Tombstone comparison: serverDeletedAt="
  //                    << serverEntry->deletedAt << "clientMtime=" <<
  //                    node->mtime
  //                    << "result=" << (serverEntry->deletedAt > node->mtime);
  //           bool serverIsTombstone = serverEntry->isTombstone;
  //
  //           if (clientHasTombstone && serverIsTombstone) {
  //             // Both tombstoned, no diff
  //             continue;
  //           }
  //
  //           if (clientHasTombstone && !serverIsTombstone) {
  //             qDebug() << "Branch taken: clientTombstone=true, serverAlive."
  //                      << "node->deletedAt=" << node->deletedAt
  //                      << "serverEntry->mtime=" << serverEntry->mtime
  //                      << "deletion newer? "
  //                      << (node->deletedAt > serverEntry->mtime);
  //             // Client deletion vs server alive — compare timestamps
  //             if (node->deletedAt > serverEntry->mtime) {
  //               // Deletion is newer → propagate delete to server
  //               negotiationState.diffEntries.deletionWinsLeft.append(
  //                   {entry, node->deletedAt});
  //             } else {
  //               // Server's content is newer than client's deletion →
  //               resurrect
  //               // on client
  //               negotiationState.diffEntries.onlyInRight.append(
  //                   {serverEntry->filetype == FileType::File, entry});
  //             }
  //             continue;
  //           }
  //
  //           if (!clientHasTombstone && serverIsTombstone) {
  //             if (serverEntry->deletedAt > node->mtime) {
  //               negotiationState.diffEntries.deletionWinsRight.append(
  //                   {entry, serverEntry->deletedAt});
  //             } else {
  //               negotiationState.diffEntries.onlyInLeft.append(
  //                   {node->type == FileType::File, entry});
  //             }
  //             continue;
  //           }
  //
  //           // Both alive
  //           if (node->type == FileType::Directory) {
  //             negotiationState.directoriesToCheckWithServer.append(entry);
  //           } else {
  //             negotiationState.diffEntries.modified.append(entry);
  //           }
  //         }
  //       };
  //   addFilesFromClientInNegotiation(diff.onlyInLeft);
  //   addFilesFromServerInNegotiation(diff.onlyInRight, fileEntries);
  //   addModifiedFromNegotiation(diff.modified, fileEntries);
  // }
  // if (!negotiationState.directoriesToCheckWithServer.isEmpty()) {
  //   MerkleSyncMessage nextMsg;
  //   nextMsg.token = token;
  //   nextMsg.phase = 1;
  //   nextMsg.depth = msg->depth + 1;
  //
  //   for (const auto &dirPath : negotiationState.directoriesToCheckWithServer)
  //   {
  //     nextMsg.fileEntriesPerChild.append({dirPath, {}});
  //   }
  //
  //   negotiationState.directoriesToCheckWithServer.clear();
  //   transport->send(nextMsg);
  //   return;
  // }
  // Q_EMIT negotiationCompleted();
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
  auto mtime = database.readMtime(username, path);
  SyncRequestMessage msg;
  msg.token = token;
  msg.path = path.toStdString();
  msg.contents = {};
  msg.operationTime = deletedAt;
  msg.operationType = FileOperationType::Delete;
  msg.operationStatus = FileOperationStatus::DoIt;
  commandsToSend.insert(path, msg);
}

SyncRequestMessage
FileClient::buildSyncRequest(const QString &path, FileOperationType op,
                             const QByteArray &contents,
                             const std::optional<QDateTime> &mtime) {
  SyncRequestMessage msg;
  msg.token = token;
  msg.path = path.toStdString();
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

  inMerkleApply = true;
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
      auto found = merkleTree->find(path.toStdString());
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
    // Server tombstoned, client should apply deletion locally
    if (fileStorage->readFile(username, path).has_value()) {
      fileStorage->deleteFile(username, path);
    }
    database.removeFileMtime(username, path);
    if (merkleTree) {
      merkleTree->deleteFile(path.toStdString(), deletedAt);
    }
    qDebug() << "Applied server tombstone via merkle negotiation:" << path;
  }

  if (pendingDirectoryRequests == 0) {
    inMerkleApply = false;
    Q_EMIT outboundFileCommandsReady();
    checkSyncCompletionAndUnlock();
  }
}

void FileClient::merkleTick() {
  qDebug() << "Merkle tick";
  if (currentlyNegotiatingFileDiffs) {
    qDebug() << "There is an existing negotiation going on";
    return;
  }
  currentlyNegotiatingFileDiffs = true;
  merkleSyncClient.startNegotiation(merkleTree.get());
  // negotiationState = NegotiationState{};
  // MerkleSyncMessage msg;
  // msg.token = token;
  // msg.phase = 0;
  // msg.rootHash = merkleTree->rootHash();
  // msg.depth = 0;
  // qDebug() << "Initiating negotiation with msg: " << msg;
  // transport->send(msg);
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
