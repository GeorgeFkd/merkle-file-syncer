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

// ─── Shared utilities ────────────────────────────────────────────────────────

class TestOperations {
public:
  static void applyOperations(FileStorage *storage, const QString &user,
                              const QString &ops) {
    for (const auto &line : ops.split('\n', Qt::SkipEmptyParts)) {
      auto parts = line.trimmed().split(' ');
      if (parts.size() < 2)
        continue;
      QString path = parts[0];
      QString op = parts.last();
      if (op == "Write") {
        QString contents = parts.size() > 2 ? parts[1] : "";
        storage->writeFile(user, path, contents.toUtf8());
      } else if (op == "Delete") {
        storage->deleteFile(user, path);
      }
    }
  }
};

// ─── Storage tags ────────────────────────────────────────────────────────────

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

// ─── SyncTest ────────────────────────────────────────────────────────────────

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

  auto contents =
      this->fileServer.getStorage()->readFile(this->username, filename);
  ASSERT_TRUE(contents.has_value());
  ASSERT_EQ(contents.value(), QByteArray::fromStdString(filecontents));
}

// ─── FilesystemFixture ───────────────────────────────────────────────────────

template <typename TreeImplTag>
class FilesystemFixture : public ::testing::Test {
protected:
  void SetUp() override {
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rootDir =
        QDir(QCoreApplication::applicationDirPath() + "/test_fs/" + runId);
    QDir().mkpath(rootDir.path());
    storage = std::make_shared<LocalFileStorage>();
    storage->setRoot(rootDir.path());
  }

  void TearDown() override {
    if (!HasFailure()) {
      rootDir.removeRecursively();
      storage->cleanup(user);
    }
  }

  void applyOperations(const QString &ops) {
    TestOperations::applyOperations(storage.get(), user, ops);
  }

  std::unique_ptr<FileTree> makeTree() {
    auto tree = FileTreeFactory<TreeImplTag::type>::create(
        storage->rootPath(user).toStdString());
    tree->buildFromStorage(storage.get(), user);
    return tree;
  }

  QDir rootDir;
  std::shared_ptr<LocalFileStorage> storage;
  QString user = "testuser";
};

using TreeImplementations = ::testing::Types<VanillaTreeTag, MerkleTreeTagV1>;
TYPED_TEST_SUITE(FilesystemFixture, TreeImplementations);

TYPED_TEST(FilesystemFixture, buildFromDiscoversFilesCorrectly) {
  this->applyOperations(R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");

  auto tree = this->makeTree();
  ASSERT_EQ(tree->fileCount(), 3);
  ASSERT_TRUE(tree->contains("foo/bar.txt"));
  ASSERT_TRUE(tree->contains("foo/baz.txt"));
  ASSERT_TRUE(tree->contains("foo/subdir/nested.txt"));
  ASSERT_TRUE(tree->contains("foo"));
  ASSERT_FALSE(tree->contains("fool"));
  ASSERT_FALSE(tree->contains("foo/new.txt"));
}

TYPED_TEST(FilesystemFixture, addFileBehaviorWorksAsExpected) {
  this->applyOperations(R"(
        foo/bar.txt hello Write
    )");

  auto tree = this->makeTree();
  this->storage->writeFile(this->user, "foo/new.txt", "new content");
  ASSERT_TRUE(tree->addFile("foo/new.txt"));
  ASSERT_TRUE(tree->contains("foo/new.txt"));
}

// ─── FilesystemDiffFixture ───────────────────────────────────────────────────

template <typename TreeImplTag>
class FilesystemDiffFixture : public ::testing::Test {
protected:
  void SetUp() override {
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    leftStorage = std::make_shared<LocalFileStorage>();
    leftStorage->setRoot(QCoreApplication::applicationDirPath() +
                         "/test_diff_left/" + runId);
    rightStorage = std::make_shared<LocalFileStorage>();
    rightStorage->setRoot(QCoreApplication::applicationDirPath() +
                          "/test_diff_right/" + runId);
  }

  void TearDown() override {
    if (!HasFailure()) {
      leftStorage->cleanup(user);
      rightStorage->cleanup(user);
    }
  }

  void applyLeft(const QString &ops) {
    TestOperations::applyOperations(leftStorage.get(), user, ops);
  }

  void applyRight(const QString &ops) {
    TestOperations::applyOperations(rightStorage.get(), user, ops);
  }

  std::unique_ptr<FileTree> makeLeftTree() {
    auto tree = FileTreeFactory<TreeImplTag::type>::create(
        leftStorage->rootPath(user).toStdString());
    tree->buildFromStorage(leftStorage.get(), user);
    return tree;
  }

  std::unique_ptr<FileTree> makeRightTree() {
    auto tree = FileTreeFactory<TreeImplTag::type>::create(
        rightStorage->rootPath(user).toStdString());
    tree->buildFromStorage(rightStorage.get(), user);
    return tree;
  }

  std::shared_ptr<LocalFileStorage> leftStorage;
  std::shared_ptr<LocalFileStorage> rightStorage;
  QString user = "testuser";
};

TYPED_TEST_SUITE(FilesystemDiffFixture, TreeImplementations);

TYPED_TEST(FilesystemDiffFixture, diffIdentifiesChanges) {
  this->applyLeft(R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");
  this->applyRight(R"(
        foo/bar.txt hello Write
        foo/baz.txt changed Write
        foo/subdir/new.txt newfile Write
    )");

  auto leftTree = this->makeLeftTree();
  auto rightTree = this->makeRightTree();
  auto diff = leftTree->diff(*rightTree);

  ASSERT_EQ(diff.modified.size(), 1);
  ASSERT_EQ(diff.modified[0], "foo/baz.txt");
  ASSERT_EQ(diff.onlyInLeft.size(), 1);
  ASSERT_EQ(diff.onlyInLeft[0], "foo/subdir/nested.txt");
  ASSERT_EQ(diff.onlyInRight.size(), 1);
  ASSERT_EQ(diff.onlyInRight[0], "foo/subdir/new.txt");
}

TYPED_TEST(FilesystemDiffFixture, addFileWithWriteReflectsInDiff) {
  this->applyLeft(R"(foo/bar.txt hello Write)");
  this->applyRight(R"(foo/bar.txt hello Write)");

  auto leftTree = this->makeLeftTree();
  auto rightTree = this->makeRightTree();

  auto diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInLeft.size(), 0);
  ASSERT_EQ(diff.onlyInRight.size(), 0);
  ASSERT_EQ(diff.modified.size(), 0);

  this->leftStorage->writeFile(this->user, "foo/new.txt", "new content");
  leftTree->addFile("foo/new.txt");

  diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInLeft.size(), 1);
  ASSERT_EQ(diff.onlyInLeft[0], "foo/new.txt");
}

TYPED_TEST(FilesystemDiffFixture, deleteFileReflectsInDiff) {
  this->applyLeft(R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");
  this->applyRight(R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");

  auto leftTree = this->makeLeftTree();
  auto rightTree = this->makeRightTree();

  ASSERT_TRUE(leftTree->deleteFile("foo/baz.txt"));
  ASSERT_FALSE(leftTree->contains("foo/baz.txt"));

  auto diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInRight.size(), 1);
  ASSERT_EQ(diff.onlyInRight[0], "foo/baz.txt");

  ASSERT_TRUE(leftTree->deleteFile("foo/subdir"));
  ASSERT_FALSE(leftTree->contains("foo/subdir"));
  ASSERT_FALSE(leftTree->contains("foo/subdir/nested.txt"));

  diff = leftTree->diff(*rightTree);
  ASSERT_EQ(diff.onlyInRight.size(), 2);
}

// ─── MerkleTreeFixture ───────────────────────────────────────────────────────

class MerkleTreeFixture : public ::testing::Test {
protected:
  void SetUp() override {
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    storage = std::make_shared<LocalFileStorage>();
    storage->setRoot(QCoreApplication::applicationDirPath() + "/test_merkle/" +
                     runId);
  }

  void TearDown() override {
    if (!HasFailure()) {
      storage->cleanup(user);
    }
  }

  std::unique_ptr<MerkleTree> makeTree() {
    auto tree =
        std::make_unique<MerkleTree>(storage->rootPath(user).toStdString());
    tree->buildFromStorage(storage.get(), user);
    return tree;
  }

  std::shared_ptr<LocalFileStorage> storage;
  QString user = "testuser";
};

TEST_F(MerkleTreeFixture, rootHashChangesOnAddFile) {
  storage->writeFile(user, "foo/bar.txt", "hello");
  auto tree = makeTree();
  ASSERT_TRUE(tree->verifyHashes());

  auto hashBefore = tree->rootHash();
  storage->writeFile(user, "foo/new.txt", "new content");
  tree->addFile("foo/new.txt");

  ASSERT_NE(hashBefore, tree->rootHash());
  ASSERT_TRUE(tree->verifyHashes());
}

TEST_F(MerkleTreeFixture, rootHashChangesOnDeleteFile) {
  storage->writeFile(user, "foo/bar.txt", "hello");
  storage->writeFile(user, "foo/baz.txt", "world");
  auto tree = makeTree();
  ASSERT_TRUE(tree->verifyHashes());

  auto hashBefore = tree->rootHash();
  tree->deleteFile("foo/baz.txt");

  ASSERT_NE(hashBefore, tree->rootHash());
  ASSERT_FALSE(tree->contains("foo/baz.txt"));
  ASSERT_TRUE(tree->verifyHashes());
}

TEST_F(MerkleTreeFixture, rootHashChangesOnDeleteDirectory) {
  storage->writeFile(user, "foo/bar.txt", "hello");
  storage->writeFile(user, "foo/subdir/nested.txt", "nested");
  auto tree = makeTree();
  ASSERT_TRUE(tree->verifyHashes());

  auto hashBefore = tree->rootHash();
  tree->deleteFile("foo/subdir");

  ASSERT_NE(hashBefore, tree->rootHash());
  ASSERT_FALSE(tree->contains("foo/subdir"));
  ASSERT_FALSE(tree->contains("foo/subdir/nested.txt"));
  ASSERT_TRUE(tree->verifyHashes());
}

// ─── MerkleHasherFixture ─────────────────────────────────────────────────────

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
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    storageImpl =
        HasherTag::makeStorage(QCoreApplication::applicationDirPath() +
                               "/test_merkle_hasher/" + runId);
    hasher = HasherTag::create(storageImpl, user);
  }

  void TearDown() override {
    if (!HasFailure()) {
      storageImpl->cleanup(user);
    }
  }

  std::shared_ptr<FileStorage> storageImpl;
  FileHasher hasher;
  QString user = "testuser";
};

using HasherImplementations = ::testing::Types<LocalHasherTag>;
TYPED_TEST_SUITE(MerkleHasherFixture, HasherImplementations);

TYPED_TEST(MerkleHasherFixture, hasherWorksCorrectly) {
  this->storageImpl->writeFile(this->user, "foo/bar.txt", "hello");
  this->storageImpl->writeFile(this->user, "foo/baz.txt", "world");

  auto localStorage =
      std::dynamic_pointer_cast<LocalFileStorage>(this->storageImpl);
  QString treePath = localStorage->rootPath(this->user);

  MerkleTree tree(treePath.toStdString());
  tree.setHasher(this->hasher);
  tree.buildFromStorage(this->storageImpl.get(), this->user);

  ASSERT_TRUE(tree.verifyHashes());
  ASSERT_EQ(tree.fileCount(), 2);

  auto hashBefore = tree.rootHash();
  this->storageImpl->writeFile(this->user, "foo/new.txt", "newcontent");
  tree.addFile("foo/new.txt");
  ASSERT_NE(hashBefore, tree.rootHash());
  ASSERT_TRUE(tree.verifyHashes());
}
