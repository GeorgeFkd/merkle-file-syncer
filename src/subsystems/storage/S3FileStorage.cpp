#include "S3FileStorage.h"
#include <QCryptographicHash>
#include <QDebug>
#include <miniocpp/args.h>
#include <sstream>
void S3FileStorage::init(const S3Config &config) {
  bucket = config.bucket;
  minio::s3::BaseUrl baseUrl(config.endpoint, config.useSSL);
  auto *provider =
      new minio::creds::StaticProvider(config.accessKey, config.secretKey);
  client = std::make_unique<minio::s3::Client>(baseUrl, provider);
  minio::s3::BucketExistsArgs existsArgs;
  existsArgs.bucket = bucket;
  auto existsResp = client->BucketExists(existsArgs);
  if (existsResp && !existsResp.exist) {
    minio::s3::MakeBucketArgs makeArgs;
    makeArgs.bucket = bucket;
    auto makeResp = client->MakeBucket(makeArgs);
    if (!makeResp) {
      qDebug() << "Failed to create bucket:"
               << makeResp.Error().String().c_str();
    }
  }
}

std::string S3FileStorage::objectKey(const QString &user,
                                     const QString &filename) const {
  return (user + "/" + filename).toStdString();
}

bool S3FileStorage::writeFile(const QString &user, const QString &filename,
                              const QByteArray &contents) {
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

std::optional<QByteArray>
S3FileStorage::readFile(const QString &user, const QString &filename) const {
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
    qDebug() << "S3 read failed:" << resp.Error().String().c_str()
             << " for user: " << user << " at path: " << filename;
    return std::nullopt;
  }
  return result;
}

std::optional<QByteArray>
S3FileStorage::readHashOf(const QString &user, const QString &filename) const {
  auto contents = readFile(user, filename);
  if (!contents.has_value())
    return std::nullopt;
  return QCryptographicHash::hash(contents.value(), QCryptographicHash::Sha256);
}

void S3FileStorage::cleanup(const QString &user) {
  // abort any in-progress multipart uploads for this user
  const QString prefix = user + "/";
  for (auto it = activeMultipartUploads.begin();
       it != activeMultipartUploads.end();) {
    if (it.key().startsWith(prefix)) {
      minio::s3::AbortMultipartUploadArgs args;
      args.bucket = bucket;
      // reconstruct object key from the transfer key (user/filename)
      args.object = it.key().toStdString();
      args.upload_id = it.value().uploadId;
      auto resp = client->AbortMultipartUpload(args);
      if (!resp) {
        qDebug() << "S3FileStorage::cleanup: abort failed:"
                 << resp.Error().String().c_str();
      }
      it = activeMultipartUploads.erase(it);
    } else {
      ++it;
    }
  }

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
  args.recursive = true;
  auto resp = client->ListObjects(args);
  for (; resp; resp++) {
    auto item = *resp;
    if (!item) {
      qDebug() << "S3 list failed:" << item.Error().String().c_str();
      break;
    }
    QString key = QString::fromStdString(item.name);
    QString relativePath = key.mid(user.length() + 1);
    auto isDirectory = relativePath.endsWith("/") || relativePath.isEmpty();
    if (isDirectory)
      continue;
    files.append(relativePath);
  }
  return files;
}

std::optional<qint64> S3FileStorage::fileSize(const QString &user,
                                              const QString &path) const {
  minio::s3::StatObjectArgs args;
  args.bucket = bucket;
  args.object = objectKey(user, path);
  auto resp = client->StatObject(args);
  if (!resp) {
    qDebug() << "S3FileStorage::fileSize: stat failed for"
             << QString::fromStdString(objectKey(user, path)) << ":"
             << resp.Error().String().c_str();
    return std::nullopt;
  }
  return static_cast<qint64>(resp.size);
}

std::optional<QByteArray> S3FileStorage::readRange(const QString &user,
                                                   const QString &path,
                                                   qint64 offset,
                                                   qint64 length) const {
  if (length == 0) {
    return QByteArray{};
  }
  QByteArray result;
  size_t offsetVal = static_cast<size_t>(offset);
  size_t lengthVal = static_cast<size_t>(length);

  minio::s3::GetObjectArgs args;
  args.bucket = bucket;
  args.object = objectKey(user, path);
  args.offset = &offsetVal;
  args.length = &lengthVal;
  args.datafunc = [&result](minio::http::DataFunctionArgs args) -> bool {
    result.append(args.datachunk.data(), args.datachunk.size());
    return true;
  };
  auto resp = client->GetObject(args);
  if (!resp) {
    qDebug() << "S3FileStorage::readRange: get failed for"
             << QString::fromStdString(objectKey(user, path)) << "offset"
             << offset << "length" << length << ":"
             << resp.Error().String().c_str();
    return std::nullopt;
  }
  return result;
}

bool S3FileStorage::beginWrite(const QString &user, const QString &path, qint64,
                               qint64) {
  auto key = QString::fromStdString(objectKey(user, path));
  Q_ASSERT_X(!activeMultipartUploads.contains(key), "beginWrite",
             "transfer already in progress");

  minio::s3::CreateMultipartUploadArgs args;
  args.bucket = bucket;
  args.object = objectKey(user, path);

  auto resp = client->CreateMultipartUpload(args);
  if (!resp) {
    qDebug() << "S3FileStorage::beginWrite: create failed: "
             << resp.Error().String().c_str();
    return false;
  }

  MultipartState state;
  state.uploadId = resp.upload_id;
  activeMultipartUploads.insert(key, std::move(state));
  return true;
}

bool S3FileStorage::writeRange(const QString &user, const QString &path,
                               quint32 partNumber, qint64,
                               const QByteArray &bytes) {
  auto it = activeMultipartUploads.find(
      QString::fromStdString(objectKey(user, path)));
  assert(it != activeMultipartUploads.end() &&
         "should beginWrite before writeRange");

  minio::s3::UploadPartArgs args;
  args.bucket = bucket;
  args.object = objectKey(user, path);
  args.upload_id = it->uploadId;
  args.part_number = partNumber;
  args.data =
      std::string_view(bytes.constData(), static_cast<size_t>(bytes.size()));

  auto resp = client->UploadPart(args);
  if (!resp) {
    qDebug() << "writeRange failed to upload part " << partNumber
             << ", error: " << resp.Error().String().c_str();
    return false;
  }

  minio::s3::Part part;
  part.number = partNumber;
  part.etag = resp.etag;
  it->parts.push_back(part);
  return true;
}

bool S3FileStorage::finishWrite(const QString &user, const QString &path) {
  auto key = QString::fromStdString(objectKey(user, path));
  auto it = activeMultipartUploads.find(key);
  assert(it != activeMultipartUploads.end() &&
         "finishWrite should be called on an active multipart upload");

  // parts must be ascending by part number for CompleteMultipartUpload
  it->parts.sort([](const minio::s3::Part &a, const minio::s3::Part &b) {
    return a.number < b.number;
  });

  minio::s3::CompleteMultipartUploadArgs args;
  args.bucket = bucket;
  args.object = objectKey(user, path);
  args.upload_id = it->uploadId;
  args.parts = it->parts;

  auto resp = client->CompleteMultipartUpload(args);
  activeMultipartUploads.remove(key);
  if (!resp) {
    qDebug() << "Failed to complete multipart upload: "
             << resp.Error().String().c_str();
    return false;
  }

  return true;
}

bool S3FileStorage::abortWrite(const QString &user, const QString &path) {
  auto key = QString::fromStdString(objectKey(user, path));
  auto it = activeMultipartUploads.find(key);
  assert(it != activeMultipartUploads.end() &&
         "abortWrite should be called on an existing multipart upload");

  minio::s3::AbortMultipartUploadArgs args;
  args.bucket = bucket;
  args.object = objectKey(user, path);
  args.upload_id = it->uploadId;

  auto resp = client->AbortMultipartUpload(args);
  activeMultipartUploads.remove(key);
  if (!resp) {
    qDebug() << "abort failed: " << resp.Error().String().c_str();
    return false;
  }

  return true;
}

quint64 S3FileStorage::chunkSizeFor(quint64 fileSize,
                                    quint64 desiredChunkSize) {
  return 5 * 1024 * 1024;
}
