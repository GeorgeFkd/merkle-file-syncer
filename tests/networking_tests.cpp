#include "ClientTransport.h"
#include "ServerTransport.h"
#include "LocalClientTransport.h"
#include "LocalServerTransport.h"
#include "TcpClientTransport.h"
#include "TcpServerTransport.h"
#include "TlsClientTransport.h"
#include "TlsServerTransport.h"

#include "Messages.h"

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QUuid>
#include <memory>
#include <vector>

// ---- helpers ----

// Spin the event loop until `pred` is true or timeout. Returns pred's final value.
template <typename Pred>
static bool waitUntil(Pred pred, int timeoutMs = 2000) {
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return pred();
}

// ---- pairing tags: each knows how to build a client, a server, and an endpoint ----

struct LocalTag {
  static std::unique_ptr<ServerTransport> makeServer() {
    return std::make_unique<LocalServerTransport>();
  }
  static std::unique_ptr<ClientTransport> makeClient() {
    return std::make_unique<LocalClientTransport>();
  }
  // unique per-test name so parallel runs / stale sockets don't collide
  static QString endpoint() {
    return "neteng_test_" + QUuid::createUuid().toString(QUuid::Id128);
  }
};

struct TcpTag {
  static std::unique_ptr<ServerTransport> makeServer() {
    return std::make_unique<TcpServerTransport>();
  }
  static std::unique_ptr<ClientTransport> makeClient() {
    return std::make_unique<TcpClientTransport>();
  }
  // port 0 => OS picks a free port; real endpoint read back via endpoint() after start()
  static QString endpoint() { return "127.0.0.1:0"; }
};

struct TlsTag {
  static std::unique_ptr<ServerTransport> makeServer() {
    return std::make_unique<TlsServerTransport>();
  }
  static std::unique_ptr<ClientTransport> makeClient() {
    return std::make_unique<TlsClientTransport>();
  }
  // port 0 => OS picks a free port; SAN in the test cert covers 127.0.0.1
  static QString endpoint() { return "127.0.0.1:0"; }
};

template <typename Tag>
class TransportTest : public ::testing::Test {
protected:
  std::unique_ptr<ServerTransport> server;

  void SetUp() override {
    server = Tag::makeServer();
    server->configure(Tag::endpoint());
    server->start();
    ASSERT_TRUE(server->isListening());
  }

  // Connect a fresh client to the running server.
  // Captures the server-side connection pointer (from newConnection) into *serverSideConn.
  std::unique_ptr<ClientTransport> connectClient(QIODevice **serverSideConn = nullptr) {
    auto c = Tag::makeClient();

    QIODevice *captured = nullptr;
    QObject::connect(server.get(), &ServerTransport::newConnection, server.get(),
                     [&](QIODevice *conn) { captured = conn; });

    bool connectedFired = false;
    QObject::connect(c.get(), &ClientTransport::connected, c.get(),
                     [&]() { connectedFired = true; });

    c->configure(server->endpoint()); // resolved endpoint (real port for TCP)
    c->connectToServer();

    EXPECT_TRUE(waitUntil([&] { return connectedFired && captured != nullptr; }));
    if (serverSideConn) *serverSideConn = captured;
    return c;
  }
};

using Tags = ::testing::Types<LocalTag, TcpTag,TlsTag>;
TYPED_TEST_SUITE(TransportTest, Tags);

// ---- lifecycle ----

TYPED_TEST(TransportTest, ServerListensAfterStart) {
  EXPECT_TRUE(this->server->isListening());
  EXPECT_FALSE(this->server->endpoint().isEmpty());
}

TYPED_TEST(TransportTest, ClientConnectFiresConnectedAndNewConnection) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);
  EXPECT_NE(serverConn, nullptr);
}

// ---- round trip: client -> server ----

TYPED_TEST(TransportTest, ClientToServerRoundTrip) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);

  std::vector<std::shared_ptr<Message>> received;
  QObject::connect(this->server.get(), &ServerTransport::messageReady,
                   this->server.get(),
                   [&](QIODevice *, std::shared_ptr<Message> m) {
                     received.push_back(m);
                   });

  auto msg = std::make_shared<AuthMessage>();
  msg->username = "alice";
  msg->password = "pw";
  msg->deviceName = "dev";
  client->send(msg);

  ASSERT_TRUE(waitUntil([&] { return !received.empty(); }));
  ASSERT_TRUE(received[0]);
  EXPECT_EQ(received[0]->type(), MessageType::ClientAuth);
  auto *auth = static_cast<AuthMessage *>(received[0].get());
  EXPECT_EQ(auth->username, QString("alice"));
  EXPECT_EQ(auth->deviceName, QString("dev"));
}

// ---- round trip: server -> client ----

TYPED_TEST(TransportTest, ServerToClientRoundTrip) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);
  ASSERT_NE(serverConn, nullptr);

  std::vector<std::shared_ptr<Message>> received;
  QObject::connect(client.get(), &ClientTransport::messageReady, client.get(),
                   [&](std::shared_ptr<Message> m) { received.push_back(m); });

  auto resp = std::make_shared<AuthResponseMessage>();
  resp->success = true;
  resp->token = "tok-123";
  this->server->send(serverConn, resp);

  ASSERT_TRUE(waitUntil([&] { return !received.empty(); }));
  ASSERT_TRUE(received[0]);
  EXPECT_EQ(received[0]->type(), MessageType::ServerAuthResponse);
  auto *r = static_cast<AuthResponseMessage *>(received[0].get());
  EXPECT_TRUE(r->success);
  EXPECT_EQ(r->token, QString("tok-123"));
}

// ---- serde-through-the-wire: a message with bytes + datetime + nested arrays ----

TYPED_TEST(TransportTest, MerkleMessageRoundTripsThroughWire) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);

  std::vector<std::shared_ptr<Message>> received;
  QObject::connect(this->server.get(), &ServerTransport::messageReady,
                   this->server.get(),
                   [&](QIODevice *, std::shared_ptr<Message> m) {
                     received.push_back(m);
                   });

  auto msg = std::make_shared<MerkleSyncMessage>();
  msg->depth = 3;
  msg->phase = 1;
  msg->rootHash = QByteArray::fromHex("deadbeef");
  MerkleEntry e;
  e.path = "dir/file.txt";
  e.hash = QByteArray::fromHex("cafe");
  e.mtime = QDateTime::fromString("2026-01-02T03:04:05.678", Qt::ISODateWithMs);
  e.filetype = FileType::File;
  msg->fileEntriesPerChild.append({"dir", {e}});
  client->send(msg);

  ASSERT_TRUE(waitUntil([&] { return !received.empty(); }));
  ASSERT_EQ(received[0]->type(), MessageType::MerkleSync);
  auto *m = static_cast<MerkleSyncMessage *>(received[0].get());
  EXPECT_EQ(m->depth, 3);
  EXPECT_EQ(m->rootHash, QByteArray::fromHex("deadbeef"));
  ASSERT_EQ(m->fileEntriesPerChild.size(), 1);
  ASSERT_EQ(m->fileEntriesPerChild[0].second.size(), 1);
  const auto &got = m->fileEntriesPerChild[0].second[0];
  EXPECT_EQ(got.path, QString("dir/file.txt"));
  EXPECT_EQ(got.hash, QByteArray::fromHex("cafe"));
  EXPECT_EQ(got.mtime,
            QDateTime::fromString("2026-01-02T03:04:05.678", Qt::ISODateWithMs));
}

// ---- framing: two messages back to back arrive as two, in order ----

TYPED_TEST(TransportTest, TwoMessagesArriveSeparately) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);

  std::vector<QString> names;
  QObject::connect(this->server.get(), &ServerTransport::messageReady,
                   this->server.get(),
                   [&](QIODevice *, std::shared_ptr<Message> m) {
                     names.push_back(static_cast<AuthMessage *>(m.get())->username);
                   });

  auto m1 = std::make_shared<AuthMessage>(); m1->username = "one"; m1->deviceName = "d";
  auto m2 = std::make_shared<AuthMessage>(); m2->username = "two"; m2->deviceName = "d";
  client->send(m1);
  client->send(m2);

  ASSERT_TRUE(waitUntil([&] { return names.size() >= 2; }));
  EXPECT_EQ(names.size(), 2u);
  EXPECT_EQ(names[0], QString("one"));
  EXPECT_EQ(names[1], QString("two")); // order preserved
}

// ---- framing: a large message (bigger than one socket read) reassembles ----

TYPED_TEST(TransportTest, LargeMessageReassembles) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);

  std::vector<std::shared_ptr<Message>> received;
  QObject::connect(this->server.get(), &ServerTransport::messageReady,
                   this->server.get(),
                   [&](QIODevice *, std::shared_ptr<Message> m) {
                     received.push_back(m);
                   });

  auto big = std::make_shared<AuthMessage>();
  big->username = QString(2 * 1024 * 1024, QChar('x')); // ~2MB, exceeds one read
  big->deviceName = "d";
  client->send(big);

  ASSERT_TRUE(waitUntil([&] { return !received.empty(); }, 5000));
  auto *a = static_cast<AuthMessage *>(received[0].get());
  EXPECT_EQ(a->username.size(), big->username.size());
}

// ---- framing: a burst of many messages all arrive, in order ----

TYPED_TEST(TransportTest, BurstOfMessagesAllArriveInOrder) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);

  std::vector<QString> names;
  QObject::connect(this->server.get(), &ServerTransport::messageReady,
                   this->server.get(),
                   [&](QIODevice *, std::shared_ptr<Message> m) {
                     names.push_back(static_cast<AuthMessage *>(m.get())->username);
                   });

  const int N = 50;
  for (int i = 0; i < N; ++i) {
    auto m = std::make_shared<AuthMessage>();
    m->username = QString::number(i);
    m->deviceName = "d";
    client->send(m);
  }

  ASSERT_TRUE(waitUntil([&] { return names.size() >= N; }, 5000));
  ASSERT_EQ(names.size(), static_cast<size_t>(N));
  for (int i = 0; i < N; ++i) {
    EXPECT_EQ(names[i], QString::number(i));
  }
}

// ---- multi-connection: distinct connections, and server routes to the right one ----

TYPED_TEST(TransportTest, ServerRoutesToCorrectConnection) {
  QIODevice *connA = nullptr;
  auto clientA = this->connectClient(&connA);
  QIODevice *connB = nullptr;
  auto clientB = this->connectClient(&connB);

  ASSERT_NE(connA, nullptr);
  ASSERT_NE(connB, nullptr);
  EXPECT_NE(connA, connB); // distinct connections

  std::vector<std::shared_ptr<Message>> rxA, rxB;
  QObject::connect(clientA.get(), &ClientTransport::messageReady, clientA.get(),
                   [&](std::shared_ptr<Message> m) { rxA.push_back(m); });
  QObject::connect(clientB.get(), &ClientTransport::messageReady, clientB.get(),
                   [&](std::shared_ptr<Message> m) { rxB.push_back(m); });

  auto resp = std::make_shared<AuthResponseMessage>();
  resp->success = true;
  resp->token = "for-A";
  this->server->send(connA, resp);

  ASSERT_TRUE(waitUntil([&] { return !rxA.empty(); }));
  EXPECT_EQ(rxA.size(), 1u);
  EXPECT_EQ(rxB.size(), 0u); // B did not receive A's message
}

// ---- server sees a message tagged with the sending connection ----

TYPED_TEST(TransportTest, ServerReceivesMessageTaggedWithSenderConnection) {
  QIODevice *connA = nullptr;
  auto clientA = this->connectClient(&connA);
  QIODevice *connB = nullptr;
  auto clientB = this->connectClient(&connB);

  std::vector<QPair<QIODevice *, QString>> received;
  QObject::connect(this->server.get(), &ServerTransport::messageReady,
                   this->server.get(),
                   [&](QIODevice *conn, std::shared_ptr<Message> m) {
                     received.push_back({conn, static_cast<AuthMessage *>(m.get())->username});
                   });

  auto fromA = std::make_shared<AuthMessage>(); fromA->username = "A"; fromA->deviceName = "d";
  clientA->send(fromA);
  ASSERT_TRUE(waitUntil([&] { return received.size() >= 1; }));
  EXPECT_EQ(received[0].first, connA);
  EXPECT_EQ(received[0].second, QString("A"));

  auto fromB = std::make_shared<AuthMessage>(); fromB->username = "B"; fromB->deviceName = "d";
  clientB->send(fromB);
  ASSERT_TRUE(waitUntil([&] { return received.size() >= 2; }));
  EXPECT_EQ(received[1].first, connB);
  EXPECT_EQ(received[1].second, QString("B"));
}

// ---- disconnect: client disconnect surfaces on the server ----

TYPED_TEST(TransportTest, ClientDisconnectSurfacesOnServer) {
  QIODevice *serverConn = nullptr;
  auto client = this->connectClient(&serverConn);

  bool serverSawDisconnect = false;
  QObject::connect(this->server.get(), &ServerTransport::disconnected,
                   this->server.get(),
                   [&](QIODevice *conn) {
                     if (conn == serverConn) serverSawDisconnect = true;
                   });

  client.reset(); // destroy the client -> its socket closes

  EXPECT_TRUE(waitUntil([&] { return serverSawDisconnect; }));
}
