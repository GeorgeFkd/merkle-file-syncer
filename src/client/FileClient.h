#pragma once
#include "FileTransferClient.h"
#include "ClientTransport.h"
#include "FSMetadata.h"
#include "LocalFileStorage.h"
#include "MerkleProtocolMessages.h"
#include "MerkleSyncClient.h"
#include "MerkleTree.h"
#include "Messages.h"
#include "NaiveSyncClient.h"
#include "UsersDb.h"
#include <QString>
#include <QTimer>

enum class SyncStrategy { Naive, Merkle };

enum class ClientState {
  Disconnected,
  Connected,
  Authenticating,
  Authenticated,
};

// A snapshot of how the local filesystem differs from the last-known DB state.
// Produced by a pure scan; consumed by an apply step that reconciles the DB
// and merkle tree. New/modified carry the filesystem mtime; deleted carry the
// time the deletion was detected (or its recorded tombstone time).
// TODO: Add inodes so i can later detect renames
using FileChangeMetadata = QPair<QString, QDateTime>;
struct LocalChangeSet {
  QList<FileChangeMetadata> newFiles;
  QList<FileChangeMetadata> modifiedFiles;
  QList<FileChangeMetadata> deletedFiles;
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
  void scanFilesystemAndApplyChangesToDb();
  std::optional<QDateTime> writeFile(const QString &user, const QString &path,
                                     const QByteArray &contents);

  LocalFileStorage *getStorage();
  const NegotiationState *getNegotiationState() const;

Q_SIGNALS:
  void syncCompleted();
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
  void dispatch(std::shared_ptr<Message>);
  void onConnected();
  void onDisconnected();
  void onAuthenticated();
  void setupSocketConnections();
  QString getDeviceName();
  void handleAuthResponse(AuthResponseMessage *);

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
  UsersDb usersDb;
  FSMetadata database;
  std::unique_ptr<MerkleTree> merkleTree;
  MerkleTree *getMerkleTree();
  QByteArray hashContents(const QByteArray &contents);
  LocalChangeSet scanFilesystemForChanges() const;
  void applyChangesToDb(const LocalChangeSet &changes);
  void recordFile(const QString &username, const QString &path,
                  const QDateTime &mtime, const QByteArray &hash);
  void recordDeletion(const QString &username, const QString &path,
                      const QDateTime &mtime);
  void applyTombstone(const QString &path, const QDateTime &mtime);

  // --- Outbound command staging ---
  QHash<QString, std::shared_ptr<SyncRequestMessage>> commandsToSend;
  int pendingMessages = 0;
  void flushOutboundCommands();
  std::shared_ptr<SyncRequestMessage>
  buildSyncRequest(const QString &path, FileOperationType op,
                   const QByteArray &contents,
                   const std::optional<QDateTime> &mtime);
  void stageUploadFor(const QString &path);
  void stageDownloadFor(const QString &path);
  void stageDeleteFor(const QString &path, const QDateTime &deletedAt);
  void stageDirectoryUpload(const QString &dirPath);
  std::unique_ptr<FileTransferClient> fileTransferClient;
  int outstandingTransfers = 0;
  void onUploadCompleted(QString path);
  void transferDone();

  // --- Server file listing (naive pull + merkle apply expansion) ---
  bool awaitingListResponse = false;
  bool inMerkleApply = false;

  int pendingDirectoryRequests = 0;
  void stageDirectoryDownload(const QString &dirPath);
  void handleListResponse(std::shared_ptr<ListResponseMessage> msg);
  void handleMerkleDirectoryListing(std::shared_ptr<ListResponseMessage> msg);

  // --- Merkle negotiation ---
  bool currentlyNegotiatingFileDiffs = false;
  QList<QString> toDescend;
  void handleMerkleSyncResponse(MerkleSyncMessage *msg);
  void handleNegotiationCompleted(const NegotiationState &state);
  MerkleSyncClient merkleSyncClient;
  NaiveSyncClient naiveSyncClient;

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

inline QDebug operator<<(QDebug dbg, const LocalChangeSet &changes) {
  QDebugStateSaver saver(dbg);
  dbg.nospace() << "LocalChangeSet(\n"
                << "  new: " << changes.newFiles << "\n"
                << "  modified: " << changes.modifiedFiles << "\n"
                << "  deleted: " << changes.deletedFiles << "\n"
                << ")";
  return dbg;
}
