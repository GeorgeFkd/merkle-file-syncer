#pragma once

#include "Messages.h"
#include <QLocalSocket>
#include <QTimer>
#include "FileDb.h"
#include <QString>
class FileClient : public QObject {
    Q_OBJECT
public:
    FileClient();
    void init();
    void connectToServer(const QString& serverName);
Q_SIGNALS:
    void syncCompleted();
private:
    QLocalSocket socket;
    QTimer timer;
    void handleAuthResponse(AuthResponseMessage *msg);
    void handleSyncResponse(SyncRequestMessage *msg);
    void handleUnrecognized(Message *msg);
    void clientTick();
    FileDb database;
    QString getUserRootDirectory(const QString& username);
    QList<QString> discoverNewFiles(const QString& rootDir);
    QList<QString> discoverDeletedFiles(const QString& rootDir,const QSet<QString>& trackedFiles);
    bool currentlyDoingSyncOps = false;
    int pendingMessages = 0;
    void sendMessage(const Message& msg);
};
