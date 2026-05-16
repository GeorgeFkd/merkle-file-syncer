#include "FileHasher.h"
#include "FileTree.h"
#include "FileTreeFactory.h"
#include "LocalFileStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QUuid>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

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

TEST_F(MerkleTreeFixture, getHashesAtDepthReturnsCorrectHashes) {
  storage->writeFile(user, "foo/bar.txt", "hello");
  storage->writeFile(user, "foo/baz.txt", "world");
  storage->writeFile(user, "foo/subdir/nested.txt", "nested");
  storage->writeFile(user, "qux.txt", "qux");

  auto tree = makeTree();

  // depth 0 — root hash only
  auto depth0 = tree->getHashesAtDepth(0);
  ASSERT_EQ(depth0.size(), 1);

  // depth 1 — root's children (foo dir, qux.txt file)
  auto depth1 = tree->getHashesAtDepth(1);
  ASSERT_EQ(depth1.size(), 2);

  // depth 2 — foo's children (bar.txt, baz.txt, subdir dir)
  auto depth2 = tree->getHashesAtDepth(2);
  ASSERT_EQ(depth2.size(), 3);

  // depth 3 — subdir's children (nested.txt)
  auto depth3 = tree->getHashesAtDepth(3);
  ASSERT_EQ(depth3.size(), 1);
  ASSERT_EQ(depth3[0].first, "foo/subdir/nested.txt");
}

TEST_F(MerkleTreeFixture, getChildHashesReturnsCorrectChildren) {
  storage->writeFile(user, "foo/bar.txt", "hello");
  storage->writeFile(user, "foo/baz.txt", "world");
  storage->writeFile(user, "foo/subdir/nested.txt", "nested");
  storage->writeFile(user, "root.txt", "file node");

  auto tree = makeTree();

  // foo has bar.txt, baz.txt and subdir
  auto fooChildren = tree->getChildHashes("foo");
  ASSERT_EQ(fooChildren.size(), 3);

  QSet<QString> paths;
  for (const auto &pair : fooChildren) {
    paths.insert(pair.first);
  }
  ASSERT_TRUE(paths.contains("foo/bar.txt"));
  ASSERT_TRUE(paths.contains("foo/baz.txt"));
  ASSERT_TRUE(paths.contains("foo/subdir"));

  // subdir has only nested.txt
  auto subdirChildren = tree->getChildHashes("foo/subdir");
  ASSERT_EQ(subdirChildren.size(), 1);
  ASSERT_EQ(subdirChildren[0].first, "foo/subdir/nested.txt");
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
