#include "Messages.h"
#include <QCoreApplication>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <print>
#include "FileServer.h"
int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  QLocalServer server;
  FileServer fileServer;
  auto defaultServerName = QString("merkle_sync");
  QLocalServer::removeServer(defaultServerName);
  server.listen("merkle_sync");
  QObject::connect(&server, &QLocalServer::newConnection, [&]() {
    QLocalSocket *socket = server.nextPendingConnection();
    fileServer.handleConnection(socket);
  });

  std::println("Hello World from server");
  return app.exec();
}
