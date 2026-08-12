#pragma once

#include "FSMetadata.h"
#include "Messages.h"
#include <QObject>
#include <QString>

using ConnectionId = QString;

class NaiveSyncServer : public QObject {
  Q_OBJECT
public:
  explicit NaiveSyncServer(QObject *parent = nullptr);

  void handleRequest(ListRequestMessage *msg, ConnectionId,const FSMetadata* updatedDb,const QString& username);

Q_SIGNALS:
  void sendMessage(std::shared_ptr<ListResponseMessage>,ConnectionId);
};
