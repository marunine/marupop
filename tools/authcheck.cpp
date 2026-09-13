// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Privilege-gate diagnostic for a binary other than this one. Give it executable paths -- or
// nothing, to check the installed and build-tree marupop -- and it prints, for each, the
// desktop entry KWin would find and the two X-KDE-* declarations it would read from it.
//
// `marupop --check-authorization` answers the same question for the installed application, and
// answers it about /proc/self/exe, which is the file KWin actually reads. This tool covers the
// paths that flag cannot reach: the two probes beside it, the test binaries, and a build-tree
// binary checked from somewhere else. Both print through
// maru::capture::authorization::describe(), so the two say the same thing.

#include "capture/authorization.h"

#include <QCoreApplication>
#include <QTextStream>

#include <KLocalizedString>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("marupop-authcheck"));
    // describe() goes through i18nc(), and the catalogue it looks in is the application domain.
    // Without this the tool prints English where `marupop --check-authorization` prints the
    // installed translation of the same report.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("marupop"));

    QStringList paths = QCoreApplication::arguments();
    paths.removeFirst();
    if (paths.isEmpty()) {
        // The directory this build installs into, not a literal /usr/bin: a check that reports
        // on a path the build never writes to answers a question nobody asked.
        paths.append(QStringLiteral(MARUPOP_INSTALLED_BINARY));
        paths.append(QCoreApplication::applicationDirPath() + QStringLiteral("/marupop"));
    }

    QTextStream out(stdout);
    bool allAuthorized = true;
    for (const QString &path : std::as_const(paths)) {
        const maru::capture::authorization::Report report = maru::capture::authorization::checkExecutable(path);
        const QStringList lines = maru::capture::authorization::describe(report);
        for (const QString &line : lines) {
            out << line << '\n';
        }
        out << '\n';
        allAuthorized = allAuthorized && report.authorized();
    }
    return allAuthorized ? 0 : 1;
}
