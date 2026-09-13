// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The option matrix of popup::renderHtml() and popup::renderPlainText(): each case flips one
// switch and states the fragment the flip adds or removes.
#include "popup/entrymodel.h"
#include "popup/renderer.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::popup;

namespace
{

Entry verbEntry()
{
    Entry entry;
    entry.headword = QStringLiteral("読む");
    entry.readings = QStringList{QStringLiteral("よむ")};
    entry.pitchPositions = QList<std::optional<quint8>>{std::optional<quint8>{1}};
    entry.alternativeSpellings = QStringList{QStringLiteral("讀む")};
    entry.deconjugationPaths = QStringList{QStringLiteral("～む→んだ; past")};
    entry.frequencyRank = 1042;
    entry.dictionaryName = QStringLiteral("JMdict");
    entry.orthographyInfo = QStringList{QStringLiteral("oK")};

    Sense first;
    first.glosses = QStringList{QStringLiteral("to read"), QStringLiteral("to peruse")};
    first.pos = QStringList{QStringLiteral("v5m"), QStringLiteral("vt")};
    first.misc = QStringList{QStringLiteral("uk")};
    first.fields = QStringList{QStringLiteral("comp")};
    first.dialects = QStringList{QStringLiteral("ksb")};
    first.info = QStringLiteral("sense note");
    first.crossReferences = QStringList{QStringLiteral("見る")};
    first.spellingRestrictions = QStringList{QStringLiteral("読む")};
    entry.senses.append(first);

    Sense second;
    second.glosses = QStringList{QStringLiteral("to guess")};
    entry.senses.append(second);
    return entry;
}

PopupModel wordModel()
{
    PopupModel model;
    model.entries.append(verbEntry());
    model.matchedText = QStringLiteral("読んだ");
    return model;
}

KanjiCard kanjiCard()
{
    KanjiCard kanji;
    kanji.character = QStringLiteral("本");
    kanji.onReadings = QStringList{QStringLiteral("ホン")};
    kanji.kunReadings = QStringList{QStringLiteral("もと")};
    kanji.nanoriReadings = QStringList{QStringLiteral("まと")};
    kanji.meanings = QStringList{QStringLiteral("book"), QStringLiteral("origin")};
    kanji.radicalNames = QStringList{QStringLiteral("き")};
    kanji.examples =
        QStringList{QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three"), QStringLiteral("four")};
    kanji.components = QStringList{QStringLiteral("木 tree")};
    kanji.strokeCount = 5;
    kanji.grade = 1;
    return kanji;
}

// Every switch off, so a case turns on exactly the one it asserts on.
RenderOptions allOff()
{
    RenderOptions options;
    options.showAllGlosses = false;
    options.showDeconjugation = false;
    options.showPartOfSpeech = false;
    options.showTags = false;
    options.showFrequency = false;
    options.showKanji = false;
    options.showKanjiExamples = false;
    options.showKanjiComponents = false;
    options.compactMode = true;
    options.showAlternativeSpellings = false;
    options.showPitchAccent = false;
    options.showDictionaryName = false;
    options.showOrthographyInfo = false;
    options.showStrokeCountAndGrade = false;
    return options;
}

QString render(const PopupModel &model, const RenderOptions &options)
{
    return renderHtml(model, options, Theme{});
}

} // namespace

TEST(RendererTest, returnsNothingForAnEmptyModel)
{
    EXPECT_TRUE(renderHtml(PopupModel{}, RenderOptions{}, Theme{}).isEmpty());
    EXPECT_TRUE(renderPlainText(PopupModel{}, RenderOptions{}).isEmpty());
}

TEST(RendererTest, alwaysEmitsTheHeadwordAndTheReading)
{
    const QString html = render(wordModel(), allOff());
    EXPECT_TRUE(html.contains(QStringLiteral("読む")));
    EXPECT_TRUE(html.contains(QStringLiteral("[よむ]")));
    // The headword carries the highlight color at the header size.
    EXPECT_TRUE(html.contains(QStringLiteral("font-size:18pt;color:#88d8ff")));
}

TEST(RendererTest, escapesEveryStringTakenFromADictionary)
{
    PopupModel model;
    Entry entry;
    entry.headword = QStringLiteral("a<b>&\"");
    Sense sense;
    sense.glosses = QStringList{QStringLiteral("<script>")};
    entry.senses.append(sense);
    model.entries.append(entry);

    const QString html = render(model, allOff());
    EXPECT_TRUE(html.contains(QStringLiteral("a&lt;b&gt;&amp;")));
    EXPECT_TRUE(html.contains(QStringLiteral("&lt;script&gt;")));
    EXPECT_FALSE(html.contains(QStringLiteral("<script>")));
}

TEST(RendererTest, showAllGlossesAddsTheSenseNumberAndEveryGloss)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("to peruse")));
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("<b>(1)</b>")));

    options.showAllGlosses = true;
    const QString html = render(wordModel(), options);
    EXPECT_TRUE(html.contains(QStringLiteral("to read, to peruse")));
    EXPECT_TRUE(html.contains(QStringLiteral("<b>(1)</b>")));
    EXPECT_TRUE(html.contains(QStringLiteral("<b>(2)</b>")));
}

TEST(RendererTest, showPartOfSpeechAddsTheItalicCodeList)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("<i>(v5m, vt)</i>")));
    options.showPartOfSpeech = true;
    EXPECT_TRUE(render(wordModel(), options).contains(QStringLiteral("<i>(v5m, vt)</i>")));
}

TEST(RendererTest, showTagsAddsTheMiscFieldAndDialectPill)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("[uk, comp, ksb]")));
    options.showTags = true;
    const QString html = render(wordModel(), options);
    EXPECT_TRUE(html.contains(QStringLiteral("[uk, comp, ksb]")));
    // The pill carries nazeka's miscellaneous background.
    EXPECT_TRUE(html.contains(QStringLiteral("background-color:#608040")));
    // The sense note and the cross reference ride the same switch.
    EXPECT_TRUE(html.contains(QStringLiteral("sense note")));
    EXPECT_TRUE(html.contains(QStringLiteral("見る")));
}

TEST(RendererTest, showOrthographyInfoAddsTheHeadwordCodesAndTheRestrictions)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("(oK)")));
    options.showOrthographyInfo = true;
    const QString html = render(wordModel(), options);
    EXPECT_TRUE(html.contains(QStringLiteral("(oK)")));
    EXPECT_TRUE(html.contains(QStringLiteral("{読む}")));
}

TEST(RendererTest, showFrequencyAddsTheRank)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("#1042")));
    options.showFrequency = true;
    EXPECT_TRUE(render(wordModel(), options).contains(QStringLiteral("#1042")));

    // A rank a rank list pads uncovered headwords with is left out.
    PopupModel padded = wordModel();
    padded.entries[0].frequencyRank = 999999;
    EXPECT_FALSE(render(padded, options).contains(QStringLiteral("#999999")));

    // Every frequency dictionary past the first arrives already rendered.
    PopupModel named = wordModel();
    named.entries[0].frequencyText = QStringLiteral("JPDB: 512");
    EXPECT_TRUE(render(named, options).contains(QStringLiteral("JPDB: 512")));
}

TEST(RendererTest, showDeconjugationAddsThePath)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("～む→んだ")));
    options.showDeconjugation = true;
    EXPECT_TRUE(render(wordModel(), options).contains(QStringLiteral("～む→んだ; past")));
}

TEST(RendererTest, showAlternativeSpellingsAddsTheParenthesizedList)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("(讀む)")));
    options.showAlternativeSpellings = true;
    EXPECT_TRUE(render(wordModel(), options).contains(QStringLiteral("(讀む)")));
}

TEST(RendererTest, showDictionaryNameAddsTheName)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("JMdict")));
    options.showDictionaryName = true;
    EXPECT_TRUE(render(wordModel(), options).contains(QStringLiteral("JMdict")));
}

TEST(RendererTest, showPitchAccentAddsTheAnchorEachContourIsPaintedOver)
{
    RenderOptions options = allOff();
    EXPECT_FALSE(render(wordModel(), options).contains(QStringLiteral("pitch:0:0")));
    options.showPitchAccent = true;
    // The third field is the reading's length in UTF-16 code units.
    EXPECT_TRUE(render(wordModel(), options).contains(QStringLiteral("<a name=\"pitch:0:0:2\">よむ</a>")));
}

TEST(RendererTest, compactModeJoinsTheSensesOntoTheHeaderLine)
{
    RenderOptions options = allOff();
    options.compactMode = true;
    const QString compact = render(wordModel(), options);
    EXPECT_TRUE(compact.contains(QStringLiteral("to read; to guess")));
    EXPECT_FALSE(compact.contains(QStringLiteral("<br>")));

    options.compactMode = false;
    const QString expanded = render(wordModel(), options);
    EXPECT_TRUE(expanded.contains(QStringLiteral("to read<br>to guess")));
}

TEST(RendererTest, separatesTwoEntriesWithARule)
{
    PopupModel model = wordModel();
    model.entries.append(verbEntry());
    const QString html = render(model, allOff());
    EXPECT_EQ(html.count(QStringLiteral("<hr")), 1);
    // The second entry's anchors carry its own index.
    RenderOptions options = allOff();
    options.showPitchAccent = true;
    EXPECT_TRUE(render(model, options).contains(QStringLiteral("pitch:1:0:2")));
}

TEST(RendererTest, splitsTheEntrySpacingAcrossTheRuleMargins)
{
    PopupModel model = wordModel();
    model.entries.append(verbEntry());

    // The top margin repeats the entry's 2 px bottom margin, which QTextDocument collapses it
    // with, so the default 0 lays the rule out where the zero margins used to.
    EXPECT_TRUE(render(model, allOff()).contains(QStringLiteral("<hr style=\"margin-top:2px;margin-bottom:0px;\"")));

    // An odd spacing puts the extra pixel below the rule.
    Theme theme;
    theme.entrySpacing = 9;
    EXPECT_TRUE(
        renderHtml(model, allOff(), theme).contains(QStringLiteral("<hr style=\"margin-top:6px;margin-bottom:5px;\"")));
}

TEST(RendererTest, emitsTheRichTextGlossaryInsteadOfTheSenses)
{
    PopupModel model = wordModel();
    model.entries[0].richTextGlossary = QStringLiteral("<i>already rendered</i>");
    const QString html = render(model, allOff());
    EXPECT_TRUE(html.contains(QStringLiteral("<i>already rendered</i>")));
    EXPECT_FALSE(html.contains(QStringLiteral("to read")));
}

TEST(RendererTest, emitsTheKanjiCardOnlyWhenShowKanjiIsSet)
{
    PopupModel model = wordModel();
    model.kanji = kanjiCard();

    RenderOptions options = allOff();
    EXPECT_FALSE(render(model, options).contains(QStringLiteral("<table")));

    options.showKanji = true;
    const QString html = render(model, options);
    EXPECT_TRUE(html.contains(QStringLiteral("<table")));
    EXPECT_TRUE(html.contains(QStringLiteral("本")));
    EXPECT_TRUE(html.contains(QStringLiteral("ホン")));
    EXPECT_TRUE(html.contains(QStringLiteral("もと")));
    EXPECT_TRUE(html.contains(QStringLiteral("まと")));
    EXPECT_TRUE(html.contains(QStringLiteral("book, origin")));
    EXPECT_TRUE(html.contains(QStringLiteral("き")));
    // The three sub-switches are each off.
    EXPECT_FALSE(html.contains(QStringLiteral("5 strokes")));
    EXPECT_FALSE(html.contains(QStringLiteral("Examples")));
    EXPECT_FALSE(html.contains(QStringLiteral("Components")));
}

TEST(RendererTest, kanjiCardSwitchesAddTheStatsExamplesAndComponents)
{
    PopupModel model = wordModel();
    model.kanji = kanjiCard();

    RenderOptions options = allOff();
    options.showKanji = true;
    options.showStrokeCountAndGrade = true;
    options.showKanjiExamples = true;
    options.showKanjiComponents = true;
    const QString html = render(model, options);

    EXPECT_TRUE(html.contains(QStringLiteral("5 strokes, 1 (Kyouiku)")));
    EXPECT_TRUE(html.contains(QStringLiteral("one; two; three")));
    // At most three example words reach the card.
    EXPECT_FALSE(html.contains(QStringLiteral("four")));
    EXPECT_TRUE(html.contains(QStringLiteral("木 tree")));
}

// The eight row labels and the stroke count run through i18nc() and i18ncp(). The test locale
// carries no marupop catalog, so each message comes back as its own source text.
TEST(RendererTest, namesEveryKanjiRowItEmits)
{
    PopupModel model = wordModel();
    model.kanji = kanjiCard();

    RenderOptions options = allOff();
    options.showKanji = true;
    options.showStrokeCountAndGrade = true;
    options.showKanjiExamples = true;
    options.showKanjiComponents = true;
    const QString html = render(model, options);

    for (const QString &label : {QStringLiteral("On"),
                                 QStringLiteral("Kun"),
                                 QStringLiteral("Nanori"),
                                 QStringLiteral("Meanings"),
                                 QStringLiteral("Stats"),
                                 QStringLiteral("Radicals"),
                                 QStringLiteral("Examples"),
                                 QStringLiteral("Components")}) {
        const QString row = label % QStringLiteral(":");
        EXPECT_TRUE(html.contains(row)) << label.toStdString();
    }

    const QString text = renderPlainText(model, options);
    EXPECT_TRUE(text.contains(QStringLiteral("On: ホン")));
    EXPECT_TRUE(text.contains(QStringLiteral("Kun: もと")));
    EXPECT_TRUE(text.contains(QStringLiteral("Nanori: まと")));
    EXPECT_TRUE(text.contains(QStringLiteral("Meanings: book, origin")));
    EXPECT_TRUE(text.contains(QStringLiteral("Stats: 5 strokes, 1 (Kyouiku)")));
    EXPECT_TRUE(text.contains(QStringLiteral("Radicals: き")));
    EXPECT_TRUE(text.contains(QStringLiteral("Examples: one; two; three")));
    EXPECT_TRUE(text.contains(QStringLiteral("Components: 木 tree")));
}

// The stroke count is one i18ncp() message rather than a number joined to a suffix, so the
// singular form is a form of its own.
TEST(RendererTest, usesTheSingularStrokeCountForOneStroke)
{
    PopupModel model = wordModel();
    model.kanji = kanjiCard();
    model.kanji->character = QStringLiteral("一");
    model.kanji->strokeCount = 1;

    RenderOptions options = allOff();
    options.showKanji = true;
    options.showStrokeCountAndGrade = true;
    EXPECT_TRUE(render(model, options).contains(QStringLiteral("1 stroke,")));
}

TEST(RendererTest, gradeToTextFollowsJlsTable)
{
    EXPECT_EQ(gradeToText(1), QStringLiteral("1 (Kyouiku)"));
    EXPECT_EQ(gradeToText(6), QStringLiteral("6 (Kyouiku)"));
    EXPECT_EQ(gradeToText(8), QStringLiteral("8 (Jouyou)"));
    EXPECT_EQ(gradeToText(9), QStringLiteral("9 (Jinmeiyou)"));
    EXPECT_EQ(gradeToText(10), QStringLiteral("10 (Jinmeiyou)"));
    EXPECT_EQ(gradeToText(0), QStringLiteral("Hyougai"));
    EXPECT_EQ(gradeToText(7), QStringLiteral("Hyougai"));
    EXPECT_EQ(gradeToText(11), QStringLiteral("Hyougai"));
}

TEST(RendererTest, parsesTheAnchorNamesItProduces)
{
    int entryIndex = -1;
    int readingIndex = -1;
    int length = -1;
    ASSERT_TRUE(parsePitchAnchorName(pitchAnchorName(3, 7, 4), &entryIndex, &readingIndex, &length));
    EXPECT_EQ(entryIndex, 3);
    EXPECT_EQ(readingIndex, 7);
    EXPECT_EQ(length, 4);

    EXPECT_FALSE(parsePitchAnchorName(QStringLiteral("other"), &entryIndex, &readingIndex, &length));
    EXPECT_FALSE(parsePitchAnchorName(QStringLiteral("pitch:1"), &entryIndex, &readingIndex, &length));
    EXPECT_FALSE(parsePitchAnchorName(QStringLiteral("pitch:1:2"), &entryIndex, &readingIndex, &length));
    EXPECT_FALSE(parsePitchAnchorName(QStringLiteral("pitch:a:b:c"), &entryIndex, &readingIndex, &length));
    // A zero-length run marks no reading.
    EXPECT_FALSE(parsePitchAnchorName(QStringLiteral("pitch:1:2:0"), &entryIndex, &readingIndex, &length));
}

TEST(RendererTest, rendersThePlainTextForTheClipboard)
{
    RenderOptions options = allOff();
    options.showAllGlosses = true;
    PopupModel model = wordModel();
    model.kanji = kanjiCard();
    options.showKanji = true;

    const QString text = renderPlainText(model, options);
    EXPECT_TRUE(text.startsWith(QStringLiteral("読む [よむ]")));
    EXPECT_TRUE(text.contains(QStringLiteral("(1) to read, to peruse")));
    EXPECT_TRUE(text.contains(QStringLiteral("本")));
    EXPECT_FALSE(text.contains(QLatin1Char('<')));
}
