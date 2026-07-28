#include "ChunkingClient.h"
#include "ChunkingProtocolMessages.h"
#include <QDebug>

ChunkingClient::ChunkingClient(QObject *parent) : QObject(parent) {}

void ChunkingClient::setReader(ChunkReader reader) {
  this->reader = std::move(reader);
}

void ChunkingClient::setMetadataReader(MetadataReader reader) {
  this->metadataReader = std::move(reader);
}

void ChunkingClient::startUpload(const QString &path) {
  assert(metadataReader != nullptr);
  assert(reader != nullptr);
  const quint64 fileSize = metadataReader(path);

  ClientUploadState state;
  state.transferProgress.currentPartNumber = 1;
  state.transferProgress.totalParts = 0;
  state.transferProgress.chunkSize = 0;
  uploadStates.insert(path, state);

  Q_EMIT requestChunkSizeForUploadSendRequest(
      RequestChunkSizeForUpload{path, fileSize});
}

void ChunkingClient::startDownload(const QString &path,
                                   quint64 desiredChunkSize) {
  ClientDownloadState state;
  state.transferProgress.currentPartNumber = 1;
  state.transferProgress.totalParts = 0;
  state.transferProgress.chunkSize = 0;
  downloadStates.insert(path, state);

  Q_EMIT requestChunkSizeForDownloadSendRequest(
      RequestChunkSizeForDownload{path, desiredChunkSize});
}

void ChunkingClient::handleUploadSizeReceived(
    const SpecifyChunkSizeUpload &msg) {
  auto it = uploadStates.find(msg.path);
  Q_ASSERT_X(it != uploadStates.end(),
             "ChunkingClient::handleUploadSizeReceived",
             "msg.path contains path not stored in upload states");

  assert(msg.chunkSize != 0 && "Chunk Size should not be zero.");
  assert(msg.totalChunks != 0 && "Total Chunks should not be zero.");
  it->transferProgress.chunkSize = msg.chunkSize;
  it->transferProgress.totalParts = msg.totalChunks;

  sendPart(msg.path, it->transferProgress.currentPartNumber);
}

void ChunkingClient::sendPart(const QString &path, quint32 partNumber) {
  assert(partNumber > 0 && "Part number is 1-indexed");
  auto it = uploadStates.find(path);
  assert(it != uploadStates.end() &&
         "In send part path should be in upload states");

  const quint64 offset =
      static_cast<quint64>(partNumber - 1) * it->transferProgress.chunkSize;
  const QByteArray bytes = reader(path, offset, it->transferProgress.chunkSize);

  ChunkTransfer msg;
  msg.filepath = path;
  msg.partNumber = partNumber;
  msg.chunkSize = static_cast<quint32>(bytes.size());
  msg.bytes = bytes;

  Q_EMIT chunkTransferSendRequest(msg);
}

void ChunkingClient::handleAckChunkOfUpload(const ACKChunkReceived &ackMsg) {
  auto it = uploadStates.find(ackMsg.path);
  assert(it != uploadStates.end());
  if (!ackMsg.failureType.isEmpty()) {
    assert(!ackMsg.failureMsg.isEmpty());
    it->transferProgress.currentPhase = TransferPhase::FAILED;
    qDebug() << ackMsg;
    return;
  }

  const bool finishedUpload = it->transferProgress.recordConfirmedPart(ackMsg.partNumber);
  if (finishedUpload) {
    const QString path = ackMsg.path;
    uploadStates.remove(path);
    Q_EMIT uploadCompleted(path);
    return;
  }

  
  // 'it' must not be used after sendPart: it emits and re-enters, which can
  // rehash uploadStates and invalidate the iterator.
  sendPart(ackMsg.path, ackMsg.partNumber + 1);
}

void ChunkingClient::handleDownloadSizeReceived(
    const SpecifyChunkSizeDownload &msg) {
  auto it = downloadStates.find(msg.path);
  assert(it != downloadStates.end());

  it->transferProgress.chunkSize = msg.chunkSize;
  it->transferProgress.totalParts = msg.totalChunks;
}

void ChunkingClient::handleChunkReceived(const ChunkTransfer &msg) {
  auto it = downloadStates.find(msg.filepath);
  assert(it != downloadStates.end());

  const quint64 chunkSz = it->transferProgress.chunkSize;
  const quint32 totalParts = it->transferProgress.totalParts;
  const quint32 nextPart = msg.partNumber + 1;
  it->transferProgress.currentPartNumber = nextPart;
  //TODO make consistent the way the increments work
  const bool finishedDownloading = totalParts != 0 && nextPart > totalParts;
  // 'it' must not be used past this point: the emits below re-enter and can
  // rehash downloadStates, invalidating this iterator.

  const quint64 offset = static_cast<quint64>(msg.partNumber - 1) * chunkSz;

  Q_EMIT writeRequested(WriteCommand{msg.filepath, offset, msg.bytes});

  if (finishedDownloading) {
    downloadStates.remove(msg.filepath);
  }

  ACKChunkReceived ack;
  ack.path = msg.filepath;
  ack.partNumber = msg.partNumber;
  Q_EMIT ackChunkReceivedSendRequest(ack);

  if (finishedDownloading) {
    Q_EMIT downloadCompleted(msg.filepath);
  }
}
