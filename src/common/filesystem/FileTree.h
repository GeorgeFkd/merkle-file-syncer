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
  // will bring this back when i start optimising stuff
  // std::array<uint8_t, 32> hash;
  QByteArray hash;
  // this is stable cos its wrapped behind a unique_ptr and std::vector is move
  // aware
  FileNode *parent;
  std::vector<std::unique_ptr<FileNode>> children;
};

class FileTree {
public:
  virtual bool contains(const std::string &relativePath) const = 0;
  virtual bool addFile(const std::string &relativePath,
                       bool writeToFilesystem = false) = 0;
  virtual int fileCount() const = 0;
  virtual TreeDiff diff(const FileTree &other) const = 0;
  virtual void debug() const = 0;
  virtual QString getRootPath() const = 0;
  virtual FileNode *getRoot() const = 0;
  virtual void build() = 0;
  virtual std::optional<FileNode *>
  find(const std::string &relativePath) const = 0;
};

class FilesystemTree : public FileTree {
public:
  explicit FilesystemTree(const std::string &rootDir);
  int fileCount() const override;
  void debug() const override;
  std::optional<FileNode *>
  find(const std::string &relativePath) const override;
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
  std::unique_ptr<FileNode> buildNode(const QString &path, FileNode *parent);
};

class MerkleTree : public FileTree {
public:
  explicit MerkleTree(const std::string &rootDir);
  void build() override;
  std::optional<FileNode *>
  find(const std::string &relativePath) const override;

  int fileCount() const override;
  void debug() const override;
  bool contains(const std::string &relativePath) const override;
  bool addFile(const std::string &relativePath,
               bool writeToFs = false) override;
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
  void collectAllFiles(const FileNode *node, const QString &path,
                       QList<QString> &files) const;
  void debugNode(const FileNode *node, int depth) const;
  int countFileNodes(FileNode *node) const;

  std::unique_ptr<FileNode> root;
  QString rootPath;
  std::unique_ptr<FileNode> buildNode(const QString &path, FileNode *parent);
  QString getRelativePath(const FileNode *node) const ;
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

template <TreeType Type> class FileTreeFactory {
public:
  static std::unique_ptr<FileTree> create(const std::string &rootDir) {
    if constexpr (Type == TreeType::Vanilla) {
      return std::make_unique<FilesystemTree>(rootDir);
    } else if constexpr (Type == TreeType::Merkle) {
      return std::make_unique<MerkleTree>(rootDir);
      Q_ASSERT_X(false, "FileTreeFactory::create",
                 "MerkleTree not yet implemented");
      return nullptr;
    }

    return nullptr;
  }
};
