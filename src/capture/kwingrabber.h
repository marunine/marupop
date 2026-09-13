// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// org.kde.KWin.ScreenShot2 client, copied from marusnap/src/capture/kwingrabber.{h,cpp} and
// moved into the maru::capture namespace this module uses throughout.
//
// MaruPop calls captureArea() alone. The other five methods are kept because they are the same
// four lines each and because a caller that has to fall back to a whole screen or a single
// window has them without a second implementation of the pipe protocol.
#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QVariantMap>

namespace maru::capture
{

// One in-flight org.kde.KWin.ScreenShot2 request. Emits exactly one of finished()/failed(),
// then deletes itself. Pixel data streams through a pipe on a worker thread while the D-Bus
// reply carries the geometry vardict; both must arrive before assembly.
class KWinGrab : public QObject
{
    Q_OBJECT

public:
    enum class Error
    {
        Cancelled,        // the user cancelled an interactive grab — not a failure
        PermissionDenied, // KWin rejected the caller (desktop-entry gate)
        DBus,             // any other D-Bus error
        Read,             // pipe read or image assembly failed
    };
    Q_ENUM(Error)

    // Vardict from the reply ("width", "height", "stride", "format", "scale", ...).
    [[nodiscard]] QVariantMap results() const;

Q_SIGNALS:
    void finished(const QImage &image);
    void failed(maru::capture::KWinGrab::Error error, const QString &message);

private:
    friend class KWinGrabber;
    explicit KWinGrab(QObject *parent);

    void start(const QString &method, const QVariantList &arguments, const QVariantMap &options);
    void maybeFinish();
    void failOnce(Error error, const QString &message);

    QVariantMap m_results;
    QByteArray m_pixelData;
    bool m_replyReceived = false;
    bool m_pixelsReceived = false;
    bool m_done = false;
};

// org.kde.KWin.ScreenShot2 client. Requires the running binary to match the absolute Exec
// of an installed desktop entry declaring X-KDE-DBUS-Restricted-Interfaces (see
// ARCHITECTURE.md "Feasibility notes"); otherwise every call fails with PermissionDenied.
// tools/install-dev-desktop.sh writes that entry for a build tree.
class KWinGrabber : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        bool includeCursor = false;
        bool includeDecoration = true;
        bool includeShadow = true;
        // Reaches ScreenShot2 as the negation, hide-caller-windows. CaptureArea, CaptureScreen,
        // CaptureActiveScreen and CaptureWorkspace read it; CaptureWindow and CaptureActiveWindow
        // render the requested window whatever it holds.
        bool includeOwnWindows = false;
        // Reaches ScreenShot2 as native-resolution. True returns the region at the largest
        // QScreen::devicePixelRatio() of any output rather than at 1 logical pixel per image
        // pixel, which is the detail an OCR pass needs on a HiDPI output.
        bool nativeResolution = true;
    };

    explicit KWinGrabber(QObject *parent = nullptr);

    [[nodiscard]] KWinGrab *captureWorkspace(const Options &options);
    [[nodiscard]] KWinGrab *captureArea(const QRect &area, const Options &options); // logical coords
    [[nodiscard]] KWinGrab *captureScreen(const QString &name, const Options &options);
    [[nodiscard]] KWinGrab *captureActiveScreen(const Options &options);
    [[nodiscard]] KWinGrab *captureActiveWindow(const Options &options);
    [[nodiscard]] KWinGrab *captureWindow(const QString &uuid, const Options &options);

    // True when org.kde.KWin owns its bus name (a KWin Wayland session is running).
    [[nodiscard]] static bool serviceAvailable();

private:
    [[nodiscard]] KWinGrab *startGrab(const QString &method, const QVariantList &arguments, const Options &options);
};

} // namespace maru::capture
