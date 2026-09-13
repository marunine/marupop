// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Which string each CopyWordMode resolves to, and the clipboard write behind it.
#include "scan/wordcopier.h"

#include <QGuiApplication>
#include <QMimeData>

#include <KSystemClipboard>

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::scan;

namespace
{

HitContext contextOver(const QString &paragraph, qsizetype cursorIndex)
{
    HitContext context;
    context.paragraphText = paragraph;
    context.cursorIndex = cursorIndex;
    return context;
}

lookup::Response responseFor(const QString &matched, const QString &spelling, const QList<QString> &readings)
{
    lookup::Result result;
    result.matchedText = matched;
    result.primarySpelling = spelling;
    result.readings = readings;

    lookup::Response response;
    response.results.append(result);
    response.highlightStart = 4;
    response.highlightLength = 3;
    return response;
}

const QString kParagraph = QStringLiteral("日本語のテキスト");

} // namespace

TEST(WordCopierTest, matchedTextModeCopiesTheRawSourceCharacters)
{
    const lookup::Response response =
        responseFor(QStringLiteral("テキス"), QStringLiteral("テキスト"), {QStringLiteral("てきすと")});
    EXPECT_EQ(wordToCopy(response, contextOver(kParagraph, 4), CopyWordMode::MatchedText), QStringLiteral("テキス"));
}

TEST(WordCopierTest, headwordModeCopiesThePrimarySpelling)
{
    const lookup::Response response =
        responseFor(QStringLiteral("テキス"), QStringLiteral("テキスト"), {QStringLiteral("てきすと")});
    EXPECT_EQ(wordToCopy(response, contextOver(kParagraph, 4), CopyWordMode::Headword), QStringLiteral("テキスト"));
}

TEST(WordCopierTest, readingModeCopiesTheFirstReading)
{
    const lookup::Response response =
        responseFor(QStringLiteral("読ん"), QStringLiteral("読む"), {QStringLiteral("よむ"), QStringLiteral("よみ")});
    EXPECT_EQ(wordToCopy(response, contextOver(kParagraph, 0), CopyWordMode::Reading), QStringLiteral("よむ"));
}

TEST(WordCopierTest, readingModeFallsBackToTheHeadwordForAKanaEntry)
{
    const lookup::Response response = responseFor(QStringLiteral("テキス"), QStringLiteral("テキスト"), {});
    EXPECT_EQ(wordToCopy(response, contextOver(kParagraph, 4), CopyWordMode::Reading), QStringLiteral("テキスト"));
}

TEST(WordCopierTest, headwordModeFallsBackToTheMatchedTextWithoutASpelling)
{
    const lookup::Response response = responseFor(QStringLiteral("テキス"), {}, {});
    EXPECT_EQ(wordToCopy(response, contextOver(kParagraph, 4), CopyWordMode::Headword), QStringLiteral("テキス"));
}

TEST(WordCopierTest, everyModeFallsBackToTheHighlightedSpanWithoutAResult)
{
    lookup::Response response;
    response.highlightStart = 4;
    response.highlightLength = 3;
    const HitContext context = contextOver(kParagraph, 4);

    EXPECT_EQ(wordToCopy(response, context, CopyWordMode::MatchedText), QStringLiteral("テキス"));
    EXPECT_EQ(wordToCopy(response, context, CopyWordMode::Headword), QStringLiteral("テキス"));
    EXPECT_EQ(wordToCopy(response, context, CopyWordMode::Reading), QStringLiteral("テキス"));
}

TEST(WordCopierTest, copiesNothingWithoutAHighlight)
{
    const lookup::Response response;
    EXPECT_TRUE(wordToCopy(response, contextOver(kParagraph, 0), CopyWordMode::MatchedText).isEmpty());
}

TEST(WordCopierTest, writesThePlainTextToTheSystemClipboard)
{
    KSystemClipboard *clipboard = KSystemClipboard::instance();
    if (clipboard == nullptr) {
        GTEST_SKIP() << "no system clipboard on this platform plugin";
    }

    copyToClipboard(QStringLiteral("テキスト"));
    const QMimeData *mime = clipboard->mimeData(QClipboard::Clipboard);
    ASSERT_NE(mime, nullptr);
    EXPECT_EQ(mime->text(), QStringLiteral("テキスト"));

    // An empty string leaves whatever the user had in the clipboard alone.
    copyToClipboard({});
    mime = clipboard->mimeData(QClipboard::Clipboard);
    ASSERT_NE(mime, nullptr);
    EXPECT_EQ(mime->text(), QStringLiteral("テキスト"));
}

int main(int argc, char **argv)
{
    // QGuiApplication: KSystemClipboard needs one, and so does the platform clipboard behind it.
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
