#include "FileTree.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>

std::optional<std::tuple<FileNode *,bool>>
FileTree::find(const std::string &relativePath) const {
  if(relativePath.empty()){
    return std::make_tuple(root.get(),root->isDeleted);
  }
  auto parts =
      QString::fromStdString(relativePath).split('/', Qt::SkipEmptyParts);
  FileNode *current = getRoot();
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
  return std::make_tuple(current, current->isDeleted);
}


int FileTree::countFileNodes(FileNode *node) const {
  if (node->type == FileType::File)
    return 1;
  int sum = 0;
  for (auto &child : node->children) {
    sum += countFileNodes(child.get());
  }
  return sum;
}

void FileTree::markTombstoneRecursively(FileNode *node,const QDateTime& deletedAt) {
  node->isDeleted = true;
  node->deletedAt = deletedAt;
  for (auto &child:node->children) {
    markTombstoneRecursively(child.get(), deletedAt);
  }
}

void FileTree::collectAllFiles(const FileNode *node, const QString &path,
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

QString FileTree::getRelativePath(const FileNode *node) const {
  QStringList parts;
  const FileNode *current = node;
  while (current->parent != nullptr) {
    parts.prepend(current->path);
    current = current->parent;
  }
  return parts.join('/');
}

int FileTree::fileCount() const { return countFileNodes(getRoot()); }
