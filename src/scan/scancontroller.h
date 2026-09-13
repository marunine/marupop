// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The scan loop: pointer position in, dictionary response out.
// Pointer moves are throttled; the region is grabbed and hashed. An unchanged hash reuses
// recognition; changed pixels go to the OCR worker. A hit test identifies the character,
// and a pool task looks up the paragraph from that position. None of these stages blocks
// the GUI thread.
//
// The requested rectangle, scan generation and lookup ticket reject stale replies from
// capture, recognition and lookup respectively.
//
// hitMoved() updates the popup anchor at pointer-sample rate; lookupReady() replaces the
// content only when the resolved lookup changes.
#pragma once

#include "capture/framesource.h"
#include "core/enums.h"
#include "lookup/lookuptypes.h"
#include "ocr/ocrtypes.h"
#include "scan/hitcontext.h"
#include "scan/scancache.h"
#include "scan/throttle.h"

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

namespace maru::cursor
{
class CursorTracker;
class LockWatcher;
} // namespace maru::cursor

namespace maru::ocr
{
class OcrService;
}

namespace maru::scan
{

class ScanController : public QObject
{
    Q_OBJECT

public:
    // The dictionary lookup. It is called on a pool thread, so the callable and everything it
    // reaches have to be safe to call from a thread that is not the one that built it;
    // lookup::Engine::lookup() is const and thread-safe, which is what it is written against.
    using LookupFunction = std::function<lookup::Response(const lookup::Request &)>;

    ScanController(cursor::CursorTracker &tracker,
                   capture::FrameSource &frames,
                   ocr::OcrService &ocr,
                   LookupFunction lookup,
                   QObject *parent = nullptr);
    ~ScanController() override;

    // Optional: without one the controller never pauses for the greeter. The watcher is not
    // owned and may outlive nothing; a null pointer detaches the current one.
    void setLockWatcher(cursor::LockWatcher *watcher);

    // Starts or stops the whole loop and writes the state through to the settings, so a
    // restart resumes what the user left.
    void setScanning(bool scanning);
    [[nodiscard]] bool isScanning() const;

    // The text last emitted through statusChanged(), for a tray tooltip built after the fact.
    [[nodiscard]] QString statusText() const;

    // Re-reads the Scanning and Lookup settings groups. Called after the settings dialog
    // applies, and once from the constructor.
    void applySettings();

    // Drops the cache and scans the region around the last known pointer position again, which
    // is what a manual retry after a failed grab does.
    void forceRescan();

    // The result count every lookup asks for at least, on top of MaxResults. app/LookupWindow
    // sets it to LookupWindowMaxResults while it is open and to 0 once it closes. A raise of the
    // count a lookup asks for, here or through MaxResults in applySettings(), runs the last lookup
    // again at the larger count while its hit is on screen, with no grab and no recognition pass,
    // and delivers it through lookupReady(). With no hit on screen the raise clears the record of
    // the last hit instead, so the next hit over that character is looked up at the larger count.
    void setMinimumResults(int count);

Q_SIGNALS:
    void scanningChanged(bool scanning);
    void lookupReady(const maru::lookup::Response &response, const maru::scan::HitContext &context);
    // The pointer reached cursorLogical while the popup holds the response lookupReady() last
    // delivered. Emitted once per pointer sample that changes the position and keeps that
    // response on screen, so the anchor is one pointer sample old rather than one character old.
    // screen is the output under cursorLogical, or nullptr where the tracker reported none.
    //
    // Two positions are left out. A position the hit test rejects raises nothingUnderCursor()
    // instead. A position outside the cached region raises nothing while both TriggerOnCursorMove
    // and PeriodicPollEnabled are off, because no path re-evaluates such a position.
    void hitMoved(QPoint cursorLogical, QScreen *screen);
    // The pointer left every recognized character, or the region under it holds none. Emitted
    // once per transition rather than per scan, because the application hides the popup on it.
    void nothingUnderCursor(QPoint cursorLogical);
    // Short human text for the tray tooltip: "Scanning · meikiocr", "Paused (screen locked)".
    void statusChanged(const QString &status);
    void error(const QString &message);

private:
    // The part of a hit the lookup response is turned into a highlight rectangle with.
    struct PendingHit
    {
        ocr::Paragraph paragraph;
        QRect logicalRect;
        qreal scale = 1.0;
        int charIndex = 0;
    };

    void onPositionChanged(QPoint logical, QScreen *screen);
    void onCursorAvailabilityChanged(bool available, const QString &reason);
    void onLockedChanged(bool locked);
    void onThrottleFired();
    void onPollTick();
    void onFrameReady(const capture::Frame &frame);
    void onFrameFailed(const QString &message);
    void onRecognized(const ocr::Result &result, QRect rect, quint64 hash, qreal scale, qint64 grabMs, QRect occluded);

    // Starts a grab. rect is in logical global desktop coordinates; an empty rect is ignored.
    void startScan(QRect rect);
    // The scan in flight returned: a frame answered from the cache, a recognition pass, or a
    // failure. Re-arms the periodic poll, so its interval counts from here.
    void scanReturned();
    // Hit-tests entry at the current pointer position and takes it from there: a lookup, a
    // grown rect, or nothingUnderCursor(). True where a character was found, which is what
    // tells the pointer path that no fresh grab is needed. allowGrowth is false on the path
    // that only re-tests a cached result, where a larger region would answer the same.
    bool evaluate(const CachedScan &entry, qint64 grabMs, bool allowGrowth);
    // Runs the lookup on the global thread pool and emits lookupReady() back on this thread,
    // unless a newer hit has been dispatched meanwhile.
    void dispatchLookup(const lookup::Request &request, HitContext context);
    // Runs on this thread once the pool thread is done. context arrives by value because the
    // matched-character rectangle is filled in here, from the highlight length the response
    // reports.
    void deliverLookup(quint64 ticket, const lookup::Response &response, HitContext context);
    // Grows the current rect one rung, if the ladder allows it. True when a grab was started.
    // entry is the cached result the grown region would replace, and a growth that would take its
    // characters below the extent the detector separates them at is declined.
    bool growAndRescan(const CachedScan &entry);
    // True where a recognition pass over rect reaches the detector with the characters of entry
    // at or above ocr::minimumCharExtent(). One pass covers a fixed detector input, so a larger
    // region reaches it with proportionally smaller characters and the hit test then answers a
    // different character for a pointer that has not moved.
    [[nodiscard]] bool growthResolvesText(const CachedScan &entry, QRect rect) const;
    // True where capture::initialRect() around the pointer is smaller than m_currentRect on
    // either axis, which is the condition for a fresh scan to resolve the same text at a larger
    // extent than the cached result did.
    [[nodiscard]] bool finerRegionAvailable() const;
    // True where a fresh scan at the initial size is worth one grab: the current region grew over
    // pixels holding no character, the pass over it recognized characters the detector could not
    // resolve, and a smaller region exists. The move throttle and the periodic poll both read it.
    [[nodiscard]] bool descentWouldResolve() const;
    // capture::initialRect() around the pointer, over the current workspace.
    [[nodiscard]] QRect initialRectAroundCursor() const;
    void reportNoHit();
    // The larger of MaxResults and the minimum setMinimumResults() set, which is what a lookup
    // asks for.
    [[nodiscard]] int requestedResults() const;
    // Where requestedResults() rose above previousResults: dispatches m_lastRequest again at the
    // new count while m_hitActive holds, and clears the record of the last hit otherwise.
    void lookUpAgainIfRaised(int previousResults);
    // What evaluate() answers where the pointer, or the character resolved for it, is under
    // MaruPop's own card in the grabbed pixels. A card already on screen keeps its answer and the
    // sample changes nothing; with no card up, the sample reports no hit. Returns what evaluate()
    // returns, so both of its occlusion branches are one line.
    bool holdThroughOcclusion();
    // The rect the periodic poll re-grabs: the one the current cached result came from, or a
    // fresh initial rect around the pointer.
    [[nodiscard]] QRect pollRect() const;
    // True where a move trigger or the periodic poll will scan a position outside the cached
    // region, which is what lets the card follow the pointer there.
    [[nodiscard]] bool willRescan() const;
    [[nodiscard]] bool isActive() const;
    // Applies the scanning and lock state to the tracker, the poller and the cached state.
    void updateActivity();
    void publishStatus();

    cursor::CursorTracker &m_tracker;
    capture::FrameSource &m_frames;
    ocr::OcrService &m_ocr;
    LookupFunction m_lookup;
    QPointer<cursor::LockWatcher> m_lockWatcher;

    ScanCache m_cache;
    Throttle m_moveThrottle;
    PeriodicPoller m_poller;

    QPoint m_cursor;
    QScreen *m_cursorScreen = nullptr;

    bool m_scanning = false;
    bool m_locked = false;
    // A lookup was emitted and the popup is showing it. The transition back to false is what
    // nothingUnderCursor() reports.
    bool m_hitActive = false;
    bool m_nothingReported = false;

    // The rect of the grab in flight. A frame for any other rect belongs to a grab that has
    // been overtaken and is dropped.
    QRect m_pendingRect;
    // A grab was started and neither its frame nor its recognition pass has returned.
    bool m_scanInFlight = false;
    // A poll tick found m_scanInFlight set and was skipped. The next tick grabs regardless.
    bool m_pollDeferred = false;
    // The rect and hash of the cached result the pointer is currently over.
    QRect m_currentRect;
    quint64 m_currentHash = 0;
    // ocr::resolvesCharactersAt() over the cached result the pointer is currently over.
    bool m_currentResolves = true;
    // The region in hand was grown by growAndRescan() out of a result holding no character to
    // measure. Paired with m_currentResolves in descentWouldResolve(), it is what bounds the
    // descent to the case the growth gate cannot see coming, and what keeps the descent and the
    // gate from alternating between two rungs for the rest of the session.
    bool m_grewUnmeasured = false;
    // The hit the last lookup was dispatched for, so an unchanged frame under an unmoved
    // pointer does not re-run the lookup on every poll.
    QRect m_lastRect;
    quint64 m_lastHash = 0;
    int m_lastParagraph = -1;
    int m_lastCharIndex = -1;
    // The paragraph and frame geometry of the hit the lookup in flight belongs to, which is
    // what turns the response's highlight length into a rectangle on the desktop. Written and
    // read on this thread alone, and always the pair of the newest ticket.
    PendingHit m_pendingHit;
    // The request and the context of the newest dispatched lookup, which lookUpAgainIfRaised()
    // sends again. Paired with m_pendingHit, which describes the same hit.
    lookup::Request m_lastRequest;
    HitContext m_lastContext;
    // The last message reported through error(), so a compositor that answers every grab the
    // same way is reported once rather than twice a second.
    QString m_lastError;

    quint64 m_scanGeneration = 0;
    quint64 m_lookupTicket = 0;
    // Zeroed by the destructor: a lookup still running on a pool thread reads it and returns
    // without touching anything of this object.
    std::shared_ptr<std::atomic<quint64>> m_alive;

    bool m_triggerOnCursorMove = true;
    bool m_periodicPollEnabled = true;
    bool m_progressiveScanArea = true;
    bool m_pauseWhileLocked = true;
    QSize m_initialSize;
    QSize m_maxSize;
    int m_maxLookupLength = 41;
    int m_maxResults = 10;
    int m_minimumResults = 0;
    LookupCategory m_category = LookupCategory::All;

    QString m_status;
    QString m_cursorReason;
    bool m_cursorAvailable = true;
};

} // namespace maru::scan
