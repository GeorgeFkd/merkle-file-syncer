#pragma once

#include <QByteArray>
#include <QString>
#include <QDebug>

struct ChunkTransfer {
  QString filepath;
  quint32 partNumber;
  quint32 chunkSize;
  QByteArray bytes;
};

struct ACKChunkReceived {
  QString path;
  quint32 partNumber;
  QString failureMsg;
  QString failureType;
};

inline QDebug operator<<(QDebug dbg, const ACKChunkReceived &ack) {
  QDebugStateSaver saver(dbg);
  dbg.nospace() << "ACKChunkReceived(path=" << ack.path
                << ", partNumber=" << ack.partNumber
                << ", failureType=" << ack.failureType
                << ", failureMsg=" << ack.failureMsg << ")";
  return dbg;
}

struct RequestChunkSizeForUpload {
  QString path;
  quint64 fileSize;
};

struct RequestChunkSizeForDownload {
  QString path;
  quint64 chunkSizeDesired;
};

struct SpecifyChunkSizeUpload {
  QString path;
  quint64 chunkSize;
  quint32 totalChunks;
};

struct SpecifyChunkSizeDownload {
  QString path;
  quint64 chunkSize;
  quint32 totalChunks;
};
