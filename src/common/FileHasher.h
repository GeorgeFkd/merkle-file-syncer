#pragma once
#include "storage/FileStorage.h"
#include <QByteArray>
#include <QString>

class FileHasher {
public:
  FileHasher() : storage(nullptr) {};
  explicit FileHasher(const FileStorage *storage, const QString &user);
  QByteArray operator()(const QString &relativePath) const;

private:
  const FileStorage *storage;
  QString user;
};
