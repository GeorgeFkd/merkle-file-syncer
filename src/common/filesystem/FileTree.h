#pragma once
#include "FileStorage.h"
#include <QByteArray>
#include <QList>
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
  FileNode *parent = nullptr;
  std::vector<std::unique_ptr<FileNode>> children;
};

class FileTree {
public:
  virtual ~FileTree() = default;
  bool contains(const std::string &relativePath) const;
  virtual bool addFile(const std::string &relativePath) = 0;
  virtual bool deleteFile(const std::string &relativePath) = 0;
  int fileCount() const;
  virtual TreeDiff diff(const FileTree &other) const = 0;
  virtual void debug() const = 0;
  virtual QString getRootPath() const = 0;
  virtual FileNode *getRoot() const = 0;
  void buildFromStorage(const FileStorage *storage, const QString &username);
  std::optional<FileNode *> find(const std::string &relativePath) const;

protected:
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
