#include "FileTransferServer.h"

FileTransferServer::FileTransferServer(FileStorage *storage, QObject *parent)
    : QObject(parent), storage(storage) {
  wireProtocolToTransport();
  wireProtocolToStorage();
}

void FileTransferServer::onMessage(const Message *msg,
                                   FileTransferServerInMsgCtx ctx) {
  // remember which user this client maps to, so storage side-effect signals
  // (which only carry clientId) can resolve the user for storage calls.
  clientUsers.insert(ctx.clientId, ctx.user);
  chunking.onMessage(msg, ChunkingServerInMsgCtx{ctx.clientId});
}

QString FileTransferServer::userForClient(const ClientId &clientId) const {
  return clientUsers.value(clientId);
}

// ---- outbound: chunking wants to send -> re-emit with routing ctx ----
void FileTransferServer::wireProtocolToTransport() {
  QObject::connect(
      &chunking, &ChunkingServer::sendMessage, this,
      [this](std::shared_ptr<Message> msg, ChunkingServerOutMsgCtx msgOutCtx) {
        Q_EMIT sendMessage(msg,
                           FileTransferServerOutMsgCtx{msgOutCtx.clientId});
      });

  // lifecycle signals -> re-emit as our own
  QObject::connect(&chunking, &ChunkingServer::uploadCompleted, this,
                   &FileTransferServer::uploadCompleted);
  QObject::connect(&chunking, &ChunkingServer::downloadCompleted, this,
                   &FileTransferServer::downloadCompleted);
  QObject::connect(&chunking, &ChunkingServer::uploadCancelled, this,
                   &FileTransferServer::uploadCancelled);
  QObject::connect(&chunking, &ChunkingServer::downloadCancelled, this,
                   &FileTransferServer::downloadCancelled);
}

// ---- storage: chunking receive/complete/cancel -> storage lifecycle ----
void FileTransferServer::wireProtocolToStorage() {
  // reader for downloads (server sends -> reads from storage)
  chunking.setReader([this](const ClientId &clientId, const QString &path,
                            quint64 offset, quint64 length) -> QByteArray {
    auto bytes = storage->readRange(userForClient(clientId), path,
                                    static_cast<qint64>(offset),
                                    static_cast<qint64>(length));
    return bytes.value_or(QByteArray());
  });

  chunking.setMetadataReader(
      [this](const ClientId &clientId, const QString &path) -> quint64 {
        auto size = storage->fileSize(userForClient(clientId), path);

        assert(size.has_value() && "something went horribly wrong");
        return size.has_value() ? static_cast<quint64>(size.value()) : 0;
      });

  chunking.setChunkSizeCalculator([this](const ClientId &, const QString &,
                                         quint64 fileSize,
                                         quint64 desiredChunkSize) -> quint64 {
    return storage->chunkSizeFor(fileSize, desiredChunkSize);
  });
  // upload receive: chunk arrived -> write to storage
  QObject::connect(&chunking, &ChunkingServer::chunkToUploadArrived, this,
                   [this](const ServerWriteCommand &w) {
                     const QString user = userForClient(w.client);
                     bool res = storage->writeRange(
                         userForClient(w.client), w.path, w.partNumber,
                         static_cast<qint64>(w.offset), w.bytes);
                     qDebug() << "Write range in server returned: " << res;
                   });

  // upload begins -> open storage write lifecycle
  QObject::connect(&chunking, &ChunkingServer::uploadStarted, this,
                   [this](const ClientId &clientId, const QString &path,
                          quint64 totalSize, quint64 chunkSize) {
                     bool res =
                         storage->beginWrite(userForClient(clientId), path,
                                             static_cast<qint64>(totalSize),
                                             static_cast<qint64>(chunkSize));
                     qDebug() << "beginWrite in server returned" << res;
                   });

  // upload complete -> finalize
  QObject::connect(&chunking, &ChunkingServer::uploadCompleted, this,
                   [this](const ClientId &clientId, const QString &path) {
                     if (storage->finishWrite(userForClient(clientId), path)) {
                       clientUsers.remove(clientId);
                     } else {
                       qDebug()
                           << "Finish write returned false for: " << clientId
                           << "," << path;
                     }
                   });

  // upload cancelled -> discard partial
  QObject::connect(&chunking, &ChunkingServer::uploadCancelled, this,
                   [this](const ClientId &clientId, const QString &path) {
                     if (storage->abortWrite(userForClient(clientId), path)) {
                       clientUsers.remove(clientId);
                     } else {
                       qDebug()
                           << "Abort write returned false for: " << clientId
                           << "," << path;
                     }
                   });

  QObject::connect(&chunking, &ChunkingServer::downloadCompleted, this,
                   [this](const ClientId &clientId, const QString &path) {
                     clientUsers.remove(clientId);
                   });
  QObject::connect(&chunking, &ChunkingServer::downloadCancelled, this,
                   [this](const ClientId &clientId, const QString &path) {
                     clientUsers.remove(clientId);
                   });
}
