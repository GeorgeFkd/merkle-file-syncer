#include "Messages.h"
#include <qnamespace.h>

// --- serde helpers ---
static QJsonValue dateTimeToJson(const QDateTime &dt) {
  return dt.toString(Qt::ISODateWithMs);
}
static QDateTime dateTimeFromJson(const QJsonValue &v) {
  return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}
static QJsonValue hexToJson(const QByteArray &b) {
  return QString::fromLatin1(b.toHex());
}
static QByteArray hexFromJson(const QJsonValue &v) {
  return QByteArray::fromHex(v.toString().toLatin1());
}
static QJsonValue base64ToJson(const QByteArray &b) {
  return QString::fromLatin1(b.toBase64());
}
static QByteArray base64FromJson(const QJsonValue &v) {
  return QByteArray::fromBase64(v.toString().toLatin1());
}

void MessageProtocol::sendMessage(QIODevice *socket,
                                  std::shared_ptr<Message> msg) {
  QByteArray payload = msg->serialize();
  QByteArray frame;
  QDataStream stream(&frame, QIODevice::WriteOnly);
  stream << quint32(payload.size());
  frame.append(payload);
  socket->write(frame);
}

void MessageProtocol::processBuffer(
    QIODevice *socket, QByteArray &buffer,
    std::function<void(std::shared_ptr<Message>)> handler) {
  buffer.append(socket->readAll());
  while (buffer.size() >= 4) {
    QDataStream stream(buffer);
    quint32 length;
    stream >> length;
    if (buffer.size() < 4 + (int)length)
      break;
    QByteArray payload = buffer.mid(4, length);
    buffer.remove(0, 4 + length);
    std::shared_ptr<Message> msg = Message::deserialize(payload);
    if (!msg) {
      qDebug() << "Failed to deserialize message";
      continue;
    }
    handler(msg);
  }
}

MessageType MerkleSyncMessage::type() const { return MessageType::MerkleSync; }

QByteArray MerkleSyncMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "merkle_sync";
  obj["token"] = token;
  obj["depth"] = depth;
  obj["phase"] = phase;
  obj["rootHash"] = hexToJson(rootHash);
  QJsonArray childrenArray;
  for (const auto &[childPath, entries] : fileEntriesPerChild) {
    QJsonObject childObj;
    childObj["path"] = childPath;
    QJsonArray entriesArray;
    for (const auto &entry : entries) {
      QJsonObject entryObj;
      entryObj["path"] = entry.path;
      entryObj["hash"] = hexToJson(entry.hash);
      entryObj["mtime"] = dateTimeToJson(entry.mtime);
      entryObj["type"] =
          entry.filetype == FileType::Directory ? "directory" : "file";
      entryObj["isTombstone"] = entry.isTombstone;
      entryObj["deletedAt"] = dateTimeToJson(entry.deletedAt);
      entriesArray.append(entryObj);
    }
    childObj["entries"] = entriesArray;
    childrenArray.append(childObj);
  }
  obj["fileEntriesPerChild"] = childrenArray;
  return QJsonDocument(obj).toJson();
}

std::unique_ptr<MerkleSyncMessage>
MerkleSyncMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<MerkleSyncMessage>();
  msg->token = obj["token"].toString();
  msg->depth = obj["depth"].toInt();
  msg->phase = static_cast<qint8>(obj["phase"].toInt());
  msg->rootHash = hexFromJson(obj["rootHash"]);
  for (const auto &childVal : obj["fileEntriesPerChild"].toArray()) {
    QJsonObject childObj = childVal.toObject();
    QString childPath = childObj["path"].toString();
    QList<MerkleEntry> entries;
    for (const auto &entryVal : childObj["entries"].toArray()) {
      QJsonObject entryObj = entryVal.toObject();
      MerkleEntry entry;
      entry.path = entryObj["path"].toString();
      entry.hash = hexFromJson(entryObj["hash"]);
      entry.mtime = dateTimeFromJson(entryObj["mtime"]);
      entry.filetype = entryObj["type"].toString() == "directory"
                           ? FileType::Directory
                           : FileType::File;
      entry.isTombstone = entryObj["isTombstone"].toBool(false);
      entry.deletedAt = dateTimeFromJson(entryObj["deletedAt"]);
      entries.append(entry);
    }
    msg->fileEntriesPerChild.append({childPath, entries});
  }
  return msg;
}

MessageType AuthMessage::type() const { return MessageType::ClientAuth; }

QByteArray AuthMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "auth";
  obj["username"] = username;
  obj["password"] = password;
  obj["devicename"] = deviceName;
  return QJsonDocument(obj).toJson();
}

MessageType AuthResponseMessage::type() const {
  return MessageType::ServerAuthResponse;
}

QByteArray AuthResponseMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "auth_response";
  obj["success"] = success;
  obj["token"] = token;
  obj["error"] = error;
  return QJsonDocument(obj).toJson();
}

std::unique_ptr<Message> Message::deserialize(const QByteArray &data) {
  QJsonObject obj = QJsonDocument::fromJson(data).object();
  QString type = obj["type"].toString();

  if (type == "auth")
    return AuthMessage::deserialize(obj);
  if (type == "auth_response")
    return AuthResponseMessage::deserialize(obj);
  if (type == "delete_request")
    return DeleteRequestMessage::deserialize(obj);
  if (type == "merkle_sync")
    return MerkleSyncMessage::deserialize(obj);
  if (type == "list_request")
    return ListRequestMessage::deserialize(obj);
  if (type == "list_response")
    return ListResponseMessage::deserialize(obj);
  if (type == "chunk_transfer")
    return ChunkTransfer::deserialize(obj);
  if (type == "ack_chunk")
    return ACKChunkReceived::deserialize(obj);
  if (type == "request_chunk_size_upload")
    return RequestChunkSizeForUpload::deserialize(obj);
  if (type == "request_chunk_size_download")
    return RequestChunkSizeForDownload::deserialize(obj);
  if (type == "specify_chunk_size_upload")
    return SpecifyChunkSizeUpload::deserialize(obj);
  if (type == "specify_chunk_size_download")
    return SpecifyChunkSizeDownload::deserialize(obj);
  if (type == "cancel_transfer")
    return CancelTransfer::deserialize(obj);
  qDebug() << "Type of message actually is: " << type;
  return nullptr;
}

MessageType DeleteRequestMessage::type() const {
  return MessageType::DeleteRequest;
}

QByteArray DeleteRequestMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "delete_request";
  obj["token"] = token;
  obj["path"] = path;
  obj["contents"] = base64ToJson(contents);
  obj["mtime"] = dateTimeToJson(operationTime);
  switch (operationStatus) {
  case FileOperationStatus::DoIt:
    obj["opstatus"] = "doit";
    break;
  case FileOperationStatus::Done:
    obj["opstatus"] = "done";
    break;
  case FileOperationStatus::Error:
    obj["opstatus"] = "error";
    break;
  case FileOperationStatus::Pending:
    obj["opstatus"] = "pending";
    break;
  case FileOperationStatus::ServerHasNewer:
    obj["opstatus"] = "serverhasnewer";
    break;
  }
  return QJsonDocument(obj).toJson();
}

std::unique_ptr<AuthMessage> AuthMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<AuthMessage>();
  msg->username = obj["username"].toString();
  msg->password = obj["password"].toString();
  msg->deviceName = obj["devicename"].toString();
  return msg;
}

std::unique_ptr<AuthResponseMessage>
AuthResponseMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<AuthResponseMessage>();
  msg->success = obj["success"].toBool();
  msg->token = obj["token"].toString();
  msg->error = obj["error"].toString();
  return msg;
}

std::unique_ptr<DeleteRequestMessage>
DeleteRequestMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<DeleteRequestMessage>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString();
  msg->contents = base64FromJson(obj["contents"]);
  msg->operationTime = dateTimeFromJson(obj["mtime"]);
  if (obj["opstatus"].toString() == "doit")
    msg->operationStatus = FileOperationStatus::DoIt;
  else if (obj["opstatus"].toString() == "done")
    msg->operationStatus = FileOperationStatus::Done;
  else if (obj["opstatus"].toString() == "error")
    msg->operationStatus = FileOperationStatus::Error;
  else if (obj["opstatus"].toString() == "pending")
    msg->operationStatus = FileOperationStatus::Pending;
  else if (obj["opstatus"].toString() == "serverhasnewer")
    msg->operationStatus = FileOperationStatus::ServerHasNewer;
  return msg;
}

QByteArray ListRequestMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "list_request";
  obj["token"] = token;
  obj["directory"] = directory;
  obj["use_merkle"] = useMerkle;
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

std::unique_ptr<ListRequestMessage>
ListRequestMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<ListRequestMessage>();
  msg->token = obj["token"].toString();
  msg->directory = obj["directory"].toString();
  msg->useMerkle = obj["use_merkle"].toBool();
  return msg;
}

MessageType ListRequestMessage::type() const {
  return MessageType::ListRequest;
}

MessageType ListResponseMessage::type() const {
  return MessageType::ListResponse;
}

QByteArray ListResponseMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "list_response";
  QJsonArray entriesArray;
  for (const auto &entry : entries) {
    QJsonObject entryObj;
    entryObj["path"] = entry.path;
    entryObj["mtime"] = dateTimeToJson(entry.mtime);
    entryObj["deleted"] = entry.deleted;
    entryObj["hash"] = base64ToJson(entry.hash);
    entriesArray.append(entryObj);
  }
  obj["entries"] = entriesArray;
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

std::unique_ptr<ListResponseMessage>
ListResponseMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<ListResponseMessage>();
  for (const auto &val : obj["entries"].toArray()) {
    QJsonObject entryObj = val.toObject();
    FileEntry entry;
    entry.path = entryObj["path"].toString();
    entry.mtime = dateTimeFromJson(entryObj["mtime"]);
    entry.deleted = entryObj["deleted"].toBool();
    entry.hash = base64FromJson(entryObj["hash"]);
    msg->entries.append(entry);
  }
  return msg;
}

MessageType ChunkTransferMessage::type() const {
  return MessageType::ChunkTransfer;
}

std::unique_ptr<ChunkTransferMessage>
ChunkTransferMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<ChunkTransferMessage>();
  msg->partNumber = obj["partNumber"].toInt();
  msg->path = obj["path"].toString();
  msg->contents = base64FromJson(obj["contents"]);
  msg->token = obj["token"].toString();
  return msg;
}

QByteArray ChunkTransferMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "chunk_transfer";
  obj["contents"] = base64ToJson(this->contents);
  obj["token"] = this->token;
  obj["path"] = this->path;
  obj["partNumber"] = this->partNumber;
  obj["chunkSize"] = this->chunkSize;
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

MessageType AckChunkMessage::type() const { return MessageType::AckChunk; }
std::unique_ptr<AckChunkMessage>
AckChunkMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<AckChunkMessage>();
  msg->path = obj["path"].toString();
  msg->partNumber = obj["partNumber"].toInt();
  msg->failureMsg = obj["failureMsg"].toString();
  msg->token = obj["token"].toString();
  auto failureType = obj["failureType"].toString();
  if (failureType == "file_not_found")
    msg->failureType = ChunkTransferError::FileNotFound;
  else
    Q_ASSERT_X(false, "AckChunkMessage::deserialize",
               "failureType was not properly set");
  return msg;
}

QByteArray AckChunkMessage::serialize() const {
  QJsonObject obj;
  switch (this->failureType) {
  case ChunkTransferError::FileNotFound: {
    obj["failureType"] = "file_not_found";
    break;
  }
  }
  obj["partNumber"] = this->partNumber;
  obj["failureMsg"] = this->failureMsg;
  obj["token"] = this->token;
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QDebug operator<<(QDebug debug, const MerkleSyncMessage &msg) {
  debug << "MerkleSyncMessage {";
  debug << "token: " << msg.token;
  debug << "phase:" << msg.phase;
  debug << "depth:" << msg.depth;
  debug << "token:" << msg.token;
  debug << "fileEntriesPerChild: [";
  for (const auto &[parentPath, entries] : msg.fileEntriesPerChild) {
    debug << "  parent:" << parentPath << "entries: [";
    for (const auto &entry : entries) {
      debug << "    {path:" << entry.path
            << "hash:" << entry.hash.toHex().left(8) << "type:"
            << (entry.filetype == FileType::Directory ? "dir" : "file") << "}";
    }
    debug << "  ]";
  }
  debug << "]}";
  return debug;
}

MessageType ChunkTransfer::type() const { return MessageType::ChunkTransfer; }
QByteArray ChunkTransfer::serialize() const {
  QJsonObject obj;
  obj["type"] = "chunk_transfer";
  obj["token"] = token;
  obj["filepath"] = filepath;
  obj["partNumber"] = static_cast<qint64>(partNumber);
  obj["chunkSize"] = static_cast<qint64>(chunkSize);
  obj["bytes"] = base64ToJson(bytes);
  obj["hash"] = base64ToJson(hash);
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
std::unique_ptr<ChunkTransfer>
ChunkTransfer::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<ChunkTransfer>();
  msg->token = obj["token"].toString();
  msg->filepath = obj["filepath"].toString();
  msg->partNumber = static_cast<quint32>(obj["partNumber"].toInteger());
  msg->chunkSize = static_cast<quint32>(obj["chunkSize"].toInteger());
  msg->bytes = base64FromJson(obj["bytes"]);
  msg->hash = base64FromJson(obj["hash"]);
  return msg;
}

QString toString(TransferFailure tf) {
  if (tf == TransferFailure::BYTES_CORRUPTED)
    return "bytes_corrupted";
  if (tf == TransferFailure::NONE)
    return "none";
  assert(false);
}

TransferFailure fromString(const QString &tf) {
  if (tf == "bytes_corrupted")
    return TransferFailure::BYTES_CORRUPTED;
  if (tf == "none")
    return TransferFailure::NONE;
  assert(false);
}

// --- ACKChunkReceived ---
MessageType ACKChunkReceived::type() const { return MessageType::AckChunk; }
QByteArray ACKChunkReceived::serialize() const {
  QJsonObject obj;
  obj["type"] = "ack_chunk";
  obj["token"] = token;
  obj["path"] = path;
  obj["partNumber"] = static_cast<qint64>(partNumber);
  obj["failureMsg"] = failureMsg;
  obj["failureType"] = toString(failureType);
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
std::unique_ptr<ACKChunkReceived>
ACKChunkReceived::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<ACKChunkReceived>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString();
  msg->partNumber = static_cast<quint32>(obj["partNumber"].toInteger());
  msg->failureMsg = obj["failureMsg"].toString();
  msg->failureType = fromString(obj["failureType"].toString());
  return msg;
}

// --- RequestChunkSizeForUpload ---
MessageType RequestChunkSizeForUpload::type() const {
  return MessageType::RequestChunkSizeUpload;
}
QByteArray RequestChunkSizeForUpload::serialize() const {
  QJsonObject obj;
  obj["type"] = "request_chunk_size_upload";
  obj["token"] = token;
  obj["path"] = path;
  obj["fileSize"] = static_cast<qint64>(fileSize);
  obj["mtime"] = dateTimeToJson(mtime);
  obj["hash"] = base64ToJson(hash);
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
std::unique_ptr<RequestChunkSizeForUpload>
RequestChunkSizeForUpload::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<RequestChunkSizeForUpload>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString();
  msg->fileSize = static_cast<quint64>(obj["fileSize"].toInteger());
  msg->hash = base64FromJson(obj["hash"]);
  msg->mtime = dateTimeFromJson(obj["mtime"]);
  return msg;
}

// --- RequestChunkSizeForDownload ---
MessageType RequestChunkSizeForDownload::type() const {
  return MessageType::RequestChunkSizeDownload;
}
QByteArray RequestChunkSizeForDownload::serialize() const {
  QJsonObject obj;
  obj["type"] = "request_chunk_size_download";
  obj["token"] = token;
  obj["path"] = path;
  obj["chunkSizeDesired"] = static_cast<qint64>(chunkSizeDesired);
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
std::unique_ptr<RequestChunkSizeForDownload>
RequestChunkSizeForDownload::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<RequestChunkSizeForDownload>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString();
  msg->chunkSizeDesired =
      static_cast<quint64>(obj["chunkSizeDesired"].toInteger());
  return msg;
}

// --- SpecifyChunkSizeUpload ---
MessageType SpecifyChunkSizeUpload::type() const {
  return MessageType::SpecifyChunkSizeUpload;
}
QByteArray SpecifyChunkSizeUpload::serialize() const {
  QJsonObject obj;
  obj["type"] = "specify_chunk_size_upload";
  obj["token"] = token;
  obj["path"] = path;
  obj["chunkSize"] = static_cast<qint64>(chunkSize);
  obj["totalChunks"] = static_cast<qint64>(totalChunks);
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
std::unique_ptr<SpecifyChunkSizeUpload>
SpecifyChunkSizeUpload::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<SpecifyChunkSizeUpload>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString();
  msg->chunkSize = static_cast<quint64>(obj["chunkSize"].toInteger());
  msg->totalChunks = static_cast<quint32>(obj["totalChunks"].toInteger());
  return msg;
}

// --- SpecifyChunkSizeDownload ---
MessageType SpecifyChunkSizeDownload::type() const {
  return MessageType::SpecifyChunkSizeDownload;
}
QByteArray SpecifyChunkSizeDownload::serialize() const {
  QJsonObject obj;
  obj["type"] = "specify_chunk_size_download";
  obj["token"] = token;
  obj["path"] = path;
  obj["chunkSize"] = static_cast<qint64>(chunkSize);
  obj["totalChunks"] = static_cast<qint64>(totalChunks);
  obj["mtime"] = dateTimeToJson(mtime);
  obj["hash"] = base64ToJson(hash);
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
std::unique_ptr<SpecifyChunkSizeDownload>
SpecifyChunkSizeDownload::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<SpecifyChunkSizeDownload>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString();
  msg->chunkSize = static_cast<quint64>(obj["chunkSize"].toInteger());
  msg->totalChunks = static_cast<quint32>(obj["totalChunks"].toInteger());
  msg->mtime = dateTimeFromJson(obj["mtime"]);
  msg->hash = base64FromJson(obj["hash"]);
  return msg;
}

// --- CancelTransfer ---
MessageType CancelTransfer::type() const { return MessageType::CancelTransfer; }
QByteArray CancelTransfer::serialize() const {
  QJsonObject obj;
  obj["type"] = "cancel_transfer";
  obj["token"] = token;
  obj["path"] = path;
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
std::unique_ptr<CancelTransfer>
CancelTransfer::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<CancelTransfer>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString();
  return msg;
}

QDebug operator<<(QDebug dbg, const ACKChunkReceived &ack) {
  QDebugStateSaver saver(dbg);
  dbg.nospace() << "ACKChunkReceived(path=" << ack.path
                << ", partNumber=" << ack.partNumber
                << ", failureType=" << toString(ack.failureType)
                << ", failureMsg=" << ack.failureMsg << ")";
  return dbg;
}

QDebug operator<<(QDebug dbg, const DeleteRequestMessage &msg) {
  QDebugStateSaver saver(dbg);
  dbg.nospace() << "SyncRequestMessage(path=" << msg.path
                << ", contentsSize=" << msg.contents.size()
                << ", operationTime=" << msg.operationTime
                << ", operationStatus=" << static_cast<int>(msg.operationStatus)
                << ")";
  return dbg;
}

QDebug operator<<(QDebug dbg, const FileEntry &entry) {
  QDebugStateSaver saver(dbg);
  dbg.nospace() << "FileEntry: mtime: " << entry.mtime << "path: " << entry.path
                << "deleted: " << entry.deleted;
  return dbg;
}
