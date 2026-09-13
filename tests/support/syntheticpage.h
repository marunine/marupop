// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// An image of Japanese text with the character rectangles that produced it, which is the ground
// truth a test asserts against without a screenshot fixture and without a recognition model.
//
// Every character is drawn centred in a cell of a fixed grid rather than advanced by font
// metrics, so the rectangles are the same on a host with a different Japanese font installed and
// on a host with none. That makes the geometry deterministic; the glyphs are not, which is why
// hasJapaneseFont() gates the cases that hand the image to a recognition backend.
//
// Two uses:
//   - truth() drives ocr::hitTest(), scan::ScanController and popup rendering with realistic
//     boxes and no recognition pass.
//   - image() is the input for a real backend under MARUPOP_MEIKI_MODELS or
//     MARUPOP_SCREEN_AI_RESOURCES, where the recognized string is compared to the drawn one.
#pragma once

#include "ocr/ocrtypes.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

namespace maru::test
{

struct SyntheticPageOptions
{
    // One entry per line of text. Each QChar occupies one cell. An empty list produces a
    // background-only image of one cell plus two margins and a truth with no lines.
    QStringList lines{QStringLiteral("日本語を読む"), QStringLiteral("辞書を引く")};

    // Cell size in image pixels. 32x32 holds a full-width glyph at the default pointSize of 22,
    // which Qt renders 29 px tall, leaving at least 1 px of background on each side. That
    // separation is what lets a recognition model segment one character from the next.
    QSize cellSize{32, 32};

    // Distance in image pixels between the near edges of two adjacent lines, measured along the
    // axis perpendicular to the text direction.
    int lineSpacing = 44;

    // Image pixels between the image edge and the first cell.
    int margin = 16;

    // True lays each line top to bottom and advances lines right to left, which is the Japanese
    // vertical writing order and the order ocr::Paragraph::vertical marks.
    bool vertical = false;

    int pointSize = 22;
    // Glyph height in image pixels, which overrides pointSize where it is above 0. A case that
    // reproduces text of a stated pixel height sets this: pointSize resolves through the logical
    // DPI of the screen the suite happens to run on, and 22 pt is 29 px at 96 dpi and 44 px at
    // 144 dpi.
    int pixelSize = 0;
    QColor background = QColor(Qt::white);
    QColor foreground = QColor(Qt::black);

    QImage::Format format = QImage::Format_RGB888;
};

class SyntheticPage
{
public:
    explicit SyntheticPage(const SyntheticPageOptions &options = {});

    // The rendered pixels, in SyntheticPageOptions::format.
    [[nodiscard]] QImage image() const;

    // The lines and character boxes that produced image(), as a backend would report them.
    // Result::success is true, Result::backendName is "synthetic", and Result::paragraphs is
    // empty: ocr::OcrService fills paragraphs through ocr::groupLines(), so a test that needs
    // them calls that function on this result.
    [[nodiscard]] ocr::Result truth() const;

    // The box of one character in image pixels.
    [[nodiscard]] QRect boxOf(int lineIndex, int charIndex) const;

    // The centre of boxOf(lineIndex, charIndex), which is the point a hit test resolves to that
    // character.
    [[nodiscard]] QPoint centreOf(int lineIndex, int charIndex) const;

private:
    QImage m_image;
    ocr::Result m_truth;
};

// True where fontconfig resolves a family covering U+65E5, which is the condition for the drawn
// glyphs to be Japanese rather than the replacement box. A case that asserts on recognized text
// skips where this is false.
[[nodiscard]] bool hasJapaneseFont();

} // namespace maru::test
