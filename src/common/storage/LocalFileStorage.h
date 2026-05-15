#pragma once
#include "FileStorage.h"
#include <QDateTime>
class LocalFileStorage : public FileStorage {
public:
  LocalFileStorage();
  std::optional<QByteArray> readHashOf(const QString &user,
                                       const QString &filename) const override;
  bool writeFile(const QString &user, const QString &filename,
                 const QByteArray &contents) override;
  std::optional<QByteArray> readFile(const QString &user,
                                     const QString &filename) const override;
  bool deleteFile(const QString &user, const QString &filename) override;
  QList<QString> listFiles(const QString &user) const override;
  void setRoot(const QString &path);
  QString rootPath(const QString &user) const;
  void cleanup(const QString &user) override;
  std::optional<QDateTime> getMtime(const QString &user,
                                    const QString &filename) const;

private:
  QString fullPath(const QString &user, const QString &filename) const;
  QString rootDir;
};
