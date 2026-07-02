#pragma once

#include "MerkleProtocolMessages.h"
#include "MerkleTree.h"
#include <QObject>
#include <QString>

using ConnectionId = QString;

class MerkleSyncServer : public QObject {
  Q_OBJECT
public:
  explicit MerkleSyncServer(QObject *parent = nullptr);

  void handleRequest(const MerkleProtocolMessage &msg, MerkleTree *tree,
                     ConnectionId conn);

Q_SIGNALS:
  void messageSendRequest(ConnectionId conn, MerkleProtocolMessage msg);
};
