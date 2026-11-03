#pragma once

#include <QSplashScreen>

namespace mini {

// 480x300 splash. Shown for at most 2 s while the shell is prepared.
class SplashScreen : public QSplashScreen
{
    Q_OBJECT

public:
    explicit SplashScreen(QWidget *parent = nullptr);

    [[nodiscard]] static QPixmap createPixmap();
};

} // namespace mini
