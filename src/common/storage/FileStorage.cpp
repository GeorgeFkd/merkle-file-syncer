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
