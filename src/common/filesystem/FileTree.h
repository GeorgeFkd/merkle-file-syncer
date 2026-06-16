#pragma once
#include "FileStorage.h"
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <memory>
#include <optional>
#include <string>
#include <vector>
enum class FileType { File, Directory };

class TreeDiff {
public:
  QList<QString> onlyInLeft;
  QList<QString> onlyInRight;
  QList<QString> modified;
};

class FileNode {
public:
  FileType type;
  QString path;
  QByteArray hash;
  // tombstone
  bool isDeleted = false;
  QDateTime deletedAt;
  QDateTime mtime;
  FileNode *parent = nullptr;
  std::vector<std::unique_ptr<FileNode>> children;
};
class FileTree {
public:
  virtual ~FileTree() = default;
  virtual bool
  addFile(const std::string &relativePath,
          const QDateTime &mtime = QDateTime::currentDateTime()) = 0;
  virtual bool
  deleteFile(const std::string &relativePath,
             const QDateTime &deletedAt = QDateTime::currentDateTime()) = 0;
  int fileCount() const;
  //TODO: remove things related to this diff(tests etc. etc.)
  virtual TreeDiff diff(const FileTree &other) const = 0;
  virtual void debug() const = 0;
  virtual QString getRootPath() const = 0;
  virtual FileNode *getRoot() const = 0;
  //TODO: remove this, trees should not depend on storage
  void buildFromStorage(const FileStorage *storage, const QString &username);
  std::optional<std::tuple<FileNode *, bool>>
  find(const std::string &relativePath) const;

protected:
  void markTombstoneRecursively(FileNode *node, const QDateTime &deletedAt);
  std::optional<FileNode *> findNode(const std::string &relativePath,
                                     FileNode *root) const;
  int countFileNodes(FileNode *node) const;
  void collectAllFiles(const FileNode *node, const QString &path,
                       QList<QString> &files) const;
  QString getRelativePath(const FileNode *node) const;
  std::unique_ptr<FileNode> root;
  virtual void afterBuild() {};
};

enum class TreeType { Vanilla, Merkle };

struct VanillaTreeTag {
  static constexpr TreeType type = TreeType::Vanilla;
  static constexpr const char *name = "vanilla";
  static constexpr const char *version = "1.0";
};

struct MerkleTreeTagV1 {
  static constexpr TreeType type = TreeType::Merkle;
  static constexpr const char *name = "merkle";
  static constexpr const char *version = "1.0";
};
