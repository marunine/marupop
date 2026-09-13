// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "ocr/backend.h"

#include <QString>

namespace maru::ocr
{

// Chrome Screen AI OCR through the user's own component install (marupop never ships it — it
// is a proprietary component of roughly 120 MB with no redistribution grant). Loaded with
// dlopen(RTLD_LAZY | RTLD_LOCAL) and version-gated; the SkBitmap-shaped entry point is a
// private C++ layout with no ABI contract. Inputs larger than GetMaxImageDimension() are
// tiled, because an oversize submission returns nothing.
//
// Copied from marusnap's src/pipeline/screenaibackend.{h,cpp} (LGPL-3.0, same author) and
// adapted to ocr::Backend and to the per-character boxes ocr/screenaiproto.h parses.
class ScreenAiBackend : public Backend
{
public:
    ScreenAiBackend() = default;
    ~ScreenAiBackend() override;

    // Where the component lives. Applied by initialize(); the process-global load makes a
    // later change take effect only after a restart.
    void setResourcesDir(const QString &directory);

    // The name written to Result::backendName and shown in the settings dialog. The static
    // form lets a caller name the backend without constructing one.
    [[nodiscard]] static QString displayName();
    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool initialize() override;
    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] Result recognize(const QImage &image) override;

    // Cheap probe used to resolve OcrEngine::Automatic without loading the library.
    [[nodiscard]] static bool isInstalled(const QString &resourcesDir);

private:
    struct Library;

    // Loads and initializes the component at most once per process; see the definition.
    [[nodiscard]] static Library *sharedLibrary(const QString &resourcesDir, int *maxDimension);

    Library *m_library = nullptr;
    QString m_resourcesDir;
    int m_maxDimension = 2048;
};

} // namespace maru::ocr
