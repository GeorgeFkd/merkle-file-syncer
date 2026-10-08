#include "LocalFileStorage.h"
#include <QDateTime>
#include <QHash>
#include <QString>

// A snapshot of how the local filesystem differs from the last-known tracked
// state. New/modified carry the filesystem mtime; deleted carry the time the
// deletion was detected.
using FileChangeMetadata = QPair<QString, QDateTime>;

// A file whose inode matches a tracked entry that vanished from `oldPath` -
// i.e. it was moved/renamed rather than deleted+recreated. mtime is the
// filesystem mtime at the new path.
struct RenamedFileMetadata {
  QString oldPath;
  QString newPath;
  QDateTime mtime;
};

struct LocalChangeSet {
  QList<FileChangeMetadata> newFiles;
  QList<FileChangeMetadata> modifiedFiles;
  QList<FileChangeMetadata> deletedFiles;
  QList<RenamedFileMetadata> renamedFiles;
  // Directories whose whole subtree moved at once (see
  // scanFilesystemForChanges). Any file-level rename that is just a
  // descendant of one of these is folded into it and does NOT also appear in
  // renamedFiles.
  QList<RenamedFileMetadata> renamedDirectories;
};

// the state should basically be: struct {
//  filename,mtime,localInode,isDir,isTombstoned
// }
struct TrackedFileState {
  QString filename;
  QDateTime mtime;
  quint64 localInode = 0;
  bool isDir = false;
  bool isTombstoned = false;
};

// File renames are detected by matching localInode across a "missing" tracked
// entry and a "new" file found on disk (see scanFilesystemForChanges).
// Directory renames are detected the same way, against tracked entries with
// isDir=true: renaming a directory doesn't change the inode of the directory
// itself or of anything inside it (only the containing directory entries
// change), so a moved directory is found by matching its own inode, exactly
// like a file. Once a directory rename is found, any file (or nested
// directory) rename that is simply a descendant of it is folded into that
// single directory-rename entry instead of being reported again on its own.
// Directory entries are still never reported as plain "new"/"deleted" -
// only an inode-matched move is reported for them.
//
// Things needed:
// what we need is: The tracked state,
// and then we diff it with the actual fs state
struct State {
  QHash<QString, TrackedFileState> entries;
};

class FSScanner {

public:
  // "user" isn't a filesystem path - it is the namespace passed to
  // LocalFileStorage, which already knows the actual root path on disk for
  // that user. That's why no rootDir needs to be passed in separately.
  LocalChangeSet scanFilesystemForChanges(const QString &user,
                                          const State &state);

  explicit FSScanner(LocalFileStorage *storage);

private:
  QHash<QString, QDateTime> getMtimesFromStorage(const QString &user);
  LocalFileStorage *storage;

  using DiscoverResult =
      QPair<QList<QString>, QList<QPair<QString, QDateTime>>>;
  using INodeT = quint64;

  const INodeT MISSING_INODE_SENTINEL = 0;

  DiscoverResult
  findNewAndModifiedFiles(const QString &user,
                          const QHash<QString, QDateTime> &currentMtimes,
                          const State &state);

  using INodeHasHasNotSplitResult =
      QPair<QList<QString>, QHash<INodeT, QString>>;
  INodeHasHasNotSplitResult
  splitNodesBasedOnINodeExists(const State &state,
                               const QHash<QString, QDateTime> &currentMtimes);
};
