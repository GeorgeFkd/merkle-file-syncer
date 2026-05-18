#include "Messages.h"
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
    obj["depth"] = depth;
    QJsonArray files;
    for (const auto &merkleEntry: fileEntries) {
        QJsonObject entry;
        entry["path"] = merkleEntry.path;
        entry["hash"] = QString::fromUtf8(merkleEntry.hash.toBase64());
        entry["mtime"] = merkleEntry.mtime.toString(Qt::ISODate);
        files.append(entry);
    }
    obj["merkleEntries"] = files;
    obj["username"] = username;
    return QJsonDocument(obj).toJson();
}

std::unique_ptr<MerkleSyncMessage> MerkleSyncMessage::deserialize(const QJsonObject &obj) {
    auto msg = std::make_unique<MerkleSyncMessage>();
    msg->depth = obj["depth"].toInt();
    auto pairs = obj["merkleEntries"].toArray();
    for (const auto &entry : pairs) {
        auto e = entry.toObject();
        QString path = e["path"].toString();
        QByteArray hash = QByteArray::fromBase64(e["hash"].toString().toUtf8());
        auto mtime = QDateTime::fromString(e["mtime"].toString(),Qt::ISODate);
        msg->fileEntries.append({path, hash,mtime});
    }
    msg->username = obj["username"].toString();
    return msg;
}


MessageType AuthMessage::type() const { return MessageType::ClientAuth; }

QByteArray AuthMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "auth";
  obj["username"] = username;
  obj["password"] = password;
  return QJsonDocument(obj).toJson();
}

MessageType AuthResponseMessage::type() const {
  return MessageType::ServerAuthResponse;
}

QByteArray AuthResponseMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "auth_response";
  obj["success"] = success;
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

  return nullptr;
}

MessageType SyncRequestMessage::type() const {
  return MessageType::SyncRequest;
}

QByteArray SyncRequestMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "sync_request";
  obj["path"] = QString::fromStdString(path);
  obj["contents"] = QString::fromUtf8(contents.toBase64());
  obj["mtime"] = QString::fromStdString(mtime);
  obj["username"] = username;
  obj["password"] = password;
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
  return msg;
}

std::unique_ptr<AuthResponseMessage>
AuthResponseMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<AuthResponseMessage>();
  msg->success = obj["success"].toBool();
  return msg;
}

std::unique_ptr<SyncRequestMessage>
SyncRequestMessage::deserialize(const QJsonObject &obj) {
  auto msg = std::make_unique<SyncRequestMessage>();
  msg->path = obj["path"].toString().toStdString();
  msg->username = obj["username"].toString();
  msg->password = obj["password"].toString();
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
