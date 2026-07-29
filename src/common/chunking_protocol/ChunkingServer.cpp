#include "ChunkingServer.h"
#include <QDebug>

ChunkingServer::ChunkingServer(QObject *parent) : QObject(parent) {}

void ChunkingServer::setChunkSizeCalculator(ChunkSizeCalculator calc) {
  chunkSizeCalculator = std::move(calc);
}

void ChunkingServer::setReader(ChunkReader reader) {
  this->reader = std::move(reader);
}

void ChunkingServer::setMetadataReader(MetadataReader reader) {
  this->metadataReader = std::move(reader);
}

void ChunkingServer::handleRequestChunkSizeForUpload(
    const ClientId &clientId, const RequestChunkSizeForUpload &msg) {
  Q_ASSERT_X(chunkSizeCalculator, "handleRequestChunkSizeForUpload",
             "chunkSizeCalculator not set");

  const quint64 chunkSize =
      chunkSizeCalculator(clientId, msg.path, msg.fileSize, 0);
  Q_ASSERT_X(chunkSize > 0, "handleRequestChunkSizeForUpload",
             "calculator returned chunkSize 0");

  const quint32 totalChunks =
      static_cast<quint32>((msg.fileSize + chunkSize - 1) / chunkSize);

  ServerUploadState state;
  state.transferProgress.currentPartNumber = 0;
  state.transferProgress.totalParts = totalChunks;
  state.transferProgress.chunkSize = chunkSize;
  uploadStates.insert(TransferKey{clientId, msg.path}, state);

  Q_EMIT specifyChunkSizeUploadSendRequest(
      clientId, SpecifyChunkSizeUpload{msg.path, chunkSize, totalChunks});
}

void ChunkingServer::handleRequestChunkSizeForDownload(
    const ClientId &clientId, const RequestChunkSizeForDownload &msg) {
  Q_ASSERT_X(chunkSizeCalculator, "handleRequestChunkSizeForDownload",
             "chunkSizeCalculator not set");
  Q_ASSERT_X(metadataReader, "handleRequestChunkSizeForDownload",
             "metadataReader not set");

  const quint64 fileSize = metadataReader(clientId, msg.path);
  const quint64 chunkSize =
      chunkSizeCalculator(clientId, msg.path, fileSize, msg.chunkSizeDesired);
  Q_ASSERT_X(chunkSize > 0, "handleRequestChunkSizeForDownload",
             "calculator returned chunkSize 0");

  const quint32 totalChunks =
      static_cast<quint32>((fileSize + chunkSize - 1) / chunkSize);

  const TransferKey key{clientId, msg.path};

  ServerDownloadState state;
  state.transferProgress.currentPartNumber = 1;
  state.transferProgress.totalParts = totalChunks;
  state.transferProgress.chunkSize = chunkSize;
  downloadStates.insert(key, state);

  Q_EMIT specifyChunkSizeDownloadSendRequest(
      clientId, SpecifyChunkSizeDownload{msg.path, chunkSize, totalChunks});

  // server drives the download: send the first chunk immediately.
  sendPart(clientId, msg.path, 1);
}

void ChunkingServer::sendPart(const ClientId &clientId, const QString &path,
                              quint32 partNumber) {
  Q_ASSERT_X(reader, "sendChunk", "reader not set");
  auto it = downloadStates.find(TransferKey{clientId, path});
  Q_ASSERT_X(it != downloadStates.end(), "sendChunk",
             "no download state for (clientId, path)");

  const quint64 offset =
      static_cast<quint64>(partNumber - 1) * it->transferProgress.chunkSize;
  const QByteArray bytes =
      reader(clientId, path, offset, it->transferProgress.chunkSize);

  ChunkTransfer msg;
  msg.filepath = path;
  msg.partNumber = partNumber;
  msg.chunkSize = it->transferProgress.chunkSize;
  msg.bytes = bytes;

  Q_EMIT chunkTransferSendRequest(clientId, msg);
}

void ChunkingServer::handleChunkReceived(const ClientId &clientId,
                                         const ChunkTransfer &msg) {
  const TransferKey key{clientId, msg.filepath};
  auto it = uploadStates.find(key);
  Q_ASSERT_X(it != uploadStates.end(), "handleChunkReceived",
             "no upload state for (clientId, path)");

  // it->transferProgress.currentPartNumber = msg.partNumber;
  const quint64 offset =
      static_cast<quint64>(msg.partNumber - 1) * it->transferProgress.chunkSize;
  const bool uploadFinished =
      it->transferProgress.recordConfirmedPart(msg.partNumber);
  // 'it' must not be used past this point: the emits below re-enter and can
  // rehash uploadStates, invalidating this iterator.

  Q_EMIT chunkToUploadArrived(
      ServerWriteCommand{clientId, msg.filepath, msg.bytes, offset});

  if (uploadFinished) {
    uploadStates.remove(key);
  }

  Q_EMIT ackChunkReceivedSendRequest(
      clientId,
      ACKChunkReceived{msg.filepath, msg.partNumber, QString(), QString()});

  if (uploadFinished) {
    Q_EMIT uploadCompleted(clientId, msg.filepath);
  }
}

void ChunkingServer::handleAckChunkOfDownload(const ClientId &clientId,
                                              const ACKChunkReceived &msg) {
  auto it = downloadStates.find(TransferKey{clientId, msg.path});
  Q_ASSERT_X(it != downloadStates.end(), "handleAckChunkOfDownload",
             "no download state for (clientId, path)");

  if (!msg.failureType.isEmpty()) {
    qDebug() << "Download chunk failure:" << clientId << msg.path << "part"
             << msg.partNumber << "type" << msg.failureType << "msg"
             << msg.failureMsg;
    return;
  }

  const bool finishedDownload =
      it->transferProgress.recordConfirmedPart(msg.partNumber);

  if (finishedDownload) {
    const QString path = msg.path;
    downloadStates.remove(TransferKey{clientId, path});
    Q_EMIT downloadCompleted(clientId, path);
    return;
  }

  const quint32 nextPart = msg.partNumber + 1;
  sendPart(clientId, msg.path, nextPart);
}
