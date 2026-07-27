#include "ChunkingProtocolMessages.h"

quint8 TransferProgress::progressPercent() const {
  if (totalParts == 0) {
    return 0;
  }
  return static_cast<quint8>((currentPartNumber * 100) / totalParts);
}

bool TransferProgress::isComplete() const {
  return totalParts != 0 && currentPartNumber == totalParts;
}
