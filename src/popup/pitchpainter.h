// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The pitch-accent contour drawn over a reading, and the location of the reading runs inside a
// laid-out QTextDocument.
#pragma once

#include <QList>
#include <QPolygonF>
#include <QRectF>
#include <QString>

class QFont;
class QColor;
class QPainter;
class QTextDocument;

namespace maru::popup
{

// One reading run of the rendered document, located by the pitch anchor renderHtml() put on
// it.
struct ReadingSpan
{
    // Index into PopupModel::entries.
    int entryIndex = -1;
    // Index into Entry::readings and into Entry::pitchPositions.
    int readingIndex = -1;
    // The characters of this run. A reading wrapped across two lines yields one span per line,
    // each carrying its own part of the text.
    QString reading;
    // The run's rectangle in document coordinates, which is the block position plus the line
    // offset. Its height is the line's height, which the tallest run on the line sets. A
    // viewport that scrolls subtracts its scroll offsets from it.
    QRectF rect;
    // The baseline of the line the run sits on, in document coordinates. The contour is drawn
    // between the reading font's ascent above it and the baseline itself, which keeps the
    // contour on the reading's own glyphs rather than on the line box a taller headword sets.
    qreal baseline = 0.0;
};

// Every reading run of a document renderHtml() produced, in document order.
//
// The reading spans are located by anchor name rather than by a custom QTextCharFormat
// property, because a document built with QTextDocument::setHtml() carries only what the HTML
// parser writes into a char format, and the parser writes no user-defined property. An anchor
// name survives setHtml() as QTextCharFormat::anchorNames(), which is what this walk reads.
// The document has to have been laid out, which QTextDocument::setTextWidth() forces.
[[nodiscard]] QList<ReadingSpan> readingSpans(const QTextDocument &document);

// The rectangles of the readings of one entry, ordered by reading index. A reading wrapped
// across two lines contributes two rectangles.
[[nodiscard]] QList<QRectF> readingRects(const QTextDocument &document, int entryIndex);

// The reading split into mora units, a small combining kana glued to the character before it,
// which is JL's JapaneseUtils.SmallCombiningKanaSet rule
// (JL.Windows/GUI/Popup/PitchAccentDecorator.cs line 77). jp::combinedForm() implements the
// same rule for the lookup path; the two stay separate so popup/ links no other module.
[[nodiscard]] QList<QStringView> moraUnits(QStringView reading);

// The advance of each mora unit, scaled so the units sum to totalWidth. The scale corrects for
// the difference between QFontMetricsF advances and the width QTextLine laid the run out at,
// which kerning and font fallback both introduce. A totalWidth of 0 or less returns the
// unscaled advances.
[[nodiscard]] QList<qreal> moraWidths(QStringView reading, const QFont &font, qreal totalWidth);

// JL's overline and downstep polyline over the given mora widths, in the coordinates of rect.
// position is the mora index of the downstep: 0 is heiban, which rises after the first mora
// and never falls; 1 is atamadaka, which starts high and falls after the first mora.
[[nodiscard]] QPolygonF pitchPolyline(const QRectF &rect, const QList<qreal> &moraWidths, quint8 position);

// The same polyline, with the mora widths measured from the reading and the font.
[[nodiscard]] QPolygonF pitchPolyline(const QRectF &rect, QStringView reading, quint8 position, const QFont &font);

// Strokes the contour of one reading. dotted selects a dotted pen, which marks a position a
// dictionary reports for the headword rather than for this reading.
void paintPitch(QPainter &painter,
                const QRectF &readingRect,
                QStringView reading,
                quint8 position,
                const QFont &font,
                const QColor &color,
                bool dotted);

} // namespace maru::popup
