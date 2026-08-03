#include "FileDb.h"

namespace {
QString makeKey(const QString &user, const QString &path) {
  return user + "/" + path;
}

std::optional<QString> pathFromKey(const QString &user, const QString &key) {
  QString prefix = user + "/";
  if (!key.startsWith(prefix))
    return std::nullopt;
  return key.mid(prefix.length());
}
} // namespace

std::optional<QDateTime> FileDb::readMtime(const QString &user,
                                           const QString &file) const {
  auto it = fileMtimes.find(makeKey(user, file));
  if (it == fileMtimes.end())
    return {};
  return it.value();
}

void FileDb::removeFileMtime(const QString &user, const QString &file) {
  auto key = makeKey(user, file);
  fileMtimes.remove(key);
}

QSet<QString> FileDb::allTrackedFiles(const QString &user) const {
  QSet<QString> result;
  QString prefix = user + "/";
  for (auto it = fileMtimes.cbegin(); it != fileMtimes.cend(); ++it) {
    if (it.key().startsWith(prefix)) {
      result.insert(it.key().mid(prefix.length()));
    }
  }
  return result;
}

void FileDb::updateFileMtime(const QString &user, const QString &file,
                             const QDateTime &mtime) {
  auto key = makeKey(user, file);
  fileMtimes[key] = mtime;
}

std::optional<QString>
FileDb::readUserDirectory(const QString &user, const QString &password) const {
  if (!users.contains(user)) {
    qDebug() << "User: " << user << " not found.\n Will be created.\n";
    return std::nullopt;
  }
  const auto &record = users[user];
  if (record.password != password) {
    qDebug() << "Wrong password\n";
    return std::nullopt;
  }
  return record.rootDirectory;
}

void FileDb::storeUser(const QString &user, const QString &password,
                       const QString &rootDirectory) {
  users[user] = {password, rootDirectory};
}

void FileDb::markDeleted(const QString &user, const QString &file,
                         const QDateTime &deletedAt,bool untrack) {
  auto key = makeKey(user, file);
  if (untrack) {
    fileMtimes.remove(key);
  }
  tombstones[key] = deletedAt;
}

bool FileDb::isDeleted(const QString &user, const QString &file) const {
  auto key = makeKey(user, file);
  return tombstones.contains(key);
}

std::optional<QDateTime> FileDb::deletedAt(const QString &user,
                                           const QString &file) const {
  auto key = makeKey(user, file);
  auto it = tombstones.find(key);
  if (it == tombstones.end())
    return {};
  return it.value();
  // if (tombstones.contains(file))
  //   return tombstones[file];
  // return std::nullopt;
}

QHash<QString, QDateTime> FileDb::allTombstones(const QString &user) const {
  QHash<QString, QDateTime> result;
  QString prefix = user + "/";
  for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
    if (it.key().startsWith(prefix)) {
      result.insert(it.key().mid(prefix.length()), it.value());
    }
  }
  return result;
}
