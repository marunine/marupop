// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// lookup::Response to popup::PopupModel, the adapter the contract at the top of
// popup/entrymodel.h describes.
//
// It applies no PopupContent option: every switch belongs to the renderer, so toggling one
// re-renders the model already in hand and never invalidates the lookup cache. It also expands
// no tag: the pos, misc, field and dialect codes stay the short JMdict entity names the popup
// shows as pills, and entityDescription() is there for the places that want the long text.
#pragma once

#include "dict/dictionary.h"
#include "lookup/lookuptypes.h"
#include "popup/entrymodel.h"

#include <QString>
#include <QStringView>

namespace maru::lookup
{

// The response as the popup's view model. The word and name results become entries in their
// order; the first kanji result becomes the kanji card, and PopupModel::matchedText is the raw
// source span of the first result, which is the string the copy-word shortcut writes in
// MatchedText mode.
[[nodiscard]] popup::PopupModel toPopupModel(const Response &response);

// The response with the results past the first count removed. The engine sorts the whole list
// before it cuts it at Request::maxResults, so the first count results of a longer answer are
// the answer of a request for count results. The popup and app/LookupWindow list a different
// number of results from one lookup through this. A count below 1 keeps every result.
[[nodiscard]] Response firstResults(const Response &response, int count);

// One result as one entry, for a caller assembling a model of its own.
[[nodiscard]] popup::Entry toPopupEntry(const Result &result);

// The JMdict or JMnedict DTD description of a short entity name ("v5r" -> "Godan verb with `ru'
// ending"), from the entity map the importer wrote into the dictionary's store. Returns code
// itself when the dictionary carries no map or does not declare it.
//
// The map is parsed on the first call per store and kept, so a tooltip over every tag of a
// result costs one JSON parse per session rather than one per tag.
[[nodiscard]] QString entityDescription(const dict::DictionaryHandle &dictionary, QStringView code);

} // namespace maru::lookup
