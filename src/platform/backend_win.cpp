// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/winshortcuts.h"
#include "capture/framesource.h"
#include "capture/winframesource.h"
#include "core/logging.h"
#include "cursor/cursortracker.h"
#include "cursor/wincursortracker.h"
#include "cursor/winlockwatcher.h"
#include "platform/backend.h"
#include "win32/window.h"

#include <QCoreApplication>
#include <QSysInfo>

#include <KLocalizedString>

namespace maru::platform
{

namespace
{

QString unsupportedSessionReason()
{
    return i18n("Unsupported desktop session. Unset MARUPOP_PLATFORM or set it to windows.");
}

} // namespace

void Backend::build()
{
    if (m_session == Session::Windows) {
        m_tracker = new cursor::WinCursorTracker(this);
        m_frames = new capture::WinFrameSource(this);
    } else {
        // MARUPOP_PLATFORM names a session other than "windows".
        m_tracker = new cursor::UnavailableTracker(unsupportedSessionReason(), this);
        auto *frames = new capture::FakeFrameSource(this);
        frames->setFailure(i18n("Screen capture unavailable in the %1 session. Unset MARUPOP_PLATFORM or set it to "
                                "windows.",
                                sessionName(m_session)));
        m_frames = frames;
    }
    m_lockWatcher = new cursor::WinLockWatcher(this);
    m_shortcuts = new WinShortcuts(this);
    qCInfo(logPlatform) << "built the" << sessionId(m_session) << "backends";
}

CaptureReport captureReportFor(Session session, bool /*screencopyBound*/)
{
    CaptureReport report;
    report.lines.append(i18n("Session: %1", sessionName(session)));
    report.lines.append(i18n("Executable: %1", QCoreApplication::applicationFilePath()));
    if (session != Session::Windows) {
        report.lines.append(i18n("Screen capture: unavailable."));
        report.remedy = unsupportedSessionReason();
        return report;
    }
    // DXGI Desktop Duplication and a BitBlt from the screen DC are permitted to every process on
    // the interactive desktop.
    report.authorized = true;
    report.lines.append(i18n("Windows version: %1 (%2)", QSysInfo::prettyProductName(), QSysInfo::kernelVersion()));
    report.lines.append(win32::captureExclusionSupported()
                            ? i18n("Popup excluded from screen capture: yes.")
                            : i18n("Popup excluded from screen capture: no. Text recognition skips the popup "
                                   "area."));
    report.lines.append(i18n("Screen capture: available."));
    return report;
}

CaptureReport Backend::captureReport() const
{
    return captureReportFor(m_session, false);
}

} // namespace maru::platform
