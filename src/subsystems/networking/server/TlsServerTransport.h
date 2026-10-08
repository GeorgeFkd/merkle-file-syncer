#pragma once
#include "ServerTransport.h"
#include "Messages.h"
#include <QSslServer>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QHostAddress>
#include <QHash>

class TlsServerTransport : public ServerTransport {
  Q_OBJECT
public:
  ~TlsServerTransport() override;
  void configure(const QString &endpoint) override;
  void start() override;
  bool isListening() const override;
  QString endpoint() const override;
  void send(QIODevice *connection, std::shared_ptr<Message> msg) override;

  void setCertificate(const QSslCertificate &cert) { localCert = cert; }
  void setPrivateKey(const QSslKey &key) { privateKey = key; }

private:
  void onNewConnection();
  void wireSocket(QSslSocket *socket);
  void onSocketReadyRead(QIODevice *socket);
  void onSocketDisconnected(QIODevice *socket);

  QSslServer server;
  QHostAddress listenAddress;
  quint16 listenPort = 0;
  QSslCertificate localCert;
  QSslKey privateKey;
  QHash<QIODevice *, QByteArray> buffers;
};
