#include "FileClient.h"
#include "Messages.h"
#include <QCoreApplication>
#include <QDebug>
#include <QLocalSocket>
#include <print>
int main(int argc, char *argv[]) {
  std::println("Hello world from client");
  QCoreApplication app(argc, argv);
  FileClient fileClient;
  fileClient.configure(FileClientConfig{
      .rootDir = "client_root",
      .username = "foo",
      .password = "bar",
      .manualTick = false,
      .tickIntervalMs = 3000,
      .serverName = "merkle_sync",
  });
  fileClient.start();
  return app.exec();
}
