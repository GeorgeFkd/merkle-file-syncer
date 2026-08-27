// AuthServer.cpp
#include "AuthServer.h"

AuthServer::AuthServer(SessionRegistry *sessions, UsersDb *users,
                       QObject *parent)
    : QObject(parent), sessions(sessions), users(users) {}

void AuthServer::onMessage(std::shared_ptr<Message> msg,
                           const AuthServerInMsgCtx &ctx) {
  switch (msg->type()) {
  case MessageType::Register:
    handleRegister(static_cast<RegisterMessage *>(msg.get()), ctx);
    break;
  case MessageType::ClientAuth:
    handleLogin(static_cast<AuthMessage *>(msg.get()), ctx);
    break;
  case MessageType::Logout:
    handleLogout(static_cast<LogoutMessage *>(msg.get()), ctx);
    break;
  case MessageType::DeleteAccount:
    handleDeleteAccount(static_cast<DeleteAccountMessage *>(msg.get()), ctx);
    break;
  default:
    return; // not ours
  }
}

void AuthServer::handleRegister(RegisterMessage *msg,
                                const AuthServerInMsgCtx &ctx) {
  auto resp = std::make_shared<RegisterResponseMessage>();

  // NOTE: UsersDb has no username-only existence check; using credential
  // check as a stand-in. See flag below the code block.
  if (users->verifyUserCredentials(msg->username, msg->password)) {
    resp->success = false;
    resp->error = "username already registered";
  } else {
    users->storeUser(msg->username, msg->password,
                     msg->username); // rootDir = username
    resp->success = true;
  }
  Q_EMIT sendMessage(resp, {ctx.connection});
}

void AuthServer::handleLogin(AuthMessage *msg, const AuthServerInMsgCtx &ctx) {
  auto resp = std::make_shared<AuthResponseMessage>();

  if (!users->verifyUserCredentials(msg->username, msg->password)) {
    resp->success = false;
    resp->error = "invalid credentials";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }

  QString token = sessions->createSession(msg->username, msg->deviceName);
  resp->success = true;
  resp->token = token; // client reads new token from msg->token
  Q_EMIT sendMessage(resp, {ctx.connection});
}

void AuthServer::handleLogout(LogoutMessage *msg,
                              const AuthServerInMsgCtx &ctx) {
  auto resp = std::make_shared<LogoutResponseMessage>();

  if (ctx.token.isEmpty() || !sessions->getSession(ctx.token).has_value()) {
    resp->success = false;
    resp->error = "no active session";
  } else {
    sessions->revokeSession(ctx.token);
    resp->success = true;
  }
  Q_EMIT sendMessage(resp, {ctx.connection});
}

void AuthServer::handleDeleteAccount(DeleteAccountMessage *msg,
                                     const AuthServerInMsgCtx &ctx) {
  auto resp = std::make_shared<DeleteAccountResponseMessage>();

  auto username = sessions->getUsername(ctx.token);
  if (!username.has_value()) {
    resp->success = false;
    resp->error = "no active session";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }

  if (!users->verifyUserCredentials(*username, msg->password)) { // re-auth confirmation
    resp->success = false;
    resp->error = "invalid credentials";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }

  users->deleteUser(*username);
  sessions->revokeAllForUser(*username); // kill every device's session
  resp->success = true;
  Q_EMIT sendMessage(resp, {ctx.connection});

  Q_EMIT accountDeleted(*username); // storage cascade hook (FileServer wires this)
}
