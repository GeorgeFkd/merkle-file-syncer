#pragma once
#include "ChunkingServer.h"
#include "FileStorage.h"
#include "Messages.h"
#include <QObject>
#include <QString>
#include <functional>

using ClientId = QString;

// might be able to just use the user provided

struct FileTransferServerInMsgCtx {
  ClientId clientId;
  QString user;
};

struct FileTransferServerOutMsgCtx {
  ClientId clientId;
};

class FileTransferServer : public QObject {
  Q_OBJECT
public:
  FileTransferServer(FileStorage *storage, QObject *parent = nullptr);

  void onMessage(std::shared_ptr<Message>, FileTransferServerInMsgCtx);

Q_SIGNALS:
  // clientId + path so a caller can observe per-client transfer lifecycle
  void uploadCompleted(ClientId, QString path);
  void uploadFailed(ClientId, QString path);
  void uploadCancelled(ClientId, QString path);

  void downloadCompleted(ClientId, QString path);
  void downloadFailed(ClientId, QString path);
  void downloadCancelled(ClientId, QString path);

  void sendMessage(std::shared_ptr<Message>, FileTransferServerOutMsgCtx);

private:
  void wireProtocolToTransport();
  void wireProtocolToStorage();

  QString userForClient(const ClientId &clientId) const;

  FileStorage *storage;
  ChunkingServer chunking;
  QHash<ClientId, QString> clientUsers;
  QHash<ClientId, bool> uploadHasBegun;
};
