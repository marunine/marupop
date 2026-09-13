// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/grouping.h"

#include "jp/japanese.h"

#include <QChar>

#include <algorithm>

namespace maru::ocr
{

namespace
{

// One line with the two properties grouping decides on: the union of its character boxes, and
// the orientation recomputed from that union.
struct GroupLine
{
    const TextLine *source = nullptr;
    QRect box;
    bool vertical = false;
};

QRect charUnion(const TextLine &line)
{
    QRect box;
    for (const CharBox &character : line.chars) {
        box = box.united(character.box);
    }
    return box;
}

double centreX(QRect box)
{
    return box.x() + (box.width() / 2.0);
}

double centreY(QRect box)
{
    return box.y() + (box.height() / 2.0);
}

// The paragraph built from one group of lines, in reading order: right to left for vertical
// lines, top to bottom for horizontal lines. The concatenation carries no separator, so the
// lookup can read a word across a line break.
Paragraph mergeLines(QList<GroupLine> group, bool vertical)
{
    if (vertical) {
        std::ranges::stable_sort(group, [](const GroupLine &left, const GroupLine &right) {
            return centreX(left.box) > centreX(right.box);
        });
    } else {
        std::ranges::stable_sort(group, [](const GroupLine &left, const GroupLine &right) {
            return centreY(left.box) < centreY(right.box);
        });
    }

    Paragraph paragraph;
    paragraph.vertical = vertical;
    for (const GroupLine &line : std::as_const(group)) {
        paragraph.lineStarts.append(static_cast<int>(paragraph.text.size()));
        paragraph.text.append(line.source->text);
        paragraph.chars.append(line.source->chars);
        paragraph.box = paragraph.box.united(line.box);
    }
    return paragraph;
}

// Transitive closure over the adjacency relation. The reference implementation restarts its
// scan whenever a line joins the group, which produces the same sets as this worklist.
QList<QList<GroupLine>> groupByAdjacency(const QList<GroupLine> &lines, bool vertical)
{
    QList<QList<GroupLine>> groups;
    QList<bool> taken(lines.size(), false);
    for (qsizetype seed = 0; seed < lines.size(); ++seed) {
        if (taken.at(seed)) {
            continue;
        }
        taken[seed] = true;
        QList<GroupLine> group{lines.at(seed)};
        QList<qsizetype> pending{seed};
        while (!pending.isEmpty()) {
            const qsizetype current = pending.takeLast();
            for (qsizetype candidate = 0; candidate < lines.size(); ++candidate) {
                if (taken.at(candidate)) {
                    continue;
                }
                if (linesAdjacent(lines.at(current).box, lines.at(candidate).box, vertical)) {
                    taken[candidate] = true;
                    group.append(lines.at(candidate));
                    pending.append(candidate);
                }
            }
        }
        groups.append(group);
    }
    return groups;
}

// Lines whose cross-axis extent is smaller than 0.65 times the median extent of their
// orientation. A single line of an orientation is always main text, because one value has no
// meaningful median to fall below.
void classifyFurigana(const QList<GroupLine> &lines, bool vertical, QList<GroupLine> &main, QList<GroupLine> &furigana)
{
    QList<GroupLine> selected;
    for (const GroupLine &line : lines) {
        if (line.vertical == vertical) {
            selected.append(line);
        }
    }
    if (selected.size() < 2) {
        main.append(selected);
        return;
    }
    QList<double> extents;
    extents.reserve(selected.size());
    for (const GroupLine &line : std::as_const(selected)) {
        extents.append(vertical ? line.box.width() : line.box.height());
    }
    const double limit = median(extents) * kFuriganaSizeRatio;
    for (const GroupLine &line : std::as_const(selected)) {
        const double extent = vertical ? line.box.width() : line.box.height();
        if (extent < limit) {
            furigana.append(line);
        } else {
            main.append(line);
        }
    }
}

} // namespace

bool containsJapanese(QStringView text)
{
    return std::ranges::any_of(text, [](QChar character) {
        const char32_t code = character.unicode();
        return jp::isHiragana(code) || jp::isKatakana(code) || jp::isKanji(code);
    });
}

bool linesAdjacent(QRect first, QRect second, bool vertical)
{
    if (vertical) {
        const int overlap =
            std::min(first.y() + first.height(), second.y() + second.height()) - std::max(first.y(), second.y());
        const double shorter = std::min(first.height(), second.height());
        const double wider = std::max(first.width(), second.width());
        return overlap > kAdjacencyOverlapRatio * shorter &&
               std::abs(centreX(first) - centreX(second)) < kAdjacencyCentreRatio * wider;
    }
    const int overlap =
        std::min(first.x() + first.width(), second.x() + second.width()) - std::max(first.x(), second.x());
    const double shorter = std::min(first.width(), second.width());
    const double taller = std::max(first.height(), second.height());
    return overlap > kAdjacencyOverlapRatio * shorter &&
           std::abs(centreY(first) - centreY(second)) < kAdjacencyCentreRatio * taller;
}

double median(QList<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const qsizetype middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values.at(middle);
    }
    return (values.at(middle - 1) + values.at(middle)) / 2.0;
}

QList<Paragraph> groupLines(const QList<TextLine> &lines, QSize imageSize)
{
    const QRect bounds{QPoint{0, 0}, imageSize};
    QList<GroupLine> prepared;
    prepared.reserve(lines.size());
    for (const TextLine &line : lines) {
        if (line.text.isEmpty() || line.chars.isEmpty() || !containsJapanese(line.text)) {
            continue;
        }
        GroupLine prepare;
        prepare.source = &line;
        prepare.box = charUnion(line);
        if (bounds.isValid()) {
            prepare.box = prepare.box.intersected(bounds);
        }
        // The orientation is recomputed from the union of the character boxes rather than
        // taken from TextLine::vertical: a two-character horizontal line is taller than it is
        // wide under the h > w rule the recognition pass used.
        prepare.vertical = prepare.box.width() * kVerticalAspectRatio < prepare.box.height();
        prepared.append(prepare);
    }

    QList<GroupLine> verticalMain;
    QList<GroupLine> horizontalMain;
    QList<GroupLine> furigana;
    classifyFurigana(prepared, true, verticalMain, furigana);
    classifyFurigana(prepared, false, horizontalMain, furigana);

    QList<Paragraph> paragraphs;
    for (const QList<GroupLine> &group : groupByAdjacency(verticalMain, true)) {
        paragraphs.append(mergeLines(group, true));
    }
    for (const QList<GroupLine> &group : groupByAdjacency(horizontalMain, false)) {
        paragraphs.append(mergeLines(group, false));
    }
    // A furigana line is its own paragraph: it reads on its own and must not interleave with
    // the base text it annotates.
    for (const GroupLine &line : std::as_const(furigana)) {
        paragraphs.append(mergeLines({line}, line.vertical));
    }
    return paragraphs;
}

} // namespace maru::ocr
