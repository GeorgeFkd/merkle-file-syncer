#include "MerkleSyncServer.h"
#include "FileTree.h"
#include "MerkleTree.h"
#include "Messages.h"
MerkleSyncServer::MerkleSyncServer(QObject *parent) : QObject(parent) {}

void MerkleSyncServer::handleRequest(const MerkleProtocolMessage &msg,
                                     MerkleTree *tree, ConnectionId conn) {
  MerkleProtocolMessage response;

  auto toEntries = [tree](const QList<QPair<QString, QByteArray>> &pathHashes) {
    QList<MerkleEntry> entries;
    for (const auto &[path, hash] : pathHashes) {
      auto found = tree->find(path.toStdString());
      if (!found.has_value())
        continue;
      auto &[node, isTombstoned] = *found;
      MerkleEntry entry;
      entry.path = path;
      entry.hash = hash;
      entry.mtime = node->mtime;
      entry.filetype = node->type;
      entry.isTombstone = isTombstoned;
      entry.deletedAt = node->deletedAt;
      entries.append(entry);
    }
    return entries;
  };

  if (msg.phase == 0) {
    if (tree->rootHash() == msg.rootHash) {
      response.phase = 2;
      Q_EMIT messageSendRequest(conn, response);
      return;
    }
    response.phase = 1;
    response.depth = 1;
    auto rootPath = QString("");
    response.fileEntriesPerChild.append(
        {rootPath, toEntries(tree->getChildHashes(rootPath))});
    Q_EMIT messageSendRequest(conn, response);
    return;
  }

  response.phase = 1;
  response.depth = msg.depth;
  for (const auto &[parentPath, _] : msg.fileEntriesPerChild) {
    auto found = tree->find(parentPath.toStdString());
    assert(found.has_value());
    auto &[node, isTombstoned] = *found;
    assert(node->type == FileType::Directory);
    response.fileEntriesPerChild.append(
        {parentPath, toEntries(tree->getChildHashes(parentPath))});
  }

  if (response.fileEntriesPerChild.isEmpty()) {
    response.phase = 2;
  }
  Q_EMIT messageSendRequest(conn, response);
}
