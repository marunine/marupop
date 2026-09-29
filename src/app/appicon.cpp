// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/appicon.h"

#ifdef Q_OS_WIN
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSvgRenderer>

#include <array>
#endif

namespace maru
{

// icons/CMakeLists.txt installs both names in the same five sizes, so either name resolves in
// the hicolor theme.
QString applicationIconName(bool paused)
{
    return paused ? QStringLiteral(MARUPOP_APPLICATION_ID "-paused") : QStringLiteral(MARUPOP_APPLICATION_ID);
}

#ifdef Q_OS_WIN

namespace
{

// The ColorScheme-Text color that every icon source declares.
constexpr QByteArrayView kSourceColor{"#232629"};

// Windows 10 and 11 draw the taskbar light when "Choose your default Windows mode" is Light.
// SystemUsesLightTheme records Light as 1. A missing value reads as 0, the dark taskbar.
bool taskbarIsLight()
{
    const QSettings personalize{QStringLiteral("HKEY_CURRENT_USER\\") + QString{taskbarThemeKey},
                                QSettings::NativeFormat};
    return personalize.value(QStringLiteral("SystemUsesLightTheme"), 0).toInt() != 0;
}

// The source SVG for a target size in px.
QString sourceFor(int size, bool paused)
{
    static constexpr std::array kHinted{16, 22, 32, 48};
    QString prefix = QStringLiteral("sc");
    for (const int hinted : kHinted) {
        if (size <= hinted) {
            prefix = QString::number(hinted);
            break;
        }
    }
    return QStringLiteral(":/marupop/icons/%1-apps-%2.svg").arg(prefix, applicationIconName(paused));
}

} // namespace

QIcon applicationIcon(bool paused, IconBackground background)
{
    const QByteArray color = background == IconBackground::Tray && !taskbarIsLight() ? QByteArrayLiteral("#ffffff")
                                                                                     : kSourceColor.toByteArray();
    // Cached per state and color, since the tray requests the icon on every scanning toggle.
    static QHash<std::pair<bool, QByteArray>, QIcon> rendered;
    QIcon &icon = rendered[{paused, color}];
    if (!icon.isNull()) {
        return icon;
    }
    // The tray requests 16 px at 100 % scale and 32 px at 200 % scale. A title bar requests 16 px
    // to 64 px.
    static constexpr std::array kSizes{16, 20, 24, 32, 40, 48, 64};
    for (const int size : kSizes) {
        QFile file{sourceFor(size, paused)};
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QByteArray svg = file.readAll();
        svg.replace(kSourceColor.toByteArray(), color);
        QSvgRenderer renderer{svg};
        QPixmap pixmap{size, size};
        pixmap.fill(Qt::transparent);
        QPainter painter{&pixmap};
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

#else

QIcon applicationIcon(bool paused, IconBackground /*background*/)
{
    return QIcon::fromTheme(applicationIconName(paused));
}

#endif

} // namespace maru
