#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QVector>

namespace mini {

enum class ThemeMode { Dark, Light, Amoled };
enum class Density { Comfortable, Compact };

struct Tokens
{
    QColor bgBase;
    QColor bgRail;
    QColor bgList;
    QColor bgChat;
    QColor bgSurface;
    QColor bgInput;
    QColor bgBubbleIn;
    QColor bgBubbleOut;
    QColor border;
    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    QColor accent;
    QColor accentText;
    QColor danger;
    QColor online;
};

struct AccentDef
{
    QString id;
    QString name;
    QColor color;
};

// Owns the design tokens and the generated stylesheet. All widgets read the
// current tokens at paint time, so switching a theme needs no restart.
class Theme : public QObject
{
    Q_OBJECT

public:
    enum class Accent { Lime, Sky, Violet, Coral, Amber, Mint, Rose, Ice };
    Q_ENUM(Accent)

    static Theme &instance();

    void apply(ThemeMode mode,
               Accent accent,
               int fontScalePercent = 100,
               Density density = Density::Comfortable);

    [[nodiscard]] ThemeMode mode() const { return m_mode; }
    [[nodiscard]] Accent accent() const { return m_accent; }
    [[nodiscard]] int fontScalePercent() const { return m_fontScale; }
    [[nodiscard]] Density density() const { return m_density; }
    [[nodiscard]] Tokens tokens() const;
    [[nodiscard]] QString styleSheet() const;
    [[nodiscard]] int rowHeight() const;
    [[nodiscard]] int scaledPx(int px) const;
    [[nodiscard]] QFont appFont() const;
    [[nodiscard]] QFont displayFont(int px, int weight = QFont::DemiBold) const;

    [[nodiscard]] static QVector<AccentDef> accents();
    [[nodiscard]] static QString modeId(ThemeMode mode);
    [[nodiscard]] static ThemeMode modeFromId(const QString &id, ThemeMode fallback = ThemeMode::Dark);
    [[nodiscard]] static QString accentId(Accent accent);
    [[nodiscard]] static Accent accentFromId(const QString &id, Accent fallback = Accent::Lime);
    [[nodiscard]] static QString densityId(Density density);
    [[nodiscard]] static Density densityFromId(const QString &id, Density fallback = Density::Comfortable);
    [[nodiscard]] static double contrastRatio(const QColor &first, const QColor &second);

private:
    Theme();
    static void loadFonts();
    static QString fontFamily();
    static QString displayFamily();

    ThemeMode m_mode = ThemeMode::Dark;
    Accent m_accent = Accent::Lime;
    int m_fontScale = 100;
    Density m_density = Density::Comfortable;
    bool m_fontsLoaded = false;

signals:
    void changed();
};

} // namespace mini
