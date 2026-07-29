#include "LocalFileStorage.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <memory>
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
  // beware: do not just delete everyone's saveFiles, might namespace them per
  // user later(TODO)
  const QString prefix = user + "/";
  QList<QString> toAbort;
  for (auto it = activeSaveFiles.cbegin(); it != activeSaveFiles.cend(); ++it) {
    if (it.key().startsWith(prefix)) {
      toAbort.append(it.key());
    }
  }

  for (const auto &k : toAbort) {
    activeSaveFiles[k]->cancelWriting();
    activeSaveFiles.remove(k);
  }
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

std::optional<QDateTime>
LocalFileStorage::getMtime(const QString &user, const QString &filename) const {
  QString path = fullPath(user, filename);
  QFileInfo info(path);
  if (!info.exists())
    return {};
  return info.lastModified();
}

std::optional<qint64> LocalFileStorage::fileSize(const QString &user,
                                                 const QString &path) const {
  QString fullPath = rootPath(user) + "/" + path;
  QFileInfo info(fullPath);
  if (!info.exists()) {
    qDebug() << "LocalFileStorage::fileSize: file does not exist" << fullPath;
    return std::nullopt;
  }
  return info.size();
}

std::optional<QByteArray> LocalFileStorage::readRange(const QString &user,
                                                      const QString &path,
                                                      qint64 offset,
                                                      qint64 length) const {
  // better be explicit than implicit
  if (length == 0) {
    return QByteArray{};
  }

  QString fullPath = rootPath(user) + "/" + path;
  QFile file(fullPath);
  if (!file.open(QIODevice::ReadOnly)) {
    qDebug() << "LocalFileStorage::readRange: could not open" << fullPath;
    return std::nullopt;
  }
  if (!file.seek(offset)) {
    qDebug() << "LocalFileStorage::readRange: seek failed for" << fullPath
             << "offset" << offset;
    return std::nullopt;
  }
  QByteArray data = file.read(length);
  return data;
}

QString LocalFileStorage::saveKeyFor(const QString &user,
                                     const QString &path) const {
  return user + "/" + path;
}

bool LocalFileStorage::beginWrite(const QString &user, const QString &path,
                                  qint64, qint64) {
  const QString key = saveKeyFor(user, path);

  Q_ASSERT_X(!activeSaveFiles.contains(key), "beginWrite",
             "transfer already in progress for (user,path)");

  const QString finalFilePath = fullPath(user, path);
  QDir().mkpath(QFileInfo(finalFilePath).absolutePath());

  auto save = std::make_unique<QSaveFile>(finalFilePath);
  if (!save->open(QIODevice::WriteOnly)) {
    return false;
  }

  activeSaveFiles.insert(key, std::move(save));
  return true;
}

bool LocalFileStorage::writeRange(const QString &user, const QString &path,
                                  quint32, qint64 offset,
                                  const QByteArray &bytes) {
  auto it = activeSaveFiles.find(saveKeyFor(user, path));
  Q_ASSERT_X(it != activeSaveFiles.end(), "writeRange",
             "no active transfer for (user,path)");
  QSaveFile *save = it->get();
  if (!save->seek(offset)) {
    return false;
  }

  return save->write(bytes) == bytes.size();
}

bool LocalFileStorage::finishWrite(const QString &user, const QString &path) {
  const QString key = saveKeyFor(user, path);
  auto it = activeSaveFiles.find(key);
  Q_ASSERT_X(it != activeSaveFiles.end(), "finishWrite",
             "no active transfer for (user,path)");

  const bool ok = it->get()->commit();
  activeSaveFiles.erase(it);
  return ok;
}

bool LocalFileStorage::abortWrite(const QString &user, const QString &path) {
  const QString key = saveKeyFor(user, path);
  auto it = activeSaveFiles.find(key);
  Q_ASSERT_X(it != activeSaveFiles.end(), "abortWrite",
             "no active transfer for (user,path)");

  it->get()->cancelWriting();
  activeSaveFiles.erase(it);
  return true;
}
