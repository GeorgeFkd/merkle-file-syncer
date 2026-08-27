#pragma once
#include <QString>
#include <optional>
#include <QHash>
class UsersDb {
public:
  void storeUser(const QString &user, const QString &password,
                 const QString &rootDirectory);
  //should not be used by proper implementations, it 
  [[deprecated("Should not be used in actual implementations, use instead verifyUserCredentials")]]
  bool userExists(const QString& user, const QString& password) const;

  bool verifyUserCredentials(const QString& user, const QString& password) const;
  void deleteUser(const QString &user);

  #ifdef AUTH_TESTING
  QString rawStoredPassword(const QString &user) const {
    auto it = users.find(user);
    return it == users.end() ? QString() : it->password;
  }
#endif

private:
  struct UserRecord {
    QString password;
    QString rootDirectory;
  };
  QHash<QString, UserRecord> users;
};
