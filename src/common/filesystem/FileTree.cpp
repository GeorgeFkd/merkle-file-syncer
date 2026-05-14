#include "FileTree.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QString>

void FileTree::buildFromStorage(const FileStorage *storage,const QString& user) {
  root = std::make_unique<FileNode>();
  root->type = FileType::Directory;
  root->path = user;
  root->parent = nullptr;

  auto files = storage->listFiles(user);
  for (const auto &file : files) {
    addFile(file.toStdString());
  }
  afterBuild();
}

std::optional<FileNode *>
FileTree::find(const std::string &relativePath) const {
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
  return current;
}

bool FileTree::contains(const std::string &relativePath) const {
  return find(relativePath).has_value();
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
