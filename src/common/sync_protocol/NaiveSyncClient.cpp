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
      right.hash = entry.hash;
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
      left.hash = updatedDb->readHash(username, path).value_or(QByteArray{});
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
      // negotiationState.diffEntries.modified.append(path);
      negotiationState.diffEntries.modifiedWinsLeft.append(path);
      break;
    case DiffBucket::ModifiedWinsRight:
      // TODO: split into directional buckets once the applier consumes them.
      negotiationState.diffEntries.modifiedWinsRight.append(path);
      // negotiationState.diffEntries.modified.append(path);
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
  for (const auto &path : updatedDb->allTrackedFiles(username)) {
    if (serverByPath.contains(path))
      continue; // already handled above
    SideState left = leftStateFor(path);
    SideState right; // absent
    applyBucket(path, classifyPath(left, right), left, right);
  }

  negotiationInProgress = false;
  Q_EMIT(negotiationCompleted(negotiationState));
}

const NegotiationState *NaiveSyncClient::getNegotiationState() const {
  return &negotiationState;
}
