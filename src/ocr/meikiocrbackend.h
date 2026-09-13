// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "ocr/backend.h"

#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>

namespace maru::ocr
{

// The meikiocr pipeline on ONNX Runtime: one D-FINE detection pass over the image, then
// recognition through the horizontal 32x960 or vertical 32x480 model. Ported from
// meikiocr/ocr.py (Apache-2.0, https://github.com/rtr46/meikiocr); ModelStore downloads
// the LGPL-3.0-only weights by rtr46.
// ONNX Runtime reports errors by throwing. The implementation enables exceptions and
// catches at entry points; no Ort type appears in this header.
class MeikiOcrBackend : public Backend
{
public:
    struct Options
    {
        float detectionThreshold = 0.5F;
        float recognitionThreshold = 0.1F;
        // meikipop's value. The meikiocr library defaults to 1.0, which disables the rule.
        float punctuationFactor = 0.2F;
        int intraOpThreads = 6;
        bool allowGpu = false;
        bool useSmallDetector = false;
        // The rewrite rules of ocr/meikicorrections.h, applied to every line after the
        // swapped-pair fix.
        bool applyCorrections = true;
    };

    // The Ocr group of PopSettings.
    [[nodiscard]] static Options optionsFromSettings();

    // Nothing is loaded until initialize() runs.
    MeikiOcrBackend(QString modelDirectory, Options options);
    ~MeikiOcrBackend() override;

    // The name written to Result::backendName and shown in the settings dialog. The static
    // form lets a caller name the backend without constructing one.
    [[nodiscard]] static QString displayName();
    [[nodiscard]] QString name() const override;
    // Builds the three sessions, which takes about 0.3 s, and runs one validation inference on
    // the detection session. Call on the worker thread.
    [[nodiscard]] bool initialize() override;
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] Result recognize(const QImage &image) override;

    // The execution provider the sessions were built with and that passed the validation
    // inference, for example "CPUExecutionProvider".
    [[nodiscard]] QString activeProvider() const;

    // Thresholds take effect on the next recognize(). A change of intraOpThreads, allowGpu or
    // useSmallDetector needs another initialize().
    void setOptions(const Options &options);

    // True where the three required models are present in directory with their expected
    // lengths, which is what resolves OcrEngine::Automatic.
    [[nodiscard]] static bool modelsPresent(const QString &directory);

private:
    struct Sessions;

    QString m_modelDirectory;
    Options m_options;
    std::unique_ptr<Sessions> m_sessions;
};

} // namespace maru::ocr
