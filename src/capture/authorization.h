// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Copied from marusnap/src/capture/authorization.{h,cpp}, with the required-interface lists
// reduced to the two keys data/io.github.marunine.marupop.desktop.cmake declares.
#pragma once

#include <QString>
#include <QStringList>

namespace maru::capture::authorization
{

// Repeats the lookup KWin performs to decide whether a process may bind the restricted Wayland
// globals and call the restricted D-Bus interfaces (kwin/src/utils/serviceutils.h): it maps the
// caller's /proc/pid/exe to the installed desktop entry whose Exec names that path, and reads
// X-KDE-Wayland-Interfaces and X-KDE-DBUS-Restricted-Interfaces from it.
//
// A build tree, a second prefix or a stale entry leaves the match failing, and the symptom is a
// capture that produces nothing with no error the user can act on. ARCHITECTURE.md, Feasibility
// notes, records the gate; this is what reports on it.

// The interfaces data/io.github.marunine.marupop.desktop.cmake declares. The capture path stops
// working when the entry KWin matches leaves out org.kde.KWin.ScreenShot2.
[[nodiscard]] QStringList requiredWaylandInterfaces();
[[nodiscard]] QStringList requiredDBusInterfaces();

struct Report
{
    // The path the lookup ran against, canonicalized as KWin canonicalizes it.
    QString executablePath;
    bool executableExists = false;
    // The entry KWin would match, or empty for a path no installed entry names.
    QString desktopEntryPath;
    QStringList waylandInterfaces;
    QStringList dbusInterfaces;

    // The members of requiredWaylandInterfaces() and requiredDBusInterfaces() the matched entry
    // leaves out. Every required interface, for a path with no entry.
    [[nodiscard]] QStringList missingInterfaces() const;
    [[nodiscard]] bool authorized() const;
};

[[nodiscard]] Report checkExecutable(const QString &executablePath);

// The running process, through QCoreApplication::applicationFilePath(), which Qt resolves from
// /proc/self/exe -- the same file KWin reads for the caller. Reporting on this rather than on an
// assumed install path is what makes the answer cover the process that would do the capturing.
[[nodiscard]] Report checkThisProcess();

// The report as printable lines, one per line, ending with a verdict. Shared by
// `marupop --check-authorization` and tools/authcheck.cpp so the two say the same thing.
[[nodiscard]] QStringList describe(const Report &report);

} // namespace maru::capture::authorization
