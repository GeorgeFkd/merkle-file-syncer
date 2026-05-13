#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

class FileStorage {
public:
    virtual ~FileStorage() = default;
    virtual std::optional<QByteArray> readHashOf(const QString &user, const QString &filename) const = 0;
    virtual bool writeFile(const QString &user, const QString &filename, const QByteArray &contents) = 0;
    virtual std::optional<QByteArray> readFile(const QString &user, const QString &filename) const = 0;
    virtual bool deleteFile(const QString &user, const QString &filename) = 0;
    virtual QList<QString> listFiles(const QString &user) const = 0;
    virtual void cleanup(const QString &user) = 0;
};
