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

class FileTree {
public:
  virtual ~FileTree() = default;
  virtual bool contains(const std::string &relativePath) const = 0;
  virtual bool addFile(const std::string &relativePath,
                       bool writeToFilesystem = false) = 0;
  virtual int fileCount() const = 0;
  virtual TreeDiff diff(const FileTree &other) const = 0;
  virtual void debug() const = 0;
  virtual QString getRootPath() const = 0;
  virtual FileNode *getRoot() const = 0;
  virtual void build() = 0;
};

class FilesystemTree : public FileTree {
public:
  explicit FilesystemTree(const std::string &rootDir);
  int fileCount() const override;
  void debug() const override;
  std::optional<FileNode *> find(const std::string &relativePath) const;
  bool contains(const std::string &relativePath) const override;
  bool addFile(const std::string &relativePath,
               bool writeToFs = false) override;
  // In this implementation equality is checked by reading file contents, not
  // hashes
  TreeDiff diff(const FileTree &other) const override;
  QString getRootPath() const override;
  FileNode *getRoot() const override;
  void build() override;

private:
  void diffNodes(const FileNode *left, const QString &leftRootPath,
                 const FileNode *right, const QString &rightRootPath,
                 const QString &path, TreeDiff &result) const;
  void collectAllFiles(const FileNode *node, const QString &path,
                       QList<QString> &files) const;
  std::unique_ptr<FileNode> root;
  int countFileNodes(FileNode *node) const;
  void debugNode(const FileNode *node, int depth) const;
  QString rootPath;
};

class FileTreeFactory {
public:
  static std::unique_ptr<FileTree> create(const std::string &type,
                                          const std::string &rootDir) {
    if (type == "vanilla") {
      return std::make_unique<FilesystemTree>(rootDir);
    } else if (type == "merkle") {
      // return std::make_unique<MerkleTree>(rootDir);
      Q_ASSERT_X(false, "FileTreeFactory::create",
                 "MerkleTree not yet implemented");
    }
    Q_ASSERT_X(false, "FileTreeFactory::create", "Unknown tree type");
    return nullptr;
  }
};
