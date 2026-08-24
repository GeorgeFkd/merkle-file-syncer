#pragma once
#include "ClientTransport.h"
#include "Messages.h"
#include <QSslSocket>
#include <QByteArray>
#include <memory>

class TlsClientTransport : public ClientTransport {
  Q_OBJECT
public:
  explicit TlsClientTransport(QObject *parent = nullptr);
  ~TlsClientTransport() override = default;

  void configure(const QString &endpoint) override;   // "host:port"
  void connectToServer() override;
  void send(std::shared_ptr<Message> msg) override;

  // test/dev hook: skip peer cert verification (self-signed). NEVER in prod.
  void setInsecureSkipVerify(bool skip) { insecureSkipVerify = skip; }
  // pin a CA / expected cert for verifying a self-signed server
  void addCaCertificate(const QSslCertificate &cert) { extraCaCerts.append(cert); }

private:
  void onEncrypted();
  void onReadyRead();
  void onSslErrors(const QList<QSslError> &errors);
  void onSocketError(QAbstractSocket::SocketError err);

  QSslSocket socket;
  QByteArray buffer;
  QString host;
  quint16 port = 0;
  bool insecureSkipVerify = false;
  QList<QSslCertificate> extraCaCerts;
};
