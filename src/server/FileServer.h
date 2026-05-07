#pragma once

#include "Messages.h"
#include <QLocalSocket>
#include "FileDb.h"
class FileServer {
public:
    void handleConnection(QLocalSocket *socket);

private:
    void handleAuth(QLocalSocket *socket, AuthMessage *msg);
    void handleUnrecognized(QLocalSocket *socket, Message *msg);
    void handleSyncRequest(QLocalSocket *socket,SyncRequestMessage *msg);
    QString computeUserDirectory(const QString &username);
    FileDb database;
};
