#pragma once

#include "FileDb.h"
#include "Messages.h"
#include <QLocalSocket>
#include <QString>
#include <QTimer>
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

Q_SIGNALS:
  void syncCompleted();

private:
  QLocalSocket *socket = nullptr;
  QTimer timer;
  void handleAuthResponse(AuthResponseMessage *msg);
  void handleSyncResponse(SyncRequestMessage *msg);
  void handleUnrecognized(Message *msg);
  FileDb database;
  QList<QString> discoverNewFiles(const QString &rootDir);
  QList<QString> discoverDeletedFiles(const QString &rootDir,
                                      const QSet<QString> &trackedFiles);
  bool currentlyDoingSyncOps = false;
  int pendingMessages = 0;
  QString clientRootDir;
  bool shouldUseTimer = true;
  QString username, password;
  QByteArray buffer;
};
