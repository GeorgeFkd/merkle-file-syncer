#pragma once

#include "FileTree.h"
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include "Messages.h"

struct MerkleProtocolMessage {
  int phase;
  int depth;
  QByteArray rootHash;
  //Ideally i would want it to be Map<QString,QList<MerkleEntry>>
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

inline QDebug operator<<(QDebug debug, const DeletionEntry &entry)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "DeletionEntry(path=" << entry.path
                    << ", deletedAt=" << entry.deletedAt << ")";
    return debug;
}
