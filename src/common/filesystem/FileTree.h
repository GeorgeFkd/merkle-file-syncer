#include <QList>
#include <QString>
#include <array>
#include <memory>
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
  // will not use it right now
  std::array<uint8_t, 32> hash;
  std::vector<std::unique_ptr<FileNode>> children;
};

class FilesystemTree {
public:
  static FilesystemTree buildFrom(const std::string &rootDir);
  int fileCount() const;
  void debug() const;
  std::optional<FileNode *> find(const std::string &relativePath) const;
  bool contains(const std::string &relativePath) const;
  bool addFile(const std::string &relativePath,bool writeToFs);

  //In this implementation equality is checked by reading file contents, not hashes
  TreeDiff diff(const FilesystemTree &other);
  QString rootPath;

private:
  void diffNodes(const FileNode *left, const QString &leftRootPath,
                 const FileNode *right, const QString &rightRootPath,
                 const QString &path, TreeDiff &result) const;
  void collectAllFiles(const FileNode *node, const QString &path,
                       QList<QString> &files) const;
  std::unique_ptr<FileNode> root;
  int countFileNodes(FileNode *node) const;
  void debugNode(const FileNode *node, int depth) const;
};
