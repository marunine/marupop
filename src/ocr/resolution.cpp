// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/resolution.h"

#include "ocr/grouping.h"
#include "ocr/meikipreprocess.h"

#include <QList>

namespace maru::ocr
{

double minimumCharExtent(QSize modelInputSize)
{
    if (modelInputSize.isEmpty()) {
        return 0.0;
    }
    return kMinCharExtentRatio * modelInputSize.height();
}

double medianCharExtent(const Result &result)
{
    QList<double> extents;
    for (const Paragraph &paragraph : result.paragraphs) {
        for (const CharBox &character : paragraph.chars) {
            // The cross axis, which is the axis the detector separates one line from the next
            // along and the axis the recognition crop is scaled by: 32 px of height for a
            // horizontal line and 32 px of width for a vertical one.
            const int extent = paragraph.vertical ? character.box.width() : character.box.height();
            if (extent > 0) {
                extents.append(static_cast<double>(extent));
            }
        }
    }
    return median(std::move(extents));
}

double charExtentAtModelInput(double medianExtent, QSize modelInputSize, QSize sourceSize)
{
    if (medianExtent <= 0.0 || modelInputSize.isEmpty() || sourceSize.isEmpty()) {
        return 0.0;
    }
    // The same fit MeikiOcrBackend::recognize() applies to the image before detection, which is
    // what makes this a prediction rather than an estimate.
    return medianExtent * detectionLetterbox(sourceSize, modelInputSize).scale;
}

double charExtentAtModelInput(const Result &result, QSize sourceSize)
{
    return charExtentAtModelInput(medianCharExtent(result), result.modelInputSize, sourceSize);
}

bool resolvesCharactersAt(double medianExtent, QSize modelInputSize, QSize sourceSize)
{
    const double minimum = minimumCharExtent(modelInputSize);
    if (minimum <= 0.0) {
        return true;
    }
    const double extent = charExtentAtModelInput(medianExtent, modelInputSize, sourceSize);
    if (extent <= 0.0) {
        return true;
    }
    return extent >= minimum;
}

bool resolvesCharactersAt(const Result &result, QSize sourceSize)
{
    return resolvesCharactersAt(medianCharExtent(result), result.modelInputSize, sourceSize);
}

} // namespace maru::ocr
