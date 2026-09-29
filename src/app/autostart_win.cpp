// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Task Manager's Startup page and Settings > Apps > Startup disable an entry by writing a 12-byte
// value of the same name under Explorer\StartupApproved\Run. The first byte is 0x03 for a
// disabled entry and 0x02 for an enabled entry.
#include "app/application.h"
#include "core/logging.h"
#include "core/settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <KLocalizedString>

namespace maru
{

namespace
{

constexpr QLatin1StringView kRunKey{R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)"};
constexpr QLatin1StringView kApprovedKey{
    R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run)"};
constexpr QLatin1StringView kTestRoot{R"(HKEY_CURRENT_USER\Software\MaruPop-test\)"};

// The copy under kTestRoot leaves the Run entry of the user unchanged while a test suite starts an
// Application.
QString registryKey(QLatin1StringView key)
{
    if (!QStandardPaths::isTestModeEnabled()) {
        return QString{key};
    }
    return QString{kTestRoot} + QString{key}.section(QLatin1Char('\\'), -2);
}

// Task Manager displays the FileDescription of the executable from
// src/marupop.rc.cmake in place of the value name.
constexpr QLatin1StringView kValueName{MARUPOP_APPLICATION_ID};

// Windows splits an unquoted Run value at the first space.
QString runCommand()
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
}

// True when command, a Run value, names a missing executable.
bool namesMissingExecutable(const QString &command)
{
    QString path = command.trimmed();
    if (path.startsWith(QLatin1Char('"'))) {
        path = path.section(QLatin1Char('"'), 1, 1);
    }
    return !QFileInfo::exists(path);
}

// The StartupApproved\Run value exists only after the startup manager changed the entry.
// QSettings returns a REG_BINARY value as a QByteArray.
bool disabledInStartupManager()
{
    const QSettings approved{registryKey(kApprovedKey), QSettings::NativeFormat};
    const QByteArray approval = approved.value(QString{kValueName}).toByteArray();
    return !approval.isEmpty() && (static_cast<unsigned char>(approval.at(0)) & 0x01U) != 0;
}

} // namespace

void Application::adoptAutostartState()
{
    const AutostartEntry entry = autostartEntryState();
    if (entry == AutostartEntry::Hidden && PopSettings::autostart()) {
        qCDebug(logApp) << "the autostart entry is disabled in the startup manager; turning the setting off";
        PopSettings::setAutostart(false);
        PopSettings::self()->save();
        return;
    }
    if (entry == AutostartEntry::Enabled && !PopSettings::autostart()) {
        qCDebug(logApp) << "an enabled autostart entry exists; turning the setting on to match it";
        PopSettings::setAutostart(true);
        PopSettings::self()->save();
    }
    if (!PopSettings::autostart()) {
        return;
    }
    // A Run value that names an existing executable belongs to an installed copy, which a
    // development build leaves in place. A moved or uninstalled executable leaves a Run value
    // naming a missing file, which adoptAutostartState() rewrites.
    const QSettings run{registryKey(kRunKey), QSettings::NativeFormat};
    if (entry == AutostartEntry::Missing || namesMissingExecutable(run.value(QString{kValueName}).toString())) {
        qCDebug(logApp) << "writing the autostart entry for" << runCommand();
        applyAutostart();
    }
}

Application::AutostartEntry Application::autostartEntryState()
{
    const QSettings run{registryKey(kRunKey), QSettings::NativeFormat};
    if (!run.contains(QString{kValueName})) {
        return AutostartEntry::Missing;
    }
    return disabledInStartupManager() ? AutostartEntry::Hidden : AutostartEntry::Enabled;
}

bool Application::applyAutostart()
{
    QSettings run{registryKey(kRunKey), QSettings::NativeFormat};
    if (!PopSettings::autostart()) {
        run.remove(QString{kValueName});
        run.sync();
        if (run.status() != QSettings::NoError) {
            reportFailure(i18nc("@title:window", "Autostart Failed"),
                          i18n("The autostart entry %1 could not be removed.", QString{kValueName}));
            return false;
        }
        return true;
    }
    run.setValue(QString{kValueName}, runCommand());
    run.sync();
    if (run.status() != QSettings::NoError) {
        reportFailure(i18nc("@title:window", "Autostart Failed"),
                      i18n("The autostart entry %1 could not be written.", QString{kValueName}));
        return false;
    }
    // A disabled StartupApproved\Run value stays in effect after the Run value is written.
    // Windows reads a missing approval value as enabled.
    if (disabledInStartupManager()) {
        QSettings approved{registryKey(kApprovedKey), QSettings::NativeFormat};
        approved.remove(QString{kValueName});
        approved.sync();
    }
    return true;
}

} // namespace maru
