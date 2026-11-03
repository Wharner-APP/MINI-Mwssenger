#include "ui/Markdown.h"

namespace mini::markdown {

namespace {

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

bool isMarkupChar(QChar c)
{
    return c == QLatin1Char('*') || c == QLatin1Char('_') || c == QLatin1Char('~')
        || c == QLatin1Char('`') || c == QLatin1Char('|') || c == QLatin1Char('[')
        || c == QLatin1Char('>') || c == QLatin1Char('\\');
}

int findPair(QStringView text, int from, QStringView marker)
{
    for (int j = from; j + marker.size() <= text.size(); ++j) {
        if (text.mid(j, marker.size()) != marker) {
            continue;
        }
        if (j > from && text.at(j - 1).isSpace()) {
            continue;
        }
        return j;
    }
    return -1;
}

int findSingle(QStringView text, int from, QChar marker)
{
    for (int j = from; j < text.size(); ++j) {
        if (text.at(j) != marker) {
            continue;
        }
        // Do not close a single delimiter on the first half of a double one.
        if (j + 1 < text.size() && text.at(j + 1) == marker) {
            continue;
        }
        if (j > from && text.at(j - 1).isSpace()) {
            continue;
        }
        if (marker == QLatin1Char('_') && j + 1 < text.size()
            && isWordChar(text.at(j + 1))) {
            continue;
        }
        return j;
    }
    return -1;
}

Document parseInline(QStringView input, int depth, int maxDepth);

void appendDocument(Document &target, const Document &source, int offset)
{
    for (Span span : source.spans) {
        span.start += offset;
        span.end += offset;
        target.spans.append(span);
    }
    target.text += source.text;
    target.quote = target.quote || source.quote;
}

Document parseInline(QStringView input, int depth, int maxDepth)
{
    Document out;
    int i = 0;
    while (i < input.size()) {
        const QChar c = input.at(i);

        // Escape: the next markup character is literal.
        if (c == QLatin1Char('\\') && i + 1 < input.size()
            && isMarkupChar(input.at(i + 1))) {
            out.text += input.at(i + 1);
            i += 2;
            continue;
        }

        // Fenced code block. Its contents are never parsed (§35.2).
        if (input.mid(i, 3) == u"```") {
            const int close = input.indexOf(u"```", i + 3);
            if (close > i) {
                const int start = out.text.size();
                out.text += input.mid(i + 3, close - i - 3).toString();
                out.spans.append(
                    Span{start, out.text.size(), Style::Code, QString(), depth});
                i = close + 3;
                continue;
            }
        }

        // Inline code.
        if (c == QLatin1Char('`')) {
            const int close = input.indexOf(QLatin1Char('`'), i + 1);
            if (close > i + 1) {
                const int start = out.text.size();
                out.text += input.mid(i + 1, close - i - 1).toString();
                out.spans.append(
                    Span{start, out.text.size(), Style::Code, QString(), depth});
                i = close + 1;
                continue;
            }
        }

        // Quote line (§35.1). Markers are stripped; the renderer draws the bar.
        if (c == QLatin1Char('>') && (i == 0 || input.at(i - 1) == QLatin1Char('\n'))) {
            int lineEnd = input.indexOf(QLatin1Char('\n'), i + 1);
            if (lineEnd < 0) {
                lineEnd = input.size();
            }
            const int start = out.text.size();
            int contentStart = i + 1;
            if (contentStart < lineEnd && input.at(contentStart) == QLatin1Char(' ')) {
                ++contentStart;
            }
            const Document line = parseInline(input.mid(contentStart, lineEnd - contentStart),
                                              depth, maxDepth);
            appendDocument(out, line, start);
            out.spans.append(
                Span{start, out.text.size(), Style::Quote, QString(), depth});
            out.quote = true;
            i = lineEnd;
            continue;
        }

        // Link: [text](url). Unsafe targets stay literal.
        if (c == QLatin1Char('[')) {
            const int closeLabel = input.indexOf(QLatin1Char(']'), i + 1);
            if (closeLabel > i && closeLabel + 1 < input.size()
                && input.at(closeLabel + 1) == QLatin1Char('(')) {
                const int closeUrl = input.indexOf(QLatin1Char(')'), closeLabel + 2);
                if (closeUrl > closeLabel + 1) {
                    const QString rawUrl =
                        input.mid(closeLabel + 2, closeUrl - closeLabel - 2).toString();
                    const QString url = sanitizeLink(rawUrl);
                    if (!url.isEmpty()) {
                        Q_UNUSED(depth);
                        const int start = out.text.size();
                        const Document label = parseInline(
                            input.mid(i + 1, closeLabel - i - 1), depth + 1, maxDepth);
                        appendDocument(out, label, start);
                        if (out.text.size() > start) {
                            out.spans.append(Span{start, out.text.size(), Style::Link,
                                                  url, depth});
                        }
                        i = closeUrl + 1;
                        continue;
                    }
                }
            }
        }

        // Bold+italic shorthand: ***text*** or ___text___.
        if (i + 2 < input.size()
            && ((c == QLatin1Char('*') && input.at(i + 1) == QLatin1Char('*')
                 && input.at(i + 2) == QLatin1Char('*'))
                || (c == QLatin1Char('_') && input.at(i + 1) == QLatin1Char('_')
                    && input.at(i + 2) == QLatin1Char('_')))) {
            const QStringView marker = input.mid(i, 3);
            if (depth < maxDepth) {
                const int close = findPair(input, i + 3, marker);
                if (close > i + 3) {
                    const Document inner =
                        parseInline(input.mid(i + 3, close - i - 3), depth + 1, maxDepth);
                    const int start = out.text.size();
                    appendDocument(out, inner, start);
                    out.spans.append(
                        Span{start, out.text.size(), Style::Bold, QString(), depth});
                    out.spans.append(
                        Span{start, out.text.size(), Style::Italic, QString(), depth});
                    i = close + 3;
                    continue;
                }
            }
        }

        // Bold: ** or __.
        if (i + 1 < input.size()
            && ((c == QLatin1Char('*') && input.at(i + 1) == QLatin1Char('*'))
                || (c == QLatin1Char('_') && input.at(i + 1) == QLatin1Char('_')))) {
            const QStringView marker = input.mid(i, 2);
            const bool opens = i + 2 < input.size() && !input.at(i + 2).isSpace();
            if (opens && depth < maxDepth) {
                const int close = findPair(input, i + 2, marker);
                if (close > i + 2) {
                    const Document inner =
                        parseInline(input.mid(i + 2, close - i - 2), depth + 1, maxDepth);
                    const int start = out.text.size();
                    appendDocument(out, inner, start);
                    out.spans.append(
                        Span{start, out.text.size(), Style::Bold, QString(), depth});
                    i = close + 2;
                    continue;
                }
            }
        }

        // Strike: ~~
        if (i + 1 < input.size() && c == QLatin1Char('~')
            && input.at(i + 1) == QLatin1Char('~')) {
            const bool opens = i + 2 < input.size() && !input.at(i + 2).isSpace();
            if (opens && depth < maxDepth) {
                const int close = findPair(input, i + 2, u"~~");
                if (close > i + 2) {
                    const Document inner =
                        parseInline(input.mid(i + 2, close - i - 2), depth + 1, maxDepth);
                    const int start = out.text.size();
                    appendDocument(out, inner, start);
                    out.spans.append(
                        Span{start, out.text.size(), Style::Strike, QString(), depth});
                    i = close + 2;
                    continue;
                }
            }
        }

        // Spoiler: ||
        if (i + 1 < input.size() && c == QLatin1Char('|')
            && input.at(i + 1) == QLatin1Char('|')) {
            const bool opens = i + 2 < input.size() && !input.at(i + 2).isSpace();
            if (opens && depth < maxDepth) {
                const int close = findPair(input, i + 2, u"||");
                if (close > i + 2) {
                    const Document inner =
                        parseInline(input.mid(i + 2, close - i - 2), depth + 1, maxDepth);
                    const int start = out.text.size();
                    appendDocument(out, inner, start);
                    out.spans.append(
                        Span{start, out.text.size(), Style::Spoiler, QString(), depth});
                    i = close + 2;
                    continue;
                }
            }
        }

        // Italic: * or _.
        if ((c == QLatin1Char('*') || c == QLatin1Char('_')) && depth < maxDepth) {
            const bool doubleMarker =
                i + 1 < input.size() && input.at(i + 1) == c;
            const bool opens =
                !doubleMarker && i + 1 < input.size() && !input.at(i + 1).isSpace();
            if (opens) {
                const int close = findSingle(input, i + 1, c);
                if (close > i + 1) {
                    const Document inner =
                        parseInline(input.mid(i + 1, close - i - 1), depth + 1, maxDepth);
                    const int start = out.text.size();
                    appendDocument(out, inner, start);
                    out.spans.append(
                        Span{start, out.text.size(), Style::Italic, QString(), depth});
                    i = close + 1;
                    continue;
                }
            }
        }

        // Mentions: @login (the sender-side admin rule is enforced elsewhere).
        if (c == QLatin1Char('@')
            && (i == 0 || !isWordChar(input.at(i - 1)))) {
            int end = i + 1;
            while (end < input.size() && isWordChar(input.at(end))) {
                ++end;
            }
            const int length = end - i - 1;
            if (length >= 2 && length <= 24) {
                const int start = out.text.size();
                out.text += input.mid(i, end - i).toString();
                out.spans.append(Span{start, out.text.size(), Style::Mention,
                                      out.text.mid(start), depth});
                i = end;
                continue;
            }
        }

        // Hashtags: #tag.
        if (c == QLatin1Char('#')
            && (i == 0 || input.at(i - 1).isSpace())) {
            int end = i + 1;
            while (end < input.size() && isWordChar(input.at(end))) {
                ++end;
            }
            if (end > i + 1) {
                const int start = out.text.size();
                out.text += input.mid(i, end - i).toString();
                out.spans.append(Span{start, out.text.size(), Style::Hashtag,
                                      out.text.mid(start), depth});
                i = end;
                continue;
            }
        }

        out.text += c;
        ++i;
    }
    return out;
}

} // namespace

bool Document::has(Style style) const
{
    for (const Span &span : spans) {
        if (span.style == style) {
            return true;
        }
    }
    return false;
}

Document parse(const QString &input, int maxDepth)
{
    return parseInline(QStringView(input), 0, qBound(1, maxDepth, 3));
}

bool isSafeLink(const QString &url)
{
    const QString trimmed = url.trimmed();
    const int colon = trimmed.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
        return false;
    }
    const QString scheme = trimmed.left(colon).toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")
        && scheme != QLatin1String("mini")) {
        return false;
    }
    for (const QChar c : trimmed) {
        if (c.isSpace() || c.category() == QChar::Other_Control) {
            return false;
        }
    }
    return true;
}

QString sanitizeLink(const QString &url)
{
    const QString trimmed = url.trimmed();
    return isSafeLink(trimmed) ? trimmed : QString();
}

QStringList mentions(const QString &input)
{
    QStringList out;
    const Document document = parse(input);
    for (const Span &span : document.spans) {
        if (span.style == Style::Mention) {
            out.append(span.target);
        }
    }
    return out;
}

QStringList hashtags(const QString &input)
{
    QStringList out;
    const Document document = parse(input);
    for (const Span &span : document.spans) {
        if (span.style == Style::Hashtag) {
            out.append(span.target);
        }
    }
    return out;
}

bool containsAllMention(const QString &input)
{
    for (const QString &mention : mentions(input)) {
        if (mention.compare(QStringLiteral("@all"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool looksFormatted(const QString &input)
{
    for (const QChar c : input) {
        if (c == QLatin1Char('*') || c == QLatin1Char('_') || c == QLatin1Char('~')
            || c == QLatin1Char('`') || c == QLatin1Char('|') || c == QLatin1Char('[')
            || c == QLatin1Char('@') || c == QLatin1Char('#')
            || c == QLatin1Char('>')) {
            return true;
        }
    }
    return false;
}

bool mentionAllowedForRole(const QString &input, bool admin)
{
    return admin || !containsAllMention(input);
}

} // namespace mini::markdown
