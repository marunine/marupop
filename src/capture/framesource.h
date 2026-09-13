// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The pixel source read by the scan controller. Each request produces one frame rather
// than a persistent stream. Plasma captures the requested region through ScreenShot2.
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QRect>

#include <functional>
#include <optional>

namespace maru::capture
{

class KWinGrab;
class KWinGrabber;

// One captured region. image is in device pixels and carries scale as its devicePixelRatio();
// logicalRect is the region that was requested, in logical global desktop coordinates.
struct Frame
{
    QImage image;
    QRect logicalRect;
    // Image pixels per logical pixel. 1.0 on an unscaled output; the devicePixelRatio() of the
    // output under the region after KWinFrameSource has corrected an over-scaled reply.
    qreal scale = 1.0;
    // XXH3-64 over the image rows. Equal hashes mean identical pixels, which is what lets the
    // scan controller reuse a cached OCR result for an unchanged region.
    quint64 hash = 0;
    // Wall-clock milliseconds from the grab() call to frameReady().
    qint64 grabMs = 0;
    // The part of logicalRect holding MaruPop's own popup rather than desktop content, in
    // logical global desktop coordinates. Empty on a source that excludes MaruPop's own windows
    // from the render, which org.kde.KWin.ScreenShot2 does through hide-caller-windows.
    //
    // zwlr_screencopy_v1 copies the output's composited frame and offers no such option, so a
    // wlroots-family compositor paints the popup into every grab it overlaps. The
    // `no_screen_share` layer rule turns that into a black rectangle at a known position, and this rectangle is where
    // that black is. The hit test rejects a character inside it, so the black never resolves as text.
    //
    // The hash covers the rectangle rather than skipping it. Two frames whose popup sits at two
    // positions hold desktop content at different places, so their recognition results differ
    // and one cache entry cannot answer for both.
    QRect occluded;
};

// Region capture, one request at a time. A grab() issued while another is in flight is
// remembered rather than queued: the newest rect replaces the remembered one, and is issued
// when the in-flight grab finishes. A pointer that moves during a 6 ms grab therefore costs one
// extra grab rather than a backlog of them.
class FrameSource : public QObject
{
    Q_OBJECT

public:
    explicit FrameSource(QObject *parent = nullptr);
    ~FrameSource() override;

    // logical is in logical global desktop coordinates, the space QScreen::geometry() uses.
    virtual void grab(const QRect &logical) = 0;

    // The rect this source will actually grab for a requested logical, which the caller uses as
    // the identity of the grab it is waiting for. The identity by default; WlrFrameSource snaps
    // to the tile grid its compositor's session cache requires.
    [[nodiscard]] virtual QRect quantize(const QRect &logical) const;

    // True where a grab composites MaruPop's own windows into the returned pixels, which is what
    // makes Frame::occluded non-empty. False on org.kde.KWin.ScreenShot2, which renders the
    // scene without them.
    [[nodiscard]] virtual bool capturesOwnWindows() const;

    // The rectangle MaruPop's own popup covers right now, in logical global desktop coordinates,
    // or an empty rect while no popup is mapped. Assigned by the application from
    // popup::PopupWindow::occlusionRect(); a source that captures its own windows calls it once
    // per grab and reports the answer as Frame::occluded. capture/ therefore needs no dependency
    // on popup/.
    void setOcclusionProvider(std::function<QRect()> provider);

    // XXH3-64 over the image rows, each row hashed over width * depth / 8 bytes so the padding
    // QImage leaves between rows never reaches the hash.
    [[nodiscard]] static quint64 hashImage(const QImage &image);

Q_SIGNALS:
    void frameReady(const maru::capture::Frame &frame);
    void failed(const QString &message);

protected:
    // The rect to issue now, or nullopt when a grab is already in flight, in which case the
    // request replaces whatever rect was remembered.
    [[nodiscard]] std::optional<QRect> beginRequest(const QRect &logical);
    // Ends the in-flight grab and returns the remembered rect, which the caller issues. Call it
    // after emitting frameReady(), so a grab() made from a frameReady() handler is the rect
    // this returns.
    [[nodiscard]] std::optional<QRect> endRequest();
    [[nodiscard]] bool requestInFlight() const;
    // Milliseconds since the in-flight grab was issued.
    [[nodiscard]] qint64 requestElapsedMs() const;
    // The occlusion provider's answer, or an empty rect where none was assigned.
    [[nodiscard]] QRect currentOcclusion() const;

private:
    bool m_inFlight = false;
    std::optional<QRect> m_pending;
    QElapsedTimer m_elapsed;
    std::function<QRect()> m_occlusion;
};

// FrameSource over org.kde.KWin.ScreenShot2 CaptureArea, with include-cursor false (so a
// pointer moving over static text leaves the frame hash unchanged), hide-caller-windows true
// (so MaruPop's own popup never reaches the OCR pass) and native-resolution true (so a HiDPI
// output is read at its own pixel density).
class KWinFrameSource : public FrameSource
{
    Q_OBJECT

public:
    explicit KWinFrameSource(QObject *parent = nullptr);
    ~KWinFrameSource() override;

    void grab(const QRect &logical) override;

    // True when org.kde.KWin owns its bus name.
    [[nodiscard]] static bool available();

private:
    void issue(const QRect &logical);
    void deliver(const QImage &source, const QRect &logical);
    void issueNext();

    KWinGrabber *m_grabber;
    QPointer<KWinGrab> m_grab;
};

// FrameSource that answers with a supplied image, for tests and for a caller that has to run
// without a compositor. The reply is delivered through the event loop, which is where a
// KWinFrameSource reply arrives from, so the latest-wins behaviour a test observes is the one
// the KWin path has.
class FakeFrameSource : public FrameSource
{
    Q_OBJECT

public:
    explicit FakeFrameSource(QObject *parent = nullptr);
    ~FakeFrameSource() override;

    // The image every subsequent grab answers with. scale is written into Frame::scale and
    // into the image's devicePixelRatio().
    void setImage(const QImage &image, qreal scale = 1.0);
    // Non-empty makes every subsequent grab answer with failed(message) instead.
    void setFailure(const QString &message);

    // Non-empty makes every subsequent frame carry it as Frame::occluded and makes
    // capturesOwnWindows() true, which is the shape a wlroots-family source has.
    void setOcclusion(QRect logical);

    // Non-empty makes quantize() snap to the tile grid of that output, which is what
    // WlrFrameSource does. Empty leaves quantize() the identity.
    void setQuantizeOutput(QRect output);

    void grab(const QRect &logical) override;
    [[nodiscard]] QRect quantize(const QRect &logical) const override;
    [[nodiscard]] bool capturesOwnWindows() const override;

    // The rects grab() was called with, in call order, including the ones that were remembered
    // rather than issued.
    [[nodiscard]] QList<QRect> requestedRects() const;
    // The rects that reached a reply, in reply order.
    [[nodiscard]] QList<QRect> issuedRects() const;

private:
    void issue(const QRect &logical);

    QImage m_image;
    qreal m_scale = 1.0;
    QString m_failure;
    QRect m_occluded;
    QRect m_quantizeOutput;
    QList<QRect> m_requested;
    QList<QRect> m_issued;
};

} // namespace maru::capture

Q_DECLARE_METATYPE(maru::capture::Frame)
