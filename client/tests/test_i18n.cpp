#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTranslator>
#include <QVector>
#include <QXmlStreamReader>
#include <QtTest>

#ifndef MINI_SOURCE_DIR
#define MINI_SOURCE_DIR "src"
#endif

#ifndef MINI_TS_FILE
#define MINI_TS_FILE "translations/mini_ru.ts"
#endif

namespace {

bool hasCyrillic(const QString &text)
{
    for (const QChar character : text) {
        const ushort code = character.unicode();
        if (code >= 0x0400 && code <= 0x04FF) {
            return true;
        }
    }
    return false;
}

QString stripComments(const QString &input)
{
    QString output = input;
    bool lineComment = false;
    bool blockComment = false;
    bool inString = false;
    bool inChar = false;
    bool escaped = false;

    for (int i = 0; i < input.size(); ++i) {
        const QChar current = input.at(i);
        const QChar next = i + 1 < input.size() ? input.at(i + 1) : QChar();

        if (lineComment) {
            if (current == QLatin1Char('\n')) {
                lineComment = false;
            } else {
                output[i] = QLatin1Char(' ');
            }
            continue;
        }
        if (blockComment) {
            if (current == QLatin1Char('*') && next == QLatin1Char('/')) {
                output[i] = QLatin1Char(' ');
                output[i + 1] = QLatin1Char(' ');
                ++i;
                blockComment = false;
            } else if (current != QLatin1Char('\n')) {
                output[i] = QLatin1Char(' ');
            }
            continue;
        }
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (current == QLatin1Char('\\')) {
                escaped = true;
            } else if (current == QLatin1Char('"')) {
                inString = false;
            }
            continue;
        }
        if (inChar) {
            if (escaped) {
                escaped = false;
            } else if (current == QLatin1Char('\\')) {
                escaped = true;
            } else if (current == QLatin1Char('\'')) {
                inChar = false;
            }
            continue;
        }
        if (current == QLatin1Char('/') && next == QLatin1Char('/')) {
            lineComment = true;
            output[i] = QLatin1Char(' ');
            output[i + 1] = QLatin1Char(' ');
            ++i;
            continue;
        }
        if (current == QLatin1Char('/') && next == QLatin1Char('*')) {
            blockComment = true;
            output[i] = QLatin1Char(' ');
            output[i + 1] = QLatin1Char(' ');
            ++i;
            continue;
        }
        if (current == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (current == QLatin1Char('\'')) {
            inChar = true;
            continue;
        }
    }
    return output;
}

struct TsMessage
{
    QString context;
    QString source;
    QString translation;
    bool unfinished = false;
};

QVector<TsMessage> readTs(const QString &path, QString *error)
{
    QVector<TsMessage> messages;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("cannot open %1").arg(path);
        return messages;
    }

    QXmlStreamReader xml(&file);
    QString context;
    TsMessage message;
    bool inMessage = false;
    bool inSource = false;
    bool inTranslation = false;

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString name = xml.name().toString();
            if (name == QLatin1String("context")) {
                context.clear();
            } else if (name == QLatin1String("name") && !inMessage) {
                context = xml.readElementText();
            } else if (name == QLatin1String("message")) {
                inMessage = true;
                message = TsMessage();
                message.context = context;
            } else if (name == QLatin1String("source") && inMessage) {
                inSource = true;
            } else if (name == QLatin1String("translation") && inMessage) {
                inTranslation = true;
                message.unfinished =
                    xml.attributes().value(QStringLiteral("type")) == QLatin1String("unfinished");
            }
        } else if (xml.isEndElement()) {
            const QString name = xml.name().toString();
            if (name == QLatin1String("source")) {
                inSource = false;
            } else if (name == QLatin1String("translation")) {
                inTranslation = false;
            } else if (name == QLatin1String("message")) {
                inMessage = false;
                messages.append(message);
            }
        } else if (xml.isCharacters() && inMessage) {
            if (inSource) {
                message.source += xml.text().toString();
            } else if (inTranslation) {
                message.translation += xml.text().toString();
            }
        }
    }

    if (xml.hasError()) {
        *error = xml.errorString();
    }
    return messages;
}

} // namespace

class TestI18n : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sourceHasNoCyrillicLiterals();
    void translationFileIsComplete();
    void russianTranslationLoads();
};

void TestI18n::initTestCase()
{
    QVERIFY2(QDir(QStringLiteral(MINI_SOURCE_DIR)).exists(),
             "MINI_SOURCE_DIR must point at client/src");
}

void TestI18n::sourceHasNoCyrillicLiterals()
{
    const QRegularExpression literal(
        QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\""));
    QStringList offenders;

    QDirIterator iterator(QStringLiteral(MINI_SOURCE_DIR),
                          {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString source = QString::fromUtf8(file.readAll());
        const QString stripped = stripComments(source);

        auto matches = literal.globalMatch(stripped);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (hasCyrillic(match.captured(0))) {
                const int line = stripped.left(match.capturedStart()).count(QLatin1Char('\n')) + 1;
                offenders.append(QStringLiteral("%1:%2 %3")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(line)
                                     .arg(match.captured(0)));
            }
        }
    }

    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Russian UI literals in code:\n%1")
                            .arg(offenders.join(QLatin1Char('\n')))));
}

void TestI18n::translationFileIsComplete()
{
    QString error;
    const QVector<TsMessage> messages = readTs(QStringLiteral(MINI_TS_FILE), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(!messages.isEmpty(), "the Russian .ts file has no messages");

    QStringList incomplete;
    for (const TsMessage &message : messages) {
        if (message.source.trimmed().isEmpty()) {
            continue;
        }
        if (message.translation.trimmed().isEmpty() || message.unfinished) {
            incomplete.append(QStringLiteral("%1: %2").arg(message.context, message.source));
        }
    }
    QVERIFY2(incomplete.isEmpty(),
             qPrintable(QStringLiteral("Untranslated messages:\n%1")
                            .arg(incomplete.join(QLatin1Char('\n')))));
}

void TestI18n::russianTranslationLoads()
{
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/mini.qm")));
    QCoreApplication::installTranslator(&translator);

    QCOMPARE((QCoreApplication::translate("mini::Sidebar", "Chats")),
             QString::fromUtf8("Чаты"));
    QCOMPARE((QCoreApplication::translate("mini::ChatView", "No chat selected")),
             QString::fromUtf8("Чат не выбран"));

    QCoreApplication::removeTranslator(&translator);
}

QTEST_MAIN(TestI18n)
#include "test_i18n.moc"
