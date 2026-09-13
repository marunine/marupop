// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Covers Japanese text conversion, character classes and normalization.
// The section 10.5 and 10.4 rows are transcribed from JL (Apache-2.0),
// JL.Core.Tests/KanaTests.cs and JL.Core.Tests/UtilsTests.cs at commit 85ae02ee.
#include "jp/japanese.h"

#include <QList>
#include <QString>

#include <gtest/gtest.h>

using namespace Qt::StringLiterals;
using maru::jp::combinedForm;
using maru::jp::combinedFormLength;
using maru::jp::containsJapaneseCharacters;
using maru::jp::containsKana;
using maru::jp::countNonConsecutiveLongVowelMarks;
using maru::jp::findExpressionBoundary;
using maru::jp::findSentence;
using maru::jp::firstCharacterIfKanji;
using maru::jp::hiraganaToKatakana;
using maru::jp::isHiragana;
using maru::jp::isJapanese;
using maru::jp::isKanji;
using maru::jp::isKatakana;
using maru::jp::isPunctuation;
using maru::jp::katakanaToHiragana;
using maru::jp::normalizeLongVowelMark;
using maru::jp::normalizeText;

namespace
{

struct NormalizeCase
{
    QStringView input;
    QStringView expected;
};

void expectNormalized(const NormalizeCase &testCase)
{
    EXPECT_EQ(normalizeText(testCase.input.toString()), testCase.expected.toString())
        << "input: " << testCase.input.toString().toStdString();
}

void expectNormalizedAll(std::initializer_list<NormalizeCase> cases)
{
    for (const NormalizeCase &testCase : cases)
        expectNormalized(testCase);
}

} // namespace

// Section 10.5: the five cases JL's own KanaTests pins.
TEST(JapaneseNormalizeText, JlKanaTests)
{
    expectNormalizedAll({
        {.input = u"ア", .expected = u"あ"},
        {.input = u"㋕", .expected = u"か"},
        {.input = u"㌀", .expected = u"あぱーと"},
    });
}

// JL marks the ㋿ case [Explicit] because the decomposition of U+32FF depends on the platform's
// ICU tables. The assertion is reported rather than failed for the same reason.
TEST(JapaneseNormalizeText, SquareEraNameIsPlatformDependent)
{
    const QString actual = normalizeText(u"㋿"_s);
    if (actual != u"令和"_s) {
        GTEST_SKIP() << "U+32FF normalizes to " << actual.toStdString()
                     << " on this Unicode data, not 令和; JL marks the same case [Explicit]";
    }
    EXPECT_EQ(actual, u"令和"_s);
}

TEST(JapaneseNormalizeText, JlKanaTestsLongVowelMark)
{
    const QList<QString> variants = normalizeLongVowelMark(normalizeText(u"オー"_s));
    ASSERT_EQ(variants.size(), 2);
    EXPECT_EQ(variants.at(0), u"おお"_s);
    EXPECT_EQ(variants.at(1), u"おう"_s);
}

TEST(JapaneseNormalizeText, KanaAndCompatibilityForms)
{
    expectNormalizedAll({
        {.input = u"カタカナ", .expected = u"かたかな"},
        {.input = u"ｶﾞ", .expected = u"が"},
        {.input = u"ﾜ", .expected = u"わ"},
        {.input = u"㍿", .expected = u"株式会社"},
        {.input = u"ＯＬ", .expected = u"OL"},
        {.input = u"～", .expected = u"~"}, // U+FF5E FULLWIDTH TILDE
        {.input = u"vs", .expected = u"VS"},
        {.input = u"MaruPop", .expected = u"MARUPOP"},
        {.input = u"⼀", .expected = u"一"}, // U+2F00 KANGXI RADICAL ONE
        {.input = u"龍", .expected = u"竜"},
        {.input = u"ゐ", .expected = u"い"},
        {.input = u"ゑ", .expected = u"え"},
        {.input = u"ヴ", .expected = u"ゔ"},
        {.input = u"ヰ", .expected = u"い"},
        {.input = u"ヱ", .expected = u"え"},
        {.input = u"ヵ", .expected = u"ゕ"},
        {.input = u"ヶ", .expected = u"ゖ"},
        {.input = u"ッ", .expected = u"っ"},
    });
}

TEST(JapaneseNormalizeText, SupplementaryPlaneMappings)
{
    // U+1B002 HENTAIGANA LETTER A, and the two kyuujitai forms JL keeps outside the BMP table.
    expectNormalizedAll({
        {.input = u"\U0001B002", .expected = u"あ"},
        {.input = u"\U0001B003か", .expected = u"あか"},
        {.input = u"\U00026936", .expected = u"致"},
    });
}

TEST(JapaneseNormalizeText, VariationSelectorsAreDropped)
{
    // U+FE00-U+FE0F and the variation selector supplement U+E0100-U+E01EF both disappear.
    expectNormalizedAll({
        {.input = u"葛\U000E0100", .expected = u"葛"},
        {.input = u"葛︀", .expected = u"葛"},
        {.input = u"あ️い", .expected = u"あい"},
    });
}

TEST(JapaneseNormalizeText, IterationMarks)
{
    expectNormalizedAll({
        {.input = u"人々", .expected = u"人人"},
        {.input = u"人〻", .expected = u"人人"},
        {.input = u"いすゝ", .expected = u"いすす"},
        {.input = u"みすゞ", .expected = u"みすず"},
        {.input = u"いすヽ", .expected = u"いすす"},
        {.input = u"みすヾ", .expected = u"みすず"},
        {.input = u"々あ", .expected = u"々あ"}, // nothing emitted yet, so the mark stays
        {.input = u"ゞあ", .expected = u"ゞあ"},
        {.input = u"\U00020000々", .expected = u"\U00020000\U00020000"}, // a surrogate pair repeats whole
        // NFKC composes U+304B U+3099 into U+304C, so the iteration mark repeats the composed
        // character; the combining-mark branch is reached only by a base with no precomposed form.
        {.input = u"\u304B\u3099\u3005", .expected = u"\u304C\u304C"},
        {.input = u"\u3042\u3099\u3005", .expected = u"\u3042\u3099\u3042\u3099"},
        {.input = u"すゞめ", .expected = u"すずめ"},
        {.input = u"はゞ", .expected = u"はば"},
        {.input = u"あゞ", .expected = u"あゞ"}, // あ has no voiced counterpart, so the mark stays
    });
}

TEST(JapaneseNormalizeText, FusejiFoldToOneCircle)
{
    for (const QStringView fuseji : {u"〇",
                                     u"◯",
                                     u"●",
                                     u"⬤",
                                     u"◎",
                                     u"◉",
                                     u"□",
                                     u"■",
                                     u"×",
                                     u"◇",
                                     u"◆",
                                     u"△",
                                     u"▲",
                                     u"▽",
                                     u"▼",
                                     u"※",
                                     u"*",
                                     u"#"}) {
        EXPECT_EQ(normalizeText(fuseji.toString()), u"○"_s) << "fuseji: " << fuseji.toString().toStdString();
    }
    EXPECT_EQ(normalizeText(u"殺●"_s), u"殺○"_s);
}

TEST(JapaneseNormalizeText, StripCharactersSkipTheFirstAndLastPosition)
{
    for (const QStringView strip : {u" ", u"・", u".", u"·", u"=", u"゠", u"☆", u"★", u"†", u"‡", u"♥", u"♡"}) {
        const QString middle = u"あ"_s + strip.toString() + u"い"_s;
        EXPECT_EQ(normalizeText(middle), u"あい"_s) << "strip: " << strip.toString().toStdString();

        const QString leading = strip.toString() + u"あい"_s;
        EXPECT_EQ(normalizeText(leading), leading) << "leading strip: " << strip.toString().toStdString();

        const QString trailing = u"あい"_s + strip.toString();
        EXPECT_EQ(normalizeText(trailing), trailing) << "trailing strip: " << strip.toString().toStdString();
    }
}

TEST(JapaneseNormalizeText, RepeatedSmallTsuCollapses)
{
    expectNormalizedAll({
        {.input = u"あっっち", .expected = u"あっち"},
        {.input = u"あッッち", .expected = u"あっち"},
        {.input = u"あっっっち", .expected = u"あっち"},
        {.input = u"あっち", .expected = u"あっち"},
    });
}

TEST(JapaneseNormalizeText, TextNeedingNoWorkIsReturnedUnchanged)
{
    const QString text = u"日本語"_s;
    EXPECT_EQ(normalizeText(text), text);
    EXPECT_EQ(normalizeText(QString()), QString());
}

TEST(JapaneseLongVowelMark, CountNonConsecutive)
{
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"らーめん"), 1);
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"とーきょー"), 2);
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"そーゆーこーゆー"), 4);
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"らーーーめん"), 1); // one run, not three
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"ーめん"), 0);       // a leading mark has no vowel
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"にほんご"), 0);
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"かぁ"), 1); // ぁ continues the あ of か
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"きぁ"), 0); // ぁ does not continue the い of き
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"こぅ"), 1); // う is お's alternative
    // The count stops at 4 even with five runs present.
    EXPECT_EQ(countNonConsecutiveLongVowelMarks(u"らーらーらーらーらー"), 4);
}

TEST(JapaneseLongVowelMark, Variants)
{
    EXPECT_EQ(normalizeLongVowelMark(u"らーめん"), QList<QString>({u"らあめん"_s}));
    // ね ends on the vowel え, which forks into ええ and えい just as お forks.
    EXPECT_EQ(normalizeLongVowelMark(u"ねーさん"), QList<QString>({u"ねえさん"_s, u"ねいさん"_s}));
    EXPECT_EQ(normalizeLongVowelMark(u"けーき"), QList<QString>({u"けえき"_s, u"けいき"_s}));
    EXPECT_EQ(normalizeLongVowelMark(u"とーきょー"),
              QList<QString>({u"とおきょお"_s, u"とうきょお"_s, u"とおきょう"_s, u"とうきょう"_s}));
    EXPECT_EQ(normalizeLongVowelMark(u"にほんご"), QList<QString>({u"にほんご"_s}));
    EXPECT_TRUE(normalizeLongVowelMark(QStringView()).isEmpty());
}

TEST(JapaneseClassification, Ranges)
{
    EXPECT_TRUE(isKatakana(u'ア'));
    EXPECT_FALSE(isKatakana(u'あ'));
    EXPECT_TRUE(isKatakana(0xFF71)); // halfwidth ｱ
    EXPECT_TRUE(isKatakana(0x30FF)); // ヿ, the last of the Katakana block
    EXPECT_TRUE(isKatakana(0x31F0)); // ㇰ, Katakana Phonetic Extensions
    EXPECT_TRUE(isKatakana(0x31FF)); // ㇿ, the last of Katakana Phonetic Extensions
    // The four blocks JL's single U+30A0-U+31FF range also accepts. ocr::grouping::
    // containsJapanese() drops a line built from them.
    EXPECT_FALSE(isKatakana(0x3105)); // ㄅ, Bopomofo
    EXPECT_FALSE(isKatakana(0x312F)); // ㄯ, the last of Bopomofo
    EXPECT_FALSE(isKatakana(0x3131)); // ㄱ, Hangul Compatibility Jamo KIYEOK
    EXPECT_FALSE(isKatakana(0x318F)); // the last of Hangul Compatibility Jamo
    EXPECT_FALSE(isKatakana(0x3190)); // ㆐, Kanbun
    EXPECT_FALSE(isKatakana(0x319F)); // ㆟, the last of Kanbun
    EXPECT_FALSE(isKatakana(0x31C0)); // ㇀, CJK Strokes
    EXPECT_FALSE(isKatakana(0x31EF)); // the last of CJK Strokes
    EXPECT_TRUE(isHiragana(u'あ'));
    EXPECT_FALSE(isHiragana(u'ア'));

    EXPECT_TRUE(isKanji(u'日'));
    EXPECT_FALSE(isKanji(u'あ'));
    EXPECT_TRUE(isKanji(0x20000));  // Extension B
    EXPECT_FALSE(isKanji(0x1F600)); // an emoji outside every kanji range

    EXPECT_TRUE(isJapanese(u'日'));
    EXPECT_TRUE(isJapanese(u'あ'));
    EXPECT_TRUE(isJapanese(u'。'));
    EXPECT_FALSE(isJapanese(u'A'));
    EXPECT_TRUE(isJapanese(0x1B002)); // hentaigana

    EXPECT_TRUE(isPunctuation(u'。'));
    EXPECT_TRUE(isPunctuation(u'('));
    EXPECT_TRUE(isPunctuation(u'「'));
    EXPECT_FALSE(isPunctuation(u'あ'));
    EXPECT_FALSE(isPunctuation(u'1'));
}

TEST(JapaneseClassification, ContainsHelpers)
{
    EXPECT_FALSE(containsJapaneseCharacters(u"hello world"));
    EXPECT_TRUE(containsJapaneseCharacters(u"hello 世界"));
    EXPECT_TRUE(containsJapaneseCharacters(u"日本語"));
    // Longer than 15 code units with a leading ASCII run: JL switches to a regex here, and the
    // port keeps the per-code-point scan, which accepts the same text.
    EXPECT_TRUE(containsJapaneseCharacters(u"aaaaaaaaaaaaaaaaaaaa日"));
    EXPECT_FALSE(containsJapaneseCharacters(u"aaaaaaaaaaaaaaaaaaaaaa"));
    EXPECT_TRUE(containsJapaneseCharacters(u"\U00020000")); // astral kanji
    EXPECT_FALSE(containsJapaneseCharacters(QStringView()));

    EXPECT_TRUE(containsKana(u"たべる"));
    EXPECT_TRUE(containsKana(u"タベル"));
    EXPECT_TRUE(containsKana(u"ㇰ")); // U+31F0, Katakana Phonetic Extensions
    EXPECT_FALSE(containsKana(u"日本"));
    EXPECT_FALSE(containsKana(u"abc"));
    // Bopomofo, Hangul Compatibility Jamo, Kanbun and CJK Strokes sit inside JL's single
    // U+3040-U+31FF range and hold no kana.
    EXPECT_FALSE(containsKana(u"ㄅㄆㄇ"));
    EXPECT_FALSE(containsKana(u"한글ㄱㄴㄷ"));
    EXPECT_FALSE(containsKana(u"㆐㆑"));
    EXPECT_FALSE(containsKana(u"㇀㇁"));
}

TEST(JapaneseClassification, FirstCharacterIfKanji)
{
    EXPECT_EQ(firstCharacterIfKanji(u"日本語"), std::optional<QString>(u"日"_s));
    EXPECT_FALSE(firstCharacterIfKanji(u"あいう").has_value());
    EXPECT_FALSE(firstCharacterIfKanji(QStringView()).has_value());

    const std::optional<QString> astral = firstCharacterIfKanji(u"\U00020000あ");
    ASSERT_TRUE(astral.has_value());
    EXPECT_EQ(astral->size(), 2);
    EXPECT_EQ(*astral, QString::fromUcs4(U"\U00020000"));

    EXPECT_FALSE(firstCharacterIfKanji(u"\U0001F600").has_value());
}

TEST(JapaneseSegmentation, FindExpressionBoundary)
{
    EXPECT_EQ(findExpressionBoundary(u"食べる。次", 0), 4);
    EXPECT_EQ(findExpressionBoundary(u"食べる", 0), 3);
    EXPECT_EQ(findExpressionBoundary(u"「食べる」", 0), 1); // the opening bracket terminates
    EXPECT_EQ(findExpressionBoundary(u"食べる」", 0), 4);
    EXPECT_EQ(findExpressionBoundary(u"食べる（次）", 0), 4);
    EXPECT_EQ(findExpressionBoundary(u"食べる！", 0), 4);
    EXPECT_EQ(findExpressionBoundary(u"食べる？", 0), 4);
    EXPECT_EQ(findExpressionBoundary(u"食べる…", 0), 4);
    EXPECT_EQ(findExpressionBoundary(u"食べる。次。", 4), 6); // the scan starts at position
    EXPECT_EQ(findExpressionBoundary(QStringView(), 0), 0);
}

TEST(JapaneseSegmentation, CombinedForm)
{
    const QStringView kyou = u"きょう";
    const QList<QStringView> units = combinedForm(kyou);
    ASSERT_EQ(units.size(), 2);
    EXPECT_EQ(units.at(0), u"きょ");
    EXPECT_EQ(units.at(1), u"う");
    EXPECT_EQ(combinedFormLength(kyou), 2);

    EXPECT_EQ(combinedFormLength(u"ファイル"), 3);
    EXPECT_EQ(combinedForm(u"ファイル").size(), 3);
    EXPECT_EQ(combinedFormLength(u"にほんご"), 4);
    EXPECT_EQ(combinedFormLength(QStringView()), 0);
}

TEST(JapaneseSegmentation, KanaScriptConversion)
{
    EXPECT_EQ(katakanaToHiragana(u"カタカナ"), u"かたかな"_s);
    EXPECT_EQ(katakanaToHiragana(u"ヴヰヱヵヶッヽヾ"), u"ゔゐゑゕゖっゝゞ"_s);
    EXPECT_EQ(katakanaToHiragana(u"日本ア"), u"日本あ"_s);
    EXPECT_EQ(hiraganaToKatakana(u"かたかな"), u"カタカナ"_s);
    EXPECT_EQ(hiraganaToKatakana(u"ゔゐゑゕゖっゝゞ"), u"ヴヰヱヵヶッヽヾ"_s);
    EXPECT_EQ(hiraganaToKatakana(katakanaToHiragana(u"タベル")), u"タベル"_s);
}

// Section 10.4, transcribed from JL.Core.Tests/UtilsTests.cs.
TEST(JapaneseFindSentence, JlUtilsTests)
{
    const QString sukiyaki =
        u"すき焼き（鋤焼、すきやき）は、薄くスライスした食肉や他の食材を浅い鉄鍋で焼いてたり煮たりして調理する日本の料"
        u"理である。調味料は醤油、砂糖が多用される。1862年「牛鍋屋」から始まる大ブームから広まったもので、当時は牛鍋"
        u"（ぎゅうなべ、うしなべ）と言った[1]。一般にすき焼きと呼ばれるようになったのは大正になってからである[2]。"_s;
    EXPECT_EQ(findSentence(sukiyaki, 0),
              u"すき焼き（鋤焼、すきやき）は、薄くスライスした食肉や他の食材を浅い鉄鍋で焼いてたり煮たりして調理する日"
              u"本の料理である。"_s);
    EXPECT_EQ(
        findSentence(sukiyaki, 97),
        u"1862年「牛鍋屋」から始まる大ブームから広まったもので、当時は牛鍋（ぎゅうなべ、うしなべ）と言った[1]。"_s);

    EXPECT_EQ(findSentence(u"a（アーカイブ）", 0), u"a（アーカイブ）"_s);
    EXPECT_EQ(findSentence(u"あああああああああ", 0), u"あああああああああ"_s);
    EXPECT_EQ(findSentence(u"「………はぁ、『高校生活を振り返って』というテーマの作文でしたが」", 15),
              u"はぁ、『高校生活を振り返って』というテーマの作文でしたが"_s);
    EXPECT_EQ(findSentence(u"『今日の晩ご飯はなんと。。。。。。。、カレーでしたっ！！』みたいな。", 0),
              u"今日の晩ご飯はなんと。"_s);
    EXPECT_EQ(findSentence(u"\t\ta（アーカイブ）", 0), u"a（アーカイブ）"_s);
    EXPECT_EQ(findSentence(u"a（アーカイブ）\n", 0), u"a（アーカイブ）"_s);
    EXPECT_EQ(findSentence(u"「なぁ、比企谷。私が授業で出した課題は何だったかな？」", 0), u"なぁ、比企谷。"_s);
    EXPECT_EQ(findSentence(u"「なぁ、比企谷。私が授業で出した課題は何だったかな？」", 8),
              u"私が授業で出した課題は何だったかな？"_s);
    EXPECT_EQ(findSentence(u"……単なる身びいきというものかも知れないが。　実際のところ、武者ではない俺が武者刀法を"
                           u"修める意味は少なく、素肌剣術をやっていれば良かったのだろうが、そうと認めてしまうのはなか"
                           u"なかに辛い。",
                           72),
              u"実際のところ、武者ではない俺が武者刀法を修める意味は少なく、素肌剣術をやっていれば良かったのだろうが、"
              u"そうと認めてしまうのはなかなかに辛い。"_s);
    EXPECT_EQ(findSentence(u"「……申し訳ありません。稽古をしていたのですが。　少し、考える事があって……没頭してお"
                           u"りました」",
                           10),
              u"申し訳ありません。"_s);
    EXPECT_EQ(findSentence(QStringView(), 0), QString());
}
