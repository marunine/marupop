// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The user's dictionary list: persistence, ordering, lazy store opening and the jobs that download
// and import a dictionary.
//
// The list is dictionaries.json in paths::dictionariesDir(), beside the store files it names. JL
// keeps the equivalent in Config/dicts.json and Config/freqs.json; frequency lists are entries of
// this one list here, because they share every operation with the other dictionaries and differ
// only in which options apply.
#pragma once

#include "dict/dictionary.h"
#include "dict/importers/importer.h"
#include "dict/wordclasses.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QUuid>

#include <memory>
#include <vector>

namespace maru::dict
{

class DictionaryDownloadJob;
class DictionaryImportJob;
class Store;
class UpdateCheckJob;

// The EDRDG dumps and the component list marupop offers without the user having to find a URL.
struct BuiltInDictionary
{
    DictType type;
    QLatin1StringView name;
    QLatin1StringView url;
    // The file the download leaves in paths::dictionariesDir().
    QLatin1StringView fileName;
    int priority;
    bool enabledByDefault;
};

// JMdict, KANJIDIC2 and JMnedict in JL's default priority order (DictUtils.BuiltInDicts,
// JL.Core/Dicts/DictUtils.cs), plus cjkvi-ids for the kanji card's component list, which JL
// ships as a prebuilt SQLite instead.
[[nodiscard]] QList<BuiltInDictionary> builtInDictionaries();

class DictionaryManager : public QObject
{
    Q_OBJECT

public:
    explicit DictionaryManager(QObject *parent = nullptr);
    // Uses directory rather than paths::dictionariesDir(), which is what a test points at a
    // temporary directory.
    DictionaryManager(QString directory, QObject *parent);
    ~DictionaryManager() override;

    [[nodiscard]] QString directory() const
    {
        return m_directory;
    }

    // Reads dictionaries.json. Returns false when the file exists and cannot be parsed; an absent
    // file is not an error and leaves an empty list.
    [[nodiscard]] bool load();
    [[nodiscard]] bool save() const;

    // Adds an entry for every built-in dictionary the list does not already carry, with an empty
    // sourcePath until it is downloaded.
    void seedBuiltIns();

    // The currently published snapshot: the enabled entries whose stores are open and current, in
    // priority order, as value handles. Thread-safe, and the one member a thread other than the
    // one the manager lives on may call. Never null; an empty vector before the first publish.
    //
    // The snapshot is rebuilt on the manager's own thread by every operation that changes the set,
    // the order, the options or the store of an enabled dictionary, which is where a store is
    // opened. A dictionary whose store does not open is absent from the snapshot.
    [[nodiscard]] DictionarySnapshot snapshot() const;

    // The word-class table the deconjugation gate reads, as a value a lookup thread can hold.
    // Thread-safe and republished by loadWordClassTable(). Never null.
    [[nodiscard]] std::shared_ptr<const WordClassTable> wordClasses() const;

    // In priority order. The pointers are stable until the dictionary is removed.
    [[nodiscard]] QList<Dictionary *> dictionaries() const;
    // The entries the current snapshot names, in priority order. Republishes the snapshot first,
    // which opens a store that is not open yet, so it runs on the manager's own thread alone.
    [[nodiscard]] QList<Dictionary *> enabledDictionaries();
    // The enabled entries of one type, in priority order.
    [[nodiscard]] QList<Dictionary *> enabledDictionariesOfType(DictType type);

    [[nodiscard]] Dictionary *dictionary(const QUuid &id) const;
    [[nodiscard]] Dictionary *dictionaryNamed(const QString &name) const;

    // Adds dictionary, assigning it an id when it has none and the next free priority when its
    // priority is 0. Returns the stored entry.
    Dictionary *add(const Dictionary &dictionary);
    // Removes the entry and its store file, its sidecar, its word-class table and its extracted
    // source directory. A built-in entry is reset to an undownloaded state rather than removed.
    bool remove(const QUuid &id);
    // Moves the entry to index in the priority order and renumbers the list.
    bool move(const QUuid &id, int index);
    // Orders the list by the Japanese dictionary rules of the auto-sort action and enables every
    // dictionary a rule recognizes. The rest keep their enabled state and their relative order at
    // the end. Returns true when the order or an enabled state changed.
    bool sortDictionaries();
    bool setEnabled(const QUuid &id, bool enabled);
    bool rename(const QUuid &id, const QString &name);
    // Replaces the options. Reports through requiresReimport whether the change invalidates the
    // store.
    bool setOptions(const QUuid &id, const DictOptions &options, bool *requiresReimport = nullptr);
    bool setSourcePath(const QUuid &id, const QString &sourcePath);

    // Opens the store of id, or returns the open one, and republishes the snapshot. Returns
    // nullptr when the file is absent or unreadable; a file written by another schema version sets
    // Dictionary::needsReimport and also returns nullptr.
    Store *store(const QUuid &id);
    void closeStore(const QUuid &id);
    // Opens every enabled dictionary's store, which is what publishing the snapshot does.
    void openAll();

    // The deconjugation gate's table, read from the JMdict entry's <id>.pos file. Empty when no
    // JMdict dictionary has been imported.
    [[nodiscard]] const WordClassTable &wordClassTable() const
    {
        return *m_wordClasses;
    }

    bool loadWordClassTable();

    // A job that imports the dictionary's sourcePath into its store, with the importer its type
    // selects. Returns nullptr when the type has no importer or the source is empty. The caller
    // owns the job and starts it; connect to KJob::result() and call applyImportResult().
    [[nodiscard]] DictionaryImportJob *createImportJob(const QUuid &id);
    // Records the counts a finished import produced, saves a JMdict word-class table, reopens the
    // store and emits ready().
    bool applyImportResult(const QUuid &id, DictionaryImportJob *job);

    // A job that downloads the dictionary's updateUrl into its sourcePath. Returns nullptr when
    // the entry carries no URL.
    [[nodiscard]] DictionaryDownloadJob *createDownloadJob(const QUuid &id);
    [[nodiscard]] UpdateCheckJob *createUpdateCheckJob(const QUuid &id);

    // The entries whose autoUpdateAfterDays has elapsed since importedAt.
    [[nodiscard]] QList<Dictionary *> dictionariesDueForUpdate() const;

Q_SIGNALS:
    void changed();
    void dictionaryAdded(const QUuid &id);
    void dictionaryRemoved(const QUuid &id);
    void dictionaryChanged(const QUuid &id);
    // The store of id is open and answering queries.
    void ready(const QUuid &id);

private:
    void renumber();
    [[nodiscard]] QString listPath() const;
    // Opens the store of entry when it is not open, and reports it. Returns nullptr for an entry
    // with no importable store, which is the state of an undownloaded built-in seed. Records the
    // ids to signal in readyIds and changedIds rather than emitting, so a slot cannot mutate the
    // list the caller is walking.
    Store *openStore(Dictionary &entry, QList<QUuid> &readyIds, QList<QUuid> &changedIds);
    // Rebuilds the handle vector from the enabled entries and publishes it, then emits ready() for
    // every store the rebuild opened and dictionaryChanged() for every entry it marked as needing
    // a re-import.
    void publishSnapshot();

    QString m_directory;
    std::vector<std::unique_ptr<Dictionary>> m_dictionaries;
    std::shared_ptr<WordClassTable> m_wordClasses = std::make_shared<WordClassTable>();

    // Guards the two published values alone. Held for one shared_ptr copy, so a lookup thread
    // reading the snapshot never waits on a store being opened.
    mutable QMutex m_publishedMutex;
    DictionarySnapshot m_snapshot = std::make_shared<const std::vector<DictionaryHandle>>();
    std::shared_ptr<const WordClassTable> m_publishedWordClasses = m_wordClasses;
};

} // namespace maru::dict
