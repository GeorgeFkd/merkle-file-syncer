#include "NaiveSyncServer.h"

NaiveSyncServer::NaiveSyncServer(QObject *parent) : QObject(parent) {}


void NaiveSyncServer::handleRequest(ListRequestMessage* msg, ConnectionId connId, const FSMetadata* updatedDb,const QString& username) {
  assert(msg->directory.isEmpty() &&
           "When using naive, directory should be empty, otherwise we should filter by path, this is not tested yet");
  ListResponseMessage response;
    auto files = updatedDb->allTrackedFiles(username);
    for (const auto &path : files) {
      auto mtime = updatedDb->readMtime(username, path);
      assert(mtime.has_value() && "File in storage must have a DB mtime entry");
      response.entries.append({path, mtime.value(), false});
    }

    auto tombstones = updatedDb->allTombstones(username);
    for (auto it = tombstones.cbegin(); it != tombstones.cend(); ++it) {
      const QString &path = it.key();
      response.entries.append({path, it.value(), true});
    }
    Q_EMIT(sendMessage(response,connId));
}

