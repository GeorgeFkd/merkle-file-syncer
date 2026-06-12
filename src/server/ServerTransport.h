#pragma once
#include "Messages.h"
#include <QIODevice>
#include <QObject>

class ServerTransport : public QObject {
  Q_OBJECT

public: 
    ~ServerTransport() override = default;

    virtual void configure(const QString& endpoint) = 0;
    virtual void start() = 0;
    virtual bool isListening() const = 0;
    virtual QString endpoint() const = 0;

    virtual void send(QIODevice *connection,const Message& msg) = 0;

Q_SIGNALS:
    void newConnection(QIODevice* connection);
    void messageReady(QIODevice* connection,Message* msg);
    void disconnected(QIODevice* connection);
};
