#include "MerkleSyncClient.h"
#include "FileTree.h"
#include "MerkleProtocolMessages.h"
MerkleSyncClient::MerkleSyncClient(QObject *parent) : QObject(parent) {}

const NegotiationState *MerkleSyncClient::getNegotiationState() const {
  return &negotiationState;
}

void MerkleSyncClient::startNegotiation(MerkleTree *tree) {
  negotiationState = NegotiationState{};
  inProgress = true;
  MerkleProtocolMessage msg;
  msg.phase = 0;
  msg.depth = 0;
  msg.rootHash = tree->rootHash();
  Q_EMIT messageSendRequest(msg);
}

void MerkleSyncClient::handleResponse(const MerkleProtocolMessage &msg,
                                      MerkleTree *tree) {
  if (msg.phase == 2) {
    inProgress = false;
    Q_EMIT negotiationCompleted(negotiationState);
    return;
  }

  for (const auto &[parentPath, fileEntries] : msg.fileEntriesPerChild) {
    QList<QPair<QString, QByteArray>> clientHashesOfNode;
    auto parent = tree->find(parentPath);
    assert(parent.has_value());
    clientHashesOfNode = tree->getChildHashes(parentPath);

    QHash<QString, MerkleEntry> serverByPath;
    QList<QPair<QString, QByteArray>> serverHashesOfNode;
    for (const auto &entry : fileEntries) {
      serverHashesOfNode.append({entry.path, entry.hash});
      serverByPath.insert(entry.path, entry);
    }

    auto diff =
        MerkleTree::symmetricHashDiff(clientHashesOfNode, serverHashesOfNode);

    auto addFilesFromClientInNegotiation = [&](const QList<QString> &paths) {
      for (const auto &entry : paths) {
        auto foundNode = tree->find(entry);
        assert(foundNode.has_value());
        auto &[node, isTombstoned] = *foundNode;
        if (isTombstoned) {
          negotiationState.diffEntries.deletionWinsLeft.append(
              {entry, node->deletedAt});
        } else {
          negotiationState.diffEntries.onlyInLeft.append(
              {node->type == FileType::File, entry});
        }
      }
    };

    auto addFilesFromServerInNegotiation = [&](const QList<QString> &paths) {
      for (const auto &entry : paths) {
        auto it = serverByPath.constFind(entry);
        assert(it != serverByPath.cend());
        const MerkleEntry &serverEntry = *it;
        if (serverEntry.isTombstone) {
          negotiationState.diffEntries.deletionWinsRight.append(
              {entry, serverEntry.deletedAt});
        } else {
          negotiationState.diffEntries.onlyInRight.append(
              {serverEntry.filetype == FileType::File, entry});
        }
      }
    };

    auto addModifiedFromNegotiation = [&](const QList<QString> &paths) {
      for (const auto &entry : paths) {
        auto foundNode = tree->find(entry);
        assert(foundNode.has_value());
        auto &[node, clientHasTombstone] = *foundNode;
        auto it = serverByPath.constFind(entry);
        assert(it != serverByPath.cend());
        const MerkleEntry &serverEntry = *it;

        bool serverHasTombstone = serverEntry.isTombstone;

        bool bothHaveTombstone = clientHasTombstone && serverHasTombstone;
        if (bothHaveTombstone)
          continue;

        bool onlyClientHasTombstone = clientHasTombstone && !serverHasTombstone;
        if (onlyClientHasTombstone) {
          bool clientDeletionLaterThanServerTime =
              node->deletedAt > serverEntry.mtime;
          if (clientDeletionLaterThanServerTime) {
            negotiationState.diffEntries.deletionWinsLeft.append(
                {entry, node->deletedAt});
          } else {
            negotiationState.diffEntries.onlyInRight.append(
                {serverEntry.filetype == FileType::File, entry});
          }
          continue;
        }

        bool onlyServerHasTombstone = !clientHasTombstone && serverHasTombstone;
        if (onlyServerHasTombstone) {
          auto serverDeletionLaterThanLocalMtime =
              serverEntry.deletedAt > node->mtime;
          if (serverDeletionLaterThanLocalMtime) {
            negotiationState.diffEntries.deletionWinsRight.append(
                {entry, serverEntry.deletedAt});
          } else {
            negotiationState.diffEntries.onlyInLeft.append(
                {node->type == FileType::File, entry});
          }
          continue;
        }
        bool noTombstones = !clientHasTombstone && !serverHasTombstone;
        if (noTombstones) {
          if (node->type == FileType::Directory) {
            negotiationState.directoriesToCheckWithServer.append(entry);
          } else {
            negotiationState.diffEntries.modified.append(entry);
          }
        }
      }
    };

    addFilesFromClientInNegotiation(diff.onlyInLeft);
    addFilesFromServerInNegotiation(diff.onlyInRight);
    addModifiedFromNegotiation(diff.modified);
  }

  if (!negotiationState.directoriesToCheckWithServer.isEmpty()) {
    MerkleProtocolMessage nextMsg;
    nextMsg.phase = 1;
    nextMsg.depth = msg.depth + 1;
    for (const auto &dirPath : negotiationState.directoriesToCheckWithServer) {
      nextMsg.fileEntriesPerChild.append({dirPath, {}});
    }
    negotiationState.directoriesToCheckWithServer.clear();
    Q_EMIT messageSendRequest(nextMsg);
    return;
  }

  inProgress = false;
  Q_EMIT negotiationCompleted(negotiationState);
}
