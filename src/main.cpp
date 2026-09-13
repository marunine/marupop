// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/application.h"
#include "capture/wlrframesource.h"
#include "marupop_version.h"
#include "platform/backend.h"
#include "platform/session.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QTextStream>

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>

// KStyleManager arrived in KConfigWidgets 6.3; the guard is the form Dolphin and Gwenview use.
#define HAVE_STYLE_MANAGER __has_include(<KStyleManager>)
#if HAVE_STYLE_MANAGER
#include <KStyleManager>
#endif

namespace
{

// --check-authorization reports on a capture that is producing nothing, which is a question
// asked from a TTY or over SSH as readily as from a session. QApplication aborts the process
// where there is no display, so the option is answered here, ahead of it, over a
// QGuiApplication where WAYLAND_DISPLAY names a reachable compositor and over a QCoreApplication
// where it does not. argv is read directly rather than parsed, because QCommandLineParser reads
// QCoreApplication::arguments() and so needs the object this runs instead of.
//
// Exactly one argument, and that argument the option: any other command line goes to
// QCommandLineParser, which answers the option after QApplication is up and reports the errors
// it is the authority on. `--settings --check-authorization` is a command line with two verbs,
// and `-- --check-authorization` is a positional argument, and reading argv by hand would call
// both of them a request.
bool authorizationIsTheWholeCommandLine(int argc, char **argv)
{
    return argc == 2 && qstrcmp(argv[1], "--check-authorization") == 0;
}

// Whether WAYLAND_DISPLAY names a socket that exists, resolved the way libwayland resolves it:
// an absolute value as given, a bare name under XDG_RUNTIME_DIR.
//
// The variable being set is not enough. It is inherited by a sudo shell, by a container started
// without the socket bind-mounted, and by anything running under a different XDG_RUNTIME_DIR, and
// in each of those the compositor it names is unreachable.
bool waylandSocketExists()
{
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (display.isEmpty()) {
        return false;
    }
    if (QFileInfo{display}.isAbsolute()) {
        return QFileInfo::exists(display);
    }
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return !runtimeDir.isEmpty() && QFileInfo::exists(runtimeDir + QLatin1Char('/') + display);
}

// screencopyBound says whether zwlr_screencopy_manager_v1 was bound, which only a caller with a
// QGuiApplication can answer. False from the QCoreApplication path, where the lines still name
// what the session requires.
int reportAuthorization(bool screencopyBound)
{
    const maru::platform::CaptureReport report =
        maru::platform::captureReportFor(maru::platform::detect(), screencopyBound);
    QTextStream out(stdout);
    for (const QString &line : report.lines) {
        out << line << '\n';
    }
    if (!report.remedy.isEmpty()) {
        out << report.remedy << '\n';
    }
    // Exit status 1 for an unauthorized binary, so a script can branch on it.
    return report.authorized ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (authorizationIsTheWholeCommandLine(argc, argv)) {
        KLocalizedString::setApplicationDomain(QByteArrayLiteral("marupop"));
        // A Wayland session is the one case where the report needs a display connection: the
        // wlroots-family verdict is whether zwlr_screencopy_manager_v1 binds, and
        // platform::detect()'s third probe reads the same registry. QGuiApplication reaches the
        // compositor WAYLAND_DISPLAY names and never opens a window, so the option stays
        // answerable from a TTY, over SSH and on a host with no display at all, which is what
        // the QCoreApplication branch below covers.
        if (waylandSocketExists()) {
            // Only where the caller named no platform. Overwriting QT_QPA_PLATFORM would abort
            // the process this option exists to diagnose: a session that sets it to xcb or
            // offscreen, or a host with no qtwayland installed, answers a forced "wayland" with
            // qFatal on the missing plugin. Left alone, Qt detects a platform and falls back.
            if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
                qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland"));
            }
            QGuiApplication app(argc, argv);
            return reportAuthorization(maru::capture::WlrFrameSource::available());
        }
        QCoreApplication app(argc, argv);
        return reportAuthorization(false);
    }

    QApplication app(argc, argv);
#if HAVE_STYLE_MANAGER
    // Applies the style the user configured, falling back to Breeze. A session running a
    // platform theme other than KDE's otherwise leaves the widgets on the Qt default style.
    KStyleManager::initStyle();
#endif
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("marupop"));

    // LGPL_V3 matches LICENSE.TXT and the SPDX-License-Identifier headers, and lets
    // KAboutData supply the license text the About dialog shows.
    KAboutData about(QStringLiteral("marupop"),
                     i18n("MaruPop"),
                     QStringLiteral(MARUPOP_VERSION_STRING),
                     i18n("Japanese Popup Dictionary"),
                     KAboutLicense::LGPL_V3,
                     i18nc("@info:credit", "© 2026 marunine"));
    about.addAuthor(QStringLiteral("marunine"),
                    i18nc("@info:credit", "Author and maintainer"),
                    {},
                    QStringLiteral("https://github.com/marunine"));
    about.setHomepage(QStringLiteral("https://github.com/marunine/marupop"));
    // DrKonqi offers the bug address after a crash. It runs its bugs.kde.org reporting
    // assistant only for submit@bugs.kde.org, so a URL sends the report to that URL instead.
    about.setBugAddress(QByteArrayLiteral("https://github.com/marunine/marupop/issues"));
    KAboutData::setApplicationData(about);
    // KAboutData sets the organization domain to kde.org, and KDBusService builds its unique
    // service name out of the reversed domain plus the application name -- which would claim
    // org.kde.marupop. The KWin cursor script calls io.github.marunine.marupop, so a name built
    // from kde.org leaves the relay loaded, reporting itself available, and delivering no
    // pointer position at all. Setting the project's own domain is what makes the two agree.
    QCoreApplication::setOrganizationDomain(QStringLiteral("marunine.github.io"));
    // Installs the crash handler that hands the backtrace to DrKonqi. A tray-resident process
    // otherwise crashes silently, and the user's only symptom is a hotkey that stopped working.
    KCrash::initialize();
    // The reverse-DNS application id, which the installed desktop entry is named after and
    // which its Icon= repeats. KWin, the taskbar and KGlobalAccel all key off this.
    QGuiApplication::setDesktopFileName(QStringLiteral(MARUPOP_APPLICATION_ID));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(MARUPOP_APPLICATION_ID)));
    // The app is tray-resident: neither a closed dialog nor a helper launched through KIO
    // finishing may end the process (Dolphin's gotcha).
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setQuitLockEnabled(false);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    maru::Application::addCommandLineOptions(parser);
    // Registered here rather than in addCommandLineOptions(), which is also the parser a second
    // invocation's forwarded arguments run through: this option reports on the process that
    // parses it, so forwarding it to the resident one would answer about the wrong binary.
    const QCommandLineOption checkAuthorizationOption(QStringLiteral("check-authorization"),
                                                      i18n("Check screen capture permissions and exit"));
    parser.addOption(checkAuthorizationOption);
    parser.process(app);
    about.processCommandLine(&parser);

    // Reached by every command line the fast path above declines, which is every one carrying a
    // second argument. Still ahead of KDBusService, which would hand the option to the resident
    // process and have it report on a different binary.
    if (parser.isSet(checkAuthorizationOption)) {
        // A QApplication is already up here, so the Wayland registry answers whether the
        // wlroots-family pixel source would bind.
        return reportAuthorization(maru::capture::WlrFrameSource::available());
    }

    // A second invocation forwards its arguments here instead of starting another instance.
    KDBusService service(KDBusService::Unique);

    maru::Application application;
    application.start();
    QObject::connect(&service,
                     &KDBusService::activateRequested,
                     &application,
                     [&application](const QStringList &arguments, const QString & /*workingDirectory*/) {
                         application.activate(arguments);
                     });
    // The first invocation only runs what it was asked to: a verbless start (autostart, or the
    // launch shortcut starting the process) settles into the tray rather than opening a window.
    application.handleCommandLine(QCoreApplication::arguments());

    return QApplication::exec();
}
