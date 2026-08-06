#include "Hasher.h"

#include <QCryptographicHash>

namespace {
constexpr auto Algo = QCryptographicHash::Sha256;
}

QByteArray Hasher::hash(const QByteArray &data)
{
    return QCryptographicHash::hash(data, Algo);
}
