#pragma once
#include "FileStorage.h"
#include "miniocpp/client.h"
#include <memory>

struct S3Config {
    std::string endpoint;
    std::string accessKey;
    std::string secretKey;
    std::string bucket;
    bool useSSL = true;
};

class S3FileStorage : public FileStorage {
public:
    S3FileStorage() = default;
    void init(const S3Config &config);
    std::optional<QByteArray> readHashOf(const QString &user, const QString &filename) const override;
    bool writeFile(const QString &user, const QString &filename, const QByteArray &contents) override;
    std::optional<QByteArray> readFile(const QString &user, const QString &filename) const override;
    bool deleteFile(const QString &user, const QString &filename) override;
    QList<QString> listFiles(const QString &user) const override;
    void cleanup(const QString& user) override;
private:
    std::unique_ptr<minio::s3::Client> client;
    std::string bucket;
    std::string objectKey(const QString &user, const QString &filename) const;
};
