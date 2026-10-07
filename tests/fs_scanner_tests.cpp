#include "FSScanner.h"
#include "LocalFileStorage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <vector>

// Seconds by which a "modified" file's mtime is advanced past the tracked
// baseline so the change is always observable by the scanner, despite:
//   - tick-granular in-memory inode timestamps: successive writes within one
//     kernel tick otherwise share an mtime (Linux stamps mtime from a coarse
//     clock, ~1-10ms depending on CONFIG_HZ), and
//   - filesystems whose on-disk mtime granularity is >= 1s (FAT is 2s).
// The build system may override this for targets that need a larger margin,
// e.g. -DFS_MTIME_GRANULARITY_SECONDS=4.
#ifndef FS_MTIME_GRANULARITY_SECONDS
#define FS_MTIME_GRANULARITY_SECONDS 1
#endif

class FSScannerTest : public ::testing::Test {
protected:
  void SetUp() override {
    runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rootDir = QDir(QDir::tempPath() + "/test_fs_scanner/" + runId);
    QDir().mkpath(rootDir.path());

    storage = std::make_unique<LocalFileStorage>();
    storage->setRoot(rootDir.path());
    storage->cleanup(user);

    scanner = std::make_unique<FSScanner>(storage.get());
  }

  void TearDown() override {
    storage->cleanup(user);
    QDir(rootDir.path()).removeRecursively();
  }

  // Records a file's actual on-disk mtime into a State so tests can build
  // "already tracked, unchanged" scenarios without guessing mtimes.
  TrackedFileState trackedEntryFor(const QString &path) {
    auto mtime = storage->getMtime(user, path);
    EXPECT_TRUE(mtime.has_value());
    TrackedFileState entry;
    entry.filename = path;
    entry.mtime = mtime.value_or(QDateTime());
    return entry;
  }

  QString runId;
  QDir rootDir;
  QString user = "alice";
  std::unique_ptr<LocalFileStorage> storage;
  std::unique_ptr<FSScanner> scanner;
};

TEST_F(FSScannerTest, emptyFsAndEmptyStateProducesNoChanges) {
  State state;
  auto changes = scanner->scanFilesystemForChanges(user, state);
  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, fileOnDiskNotInStateIsReportedAsNew) {
  storage->writeFile(user, "a.txt", "hello");

  State state;
  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_EQ(changes.newFiles.size(), 1);
  ASSERT_EQ(changes.newFiles[0].first, "a.txt");
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, fileMatchingStateProducesNoChange) {
  storage->writeFile(user, "a.txt", "hello");

  State state;
  state.entries.insert("a.txt", trackedEntryFor("a.txt"));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, fileWithDifferentMtimeInStateIsReportedAsModified) {
  storage->writeFile(user, "a.txt", "hello");

  State state;
  TrackedFileState entry;
  entry.filename = "a.txt";
  entry.mtime = QDateTime::fromSecsSinceEpoch(0);
  state.entries.insert("a.txt", entry);

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_EQ(changes.modifiedFiles.size(), 1);
  ASSERT_EQ(changes.modifiedFiles[0].first, "a.txt");
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, trackedFileMissingFromDiskIsReportedAsDeleted) {
  State state;
  TrackedFileState entry;
  entry.filename = "gone.txt";
  entry.mtime = QDateTime::currentDateTime();
  state.entries.insert("gone.txt", entry);

  auto before = QDateTime::currentDateTime();
  auto changes = scanner->scanFilesystemForChanges(user, state);
  auto after = QDateTime::currentDateTime();

  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_EQ(changes.deletedFiles.size(), 1);
  ASSERT_EQ(changes.deletedFiles[0].first, "gone.txt");
  ASSERT_GE(changes.deletedFiles[0].second, before);
  ASSERT_LE(changes.deletedFiles[0].second, after);
}

TEST_F(FSScannerTest, tombstonedEntryMissingFromDiskIsNotReportedAgain) {
  State state;
  TrackedFileState entry;
  entry.filename = "already_deleted.txt";
  entry.mtime = QDateTime::currentDateTime();
  entry.isTombstoned = true;
  state.entries.insert("already_deleted.txt", entry);

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, tombstonedEntryThatReappearsIsReportedAsNew) {
  storage->writeFile(user, "resurrected.txt", "back");

  State state;
  TrackedFileState entry;
  entry.filename = "resurrected.txt";
  entry.mtime = QDateTime::fromSecsSinceEpoch(0);
  entry.isTombstoned = true;
  state.entries.insert("resurrected.txt", entry);

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_EQ(changes.newFiles.size(), 1);
  ASSERT_EQ(changes.newFiles[0].first, "resurrected.txt");
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, directoryEntryMissingFromDiskIsNotReportedAsDeleted) {
  State state;
  TrackedFileState entry;
  entry.filename = "some_dir";
  entry.mtime = QDateTime::currentDateTime();
  entry.isDir = true;
  state.entries.insert("some_dir", entry);

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, nestedPathsAreHandledCorrectly) {
  storage->writeFile(user, "a/b/c.txt", "nested");

  State state;
  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_EQ(changes.newFiles.size(), 1);
  ASSERT_EQ(changes.newFiles[0].first, "a/b/c.txt");
}

TEST_F(FSScannerTest,
       mixOfNewModifiedDeletedAndUnchangedIsClassifiedCorrectly) {
  storage->writeFile(user, "unchanged.txt", "same");
  storage->writeFile(user, "changed.txt", "new contents");
  storage->writeFile(user, "brand_new.txt", "just added");

  State state;
  state.entries.insert("unchanged.txt", trackedEntryFor("unchanged.txt"));

  TrackedFileState changedEntry;
  changedEntry.filename = "changed.txt";
  changedEntry.mtime = QDateTime::fromSecsSinceEpoch(0);
  state.entries.insert("changed.txt", changedEntry);

  TrackedFileState missingEntry;
  missingEntry.filename = "removed.txt";
  missingEntry.mtime = QDateTime::currentDateTime();
  state.entries.insert("removed.txt", missingEntry);

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_EQ(changes.newFiles.size(), 1);
  ASSERT_EQ(changes.newFiles[0].first, "brand_new.txt");

  ASSERT_EQ(changes.modifiedFiles.size(), 1);
  ASSERT_EQ(changes.modifiedFiles[0].first, "changed.txt");

  ASSERT_EQ(changes.deletedFiles.size(), 1);
  ASSERT_EQ(changes.deletedFiles[0].first, "removed.txt");
}

// --- Rename detection (via inode match) ---

TEST_F(FSScannerTest, renamedFileIsDetectedViaInodeMatch) {
  storage->writeFile(user, "old_name.txt", "same content");
  auto inode = storage->getInode(user, "old_name.txt");
  ASSERT_TRUE(inode.has_value());

  State state;
  TrackedFileState entry;
  entry.filename = "old_name.txt";
  entry.mtime = storage->getMtime(user, "old_name.txt").value();
  entry.localInode = inode.value();
  state.entries.insert("old_name.txt", entry);

  QString oldFull = QDir(storage->rootPath(user)).filePath("old_name.txt");
  QString newFull = QDir(storage->rootPath(user)).filePath("new_name.txt");
  ASSERT_TRUE(QFile::rename(oldFull, newFull));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_EQ(changes.renamedFiles.size(), 1);
  ASSERT_EQ(changes.renamedFiles[0].oldPath, "old_name.txt");
  ASSERT_EQ(changes.renamedFiles[0].newPath, "new_name.txt");
}

TEST_F(FSScannerTest, renamedFileIntoNestedDirectoryIsDetected) {
  storage->writeFile(user, "top.txt", "content");
  auto inode = storage->getInode(user, "top.txt");
  ASSERT_TRUE(inode.has_value());

  State state;
  TrackedFileState entry;
  entry.filename = "top.txt";
  entry.mtime = storage->getMtime(user, "top.txt").value();
  entry.localInode = inode.value();
  state.entries.insert("top.txt", entry);

  QDir(storage->rootPath(user)).mkpath("nested");
  QString oldFull = QDir(storage->rootPath(user)).filePath("top.txt");
  QString newFull = QDir(storage->rootPath(user)).filePath("nested/top.txt");
  ASSERT_TRUE(QFile::rename(oldFull, newFull));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
  ASSERT_EQ(changes.renamedFiles.size(), 1);
  ASSERT_EQ(changes.renamedFiles[0].oldPath, "top.txt");
  ASSERT_EQ(changes.renamedFiles[0].newPath, "nested/top.txt");
}

TEST_F(FSScannerTest, missingEntryWithoutTrackedInodeIsNotMatchedForRename) {
  storage->writeFile(user, "new_name.txt", "content");

  State state;
  TrackedFileState entry;
  entry.filename = "old_name.txt";
  entry.mtime = QDateTime::currentDateTime();
  // localInode intentionally left at its default (0, meaning "unknown").
  state.entries.insert("old_name.txt", entry);

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.renamedFiles.isEmpty());
  ASSERT_EQ(changes.newFiles.size(), 1);
  ASSERT_EQ(changes.newFiles[0].first, "new_name.txt");
  ASSERT_EQ(changes.deletedFiles.size(), 1);
  ASSERT_EQ(changes.deletedFiles[0].first, "old_name.txt");
}

TEST_F(FSScannerTest, tombstonedEntryIsNeverMatchedForRenameEvenWithSameInode) {
  storage->writeFile(user, "old_name.txt", "content");
  auto inode = storage->getInode(user, "old_name.txt");
  ASSERT_TRUE(inode.has_value());

  State state;
  TrackedFileState entry;
  entry.filename = "old_name.txt";
  entry.mtime = storage->getMtime(user, "old_name.txt").value();
  entry.localInode = inode.value();
  entry.isTombstoned = true;
  state.entries.insert("old_name.txt", entry);

  QString oldFull = QDir(storage->rootPath(user)).filePath("old_name.txt");
  QString newFull = QDir(storage->rootPath(user)).filePath("new_name.txt");
  ASSERT_TRUE(QFile::rename(oldFull, newFull));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.renamedFiles.isEmpty());
  ASSERT_EQ(changes.newFiles.size(), 1);
  ASSERT_EQ(changes.newFiles[0].first, "new_name.txt");
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

// --- Directory rename detection (via inode match) ---

TEST_F(FSScannerTest, renamedDirectoryIsDetectedViaInodeMatchAndFoldsInFile) {
  storage->writeFile(user, "olddir/inside.txt", "content");
  auto dirInode = storage->getInode(user, "olddir");
  auto fileInode = storage->getInode(user, "olddir/inside.txt");
  ASSERT_TRUE(dirInode.has_value());
  ASSERT_TRUE(fileInode.has_value());

  State state;
  TrackedFileState dirEntry;
  dirEntry.filename = "olddir";
  dirEntry.mtime = storage->getMtime(user, "olddir").value();
  dirEntry.localInode = dirInode.value();
  dirEntry.isDir = true;
  state.entries.insert("olddir", dirEntry);

  TrackedFileState fileEntry;
  fileEntry.filename = "olddir/inside.txt";
  fileEntry.mtime = storage->getMtime(user, "olddir/inside.txt").value();
  fileEntry.localInode = fileInode.value();
  state.entries.insert("olddir/inside.txt", fileEntry);

  ASSERT_TRUE(QDir(storage->rootPath(user)).rename("olddir", "newdir"));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
  // Folded into the directory rename - not reported as its own file rename.
  ASSERT_TRUE(changes.renamedFiles.isEmpty());
  ASSERT_EQ(changes.renamedDirectories.size(), 1);
  ASSERT_EQ(changes.renamedDirectories[0].oldPath, "olddir");
  ASSERT_EQ(changes.renamedDirectories[0].newPath, "newdir");
}

TEST_F(FSScannerTest, renamedEmptyDirectoryIsDetected) {
  QDir(storage->rootPath(user)).mkpath("emptydir");
  auto dirInode = storage->getInode(user, "emptydir");
  ASSERT_TRUE(dirInode.has_value());

  State state;
  TrackedFileState dirEntry;
  dirEntry.filename = "emptydir";
  dirEntry.mtime = storage->getMtime(user, "emptydir").value();
  dirEntry.localInode = dirInode.value();
  dirEntry.isDir = true;
  state.entries.insert("emptydir", dirEntry);

  ASSERT_TRUE(
      QDir(storage->rootPath(user)).rename("emptydir", "moved_emptydir"));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_EQ(changes.renamedDirectories.size(), 1);
  ASSERT_EQ(changes.renamedDirectories[0].oldPath, "emptydir");
  ASSERT_EQ(changes.renamedDirectories[0].newPath, "moved_emptydir");
}

TEST_F(FSScannerTest, nestedDirectoryRenameOnlyReportsTopLevelMove) {
  storage->writeFile(user, "parent/child/file.txt", "content");

  State state;
  auto trackDir = [&](const QString &path) {
    auto inode = storage->getInode(user, path);
    ASSERT_TRUE(inode.has_value());
    TrackedFileState entry;
    entry.filename = path;
    entry.mtime = storage->getMtime(user, path).value();
    entry.localInode = inode.value();
    entry.isDir = true;
    state.entries.insert(path, entry);
  };
  trackDir("parent");
  trackDir("parent/child");

  TrackedFileState fileEntry;
  auto fileInode = storage->getInode(user, "parent/child/file.txt");
  ASSERT_TRUE(fileInode.has_value());
  fileEntry.filename = "parent/child/file.txt";
  fileEntry.mtime = storage->getMtime(user, "parent/child/file.txt").value();
  fileEntry.localInode = fileInode.value();
  state.entries.insert("parent/child/file.txt", fileEntry);

  ASSERT_TRUE(QDir(storage->rootPath(user)).rename("parent", "renamed_parent"));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.renamedFiles.isEmpty());
  ASSERT_EQ(changes.renamedDirectories.size(), 1);
  ASSERT_EQ(changes.renamedDirectories[0].oldPath, "parent");
  ASSERT_EQ(changes.renamedDirectories[0].newPath, "renamed_parent");
}

TEST_F(FSScannerTest, directoryWithoutTrackedInodeIsNotMatchedForRename) {
  QDir(storage->rootPath(user)).mkpath("new_dir_name");

  State state;
  TrackedFileState entry;
  entry.filename = "old_dir_name";
  entry.mtime = QDateTime::currentDateTime();
  entry.isDir = true;
  // localInode intentionally left at its default (0, meaning "unknown").
  state.entries.insert("old_dir_name", entry);

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.renamedDirectories.isEmpty());
}

TEST_F(FSScannerTest,
       tombstonedDirectoryEntryIsNeverMatchedForRenameEvenWithSameInode) {
  QDir(storage->rootPath(user)).mkpath("old_dir");
  auto dirInode = storage->getInode(user, "old_dir");
  ASSERT_TRUE(dirInode.has_value());

  State state;
  TrackedFileState entry;
  entry.filename = "old_dir";
  entry.mtime = storage->getMtime(user, "old_dir").value();
  entry.localInode = dirInode.value();
  entry.isDir = true;
  entry.isTombstoned = true;
  state.entries.insert("old_dir", entry);

  ASSERT_TRUE(QDir(storage->rootPath(user)).rename("old_dir", "new_dir"));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.renamedDirectories.isEmpty());
}

TEST_F(FSScannerTest, directoryRenameDoesNotAffectUnrelatedFiles) {
  storage->writeFile(user, "olddir/inside.txt", "content");
  storage->writeFile(user, "unrelated.txt", "unrelated");
  auto dirInode = storage->getInode(user, "olddir");
  ASSERT_TRUE(dirInode.has_value());

  State state;
  TrackedFileState dirEntry;
  dirEntry.filename = "olddir";
  dirEntry.mtime = storage->getMtime(user, "olddir").value();
  dirEntry.localInode = dirInode.value();
  dirEntry.isDir = true;
  state.entries.insert("olddir", dirEntry);

  TrackedFileState fileEntry;
  auto fileInode = storage->getInode(user, "olddir/inside.txt");
  ASSERT_TRUE(fileInode.has_value());
  fileEntry.filename = "olddir/inside.txt";
  fileEntry.mtime = storage->getMtime(user, "olddir/inside.txt").value();
  fileEntry.localInode = fileInode.value();
  state.entries.insert("olddir/inside.txt", fileEntry);

  state.entries.insert("unrelated.txt", trackedEntryFor("unrelated.txt"));

  ASSERT_TRUE(QDir(storage->rootPath(user)).rename("olddir", "newdir"));

  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_EQ(changes.renamedDirectories.size(), 1);
  ASSERT_TRUE(changes.renamedFiles.isEmpty());
  ASSERT_TRUE(changes.newFiles.isEmpty());
  ASSERT_TRUE(changes.modifiedFiles.isEmpty());
  ASSERT_TRUE(changes.deletedFiles.isEmpty());
}

TEST_F(FSScannerTest, crossUserIsolation) {
  QString otherUser = "bob";
  storage->cleanup(otherUser);
  storage->writeFile(otherUser, "only_bobs.txt", "bob's file");

  State state;
  auto changes = scanner->scanFilesystemForChanges(user, state);

  ASSERT_TRUE(changes.newFiles.isEmpty());

  storage->cleanup(otherUser);
}

// --- Property-based test: a random sequence of filesystem operations, with
// State iteratively converged after every step exactly as a real caller
// would, is always classified correctly and never drifts. ---

namespace {

enum class OpKind { CreateFile, ModifyFile, DeleteFile, RenameFile, RenameDir };

struct Op {
  OpKind kind;
  QString path;  // used by CreateFile/ModifyFile/DeleteFile/RenameFile (source)
  QString path2; // RenameFile destination
  QString dirPath;  // RenameDir source
  QString dirPath2; // RenameDir destination
};

QString parentDirOf(const QString &path) {
  QString parent = QFileInfo(path).path();
  return parent == "." ? QString() : parent;
}

// Records any directory a scan just revealed a file under, the same way a
// real caller would when it first observes a path - so nested directories
// become eligible for rename detection without a separate "create dir" op.
void trackAncestorDirs(State &state, LocalFileStorage *storage,
                       const QString &user, const QString &path) {
  QString dirPath = parentDirOf(path);
  while (!dirPath.isEmpty()) {
    if (!state.entries.contains(dirPath)) {
      TrackedFileState d;
      d.filename = dirPath;
      d.mtime = storage->getMtime(user, dirPath).value_or(QDateTime());
      d.localInode = storage->getInode(user, dirPath).value_or(0);
      d.isDir = true;
      state.entries.insert(dirPath, d);
    }
    dirPath = parentDirOf(dirPath);
  }
}

TrackedFileState trackedFileEntry(LocalFileStorage *storage,
                                  const QString &user, const QString &path,
                                  const QDateTime &mtime) {
  TrackedFileState e;
  e.filename = path;
  e.mtime = mtime;
  e.localInode = storage->getInode(user, path).value_or(0);
  e.isDir = false;
  return e;
}

// Converges `state` with a scan result exactly as a real client would after
// persisting it - this is what lets the next iteration's scan start from a
// consistent baseline instead of re-reporting the same change forever.
void applyChangesToState(State &state, const LocalChangeSet &changes,
                         LocalFileStorage *storage, const QString &user) {
  for (const auto &[path, mtime] : changes.newFiles) {
    state.entries.insert(path, trackedFileEntry(storage, user, path, mtime));
    trackAncestorDirs(state, storage, user, path);
  }
  for (const auto &[path, mtime] : changes.modifiedFiles) {
    auto it = state.entries.find(path);
    if (it != state.entries.end()) {
      it->mtime = mtime;
    }
  }
  for (const auto &[path, mtime] : changes.deletedFiles) {
    state.entries.remove(path);
  }
  for (const auto &r : changes.renamedFiles) {
    state.entries.remove(r.oldPath);
    state.entries.insert(r.newPath,
                         trackedFileEntry(storage, user, r.newPath, r.mtime));
    trackAncestorDirs(state, storage, user, r.newPath);
  }
  for (const auto &r : changes.renamedDirectories) {
    state.entries.remove(r.oldPath);
    TrackedFileState d;
    d.filename = r.newPath;
    d.mtime = r.mtime;
    d.localInode = storage->getInode(user, r.newPath).value_or(0);
    d.isDir = true;
    state.entries.insert(r.newPath, d);
    trackAncestorDirs(state, storage, user, r.newPath);

    // The scanner folds every descendant move into this single event, so
    // the model has to rewrite every tracked descendant path itself.
    const QString oldPrefix = r.oldPath + "/";
    const QString newPrefix = r.newPath + "/";
    QList<QString> toRewrite;
    for (auto it = state.entries.cbegin(); it != state.entries.cend(); ++it) {
      if (it.key().startsWith(oldPrefix)) {
        toRewrite.append(it.key());
      }
    }
    for (const auto &oldChildPath : toRewrite) {
      TrackedFileState child = state.entries.take(oldChildPath);
      child.filename = newPrefix + oldChildPath.mid(oldPrefix.size());
      state.entries.insert(child.filename, child);
    }
  }
}

QString fullDiskPath(LocalFileStorage *storage, const QString &user,
                     const QString &path) {
  return QDir(storage->rootPath(user)).filePath(path);
}

QSet<QString> namesOf(const QList<FileChangeMetadata> &list) {
  QSet<QString> s;
  for (const auto &e : list) {
    s.insert(e.first);
  }
  return s;
}

QSet<QString> renameKeysOf(const QList<RenamedFileMetadata> &list) {
  QSet<QString> s;
  for (const auto &e : list) {
    s.insert(e.oldPath + " -> " + e.newPath);
  }
  return s;
}

} // namespace

namespace rc {
template <> struct Arbitrary<OpKind> {
  static Gen<OpKind> arbitrary() {
    return gen::element(OpKind::CreateFile, OpKind::ModifyFile,
                        OpKind::DeleteFile, OpKind::RenameFile,
                        OpKind::RenameDir);
  }
};

template <> struct Arbitrary<Op> {
  static Gen<Op> arbitrary() {
    auto filePathGen = gen::element<QString>("dirA/a.txt", "dirA/b.txt",
                                             "dirB/c.txt", "top.txt");
    auto dirPathGen = gen::element<QString>("dirA", "dirB");
    return gen::build<Op>(gen::set(&Op::kind, gen::arbitrary<OpKind>()),
                          gen::set(&Op::path, filePathGen),
                          gen::set(&Op::path2, filePathGen),
                          gen::set(&Op::dirPath, dirPathGen),
                          gen::set(&Op::dirPath2, dirPathGen));
  }
};
} // namespace rc

namespace {

// The single change a well-formed operation is expected to produce this round,
// or an empty `bucket` when the generated op didn't apply against the current
// disk/state and this is a no-op round. `bucket` is one of
// "new"/"modified"/"deleted"/"renamed"/"renamedDir"; `a` is the touched path
// and `b` the rename destination.
struct Expectation {
  QString bucket;
  QString a;
  QString b;
};

// --- One function per OpKind: precondition check, disk mutation, and the
// oracle for what the very next scan should report. A failed precondition
// (file already exists, nothing to rename, ...) leaves the disk untouched and
// returns an empty Expectation, exactly as the generated sequence intended. ---

Expectation applyCreateFile(LocalFileStorage *storage, const QString &user,
                            const Op &op) {
  QString full = fullDiskPath(storage, user, op.path);
  if (QFileInfo::exists(full)) {
    return {};
  }
  RC_ASSERT(storage->writeFile(user, op.path, "content"));
  return {"new", op.path, {}};
}

Expectation applyModifyFile(const State &state, LocalFileStorage *storage,
                            const QString &user, const Op &op) {
  QString full = fullDiskPath(storage, user, op.path);
  if (!QFileInfo::exists(full)) {
    return {};
  }
  RC_ASSERT(storage->writeFile(user, op.path, "content-modified"));
  // Push mtime past the tracked baseline (see FS_MTIME_GRANULARITY_SECONDS) so
  // the modification is observable regardless of filesystem/tick granularity.
  QDateTime baseline = state.entries.value(op.path).mtime;
  QFile f(full);
  RC_ASSERT(f.open(QIODevice::ReadWrite));
  RC_ASSERT(f.setFileTime(baseline.addSecs(FS_MTIME_GRANULARITY_SECONDS),
                          QFileDevice::FileModificationTime));
  f.close();
  return {"modified", op.path, {}};
}

Expectation applyDeleteFile(LocalFileStorage *storage, const QString &user,
                            const Op &op) {
  QString full = fullDiskPath(storage, user, op.path);
  if (!QFileInfo::exists(full)) {
    return {};
  }
  RC_ASSERT(storage->deleteFile(user, op.path));
  return {"deleted", op.path, {}};
}

Expectation applyRenameFile(LocalFileStorage *storage, const QString &user,
                            const Op &op) {
  QString fullOld = fullDiskPath(storage, user, op.path);
  QString fullNew = fullDiskPath(storage, user, op.path2);
  if (op.path == op.path2 || !QFileInfo::exists(fullOld) ||
      QFileInfo::exists(fullNew)) {
    return {};
  }
  QDir().mkpath(QFileInfo(fullNew).dir().absolutePath());
  if (!QFile::rename(fullOld, fullNew)) {
    return {};
  }
  return {"renamed", op.path, op.path2};
}

Expectation applyRenameDir(const State &state, LocalFileStorage *storage,
                           const QString &user, const Op &op) {
  QString fullOld = fullDiskPath(storage, user, op.dirPath);
  QString fullNew = fullDiskPath(storage, user, op.dirPath2);
  const bool trackedAsRenamableDir =
      state.entries.contains(op.dirPath) &&
      state.entries.value(op.dirPath).isDir &&
      state.entries.value(op.dirPath).localInode != 0;
  if (op.dirPath == op.dirPath2 || !QFileInfo::exists(fullOld) ||
      !QFileInfo(fullOld).isDir() || QFileInfo::exists(fullNew) ||
      !trackedAsRenamableDir) {
    return {};
  }
  if (!QDir(storage->rootPath(user)).rename(op.dirPath, op.dirPath2)) {
    return {};
  }
  return {"renamedDir", op.dirPath, op.dirPath2};
}

Expectation applyOpToDisk(const State &state, LocalFileStorage *storage,
                          const QString &user, const Op &op) {
  switch (op.kind) {
  case OpKind::CreateFile:
    return applyCreateFile(storage, user, op);
  case OpKind::ModifyFile:
    return applyModifyFile(state, storage, user, op);
  case OpKind::DeleteFile:
    return applyDeleteFile(storage, user, op);
  case OpKind::RenameFile:
    return applyRenameFile(storage, user, op);
  case OpKind::RenameDir:
    return applyRenameDir(state, storage, user, op);
  }
  return {};
}

// --- Oracle expansion + assertions ---

struct ExpectedChangeSets {
  QSet<QString> newFiles;
  QSet<QString> modifiedFiles;
  QSet<QString> deletedFiles;
  QSet<QString> renamedFiles;
  QSet<QString> renamedDirectories;
};

ExpectedChangeSets expectedChangeSetsFor(const Expectation &e) {
  ExpectedChangeSets s;
  if (e.bucket == "new") {
    s.newFiles.insert(e.a);
  } else if (e.bucket == "modified") {
    s.modifiedFiles.insert(e.a);
  } else if (e.bucket == "deleted") {
    s.deletedFiles.insert(e.a);
  } else if (e.bucket == "renamed") {
    s.renamedFiles.insert(e.a + " -> " + e.b);
  } else if (e.bucket == "renamedDir") {
    s.renamedDirectories.insert(e.a + " -> " + e.b);
  }
  return s;
}

std::string showSet(const QSet<QString> &s) {
  QStringList sorted(s.begin(), s.end());
  sorted.sort();
  return ("{" + sorted.join(", ") + "}").toStdString();
}

void assertChangesMatch(const LocalChangeSet &actual,
                        const ExpectedChangeSets &expected) {
  auto check = [](const QSet<QString> &got, const QSet<QString> &want,
                  const char *label) {
    if (got != want) {
      RC_FAIL(std::string(label) + " mismatch: got " + showSet(got) +
              ", expected " + showSet(want));
    }
  };
  check(namesOf(actual.newFiles), expected.newFiles, "newFiles");
  check(namesOf(actual.modifiedFiles), expected.modifiedFiles, "modifiedFiles");
  check(namesOf(actual.deletedFiles), expected.deletedFiles, "deletedFiles");
  check(renameKeysOf(actual.renamedFiles), expected.renamedFiles,
        "renamedFiles");
  check(renameKeysOf(actual.renamedDirectories), expected.renamedDirectories,
        "renamedDirectories");
}

// Convergence: with `state` reconciled and nothing new on disk, scanning again
// immediately must report no changes at all.
void assertConverged(FSScanner &scanner, const QString &user,
                     const State &state) {
  auto again = scanner.scanFilesystemForChanges(user, state);
  RC_ASSERT(again.newFiles.isEmpty());
  RC_ASSERT(again.modifiedFiles.isEmpty());
  RC_ASSERT(again.deletedFiles.isEmpty());
  RC_ASSERT(again.renamedFiles.isEmpty());
  RC_ASSERT(again.renamedDirectories.isEmpty());
}

} // namespace

RC_GTEST_FIXTURE_PROP(FSScannerTest, sequenceOfOperationsConverges,
                      (const std::vector<Op> &rawOps)) {
  RC_PRE(!rawOps.empty());
  RC_PRE(rawOps.size() <= 40);

  State state;

  for (const auto &op : rawOps) {
    const Expectation expectation =
        applyOpToDisk(state, storage.get(), user, op);

    auto changes = scanner->scanFilesystemForChanges(user, state);
    assertChangesMatch(changes, expectedChangeSetsFor(expectation));

    // Reconcile `state` with the scan result exactly as a real client would
    // after persisting it, so the next iteration starts from a consistent
    // baseline instead of re-reporting the same change forever.
    applyChangesToState(state, changes, storage.get(), user);

    assertConverged(*scanner, user, state);
  }
}
