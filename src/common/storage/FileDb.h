#pragma once

#include <QString>
#include <QDateTime>
#include <QHash>
#include <optional>

class FileDb {
public:
    // file -> mtime
    std::optional<QDateTime> readMtime(const QString &file) const;
    void updateFileMtime(const QString &file, const QDateTime &mtime);

    // user -> (password, rootDirectory)
    std::optional<QString> readUserDirectory(const QString &user, const QString &password) const;
    void storeUser(const QString &user, const QString &password, const QString &rootDirectory);

private:
    struct UserRecord {
        QString password;
        QString rootDirectory;
    };

    QHash<QString, QDateTime> fileMtimes;
    QHash<QString, UserRecord> users;
};
