#include "ChunkingProtocolMessages.h"
#include "Hasher.h"
bool TransferProgress::isComplete() const {
  return totalParts != 0 && currentPartNumber >= totalParts;
}

quint8 TransferProgress::progressPercent() const {
  if (totalParts == 0) {
    return 0;
  }
  return static_cast<quint8>((currentPartNumber * 100) / totalParts);
}

// Records that `partNumber` was confirmed (acked/received).
// Sets the high-water mark and returns true if this completed the transfer.
bool TransferProgress::recordConfirmedPart(quint32 partNumber) {
  currentPartNumber = partNumber;
  const bool done = isComplete();
  if (done) {
    currentPhase = TransferPhase::COMPLETED;
  }
  return done;
}

void TransferProgress::recordRetry(quint32 partNumber) {
  if(retriesPerPart.contains(partNumber)){
    retriesPerPart[partNumber] += 1;
  }else {
    retriesPerPart.insert(partNumber,1); 
  }
}

bool checkHashMatchesThatOfContent(const QByteArray &sentHash,
                                   const QByteArray &content) {
  auto actualHash = Hasher::hash(content);
  return actualHash == sentHash;
}
QByteArray hashContents(const QByteArray &content) {
  return Hasher::hash(content);
}
