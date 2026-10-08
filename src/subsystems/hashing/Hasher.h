#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <span>

class Hasher {
public:
  static QByteArray hash(const QByteArray &data);
  template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>,
                                 QByteArrayView>
  static QByteArray hash(R &&chunks) {
    {
      QCryptographicHash h(QCryptographicHash::Sha256);
      for (QByteArrayView chunk : chunks)
        h.addData(chunk);
      return h.result();
    }
  }
};
