#include "FSMetadata.h"

namespace {
QString makeKey(const QString &user, const QString &path) {
  return user + "/" + path;
}

std::optional<QString> pathFromKey(const QString &id, const QString &key) {
  QString prefix = id + "/";
  if (!key.startsWith(prefix))
    return std::nullopt;
  return key.mid(prefix.length());
}
} // namespace

std::optional<QDateTime> FSMetadata::readMtime(const QString &id,
                                               const QString &file) const {
  auto it = fileMtimes.find(makeKey(id, file));
  if (it == fileMtimes.end())
    return {};
  return it.value();
}

std::optional<QByteArray> FSMetadata::readHash(const QString &id,
                                               const QString &file) const {
  auto it = hashes.find(makeKey(id, file));
  if (it == hashes.end())
    return {};
  return it.value();
}

void FSMetadata::removeFileMtime(const QString &user, const QString &file) {
  auto key = makeKey(user, file);
  fileMtimes.remove(key);
}

QSet<QString> FSMetadata::allTrackedFiles(const QString &id) const {
  QSet<QString> result;
  QString prefix = id + "/";
  for (auto it = fileMtimes.cbegin(); it != fileMtimes.cend(); ++it) {
    if (it.key().startsWith(prefix)) {
      result.insert(it.key().mid(prefix.length()));
    }
  }
  return result;
}

void FSMetadata::updateFileMtime(const QString &id, const QString &file,
                                 const QDateTime &mtime) {
  auto key = makeKey(id, file);
  fileMtimes[key] = mtime;
}

void FSMetadata::markDeleted(const QString &id, const QString &file,
                             const QDateTime &deletedAt, bool untrack) {
  auto key = makeKey(id, file);
  if (untrack) {
    fileMtimes.remove(key);
  }
  tombstones[key] = deletedAt;
}

void FSMetadata::recordFile(const QString &id, const QString &path,
                            const QDateTime &mtime, const QByteArray &hash) {
  hashes[makeKey(id, path)] = hash;
  updateFileMtime(id, path, mtime);
  getUserTree(id)->addFile(path, mtime, hash);
}

void FSMetadata::recordDeletion(const QString &id, const QString &path,
                                const QDateTime &deletedAt) {
  markDeleted(id, path, deletedAt, /*untrack=*/true);
  getUserTree(id)->deleteFile(path, deletedAt);
  //we dont need hashes for tombstoned things
  hashes.remove(makeKey(id, path));
}

std::unique_ptr<MerkleTree>
FSMetadata::buildMerkleTreeFromDb(const QString &id) {
  auto tree = std::make_unique<MerkleTree>(id);

  auto files = allTrackedFiles(id);
  for (const auto &path : files) {

    auto mtime = readMtime(id, path);
    if (!mtime.has_value()) {
      qDebug() << "buildMerkleTree: no mtime for path" << path;
      continue;
    }
    auto hash = hashes[makeKey(id, path)];
    bool res = tree->addFile(path, mtime.value(), hash);
    assert(res && "Merkle tree add file should not fail for file just added");
  }

  auto tombstones = allTombstones(id);
  QString prefix = id + "/";
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    tree->deleteFile(it.key(), it.value());
  }

  assert(tree->verifyHashes());
  return tree;
}

MerkleTree *FSMetadata::getUserTree(const QString &id) {
  auto it = userTrees.find(id);
  if (it != userTrees.end()) {
    return it->second.get();
  }

  auto tree = buildMerkleTreeFromDb(id);

  auto *raw = tree.get();
  userTrees.emplace(id, std::move(tree));
  return raw;
}

std::optional<QDateTime> FSMetadata::deletedAt(const QString &user,
                                               const QString &file) const {
  auto key = makeKey(user, file);
  auto it = tombstones.find(key);
  if (it == tombstones.end())
    return {};
  return it.value();
}

QHash<QString, QDateTime> FSMetadata::allTombstones(const QString &user) const {
  QHash<QString, QDateTime> result;
  QString prefix = user + "/";
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    if (it.key().startsWith(prefix)) {
      result.insert(it.key().mid(prefix.length()), it.value());
    }
  }
  return result;
}
