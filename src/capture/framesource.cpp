// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/framesource.h"

#include "capture/scanregion.h"
#include "core/logging.h"

#include <QPainter>
#include <QTimer>

#include <xxhash.h>

namespace maru::capture
{

FrameSource::FrameSource(QObject *parent)
    : QObject(parent)
{}

FrameSource::~FrameSource() = default;

QRect FrameSource::quantize(const QRect &logical) const
{
    return logical;
}

bool FrameSource::capturesOwnWindows() const
{
    return false;
}

void FrameSource::release() {}

void FrameSource::setOcclusionProvider(std::function<QRect()> provider)
{
    m_occlusion = std::move(provider);
}

QRect FrameSource::currentOcclusion() const
{
    return m_occlusion ? m_occlusion() : QRect{};
}

quint64 FrameSource::hashImage(const QImage &image)
{
    if (image.isNull()) {
        return 0;
    }
    XXH3_state_t *state = XXH3_createState();
    if (state == nullptr) {
        return 0;
    }
    XXH3_64bits_reset(state);
    // Row by row over width * depth / 8 bytes rather than over bytesPerLine() * height: QImage
    // pads each row to a 4-byte boundary and leaves the padding uninitialized, so hashing it
    // would make two images with identical pixels hash differently.
    const qsizetype rowBytes = static_cast<qsizetype>(image.width()) * image.depth() / 8;
    for (int y = 0; y < image.height(); ++y) {
        XXH3_64bits_update(state, image.constScanLine(y), static_cast<size_t>(rowBytes));
    }
    const XXH64_hash_t hash = XXH3_64bits_digest(state);
    XXH3_freeState(state);
    return static_cast<quint64>(hash);
}

std::optional<QRect> FrameSource::beginRequest(const QRect &logical)
{
    if (m_inFlight) {
        // Latest wins: the remembered rect is replaced rather than appended, so a pointer that
        // moves three times during one grab costs one extra grab, not three.
        m_pending = logical;
        return std::nullopt;
    }
    m_inFlight = true;
    m_pending.reset();
    m_elapsed.start();
    return logical;
}

std::optional<QRect> FrameSource::endRequest()
{
    m_inFlight = false;
    if (!m_pending.has_value()) {
        return std::nullopt;
    }
    const QRect next = *m_pending;
    m_pending.reset();
    m_inFlight = true;
    m_elapsed.start();
    return next;
}

bool FrameSource::requestInFlight() const
{
    return m_inFlight;
}

qint64 FrameSource::requestElapsedMs() const
{
    return m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
}

FakeFrameSource::FakeFrameSource(QObject *parent)
    : FrameSource(parent)
{}

FakeFrameSource::~FakeFrameSource() = default;

void FakeFrameSource::setImage(const QImage &image, qreal scale)
{
    m_image = image;
    m_scale = scale;
    m_failure.clear();
}

void FakeFrameSource::setFailure(const QString &message)
{
    m_failure = message;
}

void FakeFrameSource::setOcclusion(QRect logical)
{
    m_occluded = logical;
}

void FakeFrameSource::setQuantizeOutput(QRect output)
{
    m_quantizeOutput = output;
}

QRect FakeFrameSource::quantize(const QRect &logical) const
{
    return m_quantizeOutput.isEmpty() ? logical : wlrTileFor(logical, m_quantizeOutput);
}

bool FakeFrameSource::capturesOwnWindows() const
{
    return !m_occluded.isEmpty();
}

QList<QRect> FakeFrameSource::requestedRects() const
{
    return m_requested;
}

QList<QRect> FakeFrameSource::issuedRects() const
{
    return m_issued;
}

void FakeFrameSource::grab(const QRect &logical)
{
    m_requested.append(logical);
    if (const std::optional<QRect> rect = beginRequest(logical)) {
        issue(*rect);
    }
}

void FakeFrameSource::issue(const QRect &logical)
{
    // Through the event loop, because a KWin reply arrives that way: a caller that grabs again
    // from its frameReady() handler has to see the same latest-wins ordering here.
    QTimer::singleShot(0, this, [this, logical] {
        m_issued.append(logical);
        if (m_failure.isEmpty()) {
            Frame frame;
            frame.logicalRect = logical;
            frame.image = m_image;
            frame.image.setDevicePixelRatio(m_scale);
            frame.scale = m_scale;
            frame.occluded = m_occluded.intersected(logical);
            // The compositor paints the `no_screen_share` rectangle into the pixels it copies, so
            // the double paints it too. Without it the returned image is the same whatever the
            // card is doing, the frame hash never moves, and a caller keyed on (rect, hash)
            // answers every grab from one cache entry carrying one stale occlusion -- which is
            // the opposite of what capture::WlrFrameSource delivers.
            if (!frame.occluded.isEmpty() && !frame.image.isNull()) {
                const QRect box = logicalToImage(frame, frame.occluded).intersected(frame.image.rect());
                if (!box.isEmpty()) {
                    QPainter painter{&frame.image};
                    painter.fillRect(box, Qt::black);
                }
            }
            frame.hash = hashImage(frame.image);
            frame.grabMs = requestElapsedMs();
            Q_EMIT frameReady(frame);
        } else {
            Q_EMIT failed(m_failure);
        }
        if (const std::optional<QRect> next = endRequest()) {
            issue(*next);
        }
    });
}

} // namespace maru::capture
