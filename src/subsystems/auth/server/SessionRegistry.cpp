#include "SessionRegistry.h"

#include <QUuid>


bool SessionRegistry::isActive(const QString &sessionId) const {
  return sessions.contains(sessionId);
}


QString SessionRegistry::createSession(const QString &username,
                                       const QString &deviceName) {
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDateTime now = QDateTime::currentDateTime();
    sessions.insert(sessionId, Session{
                               .username = username,
                               .deviceName = deviceName,
                               .createdAt = now,
                               .lastActivityAt = now,
                           });
    return sessionId;
}

std::optional<Session> SessionRegistry::getSession(const QString &sessionId) const {
    auto it = sessions.constFind(sessionId);
    if (it == sessions.constEnd())
        return std::nullopt;
    return it.value();
}

void SessionRegistry::revokeSession(const QString &sessionId) {
    sessions.remove(sessionId);
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

void SessionRegistry::touchSession(const QString &sessionId) {
    auto it = sessions.find(sessionId);
    if (it != sessions.end())
        it.value().lastActivityAt = QDateTime::currentDateTime();
}


std::optional<QString> SessionRegistry::getUsername(const QString &sessionId) const {
  auto it = sessions.constFind(sessionId);
  if (it == sessions.constEnd())
    return std::nullopt;
  return it.value().username;
}

void SessionRegistry::revokeAllForUser(const QString &username) {
  for (auto it = sessions.begin(); it != sessions.end();) {
    if (it->username == username)
      it = sessions.erase(it);
    else
      ++it;
  }
}


std::optional<QString> SessionRegistry::getDeviceName(const QString &sessionId) const {
  auto it = sessions.constFind(sessionId);
  if (it == sessions.constEnd())
    return std::nullopt;
  return it.value().deviceName;
}

