#pragma once

#include "FileTree.h"
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include "Messages.h"
struct DeletionEntry {
  QString path;
  QDateTime deletedAt;
};

struct NodesDiff {
  QList<QPair<bool, QString>> onlyInLeft;
  QList<QPair<bool, QString>> onlyInRight;
  QList<QString> modified;
  QList<DeletionEntry> deletionWinsLeft;
  QList<DeletionEntry> deletionWinsRight;
};

struct NegotiationState {
  NodesDiff diffEntries;
  QList<QString> directoriesToCheckWithServer;
};

// struct MerkleEntry {
//   QString path;
//   QByteArray hash;
//   QDateTime mtime;
//   FileType filetype;
//   bool isTombstone;
//   QDateTime deletedAt;
// };

struct MerkleProtocolMessage {
  int phase;
  int depth;
  QByteArray rootHash;
  QList<QPair<QString, QList<MerkleEntry>>> fileEntriesPerChild;
};


inline MerkleSyncMessage toWireMessage(MerkleProtocolMessage m) {
  MerkleSyncMessage w;
  w.phase = m.phase;
  w.depth = m.depth;
  w.rootHash = std::move(m.rootHash);
  w.fileEntriesPerChild = std::move(m.fileEntriesPerChild);
  return w;
}

inline MerkleProtocolMessage toProtocolMessage(MerkleSyncMessage w) {
  MerkleProtocolMessage m;
  m.phase = w.phase;
  m.depth = w.depth;
  m.rootHash = std::move(w.rootHash);
  m.fileEntriesPerChild = std::move(w.fileEntriesPerChild);
  return m;
}
