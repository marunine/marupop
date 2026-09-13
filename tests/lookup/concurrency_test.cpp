// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The lookup engine answering on QThreadPool threads while the GUI thread mutates the dictionary
// list under it, which is what scan::ScanController and Application do together.
//
// The suite is the regression test for three defects reviewed:
//
//  1. Engine::lookup() called DictionaryManager::enabledDictionaries(), which is not const: it
//     opened stores, wrote Dictionary::store, needsReimport, storeUnavailable, recordCount,
//     keyCount and maxKeyLength, and emitted ready() and dictionaryChanged(), all from a pool
//     thread while the GUI thread mutated the same objects and reallocated the
//     std::vector<std::unique_ptr<Dictionary>> holding them.
//  2. dict::find() dereferenced Dictionary::store through a reference, so disabling a dictionary
//     mid-lookup destroyed a Store a pool thread was inside.
//  3. Store::find() probed its key filter before taking its mutex, and Store::meta() read m_meta
//     with none, while close() unmapped the filter and cleared the map under that mutex.
//
// The engine now answers from a dict::DictionarySnapshot the GUI thread pushes, whose handles hold
// their stores alive, so this suite reaches no manager state from a pool thread at all. It is run
// under -DECM_ENABLE_SANITIZERS='address;undefined' to make a use-after-free or a data race on the
// Store observable rather than latent.
#include "core/enums.h"
#include "deconj/deconjugator.h"
#include "dict/dictionary.h"
#include "dict/dictionarymanager.h"
#include "dict/importers/importer.h"
#include "dict/importers/jmdictimporter.h"
#include "dict/importers/jmnedictimporter.h"
#include "dict/store.h"
#include "lookup/engine.h"
#include "lookup/popupadapter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QRunnable>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <optional>

using namespace Qt::Literals::StringLiterals;
using namespace maru;
using namespace maru::lookup;
using maru::dict::DictType;

namespace
{

// The number of lookups the pool threads run in total, and the wall-clock budget the suite is
// allowed. Both are bounded so the suite is deterministic enough for CI: the threads stop at
// whichever comes first.
constexpr int totalLookups = 200;
constexpr int poolThreads = 4;
constexpr qint64 deadlineMs = 60000;

QString dictFixture(const QString &name)
{
    return QStringLiteral(MARUPOP_DICT_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

// Two small imported dictionaries in a temporary directory: JL's mock JMdict and the mock
// JMnedict, which is the smallest set that gives the toggled dictionary results of its own.
class Fixture
{
public:
    Fixture()
    {
        EXPECT_TRUE(m_directory.isValid());
        m_manager = std::make_unique<dict::DictionaryManager>(m_directory.path(), nullptr);
        words = import(DictType::JMdict, u"JMdict"_s, dictFixture(u"MockJMdict.xml"_s), 1);
        names = import(DictType::JMnedict, u"JMnedict"_s, dictFixture(u"mock_jmnedict.xml"_s), 2);
        EXPECT_TRUE(m_manager->loadWordClassTable());

        std::optional<deconj::RuleSet> rules = deconj::RuleSet::loadEmbedded();
        EXPECT_TRUE(rules.has_value());
        m_rules = std::move(rules);
        m_engine = std::make_unique<Engine>(*m_manager, *m_rules);
        EXPECT_EQ(m_manager->snapshot()->size(), 2U);
    }

    [[nodiscard]] dict::DictionaryManager &manager()
    {
        return *m_manager;
    }

    [[nodiscard]] Engine &engine()
    {
        return *m_engine;
    }

    // What Application does on dict::DictionaryManager::changed(), which is the only place the
    // engine is handed a new dictionary set.
    void publish()
    {
        m_engine->setDictionaries(m_manager->snapshot(), m_manager->wordClasses());
    }

    QUuid words;
    QUuid names;

private:
    QUuid import(DictType type, const QString &name, const QString &source, int priority)
    {
        dict::Dictionary entry;
        entry.type = type;
        entry.name = name;
        entry.sourcePath = source;
        entry.priority = priority;
        dict::Dictionary *stored = m_manager->add(entry);
        EXPECT_NE(stored, nullptr);
        if (stored == nullptr)
            return {};

        std::unique_ptr<dict::Importer> importer;
        if (type == DictType::JMdict)
            importer = std::make_unique<dict::JmdictImporter>(true);
        else
            importer = std::make_unique<dict::JmnedictImporter>();

        QHash<QString, QString> meta;
        meta.insert(QString(dict::metakeys::title), name);
        dict::StoreWriter writer;
        EXPECT_TRUE(writer.begin(dict::databasePathFor(m_directory.path(), stored->id), type, meta));
        std::atomic_bool cancel{false};
        // Importer::import() commits the writer, so the store is on disk when it returns.
        const dict::ImportResult result = importer->import(source, writer, [](int, const QString &) {}, cancel);
        EXPECT_TRUE(result.ok) << result.errorString.toStdString();

        stored->recordCount = result.recordCount;
        stored->keyCount = result.keyCount;
        stored->maxKeyLength = result.maxKeyLength;
        stored->importedAt = QDateTime::currentDateTimeUtc();
        if (auto *jmdict = dynamic_cast<dict::JmdictImporter *>(importer.get()))
            EXPECT_TRUE(jmdict->wordClassTable().save(dict::wordClassTablePathFor(m_directory.path(), stored->id)));
        // The entry was mutated behind the manager's back, so the snapshot is rebuilt explicitly.
        m_manager->openAll();
        return stored->id;
    }

    QTemporaryDir m_directory;
    std::unique_ptr<dict::DictionaryManager> m_manager;
    std::optional<deconj::RuleSet> m_rules;
    std::unique_ptr<Engine> m_engine;
};

// Everything one Response has to satisfy whatever the GUI thread did while it was produced. The
// dictionary a result names has to be the one whose store decoded it, and the highlight has to
// index the span the first result matched.
[[nodiscard]] bool isSelfConsistent(const Response &response, const QString &sourceText)
{
    if (response.highlightStart < 0 || response.highlightStart > sourceText.size())
        return false;
    if (response.highlightLength < 0 || response.highlightStart + response.highlightLength > sourceText.size())
        return false;
    if (response.results.isEmpty())
        return response.highlightLength == 0;
    if (response.highlightLength != response.results.first().matchedText.size())
        return false;

    return std::ranges::all_of(response.results, [](const Result &result) {
        if (!result.record)
            return false;
        // The handle keeps its own store alive, so it is non-null and open for as long as the
        // Response is held, whatever the manager did to the dictionary meanwhile.
        if (!result.dictionary.store || !result.dictionary.store->isOpen())
            return false;
        if (result.dictionary.id.isNull() || result.dictionary.name.isEmpty() || result.dictionary.priority <= 0)
            return false;
        if (result.primarySpelling.isEmpty() || result.matchedText.isEmpty())
            return false;
        // Reading the store's meta table is the second lock-free read the immutability invariant
        // covers, and entityDescription() is the caller that does it.
        return !entityDescription(result.dictionary, u"v5r"_s).isEmpty();
    });
}

class LookupTask : public QRunnable
{
public:
    LookupTask(Engine &engine, QStringList spans, std::atomic_int &remaining, std::atomic_int &failures)
        : m_engine(engine)
        , m_spans(std::move(spans))
        , m_remaining(remaining)
        , m_failures(failures)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        QElapsedTimer deadline;
        deadline.start();
        int index = 0;
        while (m_remaining.fetch_sub(1, std::memory_order_relaxed) > 0 && deadline.elapsed() < deadlineMs) {
            Request request;
            request.sourceText = m_spans.at(index++ % m_spans.size());
            request.cursorIndex = 0;
            const Response response = m_engine.lookup(request);
            if (!isSelfConsistent(response, request.sourceText))
                ++m_failures;
        }
    }

private:
    Engine &m_engine;
    QStringList m_spans;
    std::atomic_int &m_remaining;
    std::atomic_int &m_failures;
};

} // namespace

// 200 lookups across four pool threads while the GUI thread toggles one dictionary and drops the
// result cache. Before the snapshot was introduced, the pool threads opened stores through the
// manager and read a Store the toggle had destroyed.
TEST(LookupConcurrency, SurvivesADictionaryToggledUnderRunningLookups)
{
    Fixture fixture;
    Engine &engine = fixture.engine();

    const QStringList spans{
        u"始まる"_s, u"食べさせられなかった"_s, u"日"_s, u"た"_s, u"田中"_s, u"ラーメン"_s, u"帰った"_s, u"廃墟"_s};

    std::atomic_int remaining{totalLookups};
    std::atomic_int failures{0};

    QThreadPool pool;
    pool.setMaxThreadCount(poolThreads);
    std::vector<std::unique_ptr<LookupTask>> tasks;
    tasks.reserve(poolThreads);
    for (int thread = 0; thread < poolThreads; ++thread) {
        tasks.push_back(std::make_unique<LookupTask>(engine, spans, remaining, failures));
        pool.start(tasks.back().get());
    }

    // The GUI thread's half: enable and disable the name dictionary, republish the set the engine
    // answers from, and drop the result cache, until the pool threads have run out of work.
    QElapsedTimer deadline;
    deadline.start();
    int toggles = 0;
    bool enabled = false;
    while (remaining.load(std::memory_order_relaxed) > 0 && deadline.elapsed() < deadlineMs) {
        ASSERT_TRUE(fixture.manager().setEnabled(fixture.names, enabled));
        fixture.publish();
        engine.invalidateCache();
        enabled = !enabled;
        ++toggles;
    }
    EXPECT_TRUE(pool.waitForDone(static_cast<int>(deadlineMs)));

    EXPECT_GT(toggles, 0);
    EXPECT_LE(remaining.load(), 0);
    EXPECT_EQ(failures.load(), 0);
    // The engine is still answering after the run, from whichever set was published last.
    EXPECT_FALSE(engine.lookupText(u"始まる"_s, LookupCategory::All, 10).isEmpty());
}

// A Result keeps the Store it was decoded from alive, so a Response outlives the removal of the
// dictionary it came from. That is what makes the result cache safe to keep across a mutation.
TEST(LookupConcurrency, AResponseOutlivesTheDictionaryItCameFrom)
{
    Fixture fixture;
    Request request;
    request.sourceText = u"田中"_s;
    request.category = LookupCategory::Name;
    const Response response = fixture.engine().lookup(request);
    ASSERT_FALSE(response.results.isEmpty());
    const Result result = response.results.first();
    ASSERT_EQ(result.dictionary.id, fixture.names);

    ASSERT_TRUE(fixture.manager().remove(fixture.names));
    fixture.publish();
    EXPECT_EQ(handleFor(fixture.manager().snapshot(), fixture.names), nullptr);

    // The store the Result names is still open and still answering, and the record it holds is
    // still readable.
    ASSERT_TRUE(result.dictionary.store != nullptr);
    EXPECT_TRUE(result.dictionary.store->isOpen());
    EXPECT_FALSE(result.dictionary.store->meta(dict::metakeys::title).isEmpty());
    EXPECT_EQ(dict::primarySpelling(*result.record), result.primarySpelling);
    // The name dictionary is gone from the set the engine answers from.
    EXPECT_TRUE(fixture.engine().lookupText(u"田中"_s, LookupCategory::Name, 10).isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
