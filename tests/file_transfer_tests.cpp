#include "FileStorage.h"
#include "FileTransferClient.h"
#include "FileTransferServer.h"
#include "LocalFileStorage.h"
#include "Messages.h"
#include "S3FileStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>
#include <gtest/gtest.h>
#include <memory>

// S3 requires every part except the last to be >= 5 MB. Use this whenever a
// test needs a genuine multi-part transfer that must also be valid on S3.
static constexpr qint64 S3_MIN_PART_SIZE = 5 * 1024 * 1024;

// ---- storage backend tags -------------------------------------------------

struct LocalStorageTag {
  static std::unique_ptr<FileStorage> makeStorage(const QString &rootPath) {
    auto s = std::make_unique<LocalFileStorage>();
    s->setRoot(rootPath);
    return s;
  }
};

struct S3StorageTag {
  static std::unique_ptr<FileStorage> makeStorage(const QString &) {
    auto s = std::make_unique<S3FileStorage>();
    s->init(S3Config{.endpoint = "localhost:9000",
                     .accessKey = "minioadmin",
                     .secretKey = "minioadmin",
                     .bucket = "test-bucket",
                     .useSSL = false});
    return s;
  }
};

// The tag selects the SERVER storage backend only. The client is ALWAYS
// LocalFileStorage (a client is a device with a real filesystem); S3 is only
// ever a server-side backend.
using FileTransferImplementations =
    ::testing::Types<LocalStorageTag, S3StorageTag>;

// ---- fixture --------------------------------------------------------------

template <typename Tag> class FileTransferTest : public ::testing::Test {
protected:
  void SetUp() override {
    runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clientDir = QDir(QDir::tempPath() + "/ft_client/" + runId);
    serverDir = QDir(QDir::tempPath() + "/ft_server/" + runId);
    QDir().mkpath(clientDir.path());
    QDir().mkpath(serverDir.path());

    // Client is ALWAYS local; only the server backend varies by tag.
    auto localClient = std::make_unique<LocalFileStorage>();
    localClient->setRoot(clientDir.path());
    clientStorage = std::move(localClient);
    serverStorage = Tag::makeStorage(serverDir.path());
    clientStorage->cleanup(user);
    serverStorage->cleanup(user);

    // Server: reactive, owns a storage, emits sendMessage(msg, outCtx).
    server = std::make_unique<FileTransferServer>(serverStorage.get());

    // Client: always local storage + the user identity. Outbound goes via its
    // sendMessage signal (same pattern as the server).
    client = std::make_unique<FileTransferClient>(clientStorage.get(), user);

    // client -> server: forward the client's outbound into the server,
    // attaching the client id as inbound context.
    QObject::connect(
        client.get(), &FileTransferClient::sendMessage, server.get(),
        [this](std::shared_ptr<Message> msg) {
          server->onMessage(msg.get(),
                            FileTransferServerInMsgCtx{clientId, user});
        },
        Qt::DirectConnection);

    // server -> client: forward the server's outbound back into the client
    QObject::connect(
        server.get(), &FileTransferServer::sendMessage, client.get(),
        [this](std::shared_ptr<Message> msg, FileTransferServerOutMsgCtx) {
          client->onMessage(msg.get());
        },
        Qt::DirectConnection);

    wireCompletionSignals();
  }

  void TearDown() override {
    clientStorage->cleanup(user);
    serverStorage->cleanup(user);
    QDir(clientDir.path()).removeRecursively();
    QDir(serverDir.path()).removeRecursively();
  }

  // Capture terminal transfer events so tests can wait on them.
  void wireCompletionSignals() {
    QObject::connect(client.get(), &FileTransferClient::uploadCompleted,
                     client.get(),
                     [this](const QString &) { uploadDone = true; });
    QObject::connect(client.get(), &FileTransferClient::downloadCompleted,
                     client.get(),
                     [this](const QString &) { downloadDone = true; });
    QObject::connect(client.get(), &FileTransferClient::uploadCancelled,
                     client.get(),
                     [this](const QString &) { uploadCancelledFlag = true; });
    QObject::connect(client.get(), &FileTransferClient::downloadCancelled,
                     client.get(),
                     [this](const QString &) { downloadCancelledFlag = true; });
  }

  QString runId;
  QDir clientDir;
  QDir serverDir;
  std::unique_ptr<FileStorage> clientStorage;
  std::unique_ptr<FileStorage> serverStorage;
  std::unique_ptr<FileTransferClient> client;
  std::unique_ptr<FileTransferServer> server;
  QString user = "alice";
  ClientId clientId = "client-1";

  bool uploadDone = false;
  bool downloadDone = false;
  bool uploadCancelledFlag = false;
  bool downloadCancelledFlag = false;
};

TYPED_TEST_SUITE(FileTransferTest, FileTransferImplementations);

TYPED_TEST(FileTransferTest, uploadRoundTrips) {
  const QString path = "docs/report.bin";
  QByteArray original = "Hello, this is the uploaded payload.";
  ASSERT_TRUE(this->clientStorage->writeFile(this->user, path, original));

  this->client->startUpload(path);
  this->pump();

  ASSERT_TRUE(this->uploadDone);
  auto onServer = this->serverStorage->readFile(this->user, path);
  ASSERT_TRUE(onServer.has_value());
  ASSERT_EQ(onServer.value(), original);
}

TYPED_TEST(FileTransferTest, downloadRoundTrips) {
  const QString path = "docs/manual.bin";
  QByteArray original = "Server-side content to be downloaded.";
  ASSERT_TRUE(this->serverStorage->writeFile(this->user, path, original));

  this->client->startDownload(path, /*desiredChunkSize*/ 1024);
  this->pump();

  ASSERT_TRUE(this->downloadDone);
  auto onClient = this->clientStorage->readFile(this->user, path);
  ASSERT_TRUE(onClient.has_value());
  ASSERT_EQ(onClient.value(), original);
}

TYPED_TEST(FileTransferTest, uploadMultiPartRoundTrips) {
  const QString path = "big/blob.bin";
  QByteArray part1(S3_MIN_PART_SIZE, 'a');
  QByteArray part2(4096, 'b'); // short final part is allowed
  QByteArray original = part1 + part2;
  ASSERT_TRUE(this->clientStorage->writeFile(this->user, path, original));

  this->client->startUpload(path);
  this->pump(500);

  ASSERT_TRUE(this->uploadDone);
  auto onServer = this->serverStorage->readFile(this->user, path);
  ASSERT_TRUE(onServer.has_value());
  ASSERT_EQ(onServer.value(), original);
}

TYPED_TEST(FileTransferTest, cancelUploadDiscardsPartial) {
  const QString path = "cancel_me.bin";
  // multi-part so at least one progress tick fires before completion
  QByteArray original(S3_MIN_PART_SIZE + 4096, 'x');
  ASSERT_TRUE(this->clientStorage->writeFile(this->user, path, original));

  QObject::connect(
      this->client.get(), &FileTransferClient::uploadProgress,
      this->client.get(),
      [this, path](const QString &p, quint32 currentPart, quint32) {
        if (p == path && currentPart == 1) {
          this->client->cancelUpload(path);
        }
      });

  this->client->startUpload(path);
  this->pump();

  ASSERT_TRUE(this->uploadCancelledFlag);
  auto onServer = this->serverStorage->readFile(this->user, path);
  ASSERT_FALSE(onServer.has_value());
}
TYPED_TEST(FileTransferTest, cancelDownloadDiscardsPartial) {
  const QString path = "dl_cancel.bin";
  QByteArray original(S3_MIN_PART_SIZE + 4096, 'y');
  ASSERT_TRUE(this->serverStorage->writeFile(this->user, path, original));

  QObject::connect(
      this->client.get(), &FileTransferClient::downloadProgress,
      this->client.get(),
      [this, path](const QString &p, quint32 currentPart, quint32) {
        if (p == path && currentPart == 1) {
          this->client->cancelDownload(path);
        }
      });

  this->client->startDownload(path, /*desiredChunkSize*/ 65536);
  this->pump();

  ASSERT_TRUE(this->downloadCancelledFlag);
  auto onClient = this->clientStorage->readFile(this->user, path);
  ASSERT_FALSE(onClient.has_value());
}

// During an upload the server must not expose the file at its real path until
// finishWrite commits it. 
TYPED_TEST(FileTransferTest, fileInvisibleUntilFinish) {
  const QString path = "pending.bin";
  QByteArray original(S3_MIN_PART_SIZE + 4096, 'z');
  ASSERT_TRUE(this->clientStorage->writeFile(this->user, path, original));

  bool checkedMidTransfer = false;
  bool visibleMidTransfer = true; // must become false

  QObject::connect(
      this->client.get(), &FileTransferClient::uploadProgress,
      this->client.get(),
      [this, path, &checkedMidTransfer,
       &visibleMidTransfer](const QString &p, quint32 currentPart, quint32) {
        if (p == path && currentPart == 1 && !checkedMidTransfer) {
          checkedMidTransfer = true;
          // server has written part 1 but not finishWrite'd yet:
          // the file must NOT be visible at its real path
          visibleMidTransfer =
              this->serverStorage->readFile(this->user, path).has_value();
        }
      });

  this->client->startUpload(path);
  this->pump();

  // we actually observed the mid-transfer moment
  ASSERT_TRUE(checkedMidTransfer);
  // and mid-transfer the file was invisible
  ASSERT_FALSE(visibleMidTransfer);

  // after completion it must be visible with the full content
  ASSERT_TRUE(this->uploadDone);
  auto onServer = this->serverStorage->readFile(this->user, path);
  ASSERT_TRUE(onServer.has_value());
  ASSERT_EQ(onServer.value(), original);
}
