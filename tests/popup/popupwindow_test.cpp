// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// PopupWindow and PopupPreview. Under the offscreen platform plugin the layer-shell path is
// inactive and the card is a frameless tool window positioned with QWidget::move(), which is
// what every case below covers. tests/popup/CMakeLists.txt registers the same binary a second
// time inside the nested compositor, where promotesTheCardToAnOverlayLayerSurfaceInAWaylandSession
// runs instead of skipping, and a third time on two virtual outputs, which is where the three
// monitor-boundary cases run.
#include "core/settings.h"
#include "popup/entrymodel.h"
#include "popup/placement.h"
#include "popup/popuppreview.h"
#include "popup/popupview.h"
#include "popup/popupwindow.h"
#include "popup/theme.h"

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QPixmap>
#include <QScreen>
#include <QScrollBar>
#include <QTest>
#include <QWindow>

#include <LayerShellQt/Window>
#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>

using namespace maru;
using namespace maru::popup;

namespace
{

void resetSettings()
{
    PopSettings::self()->setDefaults();
    PopSettings::self()->save();
}

PopupModel longModel()
{
    PopupModel model;
    for (int index = 0; index < 12; ++index) {
        Entry entry;
        entry.headword = QStringLiteral("見出し語%1").arg(index);
        entry.readings = QStringList{QStringLiteral("みだしご")};
        entry.pitchPositions = QList<std::optional<quint8>>{std::optional<quint8>{2}};
        Sense sense;
        sense.glosses =
            QStringList{QStringLiteral("a definition long enough to wrap several times inside the maximum card width, "
                                       "repeated so the document overflows the maximum card height as well")};
        entry.senses.append(sense);
        model.entries.append(entry);
    }
    return model;
}

// The two screens of a session with two or more outputs, ordered left to right, so that "the
// screen the pointer left" and "the screen it crossed onto" name the same outputs the report
// does. A pair of nullptr reports a session with one output, which is every registration of this
// binary except popupwindow_test_dual_nested.
std::pair<QScreen *, QScreen *> leftAndRightScreens()
{
    QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        return {nullptr, nullptr};
    }
    std::ranges::sort(screens, [](const QScreen *first, const QScreen *second) {
        return first->geometry().x() < second->geometry().x();
    });
    return {screens.constFirst(), screens.at(1)};
}

// Why the three monitor-boundary cases skip on a session with one output.
const char *const singleScreenSkip = "the session has one screen, so there is no boundary to cross; run this binary "
                                     "through tests/harness/nested-session.sh --outputs 2";

// The name of the output the card's surface is on, or "<none>" for a card whose surface the
// compositor has sent no wl_surface.enter for. A string rather than a QScreen pointer, because
// gtest builds the message beside an assertion only where that assertion is false, which is the
// state a null screen is most likely in.
std::string surfaceScreenName(const QWidget &widget)
{
    const QWindow *handle = widget.windowHandle();
    if (handle == nullptr || handle->screen() == nullptr) {
        return "<none>";
    }
    return handle->screen()->name().toStdString();
}

// The desktop position the compositor draws widget's card at. The layer-shell margins are measured
// from the corner of the output the surface is bound to, so the placement is honoured only where
// that output is the one the placement was computed against. -1,-1 reports a card with no surface.
QPoint drawnTopLeft(const QWidget &widget)
{
    QWindow *handle = widget.windowHandle();
    if (handle == nullptr || handle->screen() == nullptr) {
        return QPoint{-1, -1};
    }
    const LayerShellQt::Window *layer = LayerShellQt::Window::get(handle);
    if (layer == nullptr) {
        return QPoint{-1, -1};
    }
    return handle->screen()->geometry().topLeft() + QPoint{layer->margins().left(), layer->margins().top()};
}

// A point 4 logical pixels inside the edge of screen that faces the other output, at half its
// height, which is where the text in the report is.
QPoint nearInnerEdge(const QScreen *screen, bool leftHandScreen)
{
    const QRect geometry = screen->geometry();
    const int x = leftHandScreen ? geometry.right() - 4 : geometry.x() + 4;
    return QPoint{x, geometry.y() + (geometry.height() / 2)};
}

} // namespace

TEST(PopupWindowTest, sizesTheCardToTheContentWithinTheBounds)
{
    resetSettings();
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());

    const Theme theme = themeFromSettings();
    EXPECT_GT(window.width(), 0);
    EXPECT_GT(window.height(), 0);
    EXPECT_LE(window.width(), theme.maxWidth);
    EXPECT_LE(window.height(), theme.maxHeight);
}

TEST(PopupWindowTest, clampsAnOverflowingModelToTheMaximumCardSize)
{
    resetSettings();
    PopSettings::setPopupMaxWidth(300);
    PopSettings::setPopupMaxHeight(200);
    PopupWindow window;
    window.applyTheme();
    window.setModel(longModel());

    // The height is bound by the maximum; the width settles on the widest wrapped line, which
    // is at most the maximum.
    EXPECT_LE(window.width(), 300);
    EXPECT_GT(window.width(), 200);
    EXPECT_EQ(window.height(), 200);
    resetSettings();
}

TEST(PopupWindowTest, placesTheCardWhereTheModeSaysAndMovesOnTheNextCall)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());

    QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect geometry = screen->geometry();
    const QPoint first = geometry.topLeft() + QPoint{40, 40};

    window.showNear(first, screen);
    EXPECT_TRUE(window.isVisible());
    const QRect expected =
        placePopup(first, window.size(), geometry, settings::popupPositionMode(), themeFromSettings().cursorOffset);
    EXPECT_EQ(window.popupRect(), expected);

    // A cursor move with the same model repositions while followCursor is set.
    const QPoint second = geometry.center();
    window.setFollowCursor(true);
    window.showNear(second, screen);
    EXPECT_NE(window.popupRect().topLeft(), expected.topLeft());

    // With followCursor off, a cursor move that produced no new model leaves the card in
    // place.
    const QRect held = window.popupRect();
    window.setFollowCursor(false);
    window.showNear(geometry.topLeft() + QPoint{10, 10}, screen);
    EXPECT_EQ(window.popupRect(), held);

    // A new model moves it again.
    window.setModel(samplePopupModel());
    window.showNear(geometry.topLeft() + QPoint{10, 10}, screen);
    EXPECT_NE(window.popupRect(), held);

    window.hidePopup();
    EXPECT_FALSE(window.isVisible());
    resetSettings();
}

TEST(PopupWindowTest, hidesRatherThanShowsAnEmptyModel)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(PopupModel{});
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    EXPECT_FALSE(window.isVisible());
    resetSettings();
}

// A pointer reading along a line at the half of the screen wavers across it by a pixel or two. The
// card keeps the side it took until the pointer passes the half by popup::kSideHysteresisPx, and a
// card shown afresh places without that history.
TEST(PopupWindowTest, keepsTheCardOnOneSideOfAPointerWaveringAcrossTheHalf)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    ASSERT_EQ(settings::popupPositionMode(), PopupPositionMode::VisualNovel);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());

    QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect geometry = screen->geometry();
    if (geometry.height() < 3 * (window.height() + themeFromSettings().cursorOffset)) {
        GTEST_SKIP() << "the screen is too short for the card to clear the pointer on either side";
    }
    const int half = geometry.y() + (geometry.height() / 2);
    const int x = geometry.center().x();
    const auto below = [&window](QPoint cursor) {
        return window.popupRect().top() > cursor.y();
    };
    const auto above = [&window](QPoint cursor) {
        return window.popupRect().bottom() < cursor.y();
    };

    const QPoint upper{x, half - 2};
    const QPoint lower{x, half + 2};
    window.showNear(upper, screen);
    EXPECT_TRUE(below(upper));
    window.showNear(lower, screen);
    EXPECT_TRUE(below(lower)) << "a two pixel waver across the half sent the card across the pointer";

    const QPoint past{x, half + kSideHysteresisPx + 2};
    window.showNear(past, screen);
    EXPECT_TRUE(above(past));
    window.showNear(upper, screen);
    EXPECT_TRUE(above(upper));

    window.hidePopup();
    ASSERT_FALSE(window.isVisible());
    window.showNear(upper, screen);
    EXPECT_TRUE(below(upper)) << "a card shown afresh kept the side of the one before it";
    resetSettings();
}

// The lookup window's suppression: the card on screen is hidden, and neither a new model nor a
// new position brings it back until the suppression is released.
TEST(PopupWindowTest, aSuppressedCardHidesAndDropsEveryModelAndPosition)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(longModel());
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    ASSERT_TRUE(window.isVisible());

    window.setSuppressed(true);
    EXPECT_FALSE(window.isVisible());
    PopupModel other;
    Entry entry;
    entry.headword = QStringLiteral("別");
    other.entries.append(entry);
    window.setModel(other);
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    EXPECT_FALSE(window.isVisible());
    EXPECT_FALSE(window.plainText().contains(QStringLiteral("別")));

    window.setSuppressed(false);
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    EXPECT_TRUE(window.isVisible());
    resetSettings();
}

// A pinned card survives the suppression: hiding it would end isFrozen(), and the first result
// after the release would replace the content the user pinned.
TEST(PopupWindowTest, aPinnedCardKeepsItsContentThroughASuppression)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    window.setPinned(true);
    ASSERT_TRUE(window.isFrozen());
    const QString pinned = window.plainText();

    window.setSuppressed(true);
    EXPECT_TRUE(window.isVisible());
    window.setSuppressed(false);
    ASSERT_TRUE(window.isFrozen());

    window.setModel(longModel());
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    EXPECT_EQ(window.plainText(), pinned);
    resetSettings();
}

TEST(PopupWindowTest, pinningTogglesTheInputTransparency)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());

    EXPECT_FALSE(window.isPinned());
    EXPECT_TRUE(window.windowFlags().testFlag(Qt::WindowTransparentForInput));
    EXPECT_TRUE(window.testAttribute(Qt::WA_TransparentForMouseEvents));

    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    const QRect before = window.popupRect();

    window.setPinned(true);
    EXPECT_TRUE(window.isPinned());
    EXPECT_FALSE(window.windowFlags().testFlag(Qt::WindowTransparentForInput));
    EXPECT_FALSE(window.testAttribute(Qt::WA_TransparentForMouseEvents));
    // The surface is recreated at the rectangle it was shown at.
    EXPECT_TRUE(window.isVisible());
    EXPECT_EQ(window.popupRect().topLeft(), before.topLeft());

    window.setPinned(false);
    EXPECT_FALSE(window.isPinned());
    EXPECT_TRUE(window.windowFlags().testFlag(Qt::WindowTransparentForInput));
    EXPECT_TRUE(window.isVisible());
    resetSettings();
}

// This runs under offscreen and both nested registrations. Focus must come from the
// compositor on Wayland, rather than only setting a Qt focus policy.
TEST(PopupWindowTest, pinningFocusesResultsForKeyboardUseAndEscapeReleasesFocus)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    QWidget reader;
    reader.resize(200, 100);
    reader.show();
    reader.activateWindow();
    ASSERT_TRUE(QTest::qWaitForWindowActive(&reader));
    PopupWindow window;
    window.applyTheme();
    window.setModel(longModel());
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    auto *view = window.findChild<PopupView *>();
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(window.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
    EXPECT_EQ(view->focusPolicy(), Qt::NoFocus);

    EXPECT_TRUE(reader.isActiveWindow());
    window.setPinned(true);
    ASSERT_TRUE(QTest::qWaitForWindowActive(&window));
    EXPECT_TRUE(view->hasFocus());
    EXPECT_FALSE(window.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
    if (QGuiApplication::platformName().startsWith(QLatin1StringView{"wayland"})) {
        EXPECT_EQ(LayerShellQt::Window::get(window.windowHandle())->keyboardInteractivity(),
                  LayerShellQt::Window::KeyboardInteractivityOnDemand);
    }
    ASSERT_GT(view->verticalScrollBar()->maximum(), 0);
    QTest::keyClick(view, Qt::Key_PageDown);
    EXPECT_GT(view->verticalScrollBar()->value(), 0);
    QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
    ASSERT_TRUE(view->textCursor().hasSelection());
    QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(QGuiApplication::clipboard()->text(), view->toPlainText());
    QTest::keyClick(view, Qt::Key_Escape);
    EXPECT_FALSE(window.isPinned());
    EXPECT_TRUE(QTest::qWaitForWindowActive(&reader));
    EXPECT_TRUE(window.isVisible());
    EXPECT_TRUE(window.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
    EXPECT_EQ(view->focusPolicy(), Qt::NoFocus);
    if (QGuiApplication::platformName().startsWith(QLatin1StringView{"wayland"})) {
        EXPECT_EQ(LayerShellQt::Window::get(window.windowHandle())->keyboardInteractivity(),
                  LayerShellQt::Window::KeyboardInteractivityNone);
    }
    resetSettings();
}

TEST(PopupWindowTest, showsAndHidesAtOnceWhileTheFadeIsDisabled)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());

    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    EXPECT_TRUE(window.isVisible());
    EXPECT_DOUBLE_EQ(window.fadeOpacity(), 1.0);

    window.hidePopup();
    EXPECT_FALSE(window.isVisible());
    resetSettings();
}

TEST(PopupWindowTest, startsTheFadeFromZeroWhileItIsEnabled)
{
    resetSettings();
    PopSettings::setPopupFadeMs(120);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());

    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    EXPECT_TRUE(window.isVisible());
    // The animation runs on the event loop, so the value read here is the start value.
    EXPECT_LT(window.fadeOpacity(), 1.0);

    // The hide is deferred until the animation ends.
    window.hidePopup();
    EXPECT_TRUE(window.isVisible());
    QTest::qWait(300);
    EXPECT_FALSE(window.isVisible());
    resetSettings();
}

// A hit that lands inside PopupFadeMs of a hide reverses the fade. Without that, showNear()
// renders the new entry onto a card whose animation is still on its way to zero, and the card
// hides with the fresh content on it.
TEST(PopupWindowTest, reversesAHideFadeForAResultThatArrivesInsideIt)
{
    resetSettings();
    PopSettings::setPopupFadeMs(120);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());
    window.showNear(QPoint{100, 100}, QGuiApplication::primaryScreen());
    QTest::qWait(200);
    ASSERT_TRUE(window.isVisible());

    window.hidePopup();
    QTest::qWait(40);
    ASSERT_TRUE(window.isVisible()) << "the fade finished before the case could interrupt it";
    window.setModel(longModel());
    window.showNear(QPoint{300, 300}, QGuiApplication::primaryScreen());

    QTest::qWait(300);
    EXPECT_TRUE(window.isVisible()) << "the card hid with the new entry on it";
    EXPECT_DOUBLE_EQ(window.fadeOpacity(), 1.0);
    resetSettings();
}

// A pinned card is the one the user is reading and scrolling, so a hit under a pointer that kept
// moving replaces neither its content nor its position.
TEST(PopupWindowTest, holdsTheContentAndThePositionOfAPinnedCard)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.applyRenderOptions();
    window.setModel(samplePopupModel());
    window.showNear(QPoint{200, 200}, QGuiApplication::primaryScreen());
    ASSERT_TRUE(window.isVisible());

    window.setPinned(true);
    const QRect pinnedRect = window.popupRect();
    const QString pinnedText = window.plainText();

    window.setModel(longModel());
    window.showNear(QPoint{600, 500}, QGuiApplication::primaryScreen());

    EXPECT_EQ(window.popupRect(), pinnedRect);
    EXPECT_EQ(window.plainText(), pinnedText);

    // Unpinning releases both, and hidePopup() is honoured throughout so the scanning toggle can
    // still take a pinned card away.
    window.setPinned(false);
    window.setModel(longModel());
    window.showNear(QPoint{600, 500}, QGuiApplication::primaryScreen());
    EXPECT_NE(window.popupRect(), pinnedRect);
    resetSettings();
}

TEST(PopupWindowTest, carriesThePlainTextOfTheModelForTheClipboard)
{
    resetSettings();
    PopupWindow window;
    window.applyRenderOptions();
    window.setModel(samplePopupModel());
    EXPECT_TRUE(window.plainText().contains(QStringLiteral("読む")));
}

TEST(PopupPreviewTest, rendersTheSampleModel)
{
    resetSettings();
    PopupPreview preview;
    preview.resize(preview.sizeHint());
    preview.applyTheme();
    preview.applyRenderOptions();

    QPixmap canvas{preview.size()};
    canvas.fill(Qt::transparent);
    preview.render(&canvas);
    EXPECT_FALSE(canvas.isNull());
    EXPECT_GT(preview.sizeHint().width(), 0);
}

TEST(PopupPreviewTest, followsAThemeChange)
{
    resetSettings();
    PopupPreview preview;
    preview.applyTheme();
    const QSize before = preview.sizeHint();

    PopSettings::setPopupMaxWidth(250);
    preview.applyTheme();
    EXPECT_NE(preview.sizeHint(), before);
    resetSettings();
}

TEST(PopupPreviewTest, carriesTheSampleModelTheProbeAlsoRenders)
{
    const PopupModel sample = samplePopupModel();
    EXPECT_EQ(sample.entries.size(), 2);
    EXPECT_TRUE(sample.kanji.has_value());
    EXPECT_EQ(sample.entries.at(1).readings.size(), sample.entries.at(1).pitchPositions.size());
}

// The layer-shell branch, which the offscreen platform plugin never takes. The nested
// registration of this suite (tests/popup/CMakeLists.txt, popupwindow_test_nested) is what runs
// this case; under the offscreen plugin it skips, and the rest of the suite covers the fallback
// geometry path instead.
//
// The assertion reads the four properties PopupWindow::configureSurface() assigns and compares
// each to the value LayerShellQt leaves on a Window nobody configured. Asserting that
// LayerShellQt::Window::get() answers non-null would assert nothing: get() constructs a Window
// for any QWindow that has one, and returns it under QT_QPA_PLATFORM=offscreen as well,
// with layer-shell-qt 6.7.4. The defaults it hands back there are
// LayerTop, scope "window", exclusion zone 0, all four anchors and keyboard interactivity
// OnDemand, none of which is what configureSurface() sets.
TEST(PopupWindowTest, promotesTheCardToAnOverlayLayerSurfaceInAWaylandSession)
{
    if (!QGuiApplication::platformName().startsWith(QLatin1StringView("wayland"))) {
        GTEST_SKIP() << "the platform plugin is " << QGuiApplication::platformName().toStdString()
                     << ", which has no zwlr_layer_shell_v1; run this binary through "
                        "tests/harness/nested-session.sh";
    }
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());
    window.showNear(QPoint{400, 300}, QGuiApplication::primaryScreen());

    ASSERT_TRUE(window.isVisible());
    QWindow *handle = window.windowHandle();
    ASSERT_NE(handle, nullptr) << "showNear() left the popup without a QWindow";
    LayerShellQt::Window *layer = LayerShellQt::Window::get(handle);
    ASSERT_NE(layer, nullptr);

    // Above every other surface, so the card is not covered by the window it describes.
    EXPECT_EQ(layer->layer(), LayerShellQt::Window::LayerOverlay);
    // Never pushes a panel aside.
    EXPECT_EQ(layer->exclusionZone(), -1);
    // Two anchors rather than the four a default Window carries, which is what makes the
    // margins an absolute position on the screen.
    EXPECT_EQ(layer->anchors(),
              LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
    // The card takes no keyboard focus, so typing continues to reach the window underneath.
    EXPECT_EQ(layer->keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityNone);
    // A scope of its own rather than the default "window", which is what a compositor rule can
    // match the card on.
    EXPECT_NE(layer->scope(), QStringLiteral("window"));

    // The margins place the card at the rectangle the placement computed, measured from the
    // screen origin the anchors name.
    const QPoint origin = QGuiApplication::primaryScreen()->geometry().topLeft();
    EXPECT_EQ(layer->margins().left(), window.popupRect().x() - origin.x());
    EXPECT_EQ(layer->margins().top(), window.popupRect().y() - origin.y());
    resetSettings();
}

// The pointer crossing a monitor boundary while the card is up.
//
// zwlr_layer_shell_v1 takes the output as an argument of get_layer_surface, and the surface keeps
// that output for its life: layer-shell-qt reads LayerShellQt::Window::screen() in the
// QWaylandLayerSurface constructor, and Window::screenChanged is outside the seven signals it
// connects to requests. A card left mapped across the boundary therefore keeps the output it was built on
// while its margins are re-measured from the origin of the screen the pointer moved to, and it
// draws at that offset from the corner of the screen the pointer left.
//
// The observable is the screen Qt reports for the card's window, which on Wayland is the one the
// compositor sent wl_surface.enter for, so it names the output the surface is really on rather
// than the one the placement asked for. drawnTopLeft() below reads the position that follows from
// it, which is the one a reader sees.
//
// tests/popup/CMakeLists.txt registers this binary a third time on a two-output nested session,
// which is what runs this case; it skips on the single output the other two registrations have.
TEST(PopupWindowTest, rebuildsTheSurfaceOnTheScreenThePointerCrossedOnto)
{
    const auto [leftScreen, rightScreen] = leftAndRightScreens();
    if (leftScreen == nullptr) {
        GTEST_SKIP() << singleScreenSkip;
    }
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());
    window.setFollowCursor(true);

    struct Crossing
    {
        const char *name;
        QScreen *from;
        QScreen *to;
    };

    // The report names both directions, and each one runs from a hidden card so that each one
    // binds a surface of its own before it crosses. One sequence covering both directions holds
    // less: a crossing that fails to rebuild leaves the surface on the output it started on,
    // which is the output the return crossing names, so the second half of such a sequence passes
    // on the defect it is written to catch. Separate sequences expose both failures.
    const Crossing crossings[] = {
        {.name = "right to left", .from = rightScreen, .to = leftScreen},
        {.name = "left to right", .from = leftScreen, .to = rightScreen},
    };

    for (const Crossing &crossing : crossings) {
        SCOPED_TRACE(crossing.name);
        const QPoint from = nearInnerEdge(crossing.from, crossing.from == leftScreen);
        const QPoint to = nearInnerEdge(crossing.to, crossing.to == leftScreen);

        window.showNear(from, crossing.from);
        ASSERT_TRUE(window.isVisible());
        // The compositor's wl_surface.enter arrives on the event loop rather than inside show().
        QTest::qWait(200);
        ASSERT_NE(window.windowHandle(), nullptr) << "showNear() left the popup without a QWindow";
        ASSERT_EQ(surfaceScreenName(window), crossing.from->name().toStdString())
            << "the card did not come up on the screen it was shown for";

        window.showNear(to, crossing.to);
        QTest::qWait(200);
        EXPECT_EQ(surfaceScreenName(window), crossing.to->name().toStdString())
            << "the card is on " << surfaceScreenName(window) << " after the pointer crossed onto "
            << crossing.to->name().toStdString();
        EXPECT_TRUE(crossing.to->geometry().contains(window.popupRect()))
            << "the placement put the card at " << window.popupRect().x() << ',' << window.popupRect().y()
            << ", outside the screen the pointer crossed onto";
        EXPECT_EQ(drawnTopLeft(window), window.popupRect().topLeft())
            << "the card is drawn at " << drawnTopLeft(window).x() << ',' << drawnTopLeft(window).y()
            << " and placed at " << window.popupRect().x() << ',' << window.popupRect().y();

        // The next direction starts from a hidden card, whose surface Qt has destroyed. PopupFadeMs
        // is 0 above, so the hide takes effect inside the call.
        window.hidePopup();
        ASSERT_FALSE(window.isVisible());
    }
    resetSettings();
}

// The crossing under the fade, which is the sequence the report describes.
//
// A pointer leaving the text reaches ScanController::reportNoHit() and the card is hidden, which
// fades it over PopupFadeMs and hides it at the end of the animation. A hit on the next monitor
// arriving inside that window reverses the fade rather than completing it, so the card is still
// mapped on the output the pointer left and its surface is the one that has to be rebuilt. A
// crossing slower than PopupFadeMs finds the card already hidden and its surface already
// destroyed by Qt, and the next show() binds the correct output on its own; that is what makes
// the reported defect intermittent.
//
// tests/popup/CMakeLists.txt registers this binary a third time on a two-output nested session,
// which is what runs this case.
TEST(PopupWindowTest, rebuildsTheSurfaceForACrossingInsideAHideFade)
{
    const auto [leftScreen, rightScreen] = leftAndRightScreens();
    if (leftScreen == nullptr) {
        GTEST_SKIP() << singleScreenSkip;
    }
    resetSettings();
    PopSettings::setPopupFadeMs(120);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());

    window.showNear(nearInnerEdge(rightScreen, false), rightScreen);
    QTest::qWait(200);
    ASSERT_TRUE(window.isVisible());
    ASSERT_NE(window.windowHandle(), nullptr) << "showNear() left the popup without a QWindow";
    ASSERT_EQ(surfaceScreenName(window), rightScreen->name().toStdString())
        << "the card did not come up on the screen it was shown for";

    // 40 ms into a 120 ms fade, which is the state the pointer crosses the boundary in.
    window.hidePopup();
    QTest::qWait(40);
    ASSERT_TRUE(window.isVisible()) << "the fade finished before the case could cross the boundary";

    window.setModel(longModel());
    window.showNear(nearInnerEdge(leftScreen, true), leftScreen);
    QTest::qWait(300);

    EXPECT_TRUE(window.isVisible()) << "the card hid after the crossing";
    EXPECT_DOUBLE_EQ(window.fadeOpacity(), 1.0) << "the crossing did not reverse the hide fade";
    EXPECT_EQ(surfaceScreenName(window), leftScreen->name().toStdString())
        << "the card is on " << surfaceScreenName(window) << " after the pointer crossed onto "
        << leftScreen->name().toStdString() << " inside the fade";
    EXPECT_EQ(drawnTopLeft(window), window.popupRect().topLeft())
        << "the card is drawn at " << drawnTopLeft(window).x() << ',' << drawnTopLeft(window).y() << " and placed at "
        << window.popupRect().x() << ',' << window.popupRect().y();

    window.hidePopup();
    QTest::qWait(300);
    resetSettings();
}

// What a boundary crossing costs, which is the price the rebuild above adds.
//
// A crossing destroys the layer surface and builds another one, where a move inside one screen
// changes two margins. The pointer relay reports at 125 Hz, so a pointer resting on the boundary
// between two outputs can report alternating screens on consecutive samples and ask for a rebuild
// on each: this case is the number that says whether that is affordable. It also covers the
// rebuild not leaking a surface, since 251 of them run in one process.
//
// Same accounting as movesTheCardFasterThanTheEightMillisecondPointerPump: the event-loop pass
// that flushes the request to the compositor is inside the measured interval, and the
// compositor's configure reply is not, so the sequence wall time beside it is what says the
// compositor kept up.
//
// tests/popup/CMakeLists.txt registers this binary a third time on a two-output nested session,
// which is what runs this case.
TEST(PopupWindowTest, crossesABoundaryFasterThanTheEightMillisecondPointerPump)
{
    const auto [leftScreen, rightScreen] = leftAndRightScreens();
    if (leftScreen == nullptr) {
        GTEST_SKIP() << singleScreenSkip;
    }
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());
    window.setFollowCursor(true);

    // Two points four logical pixels either side of the boundary, which is the pair a pointer at
    // rest on it alternates between.
    const QPoint onTheLeft = nearInnerEdge(leftScreen, true);
    const QPoint onTheRight = nearInnerEdge(rightScreen, false);

    window.showNear(onTheLeft, leftScreen);
    ASSERT_TRUE(window.isVisible());

    // 251 rather than 250, which is two seconds of an 8 ms pump plus one step. The count is odd so
    // that the last step names the right-hand screen, against the left-hand one the card came up
    // on, which is what makes the closing assertion read a rebuild rather than the initial map.
    constexpr int kCrossings = 251;
    QList<double> costs;
    costs.reserve(kCrossings);
    QElapsedTimer sequence;
    sequence.start();
    for (int step = 0; step < kCrossings; ++step) {
        // Even steps go right, so step 0 crosses off the left-hand screen the card came up on.
        // Starting on the screen the card is already on would meet the unchanged-rectangle return
        // in showNear() instead, and price a call that rebuilt nothing.
        const bool toTheRight = (step % 2) == 0;
        QElapsedTimer timer;
        timer.start();
        window.showNear(toTheRight ? onTheRight : onTheLeft, toTheRight ? rightScreen : leftScreen);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        costs.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    const qint64 sequenceMs = sequence.elapsed();

    std::ranges::sort(costs);
    const double p50 = costs.at(costs.size() / 2);
    const double p90 = costs.at((costs.size() * 9) / 10);
    std::printf("showNear across a boundary on %s: %d crossings, ms p50 %.3f p90 %.3f max %.3f, "
                "sequence %lld ms against a %d ms pointer budget\n",
                qUtf8Printable(QGuiApplication::platformName()),
                kCrossings,
                p50,
                p90,
                costs.constLast(),
                static_cast<long long>(sequenceMs),
                kCrossings * 8);
    std::fflush(stdout);

    EXPECT_TRUE(window.isVisible()) << "the card did not survive " << kCrossings << " rebuilds";
    // The card came up on the left-hand screen and the last step named the right-hand one, so this
    // is what separates 251 rebuilds from 251 calls that only rewrote the margins.
    QTest::qWait(200);
    EXPECT_EQ(surfaceScreenName(window), rightScreen->name().toStdString())
        << "the card is on " << surfaceScreenName(window) << " after " << kCrossings << " crossings ending on "
        << rightScreen->name().toStdString();
    EXPECT_LT(sequenceMs, static_cast<qint64>(kCrossings) * 8)
        << "the sequence took " << sequenceMs << " ms for " << kCrossings
        << " crossings, which exceeds the 8 ms pointer pump";
    EXPECT_LT(p90, 8.0) << "rebuilding the surface cannot keep up with the 8 ms pointer pump; p50 " << p50
                        << " ms, p90 " << p90 << " ms, max " << costs.constLast() << " ms";

    window.hidePopup();
    resetSettings();
}

// Measure moving the popup at the cursor relay's 125 Hz update rate.
// On Wayland this changes layer margins and flushes a surface commit; offscreen
// uses QWidget::move(). The interval covers showNear() and the event-loop flush,
// but not an asynchronous compositor configure reply. The total sequence time
// checks whether the client can sustain all moves within their pointer intervals.
TEST(PopupWindowTest, movesTheCardFasterThanTheEightMillisecondPointerPump)
{
    resetSettings();
    PopSettings::setPopupFadeMs(0);
    PopupWindow window;
    window.applyTheme();
    window.setModel(samplePopupModel());
    window.setFollowCursor(true);

    QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect geometry = screen->geometry();
    window.showNear(geometry.center(), screen);
    ASSERT_TRUE(window.isVisible());

    // 250 repositions, which is two seconds of an 8 ms pump. The point walks a 200 px by 150 px
    // sawtooth from 100,100 on the output, which stays inside a 1920x1080 and an 800x800 workspace
    // alike, so every case measures a move rather than the clamp in placePopup().
    constexpr int kMoves = 250;
    QList<double> costs;
    costs.reserve(kMoves);
    QRect previous = window.popupRect();
    int unchanged = 0;
    QElapsedTimer sequence;
    sequence.start();
    for (int step = 0; step < kMoves; ++step) {
        const QPoint point = geometry.topLeft() + QPoint{100 + (step % 200), 100 + (step % 150)};
        QElapsedTimer timer;
        timer.start();
        window.showNear(point, screen);
        // Inside the measured interval: LayerShellQt queues the margin change and the flush to the
        // compositor happens from the event loop, so a timer stopped before this pass prices the
        // local bookkeeping alone.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        costs.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        if (window.popupRect() == previous) {
            ++unchanged;
        }
        previous = window.popupRect();
    }
    const qint64 sequenceMs = sequence.elapsed();

    std::ranges::sort(costs);
    const double p50 = costs.at(costs.size() / 2);
    const double p90 = costs.at((costs.size() * 9) / 10);
    std::printf("showNear on %s: %d moves, %d left the rect unchanged, "
                "ms p50 %.3f p90 %.3f max %.3f, sequence %lld ms against a %d ms pointer budget\n",
                qUtf8Printable(QGuiApplication::platformName()),
                kMoves,
                unchanged,
                p50,
                p90,
                costs.constLast(),
                static_cast<long long>(sequenceMs),
                kMoves * 8);
    std::fflush(stdout);

    // The throughput property: 250 moves and their flushes fit inside the 250 pointer intervals
    // that would have produced them, so the card can be placed on every sample rather than on
    // every character.
    EXPECT_LT(sequenceMs, static_cast<qint64>(kMoves) * 8)
        << "the sequence took " << sequenceMs << " ms for " << kMoves << " moves, which exceeds the 8 ms pointer pump";

    // One pointer sample is 8 ms and the card is one of several things that has to happen inside
    // it. The bound reports a change of kind rather than a loaded machine: the numbers this prints
    // are two orders of magnitude under it on both platforms.
    EXPECT_LT(p90, 8.0) << "repositioning the card cannot keep up with the 8 ms pointer pump; p50 " << p50
                        << " ms, p90 " << p90 << " ms, max " << costs.constLast() << " ms on "
                        << QGuiApplication::platformName().toStdString();
    window.hidePopup();
    resetSettings();
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
