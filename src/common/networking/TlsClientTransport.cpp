#include "TlsClientTransport.h"
#include <QDebug>
#include <QSslConfiguration>
#include <QFile>

static QSslCertificate loadCert(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning() << "TLS: cannot open cert file" << path;
    return QSslCertificate();
  }
  return QSslCertificate(f.readAll(), QSsl::Pem);
}

TlsClientTransport::TlsClientTransport(QObject *parent) {
  // NOTE: signal maps to the interface's connected() ONLY after the TLS
  // handshake, so downstream never sends into an unencrypted socket.
  connect(&socket, &QSslSocket::encrypted, this,
          &TlsClientTransport::onEncrypted);
  connect(&socket, &QSslSocket::readyRead, this,
          &TlsClientTransport::onReadyRead);
  connect(&socket, &QAbstractSocket::disconnected, this,
          [this]() { Q_EMIT disconnected(); });
  connect(&socket,
          QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors), this,
          &TlsClientTransport::onSslErrors);
  connect(&socket,
          QOverload<QAbstractSocket::SocketError>::of(
              &QAbstractSocket::errorOccurred),
          this, &TlsClientTransport::onSocketError);
}

void TlsClientTransport::configure(const QString &endpoint) {
  const int colon = endpoint.lastIndexOf(':');
  host = endpoint.left(colon);
  port = static_cast<quint16>(endpoint.mid(colon + 1).toUShort());
  qDebug() << "TlsClientTransport::configure host=" << host << "port=" << port;
}

void TlsClientTransport::connectToServer() {
  const QString caPath = qEnvironmentVariable("TLS_CA_PATH");
  QSslConfiguration cfg = socket.sslConfiguration();

  if (!caPath.isEmpty()) {
    auto ca = loadCert(caPath);          // std::optional<QSslCertificate>
    if (!ca.isNull()) {
      auto cas = cfg.caCertificates();
      cas.append(ca);                    // deref the optional
      cfg.setCaCertificates(cas);
    } else {
      qWarning() << "TlsClientTransport: failed to load CA from" << caPath;
      return;
    }
  }
  socket.setSslConfiguration(cfg);
  socket.connectToHostEncrypted(host, port);
}

void TlsClientTransport::onEncrypted() {
  qDebug() << "TLS handshake complete, connection encrypted";
  Q_EMIT connected(); // interface "connected" == TLS-ready, not TCP-ready
}

void TlsClientTransport::send(std::shared_ptr<Message> msg) {
  if (socket.state() != QAbstractSocket::ConnectedState ||
      !socket.isEncrypted()) {
    qWarning() << "TlsClientTransport::send before encrypted connection ready";
    return;
  }
  MessageProtocol::sendMessage(&socket, msg);
}

void TlsClientTransport::onReadyRead() {
  MessageProtocol::processBuffer(
      &socket, buffer,
      [this](std::shared_ptr<Message> msg) { Q_EMIT messageReady(msg); });
}

void TlsClientTransport::onSslErrors(const QList<QSslError> &errors) {
  for (const auto &e : errors) {
    qWarning() << "SSL error:" << e.errorString();
  }
  if (insecureSkipVerify) {
    // dev: ignore verification failures and proceed with the handshake
    socket.ignoreSslErrors();
  }
  // In prod: do NOT ignore. Leaving them unignored aborts the handshake,
  // which is the correct, secure behavior on an untrusted cert.
}

void TlsClientTransport::onSocketError(QAbstractSocket::SocketError err) {
  qWarning() << "TlsClientTransport socket error:" << err
             << socket.errorString();
}
