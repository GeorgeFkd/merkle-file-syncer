#pragma once

#include "ChunkingProtocolMessages.h"
#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <functional>

struct ServerUploadState {
  TransferProgress transferProgress;
};

struct ServerDownloadState {
  TransferProgress transferProgress;
};

using ClientId = QString;
using TransferKey = QPair<ClientId, QString>; // (clientId, path)

struct ServerWriteCommand {
  ClientId client;
  QString path;
  QByteArray bytes;
  quint64 offset;
  quint64 partNumber;
};

struct ChunkingServerInMsgCtx {
  ClientId clientId;
};

struct ChunkingServerOutMsgCtx {
  ClientId clientId;
};

class ChunkingServer : public QObject {
  Q_OBJECT
public:
  using ChunkSizeCalculator =
      std::function<quint64(const ClientId &, const QString &, quint64 fileSize,
                            quint64 desiredChunkSize)>;
  using ChunkReader = std::function<QByteArray(
      const ClientId &, const QString &path, quint64 offset, quint64 length)>;

  using MetadataReader =
      std::function<quint64(const ClientId &, const QString &path)>;

  explicit ChunkingServer(QObject *parent = nullptr);

  void setChunkSizeCalculator(ChunkSizeCalculator calc);
  void setReader(ChunkReader reader);
  void setMetadataReader(MetadataReader reader);

  void onMessage(std::shared_ptr<Message> msg, ChunkingServerInMsgCtx msgInCtx);
  void handleRequestChunkSizeForUpload(
      const ClientId &clientId, std::shared_ptr<RequestChunkSizeForUpload> msg);
  void handleRequestChunkSizeForDownload(
      const ClientId &clientId,
      std::shared_ptr<RequestChunkSizeForDownload> msg);

  void handleChunkReceived(const ClientId &clientId,
                           std::shared_ptr<ChunkTransfer> msg);
  void handleAckChunkOfDownload(const ClientId &clientId,
                                std::shared_ptr<ACKChunkReceived> msg);
  void handleCancelReceived(const ClientId &clientId,
                            std::shared_ptr<CancelTransfer> msg);

Q_SIGNALS:
  void uploadCompleted(ClientId, QString path);
  void uploadStarted(ClientId, QString path, quint64 fileSize,
                     quint64 chunkSize);
  void uploadCancelled(ClientId, QString path);

  void downloadCompleted(ClientId clientId, QString path);
  void downloadCancelled(ClientId, QString path);

  void chunkToUploadArrived(ServerWriteCommand writeCmd);

  void sendMessage(std::shared_ptr<Message> msg,
                   ChunkingServerOutMsgCtx msgOutCtx);

private:
  void sendPart(const ClientId &clientId, const QString &path,
                quint32 partNumber);
  ChunkSizeCalculator chunkSizeCalculator;
  ChunkReader reader;
  MetadataReader metadataReader;
  QHash<TransferKey, ServerUploadState> uploadStates;
  QHash<TransferKey, ServerDownloadState> downloadStates;
};
