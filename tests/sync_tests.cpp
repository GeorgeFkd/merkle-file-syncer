#include "FileClient.h"
#include "FileHasher.h"
#include "FileServer.h"
#include "FileTree.h"
#include "FileTreeFactory.h"
#include "LocalFileStorage.h"
#include "S3FileStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
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

using SyncTestImplementations = ::testing::Types<LocalStorageTag, S3StorageTag>;

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

    fileServer.configure(
        FileServerConfig{.serverName = serverName,
                         .storage = Tag::makeStorage(serverDir->path())});
    fileServer.start();

    client = std::make_unique<FileClient>();
    client->configure(FileClientConfig{.rootDir = clientDir->path(),
                                       .username = username,
                                       .password = "bar",
                                       .syncStrategy = SyncStrategy::Naive,
                                       .manualTick = true,
                                       .serverName = serverName});
    client->start();
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

  void waitForSync(FileClient &client) {
    QEventLoop loop;
    QObject::connect(&client, &FileClient::syncCompleted, &loop,
                     &QEventLoop::quit);
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
  }

  std::unique_ptr<FileClient> client;
  QDir *clientDir = nullptr;
  QDir *serverDir = nullptr;
  QString serverName = "merkle_sync_test";
  FileServer fileServer;
  QString username = "foo";
};

TYPED_TEST_SUITE(SyncTest, SyncTestImplementations);

TYPED_TEST(SyncTest, singularFileIsSynced) {
  auto filename = "test.txt";
  auto filecontents = "Hello World";
  this->client->writeFile(this->username, filename, filecontents);

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  auto contents =
      this->fileServer.getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray(filecontents));
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

  auto contents =
      this->fileServer.getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray("updated by client"));
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
}

TYPED_TEST(SyncTest, fileInNewDirectoryIsSynced) {
  this->client->writeFile(this->username, "subdir/nested/test.txt",
                          "nested content");

  QCoreApplication::processEvents();
  this->client->clientTick();
  this->waitForSync(*(this->client));

  auto contents = this->fileServer.getStorage()->readFile(
      this->username, "subdir/nested/test.txt");
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray("nested content"));
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

  this->client->deleteFile(this->username, "test.txt");
  this->client->clientTick();
  this->waitForSync(*(this->client));

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

  this->client->deleteFile(this->username, "subdir/file1.txt");
  this->client->deleteFile(this->username, "subdir/file2.txt");
  this->client->clientTick();
  this->waitForSync(*(this->client));

  ASSERT_FALSE(this->fileServer.getStorage()
                   ->readFile(this->username, "subdir/file1.txt")
                   .has_value());
  ASSERT_FALSE(this->fileServer.getStorage()
                   ->readFile(this->username, "subdir/file2.txt")
                   .has_value());
}

template <typename Tag> class MerkleSyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clientDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_merkle_sync_client/" + runId);
    serverDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_merkle_sync_server/" + runId);
    QDir().mkpath(clientDir->path());
    QDir().mkpath(serverDir->path());
    fileServer.configure(FileServerConfig{
        .serverName = serverName,
        .storage = Tag::Storage::makeStorage(serverDir->path())});
    fileServer.start();
  }

  void TearDown() override {
    if (!HasFailure()) {
      QDir(clientDir->path()).removeRecursively();
    }
    fileServer.getStorage()->cleanup(username);

    delete clientDir;
    delete serverDir;
  }

  void waitForNegotiation(FileClient &client) {
    QEventLoop loop;
    QObject::connect(&client, &FileClient::negotiationCompleted, &loop,
                     &QEventLoop::quit);
    QTimer::singleShot(200, &loop, &QEventLoop::quit);
    loop.exec();
  }

  std::unique_ptr<FileClient> makeClient() {
    auto client = std::make_unique<FileClient>();
    client->configure(FileClientConfig{.rootDir = clientDir->path(),
                                       .username = username,
                                       .password = "bar",
                                       .syncStrategy = SyncStrategy::Merkle,
                                       .manualTick = true,
                                       .serverName = serverName});
    return client;
  }

  QDir *clientDir = nullptr;
  QDir *serverDir = nullptr;
  QString serverName = "merkle_sync_test";
  FileServer fileServer;
  QString username = "foo";
};

using MerkleSyncTestImplementations =
    ::testing::Types<LocalMerkleTag, S3MerkleTag>;
TYPED_TEST_SUITE(MerkleSyncTest, MerkleSyncTestImplementations);

TYPED_TEST(MerkleSyncTest, negotiationIdentifiesCorrectDiff) {
  auto client = this->makeClient();
  client->writeFile(this->username, "docs/report.txt", "report");
  client->writeFile(this->username, "docs/notes.txt", "notes");
  client->writeFile(this->username, "docs/draft.txt", "draft v1");
  client->writeFile(this->username, "images/photo.jpg", "photo data");
  client->writeFile(this->username, "readme.txt", "readme");

  this->fileServer.writeFile(this->username, "docs/report.txt", "report",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "docs/draft.txt", "draft v2",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "docs/extra.txt", "extra",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "images/photo.jpg", "photo data",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "readme.txt", "readme",
                             QDateTime::currentDateTime());

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForNegotiation(*client);

  auto result = client->getNegotiationState();
  ASSERT_EQ(result->diffEntries.onlyInLeft.size(), 1);
  ASSERT_EQ(result->diffEntries.onlyInLeft[0].second, "docs/notes.txt");
  ASSERT_EQ(result->diffEntries.onlyInRight.size(), 1);
  ASSERT_EQ(result->diffEntries.onlyInRight[0].second, "docs/extra.txt");
  ASSERT_EQ(result->diffEntries.modified.size(), 1);
  ASSERT_EQ(result->diffEntries.modified[0], "docs/draft.txt");
}

TYPED_TEST(MerkleSyncTest, negotiationIdentifiesDeletedFiles) {
  auto client = this->makeClient();
  client->writeFile(this->username, "docs/report.txt", "report");
  client->writeFile(this->username, "docs/notes.txt", "notes");
  client->writeFile(this->username, "docs/draft.txt", "draft");
  client->writeFile(this->username, "images/photo.jpg", "photo data");
  client->writeFile(this->username, "readme.txt", "readme");

  this->fileServer.writeFile(this->username, "docs/report.txt", "report",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "docs/notes.txt", "notes",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "docs/draft.txt", "draft",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "images/photo.jpg", "photo data",
                             QDateTime::currentDateTime());
  this->fileServer.writeFile(this->username, "readme.txt", "readme",
                             QDateTime::currentDateTime());

  client->deleteFile(this->username, "docs/notes.txt");
  client->deleteFile(this->username, "readme.txt");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForNegotiation(*client);

  auto result = client->getNegotiationState();
  ASSERT_EQ(result->diffEntries.onlyInLeft.size(), 0);
  ASSERT_EQ(result->diffEntries.onlyInRight.size(), 2);
  ASSERT_TRUE(
      result->diffEntries.onlyInRight.contains({true, "docs/notes.txt"}));
  ASSERT_TRUE(result->diffEntries.onlyInRight.contains({true, "readme.txt"}));
  ASSERT_EQ(result->diffEntries.modified.size(), 0);
}

TYPED_TEST(MerkleSyncTest, negotiationWithEmptyServer) {
  auto client = this->makeClient();
  client->writeFile(this->username, "docs/report.txt", "report");
  client->writeFile(this->username, "docs/notes.txt", "notes");
  client->writeFile(this->username, "images/photo.jpg", "photo data");
  client->writeFile(this->username, "images/thumb.jpg", "thumb data");
  client->writeFile(this->username, "readme.txt", "readme");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForNegotiation(*client);

  auto result = client->getNegotiationState();
  ASSERT_EQ(result->diffEntries.onlyInLeft.size(), 3);
  ASSERT_TRUE(result->diffEntries.onlyInLeft.contains({false, "docs"}));
  ASSERT_TRUE(result->diffEntries.onlyInLeft.contains({false, "images"}));
  ASSERT_TRUE(result->diffEntries.onlyInLeft.contains({true, "readme.txt"}));
  ASSERT_EQ(result->diffEntries.onlyInRight.size(), 0);
  ASSERT_EQ(result->diffEntries.modified.size(), 0);
}
