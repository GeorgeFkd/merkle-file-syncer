#pragma once
#include "FileTree.h"

class MerkleTree : public FileTree {
public:
  explicit MerkleTree(const std::string &rootDir);
  void debug() const override;
  bool addFile(const std::string &relativePath) override;
  bool deleteFile(const std::string &relativePath) override;
  TreeDiff diff(const FileTree &other) const override;
  QString getRootPath() const override;
  FileNode *getRoot() const override;
  QByteArray rootHash() const;
  bool verifyHashes() const;
  void setHasher(std::function<QByteArray(const QString &)> hasher);
  QList<QPair<QString, QByteArray>> getHashesAtDepth(int depth) const;
  QList<QPair<QString, QByteArray>> getChildHashes(const QString &path) const;

private:
  QByteArray hashFile(const QString &relativePath) const;
  QByteArray hashChildren(const FileNode *node) const;
  QByteArray readFileContents(const FileNode *node) const;
  void computeHashes(FileNode *node);
  void propagateHash(FileNode *node);
  void recomputeDirHash(FileNode *node);
  void diffNodes(const FileNode *left, const QString &leftRootPath,
                 const FileNode *right, const QString &rightRootPath,
                 const QString &path, TreeDiff &result) const;
  void debugNode(const FileNode *node, int depth) const;
  bool verifyNode(const FileNode *node) const;
  void collectHashesAtDepth(const FileNode *node, const QString &path,
                              int targetDepth, int currentDepth,
                              QList<QPair<QString, QByteArray>> &result) const;

  QString rootPath;
  std::function<QByteArray(const QString &)> hasher;
  void afterBuild() override;
};
