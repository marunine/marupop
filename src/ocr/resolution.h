// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The extent of a recognized character at a detector's fixed input resolution.
// Growing a capture region shrinks its characters at the detector input, which can change
// the recognition and hit-test result even when the pointer is stationary.
// ScreenAiBackend tiles at source resolution and reports an empty modelInputSize, so this
// fixed-input resolution gate does not apply to it.
#pragma once

#include "ocr/ocrtypes.h"

#include <QSize>

namespace maru::ocr
{

// Conservative character-height fraction used to gate crop growth. At 0.044, the threshold
// is 23.936 pixels for a 960x544 detector and 8.448 for a 320x192 detector. This heuristic
// is not an accuracy guarantee; detectorresolution_test can evaluate it with local models.
inline constexpr double kMinCharExtentRatio = 0.044;

// The smallest character extent a detector of modelInputSize separates one character from the
// next at, in detector-input pixels: 23.9 for 960x544 and 8.4 for 320x192. An empty
// modelInputSize gives 0.0, which reports a backend detecting at the resolution of the image it
// was given.
[[nodiscard]] double minimumCharExtent(QSize modelInputSize);

// The median cross-axis extent of the character boxes of every paragraph of result, in
// source-image pixels: the height of a box for a horizontal paragraph and its width for a
// vertical one. A result holding no paragraph gives 0.0.
//
// The value is a property of result alone, and it costs one allocation and one sort over every
// character box. scan::CachedScan carries it in medianCharExtent, computed once per recognition
// pass, and the two scalar functions below are what read it.
[[nodiscard]] double medianCharExtent(const Result &result);

// The extent a character of medianExtent source-image pixels reaches modelInputSize at during a
// pass over an image of sourceSize, in detector-input pixels. A medianExtent of 0.0 or an empty
// modelInputSize gives 0.0.
[[nodiscard]] double charExtentAtModelInput(double medianExtent, QSize modelInputSize, QSize sourceSize);

// The same quantity for the characters result already recognized, in detector-input pixels.
[[nodiscard]] double charExtentAtModelInput(const Result &result, QSize sourceSize);

// True where a pass over an image of sourceSize reaches the detector with a character of
// medianExtent source-image pixels at or above minimumCharExtent(modelInputSize). A 0.0 from
// either quantity reports an unmeasured pair and gives true.
[[nodiscard]] bool resolvesCharactersAt(double medianExtent, QSize modelInputSize, QSize sourceSize);

// The same test for the characters result already recognized.
[[nodiscard]] bool resolvesCharactersAt(const Result &result, QSize sourceSize);

} // namespace maru::ocr
