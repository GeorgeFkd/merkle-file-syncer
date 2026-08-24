#include "TlsServerTransport.h"
#include "Messages.h"
#include <QFile>
#include <QSslConfiguration>

// helper (file-static in the .cpp)
static QSslCertificate loadCert(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "TLS: cannot open cert file" << path;
    return QSslCertificate();
  }
  return QSslCertificate(f.readAll(), QSsl::Pem);
}
static QSslKey loadKey(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "TLS: cannot open key file" << path;
    return {};
  }
  return QSslKey(f.readAll(), QSsl::Rsa, QSsl::Pem);
}

TlsServerTransport::~TlsServerTransport() {
  server.close();
  for (auto *socket : buffers.keys()) {
    socket->disconnect(this);
  }
}

void TlsServerTransport::configure(const QString &endpoint) {
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

void TlsServerTransport::start() {
  const QString certPath = qEnvironmentVariable("TLS_CERT_PATH");
  const QString keyPath = qEnvironmentVariable("TLS_KEY_PATH");
  if (certPath.isEmpty() || keyPath.isEmpty()) {
    qWarning() << "TlsServerTransport::start: TLS_CERT_PATH / TLS_KEY_PATH not set";
    return;
  }

  auto cert = loadCert(certPath);
  auto key = loadKey(keyPath);
  if (cert.isNull() || key.isNull()) {
    qWarning() << "TlsServerTransport::start: failed to load cert/key";
    return;
  }

  QSslConfiguration cfg = server.sslConfiguration();
  cfg.setLocalCertificate(cert);
  cfg.setPrivateKey(key);
  server.setSslConfiguration(cfg);


  if (!server.listen(listenAddress, listenPort)) {
    qDebug() << "TlsServerTransport::start: listen failed for"
             << listenAddress.toString() << "port: " << listenPort
             << ". Error:" << server.errorString();
    return;
  }
  qDebug() << "TlsServerTransport::start: listening on"
           << server.serverAddress().toString() << ":" << server.serverPort();
  QObject::connect(&server, &QSslServer::pendingConnectionAvailable, this,
                   &TlsServerTransport::onNewConnection);
}

bool TlsServerTransport::isListening() const { return server.isListening(); }

QString TlsServerTransport::endpoint() const {
  QHostAddress addr = server.serverAddress();
  if (addr == QHostAddress::Any || addr == QHostAddress::AnyIPv4) {
    addr = QHostAddress::LocalHost;
  }
  return addr.toString() + ":" + QString::number(server.serverPort());
}

void TlsServerTransport::send(QIODevice *connection,
                              std::shared_ptr<Message> msg) {
  MessageProtocol::sendMessage(connection, msg);
}

void TlsServerTransport::onNewConnection() {
  qDebug() << "TlsServerTransport: new connection received";
  QTcpSocket *pending = server.nextPendingConnection();
  if (!pending) {
    qDebug() << "TlsServerTransport::onNewConnection: null pending socket";
    return;
  }
  // QSslServer hands back already-encrypted QSslSockets
  auto *socket = qobject_cast<QSslSocket *>(pending);
  if (!socket) {
    qDebug() << "TlsServerTransport::onNewConnection: not a QSslSocket";
    pending->deleteLater();
    return;
  }

  buffers.insert(socket, QByteArray{});
  wireSocket(socket);
  Q_EMIT newConnection(socket);
}

void TlsServerTransport::wireSocket(QSslSocket *socket) {
  QObject::connect(socket, &QSslSocket::disconnected, socket,
                   &QSslSocket::deleteLater);
  QObject::connect(socket, &QSslSocket::disconnected, this,
                   [this, socket]() { onSocketDisconnected(socket); });
  QObject::connect(socket, &QSslSocket::readyRead, this,
                   [this, socket]() { onSocketReadyRead(socket); });
}

void TlsServerTransport::onSocketReadyRead(QIODevice *socket) {
  qDebug() << "Ready read event fired.";
  MessageProtocol::processBuffer(socket, buffers[socket],
                                 [this, socket](std::shared_ptr<Message> msg) {
                                   Q_EMIT messageReady(socket, msg);
                                 });
}

void TlsServerTransport::onSocketDisconnected(QIODevice *socket) {
  Q_EMIT disconnected(socket);
  buffers.remove(socket);
}
