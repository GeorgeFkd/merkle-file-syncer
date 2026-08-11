#include "NaiveSyncClient.h"
#include "SyncCommon.h"
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
  QHash<QString, SideState> serverByPath;
  for (const auto &entry : msg->entries) {
    SideState right;
    if (entry.deleted) {
      right.deletedAt = entry.mtime; // tombstone time carried in mtime field
    } else {
      right.mtime = entry.mtime;
      right.isDirectory = false; // naive listing is files only
    }
    serverByPath.insert(entry.path, right);
  }

  auto leftStateFor = [&](const QString &path) {
    SideState left;
    auto deletedAt = updatedDb->deletedAt(username, path);
    if (deletedAt.has_value()) {
      left.deletedAt = deletedAt;
    } else {
      left.mtime = updatedDb->readMtime(username, path); // optional -> optional
      left.isDirectory = false;
    }
    return left;
  };

  auto applyBucket = [&](const QString &path, DiffBucket bucket,
                         const SideState &left, const SideState &right) {
    switch (bucket) {
    case DiffBucket::InSync:
      break;
    case DiffBucket::OnlyInLeft:
      negotiationState.diffEntries.onlyInLeft.append({true, path});
      break;
    case DiffBucket::OnlyInRight:
      negotiationState.diffEntries.onlyInRight.append({true, path});
      break;
    case DiffBucket::ModifiedWinsLeft:
    case DiffBucket::ModifiedWinsRight:
      // TODO: split into directional buckets once the applier consumes them.
      negotiationState.diffEntries.modified.append(path);
      break;
    case DiffBucket::DeletionWinsLeft:
      negotiationState.diffEntries.deletionWinsLeft.append(
          {path, left.deletedAt.value()});
      break;
    case DiffBucket::DeletionWinsRight:
      negotiationState.diffEntries.deletionWinsRight.append(
          {path, right.deletedAt.value()});
      break;
    case DiffBucket::NeedsDirectoryCheck:
      // naive has no directory recursion; shouldn't occur for file listings.
      break;
    }
  };

  // --- paths the server reported: classify each against local state ---
  for (const auto &entry : msg->entries) {
    const QString &path = entry.path;
    SideState left = leftStateFor(path);
    const SideState &right = serverByPath.value(path);
    applyBucket(path, classifyPath(left, right), left, right);
  }

  // --- local live files the server didn't mention at all -> classify with an
  //     absent server side (yields OnlyInLeft for live, InSync for tombstones)
  //     ---
  for (const auto &path : updatedDb->allTrackedFiles(username)) {
    if (serverByPath.contains(path))
      continue; // already handled above
    SideState left = leftStateFor(path);
    SideState right; // absent
    applyBucket(path, classifyPath(left, right), left, right);
  }

  negotiationInProgress = false;
  Q_EMIT(negotiationCompleted(negotiationState));

  // QSet<QString> serverLivePaths;     // paths the server has live
  // QSet<QString> serverReportedPaths; // every path the server mentioned (live
  // or tombstoned) for (const auto &entry : msg->entries) {
  //   serverReportedPaths.insert(entry.path);
  //   if (!entry.deleted)
  //     serverLivePaths.insert(entry.path);
  // }
  //
  // // --- server-reported entries: compare against reconciled local DB ---
  // for (const auto &entry : msg->entries) {
  //   if (entry.deleted) {
  //     // server has a tombstone for this path.
  //     auto localMtime = updatedDb->readMtime(username, entry.path);
  //     if (localMtime.has_value() && localMtime.value() > entry.mtime) {
  //       // we have a live version newer than the server's deletion -> upload.
  //       negotiationState.diffEntries.onlyInLeft.append({true, entry.path});
  //     } else {
  //       // server's deletion wins -> apply it locally.
  //       negotiationState.diffEntries.deletionWinsRight.append(
  //           {entry.path, entry.mtime});
  //     }
  //   } else {
  //     // server has a live version of this path.
  //     auto localMtime = updatedDb->readMtime(username, entry.path);
  //     auto localDeletedAt = updatedDb->deletedAt(username, entry.path);
  //
  //     if (localDeletedAt.has_value()) {
  //       // we have a local tombstone for a file the server still has live.
  //       if (localDeletedAt.value() > entry.mtime) {
  //         // our deletion is newer -> propagate the deletion to the server.
  //         negotiationState.diffEntries.deletionWinsLeft.append(
  //             {entry.path, localDeletedAt.value()});
  //       } else {
  //         // server's live version is newer -> resurrect it locally.
  //         negotiationState.diffEntries.onlyInRight.append({true,
  //         entry.path});
  //       }
  //     } else if (!localMtime.has_value()) {
  //       // we don't have it at all -> download.
  //       negotiationState.diffEntries.onlyInRight.append({true, entry.path});
  //     } else if (entry.mtime != localMtime.value()) {
  //       // both sides have it but versions differ -> modification, reconcile.
  //       negotiationState.diffEntries.modified.append(entry.path);
  //     }
  //     // equal mtime -> already in sync, nothing to do.
  //   }
  // }
  //
  // // --- local live files the server didn't mention AT ALL -> upload ---
  // // guard on serverReportedPaths (not just live) so a path the server
  // reported
  // // as a tombstone isn't also staged here as a fresh upload.
  // for (const auto &path : updatedDb->allTrackedFiles(username)) {
  //   if (!serverReportedPaths.contains(path)) {
  //     negotiationState.diffEntries.onlyInLeft.append({true, path});
  //   }
  // }
  //
  // negotiationInProgress = false;
  // Q_EMIT(negotiationCompleted(negotiationState));
}

const NegotiationState *NaiveSyncClient::getNegotiationState() const {
  return &negotiationState;
}
