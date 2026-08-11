#include "FileClient.h"
#include "FileServer.h"
#include "FileTree.h"
#include "LocalFileStorage.h"
#include "S3FileStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

struct LocalStorageTag {
  static std::unique_ptr<FileStorage> makeStorage(const QString &rootPath) {
    auto s = std::make_unique<LocalFileStorage>();
    s->setRoot(rootPath);
    return s;
  }
};

struct S3StorageTag {
  static std::unique_ptr<FileStorage> makeStorage(const QString &rootPath) {
    auto s = std::make_unique<S3FileStorage>();
    s->init(S3Config{.endpoint = "localhost:9000",
                     .accessKey = "minioadmin",
                     .secretKey = "minioadmin",
                     .bucket = "test-bucket",
                     .useSSL = false});
    return s;
  }
};

struct LocalNaiveLocalSocketTag {
  using Storage = LocalStorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Naive;
  static constexpr TransportProtocol protocol = TransportProtocol::LocalSocket;
};
struct LocalMerkleLocalSocketTag {
  using Storage = LocalStorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Merkle;
  static constexpr TransportProtocol protocol = TransportProtocol::LocalSocket;
};
struct S3NaiveLocalSocketTag {
  using Storage = S3StorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Naive;
  static constexpr TransportProtocol protocol = TransportProtocol::LocalSocket;
};
struct S3MerkleLocalSocketTag {
  using Storage = S3StorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Merkle;
  static constexpr TransportProtocol protocol = TransportProtocol::LocalSocket;
};

struct LocalNaiveTcpTag {
  using Storage = LocalStorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Naive;
  static constexpr TransportProtocol protocol = TransportProtocol::Tcp;
};
struct LocalMerkleTcpTag {
  using Storage = LocalStorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Merkle;
  static constexpr TransportProtocol protocol = TransportProtocol::Tcp;
};
struct S3NaiveTcpTag {
  using Storage = S3StorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Naive;
  static constexpr TransportProtocol protocol = TransportProtocol::Tcp;
};
struct S3MerkleTcpTag {
  using Storage = S3StorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Merkle;
  static constexpr TransportProtocol protocol = TransportProtocol::Tcp;
};

using SyncTestImplementations =
    ::testing::Types<S3MerkleLocalSocketTag, S3NaiveLocalSocketTag,
                     LocalMerkleLocalSocketTag, LocalNaiveLocalSocketTag,
                     S3MerkleTcpTag, S3NaiveTcpTag, LocalMerkleTcpTag,
                     LocalNaiveTcpTag>;

struct LocalNaiveTag {
  using Storage = LocalStorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Naive;
};

struct LocalMerkleTag {
  using Storage = LocalStorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Merkle;
};

struct S3NaiveTag {
  using Storage = S3StorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Naive;
};

struct S3MerkleTag {
  using Storage = S3StorageTag;
  static constexpr SyncStrategy strategy = SyncStrategy::Merkle;
};

// using SyncTestImplementations =
// ::testing::Types<S3MerkleTag,S3NaiveTag,LocalMerkleTag,LocalNaiveTag>;

template <typename Tag> class SyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clientDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_client/" + runId);
    serverDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_server/" + runId);
    QDir().mkpath(clientDir->path());
    QDir().mkpath(serverDir->path());

    QString endpoint = endpointFor(Tag::protocol, runId);

    fileServer.configure(FileServerConfig{
        .protocol = Tag::protocol,
        .serverName = endpoint,
        .storage = Tag::Storage::makeStorage(serverDir->path())});
    fileServer.getStorage()->cleanup(username);
    fileServer.start();
    qDebug() << "Server serverName after start:" << fileServer.serverName();

    QString clientEndpoint = fileServer.serverName();
    client = std::make_unique<FileClient>();
    client->configure(FileClientConfig{.protocol = Tag::protocol,
                                       .rootDir = clientDir->path(),
                                       .username = username,
                                       .password = "bar",
                                       .syncStrategy = Tag::strategy,
                                       .manualTick = true,
                                       .serverName = clientEndpoint,
                                       .deviceName = deviceName});
    client->start();
  }

  QString endpointFor(TransportProtocol protocol, const QString &runId) {
    if (protocol == TransportProtocol::Tcp) {
      return "127.0.0.1:0"; // 0 = OS picks free port
    }
    return "merkle_sync_test_" + runId; // unique local socket name
  }

  void TearDown() override {
    if (HasFailure()) {
      qDebug() << "--- Client tree ---";
      client->getStorage()->showFileTree(username);
      qDebug() << "--- Server tree ---";
      fileServer.getStorage()->showFileTree(username);
    }
    QDir(clientDir->path()).removeRecursively();
    fileServer.getStorage()->cleanup(username);
    delete clientDir;
    delete serverDir;
  }
  
  void addMinimumDelayForTimestampOrdering() {
    QThread::msleep(
        5); // ensure subsequent operations have strictly newer wall-clock time
  }

  void waitForSync(FileClient &client) {
    QEventLoop loop;
    QObject::connect(&client, &FileClient::syncCompleted, &loop,
                     &QEventLoop::quit);
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
  }
  bool filesystemsAreEqual() {
    return this->client->getStorage()->isEqualTo(*this->fileServer.getStorage(),
                                                 this->username);
  }

  std::unique_ptr<FileClient> client;
  QDir *clientDir = nullptr;
  QDir *serverDir = nullptr;
  FileServer fileServer;
  QString username = "foo";
  QString deviceName = "client1";
};

TYPED_TEST_SUITE(SyncTest, SyncTestImplementations);

TYPED_TEST(SyncTest, singularFileIsSynced) {
  auto filename = "test.txt";
  auto filecontents = "Hello World";
  this->client->writeFile(this->username, filename, filecontents);

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  ASSERT_TRUE(this->filesystemsAreEqual());
}

TYPED_TEST(SyncTest, serverFileOlderThanClientIsUpdated) {
  QString filename = "test.txt";
  QDateTime base = QDateTime::currentDateTime();

  this->fileServer.writeFile(this->username, filename, "original",
                             base.addSecs(-10));
  this->client->writeFile(this->username, filename, "updated by client");

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  ASSERT_TRUE(this->filesystemsAreEqual());
}

TYPED_TEST(SyncTest, serverFileNewerThanClientIsRejected) {
  QString filename = "test.txt";
  QDateTime base = QDateTime::currentDateTime();

  this->fileServer.writeFile(this->username, filename, "server newer version",
                             base.addSecs(10));
  this->client->writeFile(this->username, filename, "client older version");

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  auto serverContents =
      this->fileServer.getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(serverContents.has_value());
  ASSERT_EQ(serverContents.value(), QByteArray("server newer version"));

  auto clientContents =
      this->client->getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(clientContents.has_value());
  ASSERT_EQ(clientContents.value(), QByteArray("server newer version"));

  ASSERT_TRUE(this->filesystemsAreEqual());
}

TYPED_TEST(SyncTest, fileInNewDirectoryIsSynced) {
  this->client->writeFile(this->username, "subdir/nested/test.txt",
                          "nested content");

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  ASSERT_TRUE(this->filesystemsAreEqual());
}

TYPED_TEST(SyncTest, deletedFileIsSyncedToServer) {
  this->client->writeFile(this->username, "test.txt", "to be deleted");

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  auto contentsBefore =
      this->fileServer.getStorage()->readFile(this->username, "test.txt");
  ASSERT_TRUE(contentsBefore.has_value());
  ASSERT_EQ(contentsBefore.value(), QByteArray("to be deleted"));

  // delete straight through storage (simulating an external/user deletion);
  // the next tick's scan should detect the absence and propagate it.
  this->addMinimumDelayForTimestampOrdering();
  ASSERT_TRUE(
      this->client->getStorage()->deleteFile(this->username, "test.txt"));
  this->client->clientTick();
  this->waitForSync(*(this->client));

  ASSERT_TRUE(this->filesystemsAreEqual());
  ASSERT_FALSE(this->fileServer.getStorage()
                   ->readFile(this->username, "test.txt")
                   .has_value());
}

TYPED_TEST(SyncTest, directoryDeleteIsSyncedToServer) {
  this->client->writeFile(this->username, "subdir/file1.txt", "file1");
  this->client->writeFile(this->username, "subdir/file2.txt", "file2");

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  ASSERT_TRUE(this->fileServer.getStorage()
                  ->readFile(this->username, "subdir/file1.txt")
                  .has_value());
  ASSERT_TRUE(this->fileServer.getStorage()
                  ->readFile(this->username, "subdir/file2.txt")
                  .has_value());

  // this->addMinimumDelayForTimestampOrdering();
  // delete both through storage; the scan detects both absences on next tick.
  ASSERT_TRUE(this->client->getStorage()->deleteFile(this->username,
                                                     "subdir/file1.txt"));
  ASSERT_TRUE(this->client->getStorage()->deleteFile(this->username,
                                                     "subdir/file2.txt"));
  this->client->clientTick();
  this->waitForSync(*(this->client));

  ASSERT_TRUE(this->filesystemsAreEqual());
}

template <typename Tag> class MultiDeviceSyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    serverDir = QDir(QCoreApplication::applicationDirPath() +
                     "/test_multidevice_server/" + runId);
    QDir().mkpath(serverDir.path());

    endpoint = endpointFor(Tag::protocol, runId);

    fileServer.configure(FileServerConfig{
        .protocol = Tag::protocol,
        .serverName = endpoint,
        .storage = Tag::Storage::makeStorage(serverDir.path())});
    fileServer.getStorage()->cleanup(username);
    fileServer.start();

    deviceA = makeClient("device-a");
    deviceB = makeClient("device-b");
    deviceC = makeClient("device-c");
  }

  void addMinimumDelayForTimestampOrdering() {
    QThread::msleep(
        5); // ensure subsequent operations have strictly newer wall-clock time
  }

  void TearDown() override {
    if (HasFailure()) {
      qDebug() << "--- Server tree ---";
      fileServer.getStorage()->showFileTree(username);
      qDebug() << "--- Device A tree ---";
      deviceA->getStorage()->showFileTree(username);
      qDebug() << "--- Device B tree ---";
      deviceB->getStorage()->showFileTree(username);
      qDebug() << "--- Device C tree ---";
      deviceC->getStorage()->showFileTree(username);
    }
    for (const auto &dir : clientDirs) {
      QDir(dir).removeRecursively();
    }
    fileServer.getStorage()->cleanup(username);
  }

  std::unique_ptr<FileClient> makeClient(const QString &deviceName) {
    QString dirPath = QCoreApplication::applicationDirPath() +
                      "/test_multidevice_" + deviceName + "/" + runId;
    QDir().mkpath(dirPath);
    clientDirs.append(dirPath);

    auto client = std::make_unique<FileClient>();
    client->configure(FileClientConfig{
        .protocol = Tag::protocol,
        .rootDir = dirPath,
        .username = username,
        .password = "bar",
        .syncStrategy = Tag::strategy,
        .manualTick = true,
        .serverName = endpoint,
        .deviceName = deviceName,
    });
    client->start();
    return client;
  }

  void tickAndWait(FileClient &client) {
    QCoreApplication::processEvents();
    client.clientTick();
    QEventLoop loop;
    QObject::connect(&client, &FileClient::syncCompleted, &loop,
                     &QEventLoop::quit);
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();
  }

  QString endpointFor(TransportProtocol protocol, const QString &runId) {
    if (protocol == TransportProtocol::Tcp) {
      int port = 50000 + QRandomGenerator::global()->bounded(10000);
      return "127.0.0.1:" + QString::number(port);
    }
    return "multidevice_sync_test_" + runId;
  }

  QString runId;
  QString endpoint;
  QDir serverDir;
  QList<QString> clientDirs;
  QString username = "foo";
  FileServer fileServer;
  std::unique_ptr<FileClient> deviceA;
  std::unique_ptr<FileClient> deviceB;
  std::unique_ptr<FileClient> deviceC;
};

using MultiDeviceSyncTestImplementations =
    ::testing::Types<LocalNaiveLocalSocketTag, S3NaiveLocalSocketTag,
                     LocalNaiveTcpTag, S3NaiveTcpTag, LocalMerkleLocalSocketTag,
                     LocalMerkleTcpTag, S3MerkleLocalSocketTag, S3MerkleTcpTag>;
TYPED_TEST_SUITE(MultiDeviceSyncTest, MultiDeviceSyncTestImplementations);

TYPED_TEST(MultiDeviceSyncTest, singularFileIsSyncedAcrossDevices) {
  this->deviceA->writeFile(this->username, "test.txt", "Hello World");
  this->tickAndWait(*this->deviceA);
  this->tickAndWait(*this->deviceB);
  this->tickAndWait(*this->deviceC);

  ASSERT_TRUE(this->deviceA->getStorage()->isEqualTo(
      *this->fileServer.getStorage(), this->username));
  ASSERT_TRUE(this->deviceB->getStorage()->isEqualTo(
      *this->fileServer.getStorage(), this->username));
  ASSERT_TRUE(this->deviceC->getStorage()->isEqualTo(
      *this->fileServer.getStorage(), this->username));
}

TYPED_TEST(MultiDeviceSyncTest, deviceCanReceiveExistingServerState) {
  this->fileServer.writeFile(this->username, "test.txt", "preexisting",
                             QDateTime::currentDateTime());
  this->tickAndWait(*this->deviceA);

  auto contents =
      this->deviceA->getStorage()->readFile(this->username, "test.txt");
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray("preexisting"));
}

TYPED_TEST(MultiDeviceSyncTest, fileInNewDirectoryIsSyncedAcrossDevices) {
  this->deviceA->writeFile(this->username, "subdir/nested/test.txt",
                           "nested content");
  this->tickAndWait(*this->deviceA);
  this->tickAndWait(*this->deviceB);
  this->tickAndWait(*this->deviceC);

  ASSERT_TRUE(this->deviceB->getStorage()->isEqualTo(
      *this->fileServer.getStorage(), this->username));
  ASSERT_TRUE(this->deviceC->getStorage()->isEqualTo(
      *this->fileServer.getStorage(), this->username));
}

TYPED_TEST(MultiDeviceSyncTest, deletionPropagatesAcrossDevices) {
  // All three devices have the file
  this->deviceA->writeFile(this->username, "shared.txt", "hello");
  this->tickAndWait(*this->deviceA);
  this->tickAndWait(*this->deviceB);
  this->tickAndWait(*this->deviceC);

  ASSERT_TRUE(this->deviceB->getStorage()
                  ->readFile(this->username, "shared.txt")
                  .has_value());
  ASSERT_TRUE(this->deviceC->getStorage()
                  ->readFile(this->username, "shared.txt")
                  .has_value());

  this->addMinimumDelayForTimestampOrdering();
  // A deletes through storage; the scan detects the absence on next tick
  ASSERT_TRUE(
      this->deviceA->getStorage()->deleteFile(this->username, "shared.txt"));
  this->tickAndWait(*this->deviceA);

  // B and C should learn about the deletion
  this->tickAndWait(*this->deviceB);
  this->tickAndWait(*this->deviceC);

  ASSERT_FALSE(this->deviceB->getStorage()
                   ->readFile(this->username, "shared.txt")
                   .has_value());
  ASSERT_FALSE(this->deviceC->getStorage()
                   ->readFile(this->username, "shared.txt")
                   .has_value());
}

TYPED_TEST(MultiDeviceSyncTest, directoryDeletionPropagatesAcrossDevices) {
  this->deviceA->writeFile(this->username, "subdir/file1.txt", "file1");
  this->deviceA->writeFile(this->username, "subdir/file2.txt", "file2");
  this->tickAndWait(*this->deviceA);
  this->tickAndWait(*this->deviceB);
  this->tickAndWait(*this->deviceC);

  this->addMinimumDelayForTimestampOrdering();
  ASSERT_TRUE(this->deviceA->getStorage()->deleteFile(this->username,
                                                      "subdir/file1.txt"));
  ASSERT_TRUE(this->deviceA->getStorage()->deleteFile(this->username,
                                                      "subdir/file2.txt"));
  this->tickAndWait(*this->deviceA);
  this->tickAndWait(*this->deviceB);
  this->tickAndWait(*this->deviceC);

  ASSERT_TRUE(this->deviceB->getStorage()->isEqualTo(
      *this->fileServer.getStorage(), this->username));
  ASSERT_TRUE(this->deviceC->getStorage()->isEqualTo(
      *this->fileServer.getStorage(), this->username));
}

TYPED_TEST(MultiDeviceSyncTest, serverNewerWinsForAllDevices) {
  QDateTime base = QDateTime::currentDateTime();
  this->fileServer.writeFile(this->username, "test.txt", "server version",
                             base.addSecs(10));
  this->deviceA->writeFile(this->username, "test.txt", "device A older");
  this->deviceB->writeFile(this->username, "test.txt", "device B older");

  this->tickAndWait(*this->deviceA);
  this->tickAndWait(*this->deviceB);
  this->tickAndWait(*this->deviceC);

  for (auto *dev :
       {this->deviceA.get(), this->deviceB.get(), this->deviceC.get()}) {
    auto contents = dev->getStorage()->readFile(this->username, "test.txt");
    ASSERT_TRUE(contents.has_value());
    ASSERT_EQ(contents.value(), QByteArray("server version"));
  }
}

TYPED_TEST(MultiDeviceSyncTest, serverNewerRejectsClientDelete) {
  this->deviceA->writeFile(this->username, "test.txt", "original");
  this->tickAndWait(*this->deviceA);
  this->tickAndWait(*this->deviceB);

  this->addMinimumDelayForTimestampOrdering();
  // A deletes through storage (detected as absent by A's next scan)
  ASSERT_TRUE(
      this->deviceA->getStorage()->deleteFile(this->username, "test.txt"));
  this->deviceA->scanFilesystemAndApplyChangesToDb();

  this->addMinimumDelayForTimestampOrdering();
  this->deviceB->writeFile(this->username, "test.txt",
                           "B's newer version"); // later than delete
  this->tickAndWait(*this->deviceB); // B's newer version reaches server FIRST

  this->tickAndWait(
      *this->deviceA); // A's stale delete arrives -> server rejects

  auto serverContents =
      this->fileServer.getStorage()->readFile(this->username, "test.txt");
  ASSERT_TRUE(serverContents.has_value());
  ASSERT_EQ(serverContents.value(), QByteArray("B's newer version"));
  // A should have restored B's version after the reject
  auto aContents =
      this->deviceA->getStorage()->readFile(this->username, "test.txt");
  ASSERT_TRUE(aContents.has_value());
  ASSERT_EQ(aContents.value(), QByteArray("B's newer version"));
}
