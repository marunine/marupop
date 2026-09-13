// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Optional benchmarks read user-supplied dictionaries; enable MARUPOP_REAL_DATA=1.
// See docs/TESTING.md for input variables. No dictionary corpus is bundled.
#include "core/enums.h"
#include "deconj/deconjugator.h"
#include "dict/dictionary.h"
#include "dict/dictionarymanager.h"
#include "dict/importers/jmdictimporter.h"
#include "dict/records.h"
#include "dict/store.h"
#include "lookup/engine.h"
#include "lookup/format.h"
#include "lookup/query.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <numeric>

using namespace Qt::Literals::StringLiterals;
using namespace maru;
using namespace maru::lookup;

namespace
{

const QString jmdictPath = qEnvironmentVariable("MARUPOP_JMDICT_PATH");

bool realDataEnabled()
{
    return qEnvironmentVariable("MARUPOP_REAL_DATA") == "1"_L1;
}

bool mountPresent()
{
    return QFileInfo::exists(jmdictPath);
}

#define SKIP_UNLESS_REAL_DATA()                                                                                        \
    do {                                                                                                               \
        if (!mountPresent())                                                                                           \
            GTEST_SKIP() << "set MARUPOP_JMDICT_PATH to an existing JMdict XML file";                                  \
        if (!realDataEnabled())                                                                                        \
            GTEST_SKIP() << "set MARUPOP_REAL_DATA=1 to run the real-data lookups";                                    \
    } while (false)

// The real JMdict, imported once per machine into a directory under the test HOME and reused.
// The import is ten seconds; a suite that repeats it per test would be a minute of XML parsing.
class RealFixture
{
public:
    bool build()
    {
        const QString root = QDir::homePath() + u"/marupop-lookup-realdata"_s;
        if (!QDir().mkpath(root))
            return false;

        m_manager = std::make_unique<dict::DictionaryManager>(root, nullptr);
        (void)m_manager->load();

        dict::Dictionary *entry = m_manager->dictionaryNamed(u"JMdict"_s);
        if (entry == nullptr) {
            dict::Dictionary added;
            added.type = dict::DictType::JMdict;
            added.name = u"JMdict"_s;
            added.sourcePath = jmdictPath;
            added.priority = 1;
            entry = m_manager->add(added);
        }
        if (entry == nullptr)
            return false;

        const QString databasePath = dict::databasePathFor(root, entry->id);
        if (!QFileInfo::exists(databasePath)) {
            QElapsedTimer timer;
            timer.start();
            dict::StoreWriter writer;
            if (!writer.begin(databasePath, dict::DictType::JMdict))
                return false;
            dict::JmdictImporter importer(true);
            std::atomic_bool cancel{false};
            const dict::ImportResult result = importer.import(jmdictPath, writer, [](int, const QString &) {}, cancel);
            if (!result.ok)
                return false;
            entry->recordCount = result.recordCount;
            entry->keyCount = result.keyCount;
            entry->maxKeyLength = result.maxKeyLength;
            entry->importedAt = QDateTime::currentDateTimeUtc();
            (void)importer.wordClassTable().save(dict::wordClassTablePathFor(root, entry->id));
            (void)m_manager->save();
            qInfo("imported JMdict in %lld ms: %lld records, %lld keys",
                  static_cast<long long>(timer.elapsed()),
                  static_cast<long long>(result.recordCount),
                  static_cast<long long>(result.keyCount));
        }

        m_manager->openAll();
        (void)m_manager->loadWordClassTable();
        std::optional<deconj::RuleSet> rules = deconj::RuleSet::loadEmbedded();
        if (!rules.has_value())
            return false;
        m_rules = std::move(rules);
        m_engine = std::make_unique<Engine>(*m_manager, *m_rules);
        return m_manager->dictionaryNamed(u"JMdict"_s)->isReady();
    }

    [[nodiscard]] Engine &engine()
    {
        return *m_engine;
    }

    [[nodiscard]] Response lookup(const QString &text, qsizetype cursorIndex = 0)
    {
        Request request;
        request.sourceText = text;
        request.cursorIndex = cursorIndex;
        return m_engine->lookup(request);
    }

private:
    std::unique_ptr<dict::DictionaryManager> m_manager;
    std::optional<deconj::RuleSet> m_rules;
    std::unique_ptr<Engine> m_engine;
};

RealFixture &realFixture()
{
    static RealFixture instance;
    static const bool built = instance.build();
    EXPECT_TRUE(built);
    return instance;
}

bool hasSpelling(const Response &response, QStringView spelling)
{
    return std::ranges::any_of(response.results, [spelling](const Result &result) {
        return result.primarySpelling == spelling;
    });
}

double percentile(std::vector<double> &values, double fraction)
{
    std::ranges::sort(values);
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[index];
}

} // namespace

TEST(LookupRealData, DeconjugatesALongChain)
{
    SKIP_UNLESS_REAL_DATA();
    const Response response = realFixture().lookup(u"食べさせられなかった"_s);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.results.first().primarySpelling, u"食べる"_s);
    // The lemma is the normalized key, which folds kana and leaves kanji alone.
    EXPECT_EQ(response.results.first().deconjugatedMatchedText, u"食べる"_s);
    EXPECT_EQ(response.highlightLength, 10);
    qInfo("食べさせられなかった -> %s", qUtf8Printable(deconjugationProcessText(response.results.first())));
}

TEST(LookupRealData, MatchesAProgressive)
{
    SKIP_UNLESS_REAL_DATA();
    const Response response = realFixture().lookup(u"走っている"_s);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_TRUE(hasSpelling(response, u"走る"_s));
    // The whole span is one deconjugation of 走る, so it is also the highlight.
    EXPECT_EQ(response.highlightLength, 5);
}

TEST(LookupRealData, ResolvesAChoonpuLoanword)
{
    SKIP_UNLESS_REAL_DATA();
    const Response response = realFixture().lookup(u"ラーメン"_s);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.results.first().matchedText, u"ラーメン"_s);
}

TEST(LookupRealData, MatchesACompound)
{
    SKIP_UNLESS_REAL_DATA();
    const Response response = realFixture().lookup(u"日本語"_s);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.results.first().primarySpelling, u"日本語"_s);
    EXPECT_EQ(response.highlightLength, 3);
}

TEST(LookupRealData, HandlesTheIterationMark)
{
    SKIP_UNLESS_REAL_DATA();
    // 人々 is keyed 人人 after normalization, and the highlight still counts the raw span.
    const Response response = realFixture().lookup(u"人々"_s);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.results.first().matchedText, u"人々"_s);
    EXPECT_EQ(response.highlightLength, 2);
    EXPECT_TRUE(hasSpelling(response, u"人々"_s));
}

TEST(LookupRealData, BenchmarksTwoHundredHoverPositions)
{
    SKIP_UNLESS_REAL_DATA();

    const QString paragraph =
        u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。何でも薄暗いじめじめした所で"
        "ニャーニャー泣いていた事だけは記憶している。吾輩はここで始めて人間というものを見た。しかもあとで"
        "聞くとそれは書生という人間中で一番獰悪な種族であったそうだ。この書生というのは時々我々を捕えて煮て"
        "食うという話である。掌に載せられてスーと持ち上げられた時何だかフワフワした感じがあったばかりである。"
        "掌の上で少し落ちついて書生の顔を見たのがいわゆる人間というものの見始であろう。この時妙なものだと"
        "思った感じが今でも残っている。第一毛をもって装飾されべきはずの顔がつるつるしてまるで薬缶だ。"_s;
    ASSERT_GE(paragraph.size(), 200);

    Engine &engine = realFixture().engine();
    std::vector<double> cold;
    std::vector<double> warm;
    cold.reserve(200);
    warm.reserve(200);

    for (int i = 0; i < 200; ++i) {
        Request request;
        request.sourceText = paragraph;
        request.cursorIndex = i;

        engine.invalidateCache();
        QElapsedTimer timer;
        timer.start();
        const Response response = engine.lookup(request);
        cold.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);

        timer.restart();
        const Response cached = engine.lookup(request);
        warm.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        EXPECT_EQ(response.results.size(), cached.results.size());
    }

    const double coldMean = std::accumulate(cold.cbegin(), cold.cend(), 0.0) / static_cast<double>(cold.size());
    const double warmMean = std::accumulate(warm.cbegin(), warm.cend(), 0.0) / static_cast<double>(warm.size());
    qInfo("cold p50 %.3f ms, p99 %.3f ms, mean %.3f ms", percentile(cold, 0.5), percentile(cold, 0.99), coldMean);
    qInfo("warm p50 %.3f ms, p99 %.3f ms, mean %.3f ms", percentile(warm, 0.5), percentile(warm, 0.99), warmMean);

    // The scan loop budgets 5 ms per hover; a regression past that is what this guards.
    EXPECT_LT(percentile(cold, 0.5), 25.0);
    EXPECT_LT(percentile(warm, 0.5), percentile(cold, 0.5));
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
