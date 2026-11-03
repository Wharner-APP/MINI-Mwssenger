#include "crypto/IdentityKeys.h"

#include "crypto/Kdf.h"
#include "crypto/Sodium.h"

#include <QFile>
#include <QSaveFile>
#include <QSet>

#include <sodium.h>

#include <cstring>

namespace mini::crypto {

namespace {

constexpr char kKeysMagic[8] = {'M', 'I', 'N', 'I', 'K', 'B', '0', '1'};
constexpr quint8 kKeysVersion = 1;
constexpr quint8 kEntryIdentity = 1;
constexpr quint8 kEntrySignedPrekey = 2;
constexpr quint8 kEntryOneTimePrekey = 3;
constexpr int kKeysHeaderBytes = 11;   // magic + version + count
constexpr int kEntryHeaderBytes = 31;  // id + type + len + nonce
constexpr int kMaxEntryPlainBytes = 4096;
constexpr qint64 kMaxKeysFileBytes = 64 * 1024;

QByteArray deepCopy(const QByteArray &source)
{
    if (source.isEmpty()) {
        return {};
    }
    return QByteArray(source.constData(), source.size());
}

void wipeBytes(QByteArray &bytes)
{
    if (!bytes.isEmpty()) {
        sodium_memzero(bytes.data(), static_cast<std::size_t>(bytes.size()));
    }
}

void appendU16(QByteArray &out, quint16 value)
{
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
}

void appendU32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(static_cast<char>((value >> shift) & 0xff));
    }
}

void appendU64(QByteArray &out, quint64 value)
{
    for (int shift = 0; shift < 64; shift += 8) {
        out.append(static_cast<char>((value >> shift) & 0xff));
    }
}

quint16 readU16(const unsigned char *p)
{
    return static_cast<quint16>(p[0]) | static_cast<quint16>(p[1] << 8);
}

quint32 readU32(const unsigned char *p)
{
    return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8)
        | (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
}

quint64 readU64(const unsigned char *p)
{
    quint64 value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | static_cast<quint64>(p[i]);
    }
    return value;
}

QByteArray entryAad(const QString &deviceId, quint16 entryId, quint16 count)
{
    QByteArray aad = QByteArrayLiteral("mini-keys");
    aad.append(deviceId.toUtf8());
    appendU16(aad, entryId);
    appendU16(aad, count);
    return aad;
}

Result<QByteArray> sealEntry(const QByteArray &plain, const QByteArray &keyWrap,
                             const QByteArray &aad)
{
    if (keyWrap.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_bad_wrap"));
    }
    QByteArray nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, Qt::Uninitialized);
    randombytes_buf(nonce.data(), static_cast<std::size_t>(nonce.size()));

    QByteArray cipher(plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES,
                      Qt::Uninitialized);
    unsigned long long cipherLen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        reinterpret_cast<unsigned char *>(cipher.data()), &cipherLen,
        reinterpret_cast<const unsigned char *>(plain.constData()),
        static_cast<unsigned long long>(plain.size()),
        reinterpret_cast<const unsigned char *>(aad.constData()),
        static_cast<unsigned long long>(aad.size()), nullptr,
        reinterpret_cast<const unsigned char *>(nonce.constData()),
        reinterpret_cast<const unsigned char *>(keyWrap.constData()));
    if (rc != 0) {
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_seal"));
    }
    cipher.resize(static_cast<int>(cipherLen));

    QByteArray out;
    out.reserve(nonce.size() + cipher.size());
    out.append(nonce);
    out.append(cipher);
    return Result<QByteArray>::ok(out);
}

Result<QByteArray> openEntry(const QByteArray &sealed, const QByteArray &keyWrap,
                             const QByteArray &aad, int plainLen)
{
    if (keyWrap.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_bad_wrap"));
    }
    if (sealed.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES + plainLen
                                + crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_unreadable"));
    }
    const auto *nonce = reinterpret_cast<const unsigned char *>(sealed.constData());
    const auto *cipher = nonce + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    const unsigned long long cipherLen =
        static_cast<unsigned long long>(plainLen + crypto_aead_xchacha20poly1305_ietf_ABYTES);

    QByteArray plain(plainLen, Qt::Uninitialized);
    unsigned long long plainOut = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char *>(plain.data()), &plainOut, nullptr, cipher,
        cipherLen, reinterpret_cast<const unsigned char *>(aad.constData()),
        static_cast<unsigned long long>(aad.size()), nonce,
        reinterpret_cast<const unsigned char *>(keyWrap.constData()));
    if (rc != 0 || plainOut != static_cast<unsigned long long>(plainLen)) {
        wipeBytes(plain);
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_unreadable"));
    }
    return Result<QByteArray>::ok(plain);
}

Result<QByteArray> edSign(const QByteArray &message, const QByteArray &secretKey)
{
    if (secretKey.size() != crypto_sign_SECRETKEYBYTES) {
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_invalid"));
    }
    QByteArray signature(crypto_sign_BYTES, Qt::Uninitialized);
    unsigned long long signatureLen = 0;
    const int rc = crypto_sign_detached(
        reinterpret_cast<unsigned char *>(signature.data()), &signatureLen,
        reinterpret_cast<const unsigned char *>(message.constData()),
        static_cast<unsigned long long>(message.size()),
        reinterpret_cast<const unsigned char *>(secretKey.constData()));
    if (rc != 0 || signatureLen != crypto_sign_BYTES) {
        return Result<QByteArray>::error(QStringLiteral("crypto.sign_failed"));
    }
    return Result<QByteArray>::ok(signature);
}

quint32 randomNonZeroU32()
{
    const QByteArray bytes = Sodium::randomBytes(4);
    if (bytes.size() != 4) {
        return 0;
    }
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData());
    return readU32(p) == 0 ? 1 : readU32(p);
}

quint32 uniqueKeyId(QSet<quint32> &used)
{
    for (int attempt = 0; attempt < 64; ++attempt) {
        const quint32 id = randomNonZeroU32();
        if (id != 0 && !used.contains(id)) {
            used.insert(id);
            return id;
        }
    }
    // Deterministic fallback keeps generation total; collisions are impossible
    // in practice (32-bit ids, at most 21 keys per device).
    quint32 id = 1;
    while (used.contains(id)) {
        ++id;
    }
    used.insert(id);
    return id;
}

X25519KeyPair generateX25519()
{
    X25519KeyPair pair;
    pair.publicKey = QByteArray(crypto_box_PUBLICKEYBYTES, Qt::Uninitialized);
    pair.privateKey = QByteArray(crypto_box_SECRETKEYBYTES, Qt::Uninitialized);
    if (crypto_box_keypair(reinterpret_cast<unsigned char *>(pair.publicKey.data()),
                           reinterpret_cast<unsigned char *>(pair.privateKey.data()))
        != 0) {
        return {};
    }
    return pair;
}

} // namespace

bool Ed25519KeyPair::isValid() const
{
    return publicKey.size() == crypto_sign_PUBLICKEYBYTES
        && privateKey.size() == crypto_sign_SECRETKEYBYTES;
}

bool X25519KeyPair::isValid() const
{
    return publicKey.size() == crypto_box_PUBLICKEYBYTES
        && privateKey.size() == crypto_box_SECRETKEYBYTES;
}

bool SignedPrekey::isValid() const
{
    return id != 0 && pair.isValid() && signature.size() == crypto_sign_BYTES;
}

bool OneTimePrekey::isValid() const
{
    return id != 0 && pair.isValid();
}

IdentityState::IdentityState(const IdentityState &other)
{
    *this = other;
}

IdentityState &IdentityState::operator=(const IdentityState &other)
{
    if (this == &other) {
        return *this;
    }
    wipe();
    identity.publicKey = deepCopy(other.identity.publicKey);
    identity.privateKey = deepCopy(other.identity.privateKey);
    signedPrekey.id = other.signedPrekey.id;
    signedPrekey.pair.publicKey = deepCopy(other.signedPrekey.pair.publicKey);
    signedPrekey.pair.privateKey = deepCopy(other.signedPrekey.pair.privateKey);
    signedPrekey.signature = deepCopy(other.signedPrekey.signature);
    signedPrekey.createdAtMs = other.signedPrekey.createdAtMs;
    oneTimePrekeys.clear();
    oneTimePrekeys.reserve(other.oneTimePrekeys.size());
    for (const OneTimePrekey &prekey : other.oneTimePrekeys) {
        OneTimePrekey copy;
        copy.id = prekey.id;
        copy.pair.publicKey = deepCopy(prekey.pair.publicKey);
        copy.pair.privateKey = deepCopy(prekey.pair.privateKey);
        oneTimePrekeys.append(copy);
    }
    return *this;
}

IdentityState::~IdentityState()
{
    wipe();
}

void IdentityState::wipe()
{
    wipeBytes(identity.privateKey);
    wipeBytes(signedPrekey.pair.privateKey);
    for (OneTimePrekey &prekey : oneTimePrekeys) {
        wipeBytes(prekey.pair.privateKey);
    }
}

bool IdentityState::isValid() const
{
    if (!identity.isValid() || !signedPrekey.isValid()) {
        return false;
    }
    for (const OneTimePrekey &prekey : oneTimePrekeys) {
        if (!prekey.isValid()) {
            return false;
        }
    }
    return true;
}

Result<IdentityState> IdentityKeys::generate(qint64 nowMs)
{
    if (!Sodium::init()) {
        return Result<IdentityState>::error(QStringLiteral("crypto.init"));
    }

    IdentityState state;
    state.identity.publicKey = QByteArray(crypto_sign_PUBLICKEYBYTES, Qt::Uninitialized);
    state.identity.privateKey = QByteArray(crypto_sign_SECRETKEYBYTES, Qt::Uninitialized);
    if (crypto_sign_keypair(
            reinterpret_cast<unsigned char *>(state.identity.publicKey.data()),
            reinterpret_cast<unsigned char *>(state.identity.privateKey.data()))
        != 0) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_generate"));
    }

    QSet<quint32> usedIds;
    state.signedPrekey.id = uniqueKeyId(usedIds);
    state.signedPrekey.pair = generateX25519();
    state.signedPrekey.createdAtMs = nowMs;
    if (!state.signedPrekey.pair.isValid()) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_generate"));
    }
    const Result<QByteArray> signature =
        edSign(state.signedPrekey.pair.publicKey, state.identity.privateKey);
    if (signature.isError()) {
        return Result<IdentityState>::error(signature.error());
    }
    state.signedPrekey.signature = signature.value();

    state.oneTimePrekeys.reserve(kOneTimePrekeyCount);
    for (int i = 0; i < kOneTimePrekeyCount; ++i) {
        OneTimePrekey prekey;
        prekey.id = uniqueKeyId(usedIds);
        prekey.pair = generateX25519();
        if (!prekey.isValid()) {
            return Result<IdentityState>::error(QStringLiteral("crypto.keys_generate"));
        }
        state.oneTimePrekeys.append(prekey);
    }
    return Result<IdentityState>::ok(std::move(state));
}

Result<IdentityState> IdentityKeys::loadOrCreate(const QString &path,
                                                 const QByteArray &keyWrap,
                                                 const QString &deviceId,
                                                 qint64 nowMs)
{
    if (QFile::exists(path)) {
        return load(path, keyWrap, deviceId);
    }

    Result<IdentityState> generated = generate(nowMs);
    if (generated.isError()) {
        return generated;
    }
    const Result<int> saved = save(generated.value(), path, keyWrap, deviceId);
    if (saved.isError()) {
        return Result<IdentityState>::error(saved.error());
    }
    return generated;
}

Result<int> IdentityKeys::save(const IdentityState &state, const QString &path,
                              const QByteArray &keyWrap,
                              const QString &deviceId)
{
    if (!Sodium::init()) {
        return Result<int>::error(QStringLiteral("crypto.init"));
    }
    if (!state.isValid()) {
        return Result<int>::error(QStringLiteral("crypto.keys_invalid"));
    }
    if (keyWrap.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        return Result<int>::error(QStringLiteral("crypto.keys_bad_wrap"));
    }

    const quint16 count =
        static_cast<quint16>(2 + state.oneTimePrekeys.size());

    QByteArray out;
    out.reserve(kKeysHeaderBytes + count * (kEntryHeaderBytes + 160));
    out.append(kKeysMagic, sizeof(kKeysMagic));
    out.append(static_cast<char>(kKeysVersion));
    appendU16(out, count);

    auto appendEntry = [&](quint16 entryId, quint8 type, const QByteArray &plain) -> bool {
        const QByteArray aad = entryAad(deviceId, entryId, count);
        const Result<QByteArray> sealed = sealEntry(plain, keyWrap, aad);
        if (sealed.isError()) {
            return false;
        }
        appendU16(out, entryId);
        out.append(static_cast<char>(type));
        appendU32(out, static_cast<quint32>(plain.size()));
        out.append(sealed.value());
        return true;
    };

    QByteArray identityPlain;
    identityPlain.reserve(96);
    identityPlain.append(state.identity.publicKey);
    identityPlain.append(state.identity.privateKey);
    const bool identityOk = appendEntry(1, kEntryIdentity, identityPlain);
    wipeBytes(identityPlain);
    if (!identityOk) {
        return Result<int>::error(QStringLiteral("crypto.keys_seal"));
    }

    QByteArray signedPlain;
    signedPlain.reserve(140);
    appendU32(signedPlain, state.signedPrekey.id);
    appendU64(signedPlain, static_cast<quint64>(state.signedPrekey.createdAtMs));
    signedPlain.append(state.signedPrekey.pair.publicKey);
    signedPlain.append(state.signedPrekey.pair.privateKey);
    signedPlain.append(state.signedPrekey.signature);
    const bool signedOk = appendEntry(2, kEntrySignedPrekey, signedPlain);
    wipeBytes(signedPlain);
    if (!signedOk) {
        return Result<int>::error(QStringLiteral("crypto.keys_seal"));
    }

    quint16 entryId = 3;
    for (const OneTimePrekey &prekey : state.oneTimePrekeys) {
        QByteArray plain;
        plain.reserve(68);
        appendU32(plain, prekey.id);
        plain.append(prekey.pair.publicKey);
        plain.append(prekey.pair.privateKey);
        const bool ok = appendEntry(entryId, kEntryOneTimePrekey, plain);
        wipeBytes(plain);
        if (!ok) {
            return Result<int>::error(QStringLiteral("crypto.keys_seal"));
        }
        ++entryId;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return Result<int>::error(QStringLiteral("crypto.keys_io"));
    }
    if (file.write(out) != out.size()) {
        file.cancelWriting();
        return Result<int>::error(QStringLiteral("crypto.keys_io"));
    }
    if (!file.commit()) {
        return Result<int>::error(QStringLiteral("crypto.keys_io"));
    }
    return Result<int>::ok(static_cast<int>(out.size()));
}

Result<IdentityState> IdentityKeys::load(const QString &path, const QByteArray &keyWrap,
                                         const QString &deviceId)
{
    if (!Sodium::init()) {
        return Result<IdentityState>::error(QStringLiteral("crypto.init"));
    }
    if (!QFile::exists(path)) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_missing"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_io"));
    }
    const QByteArray data = file.read(kMaxKeysFileBytes + 1);
    file.close();
    if (data.size() < kKeysHeaderBytes || data.size() > kMaxKeysFileBytes) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
    }
    if (std::memcmp(data.constData(), kKeysMagic, sizeof(kKeysMagic)) != 0
        || static_cast<quint8>(data.at(8)) != kKeysVersion) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
    }

    const auto *raw = reinterpret_cast<const unsigned char *>(data.constData());
    const quint16 count = readU16(raw + 9);
    if (count < 2 || count > 2 + kOneTimePrekeyCount) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
    }

    IdentityState state;
    QSet<quint32> keyIds;
    bool haveIdentity = false;
    bool haveSigned = false;
    int pos = kKeysHeaderBytes;

    for (quint16 entryId = 1; entryId <= count; ++entryId) {
        if (pos < 0 || pos + kEntryHeaderBytes > data.size()) {
            return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
        }
        const auto *entry = raw + pos;
        const quint16 storedId = readU16(entry);
        const quint8 type = static_cast<quint8>(entry[2]);
        const quint32 plainLen = readU32(entry + 3);
        if (storedId != entryId || plainLen > kMaxEntryPlainBytes) {
            return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
        }
        // The 31-byte entry header already contains the 24-byte nonce; only
        // the ciphertext and tag follow it.
        const int sealedLen = static_cast<int>(plainLen)
            + crypto_aead_xchacha20poly1305_ietf_ABYTES;
        if (pos + kEntryHeaderBytes + sealedLen > data.size()) {
            return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
        }

        QByteArray sealed(data.constData() + pos + 7, kEntryHeaderBytes - 7);
        sealed.append(data.constData() + pos + kEntryHeaderBytes, sealedLen);
        const QByteArray aad = entryAad(deviceId, entryId, count);
        const Result<QByteArray> opened = openEntry(sealed, keyWrap, aad,
                                                    static_cast<int>(plainLen));
        if (opened.isError()) {
            return Result<IdentityState>::error(opened.error());
        }
        QByteArray plain = opened.value();
        const auto *p = reinterpret_cast<const unsigned char *>(plain.constData());

        bool parsed = false;
        if (type == kEntryIdentity && plain.size() == crypto_sign_PUBLICKEYBYTES
                                      + crypto_sign_SECRETKEYBYTES) {
            state.identity.publicKey =
                QByteArray(plain.constData(), crypto_sign_PUBLICKEYBYTES);
            state.identity.privateKey =
                QByteArray(plain.constData() + crypto_sign_PUBLICKEYBYTES,
                           crypto_sign_SECRETKEYBYTES);
            haveIdentity = state.identity.isValid();
            parsed = haveIdentity;
        } else if (type == kEntrySignedPrekey && plain.size() == 140) {
            state.signedPrekey.id = readU32(p);
            state.signedPrekey.createdAtMs = static_cast<qint64>(readU64(p + 4));
            state.signedPrekey.pair.publicKey = QByteArray(plain.constData() + 12, 32);
            state.signedPrekey.pair.privateKey = QByteArray(plain.constData() + 44, 32);
            state.signedPrekey.signature = QByteArray(plain.constData() + 76, 64);
            if (state.signedPrekey.isValid() && !keyIds.contains(state.signedPrekey.id)) {
                keyIds.insert(state.signedPrekey.id);
                haveSigned = true;
                parsed = true;
            }
        } else if (type == kEntryOneTimePrekey && plain.size() == 68) {
            OneTimePrekey prekey;
            prekey.id = readU32(p);
            prekey.pair.publicKey = QByteArray(plain.constData() + 4, 32);
            prekey.pair.privateKey = QByteArray(plain.constData() + 36, 32);
            if (prekey.isValid() && !keyIds.contains(prekey.id)) {
                keyIds.insert(prekey.id);
                state.oneTimePrekeys.append(prekey);
                parsed = true;
            }
        }
        wipeBytes(plain);
        if (!parsed) {
            return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
        }
        pos += kEntryHeaderBytes + sealedLen;
    }

    if (pos != data.size() || !haveIdentity || !haveSigned
        || state.oneTimePrekeys.size() + 2 != count) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_unreadable"));
    }
    return Result<IdentityState>::ok(std::move(state));
}

Result<PrekeyBundle> IdentityKeys::publicBundle(const IdentityState &state,
                                                quint32 oneTimePrekeyId)
{
    if (!state.identity.isValid() || !state.signedPrekey.isValid()) {
        return Result<PrekeyBundle>::error(QStringLiteral("crypto.keys_invalid"));
    }

    PrekeyBundle bundle;
    bundle.identityKey = state.identity.publicKey;
    bundle.signedPrekeyId = state.signedPrekey.id;
    bundle.signedPrekey = state.signedPrekey.pair.publicKey;
    bundle.signedPrekeySignature = state.signedPrekey.signature;

    if (oneTimePrekeyId == 0) {
        if (!state.oneTimePrekeys.isEmpty()) {
            bundle.oneTimePrekeyId = state.oneTimePrekeys.first().id;
            bundle.oneTimePrekey = state.oneTimePrekeys.first().pair.publicKey;
        }
    } else {
        const Result<OneTimePrekey> prekey = oneTimePrekeyById(state, oneTimePrekeyId);
        if (prekey.isError()) {
            return Result<PrekeyBundle>::error(prekey.error());
        }
        bundle.oneTimePrekeyId = prekey.value().id;
        bundle.oneTimePrekey = prekey.value().pair.publicKey;
    }
    return Result<PrekeyBundle>::ok(bundle);
}

Result<QByteArray> IdentityKeys::signPrekey(const IdentityState &state,
                                            const QByteArray &prekeyPublic)
{
    if (!state.identity.isValid()) {
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_invalid"));
    }
    if (prekeyPublic.size() != crypto_box_PUBLICKEYBYTES) {
        return Result<QByteArray>::error(QStringLiteral("crypto.keys_prekey_invalid"));
    }
    return edSign(prekeyPublic, state.identity.privateKey);
}

Result<IdentityState> IdentityKeys::rotatePrekeys(IdentityState state, qint64 nowMs)
{
    if (!state.identity.isValid()) {
        return Result<IdentityState>::error(QStringLiteral("crypto.keys_invalid"));
    }

    const bool signedPrekeyStale =
        !state.signedPrekey.isValid() || nowMs - state.signedPrekey.createdAtMs <= 0
        || nowMs - state.signedPrekey.createdAtMs >= kSignedPrekeyLifetimeMs;
    if (signedPrekeyStale) {
        QSet<quint32> usedIds;
        usedIds.insert(state.signedPrekey.id);
        for (const OneTimePrekey &prekey : state.oneTimePrekeys) {
            usedIds.insert(prekey.id);
        }
        SignedPrekey rotated;
        rotated.id = uniqueKeyId(usedIds);
        rotated.pair = generateX25519();
        rotated.createdAtMs = nowMs;
        if (!rotated.pair.isValid()) {
            return Result<IdentityState>::error(QStringLiteral("crypto.keys_generate"));
        }
        const Result<QByteArray> signature =
            edSign(rotated.pair.publicKey, state.identity.privateKey);
        if (signature.isError()) {
            return Result<IdentityState>::error(signature.error());
        }
        rotated.signature = signature.value();
        state.signedPrekey = rotated;
    }

    QSet<quint32> usedIds;
    usedIds.insert(state.signedPrekey.id);
    for (const OneTimePrekey &prekey : state.oneTimePrekeys) {
        usedIds.insert(prekey.id);
    }
    while (state.oneTimePrekeys.size() < kOneTimePrekeyCount) {
        OneTimePrekey prekey;
        prekey.id = uniqueKeyId(usedIds);
        prekey.pair = generateX25519();
        if (!prekey.isValid()) {
            return Result<IdentityState>::error(QStringLiteral("crypto.keys_generate"));
        }
        state.oneTimePrekeys.append(prekey);
    }
    return Result<IdentityState>::ok(std::move(state));
}

Result<OneTimePrekey> IdentityKeys::consumeOneTimePrekey(IdentityState &state, quint32 id)
{
    for (int i = 0; i < state.oneTimePrekeys.size(); ++i) {
        if (state.oneTimePrekeys.at(i).id == id) {
            const OneTimePrekey prekey = state.oneTimePrekeys.at(i);
            state.oneTimePrekeys.removeAt(i);
            return Result<OneTimePrekey>::ok(prekey);
        }
    }
    return Result<OneTimePrekey>::error(QStringLiteral("crypto.keys_prekey_missing"));
}

Result<SignedPrekey> IdentityKeys::signedPrekeyById(const IdentityState &state, quint32 id)
{
    if (state.signedPrekey.id == id && state.signedPrekey.isValid()) {
        return Result<SignedPrekey>::ok(state.signedPrekey);
    }
    return Result<SignedPrekey>::error(QStringLiteral("crypto.keys_prekey_missing"));
}

Result<OneTimePrekey> IdentityKeys::oneTimePrekeyById(const IdentityState &state, quint32 id)
{
    for (const OneTimePrekey &prekey : state.oneTimePrekeys) {
        if (prekey.id == id) {
            return Result<OneTimePrekey>::ok(prekey);
        }
    }
    return Result<OneTimePrekey>::error(QStringLiteral("crypto.keys_prekey_missing"));
}

} // namespace mini::crypto
