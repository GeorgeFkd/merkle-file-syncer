#include "FSScanner.h"
#include <QHash>
#include <QSet>

FSScanner::FSScanner(LocalFileStorage *storage) : storage(storage) {}

QHash<QString, QDateTime> FSScanner::getMtimesFromStorage(const QString &user) {
  auto fileList = storage->listFiles(user);
  QSet<QString> currentFsState(fileList.begin(), fileList.end());

  QHash<QString, QDateTime> currentMtimes;
  for (const auto &path : currentFsState) {
    auto fsMtime = storage->getMtime(user, path);
    if (!fsMtime.has_value()) {
      continue;
    }
    currentMtimes.insert(path, fsMtime.value());
  }
  return currentMtimes;
}

FSScanner::DiscoverResult FSScanner::findNewAndModifiedFiles(
    const QString &user, const QHash<QString, QDateTime> &currentMtimes,
    const State &state) {
  QList<QString> candidateNew;
  QList<QPair<QString, QDateTime>> modifiedFiles;
  for (const auto &path : currentMtimes.keys()) {
    auto it = state.entries.find(path);
    auto fsMtime = currentMtimes[path];
    bool notInPreviousState = it == state.entries.end();
    if (notInPreviousState || it->isTombstoned) {
      candidateNew.append(path);
    } else if (fsMtime != it->mtime) {
      modifiedFiles.append({path, fsMtime});
    }
  }
  return {candidateNew, modifiedFiles};
}

FSScanner::INodeHasHasNotSplitResult FSScanner::splitNodesBasedOnINodeExists(
    const State &state, const QHash<QString, QDateTime> &currentMtimes) {
  QHash<quint64, QString> missingByInode;
  QList<QString> missingWithoutInode;
  for (auto it = state.entries.cbegin(); it != state.entries.cend(); ++it) {
    const TrackedFileState &tracked = it.value();
    bool fileExistsInFs = currentMtimes.contains(tracked.filename);
    if (tracked.isTombstoned || tracked.isDir || fileExistsInFs) {
      continue;
    }
    if (tracked.localInode == MISSING_INODE_SENTINEL) {
      missingWithoutInode.append(tracked.filename);
    } else {
      missingByInode.insert(tracked.localInode, tracked.filename);
    }
  }
  return {missingWithoutInode, missingByInode};
}

LocalChangeSet FSScanner::scanFilesystemForChanges(const QString &user,
                                                   const State &state) {
  LocalChangeSet changes;

  auto currentMtimes = getMtimesFromStorage(user);

  auto [candidateNew, modifiedFiles] =
      findNewAndModifiedFiles(user, currentMtimes, state);
  changes.modifiedFiles = std::move(modifiedFiles);

  // Tracked files that vanished from their old path are candidates for
  // rename detection: if a "new" file below shares an inode with one of
  // these, it was moved/renamed rather than deleted+recreated.
  auto [missingWithoutInode, missingByInode] =
      splitNodesBasedOnINodeExists(state, currentMtimes);

  for (const auto &path : candidateNew) {
    auto fsInode = storage->getInode(user, path);
    bool inFsWithDifferentPath = fsInode.has_value() &&
                                 fsInode.value() != MISSING_INODE_SENTINEL &&
                                 missingByInode.contains(fsInode.value());
    if (inFsWithDifferentPath) {
      auto oldEntry = missingByInode.take(fsInode.value());
      changes.renamedFiles.append({oldEntry, path, currentMtimes.value(path)});
      continue;
    }
    changes.newFiles.append({path, currentMtimes.value(path)});
  }

  for (auto it = missingByInode.cbegin(); it != missingByInode.cend(); ++it) {
    changes.deletedFiles.append({*it, QDateTime::currentDateTime()});
  }
  for (const auto &filename : missingWithoutInode) {
    changes.deletedFiles.append({filename, QDateTime::currentDateTime()});
  }

  // --- Directory rename detection ---
  // Mirrors the file-level matching above: a tracked directory that vanished
  // from its old path is matched, by inode, against a directory newly found
  // on disk. Renaming a directory never changes the inode of the directory
  // itself (or of anything inside it), so this works the same way file
  // rename detection does.
  auto dirList = storage->listDirectories(user);
  QSet<QString> currentFsDirs(dirList.begin(), dirList.end());

  QHash<quint64, TrackedFileState> missingDirsByInode;
  for (auto it = state.entries.cbegin(); it != state.entries.cend(); ++it) {
    const TrackedFileState &tracked = it.value();
    bool doesntHaveINode = tracked.localInode == MISSING_INODE_SENTINEL;
    bool dirExistsInFs = currentFsDirs.contains(tracked.filename);
    if (!tracked.isDir || tracked.isTombstoned || dirExistsInFs ||
        doesntHaveINode) {
      continue;
    }
    missingDirsByInode.insert(tracked.localInode, tracked);
  }

  for (const auto &dirPath : currentFsDirs) {
    auto it = state.entries.find(dirPath);
    if (it != state.entries.end() && it->isDir && !it->isTombstoned) {
      continue; // already tracked at this path, nothing moved
    }
    auto fsInode = storage->getInode(user, dirPath);
    bool doesntHaveINode =
        !fsInode.has_value() || fsInode.value() == MISSING_INODE_SENTINEL;
    if (doesntHaveINode || !missingDirsByInode.contains(fsInode.value())) {
      continue;
    }
    auto oldEntry = missingDirsByInode.take(fsInode.value());
    changes.renamedDirectories.append(
        {oldEntry.filename, dirPath,
         storage->getMtime(user, dirPath).value_or(QDateTime())});
  }

  // A moved directory implies every path under it moved too. Drop any
  // reported rename (file or nested directory) that is just a descendant of
  // an already-reported directory rename, so it isn't reported twice.
  auto isDescendantMove = [](const RenamedFileMetadata &candidate,
                             const RenamedFileMetadata &dirRename) {
    const QString oldPrefix = dirRename.oldPath + "/";
    const QString newPrefix = dirRename.newPath + "/";
    return candidate.oldPath.startsWith(oldPrefix) &&
           candidate.newPath.startsWith(newPrefix) &&
           candidate.oldPath.mid(oldPrefix.size()) ==
               candidate.newPath.mid(newPrefix.size());
  };

  auto foldIntoDirRenames = [&](QList<RenamedFileMetadata> &candidates) {
    QList<RenamedFileMetadata> kept;
    for (const auto &candidate : candidates) {
      bool isDescendant = false;
      for (const auto &dirRename : changes.renamedDirectories) {
        if (isDescendantMove(candidate, dirRename)) {
          isDescendant = true;
          break;
        }
      }
      if (!isDescendant) {
        kept.append(candidate);
      }
    }
    candidates = std::move(kept);
  };

  foldIntoDirRenames(changes.renamedFiles);
  foldIntoDirRenames(changes.renamedDirectories);

  return changes;
}
