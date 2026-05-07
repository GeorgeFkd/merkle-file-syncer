#include "FileClient.h"

FileClient::FileClient() {}
void FileClient::connectToServer(const QString& serverName) {
  socket.connectToServer(serverName);
}
void FileClient::init() {
  QObject::connect(&socket, &QLocalSocket::connected, [this]() {
    QObject::connect(&socket, &QLocalSocket::readyRead, [this]() {
      auto bytes = socket.readAll();
      auto *msg = Message::deserialize(bytes);
      switch (msg->type()) {
      case MessageType::ClientAuth: {
        handleAuthResponse(static_cast<AuthResponseMessage *>(msg));
        break;
      }
      default: {
        handleUnrecognized(msg);
        break;
      }
      }
    });
  });
  QObject::connect(&timer,&QTimer::timeout,[this]() {
    clientTick();
  });
  timer.start(3000);
}

void FileClient::clientTick() {
  qDebug() << "Client syncing stuff" << "\n";
}

void FileClient::handleAuthResponse(AuthResponseMessage *msg) {
  if (msg->success) {
    qDebug() << "Auth successful, starting sync timer";
  } else {
    qDebug() << "Auth failed";
  }
}

void FileClient::handleUnrecognized(Message *msg) {
  qDebug() << "Unrecognized message type received";
}
