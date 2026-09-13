// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "ocr/ocrtypes.h"

#include <QByteArray>
#include <QList>

#include <optional>

namespace maru::ocr
{

// Minimal protobuf wire-format reader for chrome_screen_ai.VisualAnnotation, covering the
// fields marupop consumes: LineBox (words, bounding_box, utf8_string, language, direction,
// confidence), WordBox (symbols, bounding_box, utf8_string, language, direction, confidence)
// and SymbolBox (bounding_box, utf8_string, confidence). Field numbers derive from Chromium's
// BSD-3 services/screen_ai/proto/chrome_screen_ai.proto; an unknown field is skipped, so a
// component update that adds fields stays compatible.
//
// The reader was taken from marusnap's src/pipeline/screenaiproto.cpp (LGPL-3.0, same author),
// which parses lines alone. Per-character boxes are what marupop needs, so WordBox and
// SymbolBox are parsed here and one CharBox is produced per code point.
//
// Returns nullopt on malformed input.
[[nodiscard]] std::optional<QList<TextLine>> parseVisualAnnotation(const QByteArray &data);

// chrome_screen_ai.Direction values. Only DIRECTION_TOP_TO_BOTTOM sets TextLine::vertical.
inline constexpr int kDirectionLeftToRight = 1;
inline constexpr int kDirectionRightToLeft = 2;
inline constexpr int kDirectionTopToBottom = 3;

} // namespace maru::ocr
