#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

class FileStorage {
public:
  virtual ~FileStorage() = default;
  virtual std::optional<QByteArray>
  readHashOf(const QString &user, const QString &filename) const = 0;
  virtual bool writeFile(const QString &user, const QString &filename,
                         const QByteArray &contents) = 0;
  virtual std::optional<QByteArray> readFile(const QString &user,
                                             const QString &filename) const = 0;
  virtual bool deleteFile(const QString &user, const QString &filename) = 0;
  // Should be a QSet<QString> so i can do set operations easily
  // it also expresses the property that there should not be duplicate
  // entries.
  virtual QList<QString> listFiles(const QString &user) const = 0;
  virtual void cleanup(const QString &user) = 0;
  void showFileTree(const QString &user) const;
  bool isEqualTo(const FileStorage &other, const QString &user) const;
  virtual std::optional<qint64> fileSize(const QString &user,
                                         const QString &path) const = 0;
  virtual std::optional<QByteArray> readRange(const QString &user,
                                              const QString &path,
                                              qint64 offset,
                                              qint64 length) const = 0;
};
