#include "Messages.h"
#include <QCoreApplication>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <print>
#include "FileServer.h"
int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  FileServer fileServer;
  fileServer.setRootDir("server_root");
  auto defaultServerName = QString("merkle_sync");
  fileServer.listenOn(defaultServerName);

  std::println("Hello World from server");
  return app.exec();
}
