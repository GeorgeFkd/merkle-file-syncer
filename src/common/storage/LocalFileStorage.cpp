#include "LocalFileStorage.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
LocalFileStorage::LocalFileStorage() {
  QDir().mkpath(QCoreApplication::applicationDirPath() + "/storage");
}

QString LocalFileStorage::fullPath(const QString &user,
                                   const QString &filename) const {
  return rootPath(user) + "/" + filename;
}

void LocalFileStorage::setRoot(const QString &path) {
  rootDir = QDir(path).absolutePath();
  QDir().mkpath(rootDir);
}

void LocalFileStorage::cleanup(const QString &user) {
  QDir(rootPath(user)).removeRecursively();
}

QString LocalFileStorage::rootPath(const QString &user) const {
  return rootDir + "/storage/" + user;
}

std::optional<QByteArray>
LocalFileStorage::readHashOf(const QString &user,
                             const QString &filename) const {
  auto contents = readFile(user, filename);
  if (!contents.has_value())
    return std::nullopt;
  return QCryptographicHash::hash(contents.value(), QCryptographicHash::Sha256);
}

bool LocalFileStorage::writeFile(const QString &user, const QString &filename,
                                 const QByteArray &contents) {
  QString path = fullPath(user, filename);
  QDir().mkpath(QFileInfo(path).dir().absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    qDebug() << "Failed to write file:" << path << f.errorString();
    return false;
  }
  f.write(contents);
  f.close();
  return true;
}

std::optional<QByteArray>
LocalFileStorage::readFile(const QString &user, const QString &filename) const {
  QFile f(fullPath(user, filename));
  if (!f.open(QIODevice::ReadOnly)) {
    qDebug() << "Failed to read file:" << fullPath(user, filename)
             << f.errorString();
    return std::nullopt;
  }
  auto contents = f.readAll();
  f.close();
  return contents;
}

bool LocalFileStorage::deleteFile(const QString &user,
                                  const QString &filename) {
  if (!QFile::remove(fullPath(user, filename))) {
    qDebug() << "Failed to delete file:" << fullPath(user, filename);
    return false;
  }
  return true;
}

QList<QString> LocalFileStorage::listFiles(const QString &user) const {
  QList<QString> files;
  QString userRoot = rootPath(user);
  QDirIterator it(userRoot, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString fullFilePath = it.next();
    files.append(QDir(userRoot).relativeFilePath(fullFilePath));
  }
  return files;
}

std::optional<QDateTime> LocalFileStorage::getMtime(const QString &user, const QString &filename) const {
    QString path = fullPath(user, filename);
    QFileInfo info(path);
    if (!info.exists()) return std::nullopt;
    return info.lastModified();
}
