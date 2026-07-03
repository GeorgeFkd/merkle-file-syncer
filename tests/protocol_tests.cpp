#include "MerkleSyncClient.h"
#include "MerkleSyncServer.h"
#include "MerkleTree.h"
#include "rapidcheck/Assertions.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

namespace {

QByteArray hashOf(const QString &s) {
  return QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256);
}

QDateTime now() { return QDateTime::currentDateTime(); }

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

QPair<MerkleTree, MerkleTree> buildTreesFromSpec(const QList<FileSpec> &specs,
                                                 NodesDiff &expected) {
  MerkleTree client("root");
  MerkleTree server("root");

  QDateTime old = now().addSecs(-10);
  QDateTime recent = now();

  for (const auto &spec : specs) {
    switch (spec.fate) {
    case FileFate::OnlyClient:
      client.addFile(spec.path.toStdString(), recent, hashOf(spec.path + "c"));
      expected.onlyInLeft.append({true, spec.path});
      break;
    case FileFate::OnlyServer:
      server.addFile(spec.path.toStdString(), recent, hashOf(spec.path + "s"));
      expected.onlyInRight.append({true, spec.path});
      break;
    case FileFate::SameContent: {
      auto h = hashOf(spec.path);
      client.addFile(spec.path.toStdString(), old, h);
      server.addFile(spec.path.toStdString(), old, h);
      break;
    }
    case FileFate::DifferentContentClientNewer:
      client.addFile(spec.path.toStdString(), recent, hashOf(spec.path + "c"));
      server.addFile(spec.path.toStdString(), old, hashOf(spec.path + "s"));
      expected.modified.append(spec.path);
      break;
    case FileFate::DifferentContentServerNewer:
      client.addFile(spec.path.toStdString(), old, hashOf(spec.path + "c"));
      server.addFile(spec.path.toStdString(), recent, hashOf(spec.path + "s"));
      expected.modified.append(spec.path);
      break;
    case FileFate::DeletedOnClient:
      client.addFile(spec.path.toStdString(), old, hashOf(spec.path));
      server.addFile(spec.path.toStdString(), old, hashOf(spec.path));
      client.deleteFile(spec.path.toStdString(), recent);
      expected.deletionWinsLeft.append({spec.path, recent});
      break;
    case FileFate::DeletedOnServer:
      client.addFile(spec.path.toStdString(), old, hashOf(spec.path));
      server.addFile(spec.path.toStdString(), old, hashOf(spec.path));
      server.deleteFile(spec.path.toStdString(), recent);
      expected.deletionWinsRight.append({spec.path, recent});
      break;
    case FileFate::DeletedOnClientButServerNewer:
      // client tombstones old, server writes newer live version → resurrect on
      // client
      client.addFile(spec.path.toStdString(), old, hashOf(spec.path));
      client.deleteFile(spec.path.toStdString(), old);
      server.addFile(spec.path.toStdString(), recent, hashOf(spec.path + "s"));
      expected.onlyInRight.append({true, spec.path});
      break;
    case FileFate::DeletedOnServerButClientNewer:
      // server tombstones old, client writes newer live version → propagate to
      // server
      server.addFile(spec.path.toStdString(), old, hashOf(spec.path));
      server.deleteFile(spec.path.toStdString(), old);
      client.addFile(spec.path.toStdString(), recent, hashOf(spec.path + "c"));
      expected.onlyInLeft.append({true, spec.path});
      break;
    }
  }
  return qMakePair(std::move(client), std::move(server));
}

NodesDiff runNegotiation(MerkleTree &clientTree, MerkleTree &serverTree) {
  MerkleSyncClient protocolClient;
  MerkleSyncServer protocolServer;

  QObject::connect(&protocolClient, &MerkleSyncClient::messageSendRequest,
                   [&](MerkleProtocolMessage msg) {
                     protocolServer.handleRequest(msg, &serverTree,
                                                  "test-conn");
                   });
  QObject::connect(&protocolServer, &MerkleSyncServer::messageSendRequest,
                   [&](QString, MerkleProtocolMessage msg) {
                     protocolClient.handleResponse(msg, &clientTree);
                   });

  bool completed = false;
  QObject::connect(&protocolClient, &MerkleSyncClient::negotiationCompleted,
                   [&]() { completed = true; });

  protocolClient.startNegotiation(&clientTree);
  assert(completed);
  qDebug() << "Negotiation Completed";
  return protocolClient.getNegotiationState()->diffEntries;
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

RC_GTEST_PROP(MerkleProtocol, negotiationMatchesExpectedDiff,
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
  RC_LOG() << "specs size: " << specs.size();
  NodesDiff expected;
  auto [clientTree, serverTree] = buildTreesFromSpec(specs, expected);
  auto actual = runNegotiation(clientTree, serverTree);

  // RC_ASSERT(false);
  RC_ASSERT(pathsOf(actual.onlyInLeft) == pathsOf(expected.onlyInLeft));
  RC_ASSERT(pathsOf(actual.onlyInRight) == pathsOf(expected.onlyInRight));
  RC_ASSERT(pathsOf(actual.modified) == pathsOf(expected.modified));
  RC_ASSERT(pathsOf(actual.deletionWinsLeft) ==
            pathsOf(expected.deletionWinsLeft));
  RC_ASSERT(pathsOf(actual.deletionWinsRight) ==
            pathsOf(expected.deletionWinsRight));
}
