#include "MerkleTree.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>


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

MerkleTree::MerkleTree(const QString &rootNodeName) {
  root = std::make_unique<FileNode>();
  root->type = FileType::Directory;
  root->path = rootNodeName;
  root->parent = nullptr;
  root->hash = hashChildren(root.get());
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
  auto expected = hashChildren(node);
  if (node->hash != expected) {
    qDebug() << "Hash mismatch for directory:" << getRelativePath(node)
             << "stored:" << node->hash.toHex().left(16)
             << "expected:" << expected.toHex().left(16);
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
                            const QDateTime &deletedAt) {
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

  auto node = (*it).get();
  markTombstoneRecursively(node, deletedAt);
  propagateHashDownward(current);
  propagateHashUpward(current);
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

bool MerkleTree::addFile(const std::string &relativePath,
                         const QDateTime &mtime, const QByteArray &hash) {
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
        newNode->hash = hash;
      }
      current->children.push_back(std::move(newNode));
      current = current->children.back().get();
      if (current->type == FileType::File) {
        qDebug() << "Propagating hash of: " << current->path << ".";
        propagateHashUpward(current);
        return true;
      }
    } else {
      current = found;
      if (i == parts.size() - 1) {
        current->mtime = mtime;
        if (current->isDeleted) {
          // resurrect from tombstone
          current->isDeleted = false;
          current->deletedAt = QDateTime();
          current->type = FileType::File;
        }

        if (current->type == FileType::File) {
          current->hash = hash;
          qDebug() << "Propagating hash of: " << current->path << " (updated).";
          propagateHashUpward(current);
          return true;
        }
      }
    }
  }
  return true;
}

FileNode *MerkleTree::getRoot() const { return root.get(); }
QByteArray MerkleTree::rootHash() const { return root->hash; }

QByteArray MerkleTree::hashTombstoned(const FileNode *node) const {
  assert(node->isDeleted);
  QString path = getRelativePath(node);
  return QCryptographicHash::hash(("tombstone:" + path).toUtf8(),
                                  QCryptographicHash::Sha256);
}

void MerkleTree::propagateHashDownward(FileNode *node) {
  //if it is a file and not deleted the hash is already there from addFile
  //this is only needed after marking tombstones downward as addFile only needs
  //to propagate upward.
  if (node->isDeleted) {
    node->hash = hashTombstoned(node);
  } else if (node->type == FileType::Directory) {
    for (auto &child : node->children) {
      propagateHashDownward(child.get());
    }
    node->hash = hashChildren(node);
  }

  assert(!node->hash.isEmpty());
}

void MerkleTree::recomputeDirHash(FileNode *node) {
  Q_ASSERT_X(node->type == FileType::Directory, "recomputeDirHash",
             "node is not a directory");
  node->hash = hashChildren(node);
}

void MerkleTree::propagateHashUpward(FileNode *node) {
  FileNode *current = node;
  while (current->parent != nullptr) {
    recomputeDirHash(current->parent);
    qDebug() << "Propagating to: " << current->parent->path
             << " hash: " << current->parent->hash.toHex() << ".";

    current = current->parent;
  }
}
