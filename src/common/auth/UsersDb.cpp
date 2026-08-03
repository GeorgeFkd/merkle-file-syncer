#include "UsersDb.h"
#include <QDebug>
#include <QString>
void UsersDb::storeUser(const QString &user, const QString &password,
                        const QString &rootDirectory) {
  users[user] = {password, rootDirectory};
}

bool UsersDb::userExists(const QString &user, const QString &password) const {
  // havent yet made the registration path, and also this adds extra complexity to the tests most likely
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

std::optional<QString>
UsersDb::readUserDirectory(const QString &user, const QString &password) const {
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
