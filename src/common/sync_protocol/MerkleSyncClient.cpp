#include "MerkleSyncClient.h"
#include "MerkleProtocolMessages.h"
#include "SyncCommon.h"
MerkleSyncClient::MerkleSyncClient(QObject *parent) : QObject(parent) {}

const NegotiationState *MerkleSyncClient::getNegotiationState() const {
  return &negotiationState;
}

void MerkleSyncClient::startNegotiation(MerkleTree *tree) {
  negotiationState = NegotiationState{};
  inProgress = true;
  auto msg = std::make_shared<MerkleProtocolMessage>();
  msg->phase = 0;
  msg->depth = 0;
  msg->rootHash = tree->rootHash();
  Q_EMIT messageSendRequest(msg);
}

void MerkleSyncClient::onMessage(std::shared_ptr<MerkleProtocolMessage> msg,
                                      MerkleTree *tree) {
  if (msg->phase == 2) {
    inProgress = false;
    Q_EMIT negotiationCompleted(negotiationState);
    return;
  }

  for (const auto &[parentPath, fileEntries] : msg->fileEntriesPerChild) {
    auto parent = tree->find(parentPath);
    assert(parent.has_value());
    auto clientHashesOfNode = tree->getChildHashes(parentPath);

    QHash<QString, MerkleEntry> serverByPath;
    QList<QPair<QString, QByteArray>> serverHashesOfNode;
    for (const auto &entry : fileEntries) {
      serverHashesOfNode.append({entry.path, entry.hash});
      serverByPath.insert(entry.path, entry);
    }

    auto diff =
        MerkleTree::symmetricHashDiff(clientHashesOfNode, serverHashesOfNode);

    // --- adapters: node/entry -> SideState ---
    auto clientState = [&](const QString &path) {
      SideState s;
      auto found = tree->find(path);
      if (!found.has_value())
        return s; // absent
      auto &[node, isTombstoned] = *found;
      if (isTombstoned) {
        s.deletedAt = node->deletedAt;
      } else {
        s.mtime = node->mtime;
        s.isDirectory = (node->type == FileType::Directory);
        // s.hash = node->hash;  // available; classifier doesn't use it yet
      }
      return s;
    };

    auto serverState = [&](const QString &path) {
      SideState s;
      auto it = serverByPath.constFind(path);
      if (it == serverByPath.cend())
        return s; // absent
      const MerkleEntry &e = *it;
      if (e.isTombstone) {
        s.deletedAt = e.deletedAt;
      } else {
        s.mtime = e.mtime;
        s.isDirectory = (e.filetype == FileType::Directory);
        // s.hash = e.hash;
      }
      return s;
    };

    // isFile flag for onlyIn* entries, taken from whichever side has the live
    // node
    auto isFileFor = [&](const SideState &left, const SideState &right) {
      if (left.isLive())
        return !left.isDirectory;
      if (right.isLive())
        return !right.isDirectory;
      return true; // both tombstones — flag unused by deletion buckets
    };

    auto classifyAndAppend = [&](const QString &path) {
      SideState left = clientState(path);
      SideState right = serverState(path);
      switch (classifyPath(left, right)) {
      case DiffBucket::InSync:
        break;
      case DiffBucket::OnlyInLeft:
        negotiationState.diffEntries.onlyInLeft.append(
            {isFileFor(left, right), path});
        break;
      case DiffBucket::OnlyInRight:
        negotiationState.diffEntries.onlyInRight.append(
            {isFileFor(left, right), path});
        break;
      case DiffBucket::ModifiedWinsLeft:
        // negotiationState.diffEntries.modified.append(path);
        negotiationState.diffEntries.modifiedWinsLeft.append(path);
        break;
      case DiffBucket::ModifiedWinsRight:
        // TODO: split into directional buckets once the applier consumes them.
        // negotiationState.diffEntries.modified.append(path);
        negotiationState.diffEntries.modifiedWinsRight.append(path);
        break;
      case DiffBucket::DeletionWinsLeft:
        negotiationState.diffEntries.deletionWinsLeft.append(
            {path, left.deletedAt.value()});
        break;
      case DiffBucket::DeletionWinsRight:
        negotiationState.diffEntries.deletionWinsRight.append(
            {path, right.deletedAt.value()});
        break;
      case DiffBucket::NeedsDirectoryCheck:
        negotiationState.directoriesToCheckWithServer.append(path);
        break;
      }
    };

    for (const auto &path : diff.onlyInLeft)
      classifyAndAppend(path);
    for (const auto &path : diff.onlyInRight)
      classifyAndAppend(path);
    for (const auto &path : diff.modified)
      classifyAndAppend(path);
  }
  if (!negotiationState.directoriesToCheckWithServer.isEmpty()) {
    auto nextMsg = std::make_shared<MerkleProtocolMessage>();
    nextMsg->phase = 1;
    nextMsg->depth = msg->depth + 1;
    for (const auto &dirPath : negotiationState.directoriesToCheckWithServer) {
      nextMsg->fileEntriesPerChild.append({dirPath, {}});
    }
    negotiationState.directoriesToCheckWithServer.clear();
    Q_EMIT messageSendRequest(nextMsg);
    return;
  }

  inProgress = false;
  Q_EMIT negotiationCompleted(negotiationState);
}
