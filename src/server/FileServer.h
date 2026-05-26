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
  ~FileServer();

  void configure(FileServerConfig config);
  void start();
  bool isListening();
  QString serverName();
  FileStorage *getStorage();

  // meant to be used in testing only
  bool writeFile(const QString &user, const QString &file,
                 const QByteArray &contents, const QDateTime &mtime);

private:
  // --- Server lifecycle / connections ---
  QLocalServer server;
  QString serverUrl;
  QHash<QLocalSocket *, QByteArray> buffers;
  void setupConnections();
  void setupNewSocketConnection(QLocalSocket *socket);

  // --- Auth / sessions ---
  SessionRegistry sessionStore;
  QHash<QLocalSocket *, QString> socketToTokenMap;
  AuthResponseMessage handleAuth(AuthMessage *msg);
  bool verifyUserCredentials(const QString &username, const QString &password);
  std::optional<Session> resolveSession(const QString &token);
  std::optional<QString> getUsername(const QString &token);
  QString getUserFrom(Message *msg);

  // --- Storage / DB / per-user merkle trees ---
  std::unique_ptr<FileStorage> fileStorage;
  FileDb database;
  struct QStringHash {
    size_t operator()(const QString &s) const { return qHash(s); }
  };
  std::unordered_map<QString, std::unique_ptr<MerkleTree>, QStringHash>
      userTrees;
  MerkleTree *getUserTree(const QString &username);

  // --- Sync request handling ---
  SyncRequestMessage handleSyncRequest(SyncRequestMessage *msg);
  SyncRequestMessage
  handleWriteRequest(SyncRequestMessage *msg, const QString &storageKey,
                     const std::optional<QDateTime> &storedMtime);
  SyncRequestMessage
  handleDeleteRequest(SyncRequestMessage *msg, const QString &storageKey,
                      const std::optional<QDateTime> &storedMtime);
  SyncRequestMessage trySendNewerFile(const QString &username,
                                      const QString &path,
                                      const QDateTime &serverMtime,FileOperationType op);

  // --- Listing ---
  ListResponseMessage handleListRequest(ListRequestMessage *msg);

  // --- Merkle negotiation ---
  MerkleSyncMessage handleMerkleSyncRequest(MerkleSyncMessage *msg);

  // --- Misc ---
  void handleUnrecognized(Message *msg);
};
