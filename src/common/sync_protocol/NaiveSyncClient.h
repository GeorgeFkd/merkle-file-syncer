#include "FSMetadata.h"
#include "Messages.h"
#include <QObject>

class NaiveSyncClient : public QObject {
  Q_OBJECT
public:
  explicit NaiveSyncClient(QObject *parent = nullptr);

  void startNegotiation();
  void onMessage(std::shared_ptr<ListResponseMessage> ,const FSMetadata* updatedDb,const QString& username);
  const NegotiationState *getNegotiationState() const;

Q_SIGNALS:
  void sendMessage(std::shared_ptr<ListRequestMessage>);
  void negotiationCompleted(const NegotiationState &state);

private:
  bool negotiationInProgress = false;
  NegotiationState negotiationState;
};
