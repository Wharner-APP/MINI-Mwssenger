#include "crypto/SignInKey.h"

#include <QRegularExpression>
#include <QtTest>

using mini::Result;
using mini::crypto::NormalizedSignIn;
using mini::crypto::SignInKey;
using mini::crypto::SignInMaterial;

class TestSignInKey : public QObject
{
    Q_OBJECT

private slots:
    void generatedFileHasFiveWordsTenLettersFiveSymbols();
    void twentyNormalizationVariantsAreEqual();
    void editingOneTokenBreaksCheck();
    void wrongHeaderIsRejected();
    void tokensInWrongOrderAreRejected();
    void twoGenerationsDiffer();
};

void TestSignInKey::generatedFileHasFiveWordsTenLettersFiveSymbols()
{
    const SignInMaterial material = SignInKey::generate();
    QVERIFY(!material.parsed.normalized.isEmpty());
    QCOMPARE(material.parsed.tokens.size(), 20);

    for (int i = 0; i < 5; ++i) {
        QVERIFY2(SignInKey::isWordToken(material.parsed.tokens.at(i)),
                 qPrintable(material.parsed.tokens.at(i)));
    }
    for (int i = 5; i < 15; ++i) {
        QVERIFY2(SignInKey::isLetterToken(material.parsed.tokens.at(i)),
                 qPrintable(material.parsed.tokens.at(i)));
    }
    for (int i = 15; i < 20; ++i) {
        QVERIFY2(SignInKey::isSymbolToken(material.parsed.tokens.at(i)),
                 qPrintable(material.parsed.tokens.at(i)));
    }

    const QRegularExpression checkPattern(QStringLiteral("^[A-Z2-7]{13}$"));
    QVERIFY(checkPattern.match(material.parsed.check).hasMatch());

    const QString text = QString::fromUtf8(material.fileBytes);
    QVERIFY(text.startsWith(QStringLiteral("MINI-SIGNIN-1\n")));
    QVERIFY(text.contains(QStringLiteral("\nCHECK ")));

    const Result<NormalizedSignIn> parsed = SignInKey::normalize(material.fileBytes);
    QVERIFY2(parsed.isOk(), qPrintable(parsed.error()));
    QCOMPARE(parsed.value().normalized, material.parsed.normalized);
    QCOMPARE(parsed.value().check, material.parsed.check);
}

void TestSignInKey::twentyNormalizationVariantsAreEqual()
{
    const SignInMaterial material = SignInKey::generate();
    const QString base = QString::fromUtf8(material.fileBytes);
    const QString tokens = material.parsed.normalized;

    QVector<QByteArray> variants;
    auto makeFile = [&](const QString &header, const QString &tokenLine,
                        const QString &checkLine, const QString &ending) {
        return (header + ending + tokenLine + ending + checkLine + ending).toUtf8();
    };
    const QString check = QStringLiteral("CHECK ") + material.parsed.check;

    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), tokens, check, QStringLiteral("\n")));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), tokens, check, QStringLiteral("\r\n")));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), tokens, check, QStringLiteral("\r")));
    QString spaced = tokens;
    spaced.replace(QLatin1Char(' '), QStringLiteral("   "));
    QString tabbed = tokens;
    tabbed.replace(QLatin1Char(' '), QStringLiteral("\t"));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), spaced, check, QStringLiteral("\n")));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), tabbed, check, QStringLiteral("\r\n")));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1  "), tokens, check, QStringLiteral("\n")));
    variants.append(makeFile(QStringLiteral("  MINI-SIGNIN-1"), tokens, check, QStringLiteral("\r\n")));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), tokens + QStringLiteral("  "), check,
                             QStringLiteral("\n")));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), tokens,
                             QStringLiteral("CHECK: ") + material.parsed.check,
                             QStringLiteral("\n")));
    variants.append(QByteArrayLiteral("\xEF\xBB\xBF") + base.toUtf8());
    variants.append(QByteArrayLiteral("\n\n") + makeFile(QStringLiteral("MINI-SIGNIN-1"), tokens,
                                                         check, QStringLiteral("\n")));
    variants.append(makeFile(QStringLiteral("MINI-SIGNIN-1"), tokens, check,
                             QStringLiteral("\n")) + QByteArrayLiteral("\n\n"));
    variants.append(QStringLiteral("MINI-SIGNIN-1\r\n%1\n%2\r\n").arg(tokens, check).toUtf8());
    for (int i = 0; i < 8; ++i) {
        QString header = QStringLiteral("MINI-SIGNIN-1");
        QString line = tokens;
        QString checkLine = check;
        QString ending = (i % 3 == 0) ? QStringLiteral("\r\n")
            : (i % 3 == 1 ? QStringLiteral("\n") : QStringLiteral("\r"));
        if (i % 2 == 1) {
            variants.append(QByteArrayLiteral("\xEF\xBB\xBF")
                            + makeFile(header, line, checkLine, ending));
        } else {
            variants.append(makeFile(header, line, checkLine, ending));
        }
    }
    QVERIFY(variants.size() >= 20);

    for (const QByteArray &variant : variants) {
        const Result<NormalizedSignIn> parsed = SignInKey::normalize(variant);
        QVERIFY2(parsed.isOk(), qPrintable(parsed.error()));
        QCOMPARE(parsed.value().normalized, material.parsed.normalized);
        QCOMPARE(parsed.value().check, material.parsed.check);
    }
}

void TestSignInKey::editingOneTokenBreaksCheck()
{
    const SignInMaterial material = SignInKey::generate();
    QStringList tokens = material.parsed.tokens;
    const QString original = tokens.at(16);
    tokens[16] = original == QStringLiteral("!") ? QStringLiteral("@") : QStringLiteral("!");
    QVERIFY(tokens.at(16) != original);

    const QString edited = QStringLiteral("MINI-SIGNIN-1\n%1\nCHECK %2\n")
                               .arg(tokens.join(QLatin1Char(' ')), material.parsed.check);
    const Result<NormalizedSignIn> parsed = SignInKey::normalize(edited.toUtf8());
    QVERIFY(parsed.isError());
    QCOMPARE(parsed.error(), QStringLiteral("signin.check_mismatch"));
}

void TestSignInKey::wrongHeaderIsRejected()
{
    const SignInMaterial material = SignInKey::generate();
    QByteArray file = material.fileBytes;
    file.replace("MINI-SIGNIN-1", "MINI-SIGNIN-2");
    const Result<NormalizedSignIn> parsed = SignInKey::normalize(file);
    QVERIFY(parsed.isError());
    QCOMPARE(parsed.error(), QStringLiteral("signin.header"));
}

void TestSignInKey::tokensInWrongOrderAreRejected()
{
    const SignInMaterial material = SignInKey::generate();
    QStringList tokens = material.parsed.tokens;
    tokens.swapItemsAt(0, 5); // a letter where the first word must be
    const QString edited = QStringLiteral("MINI-SIGNIN-1\n%1\nCHECK %2\n")
                               .arg(tokens.join(QLatin1Char(' ')),
                                    SignInKey::checksumFor(tokens.join(QLatin1Char(' '))));
    const Result<NormalizedSignIn> parsed = SignInKey::normalize(edited.toUtf8());
    QVERIFY(parsed.isError());
    QCOMPARE(parsed.error(), QStringLiteral("signin.token_alphabet"));
}

void TestSignInKey::twoGenerationsDiffer()
{
    const SignInMaterial first = SignInKey::generate();
    const SignInMaterial second = SignInKey::generate();
    QVERIFY(first.parsed.normalized != second.parsed.normalized);
}

QTEST_GUILESS_MAIN(TestSignInKey)
#include "test_signin_key.moc"
