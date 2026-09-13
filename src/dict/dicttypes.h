// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The dictionary-type tag, shipped by the scaffold so lookup/ and popup/ compile against it
// before dict/ is written. dict/records.h adds the record structs and the Record variant, and
// dict/store.h the store that keys on this enum.
#pragma once

namespace maru::dict
{

// What a store holds, which selects the importer that filled it, the record shape
// decodeRecord() reads, and the lookup category the entry answers. The value is written into
// each store's metadata table, so the order is part of the on-disk format: append only.
enum class DictType
{
    JMdict,
    JMnedict,
    Kanjidic,
    YomitanWord,
    YomitanKanji,
    // A Yomitan kanji bank whose entries carry the term schema rather than the kanji schema,
    // which several published dictionaries ship.
    YomitanKanjiWordSchema,
    YomitanName,
    YomitanPitchAccent,
    YomitanOther,
    YomitanFrequency,
    YomitanKanjiFrequency,
    CustomWord,
    CustomName,
    // The cjkvi-ids kanji composition list, whose records carry the first-level components of one
    // kanji. Appended by dict/, which owns this header; the values above it keep their numbers, so
    // a store written before it was added still reads back as the type it was written with.
    KanjiComponents,
};

} // namespace maru::dict
