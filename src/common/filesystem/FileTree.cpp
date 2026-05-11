#include "FileTree.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
std::unique_ptr<FileNode> buildNode(const QString &path) {
  auto node = std::make_unique<FileNode>();
  QFileInfo info(path);

  node->path = info.fileName();

  if (info.isDir()) {
    node->type = FileType::Directory;
    QDir dir(path);
    const auto entries =
        dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
    for (const auto &entry : entries) {
      node->children.emplace_back(buildNode(entry.absoluteFilePath()));
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

FilesystemTree FilesystemTree::buildFrom(const std::string &rootDir) {
  FilesystemTree tree;
  auto strDir = QString::fromStdString(rootDir);
  QFileInfo rootPath(strDir);
  qDebug() << "Building filesystem tree from: " << strDir << " \n";
  assert(rootPath.isDir() && "Root directory path given is not a directory");
  tree.root = buildNode(QString::fromStdString(rootDir));
  tree.rootPath = strDir;
  return tree;
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

TreeDiff FilesystemTree::diff(const FilesystemTree &other) {
  TreeDiff result;
  diffNodes(root.get(), rootPath, other.root.get(), other.rootPath, "", result);
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
        //if the paths dont match but the contents do we can detect a rename
        if (lf.readAll() != rf.readAll() && leftNode->path == rightNode->path) {
          qDebug() << "Found a file with different contents";
          result.modified.append(fullPath);
        }
        // if (leftNode->hash != rightNode->hash) {
        //   result.modified.append(fullPath);
        // }
      } else {
        diffNodes(leftNode, leftRootPath, rightNode, rightRootPath, fullPath,
                  result);
      }
    }
  }
}
