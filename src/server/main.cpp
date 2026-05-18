#include "FileServer.h"
#include "FileStorage.h"
#include "LocalFileStorage.h"
#include "Messages.h"
#include <QCoreApplication>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <print>
int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  FileServer fileServer;
  auto storage = std::make_unique<LocalFileStorage>();
  storage->setRoot("server_root");
  auto defaultServerName = QString("merkle_sync");
  fileServer.configure(FileServerConfig{.serverName = defaultServerName,
                                        .storage = std::move(storage)});
  fileServer.start();
  std::println("Hello World from server");
  return app.exec();
}
