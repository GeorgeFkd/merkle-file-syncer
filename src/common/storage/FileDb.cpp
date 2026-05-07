#include "FileDb.h"

std::optional<QDateTime> FileDb::readMtime(const QString &file) const {
    if (fileMtimes.contains(file)) {
        return fileMtimes[file];
    }
    return std::nullopt;
}

void FileDb::updateFileMtime(const QString &file, const QDateTime &mtime) {
    fileMtimes[file] = mtime;
}

std::optional<QString> FileDb::readUserDirectory(const QString &user, const QString &password) const {
    if (!users.contains(user)) {
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
