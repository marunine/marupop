// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// ocr::applyCorrections() against hand-written rules, and ocr::applyMeikiCorrections() against
// the standalone scalar reference interpreter over tests/data/ocr/meikicorrections_cases.json, which
// tools/gen-meiki-correction-cases.py writes.
#include "ocr/meikicorrections.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <gtest/gtest.h>

using namespace maru::ocr;
using namespace Qt::Literals::StringLiterals;

namespace
{

// One horizontal line of 10-pixel characters at x = 10 * index.
TextLine lineOf(const QString &text, float confidence, bool vertical = false)
{
    TextLine line;
    line.text = text;
    line.vertical = vertical;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QRect box =
            vertical ? QRect(0, static_cast<int>(i) * 10, 10, 10) : QRect(static_cast<int>(i) * 10, 0, 10, 10);
        line.chars.append(CharBox{.codePoint = text.at(i).unicode(), .box = box, .confidence = confidence});
        line.box = line.box.united(box);
    }
    line.confidence = confidence;
    return line;
}

// A rule with no context, no mode restriction and no gate, which each test then narrows.
CorrectionRule rule(std::u16string_view find, std::u16string_view replace)
{
    CorrectionRule out;
    out.find = find;
    out.replace = replace;
    return out;
}

constexpr CorrectionToken literal(char16_t character)
{
    return CorrectionToken{.kind = CorrectionToken::Kind::Literal, .character = character};
}

constexpr CorrectionToken kind(CorrectionToken::Kind value)
{
    return CorrectionToken{.kind = value, .character = 0};
}

} // namespace

TEST(MeikiCorrections, tableHoldsTheOperationalRules)
{
    EXPECT_EQ(meikiCorrectionRules().size(), 74U);
    EXPECT_EQ(meikiCorrectionRules().front().find, u"…");
    EXPECT_EQ(meikiCorrectionRules().back().replace, u"だ");
}

TEST(MeikiCorrections, classifiesContextCharacters)
{
    EXPECT_EQ(correctionClassOf(QChar(u'ぁ')), CorrectionToken::Kind::Hiragana);
    EXPECT_EQ(correctionClassOf(QChar(u'ー')), CorrectionToken::Kind::Katakana);
    EXPECT_EQ(correctionClassOf(QChar(u'ｱ')), CorrectionToken::Kind::Katakana);
    EXPECT_EQ(correctionClassOf(QChar(u'々')), CorrectionToken::Kind::Kanji);
    EXPECT_EQ(correctionClassOf(QChar(u'〇')), CorrectionToken::Kind::Kanji);
    EXPECT_EQ(correctionClassOf(QChar(u'９')), CorrectionToken::Kind::Digit);
    EXPECT_EQ(correctionClassOf(QChar(u'①')), CorrectionToken::Kind::Digit);
    EXPECT_EQ(correctionClassOf(QChar(u'Ａ')), CorrectionToken::Kind::Latin);
    EXPECT_EQ(correctionClassOf(QChar(u'é')), CorrectionToken::Kind::Other);
    EXPECT_EQ(correctionClassOf(QChar(u'　')), CorrectionToken::Kind::Space);
    EXPECT_EQ(correctionClassOf(QChar(u'�')), CorrectionToken::Kind::Other);
}

TEST(MeikiCorrections, gatesOnTheConfidenceOfEveryCharacterOfTheOccurrence)
{
    std::array rules{rule(u"一", u"「")};
    rules[0].maxConfidence = 0.7;
    TextLine low = lineOf(u"一あ"_s, 0.5F);
    EXPECT_EQ(applyCorrections(low, rules), 1);
    EXPECT_EQ(low.text, u"「あ"_s);
    EXPECT_EQ(low.chars.at(0).codePoint, U'「');

    TextLine high = lineOf(u"一あ"_s, 0.8F);
    EXPECT_EQ(applyCorrections(high, rules), 0);
    EXPECT_EQ(high.text, u"一あ"_s);
}

TEST(MeikiCorrections, matchesContextNearestLastBeforeAndNearestFirstAfter)
{
    // The rule "て -> で after な い": before is [な, い] with い adjacent.
    std::array rules{rule(u"て", u"で")};
    rules[0].before = {literal(u'な'), literal(u'い')};
    rules[0].beforeCount = 2;
    rules[0].after = {kind(CorrectionToken::Kind::LineEdge)};
    rules[0].afterCount = 1;
    TextLine hit = lineOf(u"しないて"_s, 1.0F);
    EXPECT_EQ(applyCorrections(hit, rules), 1);
    EXPECT_EQ(hit.text, u"しないで"_s);

    TextLine reversed = lineOf(u"しいなて"_s, 1.0F);
    EXPECT_EQ(applyCorrections(reversed, rules), 0);

    TextLine notAtEnd = lineOf(u"ないてる"_s, 1.0F);
    EXPECT_EQ(applyCorrections(notAtEnd, rules), 0);
}

TEST(MeikiCorrections, restrictsARuleToItsWritingMode)
{
    std::array rules{rule(u"一", u"「")};
    rules[0].mode = CorrectionMode::Vertical;
    TextLine horizontal = lineOf(u"一"_s, 0.1F, false);
    EXPECT_EQ(applyCorrections(horizontal, rules), 0);
    TextLine vertical = lineOf(u"一"_s, 0.1F, true);
    EXPECT_EQ(applyCorrections(vertical, rules), 1);
}

TEST(MeikiCorrections, splitsTheUnionBoxAlongTheReadingAxisOnALengthChange)
{
    const std::array rules{rule(u"....", u"……")};
    TextLine line = lineOf(u"あ....い"_s, 0.4F);
    ASSERT_EQ(applyCorrections(line, rules), 1);
    ASSERT_EQ(line.text, u"あ……い"_s);
    ASSERT_EQ(line.chars.size(), line.text.size());
    // The four dots span x = 10 to 50; the two ellipses share it.
    EXPECT_EQ(line.chars.at(1).box, QRect(10, 0, 20, 10));
    EXPECT_EQ(line.chars.at(2).box, QRect(30, 0, 20, 10));
    EXPECT_EQ(line.chars.at(3).box, QRect(50, 0, 10, 10));

    TextLine vertical = lineOf(u"あ....い"_s, 0.4F, true);
    ASSERT_EQ(applyCorrections(vertical, rules), 1);
    EXPECT_EQ(vertical.chars.at(1).box, QRect(0, 10, 10, 20));
}

TEST(MeikiCorrections, takesTheLowestConfidenceOfTheReplacedSpan)
{
    const std::array rules{rule(u"コビー", u"コピー")};
    TextLine line = lineOf(u"コビー"_s, 0.9F);
    line.chars[1].confidence = 0.2F;
    ASSERT_EQ(applyCorrections(line, rules), 1);
    for (const CharBox &box : std::as_const(line.chars))
        EXPECT_FLOAT_EQ(box.confidence, 0.2F);
}

TEST(MeikiCorrections, recomputesTheLineBoxAfterADeletion)
{
    std::array rules{rule(u"を", u"")};
    rules[0].after = {kind(CorrectionToken::Kind::LineEdge)};
    rules[0].afterCount = 1;
    TextLine line = lineOf(u"本を"_s, 0.2F);
    ASSERT_EQ(applyCorrections(line, rules), 1);
    EXPECT_EQ(line.text, u"本"_s);
    EXPECT_EQ(line.box, QRect(0, 0, 10, 10));

    TextLine only = lineOf(u"を"_s, 0.2F);
    ASSERT_EQ(applyCorrections(only, rules), 1);
    EXPECT_TRUE(only.chars.isEmpty());
    EXPECT_TRUE(only.text.isEmpty());
}

TEST(MeikiCorrections, findsOccurrencesLeftToRightWithoutOverlap)
{
    const std::array rules{rule(u"ああ", u"い")};
    TextLine line = lineOf(u"あああ"_s, 1.0F);
    ASSERT_EQ(applyCorrections(line, rules), 1);
    EXPECT_EQ(line.text, u"いあ"_s);
}

// Every case of the fixture, through the generated table, against the reference output.
TEST(MeikiCorrections, agreesWithTheReferenceReaderOnEveryFixtureCase)
{
    QFile file(QStringLiteral(MARUPOP_OCR_TEST_DATA_DIR) + u"/meikicorrections_cases.json"_s);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << file.fileName().toStdString();
    const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).object().value(u"cases").toArray();
    ASSERT_GT(cases.size(), 1000);

    int rewritten = 0;
    for (const auto &value : cases) {
        const QJsonObject item = value.toObject();
        TextLine line;
        line.text = item.value(u"text").toString();
        line.vertical = item.value(u"vertical").toBool();
        const QJsonArray confs = item.value(u"confs").toArray();
        const QJsonArray boxes = item.value(u"boxes").toArray();
        ASSERT_EQ(confs.size(), line.text.size());
        for (qsizetype i = 0; i < line.text.size(); ++i) {
            const QJsonArray box = boxes.at(i).toArray();
            line.chars.append(CharBox{.codePoint = line.text.at(i).unicode(),
                                      .box = QRect(QPoint(box.at(0).toInt(), box.at(1).toInt()),
                                                   QPoint(box.at(2).toInt() - 1, box.at(3).toInt() - 1)),
                                      .confidence = static_cast<float>(confs.at(i).toDouble())});
        }
        const QString input = line.text;
        (void)applyMeikiCorrections(line);

        const QJsonObject expected = item.value(u"expected").toObject();
        const QString expectedText = expected.value(u"text").toString();
        ASSERT_EQ(line.text, expectedText) << "input " << input.toStdString();
        ASSERT_EQ(line.chars.size(), line.text.size());
        const QJsonArray expectedBoxes = expected.value(u"boxes").toArray();
        const QJsonArray expectedConfs = expected.value(u"confs").toArray();
        for (qsizetype i = 0; i < line.chars.size(); ++i) {
            const QJsonArray box = expectedBoxes.at(i).toArray();
            const QRect &got = line.chars.at(i).box;
            EXPECT_EQ(got.x(), box.at(0).toInt()) << input.toStdString() << " @" << i;
            EXPECT_EQ(got.y(), box.at(1).toInt()) << input.toStdString() << " @" << i;
            EXPECT_EQ(got.x() + got.width(), box.at(2).toInt()) << input.toStdString() << " @" << i;
            EXPECT_EQ(got.y() + got.height(), box.at(3).toInt()) << input.toStdString() << " @" << i;
            EXPECT_EQ(line.chars.at(i).confidence, static_cast<float>(expectedConfs.at(i).toDouble()));
        }
        if (expectedText != input)
            ++rewritten;
    }
    // The fixture exercises the rules rather than passing lines through untouched.
    EXPECT_GT(rewritten, 300);
}
