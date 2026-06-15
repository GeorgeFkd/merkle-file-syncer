#include "MerkleTree.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

MerkleTree::MerkleTree() {}

// (lhs - rhs) ∪ (rhs - lhs) ∪ {x ∈ lhs ∩ rhs | hash_a(x) ≠ hash_b(x)}
TreeDiff
MerkleTree::symmetricHashDiff(const QList<QPair<QString, QByteArray>> &lhs,
                              const QList<QPair<QString, QByteArray>> &rhs) {

  QHash<QString, QByteArray> lhsMap;
  for (const auto &[path, hash] : lhs)
    lhsMap[path] = hash;

  QHash<QString, QByteArray> rhsMap;
  for (const auto &[path, hash] : rhs)
    rhsMap[path] = hash;

  TreeDiff result;

  // lhs - rhs
  for (const auto &[path, hash] : lhs) {
    if (!rhsMap.contains(path))
      result.onlyInLeft.append(path);
  }

  // rhs - lhs
  for (const auto &[path, hash] : rhs) {
    if (!lhsMap.contains(path))
      result.onlyInRight.append(path);
  }

  // lhs ∩ rhs with different hashes
  for (const auto &[path, hash] : lhs) {
    if (rhsMap.contains(path) && rhsMap[path] != hash) {
      if (rhsMap[path].isEmpty()) {
        result.onlyInLeft.append(path);
      } else if (hash.isEmpty()) {
        result.onlyInRight.append(path);
      } else {
        result.modified.append(path);
      }
    }
  }

  return result;
}
TreeDiff MerkleTree::merkleNegotiateDiffs(const MerkleTree &lhs,
                                          const MerkleTree &rhs) {
  auto leftHashes = lhs.getHashesAtDepth(0);
  auto rightHashes = rhs.getHashesAtDepth(0);
  auto diff = symmetricHashDiff(leftHashes, rightHashes);

  TreeDiff result;
  QList<QString> toDescend;

  auto processDiff = [&](const TreeDiff &diff) {
    for (const auto &path : diff.onlyInLeft) {
      auto found = lhs.find(path.toStdString());
      if (found.has_value()) {
        auto &[leftNode, isTombstoned] = *found;
        if (leftNode->type == FileType::File) {
          result.onlyInLeft.append(path);

        } else {
          toDescend.append(path);
        }
      }
    }
    for (const auto &path : diff.onlyInRight) {
      auto found = rhs.find(path.toStdString());
      if (found.has_value()) {
        auto &[rightNode, isTombstoned] = *found;
        if (rightNode->type == FileType::File) {
          result.onlyInRight.append(path);
        } else {
          toDescend.append(path);
        }
      }
    }
    for (const auto &path : diff.modified) {
      auto found = lhs.find(path.toStdString());
      if (found.has_value()) {
        auto &[leftNode, isTombstoned] = *found;
        if (leftNode->type == FileType::File) {
          result.modified.append(path);
        } else {
          toDescend.append(path);
        }
      }
    }
  };

  while (!diff.onlyInLeft.isEmpty() || !diff.onlyInRight.isEmpty() ||
         !diff.modified.isEmpty()) {
    toDescend.clear();
    processDiff(diff);

    if (toDescend.isEmpty())
      break;

    leftHashes.clear();
    rightHashes.clear();
    for (const auto &path : toDescend) {
      leftHashes.append(lhs.getChildHashes(path));
      rightHashes.append(rhs.getChildHashes(path));
    }

    diff = symmetricHashDiff(leftHashes, rightHashes);
  }

  return result;
}

MerkleTree::MerkleTree(const std::string &rootDir) {
  rootPath = QString::fromStdString(rootDir);
}

void MerkleTree::afterBuild() {
  if (root) {
    computeHashes(root.get());
    assert(verifyHashes());
  } else {
    qWarning() << "No root found in afterBuild()";
  }
}

QList<QPair<QString, QByteArray>>
MerkleTree::getHashesAtDepth(int depth) const {
  QList<QPair<QString, QByteArray>> result;
  collectHashesAtDepth(root.get(), "", depth, 0, result);
  return result;
}

void MerkleTree::collectHashesAtDepth(
    const FileNode *node, const QString &path, int targetDepth,
    int currentDepth, QList<QPair<QString, QByteArray>> &result) const {
  if (!node)
    return;
  if (currentDepth == targetDepth) {
    result.append({path, node->hash});
    return;
  }
  for (const auto &child : node->children) {
    QString childPath = path.isEmpty() ? child->path : path + "/" + child->path;
    collectHashesAtDepth(child.get(), childPath, targetDepth, currentDepth + 1,
                         result);
  }
}

QList<QPair<QString, QByteArray>>
MerkleTree::getChildHashes(const QString &path) const {
  auto found = find(path.toStdString());
  if (!found.has_value())
    return {};
  auto &[node, isTombstoned] = *found;
  QList<QPair<QString, QByteArray>> result;
  for (const auto &child : node->children) {
    QString childPath = path.isEmpty() ? child->path : path + "/" + child->path;
    result.append({childPath, child->hash});
  }
  return result;
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

  if (node->hash != hashChildren(node)) {
    qDebug() << "Hash mismatch for directory:" << node->path;
    return false;
  }
  return true;
}

QByteArray MerkleTree::hashChildren(const FileNode *node) const {
  QCryptographicHash dirHash(QCryptographicHash::Sha256);
  for (const auto &child : node->children) {
    dirHash.addData(child->hash);
  }
  return dirHash.result();
}

bool MerkleTree::deleteFile(const std::string &relativePath,
                            bool useTombstone) {
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

  // target is either a directory or a file
  const auto &targetName = parts.last();
  auto it = std::find_if(current->children.begin(), current->children.end(),
                         [&targetName](const std::unique_ptr<FileNode> &child) {
                           return child->path == targetName;
                         });

  if (it == current->children.end()) {
    qDebug() << "File not found:" << QString::fromStdString(relativePath);
    return false;
  }
  if (useTombstone) {
    auto node = (*it).get();
    markTombstoneRecursively(node, QDateTime::currentDateTime());
    computeHashes(current);
  } else {
    current->children.erase(it);
    recomputeDirHash(current);
  }
  // // auto node = (*it).get();
  // // markTombstoneRecursively(node, QDateTime::currentDateTime());
  // current->children.erase(it);
  // recomputeDirHash(current);
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

QByteArray MerkleTree::hashFile(const QString &relativePath) const {
  if (hasher) {
    return hasher(relativePath);
  }
  QFile f(rootPath + "/" + relativePath);
  if (!f.open(QIODevice::ReadOnly)) {
    qDebug() << "Failed to open file for hashing:" << relativePath
             << f.errorString();
    return {};
  }
  auto contents = f.readAll();
  f.close();
  return QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
}

bool MerkleTree::addFile(const std::string &relativePath,
                         const QDateTime &mtime) {
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
      newNode->mtime = mtime;
      newNode->type =
          (i == parts.size() - 1) ? FileType::File : FileType::Directory;
      if (newNode->type == FileType::File) {
        newNode->hash = hashFile(QString::fromStdString(relativePath));
      }
      current->children.push_back(std::move(newNode));
      current = current->children.back().get();
      if (current->type == FileType::File) {
        qDebug() << "Propagating hash of: " << current->path << ".";
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

void MerkleTree::setHasher(std::function<QByteArray(const QString &)> hasher) {
  this->hasher = std::move(hasher);
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
  // when the node is tombstoned the hash is computed the same regardless
  // if directory or file, i wrote it like this for more explicitness
  if (node->type == FileType::File) {
    if (node->isDeleted) {
      QString path = getRelativePath(node);
      node->hash = QCryptographicHash::hash(("tombstone:" + path).toUtf8(),
                                            QCryptographicHash::Sha256);
    } else {
      if (node->hash.isEmpty()) {
        node->hash = hashFile(getRelativePath(node));
      }
    }
  }
  if (node->type == FileType::Directory) {
    if (node->isDeleted) {
      QString path = getRelativePath(node);
      node->hash = QCryptographicHash::hash(("tombstone:" + path).toUtf8(),
                                            QCryptographicHash::Sha256);
    } else {
      for (auto &child : node->children) {
        computeHashes(child.get());
      }
      node->hash = hashChildren(node);
    }
  }
}

void MerkleTree::recomputeDirHash(FileNode *node) {
  Q_ASSERT_X(node->type == FileType::Directory, "recomputeDirHash",
             "node is not a directory");
  node->hash = hashChildren(node);
}

void MerkleTree::propagateHash(FileNode *node) {
  FileNode *current = node;
  while (current->parent != nullptr) {
    recomputeDirHash(current->parent);
    qDebug() << "Propagating to: " << current->parent->path
             << " hash: " << current->parent->hash.toHex() << ".";

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

  // in left not in right(deleted or key not even in)
  for (auto it = leftChildren.begin(); it != leftChildren.end(); ++it) {
    bool found = rightChildren.contains(it.key());
    if (!found) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File)
        result.onlyInLeft.append(fullPath);
      else
        collectAllFiles(it.value(), fullPath, result.onlyInLeft);
    } else {
      auto foundRight = rightChildren[it.key()];
      if (foundRight->isDeleted) {
        QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
        if (it.value()->type == FileType::File)
          result.onlyInLeft.append(fullPath);
        else
          collectAllFiles(it.value(), fullPath, result.onlyInLeft);
      }
    }
  }

  // in right not in left(deleted or key not even in)
  for (auto it = rightChildren.begin(); it != rightChildren.end(); ++it) {
    bool found = leftChildren.contains(it.key());
    if (!found) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      if (it.value()->type == FileType::File)
        result.onlyInRight.append(fullPath);
      else
        collectAllFiles(it.value(), fullPath, result.onlyInRight);
    } else {
      auto foundLeft = leftChildren[it.key()];
      if (foundLeft->isDeleted) {
        QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
        if (it.value()->type == FileType::File)
          result.onlyInRight.append(fullPath);
        else
          collectAllFiles(it.value(), fullPath, result.onlyInRight);
      }
    }
  }

  for (auto it = leftChildren.begin(); it != leftChildren.end(); ++it) {
    auto found = rightChildren.contains(it.key());
    if (found) {
      QString fullPath = path.isEmpty() ? it.key() : path + "/" + it.key();
      const FileNode *leftNode = it.value();
      const FileNode *rightNode = rightChildren[it.key()];
      if (rightNode->isDeleted || leftNode->isDeleted) {
        continue;
      }
      auto bothFiles =
          leftNode->type == FileType::File && rightNode->type == FileType::File;
      if (bothFiles) {
        auto hashesDiffer = leftNode->hash != rightNode->hash;
        auto pathsAreSame = leftNode->path == rightNode->path;
        if (hashesDiffer && pathsAreSame) {
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
