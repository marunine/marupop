// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "ocr/ocrtypes.h"

#include <QString>

class QImage;

namespace maru::ocr
{

// One text-recognition engine. ocr::OcrService owns every instance and calls it from the one
// worker thread named marupop-ocr: an implementation is required to be correct under serial
// calls alone. Chrome Screen AI's PerformOCR drops results when two calls overlap, which is
// the constraint that fixes the rule for both backends.
//
// recognize() takes a QImage in any format and converts what it needs: MeikiOcrBackend wants
// Format_RGB888, ScreenAiBackend wants Format_RGBA8888_Premultiplied, and the capture path
// produces neither of the two on every compositor.
//
// A backend fills Result::lines. Result::paragraphs is filled by OcrService through
// ocr::groupLines(), so both backends share one grouping implementation.
class Backend
{
public:
    Backend() = default;
    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;
    Backend(Backend &&) = delete;
    Backend &operator=(Backend &&) = delete;
    virtual ~Backend() = default;

    // The name shown in the settings dialog and written to Result::backendName.
    [[nodiscard]] virtual QString name() const = 0;

    // Loads models, libraries and sessions. Takes 0.3 s for MeikiOcrBackend, so it runs on the
    // worker thread. False means the backend is unusable and OcrService selects another one.
    [[nodiscard]] virtual bool initialize() = 0;

    // True once initialize() has succeeded.
    [[nodiscard]] virtual bool isReady() const = 0;

    // Blocking recognition. Result::success is false when the backend failed, with the reason
    // in Result::errorMessage.
    [[nodiscard]] virtual Result recognize(const QImage &image) = 0;

    // True when the backend reports a vertical flag per line. Both shipped backends do;
    // a fake backend in a test can report false.
    [[nodiscard]] virtual bool supportsVerticalText() const
    {
        return true;
    }
};

} // namespace maru::ocr
