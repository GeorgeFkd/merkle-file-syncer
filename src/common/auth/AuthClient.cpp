// AuthClient.cpp
#include "AuthClient.h"

AuthClient::AuthClient(QObject *parent) : QObject(parent) {}

void AuthClient::registerAccount(const QString &username,
                                 const QString &password,
                                 const QString &deviceName) {
  auto msg = std::make_shared<RegisterMessage>();
  msg->username = username;
  msg->password = password;
  msg->deviceName = deviceName;
  stampToken(msg);
  Q_EMIT sendMessage(msg);
}

void AuthClient::login(const QString &username, const QString &password,
                       const QString &deviceName) {
  auto msg = std::make_shared<AuthMessage>();
  msg->username = username;
  msg->password = password;
  msg->deviceName = deviceName;
  stampToken(msg);
  Q_EMIT sendMessage(msg);
}

void AuthClient::logout() {
  auto msg = std::make_shared<LogoutMessage>();
  stampToken(msg);
  Q_EMIT sendMessage(msg);
}

void AuthClient::deleteAccount(const QString &password) {
  auto msg = std::make_shared<DeleteAccountMessage>();
  msg->password = password;
  stampToken(msg);
  Q_EMIT sendMessage(msg);
}

void AuthClient::stampToken(std::shared_ptr<Message> msg) const {
  msg->token = token;
}

void AuthClient::onMessage(std::shared_ptr<Message> msg) {
  switch (msg->type()) {
  case MessageType::ServerAuthResponse:
    handleAuthResponse(static_cast<AuthResponseMessage *>(msg.get()));
    break;
  case MessageType::LogoutResponse:
    handleLogoutResponse(static_cast<LogoutResponseMessage *>(msg.get()));
    break;
  case MessageType::DeleteAccountResponse:
    handleDeleteAccountResponse(
        static_cast<DeleteAccountResponseMessage *>(msg.get()));
    break;
  case MessageType::RegisterResponse:
    handleRegisterResponse(static_cast<RegisterResponseMessage *>(msg.get()));
    break;
  default:
    return; // not ours
  }
}

void AuthClient::handleAuthResponse(AuthResponseMessage *msg) {
  if (msg->success) {
    token = msg->token; // server-issued session token
    Q_EMIT authenticated(token);
  } else {
    Q_EMIT authFailed(msg->error);
  }
}

void AuthClient::handleLogoutResponse(LogoutResponseMessage *msg) {
  if (msg->success) {
    token.clear();
    Q_EMIT loggedOut();
  } else {
    Q_EMIT authFailed(msg->error);
  }
}

void AuthClient::handleDeleteAccountResponse(
    DeleteAccountResponseMessage *msg) {
  if (msg->success) {
    token.clear(); // account gone, session dead
    Q_EMIT accountDeleted();
  } else {
    Q_EMIT authFailed(msg->error);
  }
}

void AuthClient::handleRegisterResponse(RegisterResponseMessage *msg) {
  if (msg->success) {
    Q_EMIT registered();
  } else {
    Q_EMIT registerFailed(msg->error);
  }
}
