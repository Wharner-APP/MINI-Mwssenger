#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QHash>
#include <QStringList>

#include <cmath>

namespace mini {

namespace {

QColor color(const char *hex)
{
    return QColor(QString::fromLatin1(hex));
}

QString hex(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

QStringList &loadedFamilies()
{
    static QStringList families;
    return families;
}

} // namespace

Theme::Theme() = default;

Theme &Theme::instance()
{
    static Theme theme;
    return theme;
}

void Theme::loadFonts()
{
    QStringList &families = loadedFamilies();
    if (!families.isEmpty()) {
        return;
    }
    const QStringList files = QDir(QStringLiteral(":/fonts")).entryList(
        {QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files);
    for (const QString &file : files) {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + file);
        if (id >= 0) {
            families += QFontDatabase::applicationFontFamilies(id);
        }
    }
    families.removeDuplicates();
}

QString Theme::fontFamily()
{
    const QStringList &families = loadedFamilies();
    if (families.contains(QStringLiteral("Inter"))) {
        return QStringLiteral("Inter");
    }
    for (const QString &family : families) {
        if (family.startsWith(QStringLiteral("Inter"))) {
            return family;
        }
    }
    return QStringLiteral("Segoe UI");
}

QString Theme::displayFamily()
{
    const QStringList &families = loadedFamilies();
    if (families.contains(QStringLiteral("Manrope"))) {
        return QStringLiteral("Manrope");
    }
    for (const QString &family : families) {
        if (family.startsWith(QStringLiteral("Manrope"))) {
            return family;
        }
    }
    return QStringLiteral("Segoe UI");
}

QVector<AccentDef> Theme::accents()
{
    return {
        {QStringLiteral("lime"), tr("Lime"), color("#C8F56B")},
        {QStringLiteral("sky"), tr("Sky"), color("#6BC8F5")},
        {QStringLiteral("violet"), tr("Violet"), color("#B08BFF")},
        {QStringLiteral("coral"), tr("Coral"), color("#FF8A80")},
        {QStringLiteral("amber"), tr("Amber"), color("#F2C86B")},
        {QStringLiteral("mint"), tr("Mint"), color("#6BE6B4")},
        {QStringLiteral("rose"), tr("Rose"), color("#F58BB8")},
        {QStringLiteral("ice"), tr("Ice"), color("#A9B8FF")},
    };
}

QString Theme::modeId(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Amoled:
        return QStringLiteral("amoled");
    }
    return QStringLiteral("dark");
}

ThemeMode Theme::modeFromId(const QString &id, ThemeMode fallback)
{
    if (id == QLatin1String("dark")) {
        return ThemeMode::Dark;
    }
    if (id == QLatin1String("light")) {
        return ThemeMode::Light;
    }
    if (id == QLatin1String("amoled")) {
        return ThemeMode::Amoled;
    }
    return fallback;
}

QString Theme::accentId(Accent accent)
{
    switch (accent) {
    case Accent::Lime:
        return QStringLiteral("lime");
    case Accent::Sky:
        return QStringLiteral("sky");
    case Accent::Violet:
        return QStringLiteral("violet");
    case Accent::Coral:
        return QStringLiteral("coral");
    case Accent::Amber:
        return QStringLiteral("amber");
    case Accent::Mint:
        return QStringLiteral("mint");
    case Accent::Rose:
        return QStringLiteral("rose");
    case Accent::Ice:
        return QStringLiteral("ice");
    }
    return QStringLiteral("lime");
}

Theme::Accent Theme::accentFromId(const QString &id, Accent fallback)
{
    static const QHash<QString, Accent> mapping = {
        {QStringLiteral("lime"), Accent::Lime},
        {QStringLiteral("sky"), Accent::Sky},
        {QStringLiteral("violet"), Accent::Violet},
        {QStringLiteral("coral"), Accent::Coral},
        {QStringLiteral("amber"), Accent::Amber},
        {QStringLiteral("mint"), Accent::Mint},
        {QStringLiteral("rose"), Accent::Rose},
        {QStringLiteral("ice"), Accent::Ice},
    };
    return mapping.value(id, fallback);
}

QString Theme::densityId(Density density)
{
    return density == Density::Compact ? QStringLiteral("compact")
                                       : QStringLiteral("comfortable");
}

Density Theme::densityFromId(const QString &id, Density fallback)
{
    if (id == QLatin1String("compact")) {
        return Density::Compact;
    }
    if (id == QLatin1String("comfortable")) {
        return Density::Comfortable;
    }
    return fallback;
}

double Theme::contrastRatio(const QColor &first, const QColor &second)
{
    const auto luminance = [](const QColor &c) {
        const auto channel = [](double value) {
            value /= 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green())
               + 0.0722 * channel(c.blue());
    };
    const double a = luminance(first);
    const double b = luminance(second);
    const double lighter = std::max(a, b);
    const double darker = std::min(a, b);
    return (lighter + 0.05) / (darker + 0.05);
}

Tokens Theme::tokens() const
{
    Tokens t;
    switch (m_mode) {
    case ThemeMode::Dark:
        t.bgBase = color("#141518");
        t.bgRail = color("#101114");
        t.bgList = color("#181A1E");
        t.bgChat = color("#1B1D22");
        t.bgSurface = color("#202329");
        t.bgInput = color("#23262D");
        t.bgBubbleIn = color("#23262D");
        t.bgBubbleOut = color("#2E3A2A");
        t.border = color("#2A2E36");
        t.textPrimary = color("#E8E9EC");
        t.textSecondary = color("#A0A6AE");
        t.textTertiary = color("#7C838D");
        t.danger = color("#F26D6D");
        t.online = color("#5BD48A");
        break;
    case ThemeMode::Light:
        t.bgBase = color("#F4F5F7");
        t.bgRail = color("#ECEEF1");
        t.bgList = color("#FFFFFF");
        t.bgChat = color("#FAFBFC");
        t.bgSurface = color("#FFFFFF");
        t.bgInput = color("#EFF1F4");
        t.bgBubbleIn = color("#F0F2F5");
        t.bgBubbleOut = color("#E4F3D2");
        t.border = color("#D9DDE3");
        t.textPrimary = color("#1A1C20");
        t.textSecondary = color("#5A616B");
        t.textTertiary = color("#626A74");
        t.danger = color("#C63B3B");
        t.online = color("#2E9E62");
        break;
    case ThemeMode::Amoled:
        t.bgBase = color("#000000");
        t.bgRail = color("#000000");
        t.bgList = color("#050506");
        t.bgChat = color("#000000");
        t.bgSurface = color("#0E0F12");
        t.bgInput = color("#15171B");
        t.bgBubbleIn = color("#0E0F12");
        t.bgBubbleOut = color("#1A2417");
        t.border = color("#1D2025");
        t.textPrimary = color("#F2F3F5");
        t.textSecondary = color("#A7ADB6");
        t.textTertiary = color("#7E8590");
        t.danger = color("#FF7A7A");
        t.online = color("#63E693");
        break;
    }

    t.accent = accents().at(static_cast<int>(m_accent)).color;
    t.accentText = color("#141518");
    return t;
}

int Theme::rowHeight() const
{
    return m_density == Density::Compact ? 56 : 68;
}

int Theme::scaledPx(int px) const
{
    return qRound(px * m_fontScale / 100.0);
}

QFont Theme::appFont() const
{
    QFont font(fontFamily());
    font.setPointSizeF(10.0 * m_fontScale / 100.0);
    return font;
}

QFont Theme::displayFont(int px, int weight) const
{
    QFont font(displayFamily());
    font.setPixelSize(scaledPx(px));
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

QString Theme::styleSheet() const
{
    const Tokens t = tokens();
    QString qss = QStringLiteral(R"QSS(
QMainWindow, #appShell { background: @bgBase; }
#rail { background: @bgRail; border-right: 1px solid @border; }
#sidebar { background: @bgList; border-right: 1px solid @border; }
#chatView { background: @bgChat; }
#chatHeader { background: @bgChat; border-bottom: 1px solid @border; }
#composerBar { background: @bgChat; border-top: 1px solid @border; }
QLabel { background: transparent; color: @textPrimary; }
QLabel[miniRole="title"] { font-family: "@displayFamily"; font-size: @titlePx; font-weight: 600; color: @textPrimary; }
QLabel[miniRole="subtitle"] { font-family: "@displayFamily"; font-size: @subtitlePx; color: @textSecondary; }
QLabel[miniRole="caption"] { font-size: @captionPx; color: @textTertiary; }
QLineEdit, QComboBox { background: @bgInput; color: @textPrimary; border: 1px solid @border; border-radius: 10px; padding: 6px 10px; selection-background-color: @accentFill; selection-color: @accentOn; }
QLineEdit:focus, QComboBox:focus { border: 2px solid @accentFill; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: @bgSurface; color: @textPrimary; border: 1px solid @border; selection-background-color: @bgInput; selection-color: @textPrimary; }
QToolButton { background: transparent; color: @textPrimary; border: 1px solid transparent; border-radius: 10px; padding: 6px; }
QToolButton:hover { background: @bgSurface; }
QToolButton:pressed { background: @bgInput; }
QToolButton:checked { background: @bgSurface; border: 1px solid @accentFill; }
QToolButton:focus { border: 2px solid @accentFill; }
QToolButton::menu-indicator { image: none; }
QPushButton { background: @bgSurface; color: @textPrimary; border: 1px solid @border; border-radius: 10px; padding: 8px 16px; }
QPushButton:hover { background: @bgInput; }
QPushButton:focus { border: 2px solid @accentFill; }
QPushButton[miniRole="accent"] { background: @accentFill; color: @accentOn; border: 1px solid @accentFill; font-weight: 600; }
QPushButton[miniRole="accent"]:hover { background: @accentHover; }
QPushButton:disabled { color: @textTertiary; background: @bgSurface; border-color: @border; }
QTextEdit { background: @bgInput; color: @textPrimary; border: 1px solid @border; border-radius: 12px; padding: 8px 10px; selection-background-color: @accentFill; selection-color: @accentOn; }
QTextEdit:focus { border: 1px solid @accentFill; }
QTextEdit:disabled { color: @textTertiary; }
QListView { background: transparent; border: none; outline: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: @textTertiary; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 4px; min-width: 24px; }
QStatusBar { background: @bgRail; color: @textTertiary; border-top: 1px solid @border; }
QTabWidget::pane { border: 1px solid @border; border-radius: 10px; background: @bgSurface; top: -1px; }
QTabBar::tab { background: transparent; color: @textSecondary; padding: 8px 16px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: @textPrimary; border-bottom: 2px solid @accentFill; }
QDialog { background: @bgList; }
QToolTip { background: @bgSurface; color: @textPrimary; border: 1px solid @border; padding: 4px 8px; }
)QSS");

    QHash<QString, QString> values = {
        {QStringLiteral("@bgBase"), hex(t.bgBase)},
        {QStringLiteral("@bgRail"), hex(t.bgRail)},
        {QStringLiteral("@bgList"), hex(t.bgList)},
        {QStringLiteral("@bgChat"), hex(t.bgChat)},
        {QStringLiteral("@bgSurface"), hex(t.bgSurface)},
        {QStringLiteral("@bgInput"), hex(t.bgInput)},
        {QStringLiteral("@border"), hex(t.border)},
        {QStringLiteral("@textPrimary"), hex(t.textPrimary)},
        {QStringLiteral("@textSecondary"), hex(t.textSecondary)},
        {QStringLiteral("@textTertiary"), hex(t.textTertiary)},
        {QStringLiteral("@accentFill"), hex(t.accent)},
        {QStringLiteral("@accentOn"), hex(t.accentText)},
        {QStringLiteral("@accentHover"), t.accent.lighter(112).name(QColor::HexRgb)},
        {QStringLiteral("@danger"), hex(t.danger)},
        {QStringLiteral("@online"), hex(t.online)},
        {QStringLiteral("@displayFamily"), displayFamily()},
        {QStringLiteral("@titlePx"), QString::number(scaledPx(22)) + QStringLiteral("px")},
        {QStringLiteral("@subtitlePx"), QString::number(scaledPx(16)) + QStringLiteral("px")},
        {QStringLiteral("@captionPx"), QString::number(scaledPx(12)) + QStringLiteral("px")},
    };

    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        qss.replace(it.key(), it.value());
    }
    return qss;
}

void Theme::apply(ThemeMode mode, Accent accent, int fontScalePercent, Density density)
{
    loadFonts();

    const int scale = qBound(80, fontScalePercent, 150);
    const bool changedValue = mode != m_mode || accent != m_accent || scale != m_fontScale
                              || density != m_density;
    m_mode = mode;
    m_accent = accent;
    m_fontScale = scale;
    m_density = density;

    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setFont(appFont());
        app->setStyleSheet(styleSheet());
    }

    if (changedValue) {
        emit changed();
    }
}

} // namespace mini
