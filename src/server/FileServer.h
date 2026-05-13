#pragma once

#include "FileDb.h"
#include "FileStorage.h"
#include "Messages.h"
#include <QLocalServer>
#include <QLocalSocket>
class FileServer : QObject {
  Q_OBJECT
public:
  void handleConnection(QLocalSocket *socket);
  void listenOn(const QString &addr);
  bool isListening();
  QString serverName();
  QString getUserRootDirectory(const QString &username);
  void setFileStorageImpl(std::unique_ptr<FileStorage> storage);
  FileStorage* getStorage();
private:
  void handleAuth(QLocalSocket *socket, AuthMessage *msg);
  void handleUnrecognized(QLocalSocket *socket, Message *msg);
  void handleSyncRequest(QLocalSocket *socket, SyncRequestMessage *msg);
  FileDb database;
  QHash<QLocalSocket *, QByteArray> buffers;
  QString serverRootDir;
  QLocalServer server;
  std::unique_ptr<FileStorage> fileStorage;
};
