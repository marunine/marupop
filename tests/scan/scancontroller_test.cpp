// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The scan loop end to end, over a fake pointer source, capture::FakeFrameSource, a fake
// recognition backend and a canned lookup. Nothing here talks to KWin, to a recognition model
// or to a dictionary; what is under test is the state machine between them.
//
// The geometry is derived from the region the controller is expected to grab rather than
// hard-coded: capture::initialRect() centres the region on the pointer, so the pointer always
// lands at the same place inside the frame and the fake backend puts its character boxes
// around that point.
#include "capture/framesource.h"
#include "capture/scanregion.h"
#include "core/settings.h"
#include "cursor/cursortracker.h"
#include "cursor/lockwatcher.h"
#include "fakes.h"
#include "ocr/backend.h"
#include "ocr/ocrservice.h"
#include "scan/scancontroller.h"

#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QSemaphore>

#include <atomic>
#include <functional>
#include <gtest/gtest.h>
#include <memory>

using namespace maru;
using namespace maru::scan;

namespace
{

const QSize kInitialSize{128, 64};
const QSize kMaxSize{256, 128};
// A HiDPI grab: the frame is twice the logical region, so every mapping the controller does
// has to divide by it.
constexpr qreal kScale = 2.0;
const QSize kCharSize{24, 32};
const QString kText = QStringLiteral("日本語のテキスト");
// The character the pointer is put over: 'テ', the fifth of the eight.
constexpr int kHitIndex = 4;
constexpr int kHighlightLength = 3;

void pump(const std::function<bool()> &done, int timeoutMs = 5000)
{
    const QDeadlineTimer deadline{timeoutMs};
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

void settle(int ms = 150)
{
    pump(
        [] {
            return false;
        },
        ms);
}

QRect workspace()
{
    return capture::workspaceRect();
}

QRect initialRectAt(QPoint cursor)
{
    return capture::initialRect(cursor, kInitialSize, workspace());
}

// The pointer in frame pixels, which is where the fake backend centres its text.
QPoint cursorInFrame(QPoint cursor, QRect rect)
{
    return QPoint{qRound((cursor.x() - rect.x()) * kScale), qRound((cursor.y() - rect.y()) * kScale)};
}

// The top left of the first character box, chosen so the pointer falls in the middle of
// character kHitIndex.
QPoint originFor(QPoint cursor)
{
    const QPoint point = cursorInFrame(cursor, initialRectAt(cursor));
    return QPoint{point.x() - (kHitIndex * kCharSize.width()) - (kCharSize.width() / 2),
                  point.y() - (kCharSize.height() / 2)};
}

QImage frameImage(QColor color)
{
    QImage image{QSize{qRound(kInitialSize.width() * kScale), qRound(kInitialSize.height() * kScale)},
                 QImage::Format_RGB888};
    image.fill(color);
    return image;
}

void configureScanning(bool progressive, bool poll, int pollMs = 200, int throttleMs = 50)
{
    PopSettings::setTriggerOnCursorMove(true);
    PopSettings::setCursorMoveThrottleMs(throttleMs);
    PopSettings::setPeriodicPollEnabled(poll);
    PopSettings::setPeriodicPollIntervalMs(pollMs);
    PopSettings::setProgressiveScanArea(progressive);
    PopSettings::setPauseWhileLocked(true);
    PopSettings::setInitialScanWidth(kInitialSize.width());
    PopSettings::setInitialScanHeight(kInitialSize.height());
    PopSettings::setMaxScanWidth(kMaxSize.width());
    PopSettings::setMaxScanHeight(kMaxSize.height());
}

// FakeTracker, FakeLock and FakeBackend share the implementations in tests/support/fakes.h.
using maru::test::FakeBackend;
using maru::test::FakeLock;
using maru::test::FakeTracker;

// The canned dictionary answer: three characters from the pointer, whatever they are.
lookup::Response cannedResponse(const lookup::Request &request)
{
    lookup::Result result;
    result.matchedText = request.sourceText.mid(request.cursorIndex, kHighlightLength);
    result.primarySpelling = result.matchedText;

    lookup::Response response;
    response.highlightStart = request.cursorIndex;
    response.highlightLength = kHighlightLength;
    response.results.append(result);
    return response;
}

struct Harness
{
    Harness()
    {
        auto owned = std::make_unique<FakeBackend>();
        owned->text = kText;
        owned->charSize = kCharSize;
        backend = owned.get();
        ocrService.setBackend(std::move(owned));

        controller = std::make_unique<ScanController>(tracker, frames, ocrService, [this](const lookup::Request &r) {
            ++lookups;
            requests.append(r);
            return cannedResponse(r);
        });

        QObject::connect(controller.get(),
                         &ScanController::lookupReady,
                         controller.get(),
                         [this](const lookup::Response &response, const HitContext &context) {
                             responses.append(response);
                             contexts.append(context);
                         });
        QObject::connect(
            controller.get(), &ScanController::hitMoved, controller.get(), [this](QPoint point, QScreen *) {
                follows.append(point);
            });
        QObject::connect(
            controller.get(), &ScanController::nothingUnderCursor, controller.get(), [this](QPoint cursor) {
                misses.append(cursor);
            });
        QObject::connect(controller.get(), &ScanController::statusChanged, controller.get(), [this](const QString &s) {
            statuses.append(s);
        });
        QObject::connect(controller.get(), &ScanController::error, controller.get(), [this](const QString &message) {
            errors.append(message);
        });
    }

    // The pointer over the middle of the workspace, with the fake text laid out around it.
    void aimAt(QPoint cursor, QColor color = Qt::white)
    {
        backend->origin = originFor(cursor);
        frames.setImage(frameImage(color), kScale);
    }

    FakeTracker tracker;
    capture::FakeFrameSource frames;
    ocr::OcrService ocrService;
    FakeBackend *backend = nullptr;
    std::unique_ptr<ScanController> controller;

    QList<lookup::Request> requests;
    QList<lookup::Response> responses;
    QList<HitContext> contexts;
    // The pointer positions hitMoved() carried, in emission order.
    QList<QPoint> follows;
    QList<QPoint> misses;
    QList<QString> statuses;
    QList<QString> errors;
    std::atomic<int> lookups{0};
};

QPoint centreOfWorkspace()
{
    return workspace().center();
}

} // namespace

TEST(ScanControllerTest, movePickingTheCharacterUnderThePointerRunsTheWholePipeline)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    const QRect rect = initialRectAt(cursor);
    const QPoint origin = originFor(cursor);
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);

    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);
    ASSERT_EQ(harness.frames.requestedRects().size(), 1);
    EXPECT_EQ(harness.frames.requestedRects().first(), rect);
    EXPECT_EQ(harness.backend->calls.load(), 1);
    EXPECT_TRUE(harness.misses.isEmpty());

    // The lookup ran over the whole paragraph, from the character under the pointer.
    ASSERT_EQ(harness.requests.size(), 1);
    EXPECT_EQ(harness.requests.first().sourceText, kText);
    EXPECT_EQ(harness.requests.first().cursorIndex, kHitIndex);
    EXPECT_EQ(harness.requests.first().maxSearchLength, PopSettings::maxLookupLength());
    EXPECT_EQ(harness.requests.first().maxResults, PopSettings::maxResults());

    const HitContext &context = harness.contexts.first();
    EXPECT_EQ(context.cursorLogical, cursor);
    EXPECT_EQ(context.screen, QGuiApplication::primaryScreen());
    EXPECT_EQ(context.cursorIndex, kHitIndex);
    EXPECT_EQ(context.paragraphText, kText);
    EXPECT_FALSE(context.vertical);
    EXPECT_EQ(context.backendName, QStringLiteral("fake"));
    EXPECT_EQ(harness.responses.first().results.first().matchedText, QStringLiteral("テキス"));

    // The highlight of three characters, mapped out of frame pixels through the frame scale.
    const QRect span{origin.x() + (kHitIndex * kCharSize.width()),
                     origin.y(),
                     kHighlightLength * kCharSize.width(),
                     kCharSize.height()};
    const QRect expectedMatch{rect.topLeft() + QPoint{qRound(span.x() / kScale), qRound(span.y() / kScale)},
                              QSize{qRound(span.width() / kScale), qRound(span.height() / kScale)}};
    EXPECT_EQ(context.matchedRectLogical, expectedMatch);

    const QRect paragraph{
        origin.x(), origin.y(), static_cast<int>(kText.size()) * kCharSize.width(), kCharSize.height()};
    const QRect expectedParagraph{rect.topLeft() +
                                      QPoint{qRound(paragraph.x() / kScale), qRound(paragraph.y() / kScale)},
                                  QSize{qRound(paragraph.width() / kScale), qRound(paragraph.height() / kScale)}};
    EXPECT_EQ(context.paragraphRectLogical, expectedParagraph);
}

TEST(ScanControllerTest, aSecondMoveInsideTheSameFrameIsAPureHitTest)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);
    const int grabs = harness.frames.requestedRects().size();
    const int recognitions = harness.backend->calls.load();

    // Two characters to the right, inside the same region: 24 frame pixels each at scale 2.
    harness.tracker.move(cursor + QPoint{2 * kCharSize.width() / static_cast<int>(kScale), 0});
    pump([&harness] {
        return harness.responses.size() >= 2;
    });

    ASSERT_EQ(harness.responses.size(), 2);
    EXPECT_EQ(harness.contexts.at(1).cursorIndex, kHitIndex + 2);
    EXPECT_EQ(harness.frames.requestedRects().size(), grabs);
    EXPECT_EQ(harness.backend->calls.load(), recognitions);
}

// The anchor path: a pointer sample that resolves to the character the previous sample resolved to
// carries its position through hitMoved() and dispatches no second lookup. 4 logical pixels is a
// third of the 12 logical pixel character cell (24 frame pixels at scale 2), so all four samples
// land on the same character.
TEST(ScanControllerTest, aMoveInsideOneCharacterCarriesThePositionAndRunsNoSecondLookup)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);
    ASSERT_TRUE(harness.follows.isEmpty()) << "a position was carried before the first hit landed";
    const int grabs = harness.frames.requestedRects().size();

    for (int step = 1; step <= 4; ++step) {
        harness.tracker.move(cursor + QPoint{step, 0});
        settle(20);
    }

    EXPECT_EQ(harness.follows.size(), 4);
    EXPECT_EQ(harness.follows.constLast(), (cursor + QPoint{4, 0}));
    EXPECT_EQ(harness.responses.size(), 1) << "a move inside one character produced a second response";
    EXPECT_EQ(harness.lookups.load(), 1) << "a move inside one character dispatched a second lookup";
    EXPECT_EQ(harness.frames.requestedRects().size(), grabs);
}

// The lookup window's count. Opening the window raises the minimum from 0 above MaxResults, and
// raising LookupWindowMaxResults raises it again; each raise answers the hit on screen again at
// once, from the cached paragraph, with the pointer at rest and PeriodicPollEnabled off. A minimum
// lowered or below MaxResults changes nothing.
TEST(ScanControllerTest, aRaisedMinimumResultCountAnswersTheHitOnScreenAgainAtOnce)
{
    configureScanning(true, false);
    PopSettings::setMaxResults(10);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setMinimumResults(4);
    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.requests.size(), 1);
    EXPECT_EQ(harness.requests.first().maxResults, 10);
    const int grabs = harness.frames.requestedRects().size();
    const int recognitions = harness.backend->calls.load();

    // The window opens.
    harness.controller->setMinimumResults(20);
    pump([&harness] {
        return harness.responses.size() >= 2;
    });
    ASSERT_EQ(harness.requests.size(), 2);
    EXPECT_EQ(harness.requests.at(1).maxResults, 20);
    EXPECT_EQ(harness.requests.at(1).sourceText, harness.requests.first().sourceText);
    EXPECT_EQ(harness.requests.at(1).cursorIndex, harness.requests.first().cursorIndex);
    EXPECT_EQ(harness.contexts.at(1).cursorLogical, cursor);

    // LookupWindowMaxResults rises while the window is open.
    harness.controller->setMinimumResults(30);
    pump([&harness] {
        return harness.responses.size() >= 3;
    });
    ASSERT_EQ(harness.requests.size(), 3);
    EXPECT_EQ(harness.requests.at(2).maxResults, 30);
    EXPECT_EQ(harness.frames.requestedRects().size(), grabs);
    EXPECT_EQ(harness.backend->calls.load(), recognitions);

    // Lowering it again leaves the response in hand, which already carries more results.
    harness.controller->setMinimumResults(0);
    harness.tracker.move(cursor + QPoint{2, 0});
    settle(100);
    EXPECT_EQ(harness.requests.size(), 3);
}

// A raise with no hit on screen dispatches nothing: the pointer left the text, and an answer for
// the character it left would bring the card back.
TEST(ScanControllerTest, aRaisedMinimumResultCountLooksNothingUpWithNoHitOnScreen)
{
    configureScanning(false, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);
    PopSettings::setTriggerOnCursorMove(false);
    harness.controller->applySettings();

    harness.tracker.move(cursor + QPoint{0, 24});
    pump([&harness] {
        return !harness.misses.isEmpty();
    });
    ASSERT_EQ(harness.misses.size(), 1);

    harness.controller->setMinimumResults(30);
    settle(200);
    EXPECT_EQ(harness.requests.size(), 1);
    EXPECT_EQ(harness.responses.size(), 1);
}

// A MaxResults raised past the lookup window's minimum is a raise as well. Application re-reads the
// settings into the controller before it passes the minimum again, so the comparison has to be
// made against the count the response in hand was asked for.
TEST(ScanControllerTest, aRaisedMaxResultsLooksTheSameCharacterUpAgainPastTheMinimum)
{
    configureScanning(true, false);
    PopSettings::setMaxResults(10);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setMinimumResults(20);
    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.requests.size(), 1);
    EXPECT_EQ(harness.requests.first().maxResults, 20);

    PopSettings::setMaxResults(25);
    harness.controller->applySettings();
    harness.controller->setMinimumResults(20);
    pump([&harness] {
        return harness.responses.size() >= 2;
    });
    ASSERT_EQ(harness.requests.size(), 2);
    EXPECT_EQ(harness.requests.at(1).maxResults, 25);
    PopSettings::setMaxResults(10);
}

// The sample that leaves the recognized text hides the card rather than moving it onto a point
// holding no character, and hitMoved() stops with it: a hidden card has no position to carry.
//
// The text is one line 32 frame pixels tall centred on the pointer, so 24 logical pixels down is
// 48 frame pixels below its centre and clear of the 6 frame pixel hit tolerance, while staying
// inside the 128x64 logical region the first grab took. TriggerOnCursorMove goes off after the
// first hit so no re-grab re-aims the fake text under the pointer.
TEST(ScanControllerTest, aMoveThatLeavesTheTextCarriesNoPositionAndHidesTheCard)
{
    configureScanning(false, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);
    PopSettings::setTriggerOnCursorMove(false);
    harness.controller->applySettings();
    const int followsAtHit = harness.follows.size();

    harness.tracker.move(cursor + QPoint{0, 24});
    pump([&harness] {
        return !harness.misses.isEmpty();
    });
    ASSERT_EQ(harness.misses.size(), 1);
    EXPECT_EQ(harness.follows.size(), followsAtHit) << "the sample that left the text placed the card first";

    harness.tracker.move(cursor + QPoint{1, 24});
    settle(200);
    EXPECT_EQ(harness.follows.size(), followsAtHit);
}

// The card follows the pointer out of the cached region because a re-grab is on its way. With
// TriggerOnCursorMove and PeriodicPollEnabled both off no re-grab ever runs, so the card stops
// following rather than carrying an answer for a character the pointer left behind.
TEST(ScanControllerTest, carriesNoPositionOutsideTheCachedRegionWhileNothingRescans)
{
    configureScanning(false, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);

    // Inside the region, which is 128x64 logical around the first grab: the card follows.
    harness.tracker.move(cursor + QPoint{2, 0});
    settle(50);
    const int followsInside = harness.follows.size();
    EXPECT_GT(followsInside, 0);

    PopSettings::setTriggerOnCursorMove(false);
    harness.controller->applySettings();
    // 200 logical pixels to the right, which is past the 64 pixel half-width of the region.
    harness.tracker.move(cursor + QPoint{200, 0});
    settle(200);

    EXPECT_EQ(harness.follows.size(), followsInside) << "the card followed a pointer nothing will re-evaluate";
    EXPECT_EQ(harness.frames.requestedRects().size(), 1) << "a grab ran with both scan triggers off";
}

TEST(ScanControllerTest, reportsNothingUnderTheCursorOnceOverARegionWithoutText)
{
    configureScanning(false, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    harness.backend->empty = true;

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.misses.isEmpty();
    });

    ASSERT_EQ(harness.misses.size(), 1);
    EXPECT_EQ(harness.misses.first(), cursor);
    EXPECT_TRUE(harness.responses.isEmpty());

    // Still nothing under it: the popup is already hidden, so nothing is reported again.
    harness.tracker.move(cursor + QPoint{4, 0});
    settle(400);
    EXPECT_EQ(harness.misses.size(), 1);
}

TEST(ScanControllerTest, growsTheRegionWhenTheFirstRungFindsNothing)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    harness.backend->empty = true;

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return harness.frames.requestedRects().size() >= 2;
    });

    const QRect first = initialRectAt(cursor);
    const QRect grown = capture::grow(first, cursor, kMaxSize, workspace()).value_or(QRect{});
    ASSERT_FALSE(grown.isEmpty());
    ASSERT_GE(harness.frames.requestedRects().size(), 2);
    EXPECT_EQ(harness.frames.requestedRects().at(0), first);
    EXPECT_EQ(harness.frames.requestedRects().at(1), grown);

    // The ladder ends at the configured maximum, and the miss is reported there.
    pump([&harness] {
        return !harness.misses.isEmpty();
    });
    EXPECT_EQ(harness.misses.size(), 1);
    EXPECT_EQ(harness.frames.requestedRects().size(), 2);
}

TEST(ScanControllerTest, thePeriodicPollSkipsRecognitionWhileTheFrameHashIsUnchanged)
{
    configureScanning(false, true);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.backend->calls.load(), 1);

    // The poll re-grabs the same region; the hash is the one already recognized, so the
    // recognition pass is skipped and the popup is left alone.
    pump([&harness] {
        return harness.frames.requestedRects().size() >= 3;
    });
    EXPECT_GE(harness.frames.requestedRects().size(), 3);
    EXPECT_EQ(harness.frames.requestedRects().at(1), harness.frames.requestedRects().at(0));
    EXPECT_EQ(harness.backend->calls.load(), 1);
    EXPECT_EQ(harness.responses.size(), 1);

    // Different pixels under the same region: a new hash, so recognition runs again.
    harness.frames.setImage(frameImage(Qt::black), kScale);
    pump([&harness] {
        return harness.backend->calls.load() >= 2;
    });
    EXPECT_EQ(harness.backend->calls.load(), 2);
    pump([&harness] {
        return harness.responses.size() >= 2;
    });
    EXPECT_EQ(harness.responses.size(), 2);
}

TEST(ScanControllerTest, grabsNothingWhileScanningIsOff)
{
    configureScanning(true, true);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    ASSERT_FALSE(harness.controller->isScanning());
    harness.tracker.move(cursor);
    settle(300);
    EXPECT_TRUE(harness.frames.requestedRects().isEmpty());
    EXPECT_EQ(harness.backend->calls.load(), 0);

    // Turning it on and off again reports the state and hides whatever was on screen.
    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);

    const int grabs = harness.frames.requestedRects().size();
    harness.controller->setScanning(false);
    EXPECT_FALSE(harness.controller->isScanning());
    EXPECT_EQ(harness.misses.size(), 1);
    harness.tracker.move(cursor + QPoint{40, 0});
    settle(300);
    EXPECT_EQ(harness.frames.requestedRects().size(), grabs);
}

TEST(ScanControllerTest, pausesWhileTheSessionIsLockedAndResumesAfter)
{
    configureScanning(true, true);
    Harness harness;
    FakeLock lock;
    harness.controller->setLockWatcher(&lock);
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    lock.setLockedState(true);
    EXPECT_EQ(harness.controller->statusText(), QStringLiteral("Paused (screen locked)"));

    harness.tracker.move(cursor);
    settle(300);
    EXPECT_TRUE(harness.frames.requestedRects().isEmpty());

    lock.setLockedState(false);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    EXPECT_EQ(harness.responses.size(), 1);
    EXPECT_EQ(harness.controller->statusText(), QStringLiteral("Scanning · fake"));
}

TEST(ScanControllerTest, dropsTheRecognitionResultOfAnOvertakenScan)
{
    configureScanning(false, false);
    Harness harness;
    const QPoint first = centreOfWorkspace();
    const QPoint second = first + QPoint{200, 0};
    // Both regions are centred on the pointer, so one origin puts the text under both.
    ASSERT_EQ(originFor(first), originFor(second));
    harness.aimAt(first);
    harness.backend->blocking = true;

    harness.controller->setScanning(true);
    harness.tracker.move(first);
    pump([&harness] {
        return harness.backend->entered.available() > 0;
    });
    ASSERT_TRUE(harness.backend->entered.tryAcquire(1));

    // The pointer leaves for a region the first recognition knows nothing about while that
    // recognition is still running.
    harness.tracker.move(second);
    pump([&harness] {
        return harness.frames.requestedRects().size() >= 2;
    });
    ASSERT_EQ(harness.frames.requestedRects().size(), 2);
    EXPECT_EQ(harness.frames.requestedRects().at(1), initialRectAt(second));

    // Release the first recognition: its result belongs to a region the pointer has left.
    harness.backend->released.release(1);
    pump([&harness] {
        return harness.backend->entered.available() > 0;
    });
    ASSERT_TRUE(harness.backend->entered.tryAcquire(1));
    harness.backend->released.release(1);

    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    settle(200);
    ASSERT_EQ(harness.responses.size(), 1);
    EXPECT_EQ(harness.contexts.first().cursorLogical, second);
    EXPECT_EQ(harness.lookups.load(), 1);
}

TEST(ScanControllerTest, reportsTheBackendAndTheScanningStateAsStatus)
{
    configureScanning(true, false);
    Harness harness;
    EXPECT_EQ(harness.controller->statusText(), QStringLiteral("Paused"));

    harness.controller->setScanning(true);
    EXPECT_EQ(harness.controller->statusText(), QStringLiteral("Scanning · fake"));
    EXPECT_TRUE(harness.statuses.contains(QStringLiteral("Scanning · fake")));

    harness.controller->setScanning(false);
    EXPECT_EQ(harness.controller->statusText(), QStringLiteral("Paused"));
}

TEST(ScanControllerTest, reportsAFailedGrabOnce)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    harness.frames.setFailure(QStringLiteral("no compositor"));

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.errors.isEmpty();
    });
    ASSERT_EQ(harness.errors.size(), 1);
    EXPECT_EQ(harness.errors.first(), QStringLiteral("no compositor"));

    harness.tracker.move(cursor + QPoint{40, 0});
    settle(300);
    EXPECT_EQ(harness.errors.size(), 1);
}

int main(int argc, char **argv)
{
    // QGuiApplication: the controller reads QScreen geometry through capture::workspaceRect(),
    // and every reply in the loop arrives through the event loop.
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// The two properties a pixel source that composites MaruPop's own windows into a grab adds:
// the requested rect is snapped to the source's tile grid, and a character inside the popup's
// rectangle is refused.

TEST(ScanControllerTest, grabsTheRectTheSourceQuantizedTo)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    // The tile grid of the output the pointer is on, which is what WlrFrameSource applies.
    const QRect output = QGuiApplication::primaryScreen()->geometry();
    harness.frames.setQuantizeOutput(output);
    const QRect expected = capture::wlrTileFor(initialRectAt(cursor), output);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);

    pump([&harness] {
        return !harness.frames.issuedRects().isEmpty();
    });
    ASSERT_FALSE(harness.frames.requestedRects().isEmpty());
    EXPECT_EQ(harness.frames.requestedRects().constFirst(), expected)
        << "the controller asked for a rect the source would not have grabbed";
}

TEST(ScanControllerTest, dropsAReplyForARectTheSourceDidNotQuantizeTo)
{
    // The identity quantizer is what every KDE grab uses, and the case above changes it. This
    // one states the invariant the two share: the rect the controller records is the rect it
    // compares a reply against, so no reply is ever dropped for a quantization it applied.
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    harness.frames.setQuantizeOutput(QGuiApplication::primaryScreen()->geometry());

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);

    pump([&harness] {
        return harness.backend->calls.load() > 0;
    });
    settle();
    // Every rect the controller asked for was issued and recognized. A quantization the
    // controller did not apply would leave the reply's rect unequal to m_pendingRect, and
    // onFrameReady() would drop the frame before the recognition pass.
    EXPECT_EQ(harness.frames.issuedRects(), harness.frames.requestedRects());
    EXPECT_GT(harness.backend->calls.load(), 0) << "every reply was dropped as a rect nothing was waiting for";
}

TEST(ScanControllerTest, refusesACharacterUnderTheOwnPopup)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    // A card covering the whole scan region, which is what a source that composites its own
    // windows reports while the popup sits over the text.
    harness.frames.setOcclusion(initialRectAt(cursor));

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);

    pump([&harness] {
        return !harness.misses.isEmpty();
    });
    EXPECT_TRUE(harness.responses.isEmpty()) << "the card's own rendering reached a lookup";
    ASSERT_FALSE(harness.misses.isEmpty());
    EXPECT_EQ(harness.misses.constFirst(), cursor);
}

TEST(ScanControllerTest, answersACharacterOutsideTheOwnPopup)
{
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    // The card sits well away from the pointer, which is where placePopupAvoiding() puts it.
    harness.frames.setOcclusion(QRect{cursor + QPoint{400, 400}, QSize{300, 200}});

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);

    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);
    EXPECT_EQ(harness.contexts.constFirst().cursorIndex, kHitIndex);
}

TEST(ScanControllerTest, stopsTheLadderWhereQuantizationCapsTheGrowth)
{
    // capture::grow() bounds the ladder against the workspace, the union of every output, while
    // a source that quantizes clamps to the one output the region is on. Where the output is
    // smaller than MaxScanWidth by MaxScanHeight, the ladder asks for a larger rect and the
    // source keeps answering the same tile. Comparing the rect that will be grabbed against the
    // current one is what ends the ladder; comparing the rect the ladder asked for loops
    // forever on the same tile, and never reaches nothingUnderCursor().
    configureScanning(true, false);
    Harness harness;
    // An output smaller than kMaxSize in both dimensions, so every rung above it quantizes back
    // to the same tile.
    const QRect output{0, 0, 200, 100};
    const QPoint cursor = output.center();
    harness.frames.setQuantizeOutput(output);
    // The pointer over blank pixels: the fake backend puts its text far away, so the hit test
    // finds nothing and every rung asks to grow.
    harness.backend->origin = QPoint{10000, 10000};
    harness.frames.setImage(frameImage(Qt::white), kScale);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);

    pump([&harness] {
        return !harness.misses.isEmpty();
    });
    settle(300);
    EXPECT_FALSE(harness.misses.isEmpty()) << "the ladder never reported that nothing was under the pointer";
    // A bound rather than an exact count: the ladder legitimately climbs a few rungs before the
    // quantization caps it. An unbounded loop produces hundreds inside the settle above.
    EXPECT_LT(harness.frames.issuedRects().size(), 10)
        << "the growth ladder issued " << harness.frames.issuedRects().size() << " grabs";
}

TEST(ScanControllerTest, keepsTheAnswerWhileTheOwnPopupCoversTheCharacter)
{
    // popup::placePopupAvoiding() puts the card off the paragraph wherever a free band exists.
    // Where none does the card sits over the text, and the next grab reports that rectangle as
    // Frame::occluded. Reporting no hit there would hide the card, which uncovers the text,
    // which resolves the character again, which shows the card over it again: a flicker at the
    // scan rate. A card already on screen keeps its answer instead.
    configureScanning(true, true, 80);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);

    // The card is now over the text, which is what the source reports from here on.
    harness.frames.setOcclusion(initialRectAt(cursor));
    const int missesBefore = harness.misses.size();
    settle(400);
    EXPECT_EQ(harness.misses.size(), missesBefore) << "the card was taken away by the occlusion it is the cause of";
}

TEST(ScanControllerTest, keepsTheAnswerWhereTheLayerRuleLeavesNothingToRecognize)
{
    // The same rule as the case above, on the configuration the setup actually asks for. With
    // the `no_screen_share` layer rule in place the compositor composites black where
    // the card is, so the recognition pass finds no character there at all -- there is no
    // character box to test against Frame::occluded, and the sample takes the ordinary no-hit
    // path. That path hides the card, which uncovers the text, which resolves the character
    // again, which shows the card over it again: the flicker the occlusion rule exists to stop,
    // reached by the branch the rule was not written in. The pointer is what is tested for
    // occlusion, so the answer does not depend on anything being recognized.
    //
    // Progressive scanning is off so the growth ladder is not between the miss and the assertion.
    configureScanning(false, true, 80);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    // Assigned before the first scan reads it, as every FakeBackend field is.
    harness.backend->dropsBoxesOnBlackPixels = true;

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.responses.isEmpty();
    });
    ASSERT_EQ(harness.responses.size(), 1);

    // The card is now over the text. FakeFrameSource paints Frame::occluded black into the image
    // it answers with, as the compositor does, and the backend above drops the boxes it covers.
    harness.frames.setOcclusion(initialRectAt(cursor));
    const int missesBefore = harness.misses.size();
    settle(400);
    EXPECT_EQ(harness.misses.size(), missesBefore) << "the card was taken away by the occlusion it is the cause of";
}

TEST(ScanControllerTest, reportsNothingWhereTheOwnPopupCoversTheOnlyCharacterAndNoCardIsUp)
{
    // The other half of the rule: with no card on screen there is no answer to keep, so an
    // occluded character is reported as nothing rather than looked up.
    configureScanning(true, false);
    Harness harness;
    const QPoint cursor = centreOfWorkspace();
    harness.aimAt(cursor);
    harness.frames.setOcclusion(initialRectAt(cursor));

    harness.controller->setScanning(true);
    harness.tracker.move(cursor);
    pump([&harness] {
        return !harness.misses.isEmpty();
    });
    EXPECT_TRUE(harness.responses.isEmpty()) << "the card's own rendering reached a lookup";
    EXPECT_FALSE(harness.misses.isEmpty());
}
