#include "Messages.h"

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

Message *Message::deserialize(const QByteArray &data) {
  QJsonObject obj = QJsonDocument::fromJson(data).object();
  QString type = obj["type"].toString();

  if (type == "auth") {
    auto *msg = new AuthMessage();
    msg->username = obj["username"].toString();
    msg->password = obj["password"].toString();
    return msg;
  } else if (type == "auth_response") {
    auto *msg = new AuthResponseMessage();
    msg->success = obj["success"].toBool();
    return msg;
  } else if (type == "sync_request") {
    auto *msg = new ClientSyncRequestMessage();
    msg->path = obj["path"].toString().toStdString();
    msg->contents = QByteArray::fromBase64(obj["contents"].toString().toUtf8());
    msg->mtime = obj["mtime"].toString().toStdString();
    return msg;
  }

  return nullptr;
}

MessageType ClientSyncRequestMessage::type() const {
  return MessageType::ClientSyncRequest;
}

QByteArray ClientSyncRequestMessage::serialize() const {
  QJsonObject obj;
  obj["type"] = "sync_request";
  obj["path"] = QString::fromStdString(path);
  obj["contents"] = QString::fromUtf8(contents.toBase64());
  obj["mtime"] = QString::fromStdString(mtime);
  return QJsonDocument(obj).toJson();
}
