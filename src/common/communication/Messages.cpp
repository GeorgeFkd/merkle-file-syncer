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
    auto *msg = new SyncRequestMessage();
    msg->path = obj["path"].toString().toStdString();
    msg->username = obj["username"].toString();
    msg->password = obj["password"].toString();
    msg->contents = QByteArray::fromBase64(obj["contents"].toString().toUtf8());
    msg->mtime = obj["mtime"].toString().toStdString();
    if (obj["optype"].toString() == "write") {
      msg->operationType = FileOperationType::Write;
    } else if (obj["optype"].toString() == "delete") {
      msg->operationType = FileOperationType::Delete;
    }

    if (obj["opstatus"].toString() == "doit") {
      msg->operationStatus = FileOperationStatus::DoIt;
    } else if (obj["opstatus"].toString() == "done") {
      msg->operationStatus = FileOperationStatus::Done;
    } else if (obj["opstatus"].toString() == "rejected") {
      msg->operationStatus = FileOperationStatus::Rejected;
    } else if (obj["opstatus"].toString() == "pending") {
      msg->operationStatus = FileOperationStatus::Pending;
    }
    return msg;
  }

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
