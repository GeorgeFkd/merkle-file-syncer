#include "FileClient.h"
#include "FileServer.h"
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
    QDir(clientDir->path()).removeRecursively();
    QDir(serverDir->path()).removeRecursively();
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

// RC_GTEST_FIXTURE_PROP(SyncTest, serverMirrorsClient, ()) {
//   auto operations = *rc::gen::nonEmpty(
//       rc::gen::container<std::vector<std::pair<std::string, bool>>>(
//           rc::gen::pair(
//               rc::gen::nonEmpty(rc::gen::container<std::string>(
//                   rc::gen::elementOf(std::string("abcdefghijklmnopqrstuvwxyz"
//                                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//                                                  "0123456789_-")))),
//               rc::gen::arbitrary<bool>())));
//   QString username("foo");
//   FileClient client;
//   client.init();
//   client.setRootDir(clientDir->path());
//   client.setManualTick();
//   client.setPassword("bar");
//   client.setUsername(username);
//
//   QString userDir = client.getUserRootDirectory(username);
//   QDir().mkpath(userDir);
//
//   for (const auto &[filename, isWrite] : operations) {
//     QString qFilename = QString::fromStdString(filename);
//     QString fullPath = userDir + "/" + qFilename;
//     if (isWrite) {
//       QFile file(fullPath);
//       if (file.open(QIODevice::WriteOnly)) {
//         file.write(qFilename.toUtf8());
//         file.close();
//       }
//     } else {
//       QFile::remove(fullPath);
//     }
//   }
//
//   client.connectToServer(serverName);
//   QCoreApplication::processEvents();
//   client.clientTick();
//   waitForSync(client);
//
//   QString serverUserDir = fileServer.getUserRootDirectory(username);
//
//   // assert all client files exist on server with same contents
//   QDirIterator clientIt(userDir, QDir::Files, QDirIterator::Subdirectories);
//   while (clientIt.hasNext()) {
//     QString clientFile = clientIt.next();
//     QString relativePath = QDir(userDir).relativeFilePath(clientFile);
//     QString serverFilePath = serverUserDir + "/" + relativePath;
//     RC_ASSERT(QFile::exists(serverFilePath));
//     QFile cf(clientFile), sf(serverFilePath);
//     cf.open(QIODevice::ReadOnly);
//     sf.open(QIODevice::ReadOnly);
//     RC_ASSERT(cf.readAll() == sf.readAll());
//   }
//
//   // assert no extra files on server
//   QDirIterator serverIt(serverUserDir, QDir::Files,
//                         QDirIterator::Subdirectories);
//   while (serverIt.hasNext()) {
//     QString serverFile = serverIt.next();
//     QString relativePath = QDir(serverUserDir).relativeFilePath(serverFile);
//     RC_ASSERT(QFile::exists(userDir + "/" + relativePath));
//   }
// }
