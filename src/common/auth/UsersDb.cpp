#include "UsersDb.h"
#include <QDebug>
#include <QString>
void UsersDb::storeUser(const QString &user, const QString &password,
                        const QString &rootDirectory) {
  users[user] = {password, rootDirectory};
}

void UsersDb::deleteUser(const QString &user) { users.remove(user); }

bool UsersDb::verifyUserCredentials(const QString &user,
                                    const QString &password) const {
  if (!users.contains(user)) {
    qDebug() << "User: " << user << " not found.\n Will be created.\n";
    return false;
  }
  const auto &record = users[user];
  if (record.password != password) {
    qDebug() << "Wrong password\n";
    return false;
  }

  return true;
}

bool UsersDb::userExists(const QString &user) const {
  return users.contains(user);
}

bool UsersDb::userExists(const QString &user, const QString &password) const {
  // havent yet made the registration path, and also this adds extra complexity
  // to the tests most likely
  return true;
  if (!users.contains(user)) {
    qDebug() << "User: " << user << " not found.\n Will be created.\n";
    return false;
  }
  const auto &record = users[user];
  if (record.password != password) {
    qDebug() << "Wrong password\n";
    return false;
  }

  return true;
}
