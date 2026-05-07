#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

enum class MessageType {
    ClientAuth,
    ServerAuthResponse,
    ClientSyncRequest,
};

class Message {
public:
    virtual MessageType type() const = 0;
    virtual QByteArray serialize() const = 0;
    virtual ~Message() = default;

    static Message* deserialize(const QByteArray &data);
};

class AuthMessage : public Message {
public:
    QString username;
    QString password;

    MessageType type() const override;
    QByteArray serialize() const override;
};

class AuthResponseMessage : public Message {
public:
    bool success;

    MessageType type() const override;
    QByteArray serialize() const override;
};

class ClientSyncRequestMessage : public Message {
public: 
    std::string path;
    QByteArray contents;
    std::string mtime;
    MessageType type() const override;
    QByteArray serialize() const override;
};
