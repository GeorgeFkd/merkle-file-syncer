#include "ChunkingProtocolMessages.h"

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

// --- ChunkTransfer (bytes base64 in JSON; temporary until binary channel) ---
