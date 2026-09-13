// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The capture half of the scan pipeline against a real compositor, with no logged-in Plasma
// session: tests/harness/nested-session.sh starts kwin_wayland on a virtual output, and this
// binary draws Japanese text at a known logical position, grabs that rectangle back through
// org.kde.KWin.ScreenShot2 and resolves a pointer position to the character under it.
//
// This is the one chain no in-process double covers. capture::FakeFrameSource hands back an
// image the test itself supplied, so it asserts nothing about what KWin renders, where it
// renders it, or how a logical rectangle maps to image pixels. Running the same code inside a
// nested compositor exercises the D-Bus call, the pipe transfer, the scale and the coordinate
// mapping, and it fails on a host that has kwin_wayland rather than on a host that has a
// desktop.
//
// The text is drawn on a zwlr_layer_shell_v1 surface anchored to the top-left corner with
// explicit margins, because a Wayland client cannot otherwise know or choose its position on
// the output, and the grab has to name a rectangle in the same coordinates.
#include "capture/framesource.h"
#include "eventloop.h"
#include "kwinsession.h"
#include "ocr/grouping.h"
#include "ocr/hittest.h"
#include "syntheticpage.h"

#include <QApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QtEnvironmentVariables>

#include <LayerShellQt/Window>
#include <gtest/gtest.h>

using namespace maru;

namespace
{

// The top-left corner of the panel in logical desktop coordinates. Away from the origin so a
// grab that ignored the requested position would return different pixels.
constexpr QPoint kPanelOrigin{240, 160};

// A widget painting one QImage at its own size, shown as a layer surface at kPanelOrigin so its
// logical geometry is a number this test chose rather than one the compositor did.
class Panel : public QWidget
{
public:
    explicit Panel(QImage image)
        : m_image(std::move(image))
    {
        setFixedSize(m_image.size());
    }

    // Maps the panel onto the output at kPanelOrigin. LayerShellQt::Window::get()
    // constructs a handle even without compositor support; the capture assertions below
    // verify that the compositor actually honors its layer role and geometry.
    void showAt(QPoint origin)
    {
        winId(); // realizes the QWindow so the layer surface can be configured
        auto *layer = LayerShellQt::Window::get(windowHandle());
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // -1 keeps the surface out of the exclusive-zone arithmetic, so no other surface is
        // pushed aside and the margins mean what they say.
        layer->setExclusiveZone(-1);
        layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorLeft});

        const QRect screen = QGuiApplication::primaryScreen()->geometry();
        layer->setMargins(QMargins{origin.x() - screen.x(), origin.y() - screen.y(), 0, 0});
        show();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.drawImage(0, 0, m_image);
    }

private:
    QImage m_image;
};

class NestedPipelineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // MARUPOP_NESTED_SESSION is exported by tests/harness/nested-session.sh alone. It is
        // the gate rather than the platform name, because these cases need a compositor that
        // advertises zwlr_layer_shell_v1 and answers org.kde.KWin.ScreenShot2 for this binary,
        // and a Wayland session that does neither would fail on a pixel comparison rather than
        // report what is missing.
        if (qEnvironmentVariable("MARUPOP_NESTED_SESSION") != QLatin1StringView("1")) {
            GTEST_SKIP() << "not inside the nested session; run this binary through "
                            "tests/harness/nested-session.sh, which ctest does as "
                            "pipeline_test_nested";
        }
        ASSERT_TRUE(QGuiApplication::platformName().startsWith(QLatin1StringView("wayland")))
            << "the harness sets QT_QPA_PLATFORM=wayland, and the plugin in use is "
            << QGuiApplication::platformName().toStdString();
        ASSERT_TRUE(capture::KWinFrameSource::available()) << "org.kde.KWin does not own its bus name";
        // Every case grabs through org.kde.KWin.ScreenShot2, which needs OpenGL compositing and so
        // a render node on the host. The CI step reports this skip as a warning.
        if (const QString reason = test::kwinScreenShotSkipReason(); !reason.isEmpty()) {
            GTEST_SKIP() << reason.toStdString();
        }

        page = std::make_unique<test::SyntheticPage>(pageOptions());
        panel = std::make_unique<Panel>(page->image());
        panel->showAt(kPanelOrigin);
        // One composited frame has to reach the output before a grab returns the panel.
        ASSERT_TRUE(test::waitFor(
            [this] {
                return panel->isVisible() && panel->windowHandle() != nullptr && panel->windowHandle()->isExposed();
            },
            5000))
            << "the layer surface was never exposed";
        test::pumpFor(300);
    }

    void TearDown() override
    {
        panel.reset();
        page.reset();
    }

    static test::SyntheticPageOptions pageOptions()
    {
        test::SyntheticPageOptions options;
        options.lines = {QStringLiteral("日本語を読む")};
        options.cellSize = QSize{40, 40};
        options.margin = 20;
        options.pointSize = 28;
        return options;
    }

    // Grabs rect in logical coordinates and returns the frame, or a frame with a null image
    // where the grab failed.
    capture::Frame grab(const QRect &rect)
    {
        capture::KWinFrameSource source;
        capture::Frame frame;
        QString error;
        QEventLoop loop;
        QObject::connect(&source, &capture::FrameSource::frameReady, &loop, [&](const capture::Frame &ready) {
            frame = ready;
            loop.quit();
        });
        QObject::connect(&source, &capture::FrameSource::failed, &loop, [&](const QString &message) {
            error = message;
            loop.quit();
        });
        source.grab(rect);
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        EXPECT_TRUE(error.isEmpty()) << error.toStdString();
        return frame;
    }

    // The count of pixels darker than mid lightness inside cell, sampled from image at scale
    // image pixels per cell pixel. Text raises it above zero; a blank region leaves it at zero.
    static int inkPixels(const QImage &image, const QRect &cell, qreal scale)
    {
        int count = 0;
        for (int y = cell.top(); y <= cell.bottom(); ++y) {
            for (int x = cell.left(); x <= cell.right(); ++x) {
                const QPoint point{qRound(x * scale), qRound(y * scale)};
                if (image.valid(point) && image.pixelColor(point).lightness() < 128) {
                    ++count;
                }
            }
        }
        return count;
    }

    [[nodiscard]] QRect panelRect() const
    {
        return QRect{kPanelOrigin, page->image().size()};
    }

    std::unique_ptr<test::SyntheticPage> page;
    std::unique_ptr<Panel> panel;
};

} // namespace

TEST_F(NestedPipelineTest, grabsTheRectangleTheRequestNamed)
{
    const capture::Frame frame = grab(panelRect());

    ASSERT_FALSE(frame.image.isNull());
    EXPECT_EQ(frame.logicalRect, panelRect());
    // The image is in physical pixels, so its size is the logical size times the output scale.
    const qreal scale = frame.scale;
    EXPECT_EQ(frame.image.width(), qRound(panelRect().width() * scale));
    EXPECT_EQ(frame.image.height(), qRound(panelRect().height() * scale));
}

TEST_F(NestedPipelineTest, returnsThePixelsThePanelDrew)
{
    QString error;
    const QImage grabbed = test::grabIncludingOwnWindows(panelRect(), &error);
    EXPECT_TRUE(error.isEmpty()) << error.toStdString();
    ASSERT_FALSE(grabbed.isNull());
    const qreal scale = static_cast<qreal>(grabbed.width()) / panelRect().width();

    // The margin of the synthetic page is background, and sampling it is what distinguishes a
    // grab of the panel from a grab of the desktop behind it.
    const QImage drawn = page->image();
    const QPoint backgroundPoint{4, 4};
    EXPECT_TRUE(test::coloursMatch(
        grabbed.pixelColor(QPoint(qRound(backgroundPoint.x() * scale), qRound(backgroundPoint.y() * scale))),
        drawn.pixelColor(backgroundPoint)))
        << "the grabbed background differs from the drawn background";

    if (!test::hasJapaneseFont()) {
        GTEST_SKIP() << "no font covers U+65E5, so the drawn cell holds the replacement glyph";
    }
    // The foreground pixel count inside the first cell. An exact glyph comparison would depend
    // on the installed font, while the count separates text from a blank or wrong region.
    const QRect cell = page->boxOf(0, 0);
    const int drawnInk = inkPixels(drawn, cell, 1.0);
    const int grabbedInk = inkPixels(grabbed, cell, scale);
    EXPECT_GT(drawnInk, 0) << "the synthetic page drew no foreground pixels";
    // Within a quarter of the drawn count, which absorbs the compositor's own filtering while
    // failing on a grab that returned a different region.
    EXPECT_NEAR(grabbedInk, drawnInk, drawnInk / 4.0)
        << "the grabbed cell holds " << grabbedInk << " ink pixels against " << drawnInk << " drawn";
}

TEST_F(NestedPipelineTest, leavesTheCallersOwnWindowsOutOfAGrab)
{
    if (!test::hasJapaneseFont()) {
        GTEST_SKIP() << "no font covers U+65E5, so the panel draws no glyph to be excluded";
    }
    // KWinFrameSource sends hide-caller-windows, which is what stops a scan from recognizing
    // MaruPop's own popup and feeding it back into the next lookup. The panel belongs to this
    // process, so a grab through the production path has to leave it out.
    const capture::Frame frame = grab(panelRect());
    ASSERT_FALSE(frame.image.isNull());

    const QRect cell = page->boxOf(0, 0);
    const int drawnInk = inkPixels(page->image(), cell, 1.0);
    ASSERT_GT(drawnInk, 0);
    const int grabbedInk = inkPixels(frame.image, cell, frame.scale);
    // The virtual output behind the panel is a single flat colour, so the cell either holds the
    // glyph or holds none of it.
    EXPECT_TRUE(grabbedInk == 0 || grabbedInk == cell.width() * cell.height())
        << "the grab holds " << grabbedInk << " ink pixels in the cell, which is neither the empty "
        << "background nor the whole background";
    EXPECT_NE(grabbedInk, drawnInk);
}

TEST_F(NestedPipelineTest, resolvesAPointerPositionToTheCharacterUnderIt)
{
    const capture::Frame frame = grab(panelRect());
    ASSERT_FALSE(frame.image.isNull());

    // The truth boxes are in the page's own pixels, which are the grabbed image's logical
    // pixels, so a hit test over them answers for the same points the pointer names.
    // ocr::hitTest() indexes Result::paragraphs, which ocr::OcrService fills through
    // groupLines(), so the same call runs here.
    ocr::Result result = page->truth();
    result.paragraphs = ocr::groupLines(result.lines, result.sourceSize);
    ASSERT_FALSE(result.paragraphs.isEmpty());
    const QString &text = result.paragraphs.first().text;

    for (int index = 0; index < text.size(); ++index) {
        // The pointer position in logical desktop coordinates, converted to the frame's own
        // coordinate space the way scan::ScanController converts it.
        const QPoint logical = kPanelOrigin + page->centreOf(0, index);
        const QPoint inImage = logical - frame.logicalRect.topLeft();

        const std::optional<ocr::Hit> hit = ocr::hitTest(result, inImage);
        ASSERT_TRUE(hit.has_value()) << "no character at index " << index;
        EXPECT_EQ(hit->charIndex, index);
        // Against the character the page drew in that cell, not against text.at(index) again: the
        // line above already establishes charIndex == index, so comparing the string to itself at
        // the same index asserts nothing about the mapping.
        ASSERT_LT(hit->charIndex, page->truth().lines.constFirst().chars.size());
        EXPECT_EQ(static_cast<char32_t>(text.at(hit->charIndex).unicode()),
                  page->truth().lines.constFirst().chars.at(hit->charIndex).codePoint);
    }
}

int main(int argc, char **argv)
{
    // QApplication rather than QGuiApplication: the panel is a QWidget, and QWidget requires
    // the widget application object.
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
