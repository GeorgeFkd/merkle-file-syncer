#include "FileClient.h"
#include "FileHasher.h"
#include "FileServer.h"
#include "FileTree.h"
#include "FileTreeFactory.h"
#include "LocalFileStorage.h"
#include "S3FileStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QLocalServer>
#include <QTimer>
#include <QUuid>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

struct LocalStorageTag {
  static std::unique_ptr<FileStorage> create(const QString &rootPath) {
    auto s = std::make_unique<LocalFileStorage>();
    s->setRoot(rootPath);
    return s;
  }
};

struct S3StorageTag {
  static std::unique_ptr<FileStorage> create(const QString &rootPath) {
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
    fileServer.setFileStorageImpl(StorageTag::create(serverDir->path()));
    fileServer.listenOn(serverName);
    qDebug() << "Server listening:" << fileServer.isListening()
             << fileServer.serverName();
  }

  void TearDown() override {
    if (!HasFailure()) {
      QDir(clientDir->path()).removeRecursively();
      QDir(serverDir->path()).removeRecursively();
      // fileServer.getStorage()->cleanup(username);
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

  QDir *clientDir = nullptr;
  QDir *serverDir = nullptr;
  QString serverName = "merkle_sync_test";
  FileServer fileServer;
  QString username = "foo";
};

using SyncTestImplementations = ::testing::Types<LocalStorageTag, S3StorageTag>;
TYPED_TEST_SUITE(SyncTest, SyncTestImplementations);

TYPED_TEST(SyncTest, filesAreSynced) {
  std::string filecontents("Hello world");

  FileClient client;
  client.init();
  client.setRootDir(this->clientDir->path());
  client.setManualTick();
  client.setPassword("bar");
  client.setUsername(this->username);

  QString filename = "test.txt";
  QFile file(client.getUserRootDirectory(this->username) + "/" + filename);
  file.open(QIODevice::WriteOnly);
  file.write(QByteArray::fromStdString(filecontents));
  file.close();

  client.connectToServer(this->serverName);
  QCoreApplication::processEvents();
  client.clientTick();
  this->waitForSync(client);

  // QFile serverFile(fileServer.getUserRootDirectory(username) + "/" +
  // filename);
  auto contents =
      this->fileServer.getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray::fromStdString(filecontents));
  // ASSERT_TRUE(serverFile.exists());
  // serverFile.open(QIODevice::ReadOnly);
  // ASSERT_EQ(serverFile.readAll(), QByteArray::fromStdString(filecontents));
}

template <typename TreeImplTag>
class FilesystemFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rootDir = QDir(QCoreApplication::applicationDirPath() + "/test_fs/" +
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(rootDir.path());
  }

  void TearDown() override {
    if (!HasFailure()) {
      rootDir.removeRecursively();
    }
  }

  void applyOperations(const QDir &dst, const QString &ops) {
    for (const auto &line : ops.split('\n', Qt::SkipEmptyParts)) {
      auto parts = line.trimmed().split(' ');
      if (parts.size() < 2)
        continue;
      QString relativePath = parts[0];
      QString op = parts.last();
      QString fullPath = dst.path() + "/" + relativePath;
      QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
      if (op == "Write") {
        QString contents = parts.size() > 2 ? parts[1] : "";
        QDir().mkpath(dirPath);
        QFile f(fullPath);
        if (!f.open(QIODevice::WriteOnly)) {
          qDebug() << "Failed to open file:" << fullPath << f.errorString();
          return;
        }
        f.write(contents.toUtf8());
        f.close();
      } else if (op == "Delete") {
        QFile::remove(fullPath);
      }
    }
  }

  std::unique_ptr<FileTree> makeTree(const QDir &dir) {
    return FileTreeFactory<TreeImplTag::type>::create(dir.path().toStdString());
  }

  QDir rootDir;
};

using TreeImplementations = ::testing::Types<VanillaTreeTag, MerkleTreeTagV1>;

TYPED_TEST_SUITE(FilesystemFixture, TreeImplementations);

TYPED_TEST(FilesystemFixture, buildFromDiscoversFilesCorrectly) {
  this->applyOperations(this->rootDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");

  auto tree = this->makeTree(this->rootDir);
  tree->build();
  tree->debug();
  ASSERT_EQ(tree->fileCount(), 3);
  ASSERT_TRUE(tree->contains("foo/bar.txt"));
  ASSERT_TRUE(tree->contains("foo/baz.txt"));
  ASSERT_TRUE(tree->contains("foo/subdir/nested.txt"));
  ASSERT_TRUE(tree->contains("foo"));
  ASSERT_FALSE(tree->contains("fool"));
  ASSERT_FALSE(tree->contains("foo/new.txt"));
}

TYPED_TEST(FilesystemFixture, addFileBehaviorWorksAsExpected) {
  this->applyOperations(this->rootDir, R"(
        foo/bar.txt hello Write
    )");

  auto tree = this->makeTree(this->rootDir);
  tree->build();

  ASSERT_TRUE(tree->addFile("foo/new.txt"));
  ASSERT_TRUE(tree->contains("foo/new.txt"));
  ASSERT_FALSE(QFile::exists(this->rootDir.path() + "/foo/new.txt"));
}

template <typename TreeImplTag>
class FilesystemDiffFixture : public ::testing::Test {
protected:
  void SetUp() override {
    leftDir = QDir(QCoreApplication::applicationDirPath() + "/test_diff_left/" +
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    rightDir =
        QDir(QCoreApplication::applicationDirPath() + "/test_diff_right/" +
             QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(leftDir.path());
    QDir().mkpath(rightDir.path());
  }

  void TearDown() override {
    if (!HasFailure()) {
      leftDir.removeRecursively();
      rightDir.removeRecursively();
    }
  }

  void applyOperations(const QDir &dst, const QString &ops) {
    for (const auto &line : ops.split('\n', Qt::SkipEmptyParts)) {
      auto parts = line.trimmed().split(' ');
      if (parts.size() < 2)
        continue;
      QString fullPath = dst.path() + "/" + parts[0];
      QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
      QString op = parts.last();
      if (op == "Write") {
        QString contents = parts.size() > 2 ? parts[1] : "";
        QDir().mkpath(dirPath);
        QFile f(fullPath);
        if (!f.open(QIODevice::WriteOnly)) {
          qDebug() << "Failed to open file:" << fullPath << f.errorString();
          return;
        }
        f.write(contents.toUtf8());
        f.close();
      } else if (op == "Delete") {
        QFile::remove(fullPath);
      }
    }
  }

  std::unique_ptr<FileTree> makeTree(const QDir &dir) {
    return FileTreeFactory<TreeImplTag::type>::create(dir.path().toStdString());
  }

  QDir leftDir;
  QDir rightDir;
};

TYPED_TEST_SUITE(FilesystemDiffFixture, TreeImplementations);

TYPED_TEST(FilesystemDiffFixture, diffIdentifiesChanges) {
  this->applyOperations(this->leftDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");

  this->applyOperations(this->rightDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt changed Write
        foo/subdir/new.txt newfile Write
    )");

  auto leftTree = this->makeTree(this->leftDir);
  auto rightTree = this->makeTree(this->rightDir);
  leftTree->build();
  rightTree->build();

  auto diff = leftTree->diff(*rightTree);

  ASSERT_EQ(diff.modified.size(), 1);
  ASSERT_EQ(diff.modified[0], "foo/baz.txt");
  ASSERT_EQ(diff.onlyInLeft.size(), 1);
  ASSERT_EQ(diff.onlyInLeft[0], "foo/subdir/nested.txt");
  ASSERT_EQ(diff.onlyInRight.size(), 1);
  ASSERT_EQ(diff.onlyInRight[0], "foo/subdir/new.txt");
}

TYPED_TEST(FilesystemDiffFixture, addFileWithWriteReflectsInDiff) {
  this->applyOperations(this->leftDir, R"(
        foo/bar.txt hello Write
    )");

  this->applyOperations(this->rightDir, R"(
        foo/bar.txt hello Write
    )");

  auto leftTree = this->makeTree(this->leftDir);
  auto rightTree = this->makeTree(this->rightDir);
  leftTree->build();
  rightTree->build();

  auto diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInLeft.size(), 0);
  ASSERT_EQ(diff.onlyInRight.size(), 0);
  ASSERT_EQ(diff.modified.size(), 0);
  QFile newFile(this->leftDir.path() + "/foo/new.txt");
  newFile.open(QIODevice::WriteOnly);
  newFile.write("new content");
  newFile.close();
  leftTree->addFile("foo/new.txt");
  leftTree->addFile("foo/new.txt");

  diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInLeft.size(), 1);
  ASSERT_EQ(diff.onlyInLeft[0], "foo/new.txt");
}

TYPED_TEST(FilesystemDiffFixture, deleteFileReflectsInDiff) {
  this->applyOperations(this->leftDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");
  this->applyOperations(this->rightDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");
  auto leftTree = this->makeTree(this->leftDir);
  auto rightTree = this->makeTree(this->rightDir);
  leftTree->build();
  rightTree->build();

  auto diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInLeft.size(), 0);
  ASSERT_EQ(diff.onlyInRight.size(), 0);
  ASSERT_EQ(diff.modified.size(), 0);

  // delete a file
  ASSERT_TRUE(leftTree->deleteFile("foo/baz.txt"));
  ASSERT_FALSE(leftTree->contains("foo/baz.txt"));

  diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInRight.size(), 1);
  ASSERT_EQ(diff.onlyInRight[0], "foo/baz.txt");

  // delete a directory
  ASSERT_TRUE(leftTree->deleteFile("foo/subdir"));
  ASSERT_FALSE(leftTree->contains("foo/subdir"));
  ASSERT_FALSE(leftTree->contains("foo/subdir/nested.txt"));

  diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInRight.size(), 2);
}

class MerkleTreeFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rootDir = QDir(QCoreApplication::applicationDirPath() + "/test_merkle/" +
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(rootDir.path());
  }

  void TearDown() override {
    if (!HasFailure()) {
      rootDir.removeRecursively();
    }
  }

  void applyOperations(const QDir &dst, const QString &ops) {
    for (const auto &line : ops.split('\n', Qt::SkipEmptyParts)) {
      auto parts = line.trimmed().split(' ');
      if (parts.size() < 2)
        continue;
      QString fullPath = dst.path() + "/" + parts[0];
      QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
      QString op = parts.last();
      if (op == "Write") {
        QString contents = parts.size() > 2 ? parts[1] : "";
        QDir().mkpath(dirPath);
        QFile f(fullPath);
        if (!f.open(QIODevice::WriteOnly))
          continue;
        f.write(contents.toUtf8());
        f.close();
      } else if (op == "Delete") {
        QFile::remove(fullPath);
      }
    }
  }

  QDir rootDir;
};

TEST_F(MerkleTreeFixture, rootHashChangesOnAddFile) {
  applyOperations(rootDir, R"(
        foo/bar.txt hello Write
    )");

  MerkleTree tree(rootDir.path().toStdString());
  tree.build();
  ASSERT_TRUE(tree.verifyHashes());

  auto hashBefore = tree.rootHash().toHex();
  qDebug() << "rootHash before: " << hashBefore;
  QFile f(rootDir.path() + "/foo/new.txt");
  f.open(QIODevice::WriteOnly);
  f.write("new content");
  f.close();
  tree.addFile("foo/new.txt");
  auto hashAfter = tree.rootHash().toHex();
  qDebug() << "rootHash after: " << hashAfter;

  ASSERT_NE(hashBefore, hashAfter);
  ASSERT_TRUE(tree.verifyHashes());
}

TEST_F(MerkleTreeFixture, rootHashChangesOnDeleteFile) {
  applyOperations(rootDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
    )");

  MerkleTree tree(rootDir.path().toStdString());
  tree.build();
  ASSERT_TRUE(tree.verifyHashes());

  auto hashBefore = tree.rootHash();
  tree.deleteFile("foo/baz.txt");
  auto hashAfter = tree.rootHash();

  ASSERT_NE(hashBefore, hashAfter);
  ASSERT_FALSE(tree.contains("foo/baz.txt"));
  ASSERT_TRUE(tree.verifyHashes());
}

TEST_F(MerkleTreeFixture, rootHashChangesOnDeleteDirectory) {
  applyOperations(rootDir, R"(
        foo/bar.txt hello Write
        foo/subdir/nested.txt nested Write
    )");

  MerkleTree tree(rootDir.path().toStdString());
  tree.build();
  ASSERT_TRUE(tree.verifyHashes());

  auto hashBefore = tree.rootHash();
  tree.deleteFile("foo/subdir");
  auto hashAfter = tree.rootHash();

  ASSERT_NE(hashBefore, hashAfter);
  ASSERT_FALSE(tree.contains("foo/subdir"));
  ASSERT_FALSE(tree.contains("foo/subdir/nested.txt"));
  ASSERT_TRUE(tree.verifyHashes());
}

struct LocalHasherTag {
  static std::shared_ptr<FileStorage> makeStorage(const QString &rootPath) {
    auto s = std::make_shared<LocalFileStorage>();
    s->setRoot(rootPath);
    return s;
  }
  static FileHasher create(std::shared_ptr<FileStorage> storage,
                           const QString &user) {
    return FileHasher(storage.get(), user);
  }
};

struct S3HasherTag {
  static std::shared_ptr<FileStorage> makeStorage(const QString &rootPath) {
    auto s = std::make_shared<S3FileStorage>();
    s->init(S3Config{.endpoint = "localhost:9000",
                     .accessKey = "minioadmin",
                     .secretKey = "minioadmin",
                     .bucket = "test-bucket",
                     .useSSL = false});
    return s;
  }
  static FileHasher create(std::shared_ptr<FileStorage> storage,
                           const QString &user) {
    return FileHasher(storage.get(), user);
  }
};

template <typename HasherTag>
class MerkleHasherFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rootDir =
        QDir(QCoreApplication::applicationDirPath() + "/test_merkle_hasher/" +
             QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(rootDir.path());
    storageImpl = HasherTag::makeStorage(rootDir.path());
    hasher = HasherTag::create(storageImpl, user);
  }

  void TearDown() override {
    if (!HasFailure()) {
      rootDir.removeRecursively();
      storageImpl->cleanup(user);
    }
  }

  QDir rootDir;
  std::shared_ptr<FileStorage> storageImpl;
  FileHasher hasher;
  QString user = "testuser";
};

// architecturally i cant support yet testing the S3HasherTag
using HasherImplementations = ::testing::Types<LocalHasherTag>;

TYPED_TEST_SUITE(MerkleHasherFixture, HasherImplementations);

TYPED_TEST(MerkleHasherFixture, hasherWorksCorrectly) {
  this->storageImpl->writeFile(this->user, "foo/bar.txt", "hello");
  this->storageImpl->writeFile(this->user, "foo/baz.txt", "world");

  auto localStorage =
      std::dynamic_pointer_cast<LocalFileStorage>(this->storageImpl);
  QString treePath =
      localStorage ? localStorage->rootPath(this->user) : this->rootDir.path();

  MerkleTree tree(treePath.toStdString());
  tree.setHasher(this->hasher);
  tree.build();

  ASSERT_TRUE(tree.verifyHashes());
  ASSERT_EQ(tree.fileCount(), 2);

  auto hashBefore = tree.rootHash();
  this->storageImpl->writeFile(this->user, "foo/new.txt", "newcontent");
  tree.addFile("foo/new.txt");
  ASSERT_NE(hashBefore, tree.rootHash());
  ASSERT_TRUE(tree.verifyHashes());
}
