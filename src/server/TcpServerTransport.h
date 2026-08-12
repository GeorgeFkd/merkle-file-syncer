#pragma once 
#include "Messages.h"
#include "ServerTransport.h"
#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QIODevice>
#include <qhostaddress.h>
#include <qtcpserver.h>

class TcpServerTransport: public ServerTransport {
  Q_OBJECT
  public:
    ~TcpServerTransport() override;

    void configure(const QString& endpoint) override;
    void start() override;
    bool isListening() const override;
    QString endpoint() const override;
    void send(QIODevice* connection,std::shared_ptr<Message> msg) override;

private:
    void onNewConnection();
    void wireSocket(QTcpSocket* socket);
    void onSocketReadyRead(QIODevice* socket);
    void onSocketDisconnected(QIODevice* socket);

    QTcpServer server;
    QHostAddress listenAddress = QHostAddress::LocalHost;
    quint16 listenPort = 0;
    QHash<QIODevice*, QByteArray> buffers;

};
