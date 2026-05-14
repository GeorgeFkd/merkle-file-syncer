#include "FileHasher.h"
#include <QCryptographicHash>
#include <QDebug>

FileHasher::FileHasher(const FileStorage *storage, const QString &user)
    : storage(storage), user(user) {}

QByteArray FileHasher::operator()(const QString &relativePath) const {
    auto contents = storage->readFile(user, relativePath);
    if (!contents.has_value()) {
        qDebug() << "Failed to read file for hashing:" << relativePath;
        return {};
    }
    return QCryptographicHash::hash(contents.value(), QCryptographicHash::Sha256);
}
