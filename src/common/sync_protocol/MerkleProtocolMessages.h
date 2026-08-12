#pragma once

#include "FileTree.h"
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include "Messages.h"
#include <memory>

struct MerkleProtocolMessage {
  int phase;
  int depth;
  QByteArray rootHash;
  //Ideally i would want it to be Map<QString,QList<MerkleEntry>>
  QList<QPair<QString, QList<MerkleEntry>>> fileEntriesPerChild;
};


inline std::shared_ptr<MerkleSyncMessage> toWireMessage(MerkleProtocolMessage* m) {
  return std::make_shared<MerkleSyncMessage>(m->phase,m->depth,std::move(m->rootHash),std::move(m->fileEntriesPerChild));
}

inline std::shared_ptr<MerkleProtocolMessage> toProtocolMessage(MerkleSyncMessage w) {
  auto m = std::make_shared<MerkleProtocolMessage>();
  m->phase = w.phase;
  m->depth = w.depth;
  m->rootHash = std::move(w.rootHash);
  m->fileEntriesPerChild = std::move(w.fileEntriesPerChild);
  return m;
}

inline QDebug operator<<(QDebug debug, const DeletionEntry &entry)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "DeletionEntry(path=" << entry.path
                    << ", deletedAt=" << entry.deletedAt << ")";
    return debug;
}
