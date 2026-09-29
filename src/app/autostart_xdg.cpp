// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/application.h"
#include "core/logging.h"
#include "core/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <KDesktopFile>
#include <KLocalizedString>

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <unistd.h>

namespace maru
{

namespace
{

// The installed desktop entry is named after the application id, and the autostart copy has to
// carry the same name: KWin resolves a caller's /proc/pid/exe to a desktop entry, and the XDG
// autostart directory is one of the places an entry of that name is looked up.
constexpr QLatin1StringView desktopFileName(MARUPOP_APPLICATION_ID ".desktop");

QString autostartFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart/") +
           desktopFileName;
}

QString installedDesktopFilePath()
{
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopFileName);
}

// Empty for a path that is empty or unreadable, which every caller treats as "nothing to
// compare against" rather than "the two files differ".
QByteArray fileContents(const QString &path)
{
    QFile file{path};
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// True once the descriptor's data is on disk. A signal interrupts fsync() often enough in a
// Qt application to be worth retrying, and a filesystem that implements no fsync reports
// EINVAL or ENOSYS for data that was still written correctly.
bool syncToDisk(int descriptor)
{
    while (fsync(descriptor) != 0) {
        if (errno == EINTR) {
            continue;
        }
        return errno == EINVAL || errno == ENOSYS;
    }
    return true;
}

} // namespace

void Application::adoptAutostartState()
{
    // A process killed between the write and the rename in applyAutostart() leaves the staging
    // file behind. XDG autostart reads *.desktop, so it starts nothing, and a start is the one
    // moment no write is in flight to remove it at.
    QFile::remove(autostartFilePath() + QStringLiteral(".new"));

    const AutostartEntry entry = autostartEntryState();
    // System Settings' Autostart module edits the same entry, and its toggle disables one by
    // writing Hidden=true rather than deleting it. Hidden is therefore the one state that
    // means the user turned autostart off somewhere else, and reading it here is what stops
    // the next apply from rewriting the file from a setting the user changed elsewhere.
    //
    // A missing entry is written again. That is indistinguishable on disk from the module's
    // Remove button, which deletes the entry, so a removal made that way is undone at the next
    // start; the setting in this application stays authoritative for a state a home directory
    // restored from a backup produces just as readily.
    if (entry == AutostartEntry::Hidden && PopSettings::autostart()) {
        qCDebug(logApp) << "the autostart entry is hidden; turning the setting off to match it";
        PopSettings::setAutostart(false);
        PopSettings::self()->save();
        return;
    }
    if (entry == AutostartEntry::Enabled && !PopSettings::autostart()) {
        qCDebug(logApp) << "an enabled autostart entry exists; turning the setting on to match it";
        PopSettings::setAutostart(true);
        PopSettings::self()->save();
        // Falls through to the refresh below rather than returning. The entry that got here is
        // one this application did not write -- added through System Settings' Autostart
        // module, for instance -- so its contents are whatever that produced, and only a copy
        // of the installed entry carries the X-KDE-* keys the autostarted process needs.
    }
    if (!PopSettings::autostart()) {
        return;
    }
    // The setting asks for the entry. Write it when it has gone missing, and refresh a copy an
    // upgrade left behind, so a key added to the installed entry reaches the autostarted
    // process as well. The comparison is over the whole file, so an edit made to the autostart
    // copy by hand is reverted here: an Exec= line that stops matching /proc/pid/exe is the
    // way to lose the capture interfaces, and the installed entry is the one KWin resolves.
    const QByteArray installed = fileContents(installedDesktopFilePath());
    if (installed.isEmpty()) {
        // A start can neither fix a missing installation nor ask the user to, and a run from a
        // build tree reaches this every time, so the state stays in the log. The settings
        // dialog turning autostart on runs applyAutostart(), which reports it at the point the
        // user can act on it.
        qCWarning(logApp) << "no readable" << desktopFileName << "; the autostart entry is left as it is";
        return;
    }
    if (entry == AutostartEntry::Missing || installed != fileContents(autostartFilePath())) {
        qCDebug(logApp) << "writing the autostart entry from the installed one";
        applyAutostart();
    }
}

Application::AutostartEntry Application::autostartEntryState()
{
    const QString target = autostartFilePath();
    if (!QFile::exists(target)) {
        return AutostartEntry::Missing;
    }
    return KDesktopFile{target}.desktopGroup().readEntry("Hidden", false) ? AutostartEntry::Hidden
                                                                          : AutostartEntry::Enabled;
}

bool Application::applyAutostart()
{
    const QString target = autostartFilePath();
    if (!PopSettings::autostart()) {
        // Reported, because adoptAutostartState() reads an entry that survives removal as the
        // user having enabled autostart and turns the setting back on at the next start.
        if (QFile::exists(target) && !QFile::remove(target)) {
            reportFailure(i18nc("@title:window", "Autostart Failed"),
                          i18n("The autostart entry %1 could not be removed.", target));
            return false;
        }
        return true;
    }

    // A byte-exact copy of the installed entry, X-KDE-* keys included: KWin maps a caller's
    // /proc/pid/exe to a desktop entry, and a stripped copy that ever won that match would
    // silently cost the process its capture interfaces. Plasma's own Autostart module copies
    // the file for the same reason (plasma-workspace, kcms/autostart/autostartmodel.cpp).
    const QByteArray installed = fileContents(installedDesktopFilePath());
    if (installed.isEmpty()) {
        // Covers a missing, unreadable and zero-byte installed entry alike, and matches the
        // guard in adoptAutostartState(). An entry already in the autostart directory is left
        // alone: it names a binary that runs, and an empty replacement would autostart nothing.
        reportFailure(i18nc("@title:window", "Autostart Failed"),
                      i18n("Could not read the desktop entry %1. Install MaruPop to enable autostart.",
                           QLatin1StringView{desktopFileName}));
        return false;
    }
    QDir{}.mkpath(QFileInfo{target}.path());
    // Written beside the target and renamed onto it, so a write that runs out of space or
    // hits a read-only directory leaves the working entry that was there. std::filesystem
    // rather than QFile::rename, which refuses an existing target and would need the working
    // entry removed first.
    const QString staging = target + QStringLiteral(".new");
    QFile file{staging};
    // Synced before the rename: the rename is what makes the entry visible, and a crash between
    // the two would otherwise leave a zero-length entry that autostarts nothing.
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(installed) != installed.size() ||
        !file.flush() || !syncToDisk(file.handle())) {
        file.remove();
        reportFailure(i18nc("@title:window", "Autostart Failed"),
                      i18n("The autostart entry %1 could not be written.", target));
        return false;
    }
    file.close();
    std::error_code error;
    std::filesystem::rename(std::filesystem::path{QFile::encodeName(staging).toStdString()},
                            std::filesystem::path{QFile::encodeName(target).toStdString()},
                            error);
    if (error) {
        QFile::remove(staging);
        reportFailure(i18nc("@title:window", "Autostart Failed"),
                      i18n("The autostart entry %1 could not be written.", target));
        return false;
    }
    // The setting keeps whatever the user chose on every path above. Rewriting it here would
    // leave the open settings dialog showing a checkbox that disagrees with the stored value,
    // and the next start reading an entry that disagrees with the setting.
    return true;
}

} // namespace maru
