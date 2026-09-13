// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The lookup engine: a paragraph and a cursor position in, an ordered list of dictionary
// entries out.
//
// Ported from JL (Apache-2.0) at commit 85ae02eeb84f378387f48c12e7b468390a9f2007: JL.Core/Lookup/LookupUtils.cs
// LookupText for the pipeline and JL.Windows/GUI/Popup/PopupWindow.xaml.cs for the span preparation and the highlight.
//
// lookup() is const and safe to call from several threads at once, which is what lets scan/ run
// it on the global thread pool while the GUI thread keeps drawing. It reads no dict::Dictionary
// and no dict::DictionaryManager: the dictionary set is a dict::DictionarySnapshot the owner
// pushes through setDictionaries() on the GUI thread, and one lookup copies that shared_ptr once
// and works from the copy. The engine therefore stays correct while the manager adds, removes,
// reorders, disables or reimports a dictionary, and it outlives the manager safely.
//
// The owner pushes on every dict::DictionaryManager::changed(). setDictionaries() drops the
// result cache itself; invalidateCache() is for the settings that change what a lookup asks for
// rather than which dictionaries answer it.
//
// setVariantOptions() adds a second pass after the exact one: every candidate longer than the
// longest exact match is probed again in the word dictionaries under its one-character
// substitution variants (lookup/ocrvariants.h), so a word a recognition error broke is still
// found. A variant result is kept only where it is longer than every exact result, which puts it
// first under criterion 1, and where lookup::acceptsVariantResult() accepts it.
#pragma once

#include "core/enums.h"
#include "deconj/deconjugator.h"
#include "dict/dictionary.h"
#include "dict/wordclasses.h"
#include "lookup/lookuptypes.h"
#include "lookup/ocrvariants.h"

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QString>

#include <array>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>

namespace maru::dict
{
class DictionaryManager;
}

namespace maru::lookup
{

// Entries the result cache keeps. One entry is a span, so a pointer travelling along a line of
// text refills it with every character; 256 covers a paragraph of hovering with room to spare.
inline constexpr int cacheCapacity = 256;

class Engine
{
public:
    // rules is shared const state that has to outlive the engine: deconj::deconjugate() only
    // reads it, and Form::lastTag points into its arena. The dictionary set starts empty and
    // arrives through setDictionaries().
    explicit Engine(const deconj::RuleSet &rules);
    // Seeds the dictionary set from dictionaries. The reference is used by the constructor alone
    // and not kept, so the caller still pushes every later republish through setDictionaries().
    Engine(dict::DictionaryManager &dictionaries, const deconj::RuleSet &rules);
    ~Engine();

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;

    // Replaces the dictionary set every later lookup runs against, and drops the result cache,
    // whose entries were produced from the previous set. Called on the owner's thread after every
    // dict::DictionaryManager mutation; a lookup already running finishes on the set it started
    // with, which stays alive because its handles hold their stores.
    void setDictionaries(dict::DictionarySnapshot dictionaries,
                         std::shared_ptr<const dict::WordClassTable> wordClasses);

    // The results for the text at request.cursorIndex, ordered by lessThan().
    //
    // The span looked up starts at the cursor, is clamped to request.maxSearchLength and to the
    // longest key any enabled dictionary holds, and ends at the first bracket or sentence
    // terminator at or after the cursor. A cursor on a low surrogate steps back onto its pair,
    // and a cursor on whitespace or on a character no dictionary can start a word with returns
    // an empty Response rather than an error.
    [[nodiscard]] Response lookup(const Request &request) const;

    // The pipeline without the span preparation, over a span that already starts at the cursor.
    // This is the level the cache sits at.
    [[nodiscard]] QList<Result> lookupText(QStringView spanFromCursor, LookupCategory category, int maxResults) const;

    // The recognition-variant pass for every later lookup. Off on construction, so a caller that
    // sets nothing runs JL's pipeline; Application applies the LookupVariant* settings, and
    // LookupOcrVariants defaults to on. The options are part of the cache key, so no
    // invalidateCache() is needed after a change.
    void setVariantOptions(const VariantOptions &options);
    [[nodiscard]] VariantOptions variantOptions() const;

    // Drops every cached response.
    void invalidateCache();

    // Number of cached spans, for the tests.
    [[nodiscard]] int cachedSpanCount() const;

private:
    // One dictionary set, classified for one lookup category. The lists hold handles by value, so
    // the stores they name stay open for as long as the selection does.
    struct Selection
    {
        dict::DictionarySnapshot source;
        std::shared_ptr<const dict::WordClassTable> wordClasses;
        QList<dict::DictionaryHandle> words;
        QList<dict::DictionaryHandle> names;
        QList<dict::DictionaryHandle> kanji;
        QList<dict::DictionaryHandle> frequencies;
        QList<dict::DictionaryHandle> kanjiFrequencies;
        QList<dict::DictionaryHandle> pitchAccents;
        // The JMdict store the kanji card's example words come from, and the cjkvi-ids store its
        // component characters come from.
        std::optional<dict::DictionaryHandle> examples;
        std::optional<dict::DictionaryHandle> components;
        int maxKeyLength = 0;
    };

    struct CacheKey
    {
        // The set the entry was produced from. An entry keyed on a superseded snapshot never
        // answers a lookup running on the current one, which is what makes a stale cache a memory
        // cost rather than a wrong answer.
        dict::DictionarySnapshot dictionaries;
        QString span;
        LookupCategory category = LookupCategory::All;
        int maxResults = 0;
        // Default-constructed, which is disabled, for every lookup the pass does not run on.
        VariantOptions variants;
        // One byte per code unit of span, nonzero where the variant pass may substitute it;
        // empty where the request carried no confidences or the pass is off.
        QByteArray substitutable;

        [[nodiscard]] bool operator==(const CacheKey &other) const = default;
    };

    struct CacheKeyHash
    {
        [[nodiscard]] std::size_t operator()(const CacheKey &key) const noexcept;
    };

    using CacheEntry = std::pair<CacheKey, QList<Result>>;

    // The number of LookupCategory values, which is the width of the per-category classification
    // cache.
    static constexpr std::size_t categoryCount = 4;

    // The selection for category over the currently published set, classified on the first call
    // per set and per category and kept until the next setDictionaries(). Shared rather than
    // copied, so a repeated hover costs one atomic increment rather than one per handle.
    [[nodiscard]] std::shared_ptr<const Selection> selectionFor(LookupCategory category) const;
    [[nodiscard]] static std::shared_ptr<const Selection>
    classify(const dict::DictionarySnapshot &dictionaries,
             const std::shared_ptr<const dict::WordClassTable> &wordClasses,
             LookupCategory category);
    [[nodiscard]] QList<Result> lookupText(QStringView spanFromCursor,
                                           LookupCategory category,
                                           int maxResults,
                                           const Selection &dictionaries,
                                           const VariantOptions &variants,
                                           const QByteArray &substitutable) const;
    [[nodiscard]] QList<Result> lookupUncached(QStringView spanFromCursor,
                                               int maxResults,
                                               const Selection &dictionaries,
                                               const VariantOptions &variants,
                                               const QByteArray &substitutable) const;
    static void addKanjiCard(Result &result, const Selection &dictionaries);

    const deconj::RuleSet &m_rules;

    mutable QMutex m_mutex;
    dict::DictionarySnapshot m_dictionaries;
    std::shared_ptr<const dict::WordClassTable> m_wordClasses;
    VariantOptions m_variantOptions;
    mutable std::array<std::shared_ptr<const Selection>, categoryCount> m_selections;
    mutable std::list<CacheEntry> m_cache;
    mutable std::unordered_map<CacheKey, std::list<CacheEntry>::iterator, CacheKeyHash> m_cacheIndex;
};

} // namespace maru::lookup
