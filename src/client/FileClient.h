#pragma once

#include "FileDb.h"
#include "FileHasher.h"
#include "LocalFileStorage.h"
#include "MerkleTree.h"
#include "Messages.h"
#include <QLocalSocket>
#include <QString>
#include <QTimer>

// FileCommand: Operation Type,Path,Contents,Mtime

struct NodesDiff {
  // the bool is to signal whether it is a file or not(true -> file, false
  // ->directory) so we can later ask the server for whole directories
  QList<QPair<bool, QString>> onlyInLeft;
  QList<QPair<bool, QString>> onlyInRight;
  QList<QString> modified;
};

struct NegotiationState {
  NodesDiff diffEntries;
  QList<QString> directoriesToCheckWithServer;
};

enum class SyncStrategy { Naive, Merkle };
enum class ClientState {
  Disconnected,
  Connected,
  Authenticated,
  Authenticating
};

struct FileClientConfig {
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
  void setupConnections();
  void configure(const FileClientConfig &config);
  void clientTick();
  LocalFileStorage *getStorage();
  void start();
  NegotiationState *getNegotiationState();
  bool writeFile(const QString &user, const QString &path,
                 const QByteArray &contents);
  bool deleteFile(const QString &user, const QString &path);

Q_SIGNALS:
  void syncCompleted();
  void negotiationCompleted();
  void authenticated();
  void outboundFileCommandsReady();

private:
  void connectToServer();
  void stageNewFilesForSending(const QList<QString> &files);
  void stageDeletedFilesForSending(const QList<QString> &files);
  void sendAuthRequest();
  QString getDeviceName();
  void stageDownloadFor(const QString &path);
  void stageDeleteFor(const QString &path);
  void stageConflictResolution(const QString &path);
  void stageUploadFor(const QString &path);
  void stageDirectoryUpload(const QString &dirPath);
  void requestDirectoryList(const QString &dirPath);
  void resolveServerHasFileClientDoesnt(const QString &path);
  SyncRequestMessage buildSyncRequest(const QString &path, FileOperationType op,
                                      const QByteArray &contents,
                                      const std::optional<QDateTime> &mtime);
  void applyServerVersion(const QString& path,const QByteArray& contents);

  QLocalSocket *socket = nullptr;

  QTimer timer;
  unsigned int tickIntervalMs;
  bool shouldUseTimer = true;

  void handleAuthResponse(AuthResponseMessage *msg);
  void handleSyncResponse(SyncRequestMessage *msg);
  void handleMerkleSyncResponse(MerkleSyncMessage *msg);
  void handleWriteResponse(SyncRequestMessage *msg);
  void handleListResponse(ListResponseMessage *msg);
  void handleDeleteResponse(SyncRequestMessage *msg);
  void handleUnrecognized(Message *msg);

  QList<QString> discoverNewFiles();
  QList<QString> discoverDeletedFiles();
  void flushOutboundCommands();
  void checkSyncCompletionAndUnlock();

  void merkleTick();
  void handleNegotiationCompleted();
  void naiveTick();
  bool currentlyDoingSyncOps = false;
  bool awaitingListResponse = false;
  bool inMerkleApply = false;
  int pendingDirectoryRequests = 0;
  int pendingMessages = 0;
  QString username, password;
  QByteArray buffer;
  std::unique_ptr<LocalFileStorage> fileStorage;
  FileDb database;
  SyncStrategy syncStrategy;
  std::unique_ptr<MerkleTree> merkleTree;
  QString serverName;
  QString deviceName;

  bool currentlyNegotiatingFileDiffs = false;
  QList<QString> toDescend;
  NegotiationState negotiationState;

  bool pendingTick = false;
  QString token;
  ClientState state = ClientState::Disconnected;
  QHash<QString, SyncRequestMessage> commandsToSend;
};
