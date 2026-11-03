#include "ui/Markdown.h"

#include <QtTest>

using namespace mini;

class TestMarkdown : public QObject
{
    Q_OBJECT

private slots:
    void parsesInlineStyles();
    void parsesCodeAndKeepsItLiteral();
    void parsesSpoilerAndStrike();
    void parsesLinksAndSanitizesTargets();
    void parsesQuoteAndHashtag();
    void supportsNestingUpToThree();
    void leavesUnclosedMarkupAsText();
    void findsMentionsAndAllRule();
};

void TestMarkdown::parsesInlineStyles()
{
    const markdown::Document bold = markdown::parse(QStringLiteral("a **bold** b"));
    QCOMPARE(bold.text, QStringLiteral("a bold b"));
    QVERIFY(bold.has(markdown::Style::Bold));
    QCOMPARE(bold.spans.size(), 1);
    QCOMPARE(bold.spans.first().start, qsizetype(2));
    QCOMPARE(bold.spans.first().end, qsizetype(6));

    const markdown::Document italic = markdown::parse(QStringLiteral("_cursive_"));
    QCOMPARE(italic.text, QStringLiteral("cursive"));
    QVERIFY(italic.has(markdown::Style::Italic));

    const markdown::Document underscoreBold = markdown::parse(QStringLiteral("__strong__"));
    QCOMPARE(underscoreBold.text, QStringLiteral("strong"));
    QVERIFY(underscoreBold.has(markdown::Style::Bold));
}

void TestMarkdown::parsesCodeAndKeepsItLiteral()
{
    const markdown::Document inlineCode =
        markdown::parse(QStringLiteral("run `**not bold**` now"));
    QCOMPARE(inlineCode.text, QStringLiteral("run **not bold** now"));
    QVERIFY(inlineCode.has(markdown::Style::Code));
    QVERIFY(!inlineCode.has(markdown::Style::Bold));

    const markdown::Document block =
        markdown::parse(QStringLiteral("```\n*x*\n```"));
    QCOMPARE(block.text, QStringLiteral("\n*x*\n"));
    QVERIFY(block.has(markdown::Style::Code));
    QVERIFY(!block.has(markdown::Style::Italic));
}

void TestMarkdown::parsesSpoilerAndStrike()
{
    const markdown::Document spoiler = markdown::parse(QStringLiteral("||secret||"));
    QCOMPARE(spoiler.text, QStringLiteral("secret"));
    QVERIFY(spoiler.has(markdown::Style::Spoiler));

    const markdown::Document strike = markdown::parse(QStringLiteral("~~old~~"));
    QCOMPARE(strike.text, QStringLiteral("old"));
    QVERIFY(strike.has(markdown::Style::Strike));
}

void TestMarkdown::parsesLinksAndSanitizesTargets()
{
    const markdown::Document safe =
        markdown::parse(QStringLiteral("[site](https://example.com/a)"));
    QCOMPARE(safe.text, QStringLiteral("site"));
    QVERIFY(safe.has(markdown::Style::Link));
    QCOMPARE(safe.spans.first().target, QStringLiteral("https://example.com/a"));

    const markdown::Document unsafe =
        markdown::parse(QStringLiteral("[x](javascript:alert(1))"));
    QCOMPARE(unsafe.text, QStringLiteral("[x](javascript:alert(1))"));
    QVERIFY(!unsafe.has(markdown::Style::Link));

    QVERIFY(markdown::isSafeLink(QStringLiteral("mini://g/abc")));
    QVERIFY(!markdown::isSafeLink(QStringLiteral("file:///etc/passwd")));
    QVERIFY(!markdown::isSafeLink(QStringLiteral("data:text/html,x")));
    QVERIFY(markdown::sanitizeLink(QStringLiteral(" http://x ")) == QStringLiteral("http://x"));
}

void TestMarkdown::parsesQuoteAndHashtag()
{
    const markdown::Document quote = markdown::parse(QStringLiteral("> quoted line"));
    QVERIFY(quote.quote);
    QCOMPARE(quote.text, QStringLiteral("quoted line"));
    QVERIFY(quote.has(markdown::Style::Quote));

    const markdown::Document tag = markdown::parse(QStringLiteral("hi #mini"));
    QCOMPARE(markdown::hashtags(QStringLiteral("hi #mini")),
             QStringList{QStringLiteral("#mini")});
    QVERIFY(tag.has(markdown::Style::Hashtag));
}

void TestMarkdown::supportsNestingUpToThree()
{
    const markdown::Document nested =
        markdown::parse(QStringLiteral("**bold _italic ~~strike~~_**"));
    QCOMPARE(nested.text, QStringLiteral("bold italic strike"));
    QVERIFY(nested.has(markdown::Style::Bold));
    QVERIFY(nested.has(markdown::Style::Italic));
    QVERIFY(nested.has(markdown::Style::Strike));

    const markdown::Document triple =
        markdown::parse(QStringLiteral("***both***"));
    QCOMPARE(triple.text, QStringLiteral("both"));
    QVERIFY(triple.has(markdown::Style::Bold));
    QVERIFY(triple.has(markdown::Style::Italic));

    const markdown::Document capped =
        markdown::parse(QStringLiteral("**a _b ~~c~~_ d**"));
    QVERIFY(capped.has(markdown::Style::Bold));
    QVERIFY(capped.has(markdown::Style::Italic));
    QVERIFY(capped.has(markdown::Style::Strike));
}

void TestMarkdown::leavesUnclosedMarkupAsText()
{
    const markdown::Document document = markdown::parse(QStringLiteral("**open"));
    QCOMPARE(document.text, QStringLiteral("**open"));
    QVERIFY(!document.has(markdown::Style::Bold));

    const markdown::Document single = markdown::parse(QStringLiteral("a * b * c"));
    QCOMPARE(single.text, QStringLiteral("a * b * c"));
}

void TestMarkdown::findsMentionsAndAllRule()
{
    const QString body = QStringLiteral("hi @alice and @all");
    QCOMPARE(markdown::mentions(body),
             (QStringList{QStringLiteral("@alice"), QStringLiteral("@all")}));
    QVERIFY(markdown::containsAllMention(body));
    QVERIFY(markdown::mentionAllowedForRole(body, false) == false);
    QVERIFY(markdown::mentionAllowedForRole(body, true));
    QVERIFY(markdown::mentionAllowedForRole(QStringLiteral("hi @alice"), false));
}

QTEST_MAIN(TestMarkdown)
#include "test_markdown.moc"
