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

// Keyed by an opaque session id. That id is an identifier, not a credential:
// AuthServer carries it to the client inside a signed token (as the JWT "jti"
// claim), and only a verified signature turns a token back into one of these
// ids. Nothing in here knows or cares that JWTs exist.
class SessionRegistry {
public:
  void revokeAllForUser(const QString &username);
  QString createSession(const QString &username, const QString &deviceName);
  std::optional<Session> getSession(const QString &sessionId) const;
  std::optional<QString> getUsername(const QString &sessionId) const;
  std::optional<QString> getDeviceName(const QString &sessionId) const;
  void revokeSession(const QString &sessionId);
  bool hasSession(const QString &username, const QString &deviceName) const;
  void touchSession(const QString &sessionId);
  bool isActive(const QString &sessionId) const;

private:
  QHash<QString, Session> sessions;
};
