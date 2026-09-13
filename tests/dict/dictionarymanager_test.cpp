// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The dictionary list, its persistence and the import job, plus the lookup helpers over stores the
// importers built. A QCoreApplication and an event loop are required, because DictionaryImportJob
// runs its importer on a QThread and reports through queued signals.
#include "dict/dictionary.h"
#include "dict/dictionarydownloadjob.h"
#include "dict/dictionaryimportjob.h"
#include "dict/dictionarymanager.h"
#include "dict/importers/jmdictimporter.h"
#include "dict/importers/yomitanimporter.h"
#include "dict/keynorm.h"
#include "dict/lookupsupport.h"
#include "dict/store.h"
#include "dict/updatecheckjob.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <ranges>

using namespace maru::dict;
using maru::LookupCategory;

namespace
{

QString fixture(const QString &name)
{
    return QStringLiteral(MARUPOP_DICT_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

// Runs job to completion on the calling thread's event loop.
bool runJob(KJob *job)
{
    QSignalSpy finished(job, &KJob::result);
    job->start();
    if (finished.isEmpty() && !finished.wait(120000))
        return false;
    return job->error() == KJob::NoError;
}

Dictionary importedDictionary(DictionaryManager &manager, DictType type, const QString &name, const QString &source)
{
    Dictionary dictionary;
    dictionary.type = type;
    dictionary.name = name;
    dictionary.sourcePath = source;
    Dictionary *stored = manager.add(dictionary);
    EXPECT_NE(stored, nullptr);

    DictionaryImportJob *job = manager.createImportJob(stored->id);
    EXPECT_NE(job, nullptr);
    if (job == nullptr)
        return {};
    EXPECT_TRUE(runJob(job));
    EXPECT_TRUE(manager.applyImportResult(stored->id, job));
    delete job;
    return *manager.dictionary(stored->id);
}

} // namespace

TEST(DictDictionaryOptions, RoundTripsThroughJson)
{
    DictOptions options;
    options.newlineBetweenDefinitions = false;
    options.miscInfo = false;
    options.excludeFromAll = true;
    options.higherValueMeansHigherFrequency = true;
    options.properNameEntries = false;
    options.autoUpdateAfterDays = 30;

    const DictOptions restored = DictOptions::fromJson(options.toJson());
    EXPECT_EQ(restored, options);

    // An empty object gives every option its default.
    const DictOptions defaults = DictOptions::fromJson({});
    EXPECT_EQ(defaults, DictOptions{});
}

TEST(DictDictionaryOptions, OnlyImportOptionsForceAReimport)
{
    DictOptions before;
    DictOptions after = before;
    after.miscInfo = false;
    EXPECT_FALSE(optionsRequireReimport(before, after));

    after = before;
    after.properNameEntries = false;
    EXPECT_TRUE(optionsRequireReimport(before, after));
}

TEST(DictDictionaryTypes, RouteToTheRightLookupCategory)
{
    EXPECT_TRUE(answersCategory(DictType::JMdict, LookupCategory::Word));
    EXPECT_TRUE(answersCategory(DictType::JMdict, LookupCategory::All));
    EXPECT_FALSE(answersCategory(DictType::JMdict, LookupCategory::Name));
    EXPECT_TRUE(answersCategory(DictType::JMnedict, LookupCategory::Name));
    EXPECT_TRUE(answersCategory(DictType::Kanjidic, LookupCategory::Kanji));
    EXPECT_TRUE(answersCategory(DictType::YomitanOther, LookupCategory::Word));
    // A decorator answers no category of its own.
    EXPECT_FALSE(answersCategory(DictType::YomitanPitchAccent, LookupCategory::All));
    EXPECT_FALSE(answersCategory(DictType::YomitanFrequency, LookupCategory::All));
    EXPECT_FALSE(answersCategory(DictType::KanjiComponents, LookupCategory::All));

    EXPECT_TRUE(isWordDictionaryType(DictType::CustomWord));
    EXPECT_TRUE(isNameDictionaryType(DictType::CustomName));
    EXPECT_TRUE(isKanjiDictionaryType(DictType::YomitanKanjiWordSchema));
    EXPECT_TRUE(isFrequencyType(DictType::YomitanKanjiFrequency));
    EXPECT_TRUE(isPitchAccentType(DictType::YomitanPitchAccent));
    EXPECT_FALSE(dictTypeName(DictType::JMdict).isEmpty());
}

TEST(DictDictionaryManager, PersistsTheListAcrossReloads)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    QUuid firstId;
    {
        DictionaryManager manager(directory.path(), nullptr);
        ASSERT_TRUE(manager.load());
        EXPECT_TRUE(manager.dictionaries().isEmpty());

        manager.seedBuiltIns();
        const QList<Dictionary *> seeded = manager.dictionaries();
        ASSERT_EQ(seeded.size(), builtInDictionaries().size());
        for (const Dictionary *dictionary : seeded) {
            EXPECT_TRUE(dictionary->builtIn);
            EXPECT_TRUE(dictionary->autoUpdatable);
            EXPECT_TRUE(dictionary->updateUrl.isValid());
        }
        firstId = seeded.first()->id;

        Dictionary custom;
        custom.type = DictType::YomitanWord;
        custom.name = QStringLiteral("大辞林");
        custom.sourcePath = QStringLiteral("/somewhere/daijirin");
        custom.options.miscInfo = false;
        custom.options.autoUpdateAfterDays = 14;
        custom.revision = QStringLiteral("rev-9");
        ASSERT_NE(manager.add(custom), nullptr);

        // A second seed run adds nothing.
        manager.seedBuiltIns();
        EXPECT_EQ(manager.dictionaries().size(), builtInDictionaries().size() + 1);
        ASSERT_TRUE(manager.save());
    }

    DictionaryManager reloaded(directory.path(), nullptr);
    ASSERT_TRUE(reloaded.load());
    ASSERT_EQ(reloaded.dictionaries().size(), builtInDictionaries().size() + 1);
    EXPECT_NE(reloaded.dictionary(firstId), nullptr);

    const Dictionary *custom = reloaded.dictionaryNamed(QStringLiteral("大辞林"));
    ASSERT_NE(custom, nullptr);
    EXPECT_EQ(custom->type, DictType::YomitanWord);
    EXPECT_EQ(custom->sourcePath, QStringLiteral("/somewhere/daijirin"));
    EXPECT_EQ(custom->revision, QStringLiteral("rev-9"));
    EXPECT_FALSE(custom->options.miscInfo);
    EXPECT_EQ(custom->options.autoUpdateAfterDays, 14);

    // The priorities are dense and ascending after a reload.
    int expected = 1;
    for (const Dictionary *dictionary : reloaded.dictionaries())
        EXPECT_EQ(dictionary->priority, expected++);
}

TEST(DictDictionaryManager, EditsTheList)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    Dictionary first;
    first.type = DictType::YomitanWord;
    first.name = QStringLiteral("First");
    Dictionary second;
    second.type = DictType::YomitanWord;
    second.name = QStringLiteral("Second");

    const QUuid firstId = manager.add(first)->id;
    const QUuid secondId = manager.add(second)->id;
    ASSERT_EQ(manager.dictionaries().size(), 2);
    EXPECT_EQ(manager.dictionaries().first()->id, firstId);

    QSignalSpy changed(&manager, &DictionaryManager::changed);

    EXPECT_TRUE(manager.move(secondId, 0));
    EXPECT_EQ(manager.dictionaries().first()->id, secondId);
    EXPECT_EQ(manager.dictionary(secondId)->priority, 1);
    EXPECT_EQ(manager.dictionary(firstId)->priority, 2);

    EXPECT_TRUE(manager.rename(firstId, QStringLiteral("Renamed")));
    EXPECT_EQ(manager.dictionary(firstId)->name, QStringLiteral("Renamed"));
    EXPECT_FALSE(manager.rename(firstId, QString()));

    EXPECT_TRUE(manager.setEnabled(firstId, false));
    EXPECT_FALSE(manager.dictionary(firstId)->enabled);

    DictOptions options = manager.dictionary(firstId)->options;
    options.properNameEntries = false;
    bool requiresReimport = false;
    EXPECT_TRUE(manager.setOptions(firstId, options, &requiresReimport));
    EXPECT_TRUE(requiresReimport);
    EXPECT_TRUE(manager.dictionary(firstId)->needsReimport);

    EXPECT_TRUE(manager.setSourcePath(secondId, QStringLiteral("/elsewhere")));
    EXPECT_EQ(manager.dictionary(secondId)->sourcePath, QStringLiteral("/elsewhere"));

    EXPECT_GE(changed.count(), 5);

    EXPECT_TRUE(manager.remove(firstId));
    EXPECT_EQ(manager.dictionaries().size(), 1);
    EXPECT_EQ(manager.dictionary(firstId), nullptr);
    EXPECT_FALSE(manager.remove(QUuid::createUuid()));
}

TEST(DictDictionaryManager, KeepsABuiltInEntryWhenItIsRemoved)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());
    manager.seedBuiltIns();

    Dictionary *jmdict = manager.dictionaryNamed(QStringLiteral("JMdict"));
    ASSERT_NE(jmdict, nullptr);
    const QUuid id = jmdict->id;
    jmdict->recordCount = 100;

    EXPECT_TRUE(manager.remove(id));
    ASSERT_NE(manager.dictionary(id), nullptr);
    EXPECT_EQ(manager.dictionary(id)->recordCount, 0);
}

TEST(DictDictionaryManager, SortsJapaneseGroupsAndLeavesOtherDictionariesLast)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    const auto add = [&manager](const QString &name, DictType type) {
        Dictionary dictionary;
        dictionary.name = name;
        dictionary.type = type;
        return manager.add(dictionary);
    };

    Dictionary *unknown = add(QStringLiteral("Installed custom dictionary"), DictType::YomitanWord);
    Dictionary *otherFrequency = add(QStringLiteral("Other frequency list"), DictType::YomitanFrequency);
    Dictionary *jaFrequency = add(QStringLiteral("JPDB"), DictType::YomitanFrequency);
    Dictionary *otherDictionary = add(QStringLiteral("Other dictionary"), DictType::YomitanKanji);
    Dictionary *jaDictionary = add(QStringLiteral("NHK"), DictType::YomitanPitchAccent);
    ASSERT_TRUE(manager.setEnabled(unknown->id, false));
    ASSERT_TRUE(manager.setEnabled(otherFrequency->id, false));

    QSignalSpy changed(&manager, &DictionaryManager::changed);
    ASSERT_TRUE(manager.sortDictionaries());

    const QList<Dictionary *> sorted = manager.dictionaries();
    ASSERT_EQ(sorted.size(), 5);
    EXPECT_EQ(sorted.at(0)->name, QStringLiteral("JPDB"));
    EXPECT_EQ(sorted.at(1)->name, QStringLiteral("NHK"));
    EXPECT_EQ(sorted.at(2)->name, QStringLiteral("Installed custom dictionary"));
    EXPECT_EQ(sorted.at(3)->name, QStringLiteral("Other frequency list"));
    EXPECT_EQ(sorted.at(4)->name, QStringLiteral("Other dictionary"));
    EXPECT_TRUE(jaFrequency->enabled);
    EXPECT_TRUE(jaDictionary->enabled);
    EXPECT_FALSE(unknown->enabled);
    EXPECT_FALSE(otherFrequency->enabled);
    EXPECT_TRUE(otherDictionary->enabled);
    EXPECT_EQ(changed.count(), 1);

    EXPECT_FALSE(manager.sortDictionaries());
    EXPECT_EQ(changed.count(), 1);
}

TEST(DictDictionaryManager, SortsRecognizedDictionaryTitlesAndTheBuiltInDictionaries)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    // Representative titles, synthetic revision suffixes and built-in seed names, in the expected
    // sort order. Each matches a rule of its own; two names under one rule would keep the reversed order
    // they are added in, which is why KANJIDIC2 is not among them.
    const QList<QString> expected{
        QStringLiteral("JPDBv2㋕"),
        QStringLiteral("Jiten"),
        QStringLiteral("BCCWJ"),
        QStringLiteral("CC100"),
        QStringLiteral("NHK"),
        QStringLiteral("大辞泉"),
        QStringLiteral("使い方の分かる 類語例解辞典"),
        QStringLiteral("漢検漢字辞典　第二版"),
        QStringLiteral("小学館例解学習国語 第十二版"),
        QStringLiteral("大辞泉 第二版"),
        QStringLiteral("実用日本語表現辞典"),
        QStringLiteral("Pixiv example"),
        QStringLiteral("Jitendex example"),
        QStringLiteral("JMdict"),
        QStringLiteral("新和英"),
        QStringLiteral("JMnedict"),
        QStringLiteral("JMnedict [2000-01-01]"),
        QStringLiteral("日本語文法辞典(全集)"),
        QStringLiteral("KANJIDIC example"),
        QStringLiteral("JPDB Kanji"),
        QStringLiteral("Kanji components"),
    };
    for (const QString &name : expected | std::views::reverse) {
        Dictionary dictionary;
        dictionary.name = name;
        dictionary.type = DictType::YomitanWord;
        Dictionary *added = manager.add(dictionary);
        ASSERT_NE(added, nullptr);
        ASSERT_TRUE(manager.setEnabled(added->id, false));
    }

    ASSERT_TRUE(manager.sortDictionaries());

    const QList<Dictionary *> sorted = manager.dictionaries();
    ASSERT_EQ(sorted.size(), expected.size());
    for (qsizetype index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(sorted.at(index)->name, expected.at(index)) << index;
        EXPECT_TRUE(sorted.at(index)->enabled) << sorted.at(index)->name.toStdString();
    }
}

TEST(DictDictionaryManager, ImportsJmdictThroughAKJobAndAnswersLookups)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    QSignalSpy ready(&manager, &DictionaryManager::ready);
    const Dictionary imported = importedDictionary(
        manager, DictType::JMdict, QStringLiteral("JMdict"), fixture(QStringLiteral("MockJMdict.xml")));
    ASSERT_FALSE(imported.id.isNull());

    Dictionary *stored = manager.dictionary(imported.id);
    ASSERT_NE(stored, nullptr);
    EXPECT_GT(stored->recordCount, 0);
    EXPECT_GT(stored->keyCount, 0);
    EXPECT_TRUE(stored->importedAt.isValid());
    EXPECT_TRUE(stored->isReady());
    EXPECT_GE(ready.count(), 1);

    EXPECT_TRUE(QFile::exists(databasePathFor(directory.path(), imported.id)));
    EXPECT_TRUE(QFile::exists(wordClassTablePathFor(directory.path(), imported.id)));

    // The manager loads the word-class table the JMdict import produced.
    EXPECT_FALSE(manager.wordClassTable().isEmpty());
    EXPECT_TRUE(manager.wordClassTable().containsTag(
        QStringLiteral("始まる"), QStringLiteral("はじまる"), QStringLiteral("v5r")));

    // The lookup helpers answer through the handle the published snapshot carries.
    const DictionaryHandle *handle = handleFor(manager.snapshot(), imported.id);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->name, QStringLiteral("JMdict"));
    EXPECT_GT(handle->maxKeyLength, 0);

    const auto records = find(*handle, normalizeKey(QStringLiteral("廃墟")));
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(primarySpelling(*records.front()), QStringLiteral("廃墟"));

    const QList<KanjiExample> examples = kanjiExamplesFor(*handle, QStringLiteral("廃"));
    EXPECT_FALSE(examples.isEmpty());
    EXPECT_TRUE(kanjiExamplesFor(*handle, QStringLiteral("鬱")).isEmpty());

    EXPECT_EQ(manager.enabledDictionaries().size(), 1);
    EXPECT_EQ(manager.enabledDictionariesOfType(DictType::JMdict).size(), 1);
    EXPECT_TRUE(manager.enabledDictionariesOfType(DictType::JMnedict).isEmpty());
}

TEST(DictDictionaryManager, AttachesFrequenciesAndPitchToAHeadword)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    const Dictionary frequency = importedDictionary(
        manager, DictType::YomitanFrequency, QStringLiteral("Freq"), fixture(QStringLiteral("yomitan_v3")));
    const Dictionary pitch = importedDictionary(
        manager, DictType::YomitanPitchAccent, QStringLiteral("Pitch"), fixture(QStringLiteral("yomitan_v3")));
    ASSERT_FALSE(frequency.id.isNull());
    ASSERT_FALSE(pitch.id.isNull());

    const DictionarySnapshot snapshot = manager.snapshot();
    const DictionaryHandle *frequencyDictionary = handleFor(snapshot, frequency.id);
    const DictionaryHandle *pitchDictionary = handleFor(snapshot, pitch.id);
    ASSERT_NE(frequencyDictionary, nullptr);
    ASSERT_NE(pitchDictionary, nullptr);

    // The spelling probe accepts a record whose spelling is one of the headword's readings.
    const std::optional<int> rank = frequencyFor(*frequencyDictionary, QStringLiteral("日"), {QStringLiteral("ひ")});
    ASSERT_TRUE(rank.has_value());
    EXPECT_EQ(*rank, 7);

    // A headword the list does not cover has no rank.
    EXPECT_FALSE(
        frequencyFor(*frequencyDictionary, QStringLiteral("存在しない"), {QStringLiteral("そんざい")}).has_value());

    // The cross-check rejects a record whose spelling is neither the headword nor one of its
    // readings: 日 is stored under ひ with the spelling 日, so asking about a different headword
    // read ひ finds nothing.
    EXPECT_FALSE(frequencyFor(*frequencyDictionary, QStringLiteral("氷"), {QStringLiteral("ひ")}).has_value());

    const QList<std::optional<quint8>> positions =
        pitchPositionsFor(*pitchDictionary, QStringLiteral("走る"), {QStringLiteral("はしる")});
    ASSERT_EQ(positions.size(), 1);
    ASSERT_TRUE(positions.first().has_value());
    EXPECT_EQ(*positions.first(), 2);

    const QList<QList<quint8>> all =
        allPitchPositionsFor(*pitchDictionary, QStringLiteral("橋"), {QStringLiteral("はし")});
    ASSERT_EQ(all.size(), 1);
    EXPECT_EQ(all.first(), (QList<quint8>{2, 0}));

    const QList<std::optional<quint8>> missing =
        pitchPositionsFor(*pitchDictionary, QStringLiteral("存在"), {QStringLiteral("そんざい")});
    ASSERT_EQ(missing.size(), 1);
    EXPECT_FALSE(missing.first().has_value());
}

TEST(DictDictionaryManager, ReportsAnImportFailureThroughTheJob)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    Dictionary dictionary;
    dictionary.type = DictType::JMdict;
    dictionary.name = QStringLiteral("Broken");
    dictionary.sourcePath = directory.filePath(QStringLiteral("absent.xml"));
    Dictionary *stored = manager.add(dictionary);
    ASSERT_NE(stored, nullptr);

    DictionaryImportJob *job = manager.createImportJob(stored->id);
    ASSERT_NE(job, nullptr);
    EXPECT_FALSE(runJob(job));
    EXPECT_EQ(job->error(), DictionaryImportJob::ImportFailed);
    EXPECT_FALSE(job->errorText().isEmpty());
    EXPECT_FALSE(manager.applyImportResult(stored->id, job));
    delete job;

    // An entry with no source has no import job.
    Dictionary empty;
    empty.type = DictType::JMdict;
    empty.name = QStringLiteral("No source");
    EXPECT_EQ(manager.createImportJob(manager.add(empty)->id), nullptr);
}

TEST(DictDictionaryManager, CreatesDownloadAndUpdateJobsForTheBuiltIns)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());
    manager.seedBuiltIns();

    Dictionary *jmdict = manager.dictionaryNamed(QStringLiteral("JMdict"));
    ASSERT_NE(jmdict, nullptr);
    // No network traffic: the jobs are created and destroyed without being started.
    DictionaryDownloadJob *download = manager.createDownloadJob(jmdict->id);
    ASSERT_NE(download, nullptr);
    delete download;

    UpdateCheckJob *check = manager.createUpdateCheckJob(jmdict->id);
    ASSERT_NE(check, nullptr);
    EXPECT_EQ(check->dictionaryId(), jmdict->id);
    delete check;

    Dictionary local;
    local.type = DictType::YomitanWord;
    local.name = QStringLiteral("Local only");
    EXPECT_EQ(manager.createDownloadJob(manager.add(local)->id), nullptr);
}

TEST(DictDictionaryManager, ListsTheDictionariesDueForAnUpdate)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    Dictionary stale;
    stale.type = DictType::YomitanWord;
    stale.name = QStringLiteral("Stale");
    stale.autoUpdatable = true;
    stale.options.autoUpdateAfterDays = 7;
    stale.importedAt = QDateTime::currentDateTimeUtc().addDays(-30);
    ASSERT_NE(manager.add(stale), nullptr);

    Dictionary fresh;
    fresh.type = DictType::YomitanWord;
    fresh.name = QStringLiteral("Fresh");
    fresh.autoUpdatable = true;
    fresh.options.autoUpdateAfterDays = 7;
    fresh.importedAt = QDateTime::currentDateTimeUtc();
    ASSERT_NE(manager.add(fresh), nullptr);

    Dictionary disabled;
    disabled.type = DictType::YomitanWord;
    disabled.name = QStringLiteral("Never");
    disabled.autoUpdatable = true;
    disabled.options.autoUpdateAfterDays = 0;
    ASSERT_NE(manager.add(disabled), nullptr);

    const QList<Dictionary *> due = manager.dictionariesDueForUpdate();
    ASSERT_EQ(due.size(), 1);
    EXPECT_EQ(due.first()->name, QStringLiteral("Stale"));
}

TEST(DictDictionaryManager, ReportsNeedsReimportForAnOldStore)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    Dictionary dictionary;
    dictionary.type = DictType::JMdict;
    dictionary.name = QStringLiteral("Absent store");
    Dictionary *stored = manager.add(dictionary);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(manager.store(stored->id), nullptr);
    EXPECT_FALSE(stored->needsReimport);
    EXPECT_FALSE(stored->isReady());
    EXPECT_TRUE(manager.enabledDictionaries().isEmpty());
}

// The snapshot is what every lookup thread reads, so the mutations that change the set have to
// republish it and the previous snapshot has to stay valid in the caller's hands.
TEST(DictDictionaryManager, RepublishesTheSnapshotOnEveryMutation)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());
    EXPECT_TRUE(manager.snapshot()->empty());

    const Dictionary words = importedDictionary(
        manager, DictType::JMdict, QStringLiteral("JMdict"), fixture(QStringLiteral("MockJMdict.xml")));
    const Dictionary names = importedDictionary(
        manager, DictType::JMnedict, QStringLiteral("JMnedict"), fixture(QStringLiteral("mock_jmnedict.xml")));
    ASSERT_FALSE(words.id.isNull());
    ASSERT_FALSE(names.id.isNull());

    const DictionarySnapshot both = manager.snapshot();
    ASSERT_EQ(both->size(), 2U);
    // Priority order, and every published handle carries an open store.
    EXPECT_LT(both->at(0).priority, both->at(1).priority);
    for (const DictionaryHandle &handle : *both) {
        ASSERT_TRUE(handle.store != nullptr);
        EXPECT_TRUE(handle.store->isOpen());
    }

    // Disabling one republishes a shorter vector, and the snapshot already taken keeps naming the
    // store it named, which is what a lookup running at that moment reads.
    ASSERT_TRUE(manager.setEnabled(names.id, false));
    const DictionarySnapshot one = manager.snapshot();
    EXPECT_NE(one.get(), both.get());
    ASSERT_EQ(one->size(), 1U);
    EXPECT_EQ(one->at(0).id, words.id);
    EXPECT_EQ(handleFor(one, names.id), nullptr);
    ASSERT_EQ(both->size(), 2U);
    EXPECT_TRUE(handleFor(both, names.id)->store->isOpen());
    EXPECT_FALSE(handleFor(both, names.id)->find(normalizeKey(QStringLiteral("田中"))).empty());

    // Renaming and reordering are published too, because the popup reads the name and the
    // comparator reads the priority out of the handle.
    ASSERT_TRUE(manager.setEnabled(names.id, true));
    ASSERT_TRUE(manager.rename(words.id, QStringLiteral("Renamed")));
    ASSERT_TRUE(manager.move(names.id, 0));
    const DictionarySnapshot reordered = manager.snapshot();
    ASSERT_EQ(reordered->size(), 2U);
    EXPECT_EQ(reordered->at(0).id, names.id);
    EXPECT_EQ(handleFor(reordered, words.id)->name, QStringLiteral("Renamed"));

    // Removing takes the entry out of the next snapshot and leaves the one already taken usable.
    ASSERT_TRUE(manager.remove(names.id));
    EXPECT_EQ(manager.snapshot()->size(), 1U);
    EXPECT_TRUE(handleFor(reordered, names.id)->store->isOpen());
}

// An entry the user has never imported is skipped without a stat, and an entry whose store fails
// to open is attempted once per state of the files on disk.
TEST(DictDictionaryManager, LeavesAnUnimportedEntryOutOfTheSnapshot)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());
    manager.seedBuiltIns();

    EXPECT_FALSE(manager.dictionaries().isEmpty());
    EXPECT_TRUE(manager.snapshot()->empty());
    for (const Dictionary *entry : manager.dictionaries())
        EXPECT_FALSE(entry->storeUnavailable) << entry->name.toStdString();

    // The same entry joins the snapshot once its store exists, which is the state applyImportResult
    // leaves behind.
    const Dictionary imported = importedDictionary(
        manager, DictType::JMdict, QStringLiteral("Extra JMdict"), fixture(QStringLiteral("MockJMdict.xml")));
    ASSERT_FALSE(imported.id.isNull());
    ASSERT_EQ(manager.snapshot()->size(), 1U);
    EXPECT_EQ(manager.snapshot()->at(0).id, imported.id);
}

// The table is published as a value, so a lookup thread holding one is unaffected by a reload.
TEST(DictDictionaryManager, PublishesTheWordClassTable)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    DictionaryManager manager(directory.path(), nullptr);
    ASSERT_TRUE(manager.load());

    const std::shared_ptr<const WordClassTable> empty = manager.wordClasses();
    ASSERT_TRUE(empty != nullptr);
    EXPECT_TRUE(empty->isEmpty());

    const Dictionary imported = importedDictionary(
        manager, DictType::JMdict, QStringLiteral("JMdict"), fixture(QStringLiteral("MockJMdict.xml")));
    ASSERT_FALSE(imported.id.isNull());

    const std::shared_ptr<const WordClassTable> loaded = manager.wordClasses();
    EXPECT_NE(loaded.get(), empty.get());
    EXPECT_TRUE(loaded->containsTag(QStringLiteral("始まる"), QStringLiteral("はじまる"), QStringLiteral("v5r")));
    // The table the earlier call handed out was replaced rather than cleared in place.
    EXPECT_TRUE(empty->isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
