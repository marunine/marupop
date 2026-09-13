// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The queries lookup/ runs against a dictionary, kept here so the lookup engine holds no knowledge
// of the store, the key filter or the record variant.
//
// The frequency and pitch-accent attachment rules are ports of JL (Apache-2.0):
// JL.Core/Dicts/JMdict/JmdictRecord.cs for the frequency cross-check and
// JL.Core/Lookup/LookupUtils.cs GetPitchPosition for the pitch join.
#pragma once

#include "dict/dictionary.h"
#include "dict/records.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <memory>
#include <optional>
#include <vector>

namespace maru::dict
{

// The records dictionary holds under normalizedKey. Empty when the handle carries no store.
//
// Every entry point here takes a DictionaryHandle, which is what DictionaryManager::snapshot()
// publishes: the handle holds a std::shared_ptr<Store>, so the store stays alive for the whole
// query even when the GUI thread disables or reimports the dictionary while it runs.
[[nodiscard]] std::vector<std::shared_ptr<const Record>> find(const DictionaryHandle &dictionary,
                                                              QStringView normalizedKey);

// The rank freqDict gives the headword written primarySpelling and read as one of readings, or
// nothing when the list does not cover it.
//
// The probe is the two-step cross-check JL performs:
//  1. Probe with the normalized primary spelling. Accept the first record whose spelling equals
//     the primary spelling or appears in readings. The check is what stops the entry for 頭
//     read かしら from taking the rank of あたま.
//  2. Only when step 1 found no records at all, probe each reading. Accept a record whose spelling
//     equals the primary spelling, or whose spelling equals the reading when that reading is
//     katakana, which is the loanword case where the headword is written in kana.
[[nodiscard]] std::optional<int>
frequencyFor(const DictionaryHandle &freqDict, QStringView primarySpelling, const QList<QString> &readings);

// The pitch-accent position of each reading, in the order of readings, or of the primary spelling
// alone when readings is empty. An empty optional marks a reading pitchDict does not cover.
//
// A pitch record carries every position its row declared; the first is returned, which is the one
// JL keeps.
[[nodiscard]] QList<std::optional<quint8>>
pitchPositionsFor(const DictionaryHandle &pitchDict, QStringView primarySpelling, const QList<QString> &readings);

// Every position each reading carries, for a popup that draws more than one accent per reading.
[[nodiscard]] QList<QList<quint8>>
allPitchPositionsFor(const DictionaryHandle &pitchDict, QStringView primarySpelling, const QList<QString> &readings);

// The example words a JMdict store collected for kanji at import, up to three, most common first.
[[nodiscard]] QList<KanjiExample> kanjiExamplesFor(const DictionaryHandle &jmdict, QStringView kanji);

// The first-level components of kanji from a KanjiComponents dictionary.
[[nodiscard]] QList<QString> kanjiComponentsFor(const DictionaryHandle &components, QStringView kanji);

} // namespace maru::dict
