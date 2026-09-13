// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// End-to-end backend cases. Both suites need an installed component: the meikiocr models under
// MARUPOP_MEIKI_MODELS or paths::modelsDir(), and the Chrome Screen AI component under
// MARUPOP_SCREEN_AI_RESOURCES or ~/.config/screen_ai/resources. Each skips where its component
// is absent, so a checkout with neither still runs the suite.
#include "core/paths.h"
#include "ocr/grouping.h"
#include "ocr/hittest.h"
#include "ocr/meikiocrbackend.h"
#include "ocr/ortenv.h"
#include "ocr/screenaibackend.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <gtest/gtest.h>

using namespace maru::ocr;

namespace
{

QImage renderText(const QString &text, int pixelSize = 48, int width = 900, int height = 160)
{
    QImage image{width, height, QImage::Format_RGB888};
    image.fill(Qt::white);
    QPainter painter{&image};
    painter.setPen(Qt::black);
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(pixelSize);
    painter.setFont(font);
    painter.drawText(image.rect().adjusted(20, 10, -20, -10), Qt::AlignLeft | Qt::AlignVCenter, text);
    return image;
}

// The bounding rectangle of every pixel darker than the white background, which is where the
// glyphs were painted.
QRect inkRect(const QImage &image)
{
    QRect ink;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 200) {
                ink = ink.united(QRect{x, y, 1, 1});
            }
        }
    }
    return ink;
}

QString modelDirectory()
{
    const QString fromEnvironment = qEnvironmentVariable("MARUPOP_MEIKI_MODELS");
    return fromEnvironment.isEmpty() ? maru::paths::modelsDir() : fromEnvironment;
}

QString screenAiResources()
{
    const QString fromEnvironment = qEnvironmentVariable("MARUPOP_SCREEN_AI_RESOURCES");
    return fromEnvironment.isEmpty() ? QStringLiteral("~/.config/screen_ai/resources") : fromEnvironment;
}

QString joinedText(const QList<TextLine> &lines)
{
    QString text;
    for (const TextLine &line : lines) {
        text.append(line.text);
    }
    return text;
}

} // namespace

TEST(OrtEnvironment, listsTheExecutionProvidersAndTheVersion)
{
    const QStringList providers = availableProviders();
    ASSERT_FALSE(providers.isEmpty());
    EXPECT_TRUE(providers.contains(QStringLiteral("CPUExecutionProvider"))) << providers.join(u',').toStdString();
    EXPECT_FALSE(ortVersion().isEmpty());
}

TEST(MeikiOcrBackendTest, recognizesRenderedJapanese)
{
    const QString directory = modelDirectory();
    if (!MeikiOcrBackend::modelsPresent(directory)) {
        GTEST_SKIP() << "the meikiocr models are absent; set MARUPOP_MEIKI_MODELS to a directory holding them";
    }
    MeikiOcrBackend backend{directory, MeikiOcrBackend::Options{}};
    QElapsedTimer loadTimer;
    loadTimer.start();
    ASSERT_TRUE(backend.initialize());
    std::fprintf(stderr,
                 "[ TIMING   ] meikiocr: three sessions loaded on %s in %lld ms\n",
                 qPrintable(backend.activeProvider()),
                 static_cast<long long>(loadTimer.elapsed()));

    const QImage image = renderText(QStringLiteral("日本語のテキスト"));
    const Result result = backend.recognize(image);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_EQ(result.backendName, QStringLiteral("meikiocr"));
    ASSERT_FALSE(result.lines.isEmpty());
    std::fprintf(stderr,
                 "[ TIMING   ] meikiocr: %d lines in %lld ms (first call)\n",
                 static_cast<int>(result.lines.size()),
                 static_cast<long long>(result.elapsedMs));
    for (int run = 0; run < 4; ++run) {
        const Result repeated = backend.recognize(image);
        std::fprintf(
            stderr, "[ TIMING   ] meikiocr: repeat %d in %lld ms\n", run, static_cast<long long>(repeated.elapsedMs));
    }

    const QString text = joinedText(result.lines);
    EXPECT_TRUE(text.contains(QStringLiteral("日本語"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("テキスト"))) << text.toStdString();

    // Every character box lies inside the painted glyph area, with a 6 px margin for the
    // difference between a glyph's ink extent and the box the detector reports.
    const QRect ink = inkRect(image).adjusted(-6, -6, 6, 6);
    for (const TextLine &line : result.lines) {
        EXPECT_EQ(line.text.size(), line.chars.size());
        for (const CharBox &character : line.chars) {
            EXPECT_TRUE(ink.contains(character.box))
                << "character box " << character.box.x() << "," << character.box.y() << " " << character.box.width()
                << "x" << character.box.height() << " outside the ink rectangle";
        }
    }

    // The grouping and the hit test run over what the backend produced.
    const QList<Paragraph> paragraphs = groupLines(result.lines, image.size());
    ASSERT_FALSE(paragraphs.isEmpty());
    Result grouped = result;
    grouped.paragraphs = paragraphs;
    const CharBox &first = paragraphs.constFirst().chars.constFirst();
    const std::optional<Hit> hit = hitTest(grouped, first.box.center());
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->charIndex, 0);
}

TEST(MeikiOcrBackendTest, recognizesVerticalJapanese)
{
    const QString directory = modelDirectory();
    if (!MeikiOcrBackend::modelsPresent(directory)) {
        GTEST_SKIP() << "the meikiocr models are absent; set MARUPOP_MEIKI_MODELS to a directory holding them";
    }
    MeikiOcrBackend backend{directory, MeikiOcrBackend::Options{}};
    ASSERT_TRUE(backend.initialize());

    // One column of characters, which is the layout the 32x480 vertical model is trained on.
    const QString column = QStringLiteral("日本語のテキスト");
    QImage image{160, 700, QImage::Format_RGB888};
    image.fill(Qt::white);
    {
        QPainter painter{&image};
        painter.setPen(Qt::black);
        QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        font.setPixelSize(60);
        painter.setFont(font);
        for (qsizetype index = 0; index < column.size(); ++index) {
            painter.drawText(
                QRect{40, 20 + (static_cast<int>(index) * 70), 80, 70}, Qt::AlignCenter, column.mid(index, 1));
        }
    }

    const Result result = backend.recognize(image);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_FALSE(result.lines.isEmpty());
    std::fprintf(stderr,
                 "[ TIMING   ] meikiocr vertical: %d lines in %lld ms\n",
                 static_cast<int>(result.lines.size()),
                 static_cast<long long>(result.elapsedMs));
    EXPECT_TRUE(result.lines.constFirst().vertical);
    const QString text = joinedText(result.lines);
    EXPECT_TRUE(text.contains(QStringLiteral("日本語"))) << text.toStdString();

    // The grouping rule recomputes the orientation from the character boxes and has to agree.
    const QList<Paragraph> paragraphs = groupLines(result.lines, image.size());
    ASSERT_FALSE(paragraphs.isEmpty());
    EXPECT_TRUE(paragraphs.constFirst().vertical);
}

// GPU-provider probing is opt-in because it loads driver runtimes into the process.
// Run with MARUPOP_TRY_GPU=1 to check provider fallback on the installed driver
// and ONNX Runtime build.
TEST(MeikiOcrBackendTest, fallsBackToTheCpuProvider)
{
    const QString directory = modelDirectory();
    if (qEnvironmentVariable("MARUPOP_TRY_GPU") != QLatin1String("1")) {
        GTEST_SKIP() << "set MARUPOP_TRY_GPU=1 to attempt the GPU execution providers";
    }
    if (!MeikiOcrBackend::modelsPresent(directory)) {
        GTEST_SKIP() << "the meikiocr models are absent";
    }
    std::fprintf(stderr,
                 "[ TIMING   ] onnxruntime %s providers: %s\n",
                 qPrintable(ortVersion()),
                 qPrintable(availableProviders().join(u',')));
    MeikiOcrBackend backend{directory, MeikiOcrBackend::Options{.allowGpu = true}};
    QElapsedTimer selection;
    selection.start();
    ASSERT_TRUE(backend.initialize());
    std::fprintf(stderr, "[ TIMING   ] provider selection took %lld ms\n", static_cast<long long>(selection.elapsed()));
    std::fprintf(stderr, "[ TIMING   ] meikiocr: validated provider %s\n", qPrintable(backend.activeProvider()));
    const Result result = backend.recognize(renderText(QStringLiteral("日本語のテキスト")));
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
}

TEST(MeikiOcrBackendTest, reportsAMissingModelDirectory)
{
    MeikiOcrBackend backend{QStringLiteral("/nonexistent/marupop-models"), MeikiOcrBackend::Options{}};
    EXPECT_FALSE(backend.initialize());
    EXPECT_FALSE(backend.isReady());
    const Result result = backend.recognize(renderText(QStringLiteral("日本語")));
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST(ScreenAiBackendTest, reportsPerCharacterBoxes)
{
    const QString resources = screenAiResources();
    if (!ScreenAiBackend::isInstalled(resources)) {
        GTEST_SKIP() << "the Screen AI component is not installed; set MARUPOP_SCREEN_AI_RESOURCES";
    }
    ScreenAiBackend backend;
    backend.setResourcesDir(resources);
    ASSERT_TRUE(backend.initialize());

    const QImage image = renderText(QStringLiteral("日本語のテキスト"));
    const Result result = backend.recognize(image);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_EQ(result.backendName, QStringLiteral("Chrome Screen AI"));
    ASSERT_FALSE(result.lines.isEmpty());
    std::fprintf(stderr,
                 "[ TIMING   ] screenai: %d lines in %lld ms (first call)\n",
                 static_cast<int>(result.lines.size()),
                 static_cast<long long>(result.elapsedMs));
    for (int run = 0; run < 4; ++run) {
        const Result repeated = backend.recognize(image);
        std::fprintf(
            stderr, "[ TIMING   ] screenai: repeat %d in %lld ms\n", run, static_cast<long long>(repeated.elapsedMs));
    }

    const QString text = joinedText(result.lines);
    EXPECT_TRUE(text.contains(QStringLiteral("日本語"))) << text.toStdString();

    const QRect bounds = image.rect();
    for (const TextLine &line : result.lines) {
        EXPECT_EQ(line.text.size(), line.chars.size());
        EXPECT_FALSE(line.chars.isEmpty());
        for (const CharBox &character : line.chars) {
            EXPECT_TRUE(bounds.contains(character.box)) << "character box outside the image";
            EXPECT_GT(character.box.width(), 0);
            EXPECT_GT(character.box.height(), 0);
        }
    }
}

TEST(ScreenAiBackendTest, reportsAMissingComponent)
{
    EXPECT_FALSE(ScreenAiBackend::isInstalled(QStringLiteral("/nonexistent/screen_ai")));
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv}; // font rendering needs a GUI platform
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
