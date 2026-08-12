#pragma once

#include "MerkleProtocolMessages.h"
#include "MerkleTree.h"
#include <QObject>

class MerkleSyncClient : public QObject {
  Q_OBJECT
public:
  explicit MerkleSyncClient(QObject *parent = nullptr);

  void startNegotiation(MerkleTree *tree);
  void handleResponse(std::shared_ptr<MerkleProtocolMessage> msg, MerkleTree *tree);

  const NegotiationState *getNegotiationState() const;

Q_SIGNALS:
  void messageSendRequest(std::shared_ptr<MerkleProtocolMessage> msg);
  void negotiationCompleted(const NegotiationState& state);

private:
  NegotiationState negotiationState;
  bool inProgress = false;
};


