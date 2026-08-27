#pragma once
#include "Messages.h"
#include <QObject>
#include <QString>
#include <memory>

class AuthClient : public QObject {
  Q_OBJECT
public:
  explicit AuthClient(QObject *parent = nullptr);

  // Client-initiated operations. Each builds a message and emits sendMessage.
  void registerAccount(const QString &username, const QString &password,
                       const QString &deviceName);
  void login(const QString &username, const QString &password,
             const QString &deviceName);
  void logout();
  void deleteAccount(const QString &password); // public

  // AuthClient is the token authority.
  const QString &getToken() const { return token; }
  bool hasToken() const { return !token.isEmpty(); }

  // Interceptor: stamps the current token onto an outbound message so callers
  // don't repeat `msg->token = token` at every send site. Returns msg for
  // chaining.
  void stampToken(std::shared_ptr<Message> msg) const;

  // Self-filtering: handles auth responses, early-returns the rest.
  void onMessage(std::shared_ptr<Message> msg);

Q_SIGNALS:
  // Outbound to the transport (token already stamped where applicable).
  void sendMessage(std::shared_ptr<Message> msg);

  // Result signals the outer client (FileClient) reacts to.
  void authenticated(const QString &token); // login/register success
  void authFailed(const QString &error);
  void loggedOut();
  void accountDeleted(); // declared, not implemented this pass
  void registered();     // register success
  void registerFailed(const QString &error);

private:
  void handleAuthResponse(AuthResponseMessage *msg); // stores token on success
  void handleLogoutResponse(LogoutResponseMessage *msg);
  void handleDeleteAccountResponse(DeleteAccountResponseMessage *msg);
  void handleRegisterResponse(RegisterResponseMessage *msg);


  QString token; // AuthClient owns the session token
};
