// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The four session-dependent services, built for the session platform::detect() answers.
//
// Application holds one Backend rather than four named implementations, so adding a session
// family extends the switch statements in backend_wayland.cpp or adds a backend_<family>.cpp,
// such as backend_win.cpp, and leaves src/app/ unchanged.
// The services themselves are reached through their interfaces: cursor::CursorTracker,
// capture::FrameSource, cursor::LockWatcher and maru::ShortcutRegistry.
#pragma once

#include "platform/session.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace maru::capture
{
class FrameSource;
} // namespace maru::capture

namespace maru::cursor
{
class CursorTracker;
class LockWatcher;
} // namespace maru::cursor

namespace maru
{
class ShortcutRegistry;
}

namespace maru::platform
{

// What the session requires before a region can be captured.
struct CaptureReport
{
    // True where a capture will succeed as the process is installed and configured now.
    bool authorized = false;
    // The diagnostic, one line per entry, ending with a verdict. Printed by
    // `marupop --check-authorization`.
    QStringList lines;
    // A translated sentence naming what to change, empty while authorized is true.
    QString remedy;
};

// The capture report for session, without building a Backend.
//
// screencopyBound says whether zwlr_screencopy_manager_v1 was bound, which only a process with a
// display connection can answer. `marupop --check-authorization` builds a QGuiApplication where
// WAYLAND_DISPLAY names a reachable compositor and passes WlrFrameSource::available(); with no
// display it builds a QCoreApplication instead and passes false, since nothing can be bound. The
// lines name the requirements either way, so the answer stays useful from a TTY and over SSH.
[[nodiscard]] CaptureReport captureReportFor(Session session, bool screencopyBound);

class Backend : public QObject
{
    Q_OBJECT

public:
    // The backend for detect().
    explicit Backend(QObject *parent = nullptr);
    // The backend for a named session, which is what a test builds.
    explicit Backend(Session session, QObject *parent = nullptr);
    ~Backend() override;

    [[nodiscard]] Session session() const;

    // The four services, owned by the backend and valid for its life. Never null: a session that
    // offers no implementation of one gets an implementation that reports itself unavailable.
    [[nodiscard]] cursor::CursorTracker *tracker() const;
    [[nodiscard]] capture::FrameSource *frames() const;
    [[nodiscard]] cursor::LockWatcher *lockWatcher() const;
    [[nodiscard]] ShortcutRegistry *shortcuts() const;

    // Starts and stops the pointer source. stopTracking() also calls
    // capture::FrameSource::release().
    void startTracking();
    void stopTracking();

    // Whether this session's pixel source composites MaruPop's own windows into a grab, which
    // decides whether the popup avoids the text it answers for.
    [[nodiscard]] bool capturesOwnWindows() const;

    // What the session requires before a region can be captured, reported at startup. The
    // built pixel source answers the parts captureReportFor() takes as arguments.
    [[nodiscard]] CaptureReport captureReport() const;

Q_SIGNALS:
    // The compositor needs a configuration line the session does not have. configLine is the
    // literal text to add and header names the file. Raised at most once per line per process.
    void configurationMissing(const QString &header, const QString &configLine);

private:
    void build();

    Session m_session = Session::Unknown;
    cursor::CursorTracker *m_tracker = nullptr;
    capture::FrameSource *m_frames = nullptr;
    cursor::LockWatcher *m_lockWatcher = nullptr;
    ShortcutRegistry *m_shortcuts = nullptr;
};

} // namespace maru::platform
