#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <optional>

// Passwords are never held in memory or stored in plaintext: storeUser() runs
// them through libsodium's Argon2id (crypto_pwhash_str) and keeps only the
// resulting ASCII digest, which carries its own salt and cost parameters.
// libsodium stays behind this header — nothing downstream needs sodium.h.
class UsersDb {
public:
  UsersDb();

  void storeUser(const QString &user, const QString &password,
                 const QString &rootDirectory);
  //should not be used by proper implementations, it 
  [[deprecated("Should not be used in actual implementations, use instead verifyUserCredentials")]]
  bool userExists(const QString& user, const QString& password) const;
  
  bool userExists(const QString& user) const;
  bool verifyUserCredentials(const QString& user, const QString& password) const;
  void deleteUser(const QString &user);

  #ifdef AUTH_TESTING
  // The stored Argon2id digest, e.g. "$argon2id$v=19$m=65536,t=2,p=1$...".
  QString rawStoredPassword(const QString &user) const {
    auto it = users.find(user);
    return it == users.end() ? QString()
                             : QString::fromLatin1(it->passwordHash);
  }
#endif

private:
  struct UserRecord {
    QByteArray passwordHash;
    QString rootDirectory;
  };
  QHash<QString, UserRecord> users;
};
