#include "FileDb.h"

std::optional<QDateTime> FileDb::readMtime(const QString &file) const {
    if (fileMtimes.contains(file)) {
        return fileMtimes[file];
    }
    return std::nullopt;
}

void FileDb::removeFileMtime(const QString &file) {
    fileMtimes.remove(file);
}

QSet<QString> FileDb::allTrackedFiles() const {
    return QSet<QString>(fileMtimes.keyBegin(), fileMtimes.keyEnd());
}

void FileDb::updateFileMtime(const QString &file, const QDateTime &mtime) {
    fileMtimes[file] = mtime;
}

std::optional<QString> FileDb::readUserDirectory(const QString &user, const QString &password) const {
    if (!users.contains(user)) {
        qDebug() << "User: " << user << " not found.\n Will be created.\n";
        return std::nullopt;
    }
    const auto &record = users[user];
    if (record.password != password) {
        qDebug() << "Wrong password\n";
        return std::nullopt;
    }
    return record.rootDirectory;
}

void FileDb::storeUser(const QString &user, const QString &password, const QString &rootDirectory) {
    users[user] = {password, rootDirectory};
}
