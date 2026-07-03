#pragma once

#include "MerkleProtocolMessages.h"
#include "MerkleTree.h"
#include "FileTree.h"
#include <QObject>

class MerkleSyncClient : public QObject {
  Q_OBJECT
public:
  explicit MerkleSyncClient(QObject *parent = nullptr);

  void startNegotiation(MerkleTree *tree);
  void handleResponse(const MerkleProtocolMessage &msg, MerkleTree *tree);

  const NegotiationState *getNegotiationState() const;

Q_SIGNALS:
  void messageSendRequest(MerkleProtocolMessage msg);
  void negotiationCompleted(const NegotiationState& state);

private:
  NegotiationState negotiationState;
  bool inProgress = false;
};


