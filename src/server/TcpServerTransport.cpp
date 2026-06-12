#include "TcpServerTransport.h"
#include "Messages.h"
#include <qvariant.h>

TcpServerTransport::~TcpServerTransport() {
  server.close();
  for (auto *socket : buffers.keys()) {
    socket->disconnect(this);
  }
}

void TcpServerTransport::configure(const QString &endpoint) {
  QString ep = endpoint;
  int colon = ep.indexOf(':');
  if (colon >= 0) {
    listenAddress = QHostAddress(ep.left(colon));
    listenPort = ep.mid(colon + 1).toUShort();
  } else {
    listenAddress = QHostAddress::LocalHost;
    listenPort = ep.toUShort();
  }
}

void TcpServerTransport::start() {
  if (!server.listen(listenAddress, listenPort)) {
    qDebug() << "TcpServerTransport::start: listen failed for"
             << listenAddress.toString() << "port: " << listenPort
             << ". Error:" << server.errorString();
    return;
  }

  qDebug() << "TcpServerTransport::start: listening on"
           << server.serverAddress().toString() << ":" << server.serverPort();
  QObject::connect(&server, &QTcpServer::newConnection, this,
                   &TcpServerTransport::onNewConnection);
}

bool TcpServerTransport::isListening() const { return server.isListening(); }

QString TcpServerTransport::endpoint() const {
  QHostAddress addr = server.serverAddress();
  if (addr == QHostAddress::Any || addr == QHostAddress::AnyIPv4) {
    addr = QHostAddress::LocalHost;
  }
  return addr.toString() + ":" + QString::number(server.serverPort());
  // auto s = server.serverAddress().toString() + ":" +
  //          QString::number(server.serverPort());
  // qDebug() << "TcpServerTransport::endpoint:" << s;
  // return s;
}

void TcpServerTransport::send(QIODevice *connection, const Message &msg) {
  MessageProtocol::sendMessage(connection, msg);
}

void TcpServerTransport::onNewConnection() {
  qDebug() << "TcpServerTransport: new connection received";
  QTcpSocket *socket = server.nextPendingConnection();
  if (!socket) {
    qDebug() << "TcpServerTransport::onNewConnection: null pending socket";
    return;
  }

  buffers.insert(socket, QByteArray{});
  wireSocket(socket);
  Q_EMIT newConnection(socket);
}

void TcpServerTransport::wireSocket(QTcpSocket *socket) {
  QObject::connect(socket, &QTcpSocket::disconnected, socket,
                   &QTcpSocket::deleteLater);
  QObject::connect(socket, &QTcpSocket::disconnected, this,
                   [this, socket]() { onSocketDisconnected(socket); });
  QObject::connect(socket, &QTcpSocket::readyRead, this,
                   [this, socket]() { onSocketReadyRead(socket); });
}

void TcpServerTransport::onSocketReadyRead(QIODevice *socket) {
  qDebug() << "Ready read event fired.";
  MessageProtocol::processBuffer(
      socket, buffers[socket],
      [this, socket](Message *msg) { Q_EMIT messageReady(socket, msg); });
}

void TcpServerTransport::onSocketDisconnected(QIODevice *socket) {
  Q_EMIT disconnected(socket);
  buffers.remove(socket);
}
