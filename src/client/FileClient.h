#pragma once
#include "AuthClient.h"
#include "FileTransferClient.h"
#include "ClientTransport.h"
#include "FSMetadata.h"
#include "FSScanner.h"
#include "LocalFileStorage.h"
#include "MerkleProtocolMessages.h"
#include "MerkleSyncClient.h"
#include "MerkleTree.h"
#include "Messages.h"
#include "NaiveSyncClient.h"
#include <QString>
#include <QTimer>

enum class SyncStrategy { Naive, Merkle };

enum class ClientState {
  Disconnected,
  Connected,
  Authenticating,
  Authenticated,
};

// LocalChangeSet/FileChangeMetadata come from FSScanner.h. Produced by a pure
// scan; consumed by an apply step that reconciles the DB and merkle tree.
// New/modified carry the filesystem mtime; deleted carry the time the
// deletion was detected (or its recorded tombstone time).

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
  void downloadRequested(QString path);
  void uploadRequested(QString path);

private:
  // --- Identity / config ---
  QString username;
  QString password;
  QString deviceName;
  QString serverName;
  SyncStrategy syncStrategy;

  // --- Connection / auth state ---
  // AuthClient owns the session token and every auth exchange; nothing here
  // touches either. Outbound messages get the token stamped on at send time.
  std::unique_ptr<ClientTransport> transport;
  ClientState state = ClientState::Disconnected;
  AuthClient authClient;
  void connectToServer();
  void sendMessageToServer(std::shared_ptr<Message>);
  void dispatch(std::shared_ptr<Message>);
  void onConnected();
  void onDisconnected();
  void onAuthenticated();
  void setupSocketConnections();
  void setupAuthConnections();
  void setupNegotiationConnections();
  void setupFileTransferConnections();
  QString getDeviceName();

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
  QHash<QString, std::shared_ptr<DeleteRequestMessage>> commandsToSend;
  int pendingMessages = 0;
  void flushOutboundCommands();
  void stageUploadFor(const QString &path);
  void stageDownloadFor(const QString &path);
  void stageDeleteFor(const QString &path, const QDateTime &deletedAt);
  void stageDirectoryUpload(const QString &dirPath);
  std::unique_ptr<FileTransferClient> fileTransferClient;
  int outstandingTransfers = 0;
  void onUploadCompleted(QString path);
  void onDownloadCompleted(QString path);
  void transferDone();
  QHash<QString,QPair<QByteArray,QDateTime>> pendingDownloadMetadata;

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
  void handleDeleteResponse(DeleteRequestMessage *msg);
  void applyServerVersion(const QString &path, const QByteArray &contents);

  // --- Misc ---
  void handleUnrecognized(Message *msg);
};

inline QDebug operator<<(QDebug dbg, const LocalChangeSet &changes) {
  QDebugStateSaver saver(dbg);
  dbg.nospace() << "LocalChangeSet(\n"
                << "  new: " << changes.newFiles << "\n"
                << "  modified: " << changes.modifiedFiles << "\n"
                << "  deleted: " << changes.deletedFiles << "\n"
                << "  renamed: " << changes.renamedFiles.size() << "\n"
                << "  renamed directories: " << changes.renamedDirectories.size() << "\n"
                << ")";
  return dbg;
}
