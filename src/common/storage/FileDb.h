#pragma once

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <optional>
class FileDb {
public:
  // file -> mtime
  std::optional<QDateTime> readMtime(const QString &file) const;
  void updateFileMtime(const QString &file, const QDateTime &mtime);

  // user -> (password, rootDirectory)
  std::optional<QString> readUserDirectory(const QString &user,
                                           const QString &password) const;
  void storeUser(const QString &user, const QString &password,
                 const QString &rootDirectory);
  void removeFileMtime(const QString &file);
  QSet<QString> allTrackedFiles() const;

  void markDeleted(const QString &file, const QDateTime &deletedAt);
  bool isDeleted(const QString &file) const;
  std::optional<QDateTime> deletedAt(const QString &file) const;
  QHash<QString, QDateTime> allTombstones() const;

private:
  struct UserRecord {
    QString password;
    QString rootDirectory;
  };

  QHash<QString, QDateTime> fileMtimes;
  QHash<QString, UserRecord> users;
  QHash<QString, QDateTime> tombstones; // file -> deletedAt
};
