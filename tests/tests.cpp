#include "FileClient.h"
#include "FileServer.h"
#include "FileTree.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QLocalServer>
#include <QTimer>
#include <QUuid>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

class SyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clientDir = new QDir(QCoreApplication::applicationDirPath() + "/test_client/" + runId);
        serverDir = new QDir(QCoreApplication::applicationDirPath() + "/test_server/" + runId);
        fileServer.setRootDir(serverDir->path());
        fileServer.listenOn(serverName);
        qDebug() << "Server listening:" << fileServer.isListening() << fileServer.serverName();
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
        QEventLoop loop;
        QObject::connect(&client, &FileClient::syncCompleted, &loop, &QEventLoop::quit);
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
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
    ASSERT_TRUE(serverFile.exists());
    serverFile.open(QIODevice::ReadOnly);
    ASSERT_EQ(serverFile.readAll(), QByteArray::fromStdString(filecontents));
}

template<typename TreeImplTag>
class FilesystemFixture : public ::testing::Test {
protected:
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
            if (parts.size() < 2) continue;
            QString relativePath = parts[0];
            QString op = parts.last();
            QString fullPath = dst.path() + "/" + relativePath;
            QString dirPath = fullPath.left(fullPath.lastIndexOf('/'));
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

    std::unique_ptr<FileTree> makeTree(const QDir &dir) {
        return FileTreeFactory<TreeImplTag::type>::create(dir.path().toStdString());
    }

    QDir rootDir;
};

using TreeImplementations = ::testing::Types<VanillaTreeTag>;

TYPED_TEST_SUITE(FilesystemFixture,TreeImplementations);

TYPED_TEST(FilesystemFixture, buildFromDiscoversFilesCorrectly) {
    this->applyOperations(this->rootDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");
    
    auto tree = this->makeTree(this->rootDir);
    tree->build();

    ASSERT_EQ(tree->fileCount(), 3);
    ASSERT_TRUE(tree->contains("foo/bar.txt"));
    ASSERT_TRUE(tree->contains("foo/baz.txt"));
    ASSERT_TRUE(tree->contains("foo/subdir/nested.txt"));
    ASSERT_TRUE(tree->contains("foo"));
    ASSERT_FALSE(tree->contains("fool"));
    ASSERT_FALSE(tree->contains("foo/new.txt"));
}

TYPED_TEST(FilesystemFixture, addFileBehaviorWorksAsExpected) {
    this->applyOperations(this->rootDir, R"(
        foo/bar.txt hello Write
    )");

    auto tree = this->makeTree(this->rootDir);
    tree->build();

    ASSERT_TRUE(tree->addFile("foo/new.txt", false));
    ASSERT_TRUE(tree->contains("foo/new.txt"));
    ASSERT_FALSE(QFile::exists(this->rootDir.path() + "/foo/new.txt"));

    tree->addFile("foo/write_to_fs.txt", true);
    ASSERT_TRUE(tree->contains("foo/write_to_fs.txt"));
    ASSERT_TRUE(QFile::exists(this->rootDir.path() + "/foo/write_to_fs.txt"));
}


template <typename TreeImplTag>
class FilesystemDiffFixture : public ::testing::Test {
protected:
    void SetUp() override {
        leftDir = QDir(QCoreApplication::applicationDirPath() + "/test_diff_left/" +
                       QUuid::createUuid().toString(QUuid::WithoutBraces));
        rightDir = QDir(QCoreApplication::applicationDirPath() + "/test_diff_right/" +
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
            if (parts.size() < 2) continue;
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

    std::unique_ptr<FileTree> makeTree(const QDir &dir) {
        return FileTreeFactory<TreeImplTag::type>::create(dir.path().toStdString());
    }

    QDir leftDir;
    QDir rightDir;
};

TYPED_TEST_SUITE(FilesystemDiffFixture,TreeImplementations);

TYPED_TEST(FilesystemDiffFixture, diffIdentifiesChanges) {
    this->applyOperations(this->leftDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt world Write
        foo/subdir/nested.txt nested Write
    )");

    this->applyOperations(this->rightDir, R"(
        foo/bar.txt hello Write
        foo/baz.txt changed Write
        foo/subdir/new.txt newfile Write
    )");

    auto leftTree = this->makeTree(this->leftDir);
    auto rightTree = this->makeTree(this->rightDir);
    leftTree->build();
    rightTree->build();

    auto diff = leftTree->diff(*rightTree);

    ASSERT_EQ(diff.modified.size(), 1);
    ASSERT_EQ(diff.modified[0], "foo/baz.txt");
    ASSERT_EQ(diff.onlyInLeft.size(), 1);
    ASSERT_EQ(diff.onlyInLeft[0], "foo/subdir/nested.txt");
    ASSERT_EQ(diff.onlyInRight.size(), 1);
    ASSERT_EQ(diff.onlyInRight[0], "foo/subdir/new.txt");
}

TYPED_TEST(FilesystemDiffFixture, addFileWithWriteReflectsInDiff){
    this->applyOperations(this->leftDir, R"(
        foo/bar.txt hello Write
    )");

    this->applyOperations(this->rightDir, R"(
        foo/bar.txt hello Write
    )");

    auto leftTree = this->makeTree(this->leftDir);
    auto rightTree = this->makeTree(this->rightDir);
    leftTree->build();
    rightTree->build();

    auto diff = leftTree->diff(*rightTree);
    ASSERT_EQ(diff.onlyInLeft.size(), 0);
    ASSERT_EQ(diff.onlyInRight.size(), 0);
    ASSERT_EQ(diff.modified.size(), 0);

    leftTree->addFile("foo/new.txt", true);

    diff = leftTree->diff(*rightTree);
    ASSERT_EQ(diff.onlyInLeft.size(), 1);
    ASSERT_EQ(diff.onlyInLeft[0], "foo/new.txt");
}
