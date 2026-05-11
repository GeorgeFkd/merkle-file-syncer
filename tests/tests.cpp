#include "FileClient.h"
#include "FileServer.h"
#include "FileTree.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

class SyncTest : public ::testing::Test {
protected:
  void SetUp() override {
    QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clientDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_client/" + runId);
    serverDir = new QDir(QCoreApplication::applicationDirPath() +
                         "/test_server/" + runId);
    fileServer.setRootDir(serverDir->path());
    fileServer.listenOn(serverName);
    qDebug() << "Server listening:" << fileServer.isListening()
             << fileServer.serverName();
  }
  void TearDown() override {
    if (!HasFailure()) {
      QDir(clientDir->path()).removeRecursively();
      QDir(serverDir->path()).removeRecursively();
    }
    delete clientDir;
    delete serverDir;
  }
  void waitForSync(FileClient &client) {
    auto msWait = 100;
    QEventLoop loop;
    QObject::connect(&client, &FileClient::syncCompleted, &loop,
                     &QEventLoop::quit);
    QTimer::singleShot(msWait, &loop, &QEventLoop::quit);
    loop.exec();
  }
  QDir *clientDir = nullptr;
  QDir *serverDir = nullptr;
  QString serverName = "merkle_sync_test";
  FileServer fileServer;
};

TEST_F(SyncTest, filesAreSynced) {

  QString username("foo");
  std::string filecontents("Hello world");

  FileClient client;
  client.init();
  client.setRootDir(clientDir->path());
  client.setManualTick();
  client.setPassword("bar");
  client.setUsername(username);

  QString filename = "test.txt";
  QFile file(client.getUserRootDirectory(username) + "/" + filename);
  file.open(QIODevice::WriteOnly);
  file.write(QByteArray::fromStdString(filecontents));
  file.close();

  client.connectToServer(serverName);
  QCoreApplication::processEvents();
  client.clientTick();
  waitForSync(client);

  QFile serverFile(fileServer.getUserRootDirectory(username) + "/" + filename);
  RC_ASSERT(serverFile.exists());
  serverFile.open(QIODevice::ReadOnly);
  RC_ASSERT(serverFile.readAll() == QByteArray::fromStdString(filecontents));
}

class FilesystemFixture : public ::testing::Test {
protected:
  QDir rootDir;
  void SetUp() override {
    rootDir = QDir(QCoreApplication::applicationDirPath() + "/test_fs/" +
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(rootDir.path());
  }

  void TearDown() override {
    if (!HasFailure()) {
      rootDir.removeRecursively();
    }
  }

  void applyOperations(const QDir &dst, const QString &ops) {
    for (const auto &line : ops.split('\n', Qt::SkipEmptyParts)) {
      auto parts = line.trimmed().split(' ');
      if (parts.size() < 2)
        continue;
      QString relativePath = parts[0];
      QString op = parts.last();
      QString fullPath = dst.path() + "/" + relativePath;
      QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
      if (op == "Write") {
        QString contents = parts.size() > 2 ? parts[1] : "";
        QDir().mkpath(dirPath);
        QFile f(fullPath);
        f.open(QIODevice::WriteOnly);
        f.write(contents.toUtf8());
        f.close();
      } else if (op == "Delete") {
        QFile::remove(fullPath);
      }
    }
  }
};

TEST_F(FilesystemFixture, buildFromDiscoversFilesCorrectly) {
  applyOperations(rootDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");

  auto tree = FilesystemTree::buildFrom(rootDir.path().toStdString());

  ASSERT_EQ(tree.fileCount(), 3);
  ASSERT_TRUE(tree.contains("foo/bar.txt"));
  ASSERT_TRUE(tree.contains("foo/baz.txt"));
  ASSERT_TRUE(tree.contains("foo/subdir/nested.txt"));
  ASSERT_TRUE(tree.contains("foo"));
  ASSERT_FALSE(tree.contains("fool"));
  ASSERT_FALSE(tree.contains("foo/new.txt"));
}

TEST_F(FilesystemFixture, addFileBehaviorWorksAsExpected) {
  applyOperations(rootDir, R"(
        foo/bar.txt hello Write
    )");

  auto tree = FilesystemTree::buildFrom(rootDir.path().toStdString());
  ASSERT_TRUE(tree.addFile("foo/new.txt", false));
  ASSERT_TRUE(tree.contains("foo/new.txt"));
  ASSERT_FALSE(QFile::exists(rootDir.path() + "/foo/new.txt"));
  tree.addFile("foo/write_to_fs.txt", true);
  ASSERT_TRUE(tree.contains("foo/write_to_fs.txt"));
  ASSERT_TRUE(QFile::exists(rootDir.path() + "/foo/write_to_fs.txt"));
}

class FilesystemDiffFixture : public ::testing::Test {
protected:
  void SetUp() override {
    leftDir = QDir(QCoreApplication::applicationDirPath() + "/test_diff_left/" +
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    rightDir =
        QDir(QCoreApplication::applicationDirPath() + "/test_diff_right/" +
             QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(leftDir.path());
    QDir().mkpath(rightDir.path());
  }

  void TearDown() override {
    if (!HasFailure()) {
      leftDir.removeRecursively();
      rightDir.removeRecursively();
    }
  }

  void applyOperations(const QDir &dst, const QString &ops) {
    for (const auto &line : ops.split('\n', Qt::SkipEmptyParts)) {
      auto parts = line.trimmed().split(' ');
      if (parts.size() < 2)
        continue;
      QString fullPath = dst.path() + "/" + parts[0];
      QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
      QString op = parts.last();
      if (op == "Write") {
        QString contents = parts.size() > 2 ? parts[1] : "";
        QDir().mkpath(dirPath);
        QFile f(fullPath);
        if (!f.open(QIODevice::WriteOnly)) {
          qDebug() << "Failed to open file:" << fullPath << f.errorString();
          return;
        }
        f.write(contents.toUtf8());
        f.close();
      } else if (op == "Delete") {
        QFile::remove(fullPath);
      }
    }
  }

  QDir leftDir;
  QDir rightDir;
};

TEST_F(FilesystemDiffFixture, diffIdentifiesChanges) {
  applyOperations(leftDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");

  applyOperations(rightDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt changed Write
        foo/subdir/new.txt newfile Write
    )");

  auto leftTree = FilesystemTree::buildFrom(leftDir.path().toStdString());
  auto rightTree = FilesystemTree::buildFrom(rightDir.path().toStdString());

  auto diff = leftTree.diff(rightTree);

  // foo/baz.txt exists in both but content changed
  ASSERT_EQ(diff.modified.size(), 1);
  ASSERT_EQ(diff.modified[0], "foo/baz.txt");

  // foo/subdir/nested.txt only in left
  ASSERT_EQ(diff.onlyInLeft.size(), 1);
  ASSERT_EQ(diff.onlyInLeft[0], "foo/subdir/nested.txt");

  // foo/subdir/new.txt only in right
  ASSERT_EQ(diff.onlyInRight.size(), 1);
  ASSERT_EQ(diff.onlyInRight[0], "foo/subdir/new.txt");
}

TEST_F(FilesystemDiffFixture, addFileWithWriteReflectsInDiff) {
    applyOperations(leftDir, R"(
        foo/bar.txt hello Write
    )");

    applyOperations(rightDir, R"(
        foo/bar.txt hello Write
    )");

    auto leftTree = FilesystemTree::buildFrom(leftDir.path().toStdString());
    auto rightTree = FilesystemTree::buildFrom(rightDir.path().toStdString());

    auto diff = leftTree.diff(rightTree);
    ASSERT_EQ(diff.onlyInLeft.size(), 0);
    ASSERT_EQ(diff.onlyInRight.size(), 0);
    ASSERT_EQ(diff.modified.size(), 0);

    leftTree.addFile("foo/new.txt", true);

    diff = leftTree.diff(rightTree);
    ASSERT_EQ(diff.onlyInLeft.size(), 1);
    ASSERT_EQ(diff.onlyInLeft[0], "foo/new.txt");
}
