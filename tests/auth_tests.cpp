// AuthTests.cpp
#include "AuthClient.h"
#include "AuthServer.h"
#include "SessionRegistry.h"
#include "UsersDb.h"
#include <gtest/gtest.h>

// Wires one client to the server under a connection id, and captures the
// client's result signals. Heap-allocated only (holds a QObject, captures
// `this` in connections) — never copy or move.
struct TestClient {
  AuthClient client;
  QString token, error, deviceName;
  bool authed = false, failed = false, out = false, deleted = false,registered = false, registerFailed = false;

  TestClient(AuthServer *server, ConnectionId conn) {
    // client -> server: stamp connection, lift token from the stamped message
    QObject::connect(&client, &AuthClient::sendMessage, server,
                     [server, conn](std::shared_ptr<Message> m) {
                       AuthServerInMsgCtx ctx;
                       ctx.connection = conn;
                       ctx.token = m->token; // stampToken put it here
                       server->onMessage(m, ctx);
                     });
    // server -> client: route by connection (this is the anti-spill boundary)
    QObject::connect(
        server, &AuthServer::sendMessage, &client,
        [this, conn](std::shared_ptr<Message> m, AuthServerOutMsgCtx o) {
          if (o.connection != conn)
            return;
          client.onMessage(m);
        });
    QObject::connect(&client, &AuthClient::authenticated,
                     [this](const QString &t) {
                       authed = true;
                       token = t;
                     });
    QObject::connect(&client, &AuthClient::authFailed,
                     [this](const QString &e) {
                       failed = true;
                       error = e;
                     });
    QObject::connect(&client, &AuthClient::loggedOut, [this]() { out = true; });
    QObject::connect(&client, &AuthClient::accountDeleted,
                     [this]() { deleted = true; });
        QObject::connect(&client, &AuthClient::registered,
                     [this]() { registered = true; });
    QObject::connect(&client, &AuthClient::registerFailed,
                     [this](const QString &e) { registerFailed = true; error = e; });
  }
};

class AuthTest : public ::testing::Test {
protected:
  SessionRegistry sessions;
  UsersDb users;
  std::unique_ptr<AuthServer> server;

  void SetUp() override {
    server = std::make_unique<AuthServer>(&sessions, &users);
  }

  std::unique_ptr<TestClient> makeClient(const QString &conn) {
    return std::make_unique<TestClient>(server.get(), conn);
  }
};

TEST_F(AuthTest, FreshClientHasNoToken) {
  auto c = makeClient("conn1");
  EXPECT_FALSE(c->client.hasToken());
  EXPECT_TRUE(c->client.getToken().isEmpty());
}

TEST_F(AuthTest, LoginWithoutRegisterFails) {
  auto c = makeClient("conn1");
  c->client.login("ghost", "pw", "dev1");
  EXPECT_TRUE(c->failed);
  EXPECT_FALSE(c->authed);
  EXPECT_FALSE(c->error.isEmpty());
  EXPECT_FALSE(c->client.hasToken());
}

TEST_F(AuthTest, RegisterThenLoginGrantsToken) {
  auto c = makeClient("conn1");
  c->client.registerAccount("alice", "pw", "dev1");
  EXPECT_FALSE(c->client.hasToken());
  c->client.login("alice", "pw", "dev1");
  EXPECT_TRUE(c->authed);
  EXPECT_TRUE(c->registered);
  EXPECT_FALSE(c->registerFailed);
  EXPECT_FALSE(c->failed);
  EXPECT_TRUE(c->client.hasToken());
}

TEST_F(AuthTest, ClientTokenMatchesServerSession) {
  auto c = makeClient("conn1");
  c->client.registerAccount("alice", "pw", "dev1");
  c->client.login("alice", "pw", "dev1");

  auto session = sessions.getSession(c->client.getToken());
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->username, "alice");
}

TEST_F(AuthTest, TokenDoesNotSpillToOtherClient) {
  auto a = makeClient("connA");
  auto b = makeClient("connB");

  a->client.registerAccount("alice", "pw", "dev1");
  a->client.login("alice", "pw", "dev1");

  EXPECT_TRUE(a->client.hasToken());
  EXPECT_FALSE(b->authed);
  EXPECT_FALSE(b->client.hasToken());
  EXPECT_NE(a->client.getToken(), b->client.getToken()); // b's is empty
}

TEST_F(AuthTest, LogoutClearsSessionAndToken) {
  auto c = makeClient("conn1");
  c->client.registerAccount("alice", "pw", "dev1");
  c->client.login("alice", "pw", "dev1");
  QString tok = c->client.getToken();
  ASSERT_FALSE(tok.isEmpty());

  c->client.logout();
  EXPECT_TRUE(c->out);
  EXPECT_FALSE(c->client.hasToken());
  EXPECT_FALSE(sessions.getSession(tok).has_value());
}

TEST_F(AuthTest, DISABLED_LoginTwiceSameToken) {
  auto c = makeClient("conn1");
  c->client.registerAccount("alice", "pw", "dev1");
  c->client.login("alice", "pw", "dev1");
  QString first = c->client.getToken();
  c->client.login("alice", "pw", "dev1");
  EXPECT_EQ(first, c->client.getToken());
}

TEST_F(AuthTest, PasswordNotStoredPlaintext) {
  auto c = makeClient("conn1");
  c->client.registerAccount("alice", "hunter2", "dev1");
  EXPECT_NE(users.rawStoredPassword("alice"), "hunter2");
}

TEST_F(AuthTest, DeleteAccountRemovesUser) {
  auto c = makeClient("conn1");
  c->client.registerAccount("alice", "pw", "dev1");
  c->client.login("alice", "pw", "dev1");
  ASSERT_TRUE(c->client.hasToken());

  c->client.deleteAccount("pw");
  EXPECT_TRUE(c->deleted);
  EXPECT_FALSE(c->client.hasToken());

  // re-login must now fail, account is gone
  c->failed = false;
  c->authed = false;
  c->client.login("alice", "pw", "dev1");
  EXPECT_TRUE(c->failed);
  EXPECT_FALSE(c->authed);
}
