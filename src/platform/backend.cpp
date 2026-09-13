// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "platform/backend.h"

#include "app/hotkeyregistry.h"
#include "app/wlrshortcuts.h"
#include "capture/authorization.h"
#include "capture/framesource.h"
#include "capture/hyprlandconfig.h"
#include "capture/wlrframesource.h"
#include "core/logging.h"
#include "cursor/cursortracker.h"
#include "cursor/hyprcursortracker.h"
#include "cursor/kwinscriptrelay.h"
#include "cursor/lockwatcher.h"
#include "cursor/screensaverlockwatcher.h"
#include "cursor/wlrlockwatcher.h"

#include <QCoreApplication>

#include <KLocalizedString>

namespace maru::platform
{

namespace
{

QString hyprlandConfigHeader(const QString &fileName)
{
    return i18n("Add this line to %1 and reload the configuration:", fileName);
}

} // namespace

Backend::Backend(QObject *parent)
    : Backend(detect(), parent)
{}

Backend::Backend(Session session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    build();
}

Backend::~Backend() = default;

Session Backend::session() const
{
    return m_session;
}

cursor::CursorTracker *Backend::tracker() const
{
    return m_tracker;
}

capture::FrameSource *Backend::frames() const
{
    return m_frames;
}

cursor::LockWatcher *Backend::lockWatcher() const
{
    return m_lockWatcher;
}

ShortcutRegistry *Backend::shortcuts() const
{
    return m_shortcuts;
}

bool Backend::capturesOwnWindows() const
{
    return m_frames != nullptr && m_frames->capturesOwnWindows();
}

void Backend::build()
{
    switch (m_session) {
    case Session::KdePlasma: {
        m_relay = new cursor::KWinScriptRelay(this);
        m_tracker = m_relay;
        m_frames = new capture::KWinFrameSource(this);
        m_lockWatcher = new cursor::ScreenSaverLockWatcher(this);
        m_shortcuts = new HotkeyRegistry(this);
        break;
    }
    case Session::Hyprland:
    case Session::Wlroots: {
        // The pointer source is the one piece the two differ in: `cursorpos` is a Hyprland
        // command, and a wlroots compositor without it has no global pointer position at all.
        if (m_session == Session::Hyprland) {
            m_hyprTracker = new cursor::HyprCursorTracker(this);
            m_tracker = m_hyprTracker;
        } else {
            m_tracker = new cursor::UnavailableTracker(
                i18n("Pointer tracking unavailable. MaruPop requires KDE Plasma or Hyprland."), this);
        }

        auto *frames = new capture::WlrFrameSource(this);
        connect(frames, &capture::WlrFrameSource::noScreenShareMissing, this, [this](const QString &line) {
            // The line arrives already written for this user's configuration language, so the
            // sentence that introduces it names the same file.
            Q_EMIT configurationMissing(hyprlandConfigHeader(capture::hyprlandConfig().fileName()), line);
        });
        m_frames = frames;

        // hyprland_lock_notifier_v1 where the compositor has it, and org.freedesktop.ScreenSaver
        // where it does not: a wlroots session running a portal plus a locker exports the second.
        if (cursor::WlrLockWatcher::available()) {
            m_lockWatcher = new cursor::WlrLockWatcher(this);
        } else {
            m_lockWatcher = new cursor::ScreenSaverLockWatcher(this);
        }

        // kglobalacceld receives key events through KWin's own integration, which no
        // wlroots-family compositor carries, so KGlobalAccel registers actions no press ever
        // reaches. WlrShortcuts is taken where the compositor offers the protocol, and the
        // KGlobalAccel path is left for a session that offers neither: it still stores the
        // bindings the settings page shows.
        if (WlrShortcuts::available()) {
            auto *shortcuts = new WlrShortcuts(this);
            m_shortcuts = shortcuts;
        } else {
            m_shortcuts = new HotkeyRegistry(this);
        }
        break;
    }
    case Session::Unknown: {
        m_tracker = new cursor::UnavailableTracker(
            i18n("Unsupported desktop session. MaruPop requires KDE Plasma or Hyprland."), this);
        auto *frames = new capture::FakeFrameSource(this);
        frames->setFailure(i18n("Screen capture unavailable. MaruPop requires KDE Plasma or Hyprland."));
        m_frames = frames;
        m_lockWatcher = new cursor::ScreenSaverLockWatcher(this);
        m_shortcuts = new HotkeyRegistry(this);
        break;
    }
    }
    qCInfo(logPlatform) << "built the" << sessionId(m_session) << "backends";
}

void Backend::startTracking()
{
    if (m_relay != nullptr) {
        m_relay->start();
    }
    if (m_hyprTracker != nullptr) {
        m_hyprTracker->start();
    }
}

void Backend::stopTracking()
{
    if (m_relay != nullptr) {
        m_relay->stop();
    }
    if (m_hyprTracker != nullptr) {
        m_hyprTracker->stop();
    }
}

CaptureReport captureReportFor(Session session, bool screencopyBound)
{
    CaptureReport report;
    switch (session) {
    case Session::KdePlasma: {
        const capture::authorization::Report kde = capture::authorization::checkThisProcess();
        report.authorized = kde.authorized();
        report.lines = capture::authorization::describe(kde);
        if (!report.authorized) {
            report.remedy = i18n("Screen capture requires a desktop entry matching the MaruPop executable. Install "
                                 "MaruPop or run tools/install-dev-desktop.sh for a development build.");
        }
        break;
    }
    case Session::Hyprland:
    case Session::Wlroots: {
        report.authorized = screencopyBound;
        report.lines.append(i18n("Session: %1", sessionName(session)));
        report.lines.append(i18n("Executable: %1", QCoreApplication::applicationFilePath()));
        report.lines.append(
            i18n("zwlr_screencopy_manager_v1: %1", screencopyBound ? i18n("bound") : i18n("not bound")));
        // Both lines below name a file and an option that exist on Hyprland alone, so they are
        // Hyprland's branch only. The layer rule is the one thing this report cannot state
        // generally: `no_screen_share` is a Hyprland effect, and no other wlroots compositor
        // offers any way to keep one client's surface out of a zwlr_screencopy_v1 copy, so a
        // sway or river user has nothing to add and needs to be told that rather than handed a
        // line their compositor will reject.
        if (session == Session::Hyprland) {
            // Written in whichever of Hyprland's configuration languages this user's own
            // configuration is in, which capture::hyprlandConfig() decides the same way the
            // compositor does.
            const capture::HyprlandConfig config = capture::hyprlandConfig();
            report.lines.append(
                i18n("Required %1 line: %2", config.fileName(), capture::noScreenShareRule(config.language)));
            // The permission line is needed when the user enables permission enforcement.
            // The report includes it because a denied copy and a missing output can produce the
            // same failed() event on the wire.
            report.lines.append(
                i18n("Required %1 line if %2 is enabled: %3",
                     config.fileName(),
                     capture::enforcePermissionsOption(config.language),
                     capture::screencopyPermissionRule(config.language, QCoreApplication::applicationFilePath())));
        } else {
            report.lines.append(i18n("Captured regions may include MaruPop's popup. Excluding the popup on this "
                                     "compositor is unsupported."));
        }
        report.lines.append(report.authorized ? i18n("Screen capture: available.")
                                              : i18n("Screen capture: unavailable."));
        if (!report.authorized) {
            report.remedy =
                i18n("Screen capture unavailable: the compositor does not support zwlr_screencopy_manager_v1.");
        }
        break;
    }
    case Session::Unknown:
        report.lines.append(i18n("Session: %1", sessionName(session)));
        report.lines.append(i18n("Screen capture: unavailable."));
        report.remedy = i18n("Unsupported desktop session. MaruPop requires KDE Plasma or Hyprland.");
        break;
    }
    return report;
}

CaptureReport Backend::captureReport() const
{
    const auto *frames = qobject_cast<const capture::WlrFrameSource *>(m_frames);
    CaptureReport report = captureReportFor(m_session, frames != nullptr && frames->isAvailable());
    if (!report.authorized && frames != nullptr && !frames->unavailableReason().isEmpty()) {
        report.remedy = frames->unavailableReason();
    }
    return report;
}

} // namespace maru::platform
