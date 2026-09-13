// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The pointer tracker: a KWin JS script that reads workspace.cursorPos and calls back into
// CursorSink over the session bus.
//
// KWin loads a packaged script only when three things hold at once: the package is under
// kwin/scripts/<pluginId>/ in a QStandardPaths::GenericDataLocation directory, kwinrc
// [Plugins] <pluginId>Enabled is true, and Workspace::configChanged has fired since. This class establishes all three,
// then verifies the result over org.kde.kwin.Scripting, and falls back to a runtime loadScript() when the packaged path
// has not loaded within about 2 s.
#pragma once

#include "cursor/cursortracker.h"

#include <QDBusConnection>
#include <QString>

#include <KSharedConfig>

class QDBusServiceWatcher;
class QTimer;

namespace maru::cursor
{

class CursorSink;

class KWinScriptRelay : public CursorTracker
{
    Q_OBJECT

public:
    // Writes the package to paths::kwinScriptInstallDir() and edits KSharedConfig::openConfig
    // ("kwinrc").
    explicit KWinScriptRelay(QObject *parent = nullptr);
    // The same relay against an install directory and a configuration file the caller supplies,
    // which is how the two side effects on the user's home are covered by a test.
    KWinScriptRelay(QString installDir, KSharedConfig::Ptr kwinConfig, QObject *parent = nullptr);
    ~KWinScriptRelay() override;

    // Exports the sink, ensures the package and the kwinrc key, and asks KWin to reload its
    // configuration. Reports the outcome through CursorTracker::availabilityChanged().
    void start();
    // Unloads the script and leaves the package installed, so the entry stays listed under
    // System Settings, Window Management, KWin Scripts.
    void stop();
    // Reads org.kde.kwin.Scripting isScriptLoaded once and updates the availability.
    void refresh();

    void setTracking(bool tracking) override;

    [[nodiscard]] CursorSink *sink() const;
    [[nodiscard]] bool isAvailable() const;
    // Empty while isAvailable() is true.
    [[nodiscard]] QString unavailableReason() const;
    [[nodiscard]] QString installDir() const;

    // The bus the sink is exported on. Assign before start(); the default is the session bus.
    void setConnection(const QDBusConnection &connection);

    // True when org.kde.KWin owns its bus name.
    [[nodiscard]] static bool kwinAvailable();

    // "marupopcursor", the KPlugin Id in the package metadata and the last component of the
    // install directory. KWin builds the script path from it.
    [[nodiscard]] static QString pluginId();
    // installDir + "/contents/code/main.js", the path loadScript() is given.
    [[nodiscard]] static QString scriptPath(const QString &installDir);

    // Writes metadata.json, contents/code/main.js and the version marker from the Qt resources
    // into installDir, for every one of the three whose content differs from the resource.
    // False on a directory or file that could not be written.
    [[nodiscard]] static bool writePackage(const QString &installDir);
    // True when all three files hold the content the resources carry.
    [[nodiscard]] static bool packageMatchesResources(const QString &installDir);

    // Sets [Plugins] marupopcursorEnabled=true and syncs, where the current value is anything
    // else. True when the value was written, false when it was already true.
    static bool enableInConfig(const KSharedConfig::Ptr &config);
    [[nodiscard]] static bool isEnabledInConfig(const KSharedConfig::Ptr &config);

private:
    void reconfigure();
    void startPolling();
    void pollScriptLoaded();
    void loadScriptDirectly();
    void runScript(int id);
    void verifyLoaded(const QString &reasonWhenAbsent);
    // expected is true where the change is one the application asked for -- stop() -- which is
    // logged as information rather than as a warning.
    void setAvailability(bool available, const QString &reason, bool expected = false);
    void watchKWin();

    QString m_installDir;
    KSharedConfig::Ptr m_kwinConfig;
    QDBusConnection m_connection;
    CursorSink *m_sink;
    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer *m_pollTimer = nullptr;
    int m_pollAttempts = 0;
    bool m_available = false;
    bool m_availabilityReported = false;
    QString m_reason;
};

} // namespace maru::cursor
