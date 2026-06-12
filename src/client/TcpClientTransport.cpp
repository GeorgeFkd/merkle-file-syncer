#include "TcpClientTransport.h"
#include "Messages.h"

TcpClientTransport::~TcpClientTransport() {
  socket.disconnect(this);
  if (socket.state() != QTcpSocket::UnconnectedState) {
    socket.abort();
  }
}

void TcpClientTransport::configure(const QString &endpoint) {
  int colon = endpoint.indexOf(':');
  if (colon < 0) {
    qDebug() << "TcpClientTransport::configure: endpoint missing ':' separator"
             << endpoint;
    return;
  }
  host = QHostAddress(endpoint.left(colon));
  port = endpoint.mid(colon + 1).toUShort();
  qDebug() << "TcpClientTransport::configure: parsed" << endpoint
           << "as host=" << host.toString() << "port=" << port;
}

void TcpClientTransport::connectToServer() {
  QObject::connect(&socket, &QTcpSocket::connected, this,
                   &TcpClientTransport::onConnected);
  QObject::connect(&socket, &QTcpSocket::disconnected, this,
                   &TcpClientTransport::onDisconnected);
  QObject::connect(&socket, &QTcpSocket::readyRead, this,
                   &TcpClientTransport::onReadyRead);
  QObject::connect(
      &socket,
      QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
      this, [this](QAbstractSocket::SocketError) {
        qDebug() << "TcpClientTransport: socket error:" << socket.errorString();
      });

  qDebug() << "TcpClientTransport::connectToServer: connecting to"
           << host.toString() << ":" << port;

  socket.connectToHost(host, port);
}

void TcpClientTransport::send(const Message &msg) {
  MessageProtocol::sendMessage(&socket, msg);
}

void TcpClientTransport::onConnected() { Q_EMIT connected(); }

void TcpClientTransport::onDisconnected() { Q_EMIT disconnected(); }

void TcpClientTransport::onReadyRead() {
  MessageProtocol::processBuffer(
      &socket, buffer, [this](Message *msg) { Q_EMIT messageReady(msg); });
}
