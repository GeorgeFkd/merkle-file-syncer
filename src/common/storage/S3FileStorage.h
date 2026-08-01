#pragma once
#include "FileStorage.h"
#include "miniocpp/client.h"
#include <QHash>
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
  std::optional<QByteArray> readHashOf(const QString &user,
                                       const QString &filename) const override;
  bool writeFile(const QString &user, const QString &filename,
                 const QByteArray &contents) override;
  std::optional<QByteArray> readFile(const QString &user,
                                     const QString &filename) const override;
  bool deleteFile(const QString &user, const QString &filename) override;
  QList<QString> listFiles(const QString &user) const override;
  void cleanup(const QString &user) override;
  std::optional<qint64> fileSize(const QString &user,
                                 const QString &path) const override;
  std::optional<QByteArray> readRange(const QString &user, const QString &path,
                                      qint64 offset,
                                      qint64 length) const override;

  bool beginWrite(const QString &user, const QString &path, qint64 totalSize,
                  qint64 chunkSize) override;
  bool writeRange(const QString &user, const QString &path, quint32 partNumber,
                  qint64, const QByteArray &bytes) override;
  bool finishWrite(const QString &user, const QString &path) override;

  bool abortWrite(const QString &user, const QString &path) override;

  quint64 chunkSizeFor(quint64 fileSize, quint64 desiredChunkSize) override;

private:
  struct MultipartState {
    std::string uploadId;
    std::list<minio::s3::Part> parts;
  };

  QHash<QString, MultipartState> activeMultipartUploads;

  std::unique_ptr<minio::s3::Client> client;
  std::string bucket;
  std::string objectKey(const QString &user, const QString &filename) const;
};
