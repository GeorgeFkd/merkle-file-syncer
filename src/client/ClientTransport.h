#pragma once
#include "Messages.h"
#include <QIODevice>
#include <QObject>


class ClientTransport : public QObject {
  Q_OBJECT
public:
    ~ClientTransport() override = default;
  virtual void configure(const QString& endpoint) = 0;
  virtual void connectToServer() = 0;
  virtual void send(const Message& msg) = 0;

Q_SIGNALS:
  void connected();
  void disconnected();
  void messageReady(Message *msg);
};
