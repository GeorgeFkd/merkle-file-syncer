#pragma once

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <optional>

// TODO: make all methods take a user as key
// and do actual namespacing here(now the client and server do it)
class FileDb {
public:
  // file -> mtime
  std::optional<QDateTime> readMtime(const QString &user,
                                     const QString &file) const;
  void updateFileMtime(const QString &user, const QString &file,
                       const QDateTime &mtime);

  // user -> (password, rootDirectory)
  std::optional<QString> readUserDirectory(const QString &user,
                                           const QString &password) const;
  void storeUser(const QString &user, const QString &password,
                 const QString &rootDirectory);
  void removeFileMtime(const QString &user, const QString &file);
  QSet<QString> allTrackedFiles(const QString &user) const;

  void markDeleted(const QString &user, const QString &file,
                   const QDateTime &deletedAt, bool untrack = true);
  bool isDeleted(const QString &user, const QString &file) const;
  std::optional<QDateTime> deletedAt(const QString &user,
                                     const QString &file) const;
  QHash<QString, QDateTime> allTombstones(const QString &user) const;

private:
  struct UserRecord {
    QString password;
    QString rootDirectory;
  };

  QHash<QString, QDateTime> fileMtimes;
  QHash<QString, UserRecord> users;
  QHash<QString, QDateTime> tombstones; // file -> deletedAt
};
