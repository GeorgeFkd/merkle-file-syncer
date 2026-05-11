#include "FileTree.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
#include <qcryptographichash.h>
std::unique_ptr<FileNode> FilesystemTree::buildNode(const QString &path,
                                                    FileNode *parent) {
  auto node = std::make_unique<FileNode>();
  QFileInfo info(path);

  node->path = info.fileName();
  node->parent = parent;
  if (info.isDir()) {
    node->type = FileType::Directory;
    QDir dir(path);
    const auto entries =
        dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const auto &entry : entries) {
      node->children.emplace_back(
          buildNode(entry.absoluteFilePath(), node.get()));
    }
  } else {
    node->type = FileType::File;
  }

  return node;
}

int FilesystemTree::countFileNodes(FileNode *node) const {
  if (node->type == FileType::File) {
    return 1;
  }
  int sum = 0;
  for (auto &node : node->children) {
    sum += countFileNodes(node.get());
  }
  return sum;
}

void FilesystemTree::debug() const { debugNode(root.get(), 0); }

void FilesystemTree::debugNode(const FileNode *node, int depth) const {
  if (!node)
    return;
  QString indent(depth * 2, ' ');
  QString type = node->type == FileType::File ? "F" : "D";
  qDebug() << indent + "[" + type + "] " + node->path;
  for (const auto &child : node->children) {
    debugNode(child.get(), depth + 1);
  }
}

// it does not write the file to the filesystem, beware
bool FilesystemTree::addFile(const std::string &relativePath, bool writeToFs) {
  qDebug() << "Adding file: " << relativePath << "\n";
  Q_ASSERT_X(root != nullptr, "FilesystemTree::addFile", "root is null");
  Q_ASSERT_X(!relativePath.empty(), "FilesystemTree::addFile",
             "relativePath is empty");

  if (writeToFs) {
    QString fullPath = rootPath + "/" + QString::fromStdString(relativePath);
    QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
    QDir().mkpath(dirPath);
    QFile f(fullPath);
    if (!f.open(QIODevice::WriteOnly)) {
      qDebug() << "Failed to open file for writing:" << fullPath
               << f.errorString();
      return false;
    }
    f.close();
  }
  auto parts =
      QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
  FileNode *current = root.get();
  for (int i = 0; i < parts.size(); i++) {
    qDebug() << "Current is: " << current->path;
    if (!current) {
      qDebug() << "Current is nullptr something went wrong";
      return false;
    }
    const auto &part = parts[i];
    FileNode *found = nullptr;
    Q_ASSERT_X(current->type == FileType::Directory, "FilesystemTree::addFile",
               "expected directory node but found file node");
    for (const auto &child : current->children) {
      if (child->path == part) {
        found = child.get();
        break;
      }
    }
    if (!found) {
      auto newNode = std::make_unique<FileNode>();
      newNode->path = part;
      newNode->type =
          (i == parts.size() - 1) ? FileType::File : FileType::Directory;
      current->children.push_back(std::move(newNode));
      qDebug() << "Adding new Node at: " << current->path;
      current = current->children.back().get();
    } else {
      current = found;
    }
  }
  return true;
}

bool FilesystemTree::contains(const std::string &relativePath) const {
  return find(relativePath).has_value();
}

std::optional<FileNode *>
MerkleTree::find(const std::string &relativePath) const {
  auto parts =
      QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
  FileNode *current = root.get();
  for (const auto &part : parts) {
    bool found = false;
    for (const auto &child : current->children) {
      if (child->path == part) {
        current = child.get();
        found = true;
        break;
      }
    }
    if (!found)
      return {};
  }
  return current;
}

std::optional<FileNode *>
FilesystemTree::find(const std::string &relativePath) const {
  auto parts =
      QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
  FileNode *current = root.get();
  for (const auto &part : parts) {
    bool found = false;
    for (const auto &child : current->children) {
      if (child->path == part) {
        current = child.get();
        found = true;
        break;
      }
    }
    if (!found)
      return {};
  }
  return current;
}

int FilesystemTree::fileCount() const { return countFileNodes(root.get()); }
void FilesystemTree::build() {
  QFileInfo info(rootPath);
  qDebug() << "Building filesystem tree from: " << rootPath << "\n";
  Q_ASSERT_X(info.isDir(), "FilesystemTree::build",
             "rootPath is not a directory");
  root = buildNode(rootPath, nullptr);
}

void FilesystemTree::collectAllFiles(const FileNode *node, const QString &path,
                                     QList<QString> &files) const {
  if (node->type == FileType::File) {
    files.append(path);
    return;
  }
  for (const auto &child : node->children) {
    QString fullPath = path + "/" + child->path;
    collectAllFiles(child.get(), fullPath, files);
  }
}

FilesystemTree::FilesystemTree(const std::string &rootDir) {
  rootPath = QString::fromStdString(rootDir);
}

FileNode *FilesystemTree::getRoot() const { return root.get(); }

QString FilesystemTree::getRootPath() const { return rootPath; }

TreeDiff FilesystemTree::diff(const FileTree &other) const {
  TreeDiff result;
  diffNodes(root.get(), rootPath, other.getRoot(), other.getRootPath(), "",
            result);
  return result;
}
void FilesystemTree::diffNodes(const FileNode *left,
                               const QString &leftRootPath,
                               const FileNode *right,
                               const QString &rightRootPath,
                               const QString &path, TreeDiff &result) const {
  // collect children of left
  QHash<QString, const FileNode *> leftChildren;
  if (left) {
    for (const auto &child : left->children) {
      leftChildren[child->path] = child.get();
    }
  }

  // collect children of right
  QHash<QString, const FileNode *> rightChildren;
  if (right) {
    for (const auto &child : right->children) {
      rightChildren[child->path] = child.get();
    }
  }

  // find nodes only in left
  for (auto it = leftChildren.begin(); it != leftChildren.end(); ++it) {
    if (!rightChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File) {
        result.onlyInLeft.append(fullPath);
      } else {
        collectAllFiles(it.value(), fullPath, result.onlyInLeft);
      }
    }
  }

  // find nodes only in right
  for (auto it = rightChildren.begin(); it != rightChildren.end(); ++it) {
    if (!leftChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File) {
        result.onlyInRight.append(fullPath);
      } else {
        collectAllFiles(it.value(), fullPath, result.onlyInRight);
      }
    }
  }

  // recurse into nodes that exist in both
  for (auto it = leftChildren.begin(); it != leftChildren.end(); ++it) {
    if (rightChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      const FileNode *leftNode = it.value();
      const FileNode *rightNode = rightChildren[it.key()];
      if (leftNode->type == FileType::File &&
          rightNode->type == FileType::File) {
        QFile lf(leftRootPath + "/" + fullPath);
        QFile rf(rightRootPath + "/" + fullPath);
        lf.open(QIODevice::ReadOnly);
        rf.open(QIODevice::ReadOnly);
        // if the paths dont match but the contents do we can detect a rename
        if (lf.readAll() != rf.readAll() && leftNode->path == rightNode->path) {
          qDebug() << "Found a file with different contents";
          result.modified.append(fullPath);
        }
      } else {
        diffNodes(leftNode, leftRootPath, rightNode, rightRootPath, fullPath,
                  result);
      }
    }
  }
}

MerkleTree::MerkleTree(const std::string &rootDir) {
  rootPath = QString::fromStdString(rootDir);
}

void MerkleTree::build() {
  QFileInfo info(rootPath);
  qDebug() << "Building merkle based filesystem tree from: " << rootPath
           << "\n";
  Q_ASSERT_X(info.isDir(), "FilesystemTree::build",
             "rootPath is not a directory");
  root = buildNode(rootPath, nullptr);
  computeHashes(root.get());
}

int MerkleTree::fileCount() const { return countFileNodes(root.get()); }

bool MerkleTree::contains(const std::string &relativePath) const {
  return find(relativePath).has_value();
}

bool MerkleTree::addFile(const std::string &relativePath, bool writeToFs) {
  Q_ASSERT_X(root != nullptr, "MerkleTree::addFile",
             "root is null — tree not built");
  Q_ASSERT_X(!relativePath.empty(), "MerkleTree::addFile",
             "relativePath is empty");

  if (writeToFs) {
    QString fullPath = rootPath + "/" + QString::fromStdString(relativePath);
    QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
    QDir().mkpath(dirPath);
    QFile f(fullPath);
    if (!f.open(QIODevice::WriteOnly)) {
      qDebug() << "Failed to open file for writing:" << fullPath
               << f.errorString();
      return false;
    }
    f.close();
  }

  auto parts =
      QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
  FileNode *current = root.get();

  for (int i = 0; i < parts.size(); i++) {
    const auto &part = parts[i];
    FileNode *found = nullptr;
    for (const auto &child : current->children) {
      if (child->path == part) {
        found = child.get();
        break;
      }
    }
    if (!found) {
      auto newNode = std::make_unique<FileNode>();
      newNode->path = part;
      newNode->parent = current;
      newNode->type =
          (i == parts.size() - 1) ? FileType::File : FileType::Directory;
      if (newNode->type == FileType::File) {
        QString fullPath =
            rootPath + "/" + QString::fromStdString(relativePath);
        QByteArray contents;
        if (writeToFs) {
          QFile f(fullPath);
          if (f.open(QIODevice::ReadOnly)) {
            contents = f.readAll();
            f.close();
          }
        }
        newNode->hash =
            QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
      }
      current->children.push_back(std::move(newNode));
      current = current->children.back().get();
      if (current->type == FileType::File) {
        propagateHash(current);
        return true;
      }
    } else {
      current = found;
    }
  }
  return true;
}

TreeDiff MerkleTree::diff(const FileTree &other) const {
  Q_ASSERT_X(root != nullptr, "MerkleTree::diff",
             "tree doesnt have build() called.");
  const auto *otherMerkle = dynamic_cast<const MerkleTree *>(&other);
  Q_ASSERT_X(otherMerkle != nullptr, "MerkleTree::diff",
             "cannot diff trees of different types,both must be MerkleTree.");
  Q_ASSERT_X(otherMerkle->root != nullptr, "MerkleTree::diff",
             "other tree doesnt have build() called.");

  TreeDiff result;
  diffNodes(root.get(), rootPath, otherMerkle->root.get(),
            otherMerkle->rootPath, "", result);
  return result;
}

QString MerkleTree::getRootPath() const { return rootPath; }

FileNode *MerkleTree::getRoot() const { return root.get(); }

QByteArray MerkleTree::rootHash() const { return root->hash; }

QString MerkleTree::getRelativePath(const FileNode *node) const {
  QStringList parts;
  const FileNode *current = node;
  while (current->parent != nullptr) {
    parts.prepend(current->path);
    current = current->parent;
  }
  return parts.join('/');
}
QByteArray MerkleTree::readFileContents(const FileNode *node) const {
  QString fullPath = rootPath + "/" + getRelativePath(node);
  QFile file(fullPath);
  if (!file.open(QIODevice::ReadOnly)) {
    qDebug() << "Failed to open file for hashing:" << fullPath
             << file.errorString();
    return {};
  }
  auto contents = file.readAll();
  file.close();
  return contents;
}

void MerkleTree::computeHashes(FileNode *node) {
  if (node->type == FileType::File) {
    if (node->hash.isEmpty()) {
      auto contents = readFileContents(node);
      node->hash =
          QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
    }
  } else {
    QCryptographicHash dirHash(QCryptographicHash::Sha256);
    for (auto &child : node->children) {
      computeHashes(child.get());
      dirHash.addData(child->hash);
    }
    node->hash = dirHash.result();
  }
}

void MerkleTree::recomputeDirHash(FileNode *node) {
  Q_ASSERT_X(node->type == FileType::Directory, "recomputeDirectoryHash",
             "node is not a directory");
  QCryptographicHash dirHash(QCryptographicHash::Sha256);
  for (const auto &child : node->children) {
    dirHash.addData(child->hash);
  }
  node->hash = dirHash.result();
}

// it is assumed that the FileNode has already done its hash.
void MerkleTree::propagateHash(FileNode *node) {
  FileNode *current = node;
  while (current->parent != nullptr) {
    recomputeDirHash(current->parent);
    current = current->parent;
  }
}

void MerkleTree::diffNodes(const FileNode *left, const QString &leftRootPath,
                           const FileNode *right, const QString &rightRootPath,
                           const QString &path, TreeDiff &result) const {
  Q_ASSERT_X(!left->hash.isEmpty(), "MerkleTree::diffNodes",
             "left node hash is empty,call build() method on the Tree.");
  Q_ASSERT_X(!right->hash.isEmpty(), "MerkleTree::diffNodes",
             "right node hash is empty,call build() method on the Tree.");
  if (!left->hash.isEmpty() && left->hash == right->hash)
    return;

  QHash<QString, const FileNode *> leftChildren;
  for (const auto &child : left->children) {
    leftChildren[child->path] = child.get();
  }

  QHash<QString, const FileNode *> rightChildren;
  for (const auto &child : right->children) {
    rightChildren[child->path] = child.get();
  }

  for (auto it = leftChildren.begin(); it != leftChildren.end(); ++it) {
    if (!rightChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File) {
        result.onlyInLeft.append(fullPath);
      } else {
        collectAllFiles(it.value(), fullPath, result.onlyInLeft);
      }
    }
  }

  for (auto it = rightChildren.begin(); it != rightChildren.end(); ++it) {
    if (!leftChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File) {
        result.onlyInRight.append(fullPath);
      } else {
        collectAllFiles(it.value(), fullPath, result.onlyInRight);
      }
    }
  }

  for (auto it = leftChildren.begin(); it != leftChildren.end(); ++it) {
    if (rightChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      const FileNode *leftNode = it.value();
      const FileNode *rightNode = rightChildren[it.key()];
      if (leftNode->type == FileType::File &&
          rightNode->type == FileType::File) {
        if (leftNode->hash != rightNode->hash &&
            leftNode->path == rightNode->path) {
          qDebug() << "Detected file diff";
          result.modified.append(fullPath);
        }
      } else {
        diffNodes(leftNode, leftRootPath, rightNode, rightRootPath, fullPath,
                  result);
      }
    }
  }
}

void MerkleTree::collectAllFiles(const FileNode *node, const QString &path,
                                 QList<QString> &files) const {
  assert(false && "Not implemented yet");
}

void MerkleTree::debug() const { debugNode(root.get(), 0); }

void MerkleTree::debugNode(const FileNode *node, int depth) const {
  if (!node)
    return;
  QString indent(depth * 2, ' ');
  QString type = node->type == FileType::File ? "F" : "D";

  QString hashStr;
  for (uint8_t byte : node->hash) {
    hashStr += QString::number(byte, 16).rightJustified(2, '0');
  }

  qDebug() << indent + "[" + type + "] " + node->path + " hash: " + hashStr;

  for (const auto &child : node->children) {
    debugNode(child.get(), depth + 1);
  }
}

int MerkleTree::countFileNodes(FileNode *node) const {
  // it can be hashed for directories but no need at least for now
  if (node->type == FileType::File) {
    return 1;
  }
  int sum = 0;
  for (auto &node : node->children) {
    sum += countFileNodes(node.get());
  }
  return sum;
}

std::unique_ptr<FileNode> MerkleTree::buildNode(const QString &path,
                                                FileNode *parent) {
  auto node = std::make_unique<FileNode>();
  QFileInfo info(path);

  node->path = info.fileName();
  node->parent = parent;
  if (info.isDir()) {
    node->type = FileType::Directory;
    QDir dir(path);
    const auto entries =
        dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const auto &entry : entries) {
      node->children.emplace_back(
          buildNode(entry.absoluteFilePath(), node.get()));
    }
  } else {
    node->type = FileType::File;
  }

  return node;
}
