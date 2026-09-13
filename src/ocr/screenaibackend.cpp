// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/screenaibackend.h"

#include "core/logging.h"
#include "core/paths.h"
#include "ocr/screenaiproto.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLoggingCategory>

#include <KLocalizedString>

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>
#include <memory>

namespace maru::ocr
{

namespace
{

// Public Skia value types; the component reads the SkPixmap it contains, fPixelRef stays null.
// Layouts derive from Chromium's BSD-3 screen_ai_library_wrapper and Skia's public headers,
// never from the GPL/AGPL Python ports that pioneered the technique.
struct SkColorInfo
{
    void *colorSpace = nullptr;
    int32_t colorType = 0; // 4 = kRGBA_8888_SkColorType
    int32_t alphaType = 0; // 1 = kPremul_SkAlphaType
};

struct SkISize
{
    int32_t width = 0;
    int32_t height = 0;
};

struct SkImageInfo
{
    SkColorInfo colorInfo;
    SkISize dimensions;
};

struct SkPixmap
{
    const void *pixels = nullptr;
    size_t rowBytes = 0;
    SkImageInfo info;
};

struct SkBitmap
{
    void *pixelRef = nullptr;
    SkPixmap pixmap;
    uint32_t flags = 0;
};

// Chromium: void GetLibraryVersion(uint32_t& major, uint32_t& minor) — references are plain
// pointers at the C ABI level. Calling it without the out-arguments segfaults.
using GetLibraryVersionFn = void (*)(uint32_t *major, uint32_t *minor);
using SetLoggerFn = void (*)(void (*)(int severity, const char *message));
using GetFileContentSizeFn = uint32_t (*)(const char *relativePath);
using GetFileContentFn = void (*)(const char *relativePath, uint32_t size, void *buffer);
using SetFileContentFunctionsFn = void (*)(GetFileContentSizeFn, GetFileContentFn);
using InitOcrFn = bool (*)();
using SetOcrLightModeFn = void (*)(bool);
using GetMaxImageDimensionFn = uint32_t (*)();
using PerformOcrFn = char *(*)(const SkBitmap *bitmap, uint32_t *outLength);
using FreeCharArrayFn = void (*)(char *);

// The component reads its models through these C callbacks, which carry no user data, so the
// resources directory has to live in a file-static. Only one ScreenAiBackend is ever loaded
// (the OCR worker is single-threaded and owns it), so this stays consistent.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming)
QString g_resourcesDir;

uint32_t fileContentSize(const char *relativePath)
{
    return static_cast<uint32_t>(QFileInfo(g_resourcesDir + QLatin1Char('/') + QString::fromUtf8(relativePath)).size());
}

void fileContent(const char *relativePath, uint32_t size, void *buffer)
{
    QFile file(g_resourcesDir + QLatin1Char('/') + QString::fromUtf8(relativePath));
    if (file.open(QIODevice::ReadOnly)) {
        file.read(static_cast<char *>(buffer), size);
    }
}

void logSink(int severity, const char *message)
{
    if (severity >= 2) {
        qCWarning(logScreenAi) << "screenai:" << message;
    } else {
        qCDebug(logScreenAi) << "screenai:" << message;
    }
}

QString libraryPathFor(const QString &resourcesDir)
{
    return paths::expandPath(resourcesDir) + QStringLiteral("/libchromescreenai.so");
}

// Tiles overlap so a line straddling a seam is fully visible in at least one tile.
constexpr int tileOverlap = 96;

QList<int> tileOrigins(int total, int maxSide)
{
    QList<int> origins;
    if (total <= maxSide) {
        origins.append(0);
        return origins;
    }
    const int step = qMax(1, maxSide - tileOverlap);
    for (int start = 0; start < total; start += step) {
        origins.append(qMin(start, total - maxSide));
        if (start + maxSide >= total) {
            break;
        }
    }
    return origins;
}

} // namespace

struct ScreenAiBackend::Library
{
    void *handle = nullptr;
    GetLibraryVersionFn getVersion = nullptr;
    SetLoggerFn setLogger = nullptr;
    SetFileContentFunctionsFn setFileContentFunctions = nullptr;
    InitOcrFn initOcr = nullptr;
    SetOcrLightModeFn setLightMode = nullptr;
    GetMaxImageDimensionFn getMaxDimension = nullptr;
    PerformOcrFn performOcr = nullptr;
    FreeCharArrayFn freeArray = nullptr;
};

ScreenAiBackend::~ScreenAiBackend() = default;

void ScreenAiBackend::setResourcesDir(const QString &directory)
{
    m_resourcesDir = directory;
}

QString ScreenAiBackend::displayName()
{
    return QStringLiteral("Chrome Screen AI");
}

QString ScreenAiBackend::name() const
{
    return displayName();
}

bool ScreenAiBackend::isInstalled(const QString &resourcesDir)
{
    return QFileInfo::exists(libraryPathFor(resourcesDir));
}

// The component is a process-global singleton: InitOCRUsingCallback CHECK-fails ("!pipeline_")
// when it runs a second time, and dlclose does not reset that state, so the library is loaded
// and initialized exactly once per process and never unloaded. Changing the resources
// directory therefore needs a restart.
ScreenAiBackend::Library *ScreenAiBackend::sharedLibrary(const QString &resourcesDir, int *maxDimension)
{
    static Library *loaded = nullptr;
    static QString loadedDir;
    static bool attempted = false;
    static int loadedMaxDimension = 2048;

    if (attempted) {
        if (loaded != nullptr && loadedDir != paths::expandPath(resourcesDir)) {
            qCWarning(logScreenAi) << "the Screen AI component is already initialized from" << loadedDir
                                   << "- restart to load it from" << resourcesDir;
        }
        *maxDimension = loadedMaxDimension;
        return loaded;
    }
    attempted = true;

    const QString path = libraryPathFor(resourcesDir);
    if (!QFileInfo::exists(path)) {
        qCDebug(logScreenAi) << "the Screen AI component is not installed at" << path;
        return nullptr;
    }

    // RTLD_LAZY is mandatory (the library fails to resolve eagerly); RTLD_LOCAL keeps its
    // allocator and symbols out of our namespace.
    void *handle = dlopen(qPrintable(path), RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr) {
        // dlerror() reads a per-thread buffer and is the only diagnostic dlopen() offers.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        qCWarning(logScreenAi) << "cannot load the Screen AI component:" << dlerror();
        return nullptr;
    }

    auto library = std::make_unique<Library>();
    library->handle = handle;
    const auto resolve = [handle](const char *symbol) {
        void *address = dlsym(handle, symbol);
        if (address == nullptr) {
            qCWarning(logScreenAi) << "the Screen AI component is missing" << symbol;
        }
        return address;
    };
    library->getVersion = reinterpret_cast<GetLibraryVersionFn>(resolve("GetLibraryVersion"));
    library->setLogger = reinterpret_cast<SetLoggerFn>(dlsym(handle, "SetLogger"));
    library->setFileContentFunctions = reinterpret_cast<SetFileContentFunctionsFn>(resolve("SetFileContentFunctions"));
    library->initOcr = reinterpret_cast<InitOcrFn>(resolve("InitOCRUsingCallback"));
    library->setLightMode = reinterpret_cast<SetOcrLightModeFn>(dlsym(handle, "SetOCRLightMode"));
    library->getMaxDimension = reinterpret_cast<GetMaxImageDimensionFn>(resolve("GetMaxImageDimension"));
    library->performOcr = reinterpret_cast<PerformOcrFn>(resolve("PerformOCR"));
    library->freeArray = reinterpret_cast<FreeCharArrayFn>(resolve("FreeLibraryAllocatedCharArray"));

    if (library->getVersion == nullptr || library->setFileContentFunctions == nullptr || library->initOcr == nullptr ||
        library->getMaxDimension == nullptr || library->performOcr == nullptr || library->freeArray == nullptr) {
        return nullptr;
    }

    uint32_t major = 0;
    uint32_t minor = 0;
    library->getVersion(&major, &minor);
    if (major == 0) {
        qCWarning(logScreenAi) << "the Screen AI component reported version 0; falling back";
        return nullptr;
    }

    g_resourcesDir = paths::expandPath(resourcesDir);
    if (library->setLogger != nullptr) {
        library->setLogger(logSink);
    }
    library->setFileContentFunctions(fileContentSize, fileContent);
    if (!library->initOcr()) {
        qCWarning(logScreenAi) << "InitOCRUsingCallback failed; falling back";
        return nullptr;
    }
    if (library->setLightMode != nullptr) {
        library->setLightMode(false);
    }

    loadedMaxDimension = static_cast<int>(library->getMaxDimension());
    if (loadedMaxDimension <= 0) {
        loadedMaxDimension = 2048;
    }
    loadedDir = g_resourcesDir;
    loaded = library.release();
    *maxDimension = loadedMaxDimension;
    qCDebug(logScreenAi) << "Screen AI" << major << minor << "ready, max dimension" << loadedMaxDimension;
    return loaded;
}

bool ScreenAiBackend::initialize()
{
    if (m_library == nullptr) {
        m_library = sharedLibrary(m_resourcesDir, &m_maxDimension);
    }
    return m_library != nullptr;
}

bool ScreenAiBackend::isReady() const
{
    return m_library != nullptr;
}

Result ScreenAiBackend::recognize(const QImage &image)
{
    QElapsedTimer timer;
    timer.start();
    Result result;
    result.backendName = name();
    result.sourceSize = image.size();
    if (m_library == nullptr && !initialize()) {
        result.errorMessage = i18nc("@info", "Could not load the Chrome Screen AI component.");
        return result;
    }
    if (image.isNull()) {
        result.errorMessage = i18nc("@info", "The captured image is empty.");
        return result;
    }

    // Screen AI wants native resolution, so an oversize input is tiled rather than scaled;
    // 2048 is the contract even where the installed component tolerates more.
    const QImage source = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QList<TextLine> lines;
    const QList<int> columns = tileOrigins(source.width(), m_maxDimension);
    const QList<int> rows = tileOrigins(source.height(), m_maxDimension);
    for (const int y : rows) {
        for (const int x : columns) {
            const QRect tileRect{
                x, y, qMin(m_maxDimension, source.width() - x), qMin(m_maxDimension, source.height() - y)};
            const QImage tile = tileRect.size() == source.size() ? source : source.copy(tileRect);

            SkBitmap bitmap;
            bitmap.pixmap.pixels = tile.constBits();
            bitmap.pixmap.rowBytes = static_cast<size_t>(tile.bytesPerLine());
            bitmap.pixmap.info.colorInfo.colorType = 4; // kRGBA_8888
            bitmap.pixmap.info.colorInfo.alphaType = 1; // kPremul
            bitmap.pixmap.info.dimensions.width = tile.width();
            bitmap.pixmap.info.dimensions.height = tile.height();

            uint32_t length = 0;
            char *annotation = m_library->performOcr(&bitmap, &length);
            if (annotation == nullptr) {
                continue;
            }
            const QByteArray proto(annotation, static_cast<qsizetype>(length));
            m_library->freeArray(annotation);

            const std::optional<QList<TextLine>> tileLines = parseVisualAnnotation(proto);
            if (!tileLines) {
                qCWarning(logScreenAi) << "could not parse the Screen AI annotation";
                continue;
            }
            for (TextLine line : *tileLines) {
                line.box.translate(tileRect.topLeft());
                for (CharBox &character : line.chars) {
                    character.box.translate(tileRect.topLeft());
                }
                // Overlapping tiles report the lines in their shared band twice.
                const bool duplicate = std::ranges::any_of(lines, [&line](const TextLine &existing) {
                    return existing.text == line.text && existing.box.intersects(line.box);
                });
                if (!duplicate) {
                    lines.append(line);
                }
            }
        }
    }

    // Top to bottom, then left to right, so tiled results read in document order.
    std::ranges::sort(lines, [](const TextLine &left, const TextLine &right) {
        if (left.box.top() != right.box.top()) {
            return left.box.top() < right.box.top();
        }
        return left.box.left() < right.box.left();
    });

    result.lines = lines;
    result.success = true;
    result.elapsedMs = timer.elapsed();
    return result;
}

} // namespace maru::ocr
