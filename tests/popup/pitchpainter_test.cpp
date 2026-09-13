// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The mora split, the contour geometry and the reading runs popup::readingSpans() locates in a
// rendered document.
#include "popup/entrymodel.h"
#include "popup/pitchpainter.h"
#include "popup/renderer.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"

#include <QApplication>
#include <QTextDocument>

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::popup;

namespace
{

QStringList unitStrings(QStringView reading)
{
    QStringList strings;
    const QList<QStringView> units = moraUnits(reading);
    for (const QStringView &unit : units) {
        strings.append(unit.toString());
    }
    return strings;
}

// Three mora units of ten logical pixels each, which makes every expected point an integer.
const QList<qreal> threeMorae{10.0, 10.0, 10.0};
const QRectF readingRect{0.0, 0.0, 30.0, 20.0};

// paintPitch() insets the contour by half of its 1.5 pixel pen.
constexpr qreal high = 0.75;
constexpr qreal low = 19.25;

} // namespace

TEST(PitchPainterTest, gluesASmallCombiningKanaToTheCharacterBeforeIt)
{
    EXPECT_EQ(unitStrings(u"きょうしつ"),
              QStringList({QStringLiteral("きょ"), QStringLiteral("う"), QStringLiteral("し"), QStringLiteral("つ")}));
    EXPECT_EQ(unitStrings(u"にほん"), QStringList({QStringLiteral("に"), QStringLiteral("ほ"), QStringLiteral("ん")}));
    // Katakana small kana follow the same rule.
    EXPECT_EQ(unitStrings(u"シャツ"), QStringList({QStringLiteral("シャ"), QStringLiteral("ツ")}));
    EXPECT_TRUE(unitStrings(u"").isEmpty());
}

TEST(PitchPainterTest, drawsHeibanAsARiseThatNeverFalls)
{
    const QPolygonF polyline = pitchPolyline(readingRect, threeMorae, 0);
    ASSERT_EQ(polyline.size(), 5);
    EXPECT_EQ(polyline.at(0), QPointF(0.0, low));
    EXPECT_EQ(polyline.at(1), QPointF(10.0, low));
    EXPECT_EQ(polyline.at(2), QPointF(10.0, high));
    EXPECT_EQ(polyline.at(3), QPointF(20.0, high));
    EXPECT_EQ(polyline.at(4), QPointF(30.0, high));
}

TEST(PitchPainterTest, drawsAtamadakaAsAFallAfterTheFirstMora)
{
    const QPolygonF polyline = pitchPolyline(readingRect, threeMorae, 1);
    ASSERT_EQ(polyline.size(), 5);
    EXPECT_EQ(polyline.at(0), QPointF(0.0, high));
    EXPECT_EQ(polyline.at(1), QPointF(10.0, high));
    EXPECT_EQ(polyline.at(2), QPointF(10.0, low));
    EXPECT_EQ(polyline.at(3), QPointF(20.0, low));
    EXPECT_EQ(polyline.at(4), QPointF(30.0, low));
}

TEST(PitchPainterTest, drawsNakadakaAsARiseThenAFall)
{
    const QPolygonF polyline = pitchPolyline(readingRect, threeMorae, 2);
    ASSERT_EQ(polyline.size(), 6);
    EXPECT_EQ(polyline.at(0), QPointF(0.0, low));
    EXPECT_EQ(polyline.at(1), QPointF(10.0, low));
    EXPECT_EQ(polyline.at(2), QPointF(10.0, high));
    EXPECT_EQ(polyline.at(3), QPointF(20.0, high));
    EXPECT_EQ(polyline.at(4), QPointF(20.0, low));
    EXPECT_EQ(polyline.at(5), QPointF(30.0, low));
}

TEST(PitchPainterTest, drawsOdakaAsARiseThatFallsAfterTheLastMora)
{
    const QPolygonF polyline = pitchPolyline(readingRect, threeMorae, 3);
    ASSERT_EQ(polyline.size(), 6);
    EXPECT_EQ(polyline.at(0), QPointF(0.0, low));
    EXPECT_EQ(polyline.at(1), QPointF(10.0, low));
    EXPECT_EQ(polyline.at(2), QPointF(10.0, high));
    EXPECT_EQ(polyline.at(3), QPointF(20.0, high));
    EXPECT_EQ(polyline.at(4), QPointF(30.0, high));
    EXPECT_EQ(polyline.at(5), QPointF(30.0, low));
}

TEST(PitchPainterTest, drawsNothingForAnEmptyReading)
{
    EXPECT_TRUE(pitchPolyline(readingRect, QList<qreal>{}, 0).isEmpty());
}

TEST(PitchPainterTest, scalesTheMoraAdvancesOntoTheLaidOutWidth)
{
    QFont font;
    font.setPointSize(14);
    const QList<qreal> widths = moraWidths(u"にほん", font, 60.0);
    ASSERT_EQ(widths.size(), 3);
    qreal sum = 0.0;
    for (const qreal width : widths) {
        EXPECT_GT(width, 0.0);
        sum += width;
    }
    EXPECT_NEAR(sum, 60.0, 0.001);

    // A total width of zero leaves the font metrics unscaled.
    const QList<qreal> unscaled = moraWidths(u"にほん", font, 0.0);
    ASSERT_EQ(unscaled.size(), 3);
    EXPECT_GT(unscaled.at(0), 0.0);
}

TEST(PitchPainterTest, locatesEveryReadingRunByItsAnchor)
{
    PopupModel model;
    Entry first;
    first.headword = QStringLiteral("日本");
    first.readings = QStringList{QStringLiteral("にほん"), QStringLiteral("にっぽん")};
    first.pitchPositions = QList<std::optional<quint8>>{std::optional<quint8>{3}, std::nullopt};
    model.entries.append(first);
    Entry second;
    second.headword = QStringLiteral("読む");
    second.readings = QStringList{QStringLiteral("よむ")};
    second.pitchPositions = QList<std::optional<quint8>>{std::optional<quint8>{1}};
    model.entries.append(second);

    RenderOptions options;
    options.showPitchAccent = true;
    QTextDocument document;
    document.setHtml(renderHtml(model, options, Theme{}));
    document.setTextWidth(600.0);

    const QList<ReadingSpan> spans = readingSpans(document);
    ASSERT_EQ(spans.size(), 3);
    EXPECT_EQ(spans.at(0).entryIndex, 0);
    EXPECT_EQ(spans.at(0).readingIndex, 0);
    EXPECT_EQ(spans.at(0).reading, QStringLiteral("にほん"));
    EXPECT_GT(spans.at(0).rect.width(), 0.0);
    EXPECT_GT(spans.at(0).rect.height(), 0.0);

    EXPECT_EQ(spans.at(1).readingIndex, 1);
    EXPECT_EQ(spans.at(1).reading, QStringLiteral("にっぽん"));
    // The second reading starts to the right of the first.
    EXPECT_GT(spans.at(1).rect.left(), spans.at(0).rect.left());

    EXPECT_EQ(spans.at(2).entryIndex, 1);
    EXPECT_EQ(spans.at(2).reading, QStringLiteral("よむ"));
    // The second entry is on a line below the first.
    EXPECT_GT(spans.at(2).rect.top(), spans.at(0).rect.top());

    // readingRects() selects one entry out of the same walk.
    EXPECT_EQ(readingRects(document, 0).size(), 2);
    EXPECT_EQ(readingRects(document, 1).size(), 1);
    EXPECT_TRUE(readingRects(document, 2).isEmpty());
}

TEST(PitchPainterTest, locatesNoReadingRunWhilePitchAccentIsOff)
{
    PopupModel model;
    Entry entry;
    entry.headword = QStringLiteral("日本");
    entry.readings = QStringList{QStringLiteral("にほん")};
    model.entries.append(entry);

    RenderOptions options;
    options.showPitchAccent = false;
    QTextDocument document;
    document.setHtml(renderHtml(model, options, Theme{}));
    document.setTextWidth(600.0);
    EXPECT_TRUE(readingSpans(document).isEmpty());
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
