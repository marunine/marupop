// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The lookup window: the sentence located around the hit, the result count it lists apart from
// the popup's, the freeze while the pointer is inside it, the clipboard copy and the visibility
// it reports to Application.
#include "app/lookupwindow.h"
#include "core/settings.h"
#include "popup/popupview.h"
#include "scan/hitcontext.h"

#include <QApplication>
#include <QClipboard>
#include <QEnterEvent>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextBrowser>

#include <KSystemClipboard>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

// 明日 is the term: index 6 of the paragraph, the first character of its second sentence.
const QString kParagraph = QStringLiteral("今日は晴れ。明日は雨です。");
constexpr qsizetype kTermStart = 6;
constexpr qsizetype kTermLength = 2;

lookup::Response responseWith(int results)
{
    lookup::Response response;
    response.highlightStart = kTermStart;
    response.highlightLength = kTermLength;
    for (int i = 0; i < results; ++i) {
        lookup::Result result;
        result.matchedText = kParagraph.mid(kTermStart, kTermLength);
        result.primarySpelling = result.matchedText + QString::number(i);
        response.results.append(result);
    }
    return response;
}

scan::HitContext context()
{
    scan::HitContext hit;
    hit.paragraphText = kParagraph;
    hit.cursorIndex = kTermStart;
    return hit;
}

qsizetype shownCount(const LookupWindow &window)
{
    return window.resultView()->model().entries.size();
}

class LookupWindowTest : public testing::Test
{
protected:
    void SetUp() override
    {
        PopSettings::self()->setDefaults();
        PopSettings::setLookupWindowMaxResults(3);
        PopSettings::self()->save();
    }

    void TearDown() override
    {
        PopSettings::self()->setDefaults();
        PopSettings::self()->save();
    }
};

} // namespace

TEST(SentenceAroundTest, answersTheSentenceHoldingTheTermAndTheTermInsideIt)
{
    const SentenceSpan span = sentenceAround(kParagraph, kTermStart, kTermLength);
    EXPECT_EQ(span.text, QStringLiteral("明日は雨です。"));
    EXPECT_EQ(span.termStart, 0);
    EXPECT_EQ(span.termLength, kTermLength);
}

TEST(SentenceAroundTest, locatesTheOccurrenceTheTermIsInWhereTheSentenceRepeats)
{
    const QString paragraph = QStringLiteral("雨だ。雨だ。");
    const SentenceSpan span = sentenceAround(paragraph, 3, 1);
    EXPECT_EQ(span.text, QStringLiteral("雨だ。"));
    EXPECT_EQ(span.termStart, 0);
    EXPECT_EQ(span.termLength, 1);
}

TEST(SentenceAroundTest, highlightsTheCharactersOfTheParagraphAtEveryPosition)
{
    const QString paragraph = QStringLiteral("「行くよ」と彼は言った。そして、帰った！");
    for (qsizetype start = 0; start < paragraph.size(); ++start) {
        const SentenceSpan span = sentenceAround(paragraph, start, 2);
        if (span.termLength == 0) {
            continue;
        }
        EXPECT_EQ(span.text.mid(span.termStart, span.termLength), paragraph.mid(start, span.termLength))
            << "at " << start;
    }
}

TEST_F(LookupWindowTest, showsTheSentenceTheTermAndTheFirstResultsOfTheResponse)
{
    LookupWindow window;
    window.setLookup(responseWith(5), context());

    EXPECT_EQ(window.sentence().text, QStringLiteral("明日は雨です。"));
    EXPECT_EQ(window.term(), QStringLiteral("明日"));
    EXPECT_EQ(shownCount(window), 3);
    ASSERT_EQ(window.resultView()->model().entries.size(), 3);
    EXPECT_EQ(window.resultView()->model().entries.first().headword, QStringLiteral("明日0"));
    EXPECT_TRUE(window.resultView()->isScrollable());

    auto *sentenceView = window.findChild<QTextBrowser *>(QStringLiteral("sentenceView"));
    ASSERT_NE(sentenceView, nullptr);
    EXPECT_EQ(sentenceView->toPlainText(), QStringLiteral("明日は雨です。"));
    EXPECT_TRUE(sentenceView->toHtml().contains(QStringLiteral("underline")));
}

TEST_F(LookupWindowTest, listsItsOwnCountAgainOnceTheSettingMoves)
{
    LookupWindow window;
    window.setLookup(responseWith(5), context());
    ASSERT_EQ(shownCount(window), 3);

    PopSettings::setLookupWindowMaxResults(10);
    window.applySettings();
    EXPECT_EQ(shownCount(window), 5);
}

TEST_F(LookupWindowTest, dropsEveryResponseWhileThePointerIsInside)
{
    LookupWindow window;
    window.show();
    window.setLookup(responseWith(1), context());

    QEnterEvent enter{QPointF{10, 10}, QPointF{10, 10}, QPointF{10, 10}};
    QApplication::sendEvent(&window, &enter);
    ASSERT_TRUE(window.isFrozen());
    window.setLookup(responseWith(2), context());
    EXPECT_EQ(shownCount(window), 1);

    QEvent leave{QEvent::Leave};
    QApplication::sendEvent(&window, &leave);
    EXPECT_FALSE(window.isFrozen());
    window.setLookup(responseWith(2), context());
    EXPECT_EQ(shownCount(window), 2);

    // A hide carries no leave event, and must not leave a reopened window frozen.
    QApplication::sendEvent(&window, &enter);
    window.hide();
    EXPECT_FALSE(window.isFrozen());
}

TEST_F(LookupWindowTest, reportsItsVisibility)
{
    LookupWindow window;
    QSignalSpy spy{&window, &LookupWindow::visibilityChanged};

    window.show();
    window.close();
    ASSERT_EQ(spy.count(), 2);
    EXPECT_TRUE(spy.at(0).at(0).toBool());
    EXPECT_FALSE(spy.at(1).at(0).toBool());
}

TEST_F(LookupWindowTest, copiesTheSentenceFromTheButton)
{
    LookupWindow window;
    auto *button = window.findChild<QPushButton *>(QStringLiteral("copySentenceButton"));
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isEnabled()) << "no sentence to copy before the first lookup";

    window.setLookup(responseWith(1), context());
    ASSERT_TRUE(button->isEnabled());
    QSignalSpy spy{&window, &LookupWindow::sentenceCopied};
    button->click();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().first().toString(), QStringLiteral("明日は雨です。"));

    if (KSystemClipboard::instance() == nullptr) {
        GTEST_SKIP() << "no system clipboard on this platform plugin";
    }
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData(QClipboard::Clipboard);
    ASSERT_NE(mime, nullptr);
    EXPECT_EQ(mime->text(), QStringLiteral("明日は雨です。"));
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
