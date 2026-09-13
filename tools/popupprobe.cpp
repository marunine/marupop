// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Shows one sample popup on the live session next to the pointer for five seconds, which is
// the only way to see the layer-shell surface, the card geometry and the pitch-accent overlay
// that the offscreen test platform cannot exercise.
//
// Build: cmake -B build-popup -DMARUPOP_BUILD_DEV_TOOLS=ON && cmake --build build-popup
//        --target marupop-popupprobe
// Run:   ./build-popup/tools/marupop-popupprobe [--pinned] [--seconds N]
#include "core/settings.h"
#include "popup/entrymodel.h"
#include "popup/popuppreview.h"
#include "popup/popupwindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCursor>
#include <QEventLoop>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

#include <cstdio>

using namespace maru;

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("marunine.github.io"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Shows one sample MaruPop popup next to the pointer."));
    parser.addHelpOption();
    const QCommandLineOption pinnedOption{QStringLiteral("pinned"),
                                          QStringLiteral("Show the card pinned, which accepts the pointer.")};
    parser.addOption(pinnedOption);
    const QCommandLineOption atOption{
        QStringLiteral("at"),
        QStringLiteral("Pointer position in logical desktop coordinates, as x,y. Qt reports no global pointer "
                       "position on Wayland while the process has no pointer focus, so a placement on a screen "
                       "with a non-zero origin is only reachable through this option."),
        QStringLiteral("x,y")};
    parser.addOption(atOption);
    const QCommandLineOption renderOption{
        QStringLiteral("render"),
        QStringLiteral("Render the card into a PNG file and exit, without mapping a surface."),
        QStringLiteral("file")};
    parser.addOption(renderOption);
    const QCommandLineOption secondsOption{QStringLiteral("seconds"),
                                           QStringLiteral("Seconds the card stays up."),
                                           QStringLiteral("n"),
                                           QStringLiteral("5")};
    parser.addOption(secondsOption);
    parser.process(app);

    QPoint cursor = QCursor::pos();
    if (parser.isSet(atOption)) {
        const QStringList fields = parser.value(atOption).split(QLatin1Char(','));
        if (fields.size() == 2) {
            cursor = QPoint{fields.at(0).toInt(), fields.at(1).toInt()};
        }
    }
    QScreen *screen = QGuiApplication::screenAt(cursor);
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }

    popup::PopupWindow window;
    window.applyTheme();
    window.applyRenderOptions();
    window.setModel(popup::samplePopupModel());
    if (parser.isSet(pinnedOption)) {
        window.setPinned(true);
    }
    if (parser.isSet(renderOption)) {
        // The card alone, with no screen content behind it, which is what a visual check of the
        // renderer and the pitch overlay needs.
        // The surface is mapped first: a scroll bar's range comes from the layout, and the
        // layout is only activated once the widget has been shown. The wait covers the fade,
        // which starts the card at zero opacity.
        window.showNear(cursor, screen);
        QEventLoop settle;
        QTimer::singleShot(400, &settle, &QEventLoop::quit);
        settle.exec();
        QPixmap canvas{window.size()};
        canvas.fill(Qt::transparent);
        window.render(&canvas);
        window.hide();
        const bool saved = canvas.save(parser.value(renderOption));
        std::printf("rendered %dx%d saved=%d\n", canvas.width(), canvas.height(), static_cast<int>(saved));
        return saved ? 0 : 1;
    }

    window.showNear(cursor, screen);

    std::printf("platform=%s cursor=(%d,%d) screen=%s geometry=(%d,%d %dx%d)\n",
                qPrintable(QGuiApplication::platformName()),
                cursor.x(),
                cursor.y(),
                screen != nullptr ? qPrintable(screen->name()) : "none",
                screen != nullptr ? screen->geometry().x() : 0,
                screen != nullptr ? screen->geometry().y() : 0,
                screen != nullptr ? screen->geometry().width() : 0,
                screen != nullptr ? screen->geometry().height() : 0);
    const QRect placed = window.popupRect();
    std::printf("card=(%d,%d %dx%d) margins=(%d,%d) pinned=%d\n",
                placed.x(),
                placed.y(),
                placed.width(),
                placed.height(),
                screen != nullptr ? placed.x() - screen->geometry().x() : placed.x(),
                screen != nullptr ? placed.y() - screen->geometry().y() : placed.y(),
                static_cast<int>(window.isPinned()));
    std::fflush(stdout);

    bool ok = false;
    const int seconds = parser.value(secondsOption).toInt(&ok);
    QTimer::singleShot((ok ? seconds : 5) * 1000, &app, [&window] {
        window.hidePopup();
        QTimer::singleShot(400, QCoreApplication::instance(), &QCoreApplication::quit);
    });
    return QApplication::exec();
}
