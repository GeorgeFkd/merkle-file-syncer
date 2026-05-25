#include "SessionRegistry.h"

#include <QUuid>

QString SessionRegistry::createSession(const QString &username,
                                       const QString &deviceName) {
    QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDateTime now = QDateTime::currentDateTime();
    sessions.insert(token, Session{
                               .username = username,
                               .deviceName = deviceName,
                               .createdAt = now,
                               .lastActivityAt = now,
                           });
    return token;
}

std::optional<Session> SessionRegistry::getSession(const QString &token) const {
    auto it = sessions.constFind(token);
    if (it == sessions.constEnd())
        return std::nullopt;
    return it.value();
}

void SessionRegistry::revokeSession(const QString &token) {
    sessions.remove(token);
}

bool SessionRegistry::hasSession(const QString &username,
                                 const QString &deviceName) const {
    for (auto it = sessions.constBegin(); it != sessions.constEnd(); ++it) {
        if (it.value().username == username &&
            it.value().deviceName == deviceName)
            return true;
    }
    return false;
}

void SessionRegistry::touchSession(const QString &token) {
    auto it = sessions.find(token);
    if (it != sessions.end())
        it.value().lastActivityAt = QDateTime::currentDateTime();
}


std::optional<QString> SessionRegistry::getUsername(const QString &token) const {
  auto it = sessions.constFind(token);
  if (it == sessions.constEnd())
    return std::nullopt;
  return it.value().username;
}

std::optional<QString> SessionRegistry::getDeviceName(const QString &token) const {
  auto it = sessions.constFind(token);
  if (it == sessions.constEnd())
    return std::nullopt;
  return it.value().deviceName;
}

