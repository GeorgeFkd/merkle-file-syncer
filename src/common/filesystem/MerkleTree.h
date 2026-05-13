#pragma once
#include "FileTree.h"

class MerkleTree : public FileTree {
public:
  explicit MerkleTree(const std::string &rootDir);
  void build() override;
  void debug() const override;
  bool addFile(const std::string &relativePath) override;
  TreeDiff diff(const FileTree &other) const override;
  QString getRootPath() const override;
  FileNode *getRoot() const override;
  QByteArray rootHash() const;

private:
  QByteArray readFileContents(const FileNode *node) const;
  void computeHashes(FileNode *node);
  void propagateHash(FileNode *node);
  void recomputeDirHash(FileNode *node);
  void diffNodes(const FileNode *left, const QString &leftRootPath,
                 const FileNode *right, const QString &rightRootPath,
                 const QString &path, TreeDiff &result) const;
  void debugNode(const FileNode *node, int depth) const;

  std::unique_ptr<FileNode> root;
  QString rootPath;
};
