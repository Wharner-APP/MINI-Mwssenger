#include "ui/MarkdownPainter.h"

#include "ui/Markdown.h"

#include <QFontDatabase>
#include <QPainter>
#include <QRect>
#include <QTextLayout>
#include <QTextOption>

namespace mini::markdown {

namespace {

constexpr int kQuoteInset = 8;

QFont monospaceFont(const QFont &base)
{
    QFont font = base;
    font.setFamilies(QStringList{QStringLiteral("Cascadia Mono"),
                                 QStringLiteral("Consolas"),
                                 QStringLiteral("Courier New")});
    font.setStyleHint(QFont::Monospace);
    return font;
}

void applySpan(QTextLayout::FormatRange &range, const Span &span, const QFont &base,
               const QColor &textColor, const QColor &secondary, const QColor &accent,
               const QColor &codeBackground)
{
    range.start = static_cast<int>(span.start);
    range.length = static_cast<int>(qMax<qsizetype>(0, span.end - span.start));
    QTextCharFormat format;
    switch (span.style) {
    case Style::Bold:
        format.setFontWeight(QFont::DemiBold);
        break;
    case Style::Italic:
        format.setFontItalic(true);
        break;
    case Style::Strike:
        format.setFontStrikeOut(true);
        break;
    case Style::Code:
        format.setFont(monospaceFont(base));
        format.setBackground(codeBackground);
        break;
    case Style::Spoiler:
        // Hidden until revealed: the glyphs are drawn in the same colour as
        // the block, so the text cannot be read from a screenshot.
        format.setForeground(codeBackground);
        format.setBackground(codeBackground);
        break;
    case Style::Link:
        format.setForeground(accent);
        format.setFontUnderline(true);
        break;
    case Style::Mention:
        format.setForeground(accent);
        format.setFontWeight(QFont::DemiBold);
        break;
    case Style::Hashtag:
        format.setForeground(accent);
        break;
    case Style::Quote:
        format.setFontItalic(true);
        format.setForeground(secondary);
        break;
    }
    Q_UNUSED(textColor);
    range.format = format;
}

struct BuiltLayout
{
    QTextLayout layout;
    QVector<QTextLayout::FormatRange> formats;
    QString plain;

    BuiltLayout(const QString &text, const QFont &font)
        : layout(text, font)
    {
    }
};

BuiltLayout *build(const QString &text, const QFont &font, int width,
                   const QColor &textColor, const QColor &secondary, const QColor &accent,
                   const QColor &codeBackground)
{
    const Document document = parse(text);
    auto *built = new BuiltLayout(document.text, font);
    built->plain = document.text;
    int spanWidth = qMax(20, width - (document.quote ? kQuoteInset : 0));

    for (const Span &span : document.spans) {
        QTextLayout::FormatRange range;
        applySpan(range, span, font, textColor, secondary, accent, codeBackground);
        if (range.length > 0) {
            built->formats.append(range);
        }
    }
    built->layout.setFormats(built->formats);

    QTextOption option(Qt::AlignLeft);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    built->layout.setTextOption(option);
    built->layout.beginLayout();
    int y = 0;
    for (;;) {
        QTextLine line = built->layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(spanWidth);
        line.setPosition(QPointF(0, y));
        y += qCeil(line.height());
    }
    built->layout.endLayout();
    return built;
}

} // namespace

int layoutHeight(const QString &text, const QFont &font, int width)
{
    const Document document = parse(text);
    if (document.text.isEmpty()) {
        return 0;
    }
    BuiltLayout *built = build(text, font, width, QColor(0, 0, 0), QColor(0, 0, 0),
                               QColor(0, 0, 0), QColor(0, 0, 0));
    int height = 0;
    for (int i = 0; i < built->layout.lineCount(); ++i) {
        height += qCeil(built->layout.lineAt(i).height());
    }
    delete built;
    return qMax(height, 1);
}

void paint(QPainter *painter, const QRect &rect, const QString &text, const QFont &font,
           const QColor &textColor, const QColor &secondary, const QColor &accent,
           const QColor &codeBackground, const QString &highlight)
{
    const Document document = parse(text);
    if (document.text.isEmpty()) {
        return;
    }
    BuiltLayout *built = build(text, font, rect.width(), textColor, secondary, accent,
                               codeBackground);

    QVector<QTextLayout::FormatRange> selections;
    if (!highlight.trimmed().isEmpty()) {
        const QString needle = highlight.trimmed();
        int from = 0;
        while (from <= built->plain.size()) {
            const int at = built->plain.indexOf(needle, from, Qt::CaseInsensitive);
            if (at < 0) {
                break;
            }
            QTextLayout::FormatRange range;
            range.start = at;
            range.length = needle.size();
            range.format.setBackground(QColor(accent.red(), accent.green(), accent.blue(),
                                              90));
            selections.append(range);
            from = at + qMax(1, needle.size());
        }
    }

    painter->save();
    if (document.quote) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(accent);
        painter->drawRoundedRect(QRect(rect.left(), rect.top(), 3, rect.height()), 1, 1);
    }
    const QPointF origin(rect.left() + (document.quote ? kQuoteInset : 0), rect.top());
    if (selections.isEmpty()) {
        built->layout.draw(painter, origin);
    } else {
        built->layout.draw(painter, origin, selections);
    }
    painter->restore();
    delete built;
}

QString plainText(const QString &input)
{
    return parse(input).text;
}

} // namespace mini::markdown
