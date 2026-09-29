// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// docs/TESTING.md, section "Interactive desktop tests", lists the inputs.
#include "capture/winframesource.h"
#include "core/settings.h"
#include "cursor/wincursortracker.h"
#include "eventloop.h"
#include "fakes.h"
#include "livedesktop.h"
#include "lookup/lookuptypes.h"
#include "ocr/ocrservice.h"
#include "scan/hitcontext.h"
#include "scan/scancontroller.h"
#include "syntheticpage.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include <cstdio>
#include <gtest/gtest.h>
#include <vector>
#include <windows.h>

using namespace maru;

namespace
{

class PageWindow : public test::LiveWindow
{
public:
    explicit PageWindow(QImage image)
        : m_image(std::move(image))
    {
        setFixedSize(m_image.size());
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter{this}.drawImage(QPoint{}, m_image);
    }

private:
    QImage m_image;
};

// SetCursorPos() takes device pixels.
QPoint devicePoint(QPoint logical, const QScreen *screen)
{
    return capture::WinFrameSource::nativeRect(
               QRect{logical, QSize{1, 1}}, screen->geometry(), capture::WinFrameSource::nativeGeometry(screen))
        .topLeft();
}

class WinPipelineLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!test::liveDesktop()) {
            GTEST_SKIP() << test::liveDesktopSkip;
        }
        PopSettings::self()->setDefaults();
        const QString screenAi = qEnvironmentVariable("MARUPOP_SCREEN_AI_RESOURCES");
        if (!screenAi.isEmpty()) {
            PopSettings::setScreenAiResourcesDir(screenAi);
        }
        PopSettings::self()->save();
        if (ocr::OcrService::availableBackends().isEmpty()) {
            GTEST_SKIP() << "no recognition backend can load; set MARUPOP_SCREEN_AI_RESOURCES to a Chrome Screen AI "
                            "component folder or download the meikiocr models";
        }
        ocr.start();
        ocr.setEngine(OcrEngine::Automatic);
        ASSERT_TRUE(test::waitFor(
            [&] {
                return ocr.isReady();
            },
            30000))
            << "the recognition backend did not load";

        screen = QGuiApplication::primaryScreen();
        page = std::make_unique<PageWindow>(synthetic.image());
        QRect geometry{QPoint{}, page->size()};
        geometry.moveCenter(screen->geometry().center());
        page->setGeometry(geometry);
        page->show();
        ASSERT_TRUE(test::waitFor([&] {
            return page->windowHandle() != nullptr && page->windowHandle()->isExposed();
        }));
        test::pumpFor(60);
        GetCursorPos(&originalPointer);
        // positionChanged() fires on a change alone, so the pointer starts outside the page.
        pointAt(page->geometry().topLeft() - QPoint{40, 40});
    }

    void TearDown() override
    {
        if (test::liveDesktop()) {
            SetCursorPos(originalPointer.x, originalPointer.y);
        }
    }

    void pointAt(QPoint logical)
    {
        const QPoint device = devicePoint(logical, screen);
        SetCursorPos(device.x(), device.y());
    }

    [[nodiscard]] QPoint characterCentre(int line, int character) const
    {
        return page->geometry().topLeft() + synthetic.boxOf(line, character).center();
    }

    test::SyntheticPage synthetic{
        test::SyntheticPageOptions{.lines = {QStringLiteral("日本語を読む"), QStringLiteral("辞書を引く")},
                                   .cellSize = QSize{40, 40},
                                   .pixelSize = 34}};
    std::unique_ptr<PageWindow> page;
    QScreen *screen = nullptr;
    POINT originalPointer{};
    ocr::OcrService ocr;
    cursor::WinCursorTracker tracker;
    capture::WinFrameSource frames;
};

} // namespace

TEST_F(WinPipelineLive, looksUpTheCharacterUnderThePointer)
{
    scan::ScanController controller{tracker, frames, ocr, [](const lookup::Request &request) {
                                        return test::cannedResponse(request);
                                    }};
    QString matched;
    scan::HitContext hit;
    qint64 deliveredNs = 0;
    QElapsedTimer sinceMove;
    QObject::connect(&controller,
                     &scan::ScanController::lookupReady,
                     &controller,
                     [&](const lookup::Response &response, const scan::HitContext &context) {
                         deliveredNs = sinceMove.nsecsElapsed();
                         matched = response.results.isEmpty() ? QString{} : response.results.constFirst().matchedText;
                         hit = context;
                     });
    tracker.start();
    controller.setScanning(true);
    test::pumpFor(100);

    std::vector<double> cold;
    std::vector<double> cached;
    for (int round = 0; round < 5; ++round) {
        pointAt(page->geometry().topLeft() - QPoint{40, 40});
        test::pumpFor(150);
        controller.forceRescan();
        test::pumpFor(50);
        matched.clear();
        sinceMove.start();
        pointAt(characterCentre(0, 1));
        ASSERT_TRUE(test::waitFor(
            [&] {
                return matched == QStringLiteral("本");
            },
            5000))
            << "round " << round << ": the lookup answered \"" << matched.toStdString() << "\" for \""
            << hit.paragraphText.toStdString() << "\"";
        cold.push_back(static_cast<double>(deliveredNs) / 1e6);
        EXPECT_TRUE(hit.paragraphText.contains(QStringLiteral("日本語"))) << hit.paragraphText.toStdString();
        EXPECT_EQ(hit.backendName, ocr.activeBackendName());

        matched.clear();
        sinceMove.start();
        pointAt(characterCentre(0, 2));
        ASSERT_TRUE(test::waitFor(
            [&] {
                return matched == QStringLiteral("語");
            },
            5000))
            << "round " << round << ": the lookup answered \"" << matched.toStdString() << "\"";
        cached.push_back(static_cast<double>(deliveredNs) / 1e6);
    }
    std::printf("pointer to lookup through %s: cold p50 %.1f ms (min %.1f, max %.1f); cached p50 %.1f ms "
                "(min %.1f, max %.1f)\n",
                qPrintable(ocr.activeBackendName()),
                test::percentile(cold, 0.5),
                test::percentile(cold, 0.0),
                test::percentile(cold, 1.0),
                test::percentile(cached, 0.5),
                test::percentile(cached, 0.0),
                test::percentile(cached, 1.0));
    // The 100 ms bound covers the 8 ms poll, the cursorMoveThrottleMs interval and one 16.7 ms frame
    // at 60 Hz.
    EXPECT_LT(test::percentile(cached, 0.5), 100.0);
    // The cold path runs one recognition pass. The 1 s bound fails a stalled grab and a repeated
    // recognition pass.
    EXPECT_LT(test::percentile(cold, 0.5), 1000.0);
}

int main(int argc, char **argv)
{
    test::selectLivePlatform();
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
