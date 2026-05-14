#include "SimpleFileTree.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

SimpleFileTree::SimpleFileTree(const std::string &rootDir) {
  rootPath = QString::fromStdString(rootDir);
}

void SimpleFileTree::build() {
  QFileInfo info(rootPath);
  qDebug() << "Building filesystem tree from: " << rootPath << "\n";
  Q_ASSERT_X(info.isDir(), "SimpleFileTree::build",
             "rootPath is not a directory");
  root = buildNode(rootPath, nullptr);
}

void SimpleFileTree::debug() const { debugNode(root.get(), 0); }

bool SimpleFileTree::deleteFile(const std::string& relativePath) {
  Q_ASSERT_X(root != nullptr, "SimpleFileTree::deleteFile", "root is null");
    Q_ASSERT_X(!relativePath.empty(), "SimpleFileTree::deleteFile", "relativePath is empty");

    auto parts = QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
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

    const auto &filename = parts.last();
    auto it = std::find_if(current->children.begin(), current->children.end(),
                           [&filename](const std::unique_ptr<FileNode> &child) {
                               return child->path == filename;
                           });

    if (it == current->children.end()) {
        qDebug() << "File not found:" << QString::fromStdString(relativePath);
        return false;
    }

    current->children.erase(it);
    return true;
}

void SimpleFileTree::debugNode(const FileNode *node, int depth) const {
  if (!node)
    return;
  QString indent(depth * 2, ' ');
  QString type = node->type == FileType::File ? "F" : "D";
  qDebug() << indent + "[" + type + "] " + node->path;
  for (const auto &child : node->children) {
    debugNode(child.get(), depth + 1);
  }
}

bool SimpleFileTree::addFile(const std::string &relativePath) {
  qDebug() << "Adding file: " << relativePath << "\n";
  Q_ASSERT_X(root != nullptr, "SimpleFileTree::addFile", "root is null");
  Q_ASSERT_X(!relativePath.empty(), "SimpleFileTree::addFile",
             "relativePath is empty");

  auto parts =
      QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
  FileNode *current = root.get();
  for (int i = 0; i < parts.size(); i++) {
    if (!current)
      return false;
    const auto &part = parts[i];
    FileNode *found = nullptr;
    Q_ASSERT_X(current->type == FileType::Directory, "SimpleFileTree::addFile",
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
      current = current->children.back().get();
    } else {
      current = found;
    }
  }
  return true;
}

FileNode *SimpleFileTree::getRoot() const { return root.get(); }

QString SimpleFileTree::getRootPath() const { return rootPath; }

TreeDiff SimpleFileTree::diff(const FileTree &other) const {
  TreeDiff result;
  diffNodes(root.get(), rootPath, other.getRoot(), other.getRootPath(), "",
            result);
  return result;
}

void SimpleFileTree::diffNodes(const FileNode *left,
                               const QString &leftRootPath,
                               const FileNode *right,
                               const QString &rightRootPath,
                               const QString &path, TreeDiff &result) const {
  QHash<QString, const FileNode *> leftChildren;
  if (left) {
    for (const auto &child : left->children)
      leftChildren[child->path] = child.get();
  }

  QHash<QString, const FileNode *> rightChildren;
  if (right) {
    for (const auto &child : right->children)
      rightChildren[child->path] = child.get();
  }

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
        QFile lf(leftRootPath + "/" + fullPath);
        QFile rf(rightRootPath + "/" + fullPath);
        if (!lf.open(QIODevice::ReadOnly) || !rf.open(QIODevice::ReadOnly)) {
          qDebug() << "Failed to open files for comparison:" << fullPath;
          return;
        }
        // lf.open(QIODevice::ReadOnly);
        // rf.open(QIODevice::ReadOnly);
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
