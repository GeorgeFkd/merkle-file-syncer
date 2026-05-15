#pragma once

#include "FileDb.h"
#include "FileStorage.h"
#include "Messages.h"
#include <QLocalServer>
#include <QLocalSocket>
class FileServer : public QObject {
  Q_OBJECT
public:
  void listenOn(const QString &addr);
  bool isListening();
  QString serverName();
  QString getUserRootDirectory(const QString &username);
  void setFileStorageImpl(std::unique_ptr<FileStorage> storage);
  FileStorage *getStorage();
  bool writeFile(const QString &user, const QString &file,
                 const QByteArray &contents, const QDateTime &mtime);
Q_SIGNALS:
    void authMessageReceived(QLocalSocket *socket, AuthMessage *msg);
    void syncRequestReceived(QLocalSocket *socket, SyncRequestMessage *msg);
    void unrecognizedMessageReceived(QLocalSocket *socket, Message *msg);
private:
  void handleAuth(QLocalSocket *socket, AuthMessage *msg);
  void handleUnrecognized(QLocalSocket *socket, Message *msg);
  void handleSyncRequest(QLocalSocket *socket, SyncRequestMessage *msg);
  void handleConnection(QLocalSocket *socket);
  void handleDeleteRequest(SyncRequestMessage *msg,
                           SyncRequestMessage &response,
                           const QString &storageKey,
                           const std::optional<QDateTime> &storedMtime);
  void handleWriteRequest(SyncRequestMessage *msg, SyncRequestMessage &response,
                          const QString &storageKey,
                          const std::optional<QDateTime> &storedMtime);
  void fillRejectedWithContents(SyncRequestMessage &response,
                                const QString &user, const QString &path,
                                const QDateTime &serverMtime);

  FileDb database;
  QHash<QLocalSocket *, QByteArray> buffers;
  QString serverRootDir;
  QLocalServer server;
  std::unique_ptr<FileStorage> fileStorage;
};
