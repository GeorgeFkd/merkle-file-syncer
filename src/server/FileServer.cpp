#include "FileServer.h"

void FileServer::handleConnection(QLocalSocket *socket) {
    QObject::connect(socket, &QLocalSocket::readyRead, [this, socket]() {
        auto bytes = socket->readAll();
        auto *msg = Message::deserialize(bytes);

        switch (msg->type()) {
        case MessageType::ClientAuth:
            handleAuth(socket, static_cast<AuthMessage *>(msg));
            break;
        default:{
            handleUnrecognized(socket, msg);
            break;
      }
        }

        delete msg;
    });
}

void FileServer::handleAuth(QLocalSocket *socket, AuthMessage *msg) {
    qDebug() << "User: " << msg->username << "Password: " << msg->password;

    AuthResponseMessage response;
    response.success = true;
    socket->write(response.serialize());
}

void FileServer::handleUnrecognized(QLocalSocket *socket, Message *msg) {
    qDebug() << "Unrecognized message type received";
}
