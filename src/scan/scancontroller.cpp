// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "scan/scancontroller.h"

#include "capture/scanregion.h"
#include "core/logging.h"
#include "core/settings.h"
#include "cursor/cursortracker.h"
#include "cursor/lockwatcher.h"
#include "jp/japanese.h"
#include "ocr/hittest.h"
#include "ocr/meikiocrbackend.h"
#include "ocr/ocrservice.h"
#include "ocr/resolution.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QThreadPool>

#include <KLocalizedString>

#include <algorithm>
#include <utility>

namespace maru::scan
{

namespace
{

// How close a paragraph box may come to the edge of the frame before the region is grabbed
// again one rung larger. Four pixels rather than zero because a recognition model puts the box
// of an edge character a pixel or two inside the image even where the glyph is cut off.
constexpr int kEdgeMarginPx = 4;

} // namespace

ScanController::ScanController(cursor::CursorTracker &tracker,
                               capture::FrameSource &frames,
                               ocr::OcrService &ocr,
                               LookupFunction lookup,
                               QObject *parent)
    : QObject(parent)
    , m_tracker(tracker)
    , m_frames(frames)
    , m_ocr(ocr)
    , m_lookup(std::move(lookup))
    , m_alive(std::make_shared<std::atomic<quint64>>(1))
{
    connect(&m_tracker, &cursor::CursorTracker::positionChanged, this, &ScanController::onPositionChanged);
    connect(
        &m_tracker, &cursor::CursorTracker::availabilityChanged, this, &ScanController::onCursorAvailabilityChanged);
    connect(&m_frames, &capture::FrameSource::frameReady, this, &ScanController::onFrameReady);
    connect(&m_frames, &capture::FrameSource::failed, this, &ScanController::onFrameFailed);
    connect(&m_moveThrottle, &Throttle::triggered, this, &ScanController::onThrottleFired);
    connect(&m_poller, &PeriodicPoller::tick, this, &ScanController::onPollTick);
    connect(&m_ocr, &ocr::OcrService::availabilityChanged, this, [this](bool) {
        publishStatus();
    });
    connect(&m_ocr, &ocr::OcrService::backendChanged, this, [this](const QString &) {
        publishStatus();
    });

    applySettings();
}

ScanController::~ScanController()
{
    // A lookup running on a pool thread reads this on the way out of the callable and again
    // before it posts the result back, so neither reaches an object that no longer exists.
    m_alive->store(0);
}

void ScanController::setLockWatcher(cursor::LockWatcher *watcher)
{
    if (m_lockWatcher == watcher) {
        return;
    }
    if (m_lockWatcher != nullptr) {
        disconnect(m_lockWatcher, &cursor::LockWatcher::lockedChanged, this, nullptr);
    }
    m_lockWatcher = watcher;
    if (m_lockWatcher != nullptr) {
        connect(m_lockWatcher, &cursor::LockWatcher::lockedChanged, this, &ScanController::onLockedChanged);
        m_locked = m_lockWatcher->isLocked();
    } else {
        m_locked = false;
    }
    updateActivity();
}

void ScanController::setScanning(bool scanning)
{
    if (m_scanning == scanning) {
        return;
    }
    m_scanning = scanning;
    settings::persistScanningEnabled(scanning);
    updateActivity();
    Q_EMIT scanningChanged(m_scanning);
}

bool ScanController::isScanning() const
{
    return m_scanning;
}

QString ScanController::statusText() const
{
    return m_status;
}

void ScanController::applySettings()
{
    const int previousResults = requestedResults();
    m_triggerOnCursorMove = PopSettings::triggerOnCursorMove();
    m_moveThrottle.setIntervalMs(PopSettings::cursorMoveThrottleMs());
    m_periodicPollEnabled = PopSettings::periodicPollEnabled();
    m_poller.setIntervalMs(PopSettings::periodicPollIntervalMs());
    m_progressiveScanArea = PopSettings::progressiveScanArea();
    m_pauseWhileLocked = PopSettings::pauseWhileLocked();
    m_initialSize = settings::initialScanSize();
    m_maxSize = settings::maxScanSize();
    m_maxLookupLength = PopSettings::maxLookupLength();
    m_maxResults = PopSettings::maxResults();
    m_category = settings::lookupCategory();
    lookUpAgainIfRaised(previousResults);

    updateActivity();
}

void ScanController::setMinimumResults(int count)
{
    const int previousResults = requestedResults();
    m_minimumResults = qMax(0, count);
    lookUpAgainIfRaised(previousResults);
}

int ScanController::requestedResults() const
{
    return qMax(m_maxResults, m_minimumResults);
}

void ScanController::lookUpAgainIfRaised(int previousResults)
{
    if (requestedResults() <= previousResults) {
        return;
    }
    // The response in hand was cut at the smaller count. A hit on screen is answered again now:
    // a pointer at rest with PeriodicPollEnabled off produces no further evaluation that would.
    if (m_hitActive && isActive()) {
        lookup::Request request = m_lastRequest;
        request.maxResults = requestedResults();
        m_lastRequest.maxResults = request.maxResults;
        HitContext context = m_lastContext;
        context.cursorLogical = m_cursor;
        context.screen = m_cursorScreen;
        context.grabMs = 0;
        context.ocrMs = 0;
        dispatchLookup(request, context);
        return;
    }
    m_lastParagraph = -1;
    m_lastCharIndex = -1;
}

void ScanController::forceRescan()
{
    m_cache.clear();
    m_currentRect = {};
    m_currentHash = 0;
    m_currentResolves = true;
    m_lastRect = {};
    m_lastHash = 0;
    m_lastParagraph = -1;
    m_lastCharIndex = -1;
    if (!isActive()) {
        return;
    }
    m_moveThrottle.cancel();
    startScan(initialRectAroundCursor());
}

void ScanController::onPositionChanged(QPoint logical, QScreen *screen)
{
    if (!isActive()) {
        return;
    }
    const bool moved = logical != m_cursor;
    m_cursor = logical;
    m_cursorScreen = screen;

    // The region the last result came from is assumed unchanged until the periodic poll says
    // otherwise, which is what makes a pointer moving through a paragraph free: the frame was
    // grabbed without the cursor in it, so its hash does not move with the pointer.
    bool answered = false;
    if (!m_currentRect.isEmpty() && m_currentRect.contains(logical)) {
        if (const CachedScan *entry = m_cache.find(m_currentRect, m_currentHash)) {
            // No growth from here: the pointer is over a part of the region that was already
            // recognized, so a larger grab would tell nothing new about this point. The
            // throttle below is what re-centres the region on the pointer instead.
            answered = evaluate(*entry, 0, false);
        }
    }

    // The position the response is drawn at changes on every sample while the character it
    // answers for changes once per character crossed, and this is the emit that separates the
    // two. It runs after evaluate() so that a sample which left the text reaches reportNoHit()
    // first, which clears m_hitActive and takes the card away rather than moving it onto a point
    // holding no character.
    //
    // A position outside m_currentRect has no answer yet, and the card follows the pointer while
    // a move trigger or the periodic poll re-grabs. Neither of those runs while both settings are
    // off, so willRescan is what stops the card following a pointer whose position nothing will
    // ever evaluate.
    if (moved && m_hitActive && (answered || willRescan())) {
        Q_EMIT hitMoved(logical, screen);
    }
    if (answered) {
        // The region was grown over pixels holding no character and the pass over it found one
        // the detector could not resolve, so the character it names under this position is the
        // one a coarser pass resolved. A fresh scan at the initial size resolves the same text at
        // a larger extent, and the throttle bounds it to one grab per CursorMoveThrottleMs.
        if (m_triggerOnCursorMove && descentWouldResolve()) {
            m_moveThrottle.request();
        }
        return;
    }

    if (m_triggerOnCursorMove) {
        m_moveThrottle.request();
    }
}

void ScanController::onCursorAvailabilityChanged(bool available, const QString &reason)
{
    m_cursorAvailable = available;
    m_cursorReason = reason;
    // Only where the tracker went away on its own. Turning scanning off stops it deliberately,
    // and a warning for a state the user just asked for is noise.
    if (!available && m_scanning) {
        qCWarning(logScan) << "pointer tracking is unavailable:" << reason;
    }
    publishStatus();
}

void ScanController::onLockedChanged(bool locked)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    qCDebug(logScan) << "session lock state changed to" << locked;
    updateActivity();
}

void ScanController::onThrottleFired()
{
    if (!isActive()) {
        return;
    }
    startScan(initialRectAroundCursor());
}

void ScanController::onPollTick()
{
    if (!isActive()) {
        return;
    }
    // The poll exists for text that changes under a pointer that does not move: a subtitle, a
    // visual novel line, a scrolling page. It costs one grab and one hash; the recognition
    // pass runs only where the hash changed.
    //
    // A scan still in flight is left to finish: a poll grab would take over its generation and
    // drop the recognition pass it is waiting on, and its return re-arms the poll anyway. One tick
    // is the whole deferral, so a grab the source never answers delays the poll by one interval
    // rather than stopping it.
    if (m_scanInFlight && !m_pollDeferred) {
        m_pollDeferred = true;
        return;
    }
    m_pollDeferred = false;
    startScan(pollRect());
}

void ScanController::onFrameReady(const capture::Frame &frame)
{
    if (!isActive()) {
        return;
    }
    if (frame.logicalRect != m_pendingRect) {
        // A grab that was overtaken: the pointer moved, or the region grew, before this reply
        // arrived.
        qCDebug(logScan) << "dropping a frame for" << frame.logicalRect << "while waiting for" << m_pendingRect;
        return;
    }

    if (const CachedScan *entry = m_cache.find(frame.logicalRect, frame.hash)) {
        // The poll's whole point: the same pixels answer from the cache and the recognition
        // pass, which is the expensive half, does not run.
        qCDebug(logScan) << "the frame for" << frame.logicalRect << "is unchanged; skipping recognition";
        scanReturned();
        evaluate(*entry, frame.grabMs, true);
        return;
    }

    const quint64 generation = m_scanGeneration;
    const QRect rect = frame.logicalRect;
    const quint64 hash = frame.hash;
    const qreal scale = frame.scale;
    const qint64 grabMs = frame.grabMs;
    // Where MaruPop's own popup was when the pixels were taken. Empty on a source that excludes
    // the caller's windows from the render; the card's rectangle on a wlroots compositor, which
    // composites it into the copy.
    const QRect occluded = frame.occluded;
    QPointer<ScanController> self(this);
    m_ocr.recognize(frame.image, [self, generation, rect, hash, scale, grabMs, occluded](const ocr::Result &result) {
        if (self.isNull()) {
            return;
        }
        if (self->m_scanGeneration != generation) {
            qCDebug(logScan) << "dropping a recognition result for" << rect << "taken over by a newer scan";
            return;
        }
        self->onRecognized(result, rect, hash, scale, grabMs, occluded);
    });
}

void ScanController::onFrameFailed(const QString &message)
{
    // Before the repeat guard: a compositor that fails every grab is still polled at the interval.
    scanReturned();
    if (message == m_lastError) {
        // A compositor that is gone answers every grab the same way, and the poll asks twice a
        // second by default. The log line is inside the guard for that reason: a persistent
        // failure is worth one line, not two a second for the rest of the session.
        return;
    }
    qCWarning(logScan) << "grab failed:" << message;
    m_lastError = message;
    Q_EMIT error(message);
}

void ScanController::onRecognized(
    const ocr::Result &result, QRect rect, quint64 hash, qreal scale, qint64 grabMs, QRect occluded)
{
    if (!isActive()) {
        return;
    }
    scanReturned();
    if (!result.success) {
        const QString message =
            result.errorMessage.isEmpty() ? i18nc("@info", "Text recognition failed.") : result.errorMessage;
        qCWarning(logScan) << "recognition failed:" << message;
        if (message != m_lastError) {
            m_lastError = message;
            Q_EMIT error(message);
        }
        return;
    }
    m_lastError.clear();

    m_cache.insert(rect, hash, scale, result, occluded);
    const CachedScan *entry = m_cache.find(rect, hash);
    if (entry == nullptr) {
        return;
    }
    evaluate(*entry, grabMs, true);
}

bool ScanController::evaluate(const CachedScan &entry, qint64 grabMs, bool allowGrowth)
{
    m_currentRect = entry.logicalRect;
    m_currentHash = entry.hash;
    m_currentResolves =
        ocr::resolvesCharactersAt(entry.medianCharExtent, entry.result.modelInputSize, entry.result.sourceSize);

    const capture::Frame frame = entry.frame();
    if (!entry.occluded.isEmpty() && entry.occluded.contains(m_cursor)) {
        // The pointer is over MaruPop's own card, which a wlroots compositor composites into every
        // grab it overlaps. The test is on the pointer rather than on the character the hit test
        // below resolves, because under the `no_screen_share` layer rule there is no character to
        // test: the card's pixels reach the recognition pass as solid black, nothing is detected
        // in them, and the ordinary no-hit path would run -- hiding the card, which uncovers the
        // text, which resolves the character again, which shows the card over it again. That is a
        // flicker at the scan rate rather than an answer, and it is what the layer rule the setup
        // asks for would produce on its own.
        return holdThroughOcclusion();
    }
    if (allowGrowth && !entry.logicalRect.contains(m_cursor) && willRescan()) {
        // A grab that returned after the pointer left the region it covers. Its pixels say nothing
        // about the pointer, so a miss here is not the pointer leaving the text: the card keeps the
        // response it has and follows the pointer through hitMoved() until the scan around the new
        // position lands. Growing would only move the region onto the pointer one rung larger, and
        // the move trigger does that at the initial size.
        if (m_triggerOnCursorMove) {
            m_moveThrottle.request();
        }
        return m_hitActive;
    }
    const std::optional<ocr::Hit> hit = ocr::hitTest(entry.result, capture::logicalToImage(frame, m_cursor));
    if (!hit.has_value() || hit->paragraph < 0 || hit->paragraph >= entry.result.paragraphs.size()) {
        m_lastParagraph = -1;
        m_lastCharIndex = -1;
        if (allowGrowth && growAndRescan(entry)) {
            return false;
        }
        reportNoHit();
        return false;
    }

    const ocr::Paragraph &paragraph = entry.result.paragraphs.at(hit->paragraph);
    if (hit->charIndex < 0 || hit->charIndex >= paragraph.chars.size()) {
        reportNoHit();
        return false;
    }
    if (!entry.occluded.isEmpty()) {
        // The pointer is outside the card -- the check above answered that -- but the character
        // the hit test resolved for it is inside. ocr::hitTest() falls back to the nearest box
        // within kHitTolerancePx, so a pointer just off the card's edge can be answered by what
        // the recognition pass found on the card itself, which is the card's own rendering rather
        // than desktop text. Reachable only where the `no_screen_share` layer rule is absent, since
        // the rule leaves nothing there to recognize. Growing the region would move the card with
        // the pointer and find the same thing again.
        const QRect charBox = capture::imageToLogical(frame, ocr::charSpanRect(paragraph, hit->charIndex, 1));
        if (entry.occluded.contains(charBox.center())) {
            return holdThroughOcclusion();
        }
    }
    if (!jp::isJapanese(paragraph.chars.at(hit->charIndex).codePoint)) {
        // Recognized text the dictionaries have nothing to say about: a Latin caption inside a
        // Japanese paragraph. Growing the region would find the same character again.
        m_lastParagraph = -1;
        m_lastCharIndex = -1;
        reportNoHit();
        return false;
    }

    const bool sameHit = m_lastRect == entry.logicalRect && m_lastHash == entry.hash &&
                         m_lastParagraph == hit->paragraph && m_lastCharIndex == hit->charIndex;
    if (sameHit) {
        // Same pixels, same character: the popup already shows the answer, and the periodic
        // poll must not re-run the lookup for it every two seconds.
        return true;
    }

    m_lastRect = entry.logicalRect;
    m_lastHash = entry.hash;
    m_lastParagraph = hit->paragraph;
    m_lastCharIndex = hit->charIndex;

    HitContext context;
    context.cursorLogical = m_cursor;
    context.screen = m_cursorScreen;
    context.paragraphRectLogical = capture::imageToLogical(frame, paragraph.box);
    context.paragraphText = paragraph.text;
    context.cursorIndex = hit->charIndex;
    context.vertical = paragraph.vertical;
    context.backendName = entry.result.backendName;
    context.grabMs = grabMs;
    context.ocrMs = entry.result.elapsedMs;
    // A placeholder until the response says how many characters matched; a response with no
    // results never reaches the popup, so this rect is only ever replaced, never shown.
    context.matchedRectLogical = capture::imageToLogical(frame, ocr::charSpanRect(paragraph, hit->charIndex, 1));

    m_pendingHit = PendingHit{
        .paragraph = paragraph, .logicalRect = entry.logicalRect, .scale = entry.scale, .charIndex = hit->charIndex};

    lookup::Request request;
    request.sourceText = paragraph.text;
    request.cursorIndex = hit->charIndex;
    request.maxSearchLength = m_maxLookupLength;
    request.maxResults = requestedResults();
    request.category = m_category;
    // The confidence gate of the recognition-variant pass is calibrated on meikiocr's scores;
    // Chrome Screen AI reports its own scale, so its paragraphs carry none and every character
    // stays substitutable.
    if (entry.result.backendName == ocr::MeikiOcrBackend::displayName()) {
        request.confidences.reserve(paragraph.chars.size());
        for (const ocr::CharBox &character : paragraph.chars)
            request.confidences.append(character.confidence);
    }
    m_lastRequest = request;
    m_lastContext = context;
    dispatchLookup(request, context);

    // The paragraph reaches the edge of the frame, so the text continues outside it. The hit
    // in flight is still emitted: the popup appears from the region that was grabbed, and the
    // larger region replaces it a few milliseconds later with the full match.
    if (allowGrowth &&
        capture::touchesEdge(paragraph.box, QRect{QPoint{0, 0}, entry.result.sourceSize}, kEdgeMarginPx)) {
        growAndRescan(entry);
    }
    return true;
}

void ScanController::dispatchLookup(const lookup::Request &request, HitContext context)
{
    if (!m_lookup) {
        qCWarning(logScan) << "no lookup function is installed";
        reportNoHit();
        return;
    }

    const quint64 ticket = ++m_lookupTicket;
    QPointer<ScanController> self(this);
    // The dispatcher of this thread rather than this object: a queued call posted to it lands
    // on this thread even where the controller is destroyed on the way, and the QPointer above
    // is what the delivered lambda checks.
    QThread *ownerThread = thread();
    // QThreadPool::start() rather than QtConcurrent::run(): nothing here waits on a QFuture,
    // and the future run() hands back would only be discarded.
    QThreadPool::globalInstance()->start([self,
                                          alive = m_alive,
                                          lookup = m_lookup,
                                          request,
                                          context = std::move(context),
                                          ticket,
                                          ownerThread]() mutable {
        if (alive->load() == 0) {
            return;
        }
        QElapsedTimer timer;
        timer.start();
        lookup::Response response = lookup(request);
        context.lookupMs = timer.elapsed();
        if (alive->load() == 0) {
            return;
        }

        QObject *sink = QAbstractEventDispatcher::instance(ownerThread);
        if (sink == nullptr) {
            sink = QCoreApplication::instance();
        }
        if (sink == nullptr) {
            return;
        }
        QMetaObject::invokeMethod(
            sink,
            [self, ticket, response = std::move(response), context] {
                if (self.isNull()) {
                    return;
                }
                self->deliverLookup(ticket, response, context);
            },
            Qt::QueuedConnection);
    });
}

void ScanController::deliverLookup(quint64 ticket, const lookup::Response &response, HitContext context)
{
    if (ticket != m_lookupTicket) {
        qCDebug(logScan) << "dropping a lookup response taken over by a newer hit";
        return;
    }
    if (!isActive()) {
        return;
    }
    if (response.results.isEmpty()) {
        reportNoHit();
        return;
    }

    // Geometry alone, which is what imageToLogical() reads. The occlusion is left empty: the
    // rectangle is a property of the frame the hit came from, and evaluate() has already refused
    // a character inside it.
    const capture::Frame frame{.image = {},
                               .logicalRect = m_pendingHit.logicalRect,
                               .scale = m_pendingHit.scale,
                               .hash = 0,
                               .grabMs = 0,
                               .occluded = {}};
    const int length = response.highlightLength > 0 ? static_cast<int>(response.highlightLength) : 1;
    const QRect span = ocr::charSpanRect(m_pendingHit.paragraph, m_pendingHit.charIndex, length);
    if (!span.isEmpty()) {
        context.matchedRectLogical = capture::imageToLogical(frame, span);
    }

    // Where the pointer is now rather than where the hit was tested: the card followed the pointer
    // through hitMoved() while the lookup ran, and anchoring the response at the tested position
    // would pull it back by however far the pointer travelled meanwhile.
    context.anchorLogical = m_cursor;
    context.anchorScreen = m_cursorScreen;

    m_hitActive = true;
    m_nothingReported = false;
    // The one line a user or a bug report needs: what was under the pointer, what it resolved
    // to, and where the time went. Info rather than debug, because it fires once per hit
    // rather than once per pointer move.
    qCInfo(logScan).nospace() << "hit at " << context.cursorLogical << " on "
                              << (context.screen != nullptr ? context.screen->name() : QStringLiteral("?"))
                              << ": index " << context.cursorIndex << " of \"" << context.paragraphText << "\" -> \""
                              << response.results.constFirst().primarySpelling << "\" (" << response.results.size()
                              << " results, grab " << context.grabMs << " ms, ocr " << context.ocrMs << " ms, lookup "
                              << context.lookupMs << " ms)";
    Q_EMIT lookupReady(response, context);
}

bool ScanController::growAndRescan(const CachedScan &entry)
{
    if (!m_progressiveScanArea || m_currentRect.isEmpty()) {
        return false;
    }
    const std::optional<QRect> grown = capture::grow(m_currentRect, m_cursor, m_maxSize, capture::workspaceRect());
    if (!grown.has_value()) {
        return false;
    }
    // The rect the source will actually grab, compared against the current one rather than the
    // rect the ladder asked for. capture::grow() bounds its result against the workspace, which
    // is the union of every output, while a source that quantizes clamps to the one output the
    // region is on. On a workspace whose largest output is smaller than MaxScanWidth by
    // MaxScanHeight -- a 1366x768 panel beside a 1920x1080 monitor, or a portrait panel -- the
    // ladder would keep asking for a larger rect, the source would keep answering the same tile,
    // and the pair would loop: same rect, same hash, a cache hit, no character, and another
    // growth, without ever reaching reportNoHit().
    const QRect target = m_frames.quantize(*grown);
    if (target.isEmpty() || target == m_currentRect) {
        return false;
    }
    // capture::grow() ends in movedToContain(grown, cursor), but a source that quantizes is free
    // to move the region off that point: capture::wlrTileFor() snaps the tile to a lattice whose
    // stride is half its size, which shifts the centre by up to a quarter of it. A grab that does
    // not hold the pointer cannot answer for the pointer, so the rung is declined rather than
    // spent.
    if (!target.contains(m_cursor)) {
        qCDebug(logScan) << "declining to grow the scan region to" << target << ": the quantized tile does not hold"
                         << m_cursor;
        return false;
    }
    if (!growthResolvesText(entry, target)) {
        return false;
    }
    qCDebug(logScan) << "growing the scan region from" << m_currentRect << "to" << target;
    startScan(target);
    // After startScan(), which clears the flag for every scan that is not a growth. A growth made
    // with no character to measure is the one descentWouldResolve() acts on.
    m_grewUnmeasured = entry.medianCharExtent <= 0.0;
    return true;
}

bool ScanController::growthResolvesText(const CachedScan &entry, QRect rect) const
{
    // KWinFrameSource::deliver() scales a frame to the device-pixel ratio of the output under the
    // region's centre, and capture::grow() can move that centre onto a neighbouring output of
    // another ratio. The larger of the two ratios is the prediction that declines rather than
    // admits where they differ, because a larger source image reaches the detector at a smaller
    // letterbox scale.
    const QScreen *screen = QGuiApplication::screenAt(rect.center());
    const qreal grownScale = screen != nullptr ? screen->devicePixelRatio() : 1.0;
    const qreal scale = std::max({entry.scale, grownScale, qreal{1.0}});
    const QSize sourceSize{qRound(rect.width() * scale), qRound(rect.height() * scale)};
    if (ocr::resolvesCharactersAt(entry.medianCharExtent, entry.result.modelInputSize, sourceSize)) {
        return true;
    }
    // Growing a crop shrinks its characters at the fixed-size detector input. Decline growth
    // when it would lose the resolution needed to keep the character under the pointer stable.
    qCDebug(logScan) << "declining to grow the scan region from" << m_currentRect << "to" << rect << ": a character of"
                     << entry.medianCharExtent << "px would reach the detector at"
                     << ocr::charExtentAtModelInput(entry.medianCharExtent, entry.result.modelInputSize, sourceSize)
                     << "px, under the" << ocr::minimumCharExtent(entry.result.modelInputSize)
                     << "px it separates characters at";
    return false;
}

bool ScanController::finerRegionAvailable() const
{
    const QRect initial = initialRectAroundCursor();
    return initial.width() < m_currentRect.width() || initial.height() < m_currentRect.height();
}

QRect ScanController::initialRectAroundCursor() const
{
    return capture::initialRect(m_cursor, m_initialSize, capture::workspaceRect());
}

void ScanController::reportNoHit()
{
    // A lookup still in flight answers for a character the pointer has since left. Delivered, it
    // would put the card back up at a position with no text under it, and nothing re-evaluates a
    // pointer at rest to take it away again. The record of the last hit goes with it, so a return
    // to that character is looked up again rather than taken for the answer on screen.
    ++m_lookupTicket;
    m_lastParagraph = -1;
    m_lastCharIndex = -1;
    if (!m_hitActive && m_nothingReported) {
        return;
    }
    m_hitActive = false;
    m_nothingReported = true;
    Q_EMIT nothingUnderCursor(m_cursor);
}

bool ScanController::holdThroughOcclusion()
{
    // A card already on screen keeps its answer rather than being taken away, because taking it
    // away is what starts the flicker: the card hides, the text it covered is grabbed clean, the
    // character resolves, and the card comes back over it. popup::placePopupAvoiding() keeps the
    // card off the paragraph it answers for wherever a free band exists, so this is the case where
    // none did -- a paragraph too close to every edge for the card to clear it.
    if (m_hitActive) {
        return true;
    }
    m_lastParagraph = -1;
    m_lastCharIndex = -1;
    reportNoHit();
    return false;
}

void ScanController::startScan(QRect rect)
{
    if (!isActive() || rect.isEmpty()) {
        return;
    }
    // The rect the source will actually grab, which is the identity onFrameReady() compares a
    // reply against. It is the identity on KWin, and the tile grid on a wlroots compositor,
    // which snaps the box so Hyprland's per-box screenshare session cache stays bounded.
    rect = m_frames.quantize(rect);
    if (rect.isEmpty()) {
        return;
    }
    ++m_scanGeneration;
    m_pendingRect = rect;
    // Cleared here and set again by growAndRescan(), which is the one caller that grows: every
    // other path starts a scan at the initial size or re-grabs the current region.
    m_grewUnmeasured = false;
    m_scanInFlight = true;
    m_frames.grab(rect);
}

void ScanController::scanReturned()
{
    m_scanInFlight = false;
    m_pollDeferred = false;
    // The poll interval counts from the last scan that returned rather than from a fixed phase:
    // a pointer-driven scan has just looked at these pixels, and a poll right behind it would only
    // grab them again.
    if (isActive() && m_periodicPollEnabled) {
        m_poller.start();
    }
}

QRect ScanController::pollRect() const
{
    // The initial size rather than the current region where the descent applies, which covers the
    // configuration with TriggerOnCursorMove off, where the poll is the only path back down the
    // ladder.
    if (!m_currentRect.isEmpty() && m_currentRect.contains(m_cursor) && !descentWouldResolve()) {
        return m_currentRect;
    }
    return initialRectAroundCursor();
}

bool ScanController::descentWouldResolve() const
{
    // m_grewUnmeasured is what keeps this from alternating between two rungs forever. The growth
    // gate predicts from the characters of the region it grows out of, and m_currentResolves is
    // measured over the characters of the region it grew into, so a larger region that brings
    // smaller text into the frame lowers the median and reports false while the gate keeps
    // approving the same growth. Restricting the descent to a growth made with no character to
    // measure leaves one pass at the coarser rung in that case, which is the behaviour the region
    // had before the gate existed, rather than a pass at each rung every CursorMoveThrottleMs.
    if (m_currentResolves || !m_grewUnmeasured) {
        return false;
    }
    return finerRegionAvailable();
}

bool ScanController::willRescan() const
{
    return m_triggerOnCursorMove || m_periodicPollEnabled;
}

bool ScanController::isActive() const
{
    return m_scanning && !(m_pauseWhileLocked && m_locked);
}

void ScanController::updateActivity()
{
    const bool active = isActive();
    m_tracker.setTracking(active);
    if (active && m_periodicPollEnabled) {
        m_poller.start();
    } else {
        m_poller.stop();
    }

    if (!active) {
        m_moveThrottle.cancel();
        // The cached results are pixels of whatever was on screen, so they go with the pause:
        // a session that comes back from the greeter re-grabs rather than answering from what
        // was on screen before it locked.
        m_cache.clear();
        m_currentRect = {};
        m_currentHash = 0;
        m_currentResolves = true;
        m_pendingRect = {};
        m_scanInFlight = false;
        m_pollDeferred = false;
        m_lastRect = {};
        m_lastHash = 0;
        m_lastParagraph = -1;
        m_lastCharIndex = -1;
        // Anything still in flight belongs to the state that was just left.
        ++m_scanGeneration;
        ++m_lookupTicket;
        if (m_hitActive) {
            m_hitActive = false;
            m_nothingReported = true;
            Q_EMIT nothingUnderCursor(m_cursor);
        }
    }

    publishStatus();
}

void ScanController::publishStatus()
{
    QString status;
    if (!m_scanning) {
        status = i18nc("@info:status", "Paused");
    } else if (m_pauseWhileLocked && m_locked) {
        status = i18nc("@info:status", "Paused (screen locked)");
    } else if (!m_ocr.isReady()) {
        status = i18nc("@info:status", "Text recognition unavailable");
    } else if (!m_cursorAvailable) {
        status = m_cursorReason.isEmpty() ? i18nc("@info:status", "Pointer tracking unavailable") : m_cursorReason;
    } else {
        status = i18nc(
            "@info:status, %1 is the name of a text-recognition backend", "Scanning · %1", m_ocr.activeBackendName());
    }

    if (status == m_status) {
        return;
    }
    m_status = status;
    Q_EMIT statusChanged(m_status);
}

} // namespace maru::scan
