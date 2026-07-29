#pragma once
#include "FileStorage.h"
#include <QDateTime>
#include <QSaveFile>
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
  std::optional<qint64> fileSize(const QString &user,
                                 const QString &path) const override;
  std::optional<QByteArray> readRange(const QString &user, const QString &path,
                                      qint64 offset,
                                      qint64 length) const override;

  bool beginWrite(const QString &user, const QString &path, qint64 totalSize,
                  qint64 chunkSize) override;

  bool writeRange(const QString& user, const QString& path, quint32 partNumber,qint64 offset, const QByteArray& bytes) override;

  bool finishWrite(const QString& user, const QString& path) override;

  bool abortWrite(const QString& user, const QString& path) override;

private:
  
  QString saveKeyFor(const QString& user, const QString &path) const;

  QString fullPath(const QString &user, const QString &filename) const;
  QString rootDir;
  QHash<QString,std::shared_ptr<QSaveFile>> activeSaveFiles;
};
