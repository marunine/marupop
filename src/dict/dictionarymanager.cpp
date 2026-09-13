// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictionarymanager.h"

#include "core/logging.h"
#include "core/paths.h"
#include "dict/dictionarydownloadjob.h"
#include "dict/dictionaryimportjob.h"
#include "dict/importers/customnameimporter.h"
#include "dict/importers/customwordimporter.h"
#include "dict/importers/idsimporter.h"
#include "dict/importers/jmdictimporter.h"
#include "dict/importers/jmnedictimporter.h"
#include "dict/importers/kanjidicimporter.h"
#include "dict/importers/yomitanimporter.h"
#include "dict/store.h"
#include "dict/updatecheckjob.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace maru::dict
{

namespace
{

// The Japanese dictionary order the auto-sort action applies, as patterns over Dictionary::name:
// the frequency lists first, then every other recognized dictionary. Compiled once per process.
const QList<QRegularExpression> &dictionarySortRules()
{
    static const QList<QRegularExpression> rules = [] {
        const QList<QString> patterns{
            QLatin1StringView("^JPDB$"),
            QStringLiteral("^JPDBv2㋕$"),
            QLatin1StringView("^Jiten$"),
            QLatin1StringView("^Innocent Ranked$"),
            QLatin1StringView("^Novels$"),
            QLatin1StringView("^Youtube$"),
            QLatin1StringView("^BCCWJ-LUW$"),
            QLatin1StringView("^BCCWJ$"),
            QLatin1StringView("^CC100$"),
            QStringLiteral("^青空文庫熟語$"),
            QLatin1StringView("^Wikipedia$"),
            QLatin1StringView("^NHK$"),
            QStringLiteral("^大辞泉$"),
            QStringLiteral("^新明解第八版$"),
            QStringLiteral("^大辞林第四版$"),
            QStringLiteral("^三省堂国語辞典第八番$"),
            QStringLiteral("^使い方の分かる 類語例解辞典$"),
            QStringLiteral("^数え方辞典オンライン$"),
            QStringLiteral("^三省堂国語辞典　第八版$"),
            QStringLiteral("^新明解国語辞典　第八版$"),
            QStringLiteral("^漢検漢字辞典　第二版$"),
            QStringLiteral("^現代国語例解辞典　第五版$"),
            QStringLiteral("^岩波国語辞典　第八版$"),
            QStringLiteral("^広辞苑 第七版$"),
            QStringLiteral("^例解学習国語辞典 第十一版$"),
            QStringLiteral("^小学館例解学習国語 第十二版$"),
            QStringLiteral("^デジタル大辞泉$"),
            QStringLiteral("^大辞泉 第二版$"),
            QStringLiteral("^旺文社国語辞典 第十一版$"),
            QStringLiteral("^国語辞典オンライン$"),
            QStringLiteral("^明鏡国語辞典　第二版$"),
            QStringLiteral("^大辞林　第四版$"),
            QStringLiteral("^新選国語辞典　第十版$"),
            QStringLiteral("^精選版　日本国語大辞典$"),
            QStringLiteral("^漢字源$"),
            QStringLiteral("^故事・ことわざ・慣用句オンライン$"),
            QStringLiteral("^四字熟語辞典オンライン$"),
            QStringLiteral("^類語辞典オンライン$"),
            QStringLiteral("^対義語辞典オンライン$"),
            QStringLiteral("^新明解四字熟語辞典$"),
            QStringLiteral("^学研 四字熟語辞典$"),
            QStringLiteral("^実用日本語表現辞典$"),
            QLatin1StringView("^Pixiv.*$"),
            QLatin1StringView("^JA Wikipedia.*$"),
            QStringLiteral("^日本語俗語辞書$"),
            QStringLiteral("^故事ことわざの辞典$"),
            QStringLiteral("^複合語起源$"),
            QStringLiteral("^surasura 擬声語$"),
            QStringLiteral("^語源由来辞典$"),
            QStringLiteral("^weblio古語辞典$"),
            QStringLiteral("^全国方言辞典$"),
            QStringLiteral("^新語時事用語辞典$"),
            QStringLiteral("^漢字林$"),
            QStringLiteral("^福日木健二字熟語$"),
            QStringLiteral("^全訳漢辞海$"),
            QStringLiteral("^KO字源$"),
            QLatin1StringView("^YOJI-JUKUGO$"),
            QStringLiteral("^漢字でGo!.+$"),
            QLatin1StringView("^Jitendex.*$"),
            QLatin1StringView("^JMdict$"),
            QStringLiteral("^NEW斎藤和英大辞典$"),
            QStringLiteral("^新和英$"),
            QLatin1StringView("^JMnedict$"),
            QLatin1StringView(R"(^JMnedict \[[\d-]+\]$)"),
            QStringLiteral("^日本語文法辞典\\(全集\\)$"),
            QStringLiteral("^絵でわかる日本語$"),
            QStringLiteral("^JLPT文法解説まとめ$"),
            QStringLiteral("^どんなときどう使う 日本語表現文型辞典$"),
            QStringLiteral("^毎日のんびり日本語教師$"),
            QLatin1StringView("^Innocent Corpus Kanji$"),
            QLatin1StringView("^Wikipedia Kanji$"),
            QStringLiteral("^青空文庫漢字$"),
            QLatin1StringView("^JPDB Kanji Freq$"),
            QStringLiteral("^漢字辞典オンライン$"),
            QLatin1StringView("^KANJIDIC.*$"),
            QLatin1StringView("^JPDB Kanji$"),
            QLatin1StringView("^mozc Kanji Variants$"),
            QLatin1StringView("^jitai$"),
            QLatin1StringView("^TheKanjiMap Kanji Radicals/Composition$"),
            QLatin1StringView("^Kanji components$"),
            QStringLiteral("^Wiktionary漢字$"),
        };
        QList<QRegularExpression> compiled;
        compiled.reserve(patterns.size());
        for (const QString &pattern : patterns)
            compiled.append(QRegularExpression(pattern));
        return compiled;
    }();
    return rules;
}

} // namespace

QList<BuiltInDictionary> builtInDictionaries()
{
    return {
        {.type = DictType::JMdict,
         .name = QLatin1StringView("JMdict"),
         .url = QLatin1StringView("https://www.edrdg.org/pub/Nihongo/JMdict_e.gz"),
         .fileName = QLatin1StringView("JMdict_e.xml"),
         .priority = 3,
         .enabledByDefault = true},
        {.type = DictType::Kanjidic,
         .name = QLatin1StringView("KANJIDIC2"),
         .url = QLatin1StringView("https://www.edrdg.org/kanjidic/kanjidic2.xml.gz"),
         .fileName = QLatin1StringView("kanjidic2.xml"),
         .priority = 4,
         .enabledByDefault = true},
        {.type = DictType::JMnedict,
         .name = QLatin1StringView("JMnedict"),
         .url = QLatin1StringView("https://www.edrdg.org/pub/Nihongo/JMnedict.xml.gz"),
         .fileName = QLatin1StringView("JMnedict.xml"),
         .priority = 5,
         .enabledByDefault = true},
        // The component list the kanji card shows below the readings. Off by default: it is a
        // 1.4 MB download whose value is limited to the kanji card.
        {.type = DictType::KanjiComponents,
         .name = QLatin1StringView("Kanji components"),
         .url = QLatin1StringView("https://raw.githubusercontent.com/cjkvi/cjkvi-ids/master/ids.txt"),
         .fileName = QLatin1StringView("ids.txt"),
         .priority = 6,
         .enabledByDefault = false},
    };
}

DictionaryManager::DictionaryManager(QObject *parent)
    : DictionaryManager(paths::dictionariesDir(), parent)
{}

DictionaryManager::DictionaryManager(QString directory, QObject *parent)
    : QObject(parent)
    , m_directory(std::move(directory))
{
    QDir().mkpath(m_directory);
}

DictionaryManager::~DictionaryManager() = default;

QString DictionaryManager::listPath() const
{
    return m_directory + QLatin1String("/dictionaries.json");
}

bool DictionaryManager::load()
{
    m_dictionaries.clear();

    QFile file(listPath());
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(logDict) << "Cannot read" << listPath() << file.errorString();
        return false;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(logDict) << "Cannot parse" << listPath() << error.errorString();
        return false;
    }

    const QJsonArray entries = document.object().value(QLatin1String("dictionaries")).toArray();
    for (const auto &entry : entries) {
        auto dictionary = std::make_unique<Dictionary>(Dictionary::fromJson(entry.toObject()));
        m_dictionaries.push_back(std::move(dictionary));
    }
    renumber();
    publishSnapshot();
    Q_EMIT changed();
    return true;
}

bool DictionaryManager::save() const
{
    QJsonArray entries;
    for (const auto &dictionary : m_dictionaries)
        entries.append(dictionary->toJson());

    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("dictionaries"), entries);

    QDir().mkpath(m_directory);
    QSaveFile file(listPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(logDict) << "Cannot write" << listPath() << file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(logDict) << "Cannot commit" << listPath() << file.errorString();
        return false;
    }
    return true;
}

void DictionaryManager::seedBuiltIns()
{
    const QList<BuiltInDictionary> seeds = builtInDictionaries();
    bool added = false;
    for (const BuiltInDictionary &seed : seeds) {
        const bool present = std::ranges::any_of(m_dictionaries, [&seed](const std::unique_ptr<Dictionary> &existing) {
            return existing->builtIn && existing->type == seed.type;
        });
        if (present)
            continue;

        Dictionary dictionary;
        dictionary.id = QUuid::createUuid();
        dictionary.type = seed.type;
        dictionary.name = seed.name;
        dictionary.updateUrl = QUrl(seed.url);
        dictionary.autoUpdatable = true;
        dictionary.builtIn = true;
        dictionary.enabled = seed.enabledByDefault;
        dictionary.priority = seed.priority;
        // The download target is named after the dump rather than after the dictionary id, so a
        // user who already has a copy can point at the same file.
        dictionary.sourcePath = m_directory + QLatin1Char('/') + seed.fileName;
        if (!QFile::exists(dictionary.sourcePath))
            dictionary.sourcePath.clear();

        m_dictionaries.push_back(std::make_unique<Dictionary>(dictionary));
        added = true;
        Q_EMIT dictionaryAdded(dictionary.id);
    }

    if (added) {
        renumber();
        publishSnapshot();
        Q_EMIT changed();
    }
}

void DictionaryManager::renumber()
{
    std::ranges::stable_sort(m_dictionaries,
                             [](const std::unique_ptr<Dictionary> &left, const std::unique_ptr<Dictionary> &right) {
                                 return left->priority < right->priority;
                             });
    int priority = 1;
    for (auto &dictionary : m_dictionaries)
        dictionary->priority = priority++;
}

QList<Dictionary *> DictionaryManager::dictionaries() const
{
    QList<Dictionary *> result;
    result.reserve(static_cast<qsizetype>(m_dictionaries.size()));
    for (const auto &dictionary : m_dictionaries)
        result.append(dictionary.get());
    return result;
}

DictionarySnapshot DictionaryManager::snapshot() const
{
    QMutexLocker locker(&m_publishedMutex);
    return m_snapshot;
}

std::shared_ptr<const WordClassTable> DictionaryManager::wordClasses() const
{
    QMutexLocker locker(&m_publishedMutex);
    return m_publishedWordClasses;
}

Store *DictionaryManager::openStore(Dictionary &entry, QList<QUuid> &readyIds, QList<QUuid> &changedIds)
{
    if (entry.store && entry.store->isOpen())
        return entry.store.get();
    // A built-in seed the user has not downloaded yet has no store, which is a state rather
    // than a failure: Store::open() would report the absent file as a warning on a list that
    // ships three such entries, so the entry is skipped without a stat.
    if (!entry.importedAt.isValid())
        return nullptr;
    // One Store::open() attempt per state of the files on disk. A snapshot is republished on
    // every mutation and would otherwise reopen, and warn about, a store that failed each time.
    if (entry.storeUnavailable)
        return nullptr;

    auto opened = std::make_shared<Store>();
    const OpenResult result = opened->open(databasePathFor(m_directory, entry.id));
    if (result == OpenResult::NeedsReimport) {
        entry.needsReimport = true;
        entry.storeUnavailable = true;
        changedIds.append(entry.id);
        return nullptr;
    }
    if (result != OpenResult::Ok) {
        entry.storeUnavailable = true;
        return nullptr;
    }

    entry.needsReimport = false;
    entry.store = std::move(opened);
    entry.recordCount = entry.store->recordCount();
    entry.keyCount = entry.store->keyCount();
    entry.maxKeyLength = entry.store->maxKeyLength();
    readyIds.append(entry.id);
    return entry.store.get();
}

void DictionaryManager::publishSnapshot()
{
    auto handles = std::make_shared<std::vector<DictionaryHandle>>();
    handles->reserve(m_dictionaries.size());

    QList<QUuid> readyIds;
    QList<QUuid> changedIds;
    // m_dictionaries is kept in priority order by renumber() and by move(), so the handles come
    // out in that order without a second sort.
    for (const auto &dictionary : m_dictionaries) {
        if (!dictionary->enabled || dictionary->needsReimport)
            continue;
        if (openStore(*dictionary, readyIds, changedIds) == nullptr)
            continue;
        handles->push_back(DictionaryHandle{.id = dictionary->id,
                                            .type = dictionary->type,
                                            .name = dictionary->name,
                                            .priority = dictionary->priority,
                                            .maxKeyLength = dictionary->maxKeyLength,
                                            .options = dictionary->options,
                                            .store = dictionary->store});
    }

    {
        QMutexLocker locker(&m_publishedMutex);
        m_snapshot = std::move(handles);
    }

    // Emitted after the publish, so a slot that reads the snapshot sees the one the signal is
    // about, and after the walk above, so a slot that adds or removes an entry cannot invalidate
    // the iterator it was emitted from.
    for (const QUuid &id : std::as_const(changedIds))
        Q_EMIT dictionaryChanged(id);
    for (const QUuid &id : std::as_const(readyIds))
        Q_EMIT ready(id);
}

QList<Dictionary *> DictionaryManager::enabledDictionaries()
{
    publishSnapshot();
    const DictionarySnapshot published = snapshot();
    QList<Dictionary *> result;
    result.reserve(static_cast<qsizetype>(published->size()));
    for (const DictionaryHandle &handle : *published) {
        if (Dictionary *entry = dictionary(handle.id))
            result.append(entry);
    }
    return result;
}

QList<Dictionary *> DictionaryManager::enabledDictionariesOfType(DictType type)
{
    QList<Dictionary *> result;
    const QList<Dictionary *> enabled = enabledDictionaries();
    for (Dictionary *dictionary : enabled) {
        if (dictionary->type == type)
            result.append(dictionary);
    }
    return result;
}

Dictionary *DictionaryManager::dictionary(const QUuid &id) const
{
    for (const auto &dictionary : m_dictionaries) {
        if (dictionary->id == id)
            return dictionary.get();
    }
    return nullptr;
}

Dictionary *DictionaryManager::dictionaryNamed(const QString &name) const
{
    for (const auto &dictionary : m_dictionaries) {
        if (dictionary->name == name)
            return dictionary.get();
    }
    return nullptr;
}

Dictionary *DictionaryManager::add(const Dictionary &dictionary)
{
    auto stored = std::make_unique<Dictionary>(dictionary);
    if (stored->id.isNull())
        stored->id = QUuid::createUuid();
    if (stored->priority == 0)
        stored->priority = static_cast<int>(m_dictionaries.size()) + 1;

    Dictionary *pointer = stored.get();
    m_dictionaries.push_back(std::move(stored));
    renumber();
    publishSnapshot();
    Q_EMIT dictionaryAdded(pointer->id);
    Q_EMIT changed();
    return pointer;
}

bool DictionaryManager::remove(const QUuid &id)
{
    // Every caller reaches the id through the entry the manager owns (dictionaries().at(i)->id
    // and DictionaryModel::m_order both do), so erasing the entry frees the QUuid the parameter
    // binds to. The copy is what dictionaryRemoved() and the file paths below are built from,
    // because they run after the erase.
    const QUuid removedId = id;
    const auto found = std::ranges::find_if(m_dictionaries, [&removedId](const std::unique_ptr<Dictionary> &entry) {
        return entry->id == removedId;
    });
    if (found == m_dictionaries.end())
        return false;

    (*found)->store.reset();
    (*found)->storeUnavailable = false;
    QFile::remove(databasePathFor(m_directory, removedId));
    QFile::remove(keyFilterPathFor(databasePathFor(m_directory, removedId)));
    QFile::remove(wordClassTablePathFor(m_directory, removedId));
    QDir(sourceDirectoryFor(m_directory, removedId)).removeRecursively();

    // A built-in seed is part of the offered set, so removing it leaves the entry and drops the
    // import instead: the manager keeps offering the download.
    if ((*found)->builtIn) {
        (*found)->recordCount = 0;
        (*found)->keyCount = 0;
        (*found)->maxKeyLength = 0;
        (*found)->importedAt = {};
        (*found)->needsReimport = false;
        Q_EMIT dictionaryChanged(removedId);
    } else {
        m_dictionaries.erase(found);
        Q_EMIT dictionaryRemoved(removedId);
    }

    renumber();
    publishSnapshot();
    Q_EMIT changed();
    return true;
}

bool DictionaryManager::move(const QUuid &id, int index)
{
    const auto found = std::ranges::find_if(m_dictionaries, [&id](const std::unique_ptr<Dictionary> &entry) {
        return entry->id == id;
    });
    if (found == m_dictionaries.end())
        return false;

    const auto count = static_cast<int>(m_dictionaries.size());
    const int target = std::clamp(index, 0, count - 1);
    std::unique_ptr<Dictionary> moved = std::move(*found);
    m_dictionaries.erase(found);
    m_dictionaries.insert(m_dictionaries.begin() + target, std::move(moved));

    int priority = 1;
    for (auto &dictionary : m_dictionaries)
        dictionary->priority = priority++;

    publishSnapshot();
    Q_EMIT dictionaryChanged(id);
    Q_EMIT changed();
    return true;
}

bool DictionaryManager::sortDictionaries()
{
    const QList<QRegularExpression> &rules = dictionarySortRules();
    const QList<Dictionary *> before = dictionaries();
    QSet<QUuid> changedIds;

    // renumber() stable-sorts by priority, so ranking each entry by the first rule it matches
    // orders the list by rule and keeps the current order within a rule and among the unmatched.
    for (Dictionary *entry : before) {
        const auto rule = std::ranges::find_if(rules, [entry](const QRegularExpression &pattern) {
            return pattern.match(entry->name).hasMatch();
        });
        entry->priority = static_cast<int>(rule - rules.begin());
        if (rule != rules.end() && !entry->enabled) {
            entry->enabled = true;
            changedIds.insert(entry->id);
        }
    }
    renumber();
    for (std::size_t index = 0; index < m_dictionaries.size(); ++index) {
        if (m_dictionaries[index].get() != before.at(static_cast<qsizetype>(index)))
            changedIds.insert(m_dictionaries[index]->id);
    }
    if (changedIds.isEmpty())
        return false;

    publishSnapshot();
    for (const QUuid &id : std::as_const(changedIds))
        Q_EMIT dictionaryChanged(id);
    Q_EMIT changed();
    return true;
}

bool DictionaryManager::setEnabled(const QUuid &id, bool enabled)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr || entry->enabled == enabled)
        return entry != nullptr;
    entry->enabled = enabled;
    if (!enabled)
        entry->store.reset();
    publishSnapshot();
    Q_EMIT dictionaryChanged(id);
    Q_EMIT changed();
    return true;
}

bool DictionaryManager::rename(const QUuid &id, const QString &name)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr || name.isEmpty())
        return false;
    entry->name = name;
    publishSnapshot();
    Q_EMIT dictionaryChanged(id);
    Q_EMIT changed();
    return true;
}

bool DictionaryManager::setOptions(const QUuid &id, const DictOptions &options, bool *requiresReimport)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr)
        return false;
    const bool reimport = optionsRequireReimport(entry->options, options);
    entry->options = options;
    if (reimport)
        entry->needsReimport = true;
    if (requiresReimport != nullptr)
        *requiresReimport = reimport;
    publishSnapshot();
    Q_EMIT dictionaryChanged(id);
    Q_EMIT changed();
    return true;
}

bool DictionaryManager::setSourcePath(const QUuid &id, const QString &sourcePath)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr)
        return false;
    entry->sourcePath = sourcePath;
    entry->storeUnavailable = false;
    publishSnapshot();
    Q_EMIT dictionaryChanged(id);
    Q_EMIT changed();
    return true;
}

Store *DictionaryManager::store(const QUuid &id)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr)
        return nullptr;

    // Opened here rather than by publishSnapshot() alone, so that a disabled entry, which no
    // snapshot names, still opens on request.
    QList<QUuid> readyIds;
    QList<QUuid> changedIds;
    Store *opened = openStore(*entry, readyIds, changedIds);
    publishSnapshot();
    for (const QUuid &changedId : std::as_const(changedIds))
        Q_EMIT dictionaryChanged(changedId);
    for (const QUuid &readyId : std::as_const(readyIds))
        Q_EMIT ready(readyId);
    return opened;
}

// Not const: the entry it reaches through dictionary() is the one it drops the store of.
// NOLINTNEXTLINE(readability-make-member-function-const)
void DictionaryManager::closeStore(const QUuid &id)
{
    Dictionary *entry = dictionary(id);
    if (entry != nullptr) {
        entry->store.reset();
        entry->storeUnavailable = false;
    }
    publishSnapshot();
}

void DictionaryManager::openAll()
{
    publishSnapshot();
}

bool DictionaryManager::loadWordClassTable()
{
    // A new table rather than a cleared one: a lookup thread may be reading the table published
    // last, and a published WordClassTable is never written again.
    auto table = std::make_shared<WordClassTable>();
    bool loaded = false;
    for (const auto &dictionary : m_dictionaries) {
        if (dictionary->type != DictType::JMdict || !dictionary->enabled)
            continue;
        const QString path = wordClassTablePathFor(m_directory, dictionary->id);
        if (QFile::exists(path)) {
            loaded = table->load(path);
            break;
        }
    }

    m_wordClasses = std::move(table);
    QMutexLocker locker(&m_publishedMutex);
    m_publishedWordClasses = m_wordClasses;
    return loaded;
}

DictionaryImportJob *DictionaryManager::createImportJob(const QUuid &id)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr || entry->sourcePath.isEmpty())
        return nullptr;

    std::unique_ptr<Importer> importer;
    switch (entry->type) {
    case DictType::JMdict:
        importer = std::make_unique<JmdictImporter>(entry->options.properNameEntries);
        break;
    case DictType::JMnedict:
        importer = std::make_unique<JmnedictImporter>();
        break;
    case DictType::Kanjidic:
        importer = std::make_unique<KanjidicImporter>();
        break;
    case DictType::CustomWord:
        importer = std::make_unique<CustomWordImporter>();
        break;
    case DictType::CustomName:
        importer = std::make_unique<CustomNameImporter>();
        break;
    case DictType::KanjiComponents:
        importer = std::make_unique<IdsImporter>();
        break;
    default: {
        auto yomitan = std::make_unique<YomitanImporter>(entry->type);
        yomitan->setExtractDirectory(sourceDirectoryFor(m_directory, id));
        importer = std::move(yomitan);
        break;
    }
    }

    QHash<QString, QString> meta;
    meta.insert(QString(metakeys::title), entry->name);
    if (!entry->revision.isEmpty())
        meta.insert(QString(metakeys::revision), entry->revision);
    if (!entry->updateUrl.isEmpty())
        meta.insert(QString(metakeys::sourceUrl), entry->updateUrl.toString());

    auto *job =
        new DictionaryImportJob(std::move(importer), entry->sourcePath, databasePathFor(m_directory, id), entry->type);
    job->setStoreMeta(meta);
    return job;
}

bool DictionaryManager::applyImportResult(const QUuid &id, DictionaryImportJob *job)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr || job == nullptr)
        return false;

    const ImportResult &result = job->result();
    if (!result.ok)
        return false;

    // A JMdict import produces the table the deconjugation gate reads. It lives beside the store
    // rather than inside it, because the gate is consulted for every candidate of every
    // dictionary and a file the manager holds in RAM answers without a query.
    if (entry->type == DictType::JMdict) {
        if (auto *jmdict = dynamic_cast<JmdictImporter *>(job->importer())) {
            if (!jmdict->wordClassTable().save(wordClassTablePathFor(m_directory, id)))
                qCWarning(logDictImport) << "Cannot save the word-class table for" << entry->name;
        }
    }

    // A Yomitan import records the index.json fields the update check needs.
    if (auto *yomitan = dynamic_cast<YomitanImporter *>(job->importer())) {
        const YomitanIndex &index = yomitan->index();
        if (index.valid) {
            entry->revision = index.revision;
            entry->autoUpdatable = index.isUpdatable;
            if (!index.indexUrl.isEmpty())
                entry->updateUrl = QUrl(index.indexUrl);
            if (index.frequencyMode == QLatin1String("occurrence-based"))
                entry->options.higherValueMeansHigherFrequency = true;
        }
        if (!yomitan->resolvedPath().isEmpty())
            entry->sourcePath = yomitan->resolvedPath();
    }

    entry->recordCount = result.recordCount;
    entry->keyCount = result.keyCount;
    entry->maxKeyLength = result.maxKeyLength;
    entry->importedAt = QDateTime::currentDateTimeUtc();
    entry->needsReimport = false;
    entry->storeUnavailable = false;
    entry->store.reset();

    (void)store(id);
    if (entry->type == DictType::JMdict)
        (void)loadWordClassTable();

    Q_EMIT dictionaryChanged(id);
    Q_EMIT changed();
    return true;
}

DictionaryDownloadJob *DictionaryManager::createDownloadJob(const QUuid &id)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr || !entry->updateUrl.isValid() || entry->updateUrl.isEmpty())
        return nullptr;

    QString target = entry->sourcePath;
    if (target.isEmpty()) {
        // A built-in seed with no source yet: the file name comes from the built-in table so a
        // second download of the same dump reuses the same file.
        const QList<BuiltInDictionary> seeds = builtInDictionaries();
        for (const BuiltInDictionary &seed : seeds) {
            if (seed.type == entry->type) {
                target = m_directory + QLatin1Char('/') + seed.fileName;
                break;
            }
        }
    }
    if (target.isEmpty())
        return nullptr;

    return new DictionaryDownloadJob(entry->updateUrl, target);
}

// Not const: a job the caller starts operates on the manager's own entry.
// NOLINTNEXTLINE(readability-make-member-function-const)
UpdateCheckJob *DictionaryManager::createUpdateCheckJob(const QUuid &id)
{
    Dictionary *entry = dictionary(id);
    if (entry == nullptr)
        return nullptr;
    return new UpdateCheckJob(*entry);
}

QList<Dictionary *> DictionaryManager::dictionariesDueForUpdate() const
{
    QList<Dictionary *> due;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const auto &dictionary : m_dictionaries) {
        if (!dictionary->enabled || !dictionary->autoUpdatable || dictionary->options.autoUpdateAfterDays <= 0)
            continue;
        if (!dictionary->importedAt.isValid() ||
            dictionary->importedAt.daysTo(now) >= dictionary->options.autoUpdateAfterDays) {
            due.append(dictionary.get());
        }
    }
    return due;
}

} // namespace maru::dict
