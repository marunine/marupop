// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Definitions and frequencies as display strings, for the clipboard, the Anki path and the
// tests. The popup renders from the structured popup::PopupModel instead, which is what keeps
// the per-field render switches cheap.
//
// Ported from JL (Apache-2.0) at commit 85ae02eeb84f378387f48c12e7b468390a9f2007:
// JL.Core/Dicts/JMdict/JmdictRecord.cs BuildFormattedDefinition for the definition
// layout, and JL.Core/Lookup/LookupResultUtils.cs FrequenciesToText for the frequency
// line.
#pragma once

#include "dict/dictionary.h"
#include "dict/records.h"
#include "lookup/lookuptypes.h"

#include <QList>
#include <QString>

#include <span>

namespace maru::lookup
{

// The record's senses as one string, in JL's layout: a prelude of the tags every sense shares,
// then the numbered senses, each with its own tags, its glosses joined with "; ", its note, its
// restrictions and its cross references. Senses are separated by U+FF1B, or by a newline when
// options.newlineBetweenDefinitions is set.
[[nodiscard]] QString formatDefinitions(const dict::JmdictRecord &record, const dict::DictOptions &options);

// The same for every other record kind, which carry no per-sense tag arrays: the definitions
// joined with the same separator.
[[nodiscard]] QString formatDefinitions(const dict::Record &record, const dict::DictOptions &options);

// The definitions of the record behind result, with the render options of the dictionary it
// came from.
[[nodiscard]] QString formatDefinitions(const Result &result);

// JL's frequency line: "#1234" for a single dictionary, and "VN (Nazeka): 1234, JPDB: 5678"
// for several. Empty for an empty list.
[[nodiscard]] QString frequenciesToText(std::span<const FrequencyHit> frequencies);

} // namespace maru::lookup
