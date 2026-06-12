#pragma once
#include "ClientTransport.h"
#include <QByteArray>
#include <QHostAddress>
#include <QTcpSocket>
#include <qhostaddress.h>
#include <qtmetamacros.h>
#include <qvariant.h>

class TcpClientTransport : public ClientTransport {
  Q_OBJECT
public:
  ~TcpClientTransport() override;

  void configure(const QString& endpoint) override;
  void connectToServer() override;
  void send(const Message& msg) override;

private:
  void onConnected();
  void onDisconnected();
  void onReadyRead();

  QTcpSocket socket;
  QHostAddress host;
  quint16 port = 0;
  QByteArray buffer;
};
