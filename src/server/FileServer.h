#pragma once
#include "FileDb.h"
#include "FileStorage.h"
#include "MerkleSyncServer.h"
#include "MerkleTree.h"
#include "Messages.h"
#include "ServerTransport.h"
#include "SessionRegistry.h"
#include <QLocalServer>
#include <QLocalSocket>

struct FileServerConfig {
  TransportProtocol protocol;
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
  std::unique_ptr<ServerTransport> transport;
  QLocalServer server;
  QString serverUrl;
  QHash<QIODevice *, QByteArray> buffers;
  void setupConnections();
  void setupSocketConnections();
  void onSocketDisconnected(QIODevice *socket);
  void onSocketReadyRead(QIODevice *socket);
  void onNewConnection();
  void dispatch(QIODevice *socket, Message *msg);
  void setupNewSocketConnection(QLocalSocket *socket);

  // --- Auth / sessions ---
  SessionRegistry sessionStore;
  QHash<QIODevice *, QString> socketToTokenMap;
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
  std::unique_ptr<MerkleTree> buildMerkleTree(const QString &username);
  QByteArray hashContents(const QByteArray &contents);

  // --- Sync request handling ---
  SyncRequestMessage handleSyncRequest(SyncRequestMessage *msg);
  SyncRequestMessage
  handleWriteRequest(SyncRequestMessage *msg, const QString &path,
                     const std::optional<QDateTime> &storedMtime);
  SyncRequestMessage
  handleDeleteRequest(SyncRequestMessage *msg, const QString &path,
                      const std::optional<QDateTime> &storedMtime);
  SyncRequestMessage trySendNewerFile(const QString &username,
                                      const QString &path,
                                      const QDateTime &serverMtime,
                                      FileOperationType op);

  // --- Chunking ---
  AckChunkMessage handleChunkUpload(ChunkTransferMessage *msg);
  void handleAckChunk(AckChunkMessage *msg);
  // --- Listing ---
  ListResponseMessage handleListRequest(ListRequestMessage *msg);

  // --- Merkle negotiation ---
  void handleMerkleSyncRequest(MerkleSyncMessage *msg);
  MerkleSyncServer merkleSyncServer;

  // --- Misc ---
  void handleUnrecognized(Message *msg);
};
