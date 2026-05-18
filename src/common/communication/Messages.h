#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <QJsonArray>

enum class MessageType {
  ClientAuth,
  ServerAuthResponse,
  SyncRequest,
  MerkleSync
};

class Message {
public:
  virtual MessageType type() const = 0;
  virtual QByteArray serialize() const = 0;
  virtual ~Message() = default;

  static std::unique_ptr<Message> deserialize(const QByteArray &data);
};

class MessageProtocol {
public:
  static void sendMessage(QIODevice *socket, const Message &msg);
  static void processBuffer(QIODevice *socket, QByteArray &buffer,
                            std::function<void(Message *)> handler);
};

class MerkleSyncMessage : public Message {
public:
  int depth;
  QList<QPair<QString, QByteArray>> pathsAndHashes;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<MerkleSyncMessage> deserialize(const QJsonObject &obj);
};

class AuthMessage : public Message {
public:
  QString username;
  QString password;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<AuthMessage> deserialize(const QJsonObject &obj);
};

class AuthResponseMessage : public Message {
public:
  bool success;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<AuthResponseMessage>
  deserialize(const QJsonObject &obj);
};

enum class FileOperationType { Write, Delete };
enum class FileOperationStatus { DoIt, Error,ServerHasNewer, Done, Pending };

class SyncRequestMessage : public Message {
public:
  std::string path;
  QByteArray contents;
  std::string mtime;
  FileOperationType operationType;
  FileOperationStatus operationStatus;
  QString username;
  QString password;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<SyncRequestMessage>
  deserialize(const QJsonObject &obj);
};
