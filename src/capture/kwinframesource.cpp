// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/kwinframesource.h"

#include "capture/kwingrabber.h"
#include "core/logging.h"

#include <QGuiApplication>
#include <QScreen>

#include <cmath>

namespace maru::capture
{

namespace
{

// A returned scale and an output scale agree when they differ by less than this. Both come from
// the same source (KWin's Output::scale()) over two paths, so the comparison only has to absorb
// the double round trip through the D-Bus vardict.
constexpr qreal kScaleEpsilon = 1e-6;

} // namespace

KWinFrameSource::KWinFrameSource(QObject *parent)
    : FrameSource(parent)
    , m_grabber(new KWinGrabber(this))
{}

KWinFrameSource::~KWinFrameSource() = default;

bool KWinFrameSource::available()
{
    return KWinGrabber::serviceAvailable();
}

void KWinFrameSource::grab(const QRect &logical)
{
    if (const std::optional<QRect> rect = beginRequest(logical)) {
        issue(*rect);
    }
}

void KWinFrameSource::issue(const QRect &logical)
{
    KWinGrabber::Options options;
    // The cursor is excluded from the render, so moving the pointer over static text leaves the
    // frame hash unchanged and the cached OCR result usable.
    options.includeCursor = false;
    // MaruPop's own popup sits over the text it describes. Without this it would be captured
    // and recognized on the next scan.
    options.includeOwnWindows = false;
    options.nativeResolution = true;

    m_grab = m_grabber->captureArea(logical, options);
    connect(m_grab, &KWinGrab::finished, this, [this, logical](const QImage &image) {
        deliver(image, logical);
    });
    connect(m_grab, &KWinGrab::failed, this, [this](KWinGrab::Error error, const QString &message) {
        Q_UNUSED(error)
        Q_EMIT failed(message);
        issueNext();
    });
}

void KWinFrameSource::deliver(const QImage &source, const QRect &logical)
{
    Frame frame;
    frame.logicalRect = logical;
    frame.image = source;
    frame.scale = source.devicePixelRatio();

    // native-resolution computes its scale as the maximum QScreen::devicePixelRatio() over
    // every output rather than the one under the region,
    // so a region on a 1x output in a session that also has a 2x output comes back at 2x: four
    // times the bytes and four times the OCR pixels for no extra detail. Scaling back to the
    // pixel density of the output under the region's centre removes both costs.
    const QScreen *screen = QGuiApplication::screenAt(logical.center());
    const qreal targetScale = screen != nullptr ? screen->devicePixelRatio() : frame.scale;
    if (frame.scale > targetScale + kScaleEpsilon && frame.scale > 0) {
        const qreal ratio = targetScale / frame.scale;
        const QSize target{std::max(1, qRound(frame.image.width() * ratio)),
                           std::max(1, qRound(frame.image.height() * ratio))};
        frame.image = frame.image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        frame.scale = targetScale;
        frame.image.setDevicePixelRatio(targetScale);
        qCDebug(logCapture) << "downscaled" << source.size() << "to" << target << "for a" << targetScale << "output at"
                            << logical;
    }

    frame.hash = hashImage(frame.image);
    frame.grabMs = requestElapsedMs();
    Q_EMIT frameReady(frame);
    issueNext();
}

void KWinFrameSource::issueNext()
{
    if (const std::optional<QRect> next = endRequest()) {
        issue(*next);
    }
}

} // namespace maru::capture
