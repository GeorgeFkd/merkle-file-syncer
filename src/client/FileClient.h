#pragma once

#include "Messages.h"
#include <QLocalSocket>
#include <QTimer>
#include "FileDb.h"
#include <QString>
class FileClient{
public:
    FileClient();
    void init();
    void connectToServer(const QString& serverName);
private:
    QLocalSocket socket;
    QTimer timer;
    void handleAuthResponse(AuthResponseMessage *msg);
    void handleUnrecognized(Message *msg);
    void clientTick();
    FileDb database;
    QString getUserRootDirectory(const QString& username);
    bool currentlyDoingSyncOps = false;
};
