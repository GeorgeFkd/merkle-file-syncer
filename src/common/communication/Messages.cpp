#include "Messages.h"
#include <qnamespace.h>
void MessageProtocol::sendMessage(QIODevice *socket, const Message &msg) {
  QByteArray payload = msg.serialize();
  QByteArray frame;
  QDataStream stream(&frame, QIODevice::WriteOnly);
  stream << quint32(payload.size());
  frame.append(payload);
  socket->write(frame);
}

void MessageProtocol::processBuffer(QIODevice *socket, QByteArray &buffer,
                                    std::function<void(Message *)> handler) {
  buffer.append(socket->readAll());
  while (buffer.size() >= 4) {
    QDataStream stream(buffer);
    quint32 length;
    stream >> length;
    if (buffer.size() < 4 + (int)length)
      break;
    QByteArray payload = buffer.mid(4, length);
    buffer.remove(0, 4 + length);
    auto msg = Message::deserialize(payload);
    if (!msg) {
      qDebug() << "Failed to deserialize message";
      continue;
    }
    handler(msg.get());
  }
}

MessageType MerkleSyncMessage::type() const { return MessageType::MerkleSync; }

QByteArray MerkleSyncMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "merkle_sync";
  obj["token"] = token;
  obj["depth"] = depth;
  obj["phase"] = phase;
  obj["rootHash"] = QString::fromLatin1(rootHash.toHex());
  QJsonArray childrenArray;
  for (const auto &[childPath, entries] : fileEntriesPerChild) {
    QJsonObject childObj;
    childObj["path"] = childPath;
    QJsonArray entriesArray;
    for (const auto &entry : entries) {
      QJsonObject entryObj;
      entryObj["path"] = entry.path;
      entryObj["hash"] = QString::fromLatin1(entry.hash.toHex());
      entryObj["mtime"] = entry.mtime.toString(Qt::ISODateWithMs);
      entryObj["type"] =
          entry.filetype == FileType::Directory ? "directory" : "file";
      entryObj["isTombstone"] = entry.isTombstone;
      entryObj["deletedAt"] = entry.deletedAt.toString(Qt::ISODateWithMs);
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
  msg->rootHash = QByteArray::fromHex(obj["rootHash"].toString().toLatin1());
  for (const auto &childVal : obj["fileEntriesPerChild"].toArray()) {
    QJsonObject childObj = childVal.toObject();
    QString childPath = childObj["path"].toString();
    QList<MerkleEntry> entries;
    for (const auto &entryVal : childObj["entries"].toArray()) {
      QJsonObject entryObj = entryVal.toObject();
      MerkleEntry entry;
      entry.path = entryObj["path"].toString();
      entry.hash = QByteArray::fromHex(entryObj["hash"].toString().toLatin1());
      entry.mtime =
          QDateTime::fromString(entryObj["mtime"].toString(), Qt::ISODateWithMs);
      entry.filetype = entryObj["type"].toString() == "directory"
                           ? FileType::Directory
                           : FileType::File;
      entry.isTombstone = entryObj["isTombstone"].toBool(false);
      entry.deletedAt = QDateTime::fromString(
          entryObj["deletedAt"].toString(), Qt::ISODateWithMs);
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
  if (type == "sync_request")
    return SyncRequestMessage::deserialize(obj);
  if (type == "merkle_sync")
    return MerkleSyncMessage::deserialize(obj);
  if (type == "list_request")
    return ListRequestMessage::deserialize(obj);
  if (type == "list_response")
    return ListResponseMessage::deserialize(obj);

  return nullptr;
}

MessageType SyncRequestMessage::type() const {
  return MessageType::SyncRequest;
}

QByteArray SyncRequestMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "sync_request";
  obj["token"] = token;
  obj["path"] = QString::fromStdString(path);
  obj["contents"] = QString::fromUtf8(contents.toBase64());
  obj["mtime"] = QString::fromStdString(mtime);
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

  switch (operationType) {
  case FileOperationType::Write:
    obj["optype"] = "write";
    break;
  case FileOperationType::Delete:
    obj["optype"] = "delete";
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

std::unique_ptr<SyncRequestMessage>
SyncRequestMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<SyncRequestMessage>();
  msg->token = obj["token"].toString();
  msg->path = obj["path"].toString().toStdString();
  msg->contents = QByteArray::fromBase64(obj["contents"].toString().toUtf8());
  msg->mtime = obj["mtime"].toString().toStdString();

  if (obj["optype"].toString() == "write")
    msg->operationType = FileOperationType::Write;
  else if (obj["optype"].toString() == "delete")
    msg->operationType = FileOperationType::Delete;

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
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

std::unique_ptr<ListRequestMessage>
ListRequestMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<ListRequestMessage>();
  msg->token = obj["token"].toString();
  msg->directory = obj["directory"].toString();
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
    entryObj["mtime"] = entry.mtime.toString(Qt::ISODateWithMs);
    entryObj["deleted"] = entry.deleted;
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
    entry.mtime =
        QDateTime::fromString(entryObj["mtime"].toString(), Qt::ISODateWithMs);
    entry.deleted = entryObj["deleted"].toBool();
    msg->entries.append(entry);
  }
  return msg;
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
