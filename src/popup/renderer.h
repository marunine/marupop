// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Qt rich-text and plain-text renderings of a PopupModel.
#pragma once

#include <QString>

namespace maru::popup
{

struct PopupModel;
struct RenderOptions;
struct Theme;

// The card content, in the rich-text subset QTextDocument accepts. Every string taken from a
// dictionary passes through QString::toHtmlEscaped() first; Entry::richTextGlossary is the one
// exception, and the dictionary importer that produced it is what has to have escaped it.
//
// The result carries no pitch-accent contour. Each reading run is wrapped in an
// <a name="pitch:<entry>:<reading>:<length>"> anchor instead, which PopupWindow locates with
// readingSpans() to paint the contour over.
[[nodiscard]] QString renderHtml(const PopupModel &model, const RenderOptions &options, const Theme &theme);

// The same content as one plain-text block, for the clipboard. Line breaks separate the
// entries and the sections of the kanji card.
[[nodiscard]] QString renderPlainText(const PopupModel &model, const RenderOptions &options);

// JL's LookupResultUtils.GradeToText: grades 1 to 6 read "<n> (Kyouiku)", grade 8 reads
// "8 (Jouyou)", grades 9 and 10 read "<n> (Jinmeiyou)", and every other value reads
// "Hyougai". Each of the four strings is one i18nc() message, so a translation orders the
// grade number and the list name itself.
[[nodiscard]] QString gradeToText(int grade);

// The anchor name renderHtml() puts on the reading at readingIndex of the entry at
// entryIndex: "pitch:<entryIndex>:<readingIndex>:<length>", where length is the reading's
// length in UTF-16 code units.
//
// The length is part of the name because QTextDocument::setHtml() writes an anchor name onto
// the first character of the run alone, which leaves the run's end unrecorded in the document.
[[nodiscard]] QString pitchAnchorName(int entryIndex, int readingIndex, int length);

// Reads an anchor name back. Returns false, and writes no output, for a name that
// pitchAnchorName() did not produce.
bool parsePitchAnchorName(QStringView name, int *entryIndex, int *readingIndex, int *length);

} // namespace maru::popup
