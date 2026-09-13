// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// One dictionary in the user's list: its identity, its source, its render options and its opened
// store.
//
// The identity is a QUuid rather than the display name, so renaming a dictionary changes the
// configuration alone. JL keys its files on the name and therefore has to rename the SQLite file
// atomically when the user edits it (JL.Windows/GUI/Dictionary/EditDictionaryWindow.xaml.cs).
#pragma once

#include "core/enums.h"
#include "dict/dicttypes.h"
#include "dict/records.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QUuid>

#include <cstddef>
#include <memory>
#include <vector>

namespace maru::dict
{

class Store;

// Per-dictionary render toggles and update interval. All dictionary types use the
// same storage backend; the dialog exposes only options applicable to that type.
struct DictOptions
{
    // Render options. All of them are read by popup/renderer.
    bool newlineBetweenDefinitions = true;
    bool wordClassInfo = true;
    bool dialectInfo = true;
    bool primarySpellingOrthographyInfo = true;
    bool alternativeSpellingOrthographyInfo = true;
    bool readingOrthographyInfo = true;
    bool fieldInfo = true;
    bool spellingRestrictionInfo = true;
    bool extraDefinitionInfo = true;
    bool miscInfo = true;
    bool loanwordEtymology = true;
    bool crossReferences = true;
    bool showImages = true;

    // Excludes the dictionary from LookupCategory::All while leaving it reachable through its own
    // category. JL calls this NoAll.
    bool excludeFromAll = false;

    // A higher value is more frequent, which an occurrence-count corpus needs and a rank list does
    // not. Set from index.json's frequencyMode == "occurrence-based" at import.
    bool higherValueMeansHigherFrequency = false;

    // Import options. Changing one of these forces a re-import, which
    // optionsRequireReimport() reports.
    bool properNameEntries = true;

    // Days between automatic update checks. 0 disables the check.
    int autoUpdateAfterDays = 0;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static DictOptions fromJson(const QJsonObject &object);

    [[nodiscard]] bool operator==(const DictOptions &other) const = default;
};

// Whether moving from before to after changes what the store holds rather than how it is
// rendered. The manager shows a "this will re-import the dictionary" message when it is true.
[[nodiscard]] bool optionsRequireReimport(const DictOptions &before, const DictOptions &after);

// The lookup category a dictionary type answers, which is JL's routing in
// DictUtils.cs. A pitch-accent dictionary answers none of them: it decorates the results
// of the others.
[[nodiscard]] bool isWordDictionaryType(DictType type);
[[nodiscard]] bool isNameDictionaryType(DictType type);
[[nodiscard]] bool isKanjiDictionaryType(DictType type);
[[nodiscard]] bool isFrequencyType(DictType type);
[[nodiscard]] bool isPitchAccentType(DictType type);

// Whether a dictionary of this type is queried for category. Applies neither the enabled flag nor
// DictOptions::excludeFromAll, which the caller checks.
[[nodiscard]] bool answersCategory(DictType type, LookupCategory category);

// The type's user-visible name. Not translated here: the manager wraps it in i18n().
[[nodiscard]] QString dictTypeName(DictType type);

// One enabled dictionary as a lookup thread sees it: the fields a query, the ranking and the popup
// adapter read, plus a shared_ptr to the Store, so holding the handle keeps that Store alive.
// Copying a handle copies one shared_ptr and five value fields, which is what makes it safe to
// carry into a QThreadPool thread and to keep inside a cached lookup::Result.
struct DictionaryHandle
{
    QUuid id;
    DictType type = DictType::JMdict;
    QString name;
    // Ascending, renumbered by DictionaryManager: an enabled dictionary carries 1 or more.
    int priority = 0;
    // The longest key the store holds, in UTF-16 code units.
    int maxKeyLength = 0;
    DictOptions options;
    // Non-null and open in every handle DictionaryManager::snapshot() publishes.
    std::shared_ptr<Store> store;

    // The records stored under normalizedKey. Empty when store is null.
    [[nodiscard]] std::vector<std::shared_ptr<const Record>> find(QStringView normalizedKey) const;
};

// The enabled dictionaries of one instant, in priority order. DictionaryManager builds one on the
// GUI thread on every mutation and publishes it; a lookup thread copies the shared_ptr and reads
// the vector, which no thread mutates after it is published.
using DictionarySnapshot = std::shared_ptr<const std::vector<DictionaryHandle>>;

// The handle of id in snapshot, or nullptr when snapshot carries no handle with that id. The
// pointer is valid for as long as the caller holds snapshot.
[[nodiscard]] const DictionaryHandle *handleFor(const DictionarySnapshot &snapshot, const QUuid &id);

struct Dictionary
{
    // Stable for the life of the dictionary. Names the database file and the sidecar.
    QUuid id;
    DictType type = DictType::JMdict;
    // Unique, user-visible, renameable.
    QString name;
    // The file or directory the user chose, or the path a download wrote. Empty for a built-in
    // seed that has not been downloaded yet.
    QString sourcePath;
    // The EDRDG gzip URL for a built-in, or the Yomitan index.json endpoint.
    QUrl updateUrl;
    // The Yomitan index.json revision string, which a newer value on the endpoint invalidates.
    QString revision;

    bool enabled = true;
    // Ascending: the dictionary with the smallest priority is rendered first.
    int priority = 0;
    bool autoUpdatable = false;
    // Seeded by the manager and not removable, only disabled.
    bool builtIn = false;

    qint64 recordCount = 0;
    qint64 keyCount = 0;
    // The longest key the store holds, which caps the candidate prefix list the lookup engine
    // builds for this dictionary.
    int maxKeyLength = 0;
    QDateTime importedAt;
    QDateTime sourceModifiedAt;

    DictOptions options;

    // Opened by DictionaryManager while it builds a snapshot, on the GUI thread. Shared, so a
    // DictionaryHandle a lookup thread holds outlives a manager operation that replaces the entry.
    std::shared_ptr<Store> store;
    // Set when the store on disk was written by another schema or codec version. The manager
    // offers a re-import and the lookup engine skips the dictionary.
    bool needsReimport = false;
    // Set when Store::open() failed on the file this entry names. Not serialized: it stops
    // DictionaryManager reopening the same unreadable file, and warning about it, on every
    // snapshot it publishes. Cleared by the operations that change what is on disk. An entry
    // whose importedAt is invalid is skipped before any open is attempted, so an undownloaded
    // built-in seed leaves this false.
    bool storeUnavailable = false;

    // The records stored under normalizedKey. Empty when store is null. The shared_ptr is copied
    // before the query runs, so a concurrent operation that replaces store leaves the query
    // reading the Store it started on.
    [[nodiscard]] std::vector<std::shared_ptr<const Record>> find(QStringView normalizedKey) const;

    // Whether the dictionary is queried at all: enabled, opened and not awaiting a re-import.
    [[nodiscard]] bool isReady() const;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Dictionary fromJson(const QJsonObject &object);
};

// <directory>/<id>.db, the store this dictionary reads from. directory is the manager's own,
// which is paths::dictionariesDir() outside a test.
[[nodiscard]] QString databasePathFor(const QString &directory, const QUuid &id);

// <directory>/<id>.pos, the word-class table a JMdict import writes.
[[nodiscard]] QString wordClassTablePathFor(const QString &directory, const QUuid &id);

// <directory>/<id>, where a Yomitan .zip is extracted so its images stay reachable.
[[nodiscard]] QString sourceDirectoryFor(const QString &directory, const QUuid &id);

} // namespace maru::dict
