#include "MerkleTree.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <gtest/gtest.h>

namespace {

QByteArray defaultHash(const QString &content) {
  return QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256);
}

QDateTime now() { return QDateTime::currentDateTime(); }

} // namespace

TEST(MerkleTree, buildFromAddFileDiscoversFilesCorrectly) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));
  tree.addFile("foo/baz.txt", now(), defaultHash("world"));
  tree.addFile("foo/subdir/nested.txt", now(), defaultHash("nested"));

  ASSERT_EQ(tree.fileCount(), 3);

  for (const auto &path :
       {"foo/bar.txt", "foo/baz.txt", "foo/subdir/nested.txt", "foo"}) {
    auto found = tree.find(path);
    ASSERT_TRUE(found.has_value());
    auto &[node, isTombstoned] = *found;
    ASSERT_FALSE(isTombstoned);
  }

  ASSERT_FALSE(tree.find("fool").has_value());
  ASSERT_FALSE(tree.find("foo/new.txt").has_value());
}

TEST(MerkleTree, addFileBehaviorWorksAsExpected) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));

  ASSERT_TRUE(tree.addFile("foo/new.txt", now(), defaultHash("new content")));

  auto found = tree.find("foo/new.txt");
  ASSERT_TRUE(found.has_value());
  auto &[node, isTombstoned] = *found;
  ASSERT_FALSE(isTombstoned);
}

TEST(MerkleTree, rootHashChangesOnAddFile) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));
  ASSERT_TRUE(tree.verifyHashes());

  auto hashBefore = tree.rootHash();
  tree.addFile("foo/new.txt", now(), defaultHash("new content"));

  ASSERT_NE(hashBefore, tree.rootHash());
  ASSERT_TRUE(tree.verifyHashes());
}

TEST(MerkleTree, rootHashChangesOnDeleteFile) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));
  tree.addFile("foo/baz.txt", now(), defaultHash("world"));
  ASSERT_TRUE(tree.verifyHashes());

  auto hashBefore = tree.rootHash();
  tree.deleteFile("foo/baz.txt", now());

  ASSERT_NE(hashBefore, tree.rootHash());
  auto found = tree.find("foo/baz.txt");
  ASSERT_TRUE(found.has_value());
  auto &[node, isTombstoned] = *found;
  ASSERT_TRUE(isTombstoned);
  ASSERT_TRUE(tree.verifyHashes());
}

TEST(MerkleTree, rootHashChangesOnDeleteDirectory) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));
  tree.addFile("foo/subdir/nested.txt", now(), defaultHash("nested"));
  ASSERT_TRUE(tree.verifyHashes());

  auto hashBefore = tree.rootHash();
  tree.deleteFile("foo/subdir", now());

  ASSERT_NE(hashBefore, tree.rootHash());

  auto foundDir = tree.find("foo/subdir");
  ASSERT_TRUE(foundDir.has_value());
  auto &[dirNode, isDirTombstoned] = *foundDir;
  ASSERT_TRUE(isDirTombstoned);

  auto foundFile = tree.find("foo/subdir/nested.txt");
  ASSERT_TRUE(foundFile.has_value());
  auto &[fileNode, isFileTombstoned] = *foundFile;
  ASSERT_TRUE(isFileTombstoned);
  ASSERT_TRUE(tree.verifyHashes());
}

TEST(MerkleTree, getHashesAtDepthReturnsCorrectHashes) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));
  tree.addFile("foo/baz.txt", now(), defaultHash("world"));
  tree.addFile("foo/subdir/nested.txt", now(), defaultHash("nested"));
  tree.addFile("qux.txt", now(), defaultHash("qux"));

  ASSERT_EQ(tree.getHashesAtDepth(0).size(), 1);
  ASSERT_EQ(tree.getHashesAtDepth(1).size(), 2);
  ASSERT_EQ(tree.getHashesAtDepth(2).size(), 3);

  auto depth3 = tree.getHashesAtDepth(3);
  ASSERT_EQ(depth3.size(), 1);
  ASSERT_EQ(depth3[0].first, "foo/subdir/nested.txt");
}

TEST(MerkleTree, getChildHashesReturnsCorrectChildren) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));
  tree.addFile("foo/baz.txt", now(), defaultHash("world"));
  tree.addFile("foo/subdir/nested.txt", now(), defaultHash("nested"));
  tree.addFile("root.txt", now(), defaultHash("file node"));

  auto fooChildren = tree.getChildHashes("foo");
  ASSERT_EQ(fooChildren.size(), 3);

  QSet<QString> paths;
  for (const auto &pair : fooChildren) {
    paths.insert(pair.first);
  }
  ASSERT_TRUE(paths.contains("foo/bar.txt"));
  ASSERT_TRUE(paths.contains("foo/baz.txt"));
  ASSERT_TRUE(paths.contains("foo/subdir"));

  auto subdirChildren = tree.getChildHashes("foo/subdir");
  ASSERT_EQ(subdirChildren.size(), 1);
  ASSERT_EQ(subdirChildren[0].first, "foo/subdir/nested.txt");
}

TEST(MerkleTree, addFileResurrectsTombstonedNode) {
  MerkleTree tree("foo");
  tree.addFile("foo/bar.txt", now(), defaultHash("hello"));

  tree.deleteFile("foo/bar.txt", now());
  {
    auto found = tree.find("foo/bar.txt");
    ASSERT_TRUE(found.has_value());
    auto &[node, isTombstoned] = *found;
    ASSERT_TRUE(isTombstoned);
  }

  tree.addFile("foo/bar.txt", now(), defaultHash("hello again"));
  auto found = tree.find("foo/bar.txt");
  ASSERT_TRUE(found.has_value());
  auto &[node, isTombstoned] = *found;
  ASSERT_FALSE(isTombstoned);
}
