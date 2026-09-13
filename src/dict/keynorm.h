// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The single normalization applied to every search key an importer writes and to every candidate
// key the lookup engine probes with. Keys are stored byte-comparable, so the store needs no
// collation and the key-filter sidecar can hash the UTF-8 bytes directly.
//
// The implementation forwards to maru::jp::normalizeText(), the port of JL's
// JapaneseUtils.NormalizeText (JL.Core/Japanese/JapaneseUtils.cs, Apache-2.0). The
// indirection keeps every key-producing call site in dict/ on one name, so a change to the
// normalization is a change to one function.
#pragma once

#include <QString>
#include <QStringView>

namespace maru::dict
{

// NFKC, ASCII upper case, katakana folded to hiragana, variation selectors dropped, iteration
// marks expanded, repeated small tsu collapsed, fuseji characters mapped to one sentinel.
[[nodiscard]] QString normalizeKey(QStringView text);

} // namespace maru::dict
