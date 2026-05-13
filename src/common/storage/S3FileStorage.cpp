#include "S3FileStorage.h"
#include <QCryptographicHash>
#include <sstream>
#include <QDebug>
void S3FileStorage::init(const S3Config &config) {
    bucket = config.bucket;
    minio::s3::BaseUrl baseUrl(config.endpoint, config.useSSL);
    auto *provider = new minio::creds::StaticProvider(config.accessKey, config.secretKey);
    client = std::make_unique<minio::s3::Client>(baseUrl, provider);
    minio::s3::BucketExistsArgs existsArgs;
    existsArgs.bucket = bucket;
    auto existsResp = client->BucketExists(existsArgs);
    if (existsResp && !existsResp.exist) {
        minio::s3::MakeBucketArgs makeArgs;
        makeArgs.bucket = bucket;
        auto makeResp = client->MakeBucket(makeArgs);
        if (!makeResp) {
            qDebug() << "Failed to create bucket:" << makeResp.Error().String().c_str();
        }
    }
}

std::string S3FileStorage::objectKey(const QString &user, const QString &filename) const {
    return (user + "/" + filename).toStdString();
}

bool S3FileStorage::writeFile(const QString &user, const QString &filename, const QByteArray &contents) {
    std::istringstream stream(std::string(contents.constData(), contents.size()));
    minio::s3::PutObjectArgs args(stream, contents.size(), 0);
    args.bucket = bucket;
    args.object = objectKey(user, filename);
    auto resp = client->PutObject(args);
    if (!resp) {
        qDebug() << "S3 write failed:" << resp.Error().String().c_str();
        return false;
    }
    return true;
}

std::optional<QByteArray> S3FileStorage::readFile(const QString &user, const QString &filename) const {
    QByteArray result;
    minio::s3::GetObjectArgs args;
    args.bucket = bucket;
    args.object = objectKey(user, filename);
    args.datafunc = [&result](minio::http::DataFunctionArgs args) -> bool {
        result.append(args.datachunk.data(), args.datachunk.size());
        return true;
    };
    auto resp = client->GetObject(args);
    if (!resp) {
        qDebug() << "S3 read failed:" << resp.Error().String().c_str();
        return std::nullopt;
    }
    return result;
}

std::optional<QByteArray> S3FileStorage::readHashOf(const QString &user, const QString &filename) const {
    auto contents = readFile(user, filename);
    if (!contents.has_value()) return std::nullopt;
    return QCryptographicHash::hash(contents.value(), QCryptographicHash::Sha256);
}

void S3FileStorage::cleanup(const QString &user) {
    auto files = listFiles(user);
    for (const auto &file : files) {
        deleteFile(user, file);
    }
}

bool S3FileStorage::deleteFile(const QString &user, const QString &filename) {
    minio::s3::RemoveObjectArgs args;
    args.bucket = bucket;
    args.object = objectKey(user, filename);
    auto resp = client->RemoveObject(args);
    if (!resp) {
        qDebug() << "S3 delete failed:" << resp.Error().String().c_str();
        return false;
    }
    return true;
}

QList<QString> S3FileStorage::listFiles(const QString &user) const {
    QList<QString> files;
    minio::s3::ListObjectsArgs args;
    args.bucket = bucket;
    args.prefix = user.toStdString() + "/";
    auto resp = client->ListObjects(args);
    for (; resp; resp++) {
        auto item = *resp;
        if (!item) {
            qDebug() << "S3 list failed:" << item.Error().String().c_str();
            break;
        }
        QString key = QString::fromStdString(item.name);
        files.append(key.mid(user.length() + 1));
    }
    return files;
}
