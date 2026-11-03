#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace mini::markdown {

// §35.1 syntax supported by the message parser. Spans point into Document::text
// after the markup characters have been removed.
enum class Style {
    Bold,
    Italic,
    Strike,
    Code,
    Spoiler,
    Link,
    Mention,
    Hashtag,
    Quote
};

struct Span
{
    qsizetype start = 0;
    qsizetype end = 0; // exclusive
    Style style = Style::Bold;
    QString target; // URL for Link, login for Mention, tag for Hashtag
    int depth = 0;
};

struct Document
{
    QString text;
    QVector<Span> spans;
    bool quote = false;

    [[nodiscard]] bool has(Style style) const;
};

// Streaming parser, no regular expressions over the whole body (§35.2).
// Unclosed markup stays literal, nesting is capped at `maxDepth` (3 by
// default), and code spans never parse their contents.
Document parse(const QString &input, int maxDepth = 3);

// §35.2: only http, https and mini links survive. Anything else returns an
// empty string so the caller can keep the original markup as plain text.
[[nodiscard]] bool isSafeLink(const QString &url);
[[nodiscard]] QString sanitizeLink(const QString &url);

// Helpers for the mention rules (§35.4) and quick rendering decisions.
[[nodiscard]] QStringList mentions(const QString &input);
[[nodiscard]] QStringList hashtags(const QString &input);
[[nodiscard]] bool containsAllMention(const QString &input);
[[nodiscard]] bool looksFormatted(const QString &input);

// The sender-side rule: @all is accepted only from a group/channel admin.
[[nodiscard]] bool mentionAllowedForRole(const QString &input, bool admin);

} // namespace mini::markdown
