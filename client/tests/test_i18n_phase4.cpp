#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QXmlStreamReader>
#include <QtTest>

#ifndef MINI_SOURCE_DIR
#define MINI_SOURCE_DIR "src"
#endif

#ifndef MINI_TS_RU_FILE
#ifdef MINI_TS_FILE
#define MINI_TS_RU_FILE MINI_TS_FILE
#else
#define MINI_TS_RU_FILE "translations/mini_ru.ts"
#endif
#endif

#ifndef MINI_TS_EN_FILE
#define MINI_TS_EN_FILE ""
#endif

namespace {

bool isIdentifierStart(QChar character)
{
    return character.isLetter() || character == QLatin1Char('_');
}

bool isIdentifierPart(QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('_');
}

int hexValue(QChar character)
{
    const ushort code = character.unicode();
    if (code >= '0' && code <= '9') {
        return code - '0';
    }
    if (code >= 'a' && code <= 'f') {
        return code - 'a' + 10;
    }
    if (code >= 'A' && code <= 'F') {
        return code - 'A' + 10;
    }
    return -1;
}

void skipSpaceAndComments(const QString &text, int &index)
{
    while (index < text.size()) {
        const QChar current = text.at(index);
        if (current.isSpace()) {
            ++index;
            continue;
        }
        if (current == QLatin1Char('/') && index + 1 < text.size()) {
            const QChar next = text.at(index + 1);
            if (next == QLatin1Char('/')) {
                index += 2;
                while (index < text.size() && text.at(index) != QLatin1Char('\n')) {
                    ++index;
                }
                continue;
            }
            if (next == QLatin1Char('*')) {
                index += 2;
                while (index + 1 < text.size()
                       && !(text.at(index) == QLatin1Char('*')
                            && text.at(index + 1) == QLatin1Char('/'))) {
                    ++index;
                }
                if (index + 1 < text.size()) {
                    index += 2;
                }
                continue;
            }
        }
        break;
    }
}

bool parseStringLiteral(const QString &text, int &index, QString *value)
{
    int cursor = index;
    if (cursor + 1 < text.size() && text.at(cursor) == QLatin1Char('u')
        && text.at(cursor + 1) == QLatin1Char('8')) {
        cursor += 2;
    } else if (cursor < text.size()
               && (text.at(cursor) == QLatin1Char('L') || text.at(cursor) == QLatin1Char('u')
                   || text.at(cursor) == QLatin1Char('U'))) {
        ++cursor;
    }
    if (cursor >= text.size() || text.at(cursor) != QLatin1Char('"')) {
        return false;
    }
    ++cursor;

    QString parsed;
    while (cursor < text.size()) {
        const QChar current = text.at(cursor++);
        if (current == QLatin1Char('"')) {
            *value = parsed;
            index = cursor;
            return true;
        }
        if (current == QLatin1Char('\n')) {
            continue;
        }
        if (current != QLatin1Char('\\')) {
            parsed += current;
            continue;
        }

        if (cursor >= text.size()) {
            break;
        }
        const QChar escape = text.at(cursor++);
        switch (escape.unicode()) {
        case 'n':
            parsed += QLatin1Char('\n');
            break;
        case 'r':
            parsed += QLatin1Char('\r');
            break;
        case 't':
            parsed += QLatin1Char('\t');
            break;
        case 'a':
            parsed += QChar(0x07);
            break;
        case 'b':
            parsed += QChar(0x08);
            break;
        case 'f':
            parsed += QChar(0x0c);
            break;
        case 'v':
            parsed += QChar(0x0b);
            break;
        case '\\':
        case '"':
        case '\'':
        case '?':
            parsed += escape;
            break;
        case 'x': {
            QString digits;
            while (cursor < text.size() && digits.size() < 2 && hexValue(text.at(cursor)) >= 0) {
                digits += text.at(cursor++);
            }
            bool ok = false;
            const uint value = digits.toUInt(&ok, 16);
            if (ok) {
                parsed += QChar(static_cast<ushort>(value));
            }
            break;
        }
        case 'u':
        case 'U': {
            const int width = escape == QLatin1Char('u') ? 4 : 8;
            QString digits;
            while (cursor < text.size() && digits.size() < width
                   && hexValue(text.at(cursor)) >= 0) {
                digits += text.at(cursor++);
            }
            bool ok = false;
            const uint codePoint = digits.toUInt(&ok, 16);
            if (ok) {
                const char32_t character = static_cast<char32_t>(codePoint);
                parsed += QString::fromUcs4(&character, 1);
            }
            break;
        }
        default:
            if (escape >= QLatin1Char('0') && escape <= QLatin1Char('7')) {
                uint value = static_cast<uint>(escape.unicode() - '0');
                int consumed = 1;
                while (cursor < text.size() && consumed < 3
                       && text.at(cursor) >= QLatin1Char('0')
                       && text.at(cursor) <= QLatin1Char('7')) {
                    value = value * 8 + static_cast<uint>(text.at(cursor).unicode() - '0');
                    ++cursor;
                    ++consumed;
                }
                parsed += QChar(static_cast<ushort>(value));
            } else {
                parsed += escape;
            }
            break;
        }
    }
    return false;
}

bool parseConcatenatedLiteral(const QString &text, int &index, QString *value)
{
    QString parsed;
    int cursor = index;
    if (!parseStringLiteral(text, cursor, &parsed)) {
        return false;
    }

    while (true) {
        int lookahead = cursor;
        skipSpaceAndComments(text, lookahead);
        QString piece;
        int pieceCursor = lookahead;
        if (!parseStringLiteral(text, pieceCursor, &piece)) {
            break;
        }
        parsed += piece;
        cursor = pieceCursor;
    }

    *value = parsed;
    index = cursor;
    return true;
}

void collectTranslationSources(const QString &text, QSet<QString> *sources)
{
    int index = 0;
    while (index < text.size()) {
        const QChar current = text.at(index);

        if (current.isSpace()
            || (current == QLatin1Char('/') && index + 1 < text.size()
                && (text.at(index + 1) == QLatin1Char('/')
                    || text.at(index + 1) == QLatin1Char('*')))) {
            const int before = index;
            skipSpaceAndComments(text, index);
            if (index != before) {
                continue;
            }
        }

        if (current == QLatin1Char('"')) {
            QString ignored;
            int cursor = index;
            if (parseStringLiteral(text, cursor, &ignored)) {
                index = cursor;
                continue;
            }
            ++index;
            continue;
        }

        if (current == QLatin1Char('\'')) {
            ++index;
            while (index < text.size()) {
                if (text.at(index) == QLatin1Char('\\')) {
                    index += 2;
                } else if (text.at(index) == QLatin1Char('\'')) {
                    ++index;
                    break;
                } else {
                    ++index;
                }
            }
            continue;
        }

        if (!isIdentifierStart(current)) {
            ++index;
            continue;
        }

        const int start = index;
        while (index < text.size() && isIdentifierPart(text.at(index))) {
            ++index;
        }
        const QString identifier = text.mid(start, index - start);

        const bool isTr = identifier == QLatin1String("tr");
        const bool isTranslate = identifier == QLatin1String("translate");
        const bool isTrNoop = identifier == QLatin1String("QT_TR_NOOP");
        const bool isTranslateNoop =
            identifier == QLatin1String("QT_TRANSLATE_NOOP")
            || identifier == QLatin1String("QT_TRANSLATE_NOOP3");
        if (!isTr && !isTranslate && !isTrNoop && !isTranslateNoop) {
            continue;
        }

        int cursor = index;
        skipSpaceAndComments(text, cursor);
        if (cursor >= text.size() || text.at(cursor) != QLatin1Char('(')) {
            continue;
        }
        ++cursor;
        skipSpaceAndComments(text, cursor);

        if (isTranslate || isTranslateNoop) {
            QString context;
            if (!parseConcatenatedLiteral(text, cursor, &context)) {
                continue;
            }
            skipSpaceAndComments(text, cursor);
            if (cursor >= text.size() || text.at(cursor) != QLatin1Char(',')) {
                continue;
            }
            ++cursor;
            skipSpaceAndComments(text, cursor);
            QString source;
            if (parseConcatenatedLiteral(text, cursor, &source)) {
                sources->insert(source);
            }
            continue;
        }

        QString source;
        if (parseConcatenatedLiteral(text, cursor, &source)) {
            sources->insert(source);
        }
    }
}

struct TsMessage
{
    QString context;
    QString source;
    QString translation;
    QStringList numerusForms;
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
    bool inNumerusForm = false;

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
                    xml.attributes().value(QStringLiteral("type"))
                    == QLatin1String("unfinished");
            } else if (name == QLatin1String("numerusform") && inTranslation) {
                inNumerusForm = true;
                message.numerusForms.append(QString());
            }
        } else if (xml.isEndElement()) {
            const QString name = xml.name().toString();
            if (name == QLatin1String("source")) {
                inSource = false;
            } else if (name == QLatin1String("translation")) {
                inTranslation = false;
                inNumerusForm = false;
            } else if (name == QLatin1String("numerusform")) {
                inNumerusForm = false;
            } else if (name == QLatin1String("message")) {
                inMessage = false;
                messages.append(message);
            }
        } else if (xml.isCharacters() && inMessage) {
            const QString characters = xml.text().toString();
            if (inSource) {
                message.source += characters;
            } else if (inTranslation) {
                if (inNumerusForm && !message.numerusForms.isEmpty()) {
                    message.numerusForms.last() += characters;
                } else {
                    message.translation += characters;
                }
            }
        }
    }

    if (xml.hasError()) {
        *error = xml.errorString();
    }
    return messages;
}

QString russianCatalogPath()
{
    return QString::fromUtf8(MINI_TS_RU_FILE);
}

QString englishCatalogPath()
{
    const QString configured = QString::fromUtf8(MINI_TS_EN_FILE);
    if (!configured.isEmpty()) {
        return configured;
    }
    QString path = russianCatalogPath();
    path.replace(QStringLiteral("mini_ru.ts"), QStringLiteral("mini_en.ts"));
    if (path == russianCatalogPath()) {
        path = QFileInfo(russianCatalogPath()).dir().filePath(QStringLiteral("mini_en.ts"));
    }
    return path;
}

QSet<QString> catalogSources(const QVector<TsMessage> &messages)
{
    QSet<QString> sources;
    for (const TsMessage &message : messages) {
        sources.insert(message.source);
    }
    return sources;
}

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

} // namespace

class TestI18nPhase4 : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sourceStringsArePresentInBothCatalogs();
    void catalogsHaveTheSameMessages();
    void everyMessageIsTranslated();
    void englishCatalogHasNoCyrillic();
};

void TestI18nPhase4::initTestCase()
{
    QVERIFY2(QDir(QString::fromUtf8(MINI_SOURCE_DIR)).exists(),
             "MINI_SOURCE_DIR must point at client/src");
    QVERIFY2(QFile::exists(russianCatalogPath()),
             qPrintable(QStringLiteral("missing %1").arg(russianCatalogPath())));
    QVERIFY2(QFile::exists(englishCatalogPath()),
             qPrintable(QStringLiteral("missing %1").arg(englishCatalogPath())));
}

void TestI18nPhase4::sourceStringsArePresentInBothCatalogs()
{
    QSet<QString> codeSources;

    QDirIterator iterator(QString::fromUtf8(MINI_SOURCE_DIR),
                          {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        collectTranslationSources(QString::fromUtf8(file.readAll()), &codeSources);
    }

    QVERIFY2(codeSources.size() > 500,
             qPrintable(QStringLiteral("only %1 translation calls found").arg(codeSources.size())));

    QString error;
    const QSet<QString> russian = catalogSources(readTs(russianCatalogPath(), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QSet<QString> english = catalogSources(readTs(englishCatalogPath(), &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QStringList missingRussian;
    QStringList missingEnglish;
    for (const QString &source : codeSources) {
        if (!russian.contains(source)) {
            missingRussian.append(source);
        }
        if (!english.contains(source)) {
            missingEnglish.append(source);
        }
    }

    QVERIFY2(missingRussian.isEmpty(),
             qPrintable(QStringLiteral("missing in mini_ru.ts:\n%1")
                            .arg(missingRussian.join(QLatin1Char('\n')))));
    QVERIFY2(missingEnglish.isEmpty(),
             qPrintable(QStringLiteral("missing in mini_en.ts:\n%1")
                            .arg(missingEnglish.join(QLatin1Char('\n')))));
}

void TestI18nPhase4::catalogsHaveTheSameMessages()
{
    QString error;
    const QVector<TsMessage> russian = readTs(russianCatalogPath(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QVector<TsMessage> english = readTs(englishCatalogPath(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(english.size(), russian.size());

    QSet<QString> russianKeys;
    for (const TsMessage &message : russian) {
        russianKeys.insert(message.context + QChar(0x1f) + message.source);
    }
    QStringList missing;
    for (const TsMessage &message : english) {
        if (!russianKeys.contains(message.context + QChar(0x1f) + message.source)) {
            missing.append(QStringLiteral("%1: %2").arg(message.context, message.source));
        }
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("messages only in mini_en.ts:\n%1")
                            .arg(missing.join(QLatin1Char('\n')))));
}

void TestI18nPhase4::everyMessageIsTranslated()
{
    const QStringList paths{russianCatalogPath(), englishCatalogPath()};
    for (const QString &path : paths) {
        QString error;
        const QVector<TsMessage> messages = readTs(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QStringList incomplete;
        for (const TsMessage &message : messages) {
            if (message.source.trimmed().isEmpty()) {
                continue;
            }
            if (message.unfinished) {
                incomplete.append(QStringLiteral("%1: %2 (unfinished)")
                                      .arg(message.context, message.source));
                continue;
            }
            if (!message.numerusForms.isEmpty()) {
                bool empty = false;
                for (const QString &form : message.numerusForms) {
                    if (form.trimmed().isEmpty()) {
                        empty = true;
                    }
                }
                if (empty) {
                    incomplete.append(QStringLiteral("%1: %2 (empty plural form)")
                                          .arg(message.context, message.source));
                }
            } else if (message.translation.trimmed().isEmpty()) {
                incomplete.append(QStringLiteral("%1: %2").arg(message.context, message.source));
            }
        }
        QVERIFY2(incomplete.isEmpty(),
                 qPrintable(QStringLiteral("%1 has incomplete translations:\n%2")
                                .arg(path, incomplete.join(QLatin1Char('\n')))));
    }
}

void TestI18nPhase4::englishCatalogHasNoCyrillic()
{
    QString error;
    const QVector<TsMessage> messages = readTs(englishCatalogPath(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QStringList offenders;
    for (const TsMessage &message : messages) {
        bool bad = hasCyrillic(message.translation);
        for (const QString &form : message.numerusForms) {
            bad = bad || hasCyrillic(form);
        }
        if (bad) {
            offenders.append(QStringLiteral("%1: %2").arg(message.context, message.source));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Cyrillic text in the English catalog:\n%1")
                            .arg(offenders.join(QLatin1Char('\n')))));
}

QTEST_MAIN(TestI18nPhase4)
#include "test_i18n_phase4.moc"
