// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/authorization.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>

#include <KApplicationTrader>
#include <KLocalizedString>
#include <KService>

namespace maru::capture::authorization
{

namespace
{

// KWin reads the first token of Exec and compares its canonical path to the caller's, which is
// why the argument-carrying [Desktop Action] Exec lines in the same file match as well.
KService::Ptr serviceFor(const QString &canonicalPath)
{
    const auto services = KApplicationTrader::query([&canonicalPath](const KService::Ptr &service) {
        const QStringList command = QProcess::splitCommand(service->exec());
        if (command.isEmpty()) {
            return false;
        }
        return QFileInfo(command.first()).canonicalFilePath() == canonicalPath;
    });
    return services.isEmpty() ? KService::Ptr{} : services.first();
}

} // namespace

QStringList requiredWaylandInterfaces()
{
    // Capture uses ScreenShot2 over D-Bus; no restricted Wayland interface is needed.
    return {};
}

QStringList requiredDBusInterfaces()
{
    return {QStringLiteral("org.kde.KWin.ScreenShot2")};
}

QStringList Report::missingInterfaces() const
{
    QStringList missing;
    for (const QString &interface : requiredWaylandInterfaces()) {
        if (!waylandInterfaces.contains(interface)) {
            missing.append(interface);
        }
    }
    for (const QString &interface : requiredDBusInterfaces()) {
        if (!dbusInterfaces.contains(interface)) {
            missing.append(interface);
        }
    }
    return missing;
}

bool Report::authorized() const
{
    return !desktopEntryPath.isEmpty() && missingInterfaces().isEmpty();
}

Report checkExecutable(const QString &executablePath)
{
    Report report;
    report.executableExists = QFileInfo::exists(executablePath);
    // An absent file canonicalizes to an empty string, which would match every entry whose Exec
    // is equally absent, so the path is reported as given and the lookup is skipped.
    if (!report.executableExists) {
        report.executablePath = executablePath;
        return report;
    }
    report.executablePath = QFileInfo(executablePath).canonicalFilePath();
    const KService::Ptr service = serviceFor(report.executablePath);
    if (!service) {
        return report;
    }
    report.desktopEntryPath = service->entryPath();
    report.waylandInterfaces = service->property<QStringList>(QStringLiteral("X-KDE-Wayland-Interfaces"));
    report.dbusInterfaces = service->property<QStringList>(QStringLiteral("X-KDE-DBUS-Restricted-Interfaces"));
    return report;
}

Report checkThisProcess()
{
    return checkExecutable(QCoreApplication::applicationFilePath());
}

QStringList describe(const Report &report)
{
    QStringList lines{report.executablePath};
    if (!report.executableExists) {
        lines.append(i18nc("@info authorization report", "  Executable not found."));
        return lines;
    }
    if (report.desktopEntryPath.isEmpty()) {
        lines.append(
            i18nc("@info authorization report",
                  "  Screen capture denied: no installed desktop entry has an Exec matching the executable path."));
        lines.append(i18nc("@info authorization report", "  Install the desktop entry and run kbuildsycoca6."));
        return lines;
    }
    const QString separator = QStringLiteral(", ");
    const QString none = i18nc("@info authorization report, an empty list of interfaces", "(none)");
    lines.append(i18nc("@info authorization report", "  Desktop entry: %1", report.desktopEntryPath));
    lines.append(i18nc("@info authorization report",
                       "  X-KDE-Wayland-Interfaces: %1",
                       report.waylandInterfaces.isEmpty() ? none : report.waylandInterfaces.join(separator)));
    lines.append(i18nc("@info authorization report",
                       "  X-KDE-DBUS-Restricted-Interfaces: %1",
                       report.dbusInterfaces.isEmpty() ? none : report.dbusInterfaces.join(separator)));
    const QStringList missing = report.missingInterfaces();
    if (missing.isEmpty()) {
        lines.append(i18nc("@info authorization report", "  Screenshots: authorized."));
    } else {
        lines.append(i18nc("@info authorization report", "  Authorization missing: %1.", missing.join(separator)));
    }
    return lines;
}

} // namespace maru::capture::authorization
