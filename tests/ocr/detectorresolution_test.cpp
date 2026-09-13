// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Hit-test accuracy of the real detector against the extent one character reaches its input at,
// over a page rendered here rather than over a screenshot fixture.
//
// The reported defect is the popup replacing its entry once or twice while the pointer stands
// still and coming to rest on the entry for a neighbouring character. The pointer stands still
// across those replacements, so the only quantity that changed is the region the scan ladder
// grabbed, and with it the extent a character reached the 960x544 detector input at:
// MeikiOcrBackend letterboxes 480x270 at scale 2.000 and 1600x900 at scale 0.600.
//
// This suite is the measurement behind ocr::kMinCharExtentRatio and behind the growth gate in
// scan::ScanController. It reports one row per rung and asserts two properties:
//
//   the initial rung answers at least as many pointer positions with the painted character as
//   the largest rung does; and
//
//   every rung ocr::resolvesCharactersAt() admits answers what the initial rung answers, at every
//   pointer position both rungs cover.
//
// The page reproduces the parameters of the reported case: 14 px characters in RGB 129,130,138 on
// RGB 0,0,0, which is a contrast ratio of 5.4 to 1. The font of the reported case is Meiryo UI and
// is licensed, so the page is drawn with whatever family fontconfig resolves for Japanese and the
// suite skips where that family covers no Japanese; maru::test::SyntheticPage draws each character
// centred in a fixed cell, so the character rectangles are the same on every host and only the
// glyphs differ.
//
// tests/scan/scanresolution_test.cpp is the same defect at the controller level, with the detector
// modelled rather than loaded, and runs on every host.
#include "capture/scanregion.h"
#include "core/paths.h"
#include "ocr/grouping.h"
#include "ocr/hittest.h"
#include "ocr/meikiocrbackend.h"
#include "ocr/resolution.h"
#include "syntheticpage.h"

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <gtest/gtest.h>

using namespace maru;

namespace
{

// The workspace the ladder is climbed over, which is the extent of the largest rung the default
// MaxScanWidth by MaxScanHeight reaches.
const QRect kWorkspace{0, 0, 1600, 900};
const QSize kInitialSize{480, 270};
const QSize kMaxSize{1600, 900};

// Exercise small 14 px characters and the 28 px characters used by the hover probe.
constexpr int kSmallCellPx = 14;
constexpr int kLargeCellPx = 28;

const QColor kBackground{0, 0, 0};
const QColor kForeground{129, 130, 138};

QString modelDirectory()
{
    const QString fromEnvironment = qEnvironmentVariable("MARUPOP_MEIKI_MODELS");
    return fromEnvironment.isEmpty() ? paths::modelsDir() : fromEnvironment;
}

// One line of text on a workspace-sized canvas, with the character rectangles in workspace
// coordinates. maru::test::SyntheticPage sizes its image to its content, so the line is composed
// onto the canvas at an origin the boxes are translated by.
struct Desktop
{
    QImage image;
    QList<QRect> boxes;
    QString text;
};

Desktop paintLine(const QString &text, int cellPx)
{
    test::SyntheticPageOptions options;
    options.lines = QStringList{text};
    options.cellSize = QSize{cellPx, cellPx};
    options.margin = 0;
    options.pixelSize = cellPx;
    options.background = kBackground;
    options.foreground = kForeground;
    const test::SyntheticPage page{options};

    Desktop desktop;
    desktop.text = text;
    desktop.image = QImage{kWorkspace.size(), QImage::Format_RGB888};
    desktop.image.fill(kBackground);
    // Left of centre and above it, so the 480x270 region around any character of the line stays
    // inside the workspace without being clamped against an edge.
    const QPoint origin{40, kWorkspace.height() / 2};
    {
        QPainter painter{&desktop.image};
        painter.drawImage(origin, page.image());
    }
    for (int index = 0; index < text.size(); ++index) {
        desktop.boxes.append(page.boxOf(0, index).translated(origin));
    }
    return desktop;
}

// What one recognition pass over rect answers at every pointer position of the common set.
struct Rung
{
    QRect rect;
    ocr::Result result;
    // One entry per box of commonBoxes, holding the character the hit test answered or an empty
    // string where it answered none.
    QStringList answers;
    int correct = 0;
    double extentAtInput = 0.0;
    bool admitted = false;
};

Rung scanRung(ocr::MeikiOcrBackend &backend, const Desktop &desktop, QRect rect, const QList<int> &commonIndices)
{
    Rung rung;
    rung.rect = rect;
    const QImage crop = desktop.image.copy(rect);
    rung.result = backend.recognize(crop);
    rung.result.paragraphs = ocr::groupLines(rung.result.lines, crop.size());

    for (const int index : commonIndices) {
        const QPoint pointer = desktop.boxes.at(index).center() - rect.topLeft();
        QString answer;
        if (const std::optional<ocr::Hit> hit = ocr::hitTest(rung.result, pointer)) {
            if (hit->paragraph >= 0 && hit->paragraph < rung.result.paragraphs.size()) {
                const ocr::Paragraph &paragraph = rung.result.paragraphs.at(hit->paragraph);
                if (hit->charIndex >= 0 && hit->charIndex < paragraph.text.size()) {
                    answer = paragraph.text.mid(hit->charIndex, 1);
                }
            }
        }
        rung.answers.append(answer);
        if (answer == QString{desktop.text.at(index)}) {
            ++rung.correct;
        }
    }
    return rung;
}

// The rungs capture::grow() reaches from capture::initialRect() around pointer, the first one
// included.
QList<QRect> ladderFrom(QPoint pointer)
{
    QList<QRect> rungs{capture::initialRect(pointer, kInitialSize, kWorkspace)};
    while (true) {
        const std::optional<QRect> grown = capture::grow(rungs.constLast(), pointer, kMaxSize, kWorkspace);
        if (!grown.has_value() || *grown == rungs.constLast()) {
            return rungs;
        }
        rungs.append(*grown);
    }
}

void measure(int cellPx)
{
    const QString directory = modelDirectory();
    if (!ocr::MeikiOcrBackend::modelsPresent(directory)) {
        GTEST_SKIP() << "the meikiocr models are absent; set MARUPOP_MEIKI_MODELS to a directory holding them";
    }
    if (!test::hasJapaneseFont()) {
        GTEST_SKIP() << "fontconfig resolves no family covering U+65E5, so the page carries no Japanese glyphs";
    }
    ocr::MeikiOcrBackend backend{directory, ocr::MeikiOcrBackend::Options{}};
    ASSERT_TRUE(backend.initialize());

    // 50 characters, which fills 700 px at 14 px per character and 1400 px at 28 px, and so runs
    // past the left and the right edge of a 480x270 region centred anywhere near its middle.
    const QString line = QStringLiteral(
        "鋸の熱き歯をもてわが挽きし夜のひまはりつひに首無し濁流に捨て来し燃ゆる曼珠沙華あかきを何の生贄とせむ");
    const Desktop desktop = paintLine(line, cellPx);
    const QPoint pointer = desktop.boxes.at(desktop.boxes.size() / 2).center();

    const QList<QRect> rungs = ladderFrom(pointer);
    ASSERT_GE(rungs.size(), 2);

    // The pointer positions every rung covers, which is the set the initial rung holds.
    QList<int> commonIndices;
    for (int index = 0; index < desktop.boxes.size(); ++index) {
        if (rungs.constFirst().contains(desktop.boxes.at(index))) {
            commonIndices.append(index);
        }
    }
    ASSERT_FALSE(commonIndices.isEmpty());

    QList<Rung> measured;
    for (const QRect &rect : rungs) {
        Rung rung = scanRung(backend, desktop, rect, commonIndices);
        rung.extentAtInput =
            ocr::charExtentAtModelInput(measured.isEmpty() ? rung.result : measured.constFirst().result, rect.size());
        rung.admitted =
            ocr::resolvesCharactersAt(measured.isEmpty() ? rung.result : measured.constFirst().result, rect.size());
        measured.append(rung);
    }

    std::fprintf(stderr,
                 "[ MEASURE  ] %d px characters, %d pointer positions, threshold %.1f px at the detector input\n",
                 cellPx,
                 static_cast<int>(commonIndices.size()),
                 ocr::minimumCharExtent(measured.constFirst().result.modelInputSize));
    for (const Rung &rung : measured) {
        std::fprintf(stderr,
                     "[ MEASURE  ]   %4dx%4d  %5.1f px at the input  %s  %2d/%2d positions answered the painted "
                     "character\n",
                     rung.rect.width(),
                     rung.rect.height(),
                     rung.extentAtInput,
                     rung.admitted ? "admitted" : "declined",
                     rung.correct,
                     static_cast<int>(commonIndices.size()));
    }

    const Rung &initial = measured.constFirst();
    const Rung &largest = measured.constLast();
    EXPECT_GE(initial.correct, largest.correct)
        << "the " << largest.rect.width() << "x" << largest.rect.height() << " rung answered more positions than the "
        << initial.rect.width() << "x" << initial.rect.height() << " rung";

    for (const Rung &rung : measured) {
        if (!rung.admitted) {
            continue;
        }
        EXPECT_EQ(rung.answers, initial.answers)
            << "the " << rung.rect.width() << "x" << rung.rect.height()
            << " rung is admitted and answers a different character than the initial rung";
    }

    // The initial rung is always admitted: it is the one the ladder starts from, and nothing
    // finer exists to fall back to.
    EXPECT_TRUE(initial.admitted);
}

} // namespace

// The reported case: characters small enough that no rung beyond the first resolves them.
TEST(DetectorResolutionTest, resolvesFourteenPixelCharactersOnTheInitialRungAlone)
{
    measure(kSmallCellPx);
}

// Characters large enough that the ladder climbs one rung, which is the case the growth exists
// for: a word running past the edge of the initial region is matched whole from the second rung.
TEST(DetectorResolutionTest, resolvesTwentyEightPixelCharactersOnTwoRungs)
{
    measure(kLargeCellPx);
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv}; // font rendering needs a GUI platform
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
