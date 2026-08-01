#include "ChunkingClient.h"
#include "ChunkingProtocolMessages.h"
#include "Messages.h"
#include <QDebug>

ChunkingClient::ChunkingClient(QObject *parent) : QObject(parent) {}

void ChunkingClient::onMessage(const Message *msg) {
  if (msg->type() == MessageType::AckChunk) {
    handleAckChunkOfUpload(static_cast<const ACKChunkReceived *>(msg));
    return;
  }
  if (msg->type() == MessageType::SpecifyChunkSizeDownload) {
    handleDownloadSizeReceived(
        static_cast<const SpecifyChunkSizeDownload *>(msg));
    return;
  }

  if (msg->type() == MessageType::SpecifyChunkSizeUpload) {
    handleUploadSizeReceived(static_cast<const SpecifyChunkSizeUpload *>(msg));
    return;
  }

  if (msg->type() == MessageType::ChunkTransfer) {
    handleChunkReceived(static_cast<const ChunkTransfer *>(msg));
    return;
  }
}

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

  Q_EMIT(
      sendMessage(std::make_shared<RequestChunkSizeForUpload>(path, fileSize)));
}

void ChunkingClient::startDownload(const QString &path,
                                   quint64 desiredChunkSize) {
  ClientDownloadState state;
  state.transferProgress.currentPartNumber = 1;
  state.transferProgress.totalParts = 0;
  state.transferProgress.chunkSize = 0;
  downloadStates.insert(path, state);

  Q_EMIT(sendMessage(
      std::make_shared<RequestChunkSizeForDownload>(path, desiredChunkSize)));
}

void ChunkingClient::handleUploadSizeReceived(
    const SpecifyChunkSizeUpload *msg) {
  auto it = uploadStates.find(msg->path);
  Q_ASSERT_X(it != uploadStates.end(),
             "ChunkingClient::handleUploadSizeReceived",
             "msg.path contains path not stored in upload states");

  assert(msg->chunkSize != 0 && "Chunk Size should not be zero.");
  assert(msg->totalChunks != 0 && "Total Chunks should not be zero.");
  it->transferProgress.chunkSize = msg->chunkSize;
  it->transferProgress.totalParts = msg->totalChunks;

  sendPart(msg->path, it->transferProgress.currentPartNumber);
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

  Q_EMIT(sendMessage(std::make_shared<ChunkTransfer>(
      path, partNumber, static_cast<quint32>(bytes.size()), bytes)));
}

void ChunkingClient::handleAckChunkOfUpload(const ACKChunkReceived *msg) {
  auto it = uploadStates.find(msg->path);
  assert(it != uploadStates.end());
  if (!msg->failureType.isEmpty()) {
    assert(!msg->failureMsg.isEmpty());
    it->transferProgress.currentPhase = TransferPhase::FAILED;
    qDebug() << msg;
    return;
  }

  const bool finishedUpload =
      it->transferProgress.recordConfirmedPart(msg->partNumber);
  if (finishedUpload) {
    const QString path = msg->path;
    uploadStates.remove(path);
    Q_EMIT uploadCompleted(path);
    return;
  } else {
    Q_EMIT(uploadProgress(msg->path, msg->partNumber,
                          it->transferProgress.totalParts));
  }

  if (!uploadStates.contains(msg->path)) {
    qDebug() << "Upload state was removed in client, wont send next part.";
    return;
  }

  // 'it' must not be used after sendPart: it emits and re-enters, which can
  // rehash uploadStates and invalidate the iterator.
  sendPart(msg->path, msg->partNumber + 1);
}

void ChunkingClient::handleDownloadSizeReceived(
    const SpecifyChunkSizeDownload *msg) {
  auto it = downloadStates.find(msg->path);
  assert(it != downloadStates.end());

  it->transferProgress.chunkSize = msg->chunkSize;
  it->transferProgress.totalParts = msg->totalChunks;
}

void ChunkingClient::handleChunkReceived(const ChunkTransfer *msg) {
  auto it = downloadStates.find(msg->filepath);
  assert(it != downloadStates.end() && "download state was not found on client chunk receive.");
  const quint64 chunkSz = it->transferProgress.chunkSize;
  const quint32 totalParts = it->transferProgress.totalParts;
  const quint32 nextPart = msg->partNumber + 1;
  it->transferProgress.currentPartNumber = nextPart;
  // TODO make consistent the way the increments work
  const bool finishedDownloading = totalParts != 0 && nextPart > totalParts;
  // 'it' must not be used past this point: the emits below re-enter and can
  // rehash downloadStates, invalidating this iterator.
  const quint64 offset = static_cast<quint64>(msg->partNumber - 1) * chunkSz;
  Q_EMIT writeRequested(WriteCommand{msg->filepath, offset, msg->bytes});

  if (finishedDownloading) {
    downloadStates.remove(msg->filepath);
    Q_EMIT(sendMessage(std::make_shared<ACKChunkReceived>(
        msg->filepath, msg->partNumber, QString(), QString())));
    Q_EMIT downloadCompleted(msg->filepath);
    return;
  }

  Q_EMIT(downloadProgress(msg->filepath, msg->partNumber, totalParts));

  // a progress handler may have cancelled this download, removing its state
  if (!downloadStates.contains(msg->filepath)) {
    return;
  }

  Q_EMIT(sendMessage(std::make_shared<ACKChunkReceived>(
      msg->filepath, msg->partNumber, QString(), QString())));
}

void ChunkingClient::cancelUpload(const QString &path) {
  auto it = uploadStates.find(path);
  assert(it != uploadStates.end() &&
         "Cancel upload needs to have an existing upload");

  uploadStates.remove(path);
  Q_EMIT(sendMessage(std::make_shared<CancelTransfer>(path)));
  Q_EMIT uploadCancelled(path);
}

void ChunkingClient::cancelDownload(const QString &path) {
  auto it = downloadStates.find(path);
  if (it == downloadStates.end()) {
    qDebug() << "cancelDownload: no active download for" << path;
    return;
  }
  downloadStates.remove(path);
  Q_EMIT sendMessage(std::make_shared<CancelTransfer>(path));
  Q_EMIT downloadCancelled(path);
}
