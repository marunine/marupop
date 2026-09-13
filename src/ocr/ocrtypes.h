// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The result shape every text-recognition backend produces, shipped by the scaffold so scan/
// and popup/ compile against it before ocr/ is written. ocr/backend.h adds the Backend
// interface, ocr/hittest.h the pointer-to-character mapping over a Result.
//
// Every rectangle here is in source-image pixels: the coordinate space of the QImage handed to
// Backend::recognize(), before any mapping back to logical desktop coordinates.
#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

namespace maru::ocr
{

// One recognized character and the box it occupies. The code point is what the recognition
// model emitted; the confidence is the model's own score, on the 0.0 to 1.0 scale.
struct CharBox
{
    char32_t codePoint = 0;
    QRect box;
    float confidence = 0.0F;
};

// One line of recognized text. text.size() equals chars.size(): every character of text has
// exactly one box, which is what makes a hit test a lookup by index. Both counts are in UTF-16
// code units, so a code point outside the Basic Multilingual Plane would break the invariant;
// the backends drop such a character and log through maru::logOcr.
struct TextLine
{
    QString text;
    QList<CharBox> chars;
    QRect box;
    bool vertical = false;
    float confidence = 0.0F;
};

// Lines grouped into a reading unit, which is the text a lookup runs over: a lookup needs the
// characters that follow the pointer across a line break. lineStarts holds the index into text
// and into chars at which each source line begins, so a caller can recover the line structure.
struct Paragraph
{
    QString text;
    QList<CharBox> chars;
    QRect box;
    bool vertical = false;
    QList<int> lineStarts;
};

// One recognition pass over one image. elapsedMs measures the call to Backend::recognize()
// alone, excluding the grab that produced the image.
struct Result
{
    bool success = false;
    QList<TextLine> lines;
    QList<Paragraph> paragraphs;
    QSize sourceSize;
    // The extent the backend fits the whole image into before it detects, in detector-input
    // pixels. MeikiOcrBackend reports 960x544, or 320x192 under MeikiUseSmallDetector, so a
    // region twice as wide reaches its detector with characters half as tall. An empty size
    // reports a backend detecting at the resolution of the image it was given, which is what
    // ScreenAiBackend does by tiling. ocr::resolvesCharactersAt() is what reads it.
    QSize modelInputSize;
    qint64 elapsedMs = 0;
    QString backendName;
    QString errorMessage;
};

} // namespace maru::ocr
