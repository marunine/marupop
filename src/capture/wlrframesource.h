// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// FrameSource over zwlr_screencopy_unstable_v1.
//
// capture_output_region takes a region in the output's own logical coordinates and answers a
// buffer of region size times the output scale, which is the native-resolution behaviour
// org.kde.KWin.ScreenShot2 has behind its native-resolution option. overlay_cursor is 0, so
// a pointer moving over static text leaves the frame hash unchanged.
//
// Three properties differ from the KWin path, and each is answered here rather than by the scan
// controller:
//
//   - A region cannot span two outputs, because the output is an argument. grab() clips to the
//     output under the requested centre.
//   - Hyprland retains one screenshare session per distinct capture box, so the requested rect is
//     snapped to a tile grid by quantize(), which the caller applies before it records what it is
//     waiting for.
//   - The compositor composites MaruPop's own popup into the copied frame. capturesOwnWindows()
//     is true and every delivered Frame carries the popup rectangle as Frame::occluded.
#pragma once

#include "capture/framesource.h"
#include "wayland/shmbuffer.h"

#include <QRect>
#include <QSize>
#include <QString>

class QTimer;
struct wl_buffer;
struct zwlr_screencopy_frame_v1;
struct zwlr_screencopy_manager_v1;

namespace maru::capture
{

// A colour component at or above which a pixel is no longer the black the no_screen_share layer
// rule paints. The rule paints Colors::BLACK with no blending, so the exact value is 0; the
// tolerance absorbs a colour management transform on the way through the copy.
//
// Exported because the live test that proves the rule applies has to judge the same pixels the
// same way. A test demanding exact black would fail on a host whose copy carries that transform,
// and would blame the layer rule for it.
constexpr int kNoScreenShareBlack = 8;

class WlrFrameSource : public FrameSource
{
    Q_OBJECT

public:
    explicit WlrFrameSource(QObject *parent = nullptr);
    ~WlrFrameSource() override;

    void grab(const QRect &logical) override;
    [[nodiscard]] QRect quantize(const QRect &logical) const override;
    [[nodiscard]] bool capturesOwnWindows() const override;

    // True where zwlr_screencopy_manager_v1 was bound.
    [[nodiscard]] bool isAvailable() const;
    // Empty while isAvailable() is true.
    [[nodiscard]] QString unavailableReason() const;

    // True where the compositor advertises zwlr_screencopy_manager_v1, without binding it.
    [[nodiscard]] static bool available();

    // Deadline for a frame request, in milliseconds. A compositor can leave a pending capture
    // permission unanswered without sending ready or failed. The watchdog abandons that
    // request and reports failed() so the scan loop cannot remain stuck indefinitely.
    void setRequestTimeoutMs(int milliseconds);
    [[nodiscard]] int requestTimeoutMs() const;

Q_SIGNALS:
    // The popup overlapped a grabbed region and the pixels under it were not the black rectangle
    // the `no_screen_share` layer rule paints, so the card's own text would reach the recognition
    // pass. Emitted once per process; configLine is the line to add, already written for whichever
    // configuration language capture::hyprlandConfig() finds.
    void noScreenShareMissing(const QString &configLine);

private:
    // The per-grab protocol state. One at a time, which FrameSource::beginRequest() already
    // guarantees.
    struct Pending
    {
        zwlr_screencopy_frame_v1 *frame = nullptr;
        QRect logical;      // what the caller asked for, in logical global desktop coordinates
        QRect occluded;     // the popup rectangle at issue time, same space
        qreal scale = 1.0;  // image pixels per logical pixel
        QSize bufferSize;   // device pixels, from the buffer event
        quint32 format = 0; // wl_shm format, from the buffer event
        int stride = 0;
        bool haveBuffer = false;
        bool copied = false;
        bool yInvert = false;
    };

    void issue(const QRect &logical);
    void issueNext();
    void finishPending();
    void abortPending(const QString &message);

    // The events of the in-flight zwlr_screencopy_frame_v1.
    void onBuffer(quint32 format, quint32 width, quint32 height, quint32 stride);
    void onBufferDone();
    void onFlags(quint32 flags);
    void onReady();
    void onFailed();

    // Attaches the reusable buffer and sends copy. Called from buffer_done on version 3 and from
    // the first buffer event on a lower version.
    void sendCopy();

    // Checks the pixels under the popup rectangle for the black the `no_screen_share` layer rule
    // paints, once per process. A mismatch raises noScreenShareMissing(). occlusionNow is the
    // popup rectangle at the moment the pixels arrived; the check runs only where it equals the
    // one recorded when the frame was issued, so a card that moved during the grab raises nothing.
    void checkNoScreenShare(const Frame &frame, const QRect &occlusionNow);

    void onRequestTimeout();

    zwlr_screencopy_manager_v1 *m_manager = nullptr;
    quint32 m_managerVersion = 0;
    QString m_reason;
    wl::ShmBuffer m_buffer;
    Pending m_pending;
    // Armed by issue() and stopped by finishPending() and abortPending(), so exactly one issued
    // frame is ever being timed.
    QTimer *m_watchdog = nullptr;
    bool m_noScreenShareReported = false;

    friend struct ScreencopyFrameListener;
};

} // namespace maru::capture
