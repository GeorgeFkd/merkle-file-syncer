#pragma once
#include <QString>
#include <optional>
#include <QHash>
class UsersDb {
public:
  std::optional<QString> readUserDirectory(const QString &user,
                                           const QString &password) const;
  void storeUser(const QString &user, const QString &password,
                 const QString &rootDirectory);
  bool userExists(const QString& user, const QString& password) const;

private:
  struct UserRecord {
    QString password;
    QString rootDirectory;
  };
  QHash<QString, UserRecord> users;
};
