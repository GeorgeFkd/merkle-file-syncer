#pragma once

#include "FileTree.h"
#include <QByteArray>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <QJsonArray>

enum class TransportProtocol {LocalSocket,Tcp};

enum class MessageType {
  ClientAuth,
  ServerAuthResponse,
  SyncRequest,
  MerkleSync,
  ListRequest,
  ListResponse
};

class Message {
public:
  virtual MessageType type() const = 0;
  virtual QByteArray serialize() const = 0;
  virtual ~Message() = default;
  
  QString token;
  static std::unique_ptr<Message> deserialize(const QByteArray &data);
};

class MessageProtocol {
public:
  static void sendMessage(QIODevice *socket, const Message &msg);
  static void processBuffer(QIODevice *socket, QByteArray &buffer,
                            std::function<void(Message *)> handler);
};

struct MerkleEntry {
  QString path;
  QByteArray hash;
  QDateTime mtime;
  FileType filetype;
  bool isTombstone = false;
  QDateTime deletedAt;
};



class MerkleSyncMessage : public Message {
public:
  int depth;
  //0: sending root hash to check, 1: sending child hashes that differ, 2: stop
  qint8 phase;
  QByteArray rootHash;
  QList<QPair<QString,QList<MerkleEntry>>> fileEntriesPerChild;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<MerkleSyncMessage> deserialize(const QJsonObject &obj);
};

QDebug operator<<(QDebug debug, const MerkleSyncMessage &msg);


class AuthMessage : public Message {
public:
  QString username;
  QString password;
  QString deviceName;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<AuthMessage> deserialize(const QJsonObject &obj);
};

class AuthResponseMessage : public Message {
public:
  bool success;
  QString error;
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
  QDateTime operationTime;
  FileOperationType operationType;
  FileOperationStatus operationStatus;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<SyncRequestMessage>
  deserialize(const QJsonObject &obj);
};

struct FileEntry {
  QString path;
  QDateTime mtime;
  bool deleted = false;
};

class ListRequestMessage : public Message {
public:
  QString directory; //if empty we list all of the user files
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<ListRequestMessage> deserialize(const QJsonObject &obj);
};

class ListResponseMessage : public Message {
public:
  QList<FileEntry> entries;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<ListResponseMessage> deserialize(const QJsonObject &obj);
};

