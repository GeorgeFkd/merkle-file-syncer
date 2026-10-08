#pragma once
#include "FileTree.h"

class MerkleTree : public FileTree {
public:
  explicit MerkleTree(const QString& rootNodeName);
  void debug() const override;
  bool addFile(const QString& relativePath,const QDateTime& mtime,const QByteArray& hash);
  bool deleteFile(
      const QString& relativePath,
      const QDateTime &deletedAt) override;
  FileNode *getRoot() const override;
  QByteArray rootHash() const;
  bool verifyHashes() const;
  QList<QPair<QString, QByteArray>> getHashesAtDepth(int depth) const;
  QList<QPair<QString, QByteArray>> getChildHashes(const QString &path) const;
  static TreeDiff
  symmetricHashDiff(const QList<QPair<QString, QByteArray>> &lhs,
                    const QList<QPair<QString, QByteArray>> &rhs);

private:
  QByteArray hashChildren(const FileNode *node) const;
  QByteArray hashTombstoned(const FileNode* node) const;
  void propagateHashDownward(FileNode *node);
  void propagateHashUpward(FileNode *node);
  void recomputeDirHash(FileNode *node);
  void debugNode(const FileNode *node, int depth) const;
  bool verifyNode(const FileNode *node) const;
  void collectHashesAtDepth(const FileNode *node, const QString &path,
                            int targetDepth, int currentDepth,
                            QList<QPair<QString, QByteArray>> &result) const;

};
