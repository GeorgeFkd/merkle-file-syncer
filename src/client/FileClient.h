#pragma once

#include "FileDb.h"
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
  int tickIntervalMs = 1000;
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

Q_SIGNALS:
  void syncCompleted();

private:
  void connectToServer();
  QLocalSocket *socket = nullptr;
  QTimer timer;
  void handleAuthResponse(AuthResponseMessage *msg);
  void handleSyncResponse(SyncRequestMessage *msg);
  void handleUnrecognized(Message *msg);
  FileDb database;
  QList<QString> discoverNewFiles();
  QList<QString> discoverDeletedFiles(const QSet<QString> &trackedFiles);
  bool currentlyDoingSyncOps = false;
  int pendingMessages = 0;
  // QString clientRootDir;
  bool shouldUseTimer = true;
  QString username, password;
  QByteArray buffer;
  std::unique_ptr<LocalFileStorage> fileStorage;
  SyncStrategy syncStrategy;
  std::unique_ptr<MerkleTree> merkleTree;
  QString serverName;
};
