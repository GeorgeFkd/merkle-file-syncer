#include "KeyProvider.h"
#include <QFile>

// Swap this body to change backend (env path / file / KMS). Returns nullopt
// on any failure — loadSigningKeys decides that absence is fatal.
static std::optional<QByteArray> readPemFromEnvPath(const char *envVar) {
  QByteArray path = qgetenv(envVar);
  if (path.isEmpty())
    return std::nullopt;
  QFile f(QString::fromLocal8Bit(path));
  if (!f.open(QIODevice::ReadOnly))
    return std::nullopt;
  QByteArray pem = f.readAll();
  if (pem.isEmpty())
    return std::nullopt;
  return pem;
}

SigningKeys loadSigningKeys() {
  auto priv = readPemFromEnvPath("JWT_PRIVATE_KEY_PATH");
  auto pub = readPemFromEnvPath("JWT_PUBLIC_KEY_PATH");
  if (!priv || !pub)
    qFatal("signing keys unavailable; refusing to start");
  return {*priv, *pub};
}
