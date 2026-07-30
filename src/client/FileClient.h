#pragma once
#include "ClientTransport.h"
#include "FileDb.h"
#include "LocalFileStorage.h"
#include "MerkleSyncClient.h"
#include "MerkleTree.h"
#include "Messages.h"
#include <QString>
#include <QTimer>
#include "MerkleProtocolMessages.h"

enum class SyncStrategy { Naive, Merkle };

enum class ClientState {
  Disconnected,
  Connected,
  Authenticating,
  Authenticated,
};

struct FileClientConfig {
  TransportProtocol protocol;
  QString rootDir;
  QString username;
  QString password;
  SyncStrategy syncStrategy;
  bool manualTick = false;
  unsigned int tickIntervalMs = 1000;
  QString serverName;
  QString deviceName;
};

class FileClient : public QObject {
  Q_OBJECT
public:
  FileClient();
  ~FileClient();

  void configure(const FileClientConfig &config);
  void start();
  void clientTick();
  void setupConnections();

  std::optional<QDateTime> writeFile(const QString &user, const QString &path,
                 const QByteArray &contents);
  std::optional<QDateTime> deleteFile(const QString &user, const QString &path);

  LocalFileStorage *getStorage();
  const NegotiationState *getNegotiationState() const;

Q_SIGNALS:
  void syncCompleted();
  void negotiationCompleted();
  void authenticated();
  void outboundFileCommandsReady();

private:
  // --- Identity / config ---
  QString username;
  QString password;
  QString deviceName;
  QString serverName;
  SyncStrategy syncStrategy;

  // --- Connection / auth state ---
  std::unique_ptr<ClientTransport> transport;
  ClientState state = ClientState::Disconnected;
  QString token;
  void connectToServer();
  void sendAuthRequest();
  void dispatch(Message *msg);
  void onConnected();
  void onDisconnected();
  void onAuthenticated();
  void setupSocketConnections();
  QString getDeviceName();
  void handleAuthResponse(AuthResponseMessage *msg);

  // --- Ticking ---
  QTimer timer;
  void startTimer();
  unsigned int tickIntervalMs;
  bool shouldUseTimer = true;
  bool pendingTick = false;
  bool currentlyDoingSyncOps = false;
  void naiveTick();
  void merkleTick();
  void checkSyncCompletionAndUnlock();

  // --- Local state (storage + DB + merkle tree) ---
  std::unique_ptr<LocalFileStorage> fileStorage;
  FileDb database;
  std::unique_ptr<MerkleTree> merkleTree;
  QByteArray hashContents(const QByteArray& contents);
  void buildMerkleTree(const std::string& rootDir,const QString& username);
  QList<QString> discoverNewFiles();
  QSet<QString> discoverDeletedFiles();
  void applyTombstone(const QString& path,const QDateTime& mtime);

  // --- Outbound command staging ---
  QHash<QString, SyncRequestMessage> commandsToSend;
  int pendingMessages = 0;
  void flushOutboundCommands();
  SyncRequestMessage buildSyncRequest(const QString &path, FileOperationType op,
                                      const QByteArray &contents,
                                      const std::optional<QDateTime> &mtime);
  void stageUploadFor(const QString &path);
  void stageDownloadFor(const QString &path);
  void stageDeleteFor(const QString &path,const QDateTime& deletedAt);
  void stageConflictResolution(const QString &path);
  void stageDirectoryUpload(const QString &dirPath);
  void stageNewFilesForSending(const QList<QString> &files);
  void stageDeletedFilesForSending(const QSet<QString> &files);
  void resolveServerHasFileClientDoesnt(const QString &path);

  // --- Server file listing (naive pull + merkle apply expansion) ---
  bool awaitingListResponse = false;
  bool inMerkleApply = false;
  int pendingDirectoryRequests = 0;
  void requestDirectoryList(const QString &dirPath);
  void handleListResponse(ListResponseMessage *msg);

  // --- Merkle negotiation ---
  bool currentlyNegotiatingFileDiffs = false;
  QList<QString> toDescend;
  void handleMerkleSyncResponse(MerkleSyncMessage *msg);
  void handleNegotiationCompleted(const NegotiationState& state);
  MerkleSyncClient merkleSyncClient;

  // --- Sync response handling ---
  void handleSyncResponse(SyncRequestMessage *msg);
  void handleWriteResponse(SyncRequestMessage *msg);
  void handleDeleteResponse(SyncRequestMessage *msg);
  void applyServerVersion(const QString &path, const QByteArray &contents);
  void handleChunkDownload(ChunkTransferMessage *msg);
  void handleChunkAck(AckChunkMessage *msg);

  // --- Misc ---
  void handleUnrecognized(Message *msg);
};
