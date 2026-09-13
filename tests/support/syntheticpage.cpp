// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "syntheticpage.h"

#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QRawFont>

#include <algorithm>

namespace maru::test
{

namespace
{

// U+65E5 in a common word, which is the code point hasJapaneseFont() probes for coverage.
constexpr char32_t kProbeCodePoint = 0x65E5;

} // namespace

SyntheticPage::SyntheticPage(const SyntheticPageOptions &options)
{
    // The extent along the text direction is set by the longest line, and the extent across it
    // by the line count, so a page with one 6-character line and a 32x32 cell is
    // 16 + 6*32 + 16 = 224 px wide.
    int longest = 0;
    for (const QString &line : options.lines) {
        longest = std::max(longest, static_cast<int>(line.size()));
    }
    const int lineCount = static_cast<int>(options.lines.size());
    // Both extents hold one cell at a minimum, which is the size syntheticpage.h documents for an
    // empty line list. Without the two clamps that list gave (lineCount - 1) = -1, so the default
    // options produced a 32x20 image against the documented 64x64, and a lineSpacing larger than
    // the margins and the cell together produced a null QImage that QPainter refuses to begin on.
    const int columns = std::max(1, longest);
    const int gaps = std::max(0, lineCount - 1);
    const int alongText = (2 * options.margin) +
                          (options.vertical ? columns * options.cellSize.height() : columns * options.cellSize.width());
    const int acrossText = (2 * options.margin) + (gaps * options.lineSpacing) +
                           (options.vertical ? options.cellSize.width() : options.cellSize.height());

    const QSize size = options.vertical ? QSize{acrossText, alongText} : QSize{alongText, acrossText};
    m_image = QImage(size, options.format);
    m_image.fill(options.background);

    QPainter painter(&m_image);
    QFont font;
    if (options.pixelSize > 0) {
        font.setPixelSize(options.pixelSize);
    } else {
        font.setPointSize(options.pointSize);
    }
    painter.setFont(font);
    painter.setPen(options.foreground);

    m_truth.success = true;
    m_truth.backendName = QStringLiteral("synthetic");
    m_truth.sourceSize = size;

    for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
        const QString &text = options.lines.at(lineIndex);
        ocr::TextLine line;
        line.text = text;
        line.vertical = options.vertical;
        line.confidence = 1.0F;

        for (int charIndex = 0; charIndex < text.size(); ++charIndex) {
            // A vertical page advances lines right to left, so the first line is at the right
            // edge and lineIndex counts leftwards.
            const QRect box = options.vertical ? QRect{size.width() - options.margin - options.cellSize.width() -
                                                           (lineIndex * options.lineSpacing),
                                                       options.margin + (charIndex * options.cellSize.height()),
                                                       options.cellSize.width(),
                                                       options.cellSize.height()}
                                               : QRect{options.margin + (charIndex * options.cellSize.width()),
                                                       options.margin + (lineIndex * options.lineSpacing),
                                                       options.cellSize.width(),
                                                       options.cellSize.height()};

            painter.drawText(box, Qt::AlignCenter, text.mid(charIndex, 1));
            line.chars.append(ocr::CharBox{
                .codePoint = static_cast<char32_t>(text.at(charIndex).unicode()), .box = box, .confidence = 1.0F});
            line.box = line.box.united(box);
        }
        m_truth.lines.append(line);
    }
    painter.end();
}

QImage SyntheticPage::image() const
{
    return m_image;
}

ocr::Result SyntheticPage::truth() const
{
    return m_truth;
}

QRect SyntheticPage::boxOf(int lineIndex, int charIndex) const
{
    return m_truth.lines.at(lineIndex).chars.at(charIndex).box;
}

QPoint SyntheticPage::centreOf(int lineIndex, int charIndex) const
{
    return boxOf(lineIndex, charIndex).center();
}

bool hasJapaneseFont()
{
    QFont font;
    const QRawFont raw = QRawFont::fromFont(font, QFontDatabase::Japanese);
    return raw.isValid() && raw.supportsCharacter(kProbeCodePoint);
}

} // namespace maru::test
