#include "LocalServerTransport.h"
#include "Messages.h"

LocalServerTransport::~LocalServerTransport() {
  server.close();
  for (auto *socket : buffers.keys()) {
    socket->disconnect(this);
  }
}

void LocalServerTransport::configure(const QString &endpoint) {
  serverUrl = endpoint;
}

void LocalServerTransport::start() {
  QLocalServer::removeServer(serverUrl);
  if (!server.listen(serverUrl)) {
    qDebug() << "LocalServerTransport::start: listen failed for" << serverUrl;
    return;
  }
  QObject::connect(&server, &QLocalServer::newConnection, this,
                   &LocalServerTransport::onNewConnection);
}

bool LocalServerTransport::isListening() const { return server.isListening(); }

QString LocalServerTransport::endpoint() const { return server.serverName(); }

void LocalServerTransport::onNewConnection() {
  qDebug() << "New connection received";
  QLocalSocket *socket = server.nextPendingConnection();
  if (!socket) {
    qDebug() << "onNewConnectino: null pending socket";
    return;
  }

  buffers.insert(socket, QByteArray{});
  wireSocket(socket);
  Q_EMIT newConnection(socket);
}

void LocalServerTransport::send(QIODevice* connection,std::shared_ptr<Message> msg) {
  MessageProtocol::sendMessage(connection, msg);
}

void LocalServerTransport::wireSocket(QLocalSocket *socket) {
  QObject::connect(socket, &QLocalSocket::disconnected, socket,
                   &QLocalSocket::deleteLater);

  QObject::connect(socket, &QLocalSocket::disconnected, this,
                   [this, socket]() { onSocketDisconnected(socket); });
  QObject::connect(socket, &QLocalSocket::readyRead, this,
                   [this, socket]() { onSocketReadyRead(socket); });
}

void LocalServerTransport::onSocketReadyRead(QIODevice *socket) {
  qDebug() << "Ready read event fired.";
  MessageProtocol::processBuffer(
      socket, buffers[socket],
      [this, socket](std::shared_ptr<Message>msg) { Q_EMIT messageReady(socket, msg); });
}


void LocalServerTransport::onSocketDisconnected(QIODevice* socket) {
  Q_EMIT disconnected(socket);
  buffers.remove(socket);
}
