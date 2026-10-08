#pragma once
#include "FileStorage.h"
#include "LocalFileStorage.h"
#include "S3FileStorage.h"
#include <QDebug>
#include <QDir>
#include <QString>
#include <memory>
#include <miniocpp/client.h>

// Storage backend tags shared by the typed test suites. Each fixture creates a
// unique runId and passes it in, so every test gets its own namespace (a
// directory for Local, a bucket for S3) and tests can safely run in parallel.
//
// destroyStorage() removes everything makeStorage() created. It is test-only
// for now; it is meant to move into the storage layer later.

struct LocalStorageTag {
  static std::unique_ptr<FileStorage> makeStorage(const QString &rootPath,
                                                  const QString &) {
    auto s = std::make_unique<LocalFileStorage>();
    s->setRoot(rootPath);
    return s;
  }

  static bool destroyStorage(const QString &rootPath, const QString &) {
    return QDir(rootPath).removeRecursively();
  }
};

struct S3StorageTag {
  static constexpr auto kEndpoint = "localhost:9000";
  static constexpr auto kAccessKey = "minioadmin";
  static constexpr auto kSecretKey = "minioadmin";

  // Bucket names must be 3-63 chars of lowercase letters, digits and hyphens;
  // "test-" + a QUuid (lowercase hex, 36 chars) fits.
  static std::string bucketFor(const QString &runId) {
    return ("test-" + runId).toStdString();
  }

  static std::unique_ptr<FileStorage> makeStorage(const QString &,
                                                  const QString &runId) {
    auto s = std::make_unique<S3FileStorage>();
    s->init(S3Config{.endpoint = kEndpoint,
                     .accessKey = kAccessKey,
                     .secretKey = kSecretKey,
                     .bucket = bucketFor(runId),
                     .useSSL = false});
    return s;
  }

  // Deletes every object in the run's bucket, then the bucket itself (S3
  // refuses to remove a non-empty bucket).
  static bool destroyStorage(const QString &, const QString &runId) {
    const auto bucket = bucketFor(runId);
    minio::s3::BaseUrl baseUrl(kEndpoint, false);
    minio::creds::StaticProvider provider(kAccessKey, kSecretKey);
    minio::s3::Client client(baseUrl, &provider);

    std::list<std::string> keys;
    minio::s3::ListObjectsArgs listArgs;
    listArgs.bucket = bucket;
    listArgs.recursive = true;
    for (auto it = client.ListObjects(listArgs); it; it++) {
      auto item = *it;
      if (!item) {
        qWarning() << "destroyStorage: list failed for" << bucket.c_str()
                   << item.Error().String().c_str();
        return false;
      }
      keys.push_back(item.name);
    }

    bool ok = true;
    for (const auto &key : keys) {
      minio::s3::RemoveObjectArgs removeArgs;
      removeArgs.bucket = bucket;
      removeArgs.object = key;
      auto resp = client.RemoveObject(removeArgs);
      if (!resp) {
        qWarning() << "destroyStorage: remove failed for" << key.c_str()
                   << resp.Error().String().c_str();
        ok = false;
      }
    }

    minio::s3::RemoveBucketArgs bucketArgs;
    bucketArgs.bucket = bucket;
    auto resp = client.RemoveBucket(bucketArgs);
    if (!resp) {
      qWarning() << "destroyStorage: remove bucket failed for" << bucket.c_str()
                 << resp.Error().String().c_str();
      return false;
    }
    return ok;
  }
};
