#include "AuthServer.h"
#include <chrono>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

using traits = jwt::traits::kazuho_picojson;
using jwt_claim = jwt::basic_claim<traits>;

static constexpr auto kIssuer = "auth-server";
static constexpr auto kTokenTtl = std::chrono::hours(24 * 7);

AuthServer::AuthServer(SessionRegistry *sessions, UsersDb *users,
                       QObject *parent)
    : QObject(parent), sessions(sessions), users(users),
      keys(loadSigningKeys()) {} // qFatal aborts if keys absent

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

std::optional<QString> AuthServer::resolveSessionId(const QString &token) const {
  try {
    auto decoded = jwt::decode<traits>(token.toStdString());
    jwt::verify<traits>()
        .allow_algorithm(
            jwt::algorithm::rs256(keys.publicPem.toStdString(), "", "", ""))
        .with_issuer(kIssuer)
        .verify(decoded); // signature + alg + exp; throws on failure
    QString sessionId = QString::fromStdString(decoded.get_id());
    if (!sessions->isActive(sessionId)) // allowlist — catches logout/delete
      return std::nullopt;
    return sessionId;
  } catch (const std::exception &) {
    return std::nullopt; // contained here — no throw escapes AuthServer
  }
}

void AuthServer::handleRegister(RegisterMessage *msg,
                                const AuthServerInMsgCtx &ctx) {
  auto resp = std::make_shared<RegisterResponseMessage>();
  // NOTE: credential-check stand-in; needs UsersDb::usernameTaken to be correct.
  if (users->userExists(msg->username)) {
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
  if (msg->username.isEmpty() || msg->deviceName.isEmpty()) {
    resp->success = false;
    resp->error = "username and deviceName required";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }
  // One live session per (user, device). Carried over from the hand-rolled
  // auth path in FileServer so integrating this does not silently drop it.
  if (sessions->hasSession(msg->username, msg->deviceName)) {
    resp->success = false;
    resp->error = "device already connected";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }
  if (!users->verifyUserCredentials(msg->username, msg->password)) {
    resp->success = false;
    resp->error = "invalid credentials";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }
  QString sessionId = sessions->createSession(msg->username, msg->deviceName);
  auto now = std::chrono::system_clock::now();
  std::string token =
      jwt::create<traits>()
          .set_issuer(kIssuer)
          .set_type("JWT")
          .set_id(sessionId.toStdString())
          .set_subject(msg->username.toStdString())
          .set_payload_claim("deviceName",
                             jwt_claim(msg->deviceName.toStdString()))
          .set_issued_at(now)
          .set_expires_at(now + kTokenTtl)
          .sign(jwt::algorithm::rs256("", keys.privatePem.toStdString(), "",
                                      ""));
  resp->success = true;
  resp->token = QString::fromStdString(token);
  Q_EMIT sendMessage(resp, {ctx.connection});
  Q_EMIT sessionEstablished(ctx.connection, sessionId, msg->username);
}

void AuthServer::handleLogout(LogoutMessage *msg,
                              const AuthServerInMsgCtx &ctx) {
  auto resp = std::make_shared<LogoutResponseMessage>();
  auto sessionId = resolveSessionId(ctx.token);
  if (!sessionId) {
    resp->success = false;
    resp->error = "no active session";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }
  sessions->revokeSession(*sessionId);
  resp->success = true;
  Q_EMIT sendMessage(resp, {ctx.connection});
  Q_EMIT sessionEnded(ctx.connection, *sessionId);
}

void AuthServer::handleDeleteAccount(DeleteAccountMessage *msg,
                                     const AuthServerInMsgCtx &ctx) {
  auto resp = std::make_shared<DeleteAccountResponseMessage>();
  auto sessionId = resolveSessionId(ctx.token);
  if (!sessionId) {
    resp->success = false;
    resp->error = "no active session";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }
  QString username = sessions->getSession(*sessionId)->username;
  if (!users->verifyUserCredentials(username, msg->password)) { // re-auth confirmation
    resp->success = false;
    resp->error = "invalid credentials";
    Q_EMIT sendMessage(resp, {ctx.connection});
    return;
  }
  users->deleteUser(username);
  sessions->revokeAllForUser(username);
  resp->success = true;
  Q_EMIT sendMessage(resp, {ctx.connection});
  Q_EMIT sessionEnded(ctx.connection, *sessionId);
  Q_EMIT accountDeleted(username);
}
