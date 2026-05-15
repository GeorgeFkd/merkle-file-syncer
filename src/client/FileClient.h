#pragma once

#include "FileDb.h"
#include "Messages.h"
#include <QLocalSocket>
#include <QString>
#include <QTimer>
#include "LocalFileStorage.h"
class FileClient : public QObject {
  Q_OBJECT
public:
  FileClient();
  ~FileClient();
  void init();
  void connectToServer(const QString &serverName);
  void setRootDir(const QString &rootDir);
  void setManualTick();
  void clientTick();
  void setUsername(const QString &username);
  void setPassword(const QString &password);
  QString getUserRootDirectory(const QString &username);
  LocalFileStorage* getStorage();

Q_SIGNALS:
  void syncCompleted();

private:
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
};
