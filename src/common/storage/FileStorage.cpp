// in FileStorage.cpp (create if it doesn't exist)
#include "FileStorage.h"
#include <QDebug>
#include <QMap>

void FileStorage::showFileTree(const QString &user) const {
  auto files = listFiles(user);
  std::sort(files.begin(), files.end());

  qDebug().noquote() << user;
  for (const auto &path : files) {
    int depth = path.count('/');
    QString indent(depth * 2, ' ');
    QString name = path.section('/', -1);
    qDebug().noquote() << "  " << indent << name;
  }
}

bool FileStorage::isEqualTo(const FileStorage &other,
                                    const QString &user) const {
  auto a = listFiles(user);
  auto b = other.listFiles(user);
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  if (a != b)
    return false;

  for (const auto &path : a) {
    auto contentsA = readFile(user, path);
    auto contentsB = other.readFile(user, path);
    if (contentsA != contentsB)
      return false;
  }
  return true;
}
