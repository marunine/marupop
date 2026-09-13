// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/wlrframesource.h"

#include "capture/hyprlandconfig.h"
#include "capture/scanregion.h"
#include "core/logging.h"
#include "wayland-wlr-screencopy-unstable-v1-client-protocol.h"
#include "wayland/registry.h"

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <KLocalizedString>

#include <wayland-client-protocol.h>

namespace maru::capture
{

namespace
{

constexpr QByteArrayView kManagerInterface("zwlr_screencopy_manager_v1");
// Version 3 adds linux_dmabuf and buffer_done. Binding at 3 and ignoring linux_dmabuf leaves the
// shm path in use and lets buffer_done say when the format list is complete.
constexpr quint32 kManagerMaxVersion = 3;
// How many of the popup's pixels are sampled to decide whether the no_screen_share layer rule is
// in effect. A 3 by 3 grid inside the overlapping part, which is enough to tell a black
// rectangle from a rendered card and costs nine reads.
constexpr int kNoScreenShareProbes = 3;
// How long an issued frame may go unanswered. WlrFrameSource::setRequestTimeoutMs() has the
// reason this exists at all.
constexpr int kDefaultRequestTimeoutMs = 1000;

} // namespace

// The five zwlr_screencopy_frame_v1 events, forwarded to the source the listener's user data
// names.
struct ScreencopyFrameListener
{
    static void buffer(
        void *data, zwlr_screencopy_frame_v1 * /*frame*/, quint32 format, quint32 width, quint32 height, quint32 stride)
    {
        static_cast<WlrFrameSource *>(data)->onBuffer(format, width, height, stride);
    }

    static void flags(void *data, zwlr_screencopy_frame_v1 * /*frame*/, quint32 flags)
    {
        static_cast<WlrFrameSource *>(data)->onFlags(flags);
    }

    static void
    ready(void *data, zwlr_screencopy_frame_v1 * /*frame*/, quint32 /*secHi*/, quint32 /*secLo*/, quint32 /*nsec*/)
    {
        static_cast<WlrFrameSource *>(data)->onReady();
    }

    static void failed(void *data, zwlr_screencopy_frame_v1 * /*frame*/)
    {
        static_cast<WlrFrameSource *>(data)->onFailed();
    }

    static void damage(void * /*data*/,
                       zwlr_screencopy_frame_v1 * /*frame*/,
                       quint32 /*x*/,
                       quint32 /*y*/,
                       quint32 /*width*/,
                       quint32 /*height*/)
    {
        // copy_with_damage is never sent, so this event never arrives. The listener still has to
        // carry a function pointer for it: libwayland calls through the struct by index.
    }

    static void linuxDmabuf(void * /*data*/,
                            zwlr_screencopy_frame_v1 * /*frame*/,
                            quint32 /*format*/,
                            quint32 /*width*/,
                            quint32 /*height*/)
    {
        // The shm path is the one taken. A dmabuf import would need an EGL context and a
        // GPU-to-CPU readback for the recognition pass, which is the readback the shm copy
        // already performs.
    }

    static void bufferDone(void *data, zwlr_screencopy_frame_v1 * /*frame*/)
    {
        static_cast<WlrFrameSource *>(data)->onBufferDone();
    }
};

namespace
{

const zwlr_screencopy_frame_v1_listener kFrameListener = {
    .buffer = &ScreencopyFrameListener::buffer,
    .flags = &ScreencopyFrameListener::flags,
    .ready = &ScreencopyFrameListener::ready,
    .failed = &ScreencopyFrameListener::failed,
    .damage = &ScreencopyFrameListener::damage,
    .linux_dmabuf = &ScreencopyFrameListener::linuxDmabuf,
    .buffer_done = &ScreencopyFrameListener::bufferDone,
};

// The output whose logical geometry contains the centre of logical, and nullptr for a centre on
// no output.
//
// No fallback to the primary screen. A centre on no output is reachable wherever the layout
// leaves a gap inside its bounding box -- two monitors offset diagonally, where
// capture::workspaceRect() is the union and capture::grow() bounds against it -- and answering
// the primary screen there would quantize the region onto the wrong output and grab whatever
// part of it happened to intersect. Both callers handle the empty answer: quantize() leaves the
// region unsnapped, and issue() reports that no output holds it.
QScreen *screenFor(const QRect &logical)
{
    return QGuiApplication::screenAt(logical.center());
}

} // namespace

bool WlrFrameSource::available()
{
    wl::Registry *registry = wl::Registry::instance();
    return registry != nullptr && registry->has(kManagerInterface.toByteArray());
}

WlrFrameSource::WlrFrameSource(QObject *parent)
    : FrameSource(parent)
{
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(kDefaultRequestTimeoutMs);
    connect(m_watchdog, &QTimer::timeout, this, &WlrFrameSource::onRequestTimeout);

    wl::Registry *registry = wl::Registry::instance();
    if (registry == nullptr) {
        m_reason = i18n("The application is not connected to a Wayland compositor.");
        return;
    }
    m_manager = static_cast<zwlr_screencopy_manager_v1 *>(
        registry->bind(&zwlr_screencopy_manager_v1_interface, kManagerMaxVersion));
    if (m_manager == nullptr) {
        m_reason = i18n("Screen capture unavailable: the compositor does not support zwlr_screencopy_manager_v1.");
        return;
    }
    m_managerVersion = qMin(registry->version(kManagerInterface.toByteArray()), kManagerMaxVersion);
    qCDebug(logCapture) << "bound zwlr_screencopy_manager_v1 at version" << m_managerVersion;
}

WlrFrameSource::~WlrFrameSource()
{
    if (m_pending.frame != nullptr) {
        zwlr_screencopy_frame_v1_destroy(m_pending.frame);
    }
    if (m_manager != nullptr) {
        zwlr_screencopy_manager_v1_destroy(m_manager);
    }
}

bool WlrFrameSource::isAvailable() const
{
    return m_manager != nullptr;
}

QString WlrFrameSource::unavailableReason() const
{
    return m_manager != nullptr ? QString{} : m_reason;
}

bool WlrFrameSource::capturesOwnWindows() const
{
    return true;
}

QRect WlrFrameSource::quantize(const QRect &logical) const
{
    const QScreen *screen = screenFor(logical);
    if (screen == nullptr) {
        return logical;
    }
    return wlrTileFor(logical, screen->geometry());
}

void WlrFrameSource::grab(const QRect &logical)
{
    if (const std::optional<QRect> rect = beginRequest(logical)) {
        issue(*rect);
    }
}

// issue() and issueNext() call each other: a grab that fails before it reaches the compositor
// issues the remembered rect from the same call. The chain is one deep, because endRequest()
// answers a remembered rect once and clears it.
// NOLINTNEXTLINE(misc-no-recursion)
void WlrFrameSource::issue(const QRect &logical)
{
    if (m_manager == nullptr) {
        Q_EMIT failed(unavailableReason());
        issueNext();
        return;
    }
    QScreen *screen = screenFor(logical);
    wl_output *output = wl::Registry::outputFor(screen);
    if (screen == nullptr || output == nullptr) {
        Q_EMIT failed(i18n("No screen found at %1, %2.", logical.x(), logical.y()));
        issueNext();
        return;
    }

    // The protocol takes the region in the output's own logical coordinates and clips it to the
    // output, so a rect that reaches past an edge is answered with a smaller buffer. quantize()
    // has already moved the tile inside the output for a caller that applied it; a caller that
    // did not still gets a correct, smaller frame.
    const QRect clipped = logical.intersected(screen->geometry());
    if (clipped.isEmpty()) {
        Q_EMIT failed(i18n("Capture area at %1, %2 is outside all screens.", logical.x(), logical.y()));
        issueNext();
        return;
    }
    const QRect local = clipped.translated(-screen->geometry().topLeft());

    m_pending = Pending{};
    m_pending.logical = clipped;
    // The fallback ratio. finishPending() derives the real one from the buffer size the
    // compositor asks for, because Hyprland scales the region by its own fractional
    // CMonitor::m_scale while a QScreen on Wayland reports the integer wl_output.scale.
    m_pending.scale = screen->devicePixelRatio();
    // The popup is composited into the copy, so where it is right now is what the recognition
    // pass has to be told to ignore.
    m_pending.occluded = currentOcclusion().intersected(clipped);
    m_pending.frame = zwlr_screencopy_manager_v1_capture_output_region(m_manager,
                                                                       0, // overlay_cursor
                                                                       output,
                                                                       local.x(),
                                                                       local.y(),
                                                                       local.width(),
                                                                       local.height());
    zwlr_screencopy_frame_v1_add_listener(m_pending.frame, &kFrameListener, this);
    m_watchdog->start();
}

// NOLINTNEXTLINE(misc-no-recursion)
void WlrFrameSource::issueNext()
{
    if (const std::optional<QRect> next = endRequest()) {
        issue(*next);
    }
}

void WlrFrameSource::onBuffer(quint32 format, quint32 width, quint32 height, quint32 stride)
{
    if (m_pending.frame == nullptr) {
        return;
    }
    // Several buffer events arrive on version 3, one per supported shm format. The first one
    // this build can turn into a QImage is taken; a later one is ignored.
    if (m_pending.haveBuffer) {
        return;
    }
    if (wl::ShmBuffer::imageFormatFor(format) == QImage::Format_Invalid) {
        return;
    }
    m_pending.format = format;
    m_pending.bufferSize = QSize(static_cast<int>(width), static_cast<int>(height));
    m_pending.stride = static_cast<int>(stride);
    m_pending.haveBuffer = true;

    if (m_managerVersion < 3) {
        // buffer_done exists from version 3. On a lower version the single buffer event is the
        // whole offer, so the copy is sent from here.
        sendCopy();
    }
}

void WlrFrameSource::onBufferDone()
{
    sendCopy();
}

void WlrFrameSource::sendCopy()
{
    if (m_pending.frame == nullptr || m_pending.copied) {
        return;
    }
    if (!m_pending.haveBuffer) {
        abortPending(i18n("The compositor provides no supported image format."));
        return;
    }
    wl::Registry *registry = wl::Registry::instance();
    wl_shm *shm = registry != nullptr ? registry->shm() : nullptr;
    if (shm == nullptr || !m_buffer.reset(shm, m_pending.bufferSize, m_pending.format, m_pending.stride)) {
        abortPending(i18n("Could not allocate a shared-memory buffer of %1 × %2 pixels.",
                          m_pending.bufferSize.width(),
                          m_pending.bufferSize.height()));
        return;
    }
    m_pending.copied = true;
    zwlr_screencopy_frame_v1_copy(m_pending.frame, m_buffer.buffer());
}

void WlrFrameSource::onFlags(quint32 flags)
{
    m_pending.yInvert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

void WlrFrameSource::onReady()
{
    finishPending();
}

void WlrFrameSource::onFailed()
{
    // The compositor reports failed() for a denied permission and for an output that went away
    // mid-copy, and the two are indistinguishable on the wire.
    const HyprlandConfig config = hyprlandConfig();
    abortPending(i18n("Screen capture was denied. If %1 is enabled, add this line to %2: %3",
                      enforcePermissionsOption(config.language),
                      config.fileName(),
                      screencopyPermissionRule(config.language, QCoreApplication::applicationFilePath())));
}

void WlrFrameSource::finishPending()
{
    Frame frame;
    frame.logicalRect = m_pending.logical;
    frame.occluded = m_pending.occluded;
    // The ratio the compositor actually applied, read off the buffer it asked for rather than
    // taken from QScreen::devicePixelRatio(). Hyprland scales the requested region by
    // CMonitor::m_scale, which is fractional, while a QScreen on Wayland reports the integer
    // wl_output.scale unless the fractional-scale protocol is in play. Deriving it from the
    // buffer is what keeps capture::imageToLogical() correct on an output at scale 1.25 or 1.5.
    // The QScreen value is the fallback for a zero-width region, which issue() already refuses.
    frame.scale = m_pending.logical.width() > 0 && m_pending.bufferSize.width() > 0
                      ? static_cast<qreal>(m_pending.bufferSize.width()) / m_pending.logical.width()
                      : m_pending.scale;
    // A copy rather than the mapping: the buffer is reused by the next grab, and the recognition
    // pass reads the image on another thread.
    frame.image = m_buffer.image().copy();
    if (m_pending.yInvert) {
        // QImage::flipped() arrived in Qt 6.9 and QImage::mirrored() is deprecated from it. The
        // project's minimum is Qt 6.5, so both spellings are compiled.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        frame.image = frame.image.flipped(Qt::Vertical);
#else
        frame.image = frame.image.mirrored(false, true);
#endif
    }
    frame.image.setDevicePixelRatio(frame.scale);
    frame.hash = hashImage(frame.image);
    frame.grabMs = requestElapsedMs();

    m_watchdog->stop();
    zwlr_screencopy_frame_v1_destroy(m_pending.frame);
    m_pending = Pending{};

    checkNoScreenShare(frame, currentOcclusion().intersected(frame.logicalRect));
    Q_EMIT frameReady(frame);
    issueNext();
}

void WlrFrameSource::abortPending(const QString &message)
{
    m_watchdog->stop();
    if (m_pending.frame != nullptr) {
        zwlr_screencopy_frame_v1_destroy(m_pending.frame);
    }
    m_pending = Pending{};
    qCWarning(logCapture) << "a region copy failed:" << message;
    Q_EMIT failed(message);
    issueNext();
}

void WlrFrameSource::setRequestTimeoutMs(int milliseconds)
{
    m_watchdog->setInterval(qMax(1, milliseconds));
}

int WlrFrameSource::requestTimeoutMs() const
{
    return m_watchdog->interval();
}

void WlrFrameSource::onRequestTimeout()
{
    if (m_pending.frame == nullptr) {
        return;
    }
    // The frame is abandoned rather than waited on. Where the cause is a pending permission the
    // next grab asks again and succeeds the moment the user answers the compositor's prompt, so
    // the loop recovers on its own; ScanController reports one warning per distinct message
    // rather than one per grab.
    const HyprlandConfig config = hyprlandConfig();
    abortPending(i18n("Screen capture timed out after %1 ms. If %2 is enabled, accept the screen capture prompt or add "
                      "this line to %3: %4",
                      m_watchdog->interval(),
                      enforcePermissionsOption(config.language),
                      config.fileName(),
                      screencopyPermissionRule(config.language, QCoreApplication::applicationFilePath())));
}

void WlrFrameSource::checkNoScreenShare(const Frame &frame, const QRect &occlusionNow)
{
    if (m_noScreenShareReported || frame.occluded.isEmpty() || frame.image.isNull()) {
        return;
    }
    // The rectangle was sampled when the frame was issued and the pixels arrived one or more
    // output commits later. The card follows the pointer, so it can have moved or hidden in
    // between, and the probe below would then read ordinary desktop pixels and report a rule that
    // is present as missing -- once and permanently, because the report is one-shot. Checking only
    // where the card held still over the whole grab is what keeps that from happening.
    if (occlusionNow != frame.occluded) {
        return;
    }
    const QRect probe = logicalToImage(frame, frame.occluded).intersected(frame.image.rect());
    if (probe.width() < kNoScreenShareProbes || probe.height() < kNoScreenShareProbes) {
        return;
    }
    // A grid inside the overlap. The rule paints the whole surface black, so one non-black
    // sample is enough to say the rule is absent.
    for (int row = 1; row <= kNoScreenShareProbes; ++row) {
        for (int column = 1; column <= kNoScreenShareProbes; ++column) {
            const int x = probe.left() + probe.width() * column / (kNoScreenShareProbes + 1);
            const int y = probe.top() + probe.height() * row / (kNoScreenShareProbes + 1);
            const QColor colour = frame.image.pixelColor(x, y);
            if (colour.red() > kNoScreenShareBlack || colour.green() > kNoScreenShareBlack ||
                colour.blue() > kNoScreenShareBlack) {
                m_noScreenShareReported = true;
                const QString line = noScreenShareRule(hyprlandConfig().language);
                qCWarning(logCapture) << "the popup is composited into the captured region;" << line << "is missing";
                Q_EMIT noScreenShareMissing(line);
                return;
            }
        }
    }
}

} // namespace maru::capture
