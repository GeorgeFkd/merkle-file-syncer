#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

enum class MessageType {
  ClientAuth,
  ServerAuthResponse,
  SyncRequest,
};

class Message {
public:
  virtual MessageType type() const = 0;
  virtual QByteArray serialize() const = 0;
  virtual ~Message() = default;

  static Message *deserialize(const QByteArray &data);
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

enum class FileOperationType { Write, Delete };
enum class FileOperationStatus { DoIt, Rejected, Done, Pending };
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
};
