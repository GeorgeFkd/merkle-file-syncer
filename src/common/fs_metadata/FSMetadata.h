#pragma once
#include "MerkleTree.h"
#include <QByteArray>
#include <QDateTime>
#include <QString>

class FSMetadata {
public:
  void recordFile(const QString &id, const QString &path,
                  const QDateTime &mtime, const QByteArray &hash);
  void recordDeletion(const QString &id, const QString &path,
                      const QDateTime &mtime);
  std::optional<QDateTime> deletedAt(const QString &id,
                                     const QString &file) const;
  std::optional<QDateTime> readMtime(const QString &id,
                                     const QString &file) const;
  QHash<QString, QDateTime> allTombstones(const QString &id) const;
  QSet<QString> allTrackedFiles(const QString &id) const;

  MerkleTree *getUserTree(const QString &id);

private:
  void markDeleted(const QString &id, const QString &file,
                   const QDateTime &deletedAt, bool untrack = true);
  void updateFileMtime(const QString &id, const QString &file,
                       const QDateTime &mtime);

  void removeFileMtime(const QString &id, const QString &file);
  std::unique_ptr<MerkleTree> buildMerkleTreeFromDb(const QString &id);
  QHash<QString, QDateTime> fileMtimes;
  QHash<QString, QDateTime> tombstones;
  QHash<QString, QByteArray> hashes;
  struct QStringHash {
    size_t operator()(const QString &s) const { return qHash(s); }
  };
  std::unordered_map<QString, std::unique_ptr<MerkleTree>, QStringHash>
      userTrees;
};
