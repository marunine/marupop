// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The line height and the entry spacing of Theme, measured on the document PopupView lays out.
#include "popup/entrymodel.h"
#include "popup/popupview.h"
#include "popup/theme.h"

#include <QApplication>
#include <QTextBlock>
#include <QTextDocument>

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::popup;

namespace
{

Entry entry(const QString &headword, const QString &gloss)
{
    Entry result;
    result.headword = headword;
    result.readings = QStringList{QStringLiteral("よむ")};
    Sense sense;
    sense.glosses = QStringList{gloss};
    result.senses.append(sense);
    return result;
}

PopupModel twoEntries()
{
    PopupModel model;
    model.entries.append(entry(QStringLiteral("読む"), QStringLiteral("to read")));
    model.entries.append(entry(QStringLiteral("詠む"), QStringLiteral("to compose")));
    return model;
}

qreal contentHeight(const Theme &theme, const PopupModel &model)
{
    PopupView view;
    view.setTheme(theme);
    view.setModel(model);
    return view.layoutContent(400).height();
}

} // namespace

TEST(PopupViewTest, leavesEveryBlockAtSingleHeightByDefault)
{
    PopupView view;
    view.setModel(twoEntries());
    for (QTextBlock block = view.document()->begin(); block.isValid(); block = block.next()) {
        EXPECT_EQ(block.blockFormat().lineHeightType(), QTextBlockFormat::SingleHeight);
    }
}

TEST(PopupViewTest, appliesTheLineHeightToEveryBlockIncludingTheKanjiCard)
{
    PopupModel model = twoEntries();
    KanjiCard kanji;
    kanji.character = QStringLiteral("読");
    kanji.meanings = QStringList{QStringLiteral("read")};
    model.kanji = kanji;

    Theme theme;
    theme.lineHeightPercent = 150;
    PopupView view;
    view.setTheme(theme);
    view.setModel(model);

    int blocks = 0;
    for (QTextBlock block = view.document()->begin(); block.isValid(); block = block.next()) {
        ++blocks;
        EXPECT_EQ(block.blockFormat().lineHeightType(), QTextBlockFormat::ProportionalHeight)
            << block.text().toStdString();
        EXPECT_DOUBLE_EQ(block.blockFormat().lineHeight(), 150.0) << block.text().toStdString();
    }
    EXPECT_GT(blocks, 4);
}

TEST(PopupViewTest, keepsTheLineHeightAGlossarySetsItself)
{
    PopupModel model;
    Entry glossed = entry(QStringLiteral("読む"), QStringLiteral("to read"));
    glossed.richTextGlossary = QStringLiteral("<p style=\"line-height:250%;\">styled</p>");
    model.entries.append(glossed);

    Theme theme;
    theme.lineHeightPercent = 120;
    PopupView view;
    view.setTheme(theme);
    view.setModel(model);

    bool found = false;
    for (QTextBlock block = view.document()->begin(); block.isValid(); block = block.next()) {
        const qreal expected = block.text() == QStringLiteral("styled") ? 250.0 : 120.0;
        found = found || block.text() == QStringLiteral("styled");
        EXPECT_DOUBLE_EQ(block.blockFormat().lineHeight(), expected) << block.text().toStdString();
    }
    EXPECT_TRUE(found);
}

TEST(PopupViewTest, growsTheContentByTheLineHeight)
{
    Theme single;
    Theme doubled;
    doubled.lineHeightPercent = 200;
    const qreal base = contentHeight(single, twoEntries());
    // Every line becomes twice as tall; the rule and the paragraph margins stay put, so the
    // growth is less than a factor of two.
    EXPECT_GT(contentHeight(doubled, twoEntries()), base * 1.5);
}

TEST(PopupViewTest, growsTheContentByExactlyTheEntrySpacing)
{
    Theme spaced;
    spaced.entrySpacing = 13;
    const qreal base = contentHeight(Theme{}, twoEntries());
    // One rule between two entries. QTextDocument collapses the rule's top margin with the
    // entry's bottom margin, and renderHtml() compensates for that, so the height grows by the
    // spacing and nothing else.
    EXPECT_DOUBLE_EQ(contentHeight(spaced, twoEntries()), base + 13.0);
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
