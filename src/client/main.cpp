#include "Messages.h"
#include <QCoreApplication>
#include <QDebug>
#include <QLocalSocket>
#include <print>
#include "FileClient.h"
int main(int argc, char *argv[]) {
  std::println("Hello world from client");
  QCoreApplication app(argc, argv);
  FileClient fileClient;
  fileClient.init();
  fileClient.connectToServer("merkle_sync");

  return app.exec();
}
