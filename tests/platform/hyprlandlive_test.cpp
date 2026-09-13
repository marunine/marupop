// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The four wlroots-family backends against a real Hyprland.
//
// Every case is gated on MARUPOP_LIVE_HYPRLAND=1, which tests/harness/hyprland-session.sh
// exports inside the nested session it starts. Under the plain registration the whole suite
// skips, so it costs nothing on a host with no Hyprland.
//
// This is the only coverage capture::WlrFrameSource, cursor::WlrLockWatcher and maru::WlrShortcuts
// have of their protocol paths: each of the three binds a Wayland global no other compositor in
// the test matrix advertises, and no in-process fake compositor exists for them. Two parts of those paths stay
// uncovered even here, and each is stated at the case that comes closest: the lock notifier's locked/unlocked
// transition, which needs an ext-session-lock client this harness does not start, and a global shortcut press, which
// needs a key event no test can inject into the compositor.
#include "app/wlrshortcuts.h"
#include "capture/framesource.h"
#include "capture/hyprlandconfig.h"
#include "capture/scanregion.h"
#include "capture/wlrframesource.h"
#include "cursor/hyprcursortracker.h"
#include "cursor/wlrlockwatcher.h"
#include "eventloop.h"
#include "platform/backend.h"
#include "platform/session.h"
#include "popup/popuppreview.h"
#include "popup/popupwindow.h"
#include "wayland/registry.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QSignalSpy>

#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>

using namespace maru;

namespace
{

bool liveHyprland()
{
    return qgetenv("MARUPOP_LIVE_HYPRLAND") == "1";
}

// The session runs with ecosystem:enforce_permissions on and no rule granting this binary, which
// tests/harness/hyprland-session.sh --enforce-only produces. Every screencopy permission stays
// pending there, so the capture cases below have nothing to assert and the watchdog case is the
// only one that runs.
bool pendingPermission()
{
    return qgetenv("MARUPOP_HYPRLAND_PENDING_PERMISSION") == "1";
}

// How long a case waits for a copy, and how long WlrFrameSource waits before it abandons one.
// Both allow multiple refresh intervals for a copy to complete.
// What is under test here is that a copy works at all; how long it takes is asserted separately
// by measuresTheGrabAndPointerCosts(). The watchdog default of 1000 ms is an interactive choice
// and not a bound this suite should inherit, and the nested session renders on the caller's own
// compositor, so a busy or slow host moves the figure by a lot.
constexpr int kCopyWaitMs = 20000;
constexpr int kCopyTimeoutMs = 15000;

class HyprlandLiveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!liveHyprland()) {
            GTEST_SKIP() << "MARUPOP_LIVE_HYPRLAND is not 1; run this suite through "
                            "tests/harness/hyprland-session.sh";
        }
        if (wl::Registry::instance() == nullptr) {
            GTEST_SKIP() << "no Wayland registry; the platform plugin is "
                         << QGuiApplication::platformName().toStdString();
        }
        if (pendingPermission()) {
            GTEST_SKIP() << "the session grants no screencopy permission; only "
                            "HyprlandPendingPermissionTest runs there";
        }
    }
};

// The reverse gate: this fixture runs only in the session the one above skips in.
class HyprlandPendingPermissionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!liveHyprland() || !pendingPermission()) {
            GTEST_SKIP() << "run this case through tests/harness/hyprland-session.sh --enforce-only";
        }
        if (wl::Registry::instance() == nullptr) {
            GTEST_SKIP() << "no Wayland registry; the platform plugin is "
                         << QGuiApplication::platformName().toStdString();
        }
    }
};

} // namespace

TEST_F(HyprlandLiveTest, detectsAHyprlandSession)
{
    EXPECT_EQ(platform::redetect(), platform::Session::Hyprland);
}

// The generated configuration is accepted in full. Hyprland reports a rejected line to
// `hyprctl configerrors` and to its on-screen error overlay, and to neither its log at the
// default verbosity nor its exit status, so a harness writing a line the compositor throws away
// looks exactly like one writing a line it applies.
//
// That is not hypothetical: the layer rule was rejected for the whole life of this harness. Every
// other case here kept passing, because a rejected line leaves the rest of the configuration in
// force -- the outputs came up, the binds registered, the copies worked. This case is the one
// that fails on a mistake anywhere in the file rather than only in the part a later case reads.
TEST_F(HyprlandLiveTest, acceptsEveryLineOfTheGeneratedConfiguration)
{
    QProcess hyprctl;
    hyprctl.start(QStringLiteral("hyprctl"), {QStringLiteral("configerrors")});
    ASSERT_TRUE(hyprctl.waitForFinished(5000)) << "hyprctl did not answer";
    const QString errors = QString::fromUtf8(hyprctl.readAllStandardOutput()).trimmed();
    EXPECT_TRUE(errors.isEmpty()) << "the compositor rejected part of tests/harness/hyprland-session.sh's "
                                     "configuration:\n"
                                  << errors.toStdString();
}

TEST_F(HyprlandLiveTest, advertisesTheFourGlobalsTheBackendsBind)
{
    wl::Registry *registry = wl::Registry::instance();
    for (const QByteArray &name : {QByteArrayLiteral("zwlr_screencopy_manager_v1"),
                                   QByteArrayLiteral("hyprland_lock_notifier_v1"),
                                   QByteArrayLiteral("hyprland_global_shortcuts_manager_v1"),
                                   QByteArrayLiteral("zwlr_layer_shell_v1")}) {
        EXPECT_TRUE(registry->has(name)) << name.constData() << " is absent; the registry holds "
                                         << registry->interfaces().join(' ').constData();
    }
}

TEST_F(HyprlandLiveTest, readsThePointerPositionFromSocketOne)
{
    cursor::HyprCursorTracker tracker;
    ASSERT_FALSE(tracker.socketPath().isEmpty()) << "HYPRLAND_INSTANCE_SIGNATURE is unset inside the session";
    tracker.setTrackingIntervalMs(4);
    QSignalSpy positions{&tracker, &cursor::CursorTracker::positionChanged};
    tracker.setTracking(true);
    tracker.start();

    ASSERT_TRUE(maru::test::waitFor(
        [&tracker] {
            return tracker.sampleCount() > 0;
        },
        5000))
        << "the socket answered no cursorpos in 5 s";
    EXPECT_TRUE(tracker.isAvailable());

    // The position is in the same space as QScreen::geometry(): cursorpos answers
    // Pointer::mgr()->untransformedPosition() and xdg_output.logical_position is the same
    // CMonitor::m_position those coordinates are relative to.
    if (!positions.isEmpty()) {
        const QPoint point = positions.constLast().at(0).toPoint();
        EXPECT_NE(QGuiApplication::screenAt(point), nullptr)
            << "the pointer at " << point.x() << ',' << point.y() << " is on no QScreen";
    }
}

TEST_F(HyprlandLiveTest, copiesARegionOfAnOutput)
{
    capture::WlrFrameSource frames;
    ASSERT_TRUE(frames.isAvailable()) << frames.unavailableReason().toStdString();
    frames.setRequestTimeoutMs(kCopyTimeoutMs);

    const QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    // One read of the geometry, used for the request and for the assertion. Qt updates a
    // QScreen asynchronously from xdg_output, so reading it twice can straddle a change and
    // report a tile outside the output it was snapped against.
    const QRect outputGeometry = screen->geometry();
    const QRect requested{outputGeometry.left() + 100, outputGeometry.top() + 100, 400, 200};
    const QRect tile = frames.quantize(requested);
    EXPECT_TRUE(outputGeometry.contains(tile));

    QSignalSpy ready{&frames, &capture::FrameSource::frameReady};
    QSignalSpy failed{&frames, &capture::FrameSource::failed};
    frames.grab(tile);

    ASSERT_TRUE(maru::test::waitFor(
        [&ready, &failed] {
            return !ready.isEmpty() || !failed.isEmpty();
        },
        kCopyWaitMs))
        << "the compositor answered neither ready nor failed";
    ASSERT_TRUE(failed.isEmpty()) << failed.constFirst().at(0).toString().toStdString();

    const auto frame = ready.constFirst().at(0).value<capture::Frame>();
    EXPECT_EQ(frame.logicalRect, tile);
    // The buffer is the region size times the output scale, which is the native-resolution
    // behaviour the KWin path asks for explicitly.
    EXPECT_EQ(frame.image.width(), qRound(tile.width() * screen->devicePixelRatio()));
    EXPECT_EQ(frame.image.height(), qRound(tile.height() * screen->devicePixelRatio()));
    EXPECT_NE(frame.hash, 0U);
    EXPECT_TRUE(frames.capturesOwnWindows());
}

TEST_F(HyprlandLiveTest, answersTheSameHashForAnUnchangedRegion)
{
    capture::WlrFrameSource frames;
    ASSERT_TRUE(frames.isAvailable()) << frames.unavailableReason().toStdString();
    const QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    frames.setRequestTimeoutMs(kCopyTimeoutMs);
    const QRect tile = frames.quantize({screen->geometry().left() + 200, screen->geometry().top() + 200, 320, 240});

    // Four grabs spread over about 450 ms rather than two back to back: the property the scan
    // cache rests on is that the hash of a region holding still is stable over the time a reader
    // spends hovering it, and two adjacent grabs inside one refresh interval would not show that.
    constexpr int kGrabs = 4;
    constexpr int kGapMs = 150;
    QSignalSpy ready{&frames, &capture::FrameSource::frameReady};
    for (int attempt = 1; attempt <= kGrabs; ++attempt) {
        frames.grab(tile);
        ASSERT_TRUE(maru::test::waitFor(
            [&ready, attempt] {
                return ready.size() >= attempt;
            },
            kCopyWaitMs))
            << "grab " << attempt << " of " << kGrabs << " was never answered";
        maru::test::pumpFor(kGapMs);
    }

    // The generated configuration disables the animated wallpaper. Repeated grabs of
    // an unchanged region must have identical hashes for ScanCache reuse to be reliable.
    const auto first = ready.constFirst().at(0).value<capture::Frame>();
    for (int i = 1; i < ready.size(); ++i) {
        const auto later = ready.at(i).at(0).value<capture::Frame>();
        EXPECT_EQ(first.hash, later.hash) << "grab " << i << " of a region holding still hashed differently";
        EXPECT_EQ(first.image, later.image) << "grab " << i << " differs in its pixels, not only in its hash";
    }
}

TEST_F(HyprlandLiveTest, copiesARegionOfTheSecondOutput)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        GTEST_SKIP() << "this session has one output; run the registration that asks for two";
    }
    // The second output has a non-zero logical origin, which is the one thing a single-output
    // session cannot exercise: capture_output_region takes its box in the output's own logical
    // coordinates, so WlrFrameSource has to subtract the output origin, and wlrTileFor() has to
    // snap to a lattice anchored at that origin rather than at the workspace origin.
    QScreen *second = screens.at(1);
    ASSERT_NE(second->geometry().topLeft(), QPoint(0, 0)) << "the second output sits at the workspace origin";

    capture::WlrFrameSource frames;
    ASSERT_TRUE(frames.isAvailable()) << frames.unavailableReason().toStdString();
    frames.setRequestTimeoutMs(kCopyTimeoutMs);
    const QRect requested{second->geometry().left() + 300, second->geometry().top() + 200, 400, 200};
    const QRect tile = frames.quantize(requested);
    EXPECT_TRUE(second->geometry().contains(tile))
        << "the tile left the output it was snapped against: " << tile.x() << ',' << tile.y();

    QSignalSpy ready{&frames, &capture::FrameSource::frameReady};
    QSignalSpy failed{&frames, &capture::FrameSource::failed};
    frames.grab(tile);
    ASSERT_TRUE(maru::test::waitFor(
        [&ready, &failed] {
            return !ready.isEmpty() || !failed.isEmpty();
        },
        kCopyWaitMs));
    ASSERT_TRUE(failed.isEmpty()) << failed.constFirst().at(0).toString().toStdString();

    const auto frame = ready.constFirst().at(0).value<capture::Frame>();
    EXPECT_EQ(frame.logicalRect, tile);
    EXPECT_EQ(frame.image.width(), qRound(tile.width() * second->devicePixelRatio()));
    EXPECT_EQ(frame.image.height(), qRound(tile.height() * second->devicePixelRatio()));

    // capture::imageToLogical() maps the frame's own pixels back to the desktop, and a wrong
    // origin would put them on the first output.
    const QPoint mapped = capture::imageToLogical(frame, QPoint{0, 0});
    EXPECT_EQ(mapped, tile.topLeft());
    EXPECT_EQ(QGuiApplication::screenAt(mapped), second);
}

TEST_F(HyprlandLiveTest, measuresTheGrabAndPointerCosts)
{
    // Measure grab and pointer-query latency. Print the distributions rather than
    // asserting tight timing bounds that would depend on the host and its current load.
    capture::WlrFrameSource frames;
    ASSERT_TRUE(frames.isAvailable()) << frames.unavailableReason().toStdString();
    const QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    frames.setRequestTimeoutMs(kCopyTimeoutMs);
    const QRect tile = frames.quantize({screen->geometry().left() + 100, screen->geometry().top() + 100, 400, 200});

    constexpr int kSamples = 40;
    QList<qint64> grabCosts;
    QSignalSpy ready{&frames, &capture::FrameSource::frameReady};
    for (int i = 1; i <= kSamples; ++i) {
        frames.grab(tile);
        ASSERT_TRUE(maru::test::waitFor(
            [&ready, i] {
                return ready.size() >= i;
            },
            kCopyWaitMs));
        grabCosts.append(ready.constLast().at(0).value<capture::Frame>().grabMs);
    }
    std::ranges::sort(grabCosts);

    cursor::HyprCursorTracker tracker;
    tracker.setTrackingIntervalMs(1);
    tracker.setTracking(true);
    tracker.start();
    QElapsedTimer pointerClock;
    pointerClock.start();
    ASSERT_TRUE(maru::test::waitFor(
        [&tracker] {
            return tracker.sampleCount() >= kSamples;
        },
        10000))
        << "the socket answered " << tracker.sampleCount() << " of " << kSamples << " cursorpos requests";
    const double perSampleMs = static_cast<double>(pointerClock.elapsed()) / tracker.sampleCount();

    std::printf("hyprland grab %dx%d: p50 %lld ms, p90 %lld ms, max %lld ms over %d grabs\n",
                tile.width(),
                tile.height(),
                static_cast<long long>(grabCosts.at(grabCosts.size() / 2)),
                static_cast<long long>(grabCosts.at(grabCosts.size() * 9 / 10)),
                static_cast<long long>(grabCosts.constLast()),
                kSamples);
    std::printf("hyprland cursorpos: %.3f ms per sample over %llu samples at a 1 ms poll\n",
                perSampleMs,
                static_cast<unsigned long long>(tracker.sampleCount()));
    std::fflush(stdout);

    // The one property worth asserting: a grab answers at all, and inside the periodic poll's own
    // 500 ms interval, so the scan loop cannot fall behind its slowest trigger.
    // The periodic poll's own interval. The registrations run serially, so this is a claim about
    // the compositor rather than about the load the suite puts on it: a grab slower than the
    // slowest trigger in the scan loop would mean the loop can never keep up.
    EXPECT_LT(grabCosts.constLast(), 500)
        << "the slowest of " << kSamples << " grabs took " << grabCosts.constLast() << " ms";
}

TEST_F(HyprlandLiveTest, bindsLockNotifyAndReportsAnUnlockedSession)
{
    // What this covers is the bind and the query: hyprland_lock_notifier_v1 is advertised,
    // get_lock_notification is accepted, and the roundtrip query() makes returns with the session
    // reported unlocked.
    //
    // What it does not cover is the locked/unlocked transition. Driving that needs an
    // ext-session-lock client inside the nested session, which this harness starts none of, so
    // the event path is exercised only by cursor::ScreenSaverLockWatcher's fake on the KDE side.
    // The two implementations share the LockWatcher contract but not a line of transport code.
    cursor::WlrLockWatcher watcher;
    ASSERT_TRUE(watcher.isAvailable()) << "hyprland_lock_notifier_v1 was not bound";
    EXPECT_TRUE(cursor::WlrLockWatcher::available());
    watcher.query();
    EXPECT_FALSE(watcher.isLocked());

    // A second query is safe, and the connection survived both. A protocol error on the
    // notification object would take the whole wl_display down, which would show up here as a
    // registry that no longer answers rather than as a wrong lock state.
    watcher.query();
    EXPECT_FALSE(watcher.isLocked());
    ASSERT_NE(wl::Registry::instance(), nullptr);
    EXPECT_TRUE(wl::Registry::instance()->has(QByteArrayLiteral("hyprland_lock_notifier_v1")));
}

TEST_F(HyprlandLiveTest, registersTheThreeGlobalShortcuts)
{
    WlrShortcuts shortcuts;
    ASSERT_TRUE(shortcuts.isAvailable()) << shortcuts.unavailableReason().toStdString();
    EXPECT_FALSE(shortcuts.editableShortcuts());
    shortcuts.registerActions();
    for (const QString &id : ShortcutRegistry::actionIds()) {
        EXPECT_TRUE(shortcuts.isRegistered(id)) << id.toStdString();
    }
    // registerActions() ends in a roundtrip, so a protocol error -- an already_taken id, a
    // malformed app_id -- would have killed the display by now rather than failing quietly.
    ASSERT_NE(wl::Registry::instance(), nullptr);
    EXPECT_TRUE(wl::Registry::instance()->has(QByteArrayLiteral("hyprland_global_shortcuts_manager_v1")));

    // A second registerActions() is what applySettings() does on every settings change. The
    // compositor raises already_taken for a duplicate app_id and id pair, and that error is
    // fatal to the connection, so the guard against re-registering is load-bearing.
    shortcuts.registerActions();
    ASSERT_NE(wl::Registry::instance(), nullptr);
    EXPECT_TRUE(wl::Registry::instance()->has(QByteArrayLiteral("hyprland_global_shortcuts_manager_v1")))
        << "a second registerActions() took the Wayland connection down";
}

// The layer rule that keeps MaruPop's own card out of the pixels it reads, end to end: a real
// layer surface carrying the namespace PopupWindow assigns, a real zwlr_screencopy_v1 grab of the
// region it sits in, and the black rectangle the compositor is supposed to composite in its
// place.
//
// This case exists because the rule was rejected by the compositor for the whole life of the
// harness and nothing noticed. `layerrule = noscreenshare, marupop-popup` is answered by
// Hyprland 0.56.2 with "invalid field noscreenshare: missing a value": the effect is spelled
// no_screen_share and the keyword takes `key value` fields, so the line named a field that does
// not exist. The error reaches `hyprctl configerrors` and the on-screen error overlay and no log
// at the default verbosity, and every other case in this suite passed throughout -- none of them
// maps a surface, so none of them could tell. The check WlrFrameSource makes for itself is not
// coverage either: it reports the rule as *missing*, so it is silent exactly when the rule is
// broken in the way that matters.
TEST_F(HyprlandLiveTest, paintsThePopupBlackWhereAGrabOverlapsIt)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect geometry = screen->geometry();

    popup::PopupWindow window;
    window.applyTheme();
    window.setModel(popup::samplePopupModel());
    window.showNear(geometry.topLeft() + QPoint{300, 300}, screen);

    ASSERT_TRUE(maru::test::waitFor(
        [&window] {
            return !window.occlusionRect().isEmpty();
        },
        kCopyWaitMs))
        << "the card never mapped a layer surface";
    const QRect card = window.occlusionRect();

    capture::WlrFrameSource frames;
    ASSERT_TRUE(frames.isAvailable()) << frames.unavailableReason().toStdString();
    frames.setRequestTimeoutMs(kCopyTimeoutMs);
    frames.setOcclusionProvider([&window] {
        return window.occlusionRect();
    });

    // A tile that holds the card, so the assertion below reads pixels the compositor had to
    // decide about rather than an empty intersection.
    const QRect tile = frames.quantize(card);
    // WlrFrameSource records the occlusion clipped to the region it copies, so this -- not the
    // whole card -- is what a frame carries and what the pixels below are read from.
    const QRect overlap = card.intersected(tile);
    ASSERT_FALSE(overlap.isEmpty()) << "the quantized tile misses the card entirely";

    QSignalSpy ready{&frames, &capture::FrameSource::frameReady};
    QSignalSpy failed{&frames, &capture::FrameSource::failed};
    QSignalSpy missing{&frames, &capture::WlrFrameSource::noScreenShareMissing};

    // The card is mapped, but a copy is answered from an output commit and the surface reaches
    // the screen on one of its own, so the first grab can predate the card's first composite.
    // Each attempt gets its own share of the budget rather than the whole of it: one slow copy on
    // a loaded host would otherwise consume the entire wait and fail the case for contention.
    // The loop ends as soon as a frame comes back all black, so the cost on a working rule is one
    // grab, and on a broken one it is kAttempts grabs of a few milliseconds each.
    constexpr int kAttempts = 5;
    capture::Frame frame;
    int nonBlack = -1;
    QPoint firstNonBlack;
    int probePixels = 0;
    for (int attempt = 0; attempt < kAttempts && nonBlack != 0; ++attempt) {
        ready.clear();
        failed.clear();
        frames.grab(tile);
        ASSERT_TRUE(maru::test::waitFor(
            [&ready, &failed] {
                return !ready.isEmpty() || !failed.isEmpty();
            },
            kCopyWaitMs / kAttempts))
            << "the compositor answered neither ready nor failed for grab " << attempt;
        ASSERT_TRUE(failed.isEmpty()) << failed.constFirst().at(0).toString().toStdString();

        frame = ready.constFirst().at(0).value<capture::Frame>();
        // The occlusion the source recorded is the card clipped to the region it copied. A frame
        // carrying anything else means the card moved mid-grab, which nothing here does.
        ASSERT_EQ(frame.occluded, overlap) << "the frame did not record the card as its occlusion";
        ASSERT_FALSE(frame.image.isNull());

        const QRect probe = capture::logicalToImage(frame, overlap).intersected(frame.image.rect());
        ASSERT_GE(probe.width(), 4);
        ASSERT_GE(probe.height(), 4);
        probePixels = probe.width() * probe.height();

        // Every pixel, not a sample: the rule paints the whole surface, so one surviving pixel of
        // the card is a rule that did not apply. The tolerance is the production check's own
        // capture::kNoScreenShareBlack rather than exact black -- a copy can carry a colour
        // management transform, and a case demanding 0 would fail on such a host and blame the
        // layer rule for it.
        nonBlack = 0;
        for (int y = probe.top(); y <= probe.bottom(); ++y) {
            for (int x = probe.left(); x <= probe.right(); ++x) {
                const QColor colour = frame.image.pixelColor(x, y);
                if (colour.red() > capture::kNoScreenShareBlack || colour.green() > capture::kNoScreenShareBlack ||
                    colour.blue() > capture::kNoScreenShareBlack) {
                    if (nonBlack == 0) {
                        firstNonBlack = QPoint{x, y};
                    }
                    ++nonBlack;
                }
            }
        }
    }

    EXPECT_EQ(nonBlack, 0) << nonBlack << " of " << probePixels << " pixels under the card were not black in "
                           << kAttempts << " grabs, the first at " << firstNonBlack.x() << "," << firstNonBlack.y()
                           << "; the no_screen_share layer rule did not apply. Check "
                              "`hyprctl configerrors`.";
    EXPECT_TRUE(missing.isEmpty()) << missing.constFirst().at(0).toString().toStdString();

    window.hidePopup();
}

TEST_F(HyprlandLiveTest, buildsTheHyprlandBackends)
{
    platform::Backend backend{platform::Session::Hyprland};
    EXPECT_TRUE(backend.capturesOwnWindows());
    const platform::CaptureReport report = backend.captureReport();
    EXPECT_TRUE(report.authorized) << report.remedy.toStdString();
    // The compositor advertises the shortcut protocol inside this session, so the settings page
    // takes its read-only branch.
    EXPECT_FALSE(backend.shortcuts()->editableShortcuts());
}

TEST_F(HyprlandPendingPermissionTest, reportsAnUnansweredCopyRatherThanWaitingForever)
{
    // An unmatched permission is pending, so CScreenshareFrame::copy() can return
    // without ready or failed. The watchdog must finish the request so scanning can
    // continue instead of remaining stuck with one request in flight.
    capture::WlrFrameSource frames;
    ASSERT_TRUE(frames.isAvailable()) << frames.unavailableReason().toStdString();
    frames.setRequestTimeoutMs(400);

    const QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect tile = frames.quantize({screen->geometry().left() + 100, screen->geometry().top() + 100, 320, 240});

    QSignalSpy ready{&frames, &capture::FrameSource::frameReady};
    QSignalSpy failed{&frames, &capture::FrameSource::failed};
    frames.grab(tile);

    ASSERT_TRUE(maru::test::waitFor(
        [&failed] {
            return !failed.isEmpty();
        },
        5000))
        << "the copy was neither answered nor abandoned; the watchdog did not fire";
    EXPECT_TRUE(ready.isEmpty()) << "the compositor answered a copy it had no permission for";
    // The message names the configuration line, because a pending permission and a refused one
    // are indistinguishable from the client side. The line is written in whichever of Hyprland's
    // two configuration languages this session's own configuration is in, so the expectation is
    // built the same way rather than spelled out: a literal would pass in one language and fail
    // in the other for a message that is right in both.
    const capture::HyprlandConfig config = capture::hyprlandConfig();
    const QString expected =
        capture::screencopyPermissionRule(config.language, QCoreApplication::applicationFilePath());
    EXPECT_TRUE(failed.constFirst().at(0).toString().contains(expected))
        << failed.constFirst().at(0).toString().toStdString() << "\nshould have named: " << expected.toStdString();

    // The source is usable again: the watchdog abandoned the frame rather than leaving a request
    // in flight forever, so the next grab is issued and times out in its turn.
    frames.grab(tile);
    EXPECT_TRUE(maru::test::waitFor(
        [&failed] {
            return failed.size() >= 2;
        },
        5000))
        << "the source accepted no further grab after the first timed out";
}

int main(int argc, char **argv)
{
    // QApplication rather than QGuiApplication: paintsThePopupBlackWhereAGrabOverlapsIt maps a
    // real popup::PopupWindow, which is a QWidget.
    QApplication app{argc, argv};
    // QSignalSpy refuses to record an argument of a type the meta-object system does not know,
    // and cursor::CursorTracker::positionChanged carries a QScreen*.
    qRegisterMetaType<QScreen *>("QScreen*");
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
