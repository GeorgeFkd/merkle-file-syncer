// test_chunking_roundtrip.cpp
#include "ChunkingClient.h"
#include "ChunkingProtocolMessages.h"
#include "ChunkingServer.h"
#include "Messages.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QRandomGenerator>
#include <QString>

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cstdint>
#include <vector>

namespace {

double uniform01() { return QRandomGenerator::global()->generateDouble(); }
QByteArray corrupt(const QByteArray &in, int i) {
  if (in.isEmpty())
    return in;

  QByteArray out = in;                       // detached copy
  out[i] = static_cast<char>(out[i] ^ 0xFF); // flip all bits
  return out;
}
struct FailureInjector {
  bool enabled;
  float probabilityBase; // was clientProbabilityOfFailure /
                         // serverProbabilityOfFailure

  // returns true if this ChunkTransfer should be corrupted
  bool shouldCorrupt(const Message *msg) {
    if (!enabled || msg->type() != MessageType::ChunkTransfer)
      return false;
    float probability = 1.0f / probabilityBase;
    probabilityBase *= 2;
    return uniform01() < probability;
  }
};

using ClientId = QString;

// In-memory file store used as the backing "filesystem" for either side.
// Provides the reader / metadata-reader callbacks the API expects, plus a
// write handler for incoming chunks. Client- and server-shaped variants differ
// only by the leading ClientId the server callbacks carry.
class StorageMock {
public:
  void putFile(const QString &path, const QByteArray &bytes) {
    files[path] = bytes;
  }
  QByteArray file(const QString &path) const { return files.value(path); }
  void clearFile(const QString &path) { files[path].clear(); }

  // --- client-shaped callbacks (no ClientId) ---
  ChunkingClient::ChunkReader clientReader() {
    return [this](const QString &p, quint64 offset, quint64 length) {
      return files[p].mid(static_cast<int>(offset), static_cast<int>(length));
    };
  }
  ChunkingClient::MetadataReader clientMetadataReader() {
    return [this](const QString &p) {
      return static_cast<quint64>(files[p].size());
    };
  }

  // --- server-shaped callbacks (leading ClientId, ignored here) ---
  ChunkingServer::ChunkReader serverReader() {
    return [this](const ClientId &, const QString &p, quint64 offset,
                  quint64 length) {
      return files[p].mid(static_cast<int>(offset), static_cast<int>(length));
    };
  }
  ChunkingServer::MetadataReader serverMetadataReader() {
    return [this](const ClientId &, const QString &p) {
      return static_cast<quint64>(files[p].size());
    };
  }

  void applyWrite(const WriteCommand &w) {
    applyBytes(w.path, w.offset, w.bytes);
  }
  void applyWrite(const ServerWriteCommand &w) {
    applyBytes(w.path, w.offset, w.bytes);
  }

private:
  void applyBytes(const QString &path, quint64 offset,
                  const QByteArray &bytes) {
    const quint64 end = offset + static_cast<quint64>(bytes.size());
    QByteArray &buf = files[path];
    if (static_cast<quint64>(buf.size()) < end)
      buf.resize(static_cast<int>(end));
    for (int i = 0; i < bytes.size(); ++i)
      buf[static_cast<int>(offset) + i] = bytes[i];
  }

  QHash<QString, QByteArray> files;
};

// === How a client and server are wired together (the API-usage showcase) ===
//
// In production these signals cross a socket. Here each client "send request"
// signal is connected straight to the matching server handler and vice versa,
// so one emission synchronously drives the other side.
//
// Two things this makes explicit about the API:
//  * The client's outbound signals carry no ClientId (the client doesn't know
//    it) -> the transport must attach it. The lambdas capture a fixed clientId
//    and inject it into every server handler call.
//  * The server's outbound signals DO carry a ClientId as their first arg; the
//    client handlers don't take it, so the lambdas drop it.
//
// All connections are DirectConnection: WriteCommand passes a QByteArray by
// reference, valid only during synchronous dispatch, so queued delivery would
// dangle.
//

void wireClientServer(ChunkingClient &client, ChunkingServer &server,
                      const ClientId &clientId, bool injectFailures) {
  // client -> server: client sends context-less; inject clientId for the server
  auto clientInj =
      std::make_shared<FailureInjector>(FailureInjector{injectFailures, 1.0f});
  auto serverInj =
      std::make_shared<FailureInjector>(FailureInjector{injectFailures, 1.0f});
  auto clientProbabilityOfFailure = std::make_shared<float>(1.0);
  QObject::connect(
      &client, &ChunkingClient::sendMessage, &server,
      [&server, clientId, injectFailures, clientInj,
       clientProbabilityOfFailure](std::shared_ptr<Message> msg) {
        if (injectFailures) {
          if (clientInj->shouldCorrupt(msg.get())) {
            auto ct = static_cast<ChunkTransfer *>(msg.get());
            ct->bytes = corrupt(ct->bytes, 0);
          }
        }
        server.onMessage(msg, ChunkingServerInMsgCtx{clientId});
      },
      Qt::DirectConnection);

  // server -> client: server sends with clientId; client ignores it
  auto serverProbabilityOfFailure = std::make_shared<float>(1.0);
  QObject::connect(
      &server, &ChunkingServer::sendMessage, &client,
      [&client, injectFailures, serverInj, serverProbabilityOfFailure](
          std::shared_ptr<Message> msg, ChunkingServerOutMsgCtx msgCtx) {
        if (injectFailures) {
          if (serverInj->shouldCorrupt(msg.get())) {
            auto ct = static_cast<ChunkTransfer *>(msg.get());
            ct->bytes = corrupt(ct->bytes, 0);
          }
        }
        client.onMessage(msg);
      },
      Qt::DirectConnection);
}

} // namespace

RC_GTEST_PROP(ChunkRoundTrip, uploadThenDownloadPreservesBytes, ()) {
  // fileSize >= 1: a 0-byte upload never completes (client skips sending when
  // totalChunks == 0, so uploadCompleted never fires). Excluded until the
  // empty-file case is handled explicitly.
  // Bounds kept modest because upload is stop-and-wait over direct
  // connections, so each part nests one recursion level (depth ~ part count).
  const int fileSize = *rc::gen::inRange(1, 1001);
  const int chunkSize = *rc::gen::inRange(1, 1001);
  const auto vec = *rc::gen::container<std::vector<uint8_t>>(
      static_cast<std::size_t>(fileSize), rc::gen::arbitrary<uint8_t>());

  const QString path = "file.bin";
  const ClientId clientId = "client-1";
  const QByteArray original(reinterpret_cast<const char *>(vec.data()),
                            static_cast<int>(vec.size()));

  StorageMock clientStore;
  StorageMock serverStore;
  clientStore.putFile(path, original); // the file we upload

  ChunkingClient client;
  ChunkingServer server;

  // Wire both sides' readers, writers, and the server's chunk-size calculator.
  auto setupReadsWritesClientServer = [chunkSize](ChunkingClient &client,
                                                  ChunkingServer &server,
                                                  StorageMock &clientStore,
                                                  StorageMock &serverStore) {
    // client side: read source, report size, apply downloaded chunks
    client.setReader(clientStore.clientReader());
    client.setMetadataReader(clientStore.clientMetadataReader());
    QObject::connect(
        &client, &ChunkingClient::writeRequested, &client,
        [&clientStore](const WriteCommand &w) { clientStore.applyWrite(w); },
        Qt::DirectConnection);

    // server side: read source, report size, apply uploaded chunks
    server.setReader(serverStore.serverReader());
    server.setMetadataReader(serverStore.serverMetadataReader());
    QObject::connect(
        &server, &ChunkingServer::chunkToUploadArrived, &server,
        [&serverStore](const ServerWriteCommand &w) {
          serverStore.applyWrite(w);
        },
        Qt::DirectConnection);

    // server picks chunk size, honoring the client's hint when present
    server.setChunkSizeCalculator([chunkSize](const ClientId &, const QString &,
                                              quint64 /*fileSize*/,
                                              quint64 desired) -> quint64 {
      const quint64 cs =
          desired != 0 ? desired : static_cast<quint64>(chunkSize);
      return cs == 0 ? 1 : cs;
    });
  };

  setupReadsWritesClientServer(client, server, clientStore, serverStore);

  bool uploadDone = false;
  bool downloadDone = false;
  QObject::connect(&client, &ChunkingClient::uploadCompleted, &client,
                   [&uploadDone](const QString &) { uploadDone = true; });
  QObject::connect(&client, &ChunkingClient::downloadCompleted, &client,
                   [&downloadDone](const QString &) { downloadDone = true; });

  wireClientServer(client, server, clientId, true);

  client.startUpload(path);
  qDebug() << "upload: orig" << original.size() << "server"
           << serverStore.file(path).size();
  RC_ASSERT(uploadDone);
  RC_ASSERT(serverStore.file(path).size() == original.size()); // size first
  RC_ASSERT(serverStore.file(path) == original);               // then content

  clientStore.clearFile(path);
  client.startDownload(path, static_cast<quint64>(chunkSize));
  qDebug() << "download: orig" << original.size() << "client"
           << clientStore.file(path).size();
  RC_ASSERT(downloadDone);
  RC_ASSERT(clientStore.file(path).size() == original.size());
  RC_ASSERT(clientStore.file(path) == original);
}
