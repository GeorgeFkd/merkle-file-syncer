#pragma once
#include <QByteArray>
#include <QDateTime>

// State of one path on one side (client=left, server=right), in a form both the
// naive and merkle traversals can produce. Pure data — no DB, tree, or wire.
struct SideState {
  std::optional<QDateTime> mtime;     // set iff present && live
  std::optional<QDateTime> deletedAt; // set iff present && tombstoned
  bool isDirectory = false;           // meaningful only when live
  QByteArray hash;                    // content hash, when live

  bool present() const;
  bool isTombstone() const;
  bool isLive() const;
};

enum class DiffBucket {
  InSync,              // identical, nothing to do
  OnlyInLeft,          // only client has it -> upload
  OnlyInRight,         // only server has it -> download
  ModifiedWinsLeft,    // both live, differ, client newer -> push client's version
  ModifiedWinsRight,   // both live, differ, server newer -> pull server's version
  DeletionWinsLeft,    // client's deletion wins -> propagate delete to server
  DeletionWinsRight,   // server's deletion wins -> apply delete locally
  NeedsDirectoryCheck, // both live directories, differ -> recurse
};

// Pure decision: given both sides' state for a single path, which bucket.
// `left` is the client, `right` is the server.
DiffBucket classifyPath(const SideState &left, const SideState &right);
