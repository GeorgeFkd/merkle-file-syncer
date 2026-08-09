#pragma once
#include "Messages.h"
#include <QByteArray>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

enum class TransferPhase {
  NOT_STARTED,
  NEGOTIATING,
  ACTIVE,
  COMPLETED,
  FAILED
};

class TransferProgress {
public:
  // 1-indexed
  quint32 currentPartNumber = 0;
  quint32 totalParts = 0;
  quint64 chunkSize = 0;
  TransferPhase currentPhase = TransferPhase::NOT_STARTED;

  bool isComplete() const;
  quint8 progressPercent() const;
  bool recordConfirmedPart(quint32 partNumber);
};

bool checkHashMatchesThatOfContent(const QByteArray &hash,
                                   const QByteArray &content);
QByteArray hashContents(const QByteArray& content);
