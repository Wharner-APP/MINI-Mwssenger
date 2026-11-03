#include "crypto/Sodium.h"

#include <QtTest>

#include <sodium.h>

using mini::crypto::Sodium;

namespace {

QByteArray hex(const char *text)
{
    return QByteArray::fromHex(QByteArray(text));
}

QByteArray bytes(const unsigned char *data, int size)
{
    return QByteArray(reinterpret_cast<const char *>(data), size);
}

} // namespace

// Official vectors only: RFC 7748 (X25519), RFC 8032 (Ed25519),
// draft-irtf-cfrg-xchacha (HChaCha20) and RFC 8439 (Poly1305).
class TestSodium : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void x25519MatchesRfc7748();
    void ed25519MatchesRfc8032();
    void hchacha20MatchesDraftVector();
    void poly1305MatchesRfc8439();
    void randomBytesAreFresh();
    void memzeroWipesInPlace();
    void constantTimeEqualsIsExact();
};

void TestSodium::initTestCase()
{
    QVERIFY(Sodium::init());
    QVERIFY(Sodium::isAvailable());
}

void TestSodium::x25519MatchesRfc7748()
{
    const QByteArray aliceSecret =
        hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const QByteArray bobSecret =
        hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const QByteArray bobPublic =
        hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");

    unsigned char alicePublic[32] = {};
    QCOMPARE(crypto_scalarmult_curve25519_base(
                 alicePublic,
                 reinterpret_cast<const unsigned char *>(aliceSecret.constData())),
             0);
    QCOMPARE(bytes(alicePublic, 32).toHex(),
             QByteArrayLiteral("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));

    unsigned char shared[32] = {};
    QCOMPARE(crypto_scalarmult_curve25519(
                 shared, reinterpret_cast<const unsigned char *>(aliceSecret.constData()),
                 reinterpret_cast<const unsigned char *>(bobPublic.constData())),
             0);
    QCOMPARE(bytes(shared, 32).toHex(),
             QByteArrayLiteral("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"));

    unsigned char reverse[32] = {};
    QCOMPARE(crypto_scalarmult_curve25519(
                 reverse, reinterpret_cast<const unsigned char *>(bobSecret.constData()),
                 alicePublic),
             0);
    QCOMPARE(bytes(reverse, 32), bytes(shared, 32));
}

void TestSodium::ed25519MatchesRfc8032()
{
    const QByteArray seed = hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");

    unsigned char publicKey[32] = {};
    unsigned char secretKey[64] = {};
    QCOMPARE(crypto_sign_seed_keypair(
                 publicKey, secretKey,
                 reinterpret_cast<const unsigned char *>(seed.constData())),
             0);
    QCOMPARE(bytes(publicKey, 32).toHex(),
             QByteArrayLiteral("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));

    unsigned char signature[64] = {};
    unsigned long long signatureLength = 0;
    const unsigned char empty = 0;
    QCOMPARE(crypto_sign_detached(signature, &signatureLength, &empty, 0, secretKey), 0);
    QCOMPARE(signatureLength, static_cast<unsigned long long>(64));
    QCOMPARE(bytes(signature, 64).toHex(),
             QByteArrayLiteral("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                               "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));
    QCOMPARE(crypto_sign_verify_detached(signature, &empty, 0, publicKey), 0);
}

void TestSodium::hchacha20MatchesDraftVector()
{
    QByteArray key;
    for (int i = 0; i < 32; ++i) {
        key.append(static_cast<char>(i));
    }
    const QByteArray nonce = hex("000000090000004a0000000031415927");
    unsigned char out[32] = {};
    QCOMPARE(crypto_core_hchacha20(
                 out, reinterpret_cast<const unsigned char *>(nonce.constData()),
                 reinterpret_cast<const unsigned char *>(key.constData()), nullptr),
             0);
    QCOMPARE(bytes(out, 32).toHex(),
             QByteArrayLiteral("82413b4227b27bfed30e42508a877d73a0f9e4d58a74a853c12ec41326d3ecdc"));
}

void TestSodium::poly1305MatchesRfc8439()
{
    const QByteArray key =
        hex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
    const QByteArray message = QByteArrayLiteral("Cryptographic Forum Research Group");
    unsigned char tag[16] = {};
    QCOMPARE(crypto_onetimeauth_poly1305(
                 tag, reinterpret_cast<const unsigned char *>(message.constData()),
                 static_cast<unsigned long long>(message.size()),
                 reinterpret_cast<const unsigned char *>(key.constData())),
             0);
    QCOMPARE(bytes(tag, 16).toHex(), QByteArrayLiteral("a8061dc1305136c6c22b8baf0c0127a9"));
}

void TestSodium::randomBytesAreFresh()
{
    const QByteArray first = Sodium::randomBytes(32);
    const QByteArray second = Sodium::randomBytes(32);
    QCOMPARE(first.size(), 32);
    QCOMPARE(second.size(), 32);
    QVERIFY(first != second);
    QVERIFY(Sodium::randomBytes(0).isEmpty());
    QVERIFY(Sodium::randomBytes(-5).isEmpty());
}

void TestSodium::memzeroWipesInPlace()
{
    QByteArray secret = QByteArrayLiteral("top secret material");
    const int originalSize = secret.size();
    Sodium::memzero(secret);
    QCOMPARE(secret.size(), originalSize);
    for (const char byte : secret) {
        QCOMPARE(byte, '\0');
    }
}

void TestSodium::constantTimeEqualsIsExact()
{
    const QByteArray a = QByteArrayLiteral("0123456789abcdef");
    const QByteArray b = QByteArrayLiteral("0123456789abcdef");
    const QByteArray c = QByteArrayLiteral("0123456789abcdeF");
    QVERIFY(Sodium::constantTimeEquals(a, b));
    QVERIFY(!Sodium::constantTimeEquals(a, c));
    QVERIFY(!Sodium::constantTimeEquals(a, a.left(8)));
    QVERIFY(!Sodium::constantTimeEquals(QByteArray(), QByteArray()));
}

QTEST_MAIN(TestSodium)
#include "test_sodium.moc"
