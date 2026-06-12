#pragma once 
#include "ServerTransport.h"
#include <QHash>
#include <QLocalSocket>
#include <QLocalServer>


class LocalServerTransport : public ServerTransport {
  Q_OBJECT

public: 
    ~LocalServerTransport() override;
    void configure(const QString& endpoint) override;
    void start() override;
    bool isListening() const override;
    QString endpoint() const override;
    void send(QIODevice* connection,const Message &msg) override;

private: 
    void onNewConnection();
    void wireSocket(QLocalSocket* socket);
    void onSocketReadyRead(QIODevice* socket);
    void onSocketDisconnected(QIODevice* socket);

    QLocalServer server;
    QString serverUrl;
    QHash<QIODevice*,QByteArray> buffers;

};
