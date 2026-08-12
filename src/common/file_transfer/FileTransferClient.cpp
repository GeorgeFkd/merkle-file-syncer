#include "FileTransferClient.h"

FileTransferClient::FileTransferClient(FileStorage *storage,
                                       const QString &user, QObject *parent)
    : QObject(parent), storage(storage), user(user) {
  wireProtocolToStorage();
  wireProtocolToTransport();
}

void FileTransferClient::startUpload(const QString &path) {
  storage->beginWrite(user, path, 0, 0);
  chunking.startUpload(path);
}

void FileTransferClient::startDownload(const QString &path,
                                       quint64 desiredChunkSize) {
  storage->beginWrite(user, path, 0, 0);
  chunking.startDownload(path, desiredChunkSize);
}

void FileTransferClient::cancelUpload(const QString &path) {
  chunking.cancelUpload(path);
}

void FileTransferClient::cancelDownload(const QString &path) {
  chunking.cancelDownload(path);
}

void FileTransferClient::wireProtocolToStorage() {
  const QString u = user;
  chunking.setReader([this, u](const QString &path, quint64 offset,
                               quint64 length) -> QByteArray {
    auto bytes = storage->readRange(u, path, static_cast<qint64>(offset),
                                    static_cast<qint64>(length));
    return bytes.value_or(QByteArray());
  });

  chunking.setMetadataReader([this, u](const QString &path) -> quint64 {
    auto size = storage->fileSize(u, path);
    return size.has_value() ? static_cast<quint64>(size.value()) : 0;
  });

  QObject::connect(&chunking, &ChunkingClient::writeRequested, this,
                   [this, u](const WriteCommand &w) {
                     storage->writeRange(
                         u, w.path, 0, static_cast<qint64>(w.offset), w.bytes);
                   });

  QObject::connect(&chunking, &ChunkingClient::downloadCompleted, this,
                   [this](const QString &path) {
                     storage->finishWrite(user, path);
                     Q_EMIT downloadCompleted(path);
                   });

  QObject::connect(&chunking, &ChunkingClient::downloadCancelled, this,
                   [this](const QString &path) {
                     storage->abortWrite(user, path);
                     Q_EMIT downloadCancelled(path);
                   });
}

void FileTransferClient::wireProtocolToTransport() {
  QObject::connect(
      &chunking, &ChunkingClient::sendMessage, this,
      [this](std::shared_ptr<Message> msg) { Q_EMIT(sendMessage(msg)); });

  // protocol lifecycle signals -> re-emit as our own
  QObject::connect(&chunking, &ChunkingClient::uploadCompleted, this,
                   &FileTransferClient::uploadCompleted);
  QObject::connect(&chunking, &ChunkingClient::downloadCompleted, this,
                   &FileTransferClient::downloadCompleted);
  QObject::connect(&chunking, &ChunkingClient::uploadCancelled, this,
                   &FileTransferClient::uploadCancelled);
  QObject::connect(&chunking, &ChunkingClient::downloadCancelled, this,
                   &FileTransferClient::downloadCancelled);
  QObject::connect(&chunking, &ChunkingClient::uploadProgress, this,
                   &FileTransferClient::uploadProgress);
  QObject::connect(&chunking, &ChunkingClient::downloadProgress, this,
                   &FileTransferClient::downloadProgress);
}

void FileTransferClient::onMessage(std::shared_ptr<Message> msg) {
  chunking.onMessage(msg);
}
