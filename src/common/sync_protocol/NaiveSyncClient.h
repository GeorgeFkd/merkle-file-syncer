#include "FSMetadata.h"
#include "Messages.h"
#include <QObject>

class NaiveSyncClient : public QObject {
  Q_OBJECT
public:
  explicit NaiveSyncClient(QObject *parent = nullptr);

  void startNegotiation();
  void handleListingResponse(ListResponseMessage *,const FSMetadata* updatedDb,const QString& username);
  const NegotiationState *getNegotiationState() const;

Q_SIGNALS:
  void sendMessage(ListRequestMessage);
  void negotiationCompleted(const NegotiationState &state);

private:
  bool negotiationInProgress = false;
  NegotiationState negotiationState;
};
