#pragma once
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
  FileTree() = default;
  FileTree(FileTree&&) = default;
  FileTree &operator=(FileTree &&) = default;
  // virtual ~FileTree() = default;
  virtual bool
  deleteFile(const QString& relativePath,
             const QDateTime &deletedAt = QDateTime::currentDateTime()) = 0;
  int fileCount() const;
  virtual void debug() const = 0;
  virtual FileNode *getRoot() const = 0;
  std::optional<std::tuple<FileNode *, bool>>
  find(const QString& relativePath) const;

protected:
  void markTombstoneRecursively(FileNode *node, const QDateTime &deletedAt);
  int countFileNodes(FileNode *node) const;
  void collectAllFiles(const FileNode *node, const QString &path,
                       QList<QString> &files) const;
  QString getRelativePath(const FileNode *node) const;
  std::unique_ptr<FileNode> root;
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
