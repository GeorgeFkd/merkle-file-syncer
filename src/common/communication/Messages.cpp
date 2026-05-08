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
  case FileOperationStatus::Rejected:
    obj["opstatus"] = "rejected";
    break;
  case FileOperationStatus::Pending:
    obj["opstatus"] = "pending";
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
  else if (obj["opstatus"].toString() == "rejected")
    msg->operationStatus = FileOperationStatus::Rejected;
  else if (obj["opstatus"].toString() == "pending")
    msg->operationStatus = FileOperationStatus::Pending;

  return msg;
}
