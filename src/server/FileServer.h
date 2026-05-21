#pragma once

#include "FileDb.h"
#include "FileStorage.h"
#include "MerkleTree.h"
#include "Messages.h"
#include "SessionRegistry.h"
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
  ~FileServer();
  void configure(FileServerConfig config);
  bool isListening();
  QString serverName();
  FileStorage *getStorage();
  // meant to be used in testing only
  bool writeFile(const QString &user, const QString &file,
                 const QByteArray &contents, const QDateTime &mtime);
Q_SIGNALS:
  void authMessageReceived(QLocalSocket *socket, AuthMessage *msg);
  void syncRequestReceived(QLocalSocket *socket, SyncRequestMessage *msg);
  void unrecognizedMessageReceived(QLocalSocket *socket, Message *msg);
  void merkleSyncRequestReceived(QLocalSocket *socket, MerkleSyncMessage *msg);

private:
  void setupConnections();
  void handleAuth(QLocalSocket *socket, AuthMessage *msg);
  void handleUnrecognized(QLocalSocket *socket, Message *msg);
  void handleSyncRequest(QLocalSocket *socket, SyncRequestMessage *msg);
  void handleMerkleSyncRequest(QLocalSocket *socket, MerkleSyncMessage *msg);
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
  void trySendNewerFile(SyncRequestMessage &response,const QString& username, const QString &path,
                        const QDateTime &serverMtime);
  QString getUserFrom(Message* msg);
  MerkleTree *getUserTree(const QString &username);
  std::optional<QString> getUsername(const QString &token);

  FileDb database;
  QHash<QLocalSocket *, QByteArray> buffers;
  QLocalServer server;
  QString serverUrl;
  std::unique_ptr<FileStorage> fileStorage;
  struct QStringHash {
    size_t operator()(const QString &s) const { return qHash(s); }
  };

  std::unordered_map<QString, std::unique_ptr<MerkleTree>, QStringHash>
      userTrees;
  SessionRegistry sessionStore;
  QHash<QLocalSocket *, QString> socketToTokenMap;
  bool verifyUserCredentials(const QString &username, const QString &password);

  std::optional<Session> resolveSession(const QString &token);
};
