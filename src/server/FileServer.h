#pragma once
#include "AuthServer.h"
#include "FSMetadata.h"
#include "FileStorage.h"
#include "FileTransferServer.h"
#include "MerkleSyncServer.h"
#include "MerkleTree.h"
#include "Messages.h"
#include "NaiveSyncServer.h"
#include "ServerTransport.h"
#include "SessionRegistry.h"
#include "UsersDb.h"
#include <QLocalServer>
#include <QLocalSocket>

struct FileServerConfig {
  TransportProtocol protocol;
  QString serverName;
  std::unique_ptr<FileStorage> storage;
  // Optional: hand in a pre-populated account store (tests seed it so clients
  // can log in without a registration round trip). Empty means a fresh, empty
  // UsersDb, i.e. nobody can log in until they register.
  std::unique_ptr<UsersDb> users;
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

Q_SIGNALS:
  void sendMessage(std::shared_ptr<Message> msg, ConnectionId conn);

private:
  // --- Server lifecycle / connections ---
  std::unique_ptr<ServerTransport> transport;
  QLocalServer server;
  QString serverUrl;
  void setupConnections();
  void setupSocketConnections();
  void setupAuthConnections();

  void setupNegotiationConnections();
  void setupFileTransferConnections();
  void onSocketDisconnected(QIODevice *socket);
  void onSocketReadyRead(QIODevice *socket);
  void onNewConnection(QIODevice *socket);
  void dispatch(QIODevice *socket, std::shared_ptr<Message> msg);
  void setupNewSocketConnection(QLocalSocket *socket);

  void sendToClient(const ConnectionId &conn, std::shared_ptr<Message> msg);

  static QString transferMetadataKey(const ClientId &conn, const QString &path) {
    return conn + "|" + path;
  }
  QHash<QString, QPair<QByteArray, QDateTime>> pendingTransfersMetadata;

  // --- Auth / sessions ---
  // Connection identity is minted per socket and is independent of auth: it
  // exists before a client has logged in (register and failed-login replies
  // need somewhere to go) and it is what every subsystem downstream is keyed
  // on. The token never leaves this file — connToSession holds the session a
  // connection has proven it owns.
  SessionRegistry sessionStore;
  std::unique_ptr<UsersDb> usersDb;
  std::unique_ptr<AuthServer> authServer;
  QHash<QIODevice *, ConnectionId> socketToConn;
  QHash<ConnectionId, QString> connToSession;

  void bindSession(ConnectionId conn, QString sessionId, QString username);
  void unbindSession(const ConnectionId &conn);
  // Returns the session id this connection owns, but only if the token the
  // message actually carried resolves to that same session. A globally valid
  // token presented on someone else's connection is refused.
  std::optional<QString> authorize(const ConnectionId &conn,
                                   const QString &token);
  std::optional<QString> usernameFor(const ConnectionId &conn) const;

  // --- Storage / DB / per-user merkle trees ---
  std::unique_ptr<FileStorage> fileStorage;
  FSMetadata database;
  struct QStringHash {
    size_t operator()(const QString &s) const { return qHash(s); }
  };
  std::unordered_map<QString, std::unique_ptr<MerkleTree>, QStringHash>
      userTrees;
  MerkleTree *getUserTree(const QString &username);
  void recordFile(const QString &username, const QString &path,
                  const QDateTime &mtime, const QByteArray &hash);
  void recordDeletion(const QString &username, const QString &path,
                      const QDateTime &deletedAt);
  QByteArray hashContents(const QByteArray &contents);

  // --- Sync request handling ---
  std::shared_ptr<DeleteRequestMessage>
  handleDeleteRequest(std::shared_ptr<DeleteRequestMessage> msg,
                      const ConnectionId &conn);
  std::unique_ptr<FileTransferServer> fileTransferServer;
  void fillDownloadMetadata(SpecifyChunkSizeDownload*,const QString& user);
  void storeUploadMetadata(RequestChunkSizeForUpload *,
                           const ConnectionId &conn);

  // --- Listing ---
  void handleListRequest(std::shared_ptr<ListRequestMessage> msg,
                         const ConnectionId &conn);
  NaiveSyncServer naiveSyncServer;

  // --- Merkle negotiation ---
  void handleMerkleSyncRequest(std::shared_ptr<MerkleSyncMessage> msg,
                               const ConnectionId &conn);
  MerkleSyncServer merkleSyncServer;

  // --- Misc ---
  void handleUnrecognized(Message *msg);
};
