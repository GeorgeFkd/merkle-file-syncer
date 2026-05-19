#pragma once

#include "FileDb.h"
#include "FileHasher.h"
#include "LocalFileStorage.h"
#include "MerkleTree.h"
#include "Messages.h"
#include <QLocalSocket>
#include <QString>
#include <QTimer>

enum class SyncStrategy { Naive, Merkle };

struct FileClientConfig {
  QString rootDir;
  QString username;
  QString password;
  SyncStrategy syncStrategy;
  bool manualTick = false;
  unsigned int tickIntervalMs = 1000;
  QString serverName;
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
  TreeDiff *getNegotiationState();
  bool writeFile(const QString &path, const QByteArray &contents);

Q_SIGNALS:
  void syncCompleted();
  void negotiationCompleted();

private:
  void connectToServer();
  void sendNewFiles(const QList<QString> &files);
  void sendDeletedFiles(const QList<QString> &files);
  QLocalSocket *socket = nullptr;

  QTimer timer;
  unsigned int tickIntervalMs;
  bool shouldUseTimer = true;

  void handleAuthResponse(AuthResponseMessage *msg);
  void handleSyncResponse(SyncRequestMessage *msg);
  void handleMerkleSyncResponse(MerkleSyncMessage *msg);
  void handleWriteResponse(SyncRequestMessage *msg);

  void handleDeleteResponse(SyncRequestMessage *msg);
  void handleUnrecognized(Message *msg);

  QList<QString> discoverNewFiles();
  QList<QString> discoverDeletedFiles(const QSet<QString> &trackedFiles);
  void checkSyncCompletionAndUnlock();

  void merkleTick();
  void handleNegotiationCompleted();
  void naiveTick();
  bool currentlyDoingSyncOps = false;
  int pendingMessages = 0;
  QString username, password;
  QByteArray buffer;
  std::unique_ptr<LocalFileStorage> fileStorage;
  FileDb database;
  SyncStrategy syncStrategy;
  std::unique_ptr<MerkleTree> merkleTree;
  QString serverName;

  bool currentlyNegotiatingFileDiffs = false;
  QList<QString> toDescend;
  TreeDiff negotiationState;
  
};
