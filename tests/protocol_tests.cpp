#include "FSMetadata.h"
#include "MerkleSyncClient.h"
#include "MerkleSyncServer.h"
#include "MerkleTree.h"
#include "NaiveSyncClient.h"
#include "NaiveSyncServer.h"
#include "rapidcheck/Assertions.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <type_traits>

namespace {

QByteArray hashOf(const QString &s) {
  return QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256);
}

QDateTime now() { return QDateTime::currentDateTime(); }

const QString kUser = "test-user";

// strategy tags
struct MerkleTag {};
struct NaiveTag {};

enum class FileFate {
  OnlyClient,
  OnlyServer,
  SameContent,
  DifferentContentClientNewer,
  DifferentContentServerNewer,
  DeletedOnClient,
  DeletedOnServer,
  DeletedOnClientButServerNewer,
  DeletedOnServerButClientNewer,
};

struct FileSpec {
  QString path;
  FileFate fate;
};

void buildDbsFromSpec(const QList<FileSpec> &specs, FSMetadata &clientDb,
                      FSMetadata &serverDb, NodesDiff &expected) {
  QDateTime old = now().addSecs(-10);
  QDateTime recent = now();

  qDebug() << "Old is: " << old << " recent is: " << recent;

  for (const auto &spec : specs) {
    switch (spec.fate) {
    case FileFate::OnlyClient:
      clientDb.recordFile(kUser, spec.path, recent, hashOf(spec.path + "c"));
      expected.onlyInLeft.append({true, spec.path});
      break;
    case FileFate::OnlyServer:
      serverDb.recordFile(kUser, spec.path, recent, hashOf(spec.path + "s"));
      expected.onlyInRight.append({true, spec.path});
      break;
    case FileFate::SameContent: {
      auto h = hashOf(spec.path);
      clientDb.recordFile(kUser, spec.path, old, h);
      serverDb.recordFile(kUser, spec.path, old, h);
      break;
    }
    case FileFate::DifferentContentClientNewer:
      clientDb.recordFile(kUser, spec.path, recent, hashOf(spec.path + "c"));
      serverDb.recordFile(kUser, spec.path, old, hashOf(spec.path + "s"));
      expected.modified.append(spec.path);
      break;
    case FileFate::DifferentContentServerNewer:
      clientDb.recordFile(kUser, spec.path, old, hashOf(spec.path + "c"));
      serverDb.recordFile(kUser, spec.path, recent, hashOf(spec.path + "s"));
      expected.modified.append(spec.path);
      break;
    case FileFate::DeletedOnClient:
      clientDb.recordFile(kUser, spec.path, old, hashOf(spec.path));
      serverDb.recordFile(kUser, spec.path, old, hashOf(spec.path));
      clientDb.recordDeletion(kUser, spec.path, recent);
      expected.deletionWinsLeft.append({spec.path, recent});
      break;
    case FileFate::DeletedOnServer:
      clientDb.recordFile(kUser, spec.path, old, hashOf(spec.path));
      serverDb.recordFile(kUser, spec.path, old, hashOf(spec.path));
      serverDb.recordDeletion(kUser, spec.path, recent);
      expected.deletionWinsRight.append({spec.path, recent});
      break;
    case FileFate::DeletedOnClientButServerNewer:
      clientDb.recordFile(kUser, spec.path, old, hashOf(spec.path));
      clientDb.recordDeletion(kUser, spec.path, old);
      serverDb.recordFile(kUser, spec.path, recent, hashOf(spec.path + "s"));
      expected.onlyInRight.append({true, spec.path});
      break;
    case FileFate::DeletedOnServerButClientNewer:
      serverDb.recordFile(kUser, spec.path, old, hashOf(spec.path));
      serverDb.recordDeletion(kUser, spec.path, old);
      clientDb.recordFile(kUser, spec.path, recent, hashOf(spec.path + "c"));
      expected.onlyInLeft.append({true, spec.path});
      break;
    }
  }
}

// --- merkle negotiation ---
NodesDiff runMerkle(FSMetadata &clientDb, FSMetadata &serverDb) {
  MerkleTree *clientTree = clientDb.getUserTree(kUser);
  MerkleTree *serverTree = serverDb.getUserTree(kUser);

  MerkleSyncClient protocolClient;
  MerkleSyncServer protocolServer;

  QObject::connect(&protocolClient, &MerkleSyncClient::messageSendRequest,
                   [&](MerkleProtocolMessage msg) {
                     protocolServer.handleRequest(msg, serverTree, "test-conn");
                   });
  QObject::connect(&protocolServer, &MerkleSyncServer::messageSendRequest,
                   [&](QString, MerkleProtocolMessage msg) {
                     protocolClient.handleResponse(msg, clientTree);
                   });

  bool completed = false;
  QObject::connect(&protocolClient, &MerkleSyncClient::negotiationCompleted,
                   [&]() { completed = true; });

  protocolClient.startNegotiation(clientTree);
  assert(completed);
  return protocolClient.getNegotiationState()->diffEntries;
}

// --- naive negotiation ---
NodesDiff runNaive(FSMetadata &clientDb, FSMetadata &serverDb) {
  NaiveSyncClient protocolClient;
  NaiveSyncServer protocolServer;

  QObject::connect(&protocolClient, &NaiveSyncClient::sendMessage,
                   [&](ListRequestMessage req) {
                     protocolServer.handleRequest(&req, "test-conn", &serverDb,
                                                  kUser);
                   });
  QObject::connect(&protocolServer, &NaiveSyncServer::sendMessage,
                   [&](ListResponseMessage resp, ConnectionId) {
                     protocolClient.handleListingResponse(&resp, &clientDb,
                                                          kUser);
                   });

  bool completed = false;
  QObject::connect(&protocolClient, &NaiveSyncClient::negotiationCompleted,
                   [&](const NegotiationState &) { completed = true; });

  protocolClient.startNegotiation();
  assert(completed);
  return protocolClient.getNegotiationState()->diffEntries;
}

// tag dispatch
template <typename Tag>
NodesDiff runNegotiation(FSMetadata &clientDb, FSMetadata &serverDb) {
  if constexpr (std::is_same_v<Tag, MerkleTag>) {
    return runMerkle(clientDb, serverDb);
  } else if constexpr (std::is_same_v<Tag, NaiveTag>) {
    return runNaive(clientDb, serverDb);
  } else {
    static_assert(sizeof(Tag) == 0, "unknown sync strategy tag");
  }
}

QSet<QString> pathsOf(const QList<QPair<bool, QString>> &list) {
  QSet<QString> out;
  for (const auto &[isFile, p] : list)
    out.insert(p);
  return out;
}
QSet<QString> pathsOf(const QList<DeletionEntry> &list) {
  QSet<QString> out;
  for (const auto &e : list)
    out.insert(e.path);
  return out;
}
QSet<QString> pathsOf(const QList<QString> &list) {
  return QSet<QString>(list.begin(), list.end());
}


} // namespace

namespace rc {
template <> struct Arbitrary<FileFate> {
  static Gen<FileFate> arbitrary() {
    return gen::element(
        FileFate::OnlyClient, FileFate::OnlyServer, FileFate::SameContent,
        FileFate::DifferentContentClientNewer,
        FileFate::DifferentContentServerNewer, FileFate::DeletedOnClient,
        FileFate::DeletedOnServer, FileFate::DeletedOnClientButServerNewer,
        FileFate::DeletedOnServerButClientNewer);
  }
};
template <> struct Arbitrary<FileSpec> {
  static Gen<FileSpec> arbitrary() {
    return gen::build<FileSpec>(
        gen::set(&FileSpec::path,
                 gen::map(gen::inRange(0, 1000),
                          [](int i) { return QString("file%1.txt").arg(i); })),
        gen::set(&FileSpec::fate, gen::arbitrary<FileFate>()));
  }
};
} // namespace rc

template <typename T> class SyncProtocolTest : public ::testing::Test {};

using SyncStrategies = ::testing::Types<MerkleTag, NaiveTag>;
TYPED_TEST_SUITE(SyncProtocolTest, SyncStrategies);

RC_GTEST_TYPED_FIXTURE_PROP(SyncProtocolTest, negotiationMatchesExpectedDiff,
                            (const std::vector<FileSpec> &rawSpecs)) {
  QSet<QString> seen;
  QList<FileSpec> specs;
  for (const auto &s : rawSpecs) {
    if (seen.contains(s.path))
      continue;
    seen.insert(s.path);
    specs.append(s);
  }
  RC_PRE(!specs.isEmpty());
  RC_LOG() << "specs size: " << specs.size() << "\n";

  NodesDiff expected;
  FSMetadata clientDb;
  FSMetadata serverDb;
  buildDbsFromSpec(specs, clientDb, serverDb, expected);

  auto actual = runNegotiation<TypeParam>(clientDb, serverDb);

    // ---- ADD DIAGNOSTIC HERE ----
  for (const auto &s : specs) {
    RC_LOG() << "fate: " << static_cast<int>(s.fate)
             << " path: " << s.path.toStdString() << "\n";
  }
  RC_LOG() << "actual onlyInLeft:   "
           << pathsOf(actual.onlyInLeft).values().join(",").toStdString();
  RC_LOG() << " expected onlyInLeft: "
           << pathsOf(expected.onlyInLeft).values().join(",").toStdString();


  RC_ASSERT(pathsOf(actual.onlyInLeft) == pathsOf(expected.onlyInLeft));
  RC_ASSERT(pathsOf(actual.onlyInRight) == pathsOf(expected.onlyInRight));
  RC_ASSERT(pathsOf(actual.modified) == pathsOf(expected.modified));
  RC_ASSERT(pathsOf(actual.deletionWinsLeft) ==
            pathsOf(expected.deletionWinsLeft));
  RC_ASSERT(pathsOf(actual.deletionWinsRight) ==
            pathsOf(expected.deletionWinsRight));
}

