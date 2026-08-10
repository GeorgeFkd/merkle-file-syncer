#include "NaiveSyncClient.h"

NaiveSyncClient::NaiveSyncClient(QObject *parent) : QObject(parent) {}

void NaiveSyncClient::startNegotiation() {
  negotiationState = NegotiationState{};
  ListRequestMessage msg;
  negotiationInProgress = true;
  Q_EMIT(sendMessage(msg));
}

void NaiveSyncClient::handleListingResponse(ListResponseMessage *msg,
                                            const FSMetadata *updatedDb,
                                            const QString &username) {
  
  QSet<QString> serverLivePaths;     // paths the server has live
  QSet<QString> serverReportedPaths; // every path the server mentioned (live or tombstoned)
  for (const auto &entry : msg->entries) {
    serverReportedPaths.insert(entry.path);
    if (!entry.deleted)
      serverLivePaths.insert(entry.path);
  }

  // --- server-reported entries: compare against reconciled local DB ---
  for (const auto &entry : msg->entries) {
    if (entry.deleted) {
      // server has a tombstone for this path.
      auto localMtime = updatedDb->readMtime(username, entry.path);
      if (localMtime.has_value() && localMtime.value() > entry.mtime) {
        // we have a live version newer than the server's deletion -> upload.
        negotiationState.diffEntries.onlyInLeft.append({true, entry.path});
      } else {
        // server's deletion wins -> apply it locally.
        negotiationState.diffEntries.deletionWinsRight.append(
            {entry.path, entry.mtime});
      }
    } else {
      // server has a live version of this path.
      auto localMtime = updatedDb->readMtime(username, entry.path);
      auto localDeletedAt = updatedDb->deletedAt(username, entry.path);

      if (localDeletedAt.has_value()) {
        // we have a local tombstone for a file the server still has live.
        if (localDeletedAt.value() > entry.mtime) {
          // our deletion is newer -> propagate the deletion to the server.
          negotiationState.diffEntries.deletionWinsLeft.append(
              {entry.path, localDeletedAt.value()});
        } else {
          // server's live version is newer -> resurrect it locally.
          negotiationState.diffEntries.onlyInRight.append({true, entry.path});
        }
      } else if (!localMtime.has_value()) {
        // we don't have it at all -> download.
        negotiationState.diffEntries.onlyInRight.append({true, entry.path});
      } else if (entry.mtime != localMtime.value()) {
        // both sides have it but versions differ -> modification, reconcile.
        negotiationState.diffEntries.modified.append(entry.path);
      }
      // equal mtime -> already in sync, nothing to do.
    }
  }

  // --- local live files the server didn't mention AT ALL -> upload ---
  // guard on serverReportedPaths (not just live) so a path the server reported
  // as a tombstone isn't also staged here as a fresh upload.
  for (const auto &path : updatedDb->allTrackedFiles(username)) {
    if (!serverReportedPaths.contains(path)) {
      negotiationState.diffEntries.onlyInLeft.append({true, path});
    }
  }

  negotiationInProgress = false;
  Q_EMIT(negotiationCompleted(negotiationState));
}

const NegotiationState *NaiveSyncClient::getNegotiationState() const {
  return &negotiationState;
}
