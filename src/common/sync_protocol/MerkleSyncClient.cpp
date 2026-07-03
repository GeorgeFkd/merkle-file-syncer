#include "MerkleSyncClient.h"
#include "FileTree.h"
#include "MerkleProtocolMessages.h"
MerkleSyncClient::MerkleSyncClient(QObject *parent) : QObject(parent) {}

const NegotiationState* MerkleSyncClient::getNegotiationState() const {
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
    if (parentPath.isEmpty()) {
      clientHashesOfNode = tree->getHashesAtDepth(1);
    } else {
      auto parent = tree->find(parentPath.toStdString());
      assert(parent.has_value());
      clientHashesOfNode = tree->getChildHashes(parentPath);
    }

    QList<QPair<QString, QByteArray>> serverHashesOfNode;
    for (const auto &entry : fileEntries) {
      serverHashesOfNode.append({entry.path, entry.hash});
    }

    auto diff = MerkleTree::symmetricHashDiff(clientHashesOfNode,
                                              serverHashesOfNode);

    auto findServerEntry =
        [&fileEntries](const QString &path) -> std::optional<MerkleEntry> {
      for (const auto &entry : fileEntries) {
        if (entry.path == path) return entry;
      }
      return {};
    };

    auto addFilesFromClientInNegotiation = [&](const QList<QString> &paths) {
      for (const auto &entry : paths) {
        auto foundNode = tree->find(entry.toStdString());
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
        auto serverEntry = findServerEntry(entry);
        if (!serverEntry.has_value()) continue;
        if (serverEntry->isTombstone) {
          negotiationState.diffEntries.deletionWinsRight.append(
              {entry, serverEntry->deletedAt});
        } else {
          negotiationState.diffEntries.onlyInRight.append(
              {serverEntry->filetype == FileType::File, entry});
        }
      }
    };

    auto addModifiedFromNegotiation = [&](const QList<QString> &paths) {
      for (const auto &entry : paths) {
        auto foundNode = tree->find(entry.toStdString());
        assert(foundNode.has_value());
        auto &[node, clientHasTombstone] = *foundNode;

        auto serverEntry = findServerEntry(entry);
        if (!serverEntry.has_value()) continue;
        bool serverIsTombstone = serverEntry->isTombstone;

        if (clientHasTombstone && serverIsTombstone) continue;

        if (clientHasTombstone && !serverIsTombstone) {
          if (node->deletedAt > serverEntry->mtime) {
            negotiationState.diffEntries.deletionWinsLeft.append(
                {entry, node->deletedAt});
          } else {
            negotiationState.diffEntries.onlyInRight.append(
                {serverEntry->filetype == FileType::File, entry});
          }
          continue;
        }

        if (!clientHasTombstone && serverIsTombstone) {
          if (serverEntry->deletedAt > node->mtime) {
            negotiationState.diffEntries.deletionWinsRight.append(
                {entry, serverEntry->deletedAt});
          } else {
            negotiationState.diffEntries.onlyInLeft.append(
                {node->type == FileType::File, entry});
          }
          continue;
        }

        if (node->type == FileType::Directory) {
          negotiationState.directoriesToCheckWithServer.append(entry);
        } else {
          negotiationState.diffEntries.modified.append(entry);
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
