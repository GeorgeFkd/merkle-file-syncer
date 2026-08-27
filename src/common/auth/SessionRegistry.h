#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <optional>

struct Session {
  QString username;
  QString deviceName;
  QDateTime createdAt;
  QDateTime lastActivityAt;
};

class SessionRegistry {
public:
  void revokeAllForUser(const QString &username);
  QString createSession(const QString &username, const QString &deviceName);
  std::optional<Session> getSession(const QString &token) const;
  std::optional<QString> getUsername(const QString &token) const;
  std::optional<QString> getDeviceName(const QString &token) const;
  void revokeSession(const QString &token);
  bool hasSession(const QString &username, const QString &deviceName) const;
  void touchSession(const QString &token);

private:
  QHash<QString, Session> sessions;
};
