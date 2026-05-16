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

template <typename StorageTag> class SyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clientDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_client/" + runId);
    serverDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_server/" + runId);
    QDir().mkpath(clientDir->path());
    QDir().mkpath(serverDir->path());
    fileServer.setFileStorageImpl(StorageTag::makeStorage(serverDir->path()));
    fileServer.listenOn(serverName);
  }

  void TearDown() override {
    if (!HasFailure()) {
      QDir(clientDir->path()).removeRecursively();
      fileServer.getStorage()->cleanup(username);
    }
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

using SyncTestImplementations = ::testing::Types<LocalStorageTag, S3StorageTag>;
TYPED_TEST_SUITE(SyncTest, SyncTestImplementations);

TYPED_TEST(SyncTest, filesAreSynced) {
  auto client = this->makeClient();
  client->getStorage()->writeFile(this->username, "test.txt", "Hello world");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForSync(*client);

  auto contents =
      this->fileServer.getStorage()->readFile(this->username, "test.txt");
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray("Hello world"));
}

TYPED_TEST(SyncTest, serverFileOlderThanClientIsUpdated) {
  auto client = this->makeClient();
  QString filename = "test.txt";
  QDateTime base = QDateTime::currentDateTime();

  this->fileServer.writeFile(this->username, filename, "original",
                             base.addSecs(-10));
  client->getStorage()->writeFile(this->username, filename,
                                  "updated by client");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForSync(*client);

  auto contents =
      this->fileServer.getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray("updated by client"));
}

TYPED_TEST(SyncTest, serverFileNewerThanClientIsRejected) {
  auto client = this->makeClient();
  QString filename = "test.txt";
  QDateTime base = QDateTime::currentDateTime();

  this->fileServer.writeFile(this->username, filename, "server newer version",
                             base.addSecs(10));
  client->getStorage()->writeFile(this->username, filename,
                                  "client older version");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForSync(*client);

  auto serverContents =
      this->fileServer.getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(serverContents.has_value());
  ASSERT_EQ(serverContents.value(), QByteArray("server newer version"));

  auto clientContents =
      client->getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(clientContents.has_value());
  ASSERT_EQ(clientContents.value(), QByteArray("server newer version"));
}

TYPED_TEST(SyncTest, fileInNewDirectoryIsSynced) {
  auto client = this->makeClient();
  client->getStorage()->writeFile(this->username, "subdir/nested/test.txt",
                                  "nested content");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForSync(*client);

  auto contents = this->fileServer.getStorage()->readFile(
      this->username, "subdir/nested/test.txt");
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray("nested content"));
}

TYPED_TEST(SyncTest, deletedFileIsSyncedToServer) {
  auto client = this->makeClient();
  client->getStorage()->writeFile(this->username, "test.txt", "to be deleted");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForSync(*client);

  auto contentsBefore =
      this->fileServer.getStorage()->readFile(this->username, "test.txt");
  ASSERT_TRUE(contentsBefore.has_value());
  ASSERT_EQ(contentsBefore.value(), QByteArray("to be deleted"));

  client->getStorage()->deleteFile(this->username, "test.txt");
  client->clientTick();
  this->waitForSync(*client);

  ASSERT_FALSE(this->fileServer.getStorage()
                   ->readFile(this->username, "test.txt")
                   .has_value());
}

TYPED_TEST(SyncTest, directoryDeleteIsSyncedToServer) {
  auto client = this->makeClient();
  client->getStorage()->writeFile(this->username, "subdir/file1.txt", "file1");
  client->getStorage()->writeFile(this->username, "subdir/file2.txt", "file2");

  client->start();
  QCoreApplication::processEvents();
  client->clientTick();
  this->waitForSync(*client);

  ASSERT_TRUE(this->fileServer.getStorage()
                  ->readFile(this->username, "subdir/file1.txt")
                  .has_value());
  ASSERT_TRUE(this->fileServer.getStorage()
                  ->readFile(this->username, "subdir/file2.txt")
                  .has_value());

  client->getStorage()->deleteFile(this->username, "subdir/file1.txt");
  client->getStorage()->deleteFile(this->username, "subdir/file2.txt");
  client->clientTick();
  this->waitForSync(*client);

  ASSERT_FALSE(this->fileServer.getStorage()
                   ->readFile(this->username, "subdir/file1.txt")
                   .has_value());
  ASSERT_FALSE(this->fileServer.getStorage()
                   ->readFile(this->username, "subdir/file2.txt")
                   .has_value());
}
