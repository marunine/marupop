// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The content switches of the popup, one field per entry of the PopupContent group of
// marupoprc.
#pragma once

namespace maru::popup
{

// Every switch is applied by renderHtml() and renderPlainText() rather than by the lookup, so
// toggling one re-renders the PopupModel already in hand. meikipop gates its kanji card in the
// lookup instead, which forces a cache flush on every toggle
// (meikipop/src/meikipop/dictionary/lookup.py line 103).
struct RenderOptions
{
    // false emits the first gloss of each sense alone, and no sense number.
    bool showAllGlosses = false;
    // Emits the deconjugation paths of an entry, in parentheses at 80 percent of the
    // foreground over the background.
    bool showDeconjugation = true;
    // Emits the JMdict <pos> codes of each sense, in italics.
    bool showPartOfSpeech = false;
    // Emits the JMdict <misc>, <field> and <dial> codes of each sense as a "[uk, col]" pill
    // list, plus the sense note and the cross references.
    bool showTags = false;
    // Emits the frequency rank of an entry, as "#rank" at 60 percent of the foreground over
    // the background.
    bool showFrequency = true;
    // Emits the kanji card of PopupModel::kanji.
    bool showKanji = true;
    // Emits at most three example words in the kanji card.
    bool showKanjiExamples = true;
    // Emits the component characters in the kanji card.
    bool showKanjiComponents = true;
    // Joins the senses of one entry with "; " on one line. false emits one sense per line.
    bool compactMode = true;
    // Emits the spellings of an entry other than the headword, in parentheses.
    bool showAlternativeSpellings = true;
    // Lets PopupWindow paint the pitch-accent contour over each reading. renderHtml() emits no
    // contour under either value; what the switch changes in the HTML is the pitch anchor that
    // marks each reading span.
    bool showPitchAccent = true;
    // Emits the name of the dictionary each entry came from.
    bool showDictionaryName = false;
    // Emits the JMdict <ke_inf> codes of the headword and the spelling restrictions of each
    // sense.
    bool showOrthographyInfo = true;
    // Emits the stroke count and the school grade in the kanji card.
    bool showStrokeCountAndGrade = true;
};

// The options the PopupContent group describes.
[[nodiscard]] RenderOptions renderOptionsFromSettings();

} // namespace maru::popup
