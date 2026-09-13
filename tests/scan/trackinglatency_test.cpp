// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// How far behind the pointer the popup anchor runs, measured rather than reasoned about.
//
// The card is placed by popup::PopupWindow::showNear(), and src/app/application.cpp calls it from
// two handlers: lookupReady(), which carries a new model and the point the hit was tested at, and
// hitMoved(), which carries a point alone. The point the card is drawn against is therefore the
// point of whichever of the two fired last. The distance between that point and where the pointer
// actually is right now is the lag a user sees. This suite samples both at the relay's own 8 ms
// rate and prints the distribution, with the placement count and the lookup count in separate
// columns so a change to one is visible against the other.
//
// The difference from tests/scan/scancontroller_test.cpp is the coordinate space. That suite
// anchors its fake text to the grabbed frame, so the text follows the pointer and every grab
// finds the same character; here the text is fixed on the desktop and the frame source crops the
// region out of it, so the character under the pointer changes as the pointer travels. That is
// what makes an anchor-lag number mean anything.
//
// Every rectangle is derived from capture::workspaceRect(), so the same binary measures an
// 800x800 offscreen workspace and the 1920x1080 virtual output of tests/harness/nested-session.sh
// without a second set of constants.
//
// Both stage latencies are injectable. Defaults simulate a fast capture and a slower OCR pass
// so anchor updates must remain responsive while recognition is in flight.
#include "capture/framesource.h"
#include "capture/scanregion.h"
#include "core/settings.h"
#include "cropframesource.h"
#include "cursor/cursortracker.h"
#include "eventloop.h"
#include "fakes.h"
#include "ocr/backend.h"
#include "ocr/ocrservice.h"
#include "scan/scancontroller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>

using namespace maru;
using namespace maru::scan;
using maru::test::FakeTracker;

namespace
{

// One full-width glyph at the size a visual novel presents, which is what tools/hoverprobe.cpp
// paints and what a sweep crosses one cell at a time.
constexpr int kCellPx = 32;
// Distance between the top edges of two stacked lines, which leaves a 16 px gap.
constexpr int kLineSpacingPx = 48;
// The relay's active pump: 8 ms bounds the age of a pointer sample (data/kwin-script).
constexpr int kSampleMs = 8;
// The simulated stage costs a sweep is charged with by default.
constexpr int kGrabMs = 2;
constexpr int kRecognizeMs = 33;
// Logical pixels per sample for a full-page sweep, which at the 8 ms pump is 500 px/s: the speed a
// hand produces. Fixing the speed rather than the sample count is what makes the 800x800 and the
// 1920x1080 registration comparable, because the lag being measured is a distance covered per unit
// time.
constexpr int kSweepStepPx = 4;
// Bound a full-page sweep to kFullPageSamples * kSampleMs on every output size.
constexpr int kFullPageSamples = 200;

const QSize kInitialSize{480, 270};
const QSize kMaxSize{1600, 900};

QRect workspace()
{
    return capture::workspaceRect();
}

// The point kFullPageSamples steps of kSweepStepPx along the line from from to to, or to itself
// where the line is shorter than the budget allows.
QPoint boundedEnd(QPoint from, QPoint to)
{
    const QPoint delta = to - from;
    const double length = std::hypot(static_cast<double>(delta.x()), static_cast<double>(delta.y()));
    const double budget = static_cast<double>(kFullPageSamples) * kSweepStepPx;
    if (length <= budget || length <= 0.0) {
        return to;
    }
    const double ratio = budget / length;
    return from +
           QPoint{static_cast<int>(std::lround(delta.x() * ratio)), static_cast<int>(std::lround(delta.y() * ratio))};
}

// The number of samples a sweep from from to to takes at kSweepStepPx per sample.
int stepsFor(QPoint from, QPoint to)
{
    const QPoint delta = to - from;
    const double length = std::hypot(static_cast<double>(delta.x()), static_cast<double>(delta.y()));
    return std::max(1, static_cast<int>(std::lround(length / kSweepStepPx)));
}

// The text painted on the desktop, as a block of horizontal lines. Two shapes are used:
//
//   a short single line, laid out so a 480x270 region centred anywhere on it holds the whole
//   line with room to spare, which is the case that never grows the region and so measures the
//   cached path on its own; and
//
//   a block filling the workspace, which is what a page of text or a subtitle actually is: the
//   region cannot hold it, so the growth ladder and the re-grab path are in the measurement.
struct Page
{
    QStringList lines;
    QPoint origin;

    [[nodiscard]] QRect boxOf(int line, int index) const
    {
        return QRect{origin.x() + (index * kCellPx), origin.y() + (line * kLineSpacingPx), kCellPx, kCellPx};
    }

    [[nodiscard]] QRect box() const
    {
        QRect all;
        for (int line = 0; line < lines.size(); ++line) {
            all = all.united(boxOf(line, 0).united(boxOf(line, static_cast<int>(lines.at(line).size()) - 1)));
        }
        return all;
    }
};

// The page under test, read by PageBackend::recognize() on the marupop-ocr thread and assigned by
// setShortPage() and setFullPage() on the main thread. The two never overlap: a Harness destroys
// its ocr::OcrService before the case ends, and OcrService::~OcrService() joins the worker with a
// 5000 ms wait (ocrservice.cpp:131 to ocrservice.cpp:145), so no recognition is in flight when the
// next case assigns. A setter called while a Harness is alive would race on the QStringList
// reference count, so both setters are called before the Harness is constructed.
Page &page()
{
    static Page current;
    return current;
}

// Eight characters centred in the workspace. A 480x270 region centred on any of them clears the
// line's ends by more than the 4 px capture::touchesEdge() treats as the text running out of the
// frame, so nothing on this page ever grows the region.
void setShortPage()
{
    const QString text = QStringLiteral("日本語のテキスト");
    const QPoint centre = workspace().center();
    const int width = static_cast<int>(text.size()) * kCellPx;
    page() = Page{.lines = {text}, .origin = QPoint{centre.x() - (width / 2), centre.y()}};
}

// As many lines and characters as the workspace holds with a 60 px inset, which is a page of
// text. The region cannot cover it at any size the ladder reaches on a 1920x1080 output.
void setFullPage()
{
    const QString glyphs = QStringLiteral("今日は天気がいいですねと言われたので日本語のテキストを読んでいる");
    const int inset = 60;
    const int columns = std::max(1, (workspace().width() - (2 * inset)) / kCellPx);
    const int rows = std::max(1, (workspace().height() - (2 * inset)) / kLineSpacingPx);
    QStringList lines;
    for (int row = 0; row < rows; ++row) {
        QString text;
        text.reserve(columns);
        for (int column = 0; column < columns; ++column) {
            text.append(glyphs.at((static_cast<qsizetype>((row * columns) + column) * 7) % glyphs.size()));
        }
        lines.append(text);
    }
    page() = Page{.lines = lines, .origin = workspace().topLeft() + QPoint{inset, inset}};
}

// Answers every grab with a crop of the page, after a delay, through the event loop. The crop and
// the origin it carries come from maru::test::CropFrameSource, which tests/scan/scanresolution_test.cpp
// drives over the same page shape.
class PageFrameSource : public test::CropFrameSource
{
public:
    explicit PageFrameSource(QObject *parent = nullptr)
        : test::CropFrameSource(parent)
    {
        setDelayMs(kGrabMs);
    }

    void grab(const QRect &logical) override
    {
        ++requests;
        const qsizetype before = issued.size();
        test::CropFrameSource::grab(logical);
        grabs += static_cast<int>(issued.size() - before);
    }

    // Every grab() call, including the ones capture::FrameSource::beginRequest() coalesces into the
    // pending slot, and the subset that reached the compositor. The two differ whenever the
    // controller asks again while a reply is outstanding, so an assertion about compositor traffic
    // reads grabs and one about the controller's behaviour reads requests.
    int requests = 0;
    int grabs = 0;
};

// Reports the characters of the page that fall inside the image, in image pixels, after burning
// the recognition budget on the worker thread the way a real model does.
class PageBackend : public ocr::Backend
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("page");
    }

    bool initialize() override
    {
        m_ready = true;
        return true;
    }

    [[nodiscard]] bool isReady() const override
    {
        return m_ready;
    }

    ocr::Result recognize(const QImage &image) override
    {
        ++calls;
        if (delayMs > 0) {
            QThread::msleep(static_cast<unsigned long>(delayMs));
        }

        const QPoint origin = test::cropOriginOf(image);
        const QRect frame{QPoint{0, 0}, image.size()};

        ocr::Result result;
        result.success = true;
        result.backendName = name();
        result.sourceSize = image.size();

        for (int index = 0; index < page().lines.size(); ++index) {
            const ocr::TextLine line = lineIn(index, origin, frame);
            if (!line.text.isEmpty()) {
                result.lines.append(line);
            }
        }
        return result;
    }

    int delayMs = kRecognizeMs;
    std::atomic<int> calls{0};

private:
    // One line of the page, clipped to the frame. A glyph cut in half by the region edge is
    // clipped rather than dropped, because that is what makes capture::touchesEdge() fire and the
    // region grow one rung, a path this measurement has to keep.
    [[nodiscard]] static ocr::TextLine lineIn(int lineIndex, QPoint origin, QRect frame)
    {
        const QString &text = page().lines.at(lineIndex);
        ocr::TextLine line;
        for (int index = 0; index < text.size(); ++index) {
            const QRect box = page().boxOf(lineIndex, index).translated(-origin);
            if (!box.intersects(frame)) {
                continue;
            }
            const QRect clipped = box.intersected(frame);
            line.text.append(text.at(index));
            line.chars.append(ocr::CharBox{
                .codePoint = static_cast<char32_t>(text.at(index).unicode()), .box = clipped, .confidence = 1.0F});
            line.box = line.box.united(clipped);
        }
        line.confidence = line.text.isEmpty() ? 0.0F : 1.0F;
        return line;
    }

    bool m_ready = false;
};

// Return one character at the pointer so the controller can build a highlight.
// No dictionary work is charged; this isolates capture, OCR and placement behavior.
lookup::Response cannedResponse(const lookup::Request &request)
{
    lookup::Result result;
    result.matchedText = request.sourceText.mid(request.cursorIndex, 1);
    result.primarySpelling = result.matchedText;

    lookup::Response response;
    response.highlightStart = request.cursorIndex;
    response.highlightLength = 1;
    response.results.append(result);
    return response;
}

// One pointer sample and what the popup was anchored at when it was taken.
struct Sample
{
    qint64 atMs = 0;
    QPoint cursor;
    QPoint anchor;
    bool anchored = false;
};

struct Stats
{
    double p50 = 0.0;
    double p90 = 0.0;
    double max = 0.0;
    double mean = 0.0;
};

Stats statsOf(QList<double> values)
{
    Stats stats;
    if (values.isEmpty()) {
        return stats;
    }
    std::ranges::sort(values);
    const auto at = [&values](double fraction) {
        const auto index = static_cast<qsizetype>(fraction * static_cast<double>(values.size() - 1));
        return values.at(std::clamp<qsizetype>(index, 0, values.size() - 1));
    };
    stats.p50 = at(0.5);
    stats.p90 = at(0.9);
    stats.max = values.constLast();
    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    stats.mean = total / static_cast<double>(values.size());
    return stats;
}

struct Harness
{
    Harness()
    {
        auto owned = std::make_unique<PageBackend>();
        backend = owned.get();
        ocrService.setBackend(std::move(owned));

        // Counted through a shared_ptr rather than through the Harness: the callable runs on a
        // global thread pool thread, and ScanController::~ScanController() releases the controller
        // before the Harness members it would otherwise reach.
        controller = std::make_unique<ScanController>(
            tracker, frames, ocrService, [calls = lookupCalls](const lookup::Request &r) {
                calls->fetch_add(1);
                return cannedResponse(r);
            });

        // The two signals src/app/application.cpp places the card on. lookupReady() carries a new
        // model and a new point; hitMoved() carries a new point alone.
        QObject::connect(controller.get(),
                         &ScanController::lookupReady,
                         controller.get(),
                         [this](const lookup::Response &, const HitContext &context) {
                             ++lookups;
                             lookupLatencyNs = moveTimer.isValid() ? moveTimer.nsecsElapsed() : 0;
                             place(context.anchorLogical);
                         });
        QObject::connect(
            controller.get(), &ScanController::hitMoved, controller.get(), [this](QPoint point, QScreen *) {
                ++follows;
                followLatencyNs = moveTimer.isValid() ? moveTimer.nsecsElapsed() : 0;
                place(point);
            });
        QObject::connect(controller.get(), &ScanController::nothingUnderCursor, controller.get(), [this](QPoint) {
            anchored = false;
            ++misses;
        });

        // The GUI thread's own responsiveness, sampled by a 1 ms timer that runs on it. The gap
        // between two fires is the wall time the thread spent elsewhere, and the detection work
        // that runs there -- the hit test, the frame hash and the placement -- is inside it. The
        // largest gap over a sweep is the stall the pointer path can suffer from that work.
        watchdog.setInterval(1);
        watchdog.setTimerType(Qt::PreciseTimer);
        QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [this] {
            if (watchdogTimer.isValid()) {
                watchdogGaps.append(static_cast<double>(watchdogTimer.nsecsElapsed()) / 1e6);
            }
            watchdogTimer.start();
        });
        watchdog.start();
        clock.start();
    }

    // One placement of the card, from either of the two signals it is placed on.
    void place(QPoint point)
    {
        anchor = point;
        anchored = true;
        anchorAtMs = clock.elapsed();
        anchorLatencyNs = moveTimer.isValid() ? moveTimer.nsecsElapsed() : 0;
        ++anchorUpdates;
    }

    // Moves the pointer along a straight line at the relay's rate, keeping the event loop live
    // for the whole of each interval, and records where the anchor was at each sample.
    QList<Sample> sweep(QPoint from, QPoint to, int steps)
    {
        QList<Sample> samples;
        samples.reserve(steps + 1);
        for (int step = 0; step <= steps; ++step) {
            const double ratio = steps == 0 ? 0.0 : static_cast<double>(step) / steps;
            const QPoint point{from.x() + static_cast<int>(std::lround((to.x() - from.x()) * ratio)),
                               from.y() + static_cast<int>(std::lround((to.y() - from.y()) * ratio))};
            move(point);
            spin(kSampleMs);
            samples.append(Sample{.atMs = clock.elapsed(), .cursor = point, .anchor = anchor, .anchored = anchored});
        }
        return samples;
    }

    // Emits one pointer sample and restarts the clock the anchor latency is measured against.
    void move(QPoint logical)
    {
        moveTimer.start();
        tracker.move(logical);
    }

    // Runs the event loop for ms of wall time. Nothing here waits on a condition: the point of the
    // measurement is what the loop manages to deliver inside one pointer interval.
    //
    // maru::test::pumpFor() rather than a loop around
    // processEvents(): that loop returns as soon as the queue is empty and so holds a core for the
    // whole interval (997 ms of CPU per 1000 ms, tests/support/eventloop.h). Here it would have
    // competed with the 33 ms QThread::msleep PageBackend runs on the marupop-ocr thread, which is
    // the thread every latency below is measured against.
    void spin(int ms)
    {
        maru::test::pumpFor(ms);
    }

    void settle(int ms = 400)
    {
        spin(ms);
    }

    FakeTracker tracker;
    PageFrameSource frames;
    ocr::OcrService ocrService;
    PageBackend *backend = nullptr;
    std::unique_ptr<ScanController> controller;

    QElapsedTimer clock;
    // Restarted by move() and read by each of the two placement handlers, so the three latencies
    // below are each the cost of one pointer sample reaching the popup along one path.
    QElapsedTimer moveTimer;
    QPoint anchor;
    bool anchored = false;
    qint64 anchorAtMs = 0;
    // The last placement of either kind, the last hitMoved() placement, and the last lookupReady()
    // placement, in nanoseconds from the pointer sample that produced them.
    qint64 anchorLatencyNs = 0;
    qint64 followLatencyNs = 0;
    qint64 lookupLatencyNs = 0;
    // Placements of either kind, hitMoved() placements, lookupReady() placements, and lookup
    // dispatches onto the thread pool. anchorUpdates equals follows plus lookups.
    int anchorUpdates = 0;
    int follows = 0;
    int lookups = 0;
    std::shared_ptr<std::atomic<int>> lookupCalls = std::make_shared<std::atomic<int>>(0);
    int misses = 0;

    QTimer watchdog;
    QElapsedTimer watchdogTimer;
    QList<double> watchdogGaps;
};

void configureScanning(int throttleMs, bool progressive = true)
{
    PopSettings::setTriggerOnCursorMove(true);
    PopSettings::setCursorMoveThrottleMs(throttleMs);
    PopSettings::setPeriodicPollEnabled(false);
    PopSettings::setPeriodicPollIntervalMs(2000);
    PopSettings::setProgressiveScanArea(progressive);
    PopSettings::setPauseWhileLocked(true);
    PopSettings::setInitialScanWidth(kInitialSize.width());
    PopSettings::setInitialScanHeight(kInitialSize.height());
    PopSettings::setMaxScanWidth(kMaxSize.width());
    PopSettings::setMaxScanHeight(kMaxSize.height());
}

// The lag of one sample: how far the pointer is from the point the card was placed against.
double lagOf(const Sample &sample)
{
    const QPoint delta = sample.cursor - sample.anchor;
    return std::hypot(delta.x(), delta.y());
}

// Count the longest consecutive run where the visible card's anchor stays fixed
// while the pointer moves. A hidden card ends the run. Assertions use sample counts
// to avoid scheduler-dependent limits; longestFrozenIntervalMs() reports elapsed time.
int longestFrozenSampleRun(const QList<Sample> &samples)
{
    int longest = 0;
    int run = 0;
    bool open = false;
    QPoint previous;
    for (const Sample &sample : samples) {
        if (!sample.anchored) {
            longest = std::max(longest, run);
            open = false;
            run = 0;
            continue;
        }
        if (!open) {
            previous = sample.anchor;
            open = true;
            run = 1;
            continue;
        }
        if (sample.anchor == previous) {
            ++run;
            continue;
        }
        longest = std::max(longest, run);
        previous = sample.anchor;
        run = 1;
    }
    return std::max(longest, run);
}

// The same runs in wall time, for the report table alone.
qint64 longestFrozenIntervalMs(const QList<Sample> &samples)
{
    qint64 longest = 0;
    qint64 sinceMs = 0;
    bool open = false;
    QPoint previous;
    for (const Sample &sample : samples) {
        if (!sample.anchored) {
            if (open) {
                longest = std::max(longest, sample.atMs - sinceMs);
                open = false;
            }
            continue;
        }
        if (!open) {
            sinceMs = sample.atMs;
            previous = sample.anchor;
            open = true;
            continue;
        }
        if (sample.anchor != previous) {
            longest = std::max(longest, sample.atMs - sinceMs);
            sinceMs = sample.atMs;
            previous = sample.anchor;
        }
    }
    if (open) {
        longest = std::max(longest, samples.constLast().atMs - sinceMs);
    }
    return longest;
}

// The lag distribution over the samples whose card was shown, printed as one table row.
Stats report(const QString &label, const QList<Sample> &samples, const Harness &harness)
{
    QList<double> lags;
    int hidden = 0;
    for (const Sample &sample : samples) {
        if (!sample.anchored) {
            ++hidden;
            continue;
        }
        lags.append(lagOf(sample));
    }
    const Stats stats = statsOf(lags);
    const Stats gaps = statsOf(harness.watchdogGaps);
    std::printf("%-30s | %4lld %5lld %5d | %6.1f %6.1f %6.1f %6.1f | %5lld | %4d %4d %4d %4d %4d | %6.2f\n",
                qUtf8Printable(label),
                static_cast<long long>(samples.size()),
                static_cast<long long>(lags.size()),
                hidden,
                stats.mean,
                stats.p50,
                stats.p90,
                stats.max,
                static_cast<long long>(longestFrozenIntervalMs(samples)),
                harness.anchorUpdates,
                harness.lookups,
                harness.frames.requests,
                harness.frames.grabs,
                harness.backend->calls.load(),
                gaps.max);
    std::fflush(stdout);
    return stats;
}

void header()
{
    std::printf("\n%-30s | %4s %5s %5s | %6s %6s %6s %6s | %5s | %4s %4s %4s %4s %4s | %6s\n",
                "case",
                "smpl",
                "shown",
                "hidden",
                "lag~",
                "lag50",
                "lag90",
                "lagmax",
                "froze",
                "anch",
                "look",
                "req",
                "grab",
                "ocr",
                "stall");
    std::printf("%s\n", QByteArray(135, '-').constData());
    std::fflush(stdout);
}

} // namespace

// The pointer moves inside one character for 24 samples of 8 ms. Every sample hits
// the cache and resolves to the same character, so no second lookup is needed.
// hitMoved() must still move the card for each sample rather than leaving its anchor
// fixed throughout the 192 ms sweep.
TEST(TrackingLatencyTest, aPointerInsideOneCharacterMovesTheAnchorAndRunsOneLookup)
{
    configureScanning(200);
    setShortPage();
    Harness harness;
    const QRect box = page().boxOf(0, 4);
    const QPoint start = box.topLeft() + QPoint{4, 4};

    harness.controller->setScanning(true);
    harness.move(start);
    harness.settle();
    ASSERT_TRUE(harness.anchored) << "the first hit never landed";
    ASSERT_EQ(harness.anchorUpdates, 1);

    const QList<Sample> samples = harness.sweep(start, start + QPoint{kCellPx - 8, kCellPx - 8}, 24);
    header();
    const Stats stats = report(QStringLiteral("inside one character"), samples, harness);

    EXPECT_EQ(harness.frames.grabs, 1) << "a pointer over cached pixels grabbed again";
    EXPECT_EQ(harness.backend->calls.load(), 1);
    EXPECT_EQ(harness.lookups, 1) << "a pointer inside one character ran a second lookup";
    EXPECT_EQ(harness.lookupCalls->load(), 1) << "a pointer inside one character dispatched a second lookup";
    // Every sample of the sweep past the first moved the pointer by 1 px, and each is expected to
    // reach the card on its own.
    EXPECT_GE(harness.anchorUpdates, samples.size() - 1)
        << "a pointer sample inside a single character produced no placement";
    EXPECT_LE(stats.max, static_cast<double>(kCellPx) / 4.0) << "the anchor fell more than a quarter cell behind";
}

// The pointer crossing six characters inside one cached region, 1 px per 8 ms sample, which is a
// slow deliberate hover. The anchor follows every sample; the six characters are what set the
// lookup count.
TEST(TrackingLatencyTest, aPointerCrossingCharactersInsideTheCacheAnchorsOnEverySample)
{
    configureScanning(200);
    setShortPage();
    Harness harness;
    const QPoint start = page().boxOf(0, 1).center();
    const QPoint end = page().boxOf(0, 7).center();

    harness.controller->setScanning(true);
    harness.move(start);
    harness.settle();
    ASSERT_TRUE(harness.anchored);

    const QList<Sample> samples = harness.sweep(start, end, end.x() - start.x());
    report(QStringLiteral("in cache, 1 px/8 ms"), samples, harness);

    EXPECT_EQ(harness.frames.grabs, 1) << "the region was re-grabbed while the pointer stayed inside it";
    EXPECT_EQ(harness.backend->calls.load(), 1) << "the cached region was recognized more than once";
    EXPECT_GE(harness.anchorUpdates, samples.size() - 1) << "a pointer sample produced no placement";
    // Six characters were crossed over 192 samples, so the lookup runs on a small fraction of them.
    EXPECT_GT(harness.lookups, 1);
    EXPECT_LT(harness.lookups, samples.size() / 4) << "the lookup ran at the pointer rate rather than at the "
                                                      "character rate";
}

// The same six characters at 8 px per sample, which is 1000 px/s: an ordinary flick of the wrist.
// The pointer crosses a character every fourth sample or so, which separates the placement rate
// from the lookup rate.
TEST(TrackingLatencyTest, aFastPointerInsideTheCacheSkipsLookupsAndNotPlacements)
{
    configureScanning(200);
    setShortPage();
    Harness harness;
    const QPoint start = page().boxOf(0, 1).center();
    const QPoint end = page().boxOf(0, 7).center();

    harness.controller->setScanning(true);
    harness.move(start);
    harness.settle();
    ASSERT_TRUE(harness.anchored);

    const QList<Sample> samples = harness.sweep(start, end, (end.x() - start.x()) / 8);
    report(QStringLiteral("in cache, 8 px/8 ms"), samples, harness);

    EXPECT_EQ(harness.frames.grabs, 1);
    // The name of the case: at 8 px per sample the pointer crosses 6 characters in 25 samples, so
    // the lookup runs on fewer samples than there are samples while the placement runs on all of
    // them.
    EXPECT_LT(harness.lookups, samples.size()) << "the lookup ran once per pointer sample";
    EXPECT_GT(harness.lookups, 1);
    EXPECT_GE(harness.anchorUpdates, samples.size() - 1) << "a pointer sample produced no placement";
}

// The cost of both cached paths, measured from the same pointer sample. The pointer is stepped
// onto a character it was not on, and two wall times are taken: to the hitMoved() placement, which
// is emitted from the pointer handler itself, and to the lookupReady() placement, which adds a hit
// test and the round trip the lookup is dispatched over the global thread pool. The first is what
// the anchor rides on and the second is what the content rides on.
//
// The step alternates between two neighbouring characters rather than walking the line, because the
// line is only eight characters long and every step has to land on a character the previous step
// was not on: a step inside one character emits hitMoved() alone, which
// aPointerInsideOneCharacterMovesTheAnchorAndRunsOneLookup is the dedicated case for.
TEST(TrackingLatencyTest, theCachedPathCostsAPoolRoundTripAndTheAnchorPathCostsNone)
{
    configureScanning(200);
    setShortPage();
    Harness harness;

    harness.controller->setScanning(true);
    harness.move(page().boxOf(0, 1).center());
    harness.settle();
    ASSERT_TRUE(harness.anchored);

    QList<double> follows;
    QList<double> lookups;
    int missed = 0;
    for (int step = 0; step < 60; ++step) {
        const QPoint target = page().boxOf(0, 3 + (step % 2)).center();
        const int before = harness.lookups;
        harness.move(target);
        harness.spin(20);
        if (harness.lookups > before) {
            follows.append(static_cast<double>(harness.followLatencyNs) / 1e6);
            lookups.append(static_cast<double>(harness.lookupLatencyNs) / 1e6);
        } else {
            ++missed;
        }
    }
    for (const auto &[label, latencies] :
         {std::pair{"cached path move -> anchor", follows}, std::pair{"cached path move -> content", lookups}}) {
        const Stats stats = statsOf(latencies);
        std::printf("%-30s | %4lld of %d steps      | %6.3f %6.3f %6.3f %6.3f ms\n",
                    label,
                    static_cast<long long>(latencies.size()),
                    60,
                    stats.mean,
                    stats.p50,
                    stats.p90,
                    stats.max);
    }
    std::fflush(stdout);

    EXPECT_EQ(missed, 0) << "a step onto a different character produced no lookup";
    EXPECT_EQ(harness.frames.grabs, 1);
    EXPECT_EQ(harness.backend->calls.load(), 1);
    // The anchor path runs inside the pointer handler, so it has to fit inside one 8 ms pointer
    // interval with room for the rest of the loop. 1 ms is an eighth of that interval, against the
    // 0.001 ms this case reports at the median.
    EXPECT_LT(statsOf(follows).p50, 1.0) << "the anchor path costs more than an eighth of a pointer interval";
}

// A page the region cannot cover, swept at 4 px per sample (500 px/s) across and down. The
// throttle, the grab, the growth ladder and the recognition pass stand between the pointer and the
// content; the anchor rides hitMoved() past all four.
TEST(TrackingLatencyTest, aPointerAcrossAWholePageAnchorsAheadOfThePipeline)
{
    configureScanning(200);
    setFullPage();
    Harness harness;
    const QRect box = page().box();
    const QPoint start = box.topLeft() + QPoint{kCellPx / 2, kCellPx / 2};
    const QPoint end = boundedEnd(start, QPoint{box.right() - (kCellPx / 2), box.bottom() - (kCellPx / 2)});

    harness.controller->setScanning(true);
    harness.move(start);
    harness.settle(600);

    const QList<Sample> samples = harness.sweep(start, end, stepsFor(start, end));
    report(QStringLiteral("full page, 4 px/8 ms"), samples, harness);

    EXPECT_GT(harness.frames.grabs, 1) << "leaving the region never re-grabbed";
    // Allow one sample without a placement. The bound distinguishes an anchor updated
    // for each pointer sample from one updated only when a different character is hit.
    EXPECT_LE(longestFrozenSampleRun(samples), 2) << "the card held one position across a grab";
}

// The same sweep with both stage costs set to zero, which separates the part of the lag that is
// the cost of the pipeline from the part that is the shape of the control flow. A hold that
// survives here is a control-flow hold, since neither the grab nor the model costs anything.
TEST(TrackingLatencyTest, theAnchorHoldsForOnePointerSampleWithFreeStages)
{
    configureScanning(200);
    setFullPage();
    Harness harness;
    harness.frames.setDelayMs(0);
    harness.backend->delayMs = 0;
    const QRect box = page().box();
    const QPoint start = box.topLeft() + QPoint{kCellPx / 2, kCellPx / 2};
    const QPoint end = boundedEnd(start, QPoint{box.right() - (kCellPx / 2), box.bottom() - (kCellPx / 2)});

    harness.controller->setScanning(true);
    harness.move(start);
    harness.settle(600);

    const QList<Sample> samples = harness.sweep(start, end, stepsFor(start, end));
    report(QStringLiteral("free stages, 4 px/8 ms"), samples, harness);

    // Both charged and free stages must meet the same anchor bound: pointer updates
    // control placement independently of the capture and recognition stage costs.
    EXPECT_LE(longestFrozenSampleRun(samples), 2) << "a free pipeline still held the card";
    EXPECT_GT(harness.anchorUpdates, harness.lookups) << "every placement came from a lookup";
}

// The throttle at its configurable floor, which is what a user reaching for a faster popup would
// try. It buys grabs and recognition passes; the anchor rate is the pointer rate at either
// setting.
TEST(TrackingLatencyTest, theShortestThrottleBuysGrabsAndNotPlacements)
{
    configureScanning(50);
    setFullPage();
    Harness harness;
    const QRect box = page().box();
    const QPoint start = box.topLeft() + QPoint{kCellPx / 2, kCellPx / 2};
    const QPoint end = boundedEnd(start, QPoint{box.right() - (kCellPx / 2), box.bottom() - (kCellPx / 2)});

    harness.controller->setScanning(true);
    harness.move(start);
    harness.settle(600);

    const QList<Sample> samples = harness.sweep(start, end, stepsFor(start, end));
    report(QStringLiteral("50 ms throttle, 4 px/8 ms"), samples, harness);

    // The throttle at a quarter of its default buys compositor and model work: the grab count
    // rises and the hold stays inside the same two-sample bound the 200 ms sweep reports.
    EXPECT_LE(longestFrozenSampleRun(samples), 2) << "the 50 ms throttle held the card";
    EXPECT_GT(harness.frames.grabs, 1);
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    std::printf("workspace %dx%d at %d,%d; cell %d px, line spacing %d px, "
                "grab %d ms, recognition %d ms, pointer sample %d ms\n",
                workspace().width(),
                workspace().height(),
                workspace().x(),
                workspace().y(),
                kCellPx,
                kLineSpacingPx,
                kGrabMs,
                kRecognizeMs,
                kSampleMs);
    std::printf("lag columns are the distance in logical pixels from the pointer to the point the "
                "card was last placed at;\n\"froze\" is the longest run in ms with the card shown, the pointer "
                "moving and the anchor unchanged;\n\"anch\" counts placements of either kind and \"look\" counts "
                "the lookupReady() subset of them;\n\"stall\" is the largest gap in ms between two fires of a "
                "1 ms timer on the GUI thread.\n");
    std::fflush(stdout);
    return RUN_ALL_TESTS();
}
