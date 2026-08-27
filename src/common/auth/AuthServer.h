#pragma once
#include "Messages.h"
#include "SessionRegistry.h"
#include "UsersDb.h"
#include <QObject>
#include <QString>
#include <memory>

using ConnectionId = QString;
struct AuthServerInMsgCtx {
  ConnectionId connection;
  QString token; // empty for pre-auth (register/login)
};

struct AuthServerOutMsgCtx {
  ConnectionId connection;
};

class AuthServer : public QObject {
  Q_OBJECT
public:
  // Collaborators are injected, not owned. SessionStore is shared
  // infrastructure the whole server reads; AuthServer only writes session
  // transitions to it.
  AuthServer(SessionRegistry *sessions, UsersDb *users,
             QObject *parent = nullptr);

  // Self-filtering: switches on message type, early-returns non-auth messages.
  void onMessage(std::shared_ptr<Message> msg, const AuthServerInMsgCtx &ctx);

Q_SIGNALS:
  // Outbound response to the transport, carrying its destination connection.
  void sendMessage(std::shared_ptr<Message> msg, AuthServerOutMsgCtx out);

  // Storage cascade hook for account deletion — FileServer wires this to
  // blob/file cleanup. Declared now, not implemented this pass.
  void accountDeleted(const QString &username);

private:
  void handleRegister(RegisterMessage *msg, const AuthServerInMsgCtx &ctx);
  void handleLogin(AuthMessage *msg, const AuthServerInMsgCtx &ctx);
  void handleLogout(LogoutMessage *msg, const AuthServerInMsgCtx &ctx);

  // Stubbed this pass — signal/method present, body deferred.
  void handleDeleteAccount(DeleteAccountMessage *msg,
                           const AuthServerInMsgCtx &ctx);

  SessionRegistry *sessions; // not owned
  UsersDb *users;         // not owned
};
