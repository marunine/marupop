// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The input of popup::renderHtml(), popup::renderPlainText() and popup::PopupWindow.
//
// The renderer reads this view model rather than lookup::Response so that popup/ carries no
// dependency on dict/records.h and on the record variant behind lookup::Result::record. The
// adapter from lookup::Response to PopupModel belongs to the wave 2 lookup and scan agents,
// and the mapping it has to implement is:
//
//   Response::results[i]          -> PopupModel::entries[i]
//   Result::primarySpelling       -> Entry::headword
//   Result::readings              -> Entry::readings
//   Result::alternativeSpellings  -> Entry::alternativeSpellings
//   Result::deconjugationPaths    -> Entry::deconjugationPaths
//   Result::frequencies[0].rank   -> Entry::frequencyRank, and the rendered
//                                    "Name: rank, Name: rank" of every hit past the first
//                                    -> Entry::frequencyText
//   Result::pitchPositions        -> Entry::pitchPositions, one element per reading
//   Result::record                -> Entry::senses, decoded per dict::Record alternative, or
//                                    Entry::richTextGlossary for a Yomitan term whose
//                                    structured content the importer already rendered to Qt
//                                    rich text
//   Result::dictionary.name       -> Entry::dictionaryName
//   Result::kanjiRecord + kanjiExamples + kanjiComponents -> PopupModel::kanji
//   Response highlight span of the first result -> PopupModel::matchedText
//
// The adapter applies no PopupContent option: every switch is applied by the renderer, so a
// toggled option re-renders the model already in hand and never invalidates a lookup cache.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace maru::popup
{

// One sense of one dictionary entry, with the JMdict tag groups kept apart so the render
// options can select them one group at a time.
struct Sense
{
    // Definitions of this sense, in dictionary order. renderHtml() emits the first one alone
    // unless RenderOptions::showAllGlosses is set.
    QStringList glosses;
    // Part-of-speech codes, JMdict <pos>, for example "v5r" and "vt".
    QStringList pos;
    // Miscellaneous codes, JMdict <misc>, for example "uk" and "col".
    QStringList misc;
    // Field-of-application codes, JMdict <field>, for example "med" and "comp".
    QStringList fields;
    // Dialect codes, JMdict <dial>, for example "ksb".
    QStringList dialects;
    // Free-form sense note, JMdict <s_inf>.
    QString info;
    // Referenced headwords, JMdict <xref> and <ant>.
    QStringList crossReferences;
    // Spellings and readings this sense is restricted to, JMdict <stagk> and <stagr>.
    QStringList spellingRestrictions;
};

// One dictionary entry of one lookup result.
struct Entry
{
    // The spelling the entry is filed under, rendered at Theme::headerPt in
    // Theme::highlightWord.
    QString headword;
    // Kana readings of the headword, rendered as "[reading、reading]" in
    // Theme::highlightReading.
    QStringList readings;
    // Spellings other than the headword, rendered in parentheses.
    QStringList alternativeSpellings;
    // One rendered deconjugation path per way the deconjugator reached the headword, already
    // in JL's "～る→た; past" form.
    QStringList deconjugationPaths;
    // Rank of the first frequency dictionary that covers the headword. renderHtml() emits
    // "#rank" for it.
    std::optional<int> frequencyRank;
    // Ranks of every frequency dictionary past the first, already rendered as
    // "Name: rank, Name: rank". Emitted after frequencyRank.
    QString frequencyText;
    // One element per element of readings, in the same order. An empty optional marks a
    // reading no pitch-accent dictionary covers. The value is the mora index of the downstep,
    // 0 for heiban. PopupWindow paints the contour over the reading; renderHtml() emits
    // nothing for it.
    QList<std::optional<quint8>> pitchPositions;
    // Senses in dictionary order. Ignored while richTextGlossary is non-empty.
    QList<Sense> senses;
    // Name of the dictionary the entry came from.
    QString dictionaryName;
    // Orthography codes of the headword, JMdict <ke_inf>, for example "oK" and "rK".
    QStringList orthographyInfo;
    // A glossary the dictionary importer already rendered to the Qt rich-text subset, which
    // is the shape a Yomitan structured-content term arrives in. renderHtml() emits it
    // verbatim, and emits nothing from senses, while it is non-empty.
    QString richTextGlossary;
};

// The KANJIDIC2 card for a single-character lookup, shown after the word entries.
struct KanjiCard
{
    QString character;
    QStringList onReadings;
    QStringList kunReadings;
    QStringList nanoriReadings;
    QStringList meanings;
    QStringList radicalNames;
    // Example words built from JMdict at import, each already in "word [reading] gloss" form.
    // renderHtml() emits at most three of them.
    QStringList examples;
    // Component characters from the CHISE IDS data, each already in "char meaning" form.
    QStringList components;
    int strokeCount = 0;
    // School grade, as KANJIDIC2 reports it. gradeToText() renders the value.
    std::optional<int> grade;
    // Rank in KANJIDIC2's newspaper frequency list, from 1 to 2501.
    std::optional<int> frequency;
};

// One lookup's worth of content.
struct PopupModel
{
    QList<Entry> entries;
    std::optional<KanjiCard> kanji;
    // The raw source characters the lookup matched, carried for renderPlainText() and for the
    // copy-word shortcut.
    QString matchedText;

    [[nodiscard]] bool isEmpty() const
    {
        return entries.isEmpty() && !kanji.has_value();
    }
};

} // namespace maru::popup
