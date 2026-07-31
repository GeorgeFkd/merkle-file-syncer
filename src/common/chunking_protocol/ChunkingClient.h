#pragma once
#include "ChunkingProtocolMessages.h"
#include <QHash>
#include <QObject>
#include <QString>

struct ClientUploadState {
  TransferProgress transferProgress;
};
struct ClientDownloadState {
  TransferProgress transferProgress;
};
struct WriteCommand {
  QString path;
  quint64 offset;
  // this is CoW so on assignment it just increments a ref-count
  QByteArray bytes;
};

class ChunkingClient : public QObject {
  Q_OBJECT
public:
  explicit ChunkingClient(QObject *parent = nullptr);
  using ChunkReader = std::function<QByteArray(const QString &path,
                                               quint64 offset, quint64 length)>;
  using MetadataReader = std::function<quint64(const QString &path)>;
  void setReader(ChunkReader reader);
  void setMetadataReader(MetadataReader reader);
  void startUpload(const QString &path);
  void startDownload(const QString &path, quint64 desiredChunkSize);
  void cancelUpload(const QString &path);
  void cancelDownload(const QString &path);

  void onMessage(const Message *msg);

  qint8 progressPercent(const QString &path);
  void handleAckChunkOfUpload(const ACKChunkReceived *ackMsg);
  void handleUploadSizeReceived(const SpecifyChunkSizeUpload *msg);
  void handleDownloadSizeReceived(const SpecifyChunkSizeDownload *msg);
  void handleChunkReceived(const ChunkTransfer *msg);

Q_SIGNALS:
  void uploadCompleted(QString path);
  void writeRequested(WriteCommand write);
  void downloadCompleted(QString path);
  void uploadCancelled(QString path);
  void downloadCancelled(QString path);
  // void partNumberAcked(QString path, quint32 partNumber);

  void sendMessage(std::shared_ptr<Message> msg);

private:
  void sendPart(const QString &path, quint32 partNumber);
  ChunkReader reader;
  MetadataReader metadataReader;
  QHash<QString, ClientUploadState> uploadStates;
  QHash<QString, ClientDownloadState> downloadStates;
};
