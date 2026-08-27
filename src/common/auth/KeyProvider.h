#pragma once
#include <QByteArray>
#include <optional>

struct SigningKeys {
  QByteArray privatePem;
  QByteArray publicPem;
};

// Non-optional: qFatal aborts before return if keys are absent.
SigningKeys loadSigningKeys();
