#pragma once

#include "FileDb.h"
#include "FileStorage.h"
#include "Messages.h"
#include <QLocalServer>
#include <QLocalSocket>

struct FileServerConfig {
  QString serverName;
  std::unique_ptr<FileStorage> storage;
};

class FileServer : public QObject {
  Q_OBJECT
public:
  void start();
  void configure(FileServerConfig config);
  bool isListening();
  QString serverName();
  FileStorage *getStorage();
  //meant to be used in testing only
  bool writeFile(const QString &user, const QString &file,
                 const QByteArray &contents, const QDateTime &mtime);
Q_SIGNALS:
  void authMessageReceived(QLocalSocket *socket, AuthMessage *msg);
  void syncRequestReceived(QLocalSocket *socket, SyncRequestMessage *msg);
  void unrecognizedMessageReceived(QLocalSocket *socket, Message *msg);

private:
  void setupConnections();
  void handleAuth(QLocalSocket *socket, AuthMessage *msg);
  void handleUnrecognized(QLocalSocket *socket, Message *msg);
  void handleSyncRequest(QLocalSocket *socket, SyncRequestMessage *msg);
  void handleWriteResponse(SyncRequestMessage *msg);
  void handleDeleteResponse(SyncRequestMessage *msg);
  void setupNewSocketConnection(QLocalSocket *socket);
  void handleDeleteRequest(SyncRequestMessage *msg,
                           SyncRequestMessage &response,
                           const QString &storageKey,
                           const std::optional<QDateTime> &storedMtime);
  void handleWriteRequest(SyncRequestMessage *msg, SyncRequestMessage &response,
                          const QString &storageKey,
                          const std::optional<QDateTime> &storedMtime);
  void trySendNewerFile(SyncRequestMessage &response,
                                const QString &user, const QString &path,
                                const QDateTime &serverMtime);

  FileDb database;
  QHash<QLocalSocket *, QByteArray> buffers;
  QLocalServer server;
  QString serverUrl;
  std::unique_ptr<FileStorage> fileStorage;
};
