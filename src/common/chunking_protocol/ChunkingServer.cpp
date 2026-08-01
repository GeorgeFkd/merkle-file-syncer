#include "ChunkingServer.h"
#include "Messages.h"
#include <QDebug>

ChunkingServer::ChunkingServer(QObject *parent) : QObject(parent) {}

void ChunkingServer::onMessage(const Message *msg,
                               ChunkingServerInMsgCtx msgInCtx) {
  if (msg->type() == MessageType::RequestChunkSizeUpload) {
    handleRequestChunkSizeForUpload(
        msgInCtx.clientId, static_cast<const RequestChunkSizeForUpload *>(msg));
    return;
  }

  if (msg->type() == MessageType::RequestChunkSizeDownload) {
    handleRequestChunkSizeForDownload(
        msgInCtx.clientId,
        static_cast<const RequestChunkSizeForDownload *>(msg));
    return;
  }

  if (msg->type() == MessageType::ChunkTransfer) {
    handleChunkReceived(msgInCtx.clientId,
                        static_cast<const ChunkTransfer *>(msg));
    return;
  }

  if (msg->type() == MessageType::AckChunk) {
    handleAckChunkOfDownload(msgInCtx.clientId,
                             static_cast<const ACKChunkReceived *>(msg));
    return;
  }

  if (msg->type() == MessageType::CancelTransfer) {
    handleCancelReceived(msgInCtx.clientId,
                         static_cast<const CancelTransfer *>(msg));
    return;
  }
}

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
    const ClientId &clientId, const RequestChunkSizeForUpload *msg) {
  Q_ASSERT_X(chunkSizeCalculator, "handleRequestChunkSizeForUpload",
             "chunkSizeCalculator not set");

  const quint64 chunkSize =
      chunkSizeCalculator(clientId, msg->path, msg->fileSize, 0);
  Q_ASSERT_X(chunkSize > 0, "handleRequestChunkSizeForUpload",
             "calculator returned chunkSize 0");

  const quint32 totalChunks =
      static_cast<quint32>((msg->fileSize + chunkSize - 1) / chunkSize);

  ServerUploadState state;
  state.transferProgress.currentPartNumber = 0;
  state.transferProgress.totalParts = totalChunks;
  state.transferProgress.chunkSize = chunkSize;
  uploadStates.insert(TransferKey{clientId, msg->path}, state);

  auto msgOut = SpecifyChunkSizeUpload{msg->path, chunkSize, totalChunks};
  Q_EMIT(uploadStarted(clientId, msg->path, msg->fileSize, chunkSize));
  auto msgOutCtx = ChunkingServerOutMsgCtx{clientId};
  Q_EMIT sendMessage(std::make_shared<SpecifyChunkSizeUpload>(
                         msg->path, chunkSize, totalChunks),
                     msgOutCtx);
}

void ChunkingServer::handleRequestChunkSizeForDownload(
    const ClientId &clientId, const RequestChunkSizeForDownload *msg) {
  Q_ASSERT_X(chunkSizeCalculator, "handleRequestChunkSizeForDownload",
             "chunkSizeCalculator not set");
  Q_ASSERT_X(metadataReader, "handleRequestChunkSizeForDownload",
             "metadataReader not set");

  const quint64 fileSize = metadataReader(clientId, msg->path);
  const quint64 chunkSize =
      chunkSizeCalculator(clientId, msg->path, fileSize, msg->chunkSizeDesired);
  Q_ASSERT_X(chunkSize > 0, "handleRequestChunkSizeForDownload",
             "calculator returned chunkSize 0");

  const quint32 totalChunks =
      static_cast<quint32>((fileSize + chunkSize - 1) / chunkSize);

  const TransferKey key{clientId, msg->path};

  ServerDownloadState state;
  state.transferProgress.currentPartNumber = 1;
  state.transferProgress.totalParts = totalChunks;
  state.transferProgress.chunkSize = chunkSize;
  downloadStates.insert(key, state);

  auto msgOut = SpecifyChunkSizeDownload(msg->path, chunkSize, totalChunks);
  auto msgOutCtx = ChunkingServerOutMsgCtx{clientId};
  Q_EMIT sendMessage(std::make_shared<SpecifyChunkSizeDownload>(
                         msg->path, chunkSize, totalChunks),
                     msgOutCtx);

  // server drives the download: send the first chunk immediately.
  sendPart(clientId, msg->path, 1);
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

  auto msgOutCtx = ChunkingServerOutMsgCtx{clientId};
  Q_EMIT(
      sendMessage(std::make_shared<ChunkTransfer>(
                      path, partNumber, it->transferProgress.chunkSize, bytes),
                  msgOutCtx));
}

void ChunkingServer::handleChunkReceived(const ClientId &clientId,
                                         const ChunkTransfer *msg) {
  const TransferKey key{clientId, msg->filepath};
  auto it = uploadStates.find(key);
  Q_ASSERT_X(it != uploadStates.end(), "handleChunkReceived",
             "no upload state for (clientId, path)");

  // it->transferProgress.currentPartNumber = msg.partNumber;
  const quint64 offset = static_cast<quint64>(msg->partNumber - 1) *
                         it->transferProgress.chunkSize;
  const bool uploadFinished =
      it->transferProgress.recordConfirmedPart(msg->partNumber);
  // 'it' must not be used past this point: the emits below re-enter and can
  // rehash uploadStates, invalidating this iterator.

  Q_EMIT chunkToUploadArrived(ServerWriteCommand{
      clientId, msg->filepath, msg->bytes, offset, msg->partNumber});

  if (uploadFinished) {
    uploadStates.remove(key);
  }

  auto msgOutCtx = ChunkingServerOutMsgCtx{clientId};
  Q_EMIT(sendMessage(std::make_shared<ACKChunkReceived>(
                         msg->filepath, msg->partNumber, QString(), QString()),
                     msgOutCtx));

  if (uploadFinished) {
    Q_EMIT uploadCompleted(clientId, msg->filepath);
  }
}

void ChunkingServer::handleAckChunkOfDownload(const ClientId &clientId,
                                              const ACKChunkReceived *msg) {
  auto it = downloadStates.find(TransferKey{clientId, msg->path});
  Q_ASSERT_X(it != downloadStates.end(), "handleAckChunkOfDownload",
             "no download state for (clientId, path)");

  if (!msg->failureType.isEmpty()) {
    qDebug() << "Download chunk failure:" << clientId << msg->path << "part"
             << msg->partNumber << "type" << msg->failureType << "msg"
             << msg->failureMsg;
    return;
  }

  const bool finishedDownload =
      it->transferProgress.recordConfirmedPart(msg->partNumber);

  if (finishedDownload) {
    const QString path = msg->path;
    downloadStates.remove(TransferKey{clientId, path});
    Q_EMIT downloadCompleted(clientId, path);
    return;
  }

  const quint32 nextPart = msg->partNumber + 1;
  sendPart(clientId, msg->path, nextPart);
}

void ChunkingServer::handleCancelReceived(const ClientId &clientId,
                                          const CancelTransfer *msg) {
  const TransferKey key{clientId, msg->path};

  if (uploadStates.contains(key)) {
    uploadStates.remove(key);
    Q_EMIT uploadCancelled(clientId, msg->path);
    return;
  }

  if (downloadStates.contains(key)) {
    downloadStates.remove(key);
    Q_EMIT downloadCancelled(clientId, msg->path);
    return;
  }

  assert(false && "Cancellation was given the wrong clientId,path pair");
}
