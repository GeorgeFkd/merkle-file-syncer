#pragma once
#include "ChunkingClient.h"
#include "FileStorage.h"
#include "Messages.h"
#include <QObject>
#include <functional>
#include <memory>

class FileTransferClient : public QObject {
  Q_OBJECT
public:
  // this will become a signal

  FileTransferClient(FileStorage *storage,
                     const QString &user, QObject *parent = nullptr);

  void startUpload(const QString &path);
  void startDownload(const QString &path, quint64 desiredChunkSize);
  void cancelUpload(const QString &path);
  void cancelDownload(const QString &path);

  void onMessage(const Message*);

Q_SIGNALS:
	void uploadCompleted(QString path);
	void downloadCompleted(QString path);
	void uploadCancelled(QString path);
	void downloadCancelled(QString path);
	void sendMessage(std::shared_ptr<Message> msg);
	void uploadProgress(QString path,quint32 currentPart,quint32 totalParts);
	void downloadProgress(QString path, quint32 currentPart,quint32 totalParts);
private:
	void wireProtocolToTransport();
	void wireProtocolToStorage();

	FileStorage *storage;
	QString user;
	ChunkingClient chunking;
	
};
