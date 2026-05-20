#pragma once

#include "FileDb.h"
#include "FileHasher.h"
#include "LocalFileStorage.h"
#include "MerkleTree.h"
#include "Messages.h"
#include <QLocalSocket>
#include <QString>
#include <QTimer>


struct NodesDiff{
  //the bool is to signal whether it is a file or not(true -> file, false ->directory)
  //so we can later ask the server for whole directories
  QList<QPair<bool,QString>> onlyInLeft;
  QList<QPair<bool,QString>> onlyInRight;
  QList<QString> modified;
};

struct NegotiationState {
  NodesDiff diffEntries;
  QList<QString> directoriesToCheckWithServer;
};

enum class SyncStrategy { Naive, Merkle };
enum class ClientState {Disconnected,Connected,Authenticated,Authenticating};


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
  NegotiationState *getNegotiationState();
  bool writeFile(const QString& user,const QString &path, const QByteArray &contents);
  bool deleteFile(const QString &user, const QString &path);

Q_SIGNALS:
  void syncCompleted();
  void negotiationCompleted();
  void authenticated();

private:
  void connectToServer();
  void sendNewFiles(const QList<QString> &files);
  void sendDeletedFiles(const QList<QString> &files);
  void sendAuthRequest();
  QString getDeviceName();
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
  QList<QString> discoverDeletedFiles();
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
  NegotiationState negotiationState;

  bool pendingTick = false;
  QString token;
  ClientState state = ClientState::Disconnected;
  
};
