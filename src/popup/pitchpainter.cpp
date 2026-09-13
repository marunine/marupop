// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/pitchpainter.h"

#include "popup/renderer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextLine>

namespace maru::popup
{

namespace
{

// JL's JapaneseUtils.SmallCombiningKanaSet (JL.Core/Japanese/JapaneseUtils.cs line 300), the
// eighteen kana that attach to the character before them instead of carrying a mora of their
// own.
bool isSmallCombiningKana(QChar character)
{
    switch (character.unicode()) {
    case u'ァ':
    case u'ィ':
    case u'ゥ':
    case u'ェ':
    case u'ォ':
    case u'ヮ':
    case u'ャ':
    case u'ュ':
    case u'ョ':
    case u'ぁ':
    case u'ぃ':
    case u'ぅ':
    case u'ぇ':
    case u'ぉ':
    case u'ゎ':
    case u'ゃ':
    case u'ゅ':
    case u'ょ':
        return true;
    default:
        return false;
    }
}

// Half of the pen width, so the contour stays inside the rectangle instead of straddling its
// top and bottom edges.
constexpr qreal penWidth = 1.5;

} // namespace

QList<QStringView> moraUnits(QStringView reading)
{
    QList<QStringView> units;
    qsizetype index = 0;
    while (index < reading.size()) {
        // A unit spans two code units in two cases: a surrogate pair, and a character followed
        // by a small combining kana. No small combining kana lives outside the basic plane, so
        // a surrogate pair is never extended past its second code unit.
        const bool hasNext = index + 1 < reading.size();
        const bool pairs =
            hasNext && (reading.at(index).isHighSurrogate() || isSmallCombiningKana(reading.at(index + 1)));
        const qsizetype length = pairs ? 2 : 1;
        units.append(reading.sliced(index, length));
        index += length;
    }
    return units;
}

QList<qreal> moraWidths(QStringView reading, const QFont &font, qreal totalWidth)
{
    const QFontMetricsF metrics{font};
    const QList<QStringView> units = moraUnits(reading);
    QList<qreal> widths;
    widths.reserve(units.size());
    qreal sum = 0.0;
    for (const QStringView &unit : units) {
        const qreal advance = metrics.horizontalAdvance(unit.toString());
        widths.append(advance);
        sum += advance;
    }
    if (totalWidth > 0.0 && sum > 0.0) {
        const qreal scale = totalWidth / sum;
        for (qreal &width : widths) {
            width *= scale;
        }
    }
    return widths;
}

QPolygonF pitchPolyline(const QRectF &rect, const QList<qreal> &moraWidths, quint8 position)
{
    QPolygonF polyline;
    if (moraWidths.isEmpty()) {
        return polyline;
    }

    const qreal high = rect.top() + (penWidth / 2.0);
    const qreal low = rect.bottom() - (penWidth / 2.0);
    qreal x = rect.left();
    bool lowPitch = false;

    const auto append = [&polyline](const QPointF &point) {
        if (polyline.isEmpty() || polyline.constLast() != point) {
            polyline.append(point);
        }
    };

    for (qsizetype index = 0; index < moraWidths.size(); ++index) {
        const qreal width = moraWidths.at(index);
        if (static_cast<int>(position) - 1 == static_cast<int>(index)) {
            // The mora the downstep follows: a high segment, then the fall.
            append(QPointF{x, high});
            append(QPointF{x + width, high});
            append(QPointF{x + width, low});
            lowPitch = true;
        } else if (index == 0) {
            // Every accent but atamadaka starts low and rises after the first mora.
            append(QPointF{x, low});
            append(QPointF{x + width, low});
            append(QPointF{x + width, high});
        } else {
            const qreal y = lowPitch ? low : high;
            append(QPointF{x, y});
            append(QPointF{x + width, y});
        }
        x += width;
    }
    return polyline;
}

QPolygonF pitchPolyline(const QRectF &rect, QStringView reading, quint8 position, const QFont &font)
{
    return pitchPolyline(rect, moraWidths(reading, font, rect.width()), position);
}

void paintPitch(QPainter &painter,
                const QRectF &readingRect,
                QStringView reading,
                quint8 position,
                const QFont &font,
                const QColor &color,
                bool dotted)
{
    const QPolygonF polyline = pitchPolyline(readingRect, reading, position, font);
    if (polyline.size() < 2) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen{color, penWidth};
    pen.setStyle(dotted ? Qt::DotLine : Qt::SolidLine);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(polyline);
    painter.restore();
}

QList<ReadingSpan> readingSpans(const QTextDocument &document)
{
    QList<ReadingSpan> spans;
    // QTextDocument lays a block out on demand, and QTextBlock::layout() reports zero lines
    // until it has. Reading the document size is what forces the layout of every block.
    Q_UNUSED(document.size())
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (layout == nullptr || layout->lineCount() == 0) {
            continue;
        }
        const QPointF blockPosition = layout->position();
        const QString blockText = block.text();

        for (QTextBlock::iterator iterator = block.begin(); !iterator.atEnd(); ++iterator) {
            const QTextFragment fragment = iterator.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            int entryIndex = -1;
            int readingIndex = -1;
            int length = 0;
            bool matched = false;
            const QStringList names = fragment.charFormat().anchorNames();
            for (const QString &name : names) {
                if (parsePitchAnchorName(name, &entryIndex, &readingIndex, &length)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }

            // QTextDocument::setHtml() writes an anchor name onto the first character of the
            // run alone, so the run's end comes from the length the anchor name carries rather
            // than from the fragment's own length.
            const int start = fragment.position() - block.position();
            const int end = qMin(start + length, static_cast<int>(blockText.size()));
            int cursor = start;
            // A run wrapped across two lines yields one span per line, since a polyline cannot
            // span a line break.
            while (cursor < end) {
                const QTextLine line = layout->lineForTextPosition(cursor);
                if (!line.isValid()) {
                    break;
                }
                const int lineEnd = qMin(end, line.textStart() + line.textLength());
                if (lineEnd <= cursor) {
                    break;
                }
                const qreal left = line.cursorToX(cursor);
                const qreal right = line.cursorToX(lineEnd);
                ReadingSpan span;
                span.entryIndex = entryIndex;
                span.readingIndex = readingIndex;
                span.reading = blockText.mid(cursor, lineEnd - cursor);
                span.rect = QRectF{blockPosition.x() + qMin(left, right),
                                   blockPosition.y() + line.y(),
                                   qAbs(right - left),
                                   line.height()};
                span.baseline = blockPosition.y() + line.y() + line.ascent();
                spans.append(span);
                cursor = lineEnd;
            }
        }
    }
    return spans;
}

QList<QRectF> readingRects(const QTextDocument &document, int entryIndex)
{
    QList<QRectF> rects;
    const QList<ReadingSpan> spans = readingSpans(document);
    for (const ReadingSpan &span : spans) {
        if (span.entryIndex == entryIndex) {
            rects.append(span.rect);
        }
    }
    return rects;
}

} // namespace maru::popup
