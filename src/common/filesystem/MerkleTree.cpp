#include "MerkleTree.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

MerkleTree::MerkleTree(const std::string &rootDir) {
  rootPath = QString::fromStdString(rootDir);
}

void MerkleTree::build() {
  QFileInfo info(rootPath);
  qDebug() << "Building merkle based filesystem tree from: " << rootPath
           << "\n";
  Q_ASSERT_X(info.isDir(), "MerkleTree::build", "rootPath is not a directory");
  root = buildNode(rootPath, nullptr);
  computeHashes(root.get());
}

bool MerkleTree::verifyHashes() const { return verifyNode(root.get()); }

bool MerkleTree::verifyNode(const FileNode *node) const {
  if (node->type == FileType::File) {
    if (node->hash.isEmpty()) {
      qDebug() << "Empty hash for file:" << node->path;
      return false;
    }
    return true;
  }

  QCryptographicHash dirHash(QCryptographicHash::Sha256);
  for (const auto &child : node->children) {
    if (!verifyNode(child.get()))
      return false;
    dirHash.addData(child->hash);
  }

  if (node->hash != dirHash.result()) {
    qDebug() << "Hash mismatch for directory:" << node->path;
    return false;
  }
  return true;
}

bool MerkleTree::deleteFile(const std::string &relativePath) {
  Q_ASSERT_X(root != nullptr, "MerkleTree::deleteFile", "root is null");
  Q_ASSERT_X(!relativePath.empty(), "MerkleTree::deleteFile",
             "relativePath is empty");

  auto parts =
      QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
  FileNode *current = root.get();

  for (int i = 0; i < parts.size() - 1; i++) {
    const auto &part = parts[i];
    FileNode *found = nullptr;
    for (const auto &child : current->children) {
      if (child->path == part) {
        found = child.get();
        break;
      }
    }
    if (!found) {
      qDebug() << "Path not found:" << QString::fromStdString(relativePath);
      return false;
    }
    current = found;
  }

  //target is either a directory or a file
  const auto &targetName = parts.last();
  auto it = std::find_if(current->children.begin(), current->children.end(),
                         [&targetName](const std::unique_ptr<FileNode> &child) {
                           return child->path == targetName;
                         });

  if (it == current->children.end()) {
    qDebug() << "File not found:" << QString::fromStdString(relativePath);
    return false;
  }

  current->children.erase(it);
  recomputeDirHash(current);
  propagateHash(current);
  return true;
}

void MerkleTree::debug() const { debugNode(root.get(), 0); }

void MerkleTree::debugNode(const FileNode *node, int depth) const {
  if (!node)
    return;
  QString indent(depth * 2, ' ');
  QString type = node->type == FileType::File ? "F" : "D";
  QString hashStr;
  for (uint8_t byte : node->hash)
    hashStr += QString::number(byte, 16).rightJustified(2, '0');
  qDebug() << indent + "[" + type + "] " + node->path + " hash: " + hashStr;
  for (const auto &child : node->children)
    debugNode(child.get(), depth + 1);
}

bool MerkleTree::addFile(const std::string &relativePath) {
  Q_ASSERT_X(root != nullptr, "MerkleTree::addFile",
             "root is null — tree not built");
  Q_ASSERT_X(!relativePath.empty(), "MerkleTree::addFile",
             "relativePath is empty");

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
        QByteArray contents;
        QFile f(rootPath + "/" + QString::fromStdString(relativePath));
        if (f.open(QIODevice::ReadOnly)) {
          contents = f.readAll();
          f.close();
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
             "cannot diff trees of different types, both must be MerkleTree.");
  Q_ASSERT_X(otherMerkle->root != nullptr, "MerkleTree::diff",
             "other tree doesnt have build() called.");
  TreeDiff result;
  diffNodes(root.get(), rootPath, otherMerkle->root.get(),
            otherMerkle->rootPath, "", result);
  return result;
}

FileNode *MerkleTree::getRoot() const { return root.get(); }
QString MerkleTree::getRootPath() const { return rootPath; }
QByteArray MerkleTree::rootHash() const { return root->hash; }

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
      node->hash = QCryptographicHash::hash(readFileContents(node),
                                            QCryptographicHash::Sha256);
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
  Q_ASSERT_X(node->type == FileType::Directory, "recomputeDirHash",
             "node is not a directory");
  QCryptographicHash dirHash(QCryptographicHash::Sha256);
  for (const auto &child : node->children)
    dirHash.addData(child->hash);
  node->hash = dirHash.result();
}

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
             "left node hash is empty, call build().");
  Q_ASSERT_X(!right->hash.isEmpty(), "MerkleTree::diffNodes",
             "right node hash is empty, call build().");
  if (left->hash == right->hash)
    return;

  QHash<QString, const FileNode *> leftChildren;
  for (const auto &child : left->children)
    leftChildren[child->path] = child.get();

  QHash<QString, const FileNode *> rightChildren;
  for (const auto &child : right->children)
    rightChildren[child->path] = child.get();

  for (auto it = leftChildren.begin(); it != leftChildren.end(); ++it) {
    if (!rightChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File)
        result.onlyInLeft.append(fullPath);
      else
        collectAllFiles(it.value(), fullPath, result.onlyInLeft);
    }
  }

  for (auto it = rightChildren.begin(); it != rightChildren.end(); ++it) {
    if (!leftChildren.contains(it.key())) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File)
        result.onlyInRight.append(fullPath);
      else
        collectAllFiles(it.value(), fullPath, result.onlyInRight);
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
