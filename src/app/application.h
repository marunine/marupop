// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "lookup/lookuptypes.h"
#include "popup/entrymodel.h"
#include "scan/hitcontext.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <memory>

class KAboutApplicationDialog;
class KJob;
class QCommandLineParser;
class QTimer;

namespace maru::platform
{
class Backend;
}

namespace maru::deconj
{
class RuleSet;
}

namespace maru::dict
{
class DictionaryImportJob;
class DictionaryManager;
class UpdateCheckJob;
} // namespace maru::dict

namespace maru::lookup
{
class Engine;
}

namespace maru::ocr
{
class ModelStore;
class OcrService;
} // namespace maru::ocr

namespace maru::popup
{
class PopupWindow;
}

namespace maru::scan
{
class ScanController;
}

namespace maru
{

class LookupWindow;
class Notifier;
class SettingsDialog;
class ShortcutRegistry;
class TrayIcon;

// The resident shell: it owns every service, wires them to each other, routes requests from
// the tray, the hotkeys and the command line, and re-applies the settings the dialog writes.
// Kept out of main(), which only builds the KAboutData, the parser and the KDBusService.
//
// Nothing is constructed by the constructor: start() builds the pipeline, so a test can
// construct an Application without registering a StatusNotifierItem on the session bus.
class Application : public QObject
{
    Q_OBJECT

public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    // Builds the pipeline, shows the tray icon, registers the hotkeys and reconciles the
    // autostart entry with the setting.
    void start();

    static void addCommandLineOptions(QCommandLineParser &parser);
    // Runs whatever the arguments ask for. False means nothing was requested.
    bool handleCommandLine(const QStringList &arguments);
    // A second invocation: the tray-resident process is asked to do something instead of a new
    // one starting. Plain `marupop` with no verb arrives here from the launcher and from a
    // System Settings "_launch" shortcut, and opens the settings window rather than being a
    // silent no-op.
    void activate(const QStringList &arguments);

    void setScanning(bool scanning);
    void toggleScanning();

    // Re-applies everything the settings dialog can change while the application is resident.
    // Public because it is also what the first-run dialog's answer is applied through.
    void applySettings();

    [[nodiscard]] TrayIcon *trayIcon() const;
    // The lookup window, or nullptr until the first request for it builds one.
    [[nodiscard]] LookupWindow *lookupWindow() const;

    // Shows the lookup window, building it on the first call, or closes it where it is shown.
    void toggleLookupWindow();
    void showLookupWindow();

private:
    void buildPipeline();
    void wireScanning();
    void wireHotkeys();

    void showSettings();
    void showDictionaryManager();
    // KAboutApplicationDialog over the KAboutData main() installed.
    void showAbout();
    // The first-run model prompt, shown once and only where no engine can run yet.
    void maybeRunFirstRun();

    void copyCurrentWord();
    void togglePinned();

    // The scan's result count and the popup's suppression, for the lookup window's visibility
    // and the two LookupWindow settings.
    void applyLookupWindowState();

    // The session's capture gate and the compositor's own availability, reported once at start:
    // a capture that produces nothing has no other symptom the user could act on. On KDE that
    // is the desktop-entry lookup KWin performs; on a wlroots session it is whether
    // zwlr_screencopy_manager_v1 was bound.
    void checkCaptureAvailability();

    // The dictionary auto-update chain: a check job per dictionary that is due, a download on a
    // positive answer, and an import on a finished download.
    void checkDictionaryUpdates();
    void onUpdateCheckFinished(dict::UpdateCheckJob *job);
    void startDictionaryDownload(const QUuid &id);
    void startDictionaryImport(const QUuid &id);

    // States of the entry in ~/.config/autostart. Hidden is the state System Settings'
    // Autostart module writes to disable an entry, so it is the one that means the user turned
    // autostart off elsewhere; Missing means the entry has to be written again.
    enum class AutostartEntry
    {
        Missing,
        Hidden,
        Enabled,
    };

    // Reconciles the setting with the autostart entry on disk at startup: an entry hidden in
    // System Settings turns the setting off, and a setting left on writes a missing or stale
    // entry again.
    void adoptAutostartState();
    [[nodiscard]] static AutostartEntry autostartEntryState();
    // Installs or removes the autostart entry to match the setting. False when the entry on
    // disk still disagrees with it, which is reported to the user before returning.
    bool applyAutostart();
    void reportFailure(const QString &title, const QString &message);
    // A short line in the tray tooltip that clears itself, for a hotkey that has no other
    // visible effect.
    void flashStatus(const QString &message);

    TrayIcon *m_tray = nullptr;
    Notifier *m_notifier = nullptr;
    ShortcutRegistry *m_hotkeys = nullptr;

    dict::DictionaryManager *m_dictionaries = nullptr;
    // Shared rather than held by value: lookup::Engine keeps a reference to it, and the
    // header stays free of deconj/deconjugator.h this way.
    std::shared_ptr<deconj::RuleSet> m_rules;
    std::shared_ptr<lookup::Engine> m_engine;
    // The lookup the scan controller runs on a pool thread, and the adapter the popup is fed
    // through. Both are indirections rather than direct calls so that the shell still builds
    // and runs while lookup/ is being written.
    std::function<lookup::Response(const lookup::Request &)> m_lookupFunction;
    std::function<popup::PopupModel(const lookup::Response &)> m_popupAdapter;

    ocr::OcrService *m_ocr = nullptr;
    ocr::ModelStore *m_models = nullptr;
    // The pointer source, the pixel source, the lock watcher and the shortcut registry the
    // detected session offers. m_hotkeys above is the backend's shortcut registry, held
    // separately because the settings dialog takes it.
    platform::Backend *m_platform = nullptr;
    scan::ScanController *m_scan = nullptr;
    std::unique_ptr<popup::PopupWindow> m_popup;
    // Kept across a close, so a window opened again shows the last lookup at once.
    std::unique_ptr<LookupWindow> m_lookupWindow;
    // The visibility LookupWindow::visibilityChanged() last reported.
    bool m_lookupWindowOpen = false;

    // The hit the popup is showing, which the copy-word shortcut reads.
    lookup::Response m_lastResponse;
    scan::HitContext m_lastHit;

    QTimer *m_updateTimer = nullptr;
    QTimer *m_statusFlash = nullptr;

    QPointer<KAboutApplicationDialog> m_aboutDialog; // created on the first About request
    QPointer<QWidget> m_dictionaryDialog;            // the dictui window, one at a time
    bool m_autostartApplied = false;                 // Autostart value the entry was last written for
};

} // namespace maru
