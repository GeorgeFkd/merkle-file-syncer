#include "SyncCommon.h"
#include <QDebug>

bool SideState::present() const {
  return mtime.has_value() || deletedAt.has_value();
}

bool SideState::isTombstone() const { return deletedAt.has_value(); }

bool SideState::isLive() const { return mtime.has_value(); }

DiffBucket classifyPath(const SideState &left, const SideState &right) {
  if (!left.present() && !right.present()) {
    return DiffBucket::InSync;
  }
  if (left.present() && !right.present()) {
    // a client tombstone the server never knew about is a no-op; a live file
    // is an upload.
    return left.isTombstone() ? DiffBucket::InSync : DiffBucket::OnlyInLeft;
  }
  if (!left.present() && right.present()) {
    return right.isTombstone() ? DiffBucket::InSync : DiffBucket::OnlyInRight;
  }

  // --- both sides know the path ---
  const bool leftTomb = left.isTombstone();
  const bool rightTomb = right.isTombstone();

  if (leftTomb && rightTomb) {
    return DiffBucket::InSync; // both deleted -> agree
  }

  if (leftTomb && !rightTomb) {
    // client deleted, server has a live version.
    return (left.deletedAt.value() > right.mtime.value())
               ? DiffBucket::DeletionWinsLeft
               : DiffBucket::OnlyInRight;
  }

  if (!leftTomb && rightTomb) {
    // server deleted, client has a live version.
    return (right.deletedAt.value() > left.mtime.value())
               ? DiffBucket::DeletionWinsRight
               : DiffBucket::OnlyInLeft;
  }

  // --- both live ---

  if (left.isDirectory && right.isDirectory) {
    // directories are further recursed
    return DiffBucket::NeedsDirectoryCheck;
  }

  if (!left.hash.isEmpty() && left.hash == right.hash) {
    return DiffBucket::InSync; // same content, regardless of mtime
  }

  if (left.mtime.value() > right.mtime.value()) {
    return DiffBucket::ModifiedWinsLeft; // client newer -> push
  } else if (right.mtime.value() > left.mtime.value()) {
    return DiffBucket::ModifiedWinsRight;
  } else {
    if (left.mtime.value() == right.mtime.value()) {
      qWarning()
          << "classifyPath: equal mtime, differing content — unresolvable at "
             "millisecond resolution, applying deterministic tiebreak";
    }
    return DiffBucket::ModifiedWinsRight;
  }
}
