#pragma once

#include "FileTree.h"
#include <QByteArray>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <functional>

enum class TransportProtocol { LocalSocket, Tcp };

enum class MessageType {
  ClientAuth,
  ServerAuthResponse,
  SyncRequest,
  MerkleSync,
  ListRequest,
  ListResponse,
  ChunkTransfer,
  AckChunk,
  RequestChunkSizeUpload,
  RequestChunkSizeDownload,
  SpecifyChunkSizeUpload,
  SpecifyChunkSizeDownload,
  CancelTransfer,
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
  static void sendMessage(QIODevice *socket, std::shared_ptr<Message> msg);
  static void processBuffer(QIODevice *socket, QByteArray &buffer,
                            std::function<void(std::shared_ptr<Message>)> handler);
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
  // 0: sending root hash to check, 1: sending child hashes that differ, 2: stop
  qint8 phase;
  QByteArray rootHash;
  QList<QPair<QString, QList<MerkleEntry>>> fileEntriesPerChild;
    MerkleSyncMessage(int depth, qint8 phase, QByteArray rootHash,
                    QList<QPair<QString, QList<MerkleEntry>>> fileEntriesPerChild)
      : depth(depth), phase(phase), rootHash(std::move(rootHash)),
        fileEntriesPerChild(std::move(fileEntriesPerChild)) {}
  MerkleSyncMessage() = default;
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
enum class FileOperationStatus { DoIt, Error, ServerHasNewer, Done, Pending };

class SyncRequestMessage : public Message {
public:
  QString path;
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
  QByteArray hash;
};



QDebug operator<<(QDebug dbg,const FileEntry& entry);

class ListRequestMessage : public Message {
public:
  QString directory; // if empty we list all of the user files
  bool useMerkle = true;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<ListRequestMessage>
  deserialize(const QJsonObject &obj);
};

class ListResponseMessage : public Message {
public:
  QList<FileEntry> entries;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<ListResponseMessage>
  deserialize(const QJsonObject &obj);
};

class ChunkTransferMessage : public Message {
public:
  QString path;
  qint32 partNumber;
  qint64 chunkSize;
  QByteArray contents;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<ChunkTransferMessage>
  deserialize(const QJsonObject &obj);
};

enum class ChunkTransferError { FileNotFound };

class AckChunkMessage : public Message {
public:
  QString path;
  qint32 partNumber;
  QString failureMsg;
  ChunkTransferError failureType;
  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<AckChunkMessage> deserialize(const QJsonObject &obj);
};

class ChunkTransfer : public Message {
public:
  QString filepath;
  quint32 partNumber = 0;
  quint32 chunkSize = 0;
  QByteArray bytes;
  QByteArray hash;

  ChunkTransfer() = default;
  ChunkTransfer(QString filepath, quint32 partNumber, quint32 chunkSize,
                QByteArray bytes, QByteArray hash)
      : filepath(std::move(filepath)), partNumber(partNumber),
        chunkSize(chunkSize), bytes(std::move(bytes)), hash(std::move(hash)) {}

  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<ChunkTransfer> deserialize(const QJsonObject &obj);
};

enum class TransferFailure { BYTES_CORRUPTED, NONE };
QString toString(TransferFailure tf);
TransferFailure fromString(TransferFailure tf);

class ACKChunkReceived : public Message {
public:
  QString path;
  quint32 partNumber = 0;
  QString failureMsg;
  TransferFailure failureType;

  ACKChunkReceived() = default;
  ACKChunkReceived(QString path, quint32 partNumber, QString failureMsg,
                   TransferFailure failureType)
      : path(std::move(path)), partNumber(partNumber),
        failureMsg(std::move(failureMsg)), failureType(std::move(failureType)) {
  }

  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<ACKChunkReceived> deserialize(const QJsonObject &obj);
};

class RequestChunkSizeForUpload : public Message {
public:
  QString path;
  quint64 fileSize = 0;
  QByteArray hash;
  QDateTime mtime;

  RequestChunkSizeForUpload() = default;
  RequestChunkSizeForUpload(QString path, quint64 fileSize)
      : path(std::move(path)), fileSize(fileSize) {}

  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<RequestChunkSizeForUpload>
  deserialize(const QJsonObject &obj);
};

class RequestChunkSizeForDownload : public Message {
public:
  QString path;
  quint64 chunkSizeDesired = 0;

  RequestChunkSizeForDownload() = default;
  RequestChunkSizeForDownload(QString path, quint64 chunkSizeDesired)
      : path(std::move(path)), chunkSizeDesired(chunkSizeDesired) {}

  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<RequestChunkSizeForDownload>
  deserialize(const QJsonObject &obj);
};

class SpecifyChunkSizeUpload : public Message {
public:
  QString path;
  quint64 chunkSize = 0;
  quint32 totalChunks = 0;


  SpecifyChunkSizeUpload() = default;
  SpecifyChunkSizeUpload(QString path, quint64 chunkSize, quint32 totalChunks)
      : path(std::move(path)), chunkSize(chunkSize), totalChunks(totalChunks) {}

  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<SpecifyChunkSizeUpload>
  deserialize(const QJsonObject &obj);
};

class SpecifyChunkSizeDownload : public Message {
public:
  QString path;
  quint64 chunkSize = 0;
  quint32 totalChunks = 0;

  SpecifyChunkSizeDownload() = default;
  SpecifyChunkSizeDownload(QString path, quint64 chunkSize, quint32 totalChunks)
      : path(std::move(path)), chunkSize(chunkSize), totalChunks(totalChunks) {}

  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<SpecifyChunkSizeDownload>
  deserialize(const QJsonObject &obj);
};

class CancelTransfer : public Message {
public:
  QString path;

  CancelTransfer() = default;
  explicit CancelTransfer(QString path) : path(std::move(path)) {}

  MessageType type() const override;
  QByteArray serialize() const override;
  static std::unique_ptr<CancelTransfer> deserialize(const QJsonObject &obj);
};

QDebug operator<<(QDebug dbg, const SyncRequestMessage &msg);
QDebug operator<<(QDebug dbg, const ACKChunkReceived &ack);


//TODO: should later be in a types module or sth
struct DeletionEntry {
  QString path;
  QDateTime deletedAt;
};

struct NodesDiff {
  QList<QPair<bool, QString>> onlyInLeft;
  QList<QPair<bool, QString>> onlyInRight;
  // QList<QString> modified;
  QList<QString> modifiedWinsLeft;
  QList<QString> modifiedWinsRight;
  QList<DeletionEntry> deletionWinsLeft;
  QList<DeletionEntry> deletionWinsRight;
};

struct NegotiationState {
  NodesDiff diffEntries;
  QList<QString> directoriesToCheckWithServer;
};



