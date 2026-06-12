#pragma once
#include "ClientTransport.h"
#include <QByteArray>
#include <QLocalSocket>

class LocalClientTransport : public ClientTransport {
  Q_OBJECT
public:
    ~LocalClientTransport() override;
    void configure(const QString& endpoint) override;
    void connectToServer() override;
    void send(const Message& msg) override;

private: 
    void onConnected();
    void onDisconnected();
    void onReadyRead();

    QLocalSocket socket;
    QString serverUrl;
    QByteArray buffer;
};
