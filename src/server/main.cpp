#include "Messages.h"
#include <QCoreApplication>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <print>
#include "FileServer.h"
#include "FileStorage.h"
#include "LocalFileStorage.h"
int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  FileServer fileServer;
  //this method should be removed
  auto storage = std::make_unique<LocalFileStorage>();
  storage->setRoot("server_root");
  fileServer.setFileStorageImpl(std::move(storage));
  auto defaultServerName = QString("merkle_sync");
  fileServer.listenOn(defaultServerName);

  std::println("Hello World from server");
  return app.exec();
}
