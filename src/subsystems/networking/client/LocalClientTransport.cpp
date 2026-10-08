#include "LocalClientTransport.h"

LocalClientTransport::~LocalClientTransport() {
  socket.disconnect(this);
  if (socket.state() != QLocalSocket::UnconnectedState) {
    socket.disconnectFromServer();
  }
}

void LocalClientTransport::configure(const QString &endpoint) {
  serverUrl = endpoint;
}

void LocalClientTransport::connectToServer() {
  QObject::connect(&socket, &QLocalSocket::connected, this,
                   &LocalClientTransport::onConnected);
  QObject::connect(&socket, &QLocalSocket::disconnected, this,
                   &LocalClientTransport::onDisconnected);
  QObject::connect(&socket, &QLocalSocket::readyRead, this,
                   &LocalClientTransport::onReadyRead);

  socket.connectToServer(serverUrl);
}

void LocalClientTransport::send(std::shared_ptr<Message> msg) {
  MessageProtocol::sendMessage(&socket, msg);
}

void LocalClientTransport::onConnected() { Q_EMIT connected(); }

void LocalClientTransport::onDisconnected() { Q_EMIT disconnected(); }

void LocalClientTransport::onReadyRead() {
  MessageProtocol::processBuffer(
      &socket, buffer, [this](std::shared_ptr<Message> msg) { Q_EMIT messageReady(msg); });
}
