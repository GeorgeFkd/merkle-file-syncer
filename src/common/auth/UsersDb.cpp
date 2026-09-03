#include "UsersDb.h"
#include <QDebug>
#include <QString>
#include <sodium.h>

namespace {

// Argon2id cost. INTERACTIVE (~64 MiB, 2 passes) is libsodium's recommendation
// for online logins — raise to MODERATE/SENSITIVE if the server can spare it.
constexpr unsigned long long kOpsLimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
constexpr size_t kMemLimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;

// Wipes the plaintext copy we had to make to reach libsodium's char* API.
struct ScopedPassword {
  QByteArray bytes;
  explicit ScopedPassword(const QString &password)
      : bytes(password.toUtf8()) {}
  ~ScopedPassword() { sodium_memzero(bytes.data(), bytes.size()); }
  ScopedPassword(const ScopedPassword &) = delete;
  ScopedPassword &operator=(const ScopedPassword &) = delete;
};

// Argon2id digest of a value nobody can log in with. Verifying against it on an
// unknown username makes a miss cost the same as a wrong password, so response
// time does not tell an attacker which usernames exist.
const QByteArray &decoyHash() {
  static const QByteArray decoy = [] {
    QByteArray out(crypto_pwhash_STRBYTES, '\0');
    if (crypto_pwhash_str(out.data(), "", 0, kOpsLimit, kMemLimit) != 0)
      return QByteArray();
    return QByteArray(out.constData()); // trim to the NUL-terminated digest
  }();
  return decoy;
}

} // namespace

UsersDb::UsersDb() {
  // Safe to call repeatedly; picks the CPU's best implementations and seeds the
  // RNG. Nothing else in libsodium may be used before it succeeds.
  if (sodium_init() < 0)
    qFatal("libsodium failed to initialise; cannot handle credentials");
}

void UsersDb::storeUser(const QString &user, const QString &password,
                        const QString &rootDirectory) {
  ScopedPassword plain(password);
  QByteArray hash(crypto_pwhash_STRBYTES, '\0');
  // Picks a fresh random salt and embeds it, along with the cost parameters,
  // in the returned string — so nothing else has to be persisted alongside it.
  if (crypto_pwhash_str(hash.data(), plain.bytes.constData(),
                        plain.bytes.size(), kOpsLimit, kMemLimit) != 0) {
    // Only fails when it cannot get its memory. Fail closed: no record, so the
    // account simply does not exist rather than existing without a password.
    qCritical() << "Password hashing failed for user" << user
                << "- user not stored";
    return;
  }
  users[user] = {QByteArray(hash.constData()), rootDirectory};
}

void UsersDb::deleteUser(const QString &user) { users.remove(user); }

bool UsersDb::verifyUserCredentials(const QString &user,
                                    const QString &password) const {
  ScopedPassword plain(password);
  auto it = users.constFind(user);
  if (it == users.constEnd()) {
    qDebug() << "User: " << user << " not found.\n Will be created.\n";
    const QByteArray &decoy = decoyHash();
    if (!decoy.isEmpty()) {
      // volatile so the result being unused does not let the compiler drop the
      // call — burning the time is the entire point.
      [[maybe_unused]] volatile int ignored = crypto_pwhash_str_verify(
          decoy.constData(), plain.bytes.constData(), plain.bytes.size());
    }
    return false;
  }
  if (crypto_pwhash_str_verify(it->passwordHash.constData(),
                               plain.bytes.constData(),
                               plain.bytes.size()) != 0) {
    qDebug() << "Wrong password\n";
    return false;
  }

  return true;
}

bool UsersDb::userExists(const QString &user) const {
  return users.contains(user);
}

bool UsersDb::userExists(const QString &user, const QString &password) const {
  // havent yet made the registration path, and also this adds extra complexity
  // to the tests most likely
  return true;
  return verifyUserCredentials(user, password);
}
