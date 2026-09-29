// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/winframesource.h"

#include "core/logging.h"
#include "win32/window.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QtGui/qscreen_platform.h>

#include <KLocalizedString>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <functional>
#include <memory>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace maru::capture
{

namespace
{

// Idle time after which a duplication and its copy of the output are released. A held
// duplication costs 4 bytes of video memory per output pixel, 33.2 MB at 3840x2160, and a GPU
// copy on every present of the output.
constexpr int kIdleReleaseMs = 10000;
// DXGI_ERROR_ACCESS_LOST arrives for a mode change, the secure desktop and a lock. Each
// recreation attempt while the condition lasts costs a failed DuplicateOutput.
constexpr int kRetryAfterFailureMs = 1000;
// A display configuration change is the only event that ends a Failure::Unsupported condition.
// Each attempt enumerates every adapter and output.
constexpr int kRetryAfterUnsupportedMs = 30000;

D3D11_TEXTURE2D_DESC bgraTexture(UINT width, UINT height, D3D11_USAGE usage, UINT cpuAccess)
{
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = 1;
    texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = usage;
    texture.CPUAccessFlags = cpuAccess;
    return texture;
}

// Format_RGB32 requires 0xff alpha. DXGI Desktop Duplication and GDI BitBlt leave the alpha byte
// undefined.
QImage opaqueImage(const uchar *rows, qsizetype stride, int width, int height)
{
    QImage image{width, height, QImage::Format_RGB32};
    for (int y = 0; y < height; ++y) {
        const auto *source = reinterpret_cast<const quint32 *>(rows + y * stride);
        auto *target = reinterpret_cast<quint32 *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            target[x] = source[x] | 0xff000000U;
        }
    }
    return image;
}

class DibSection
{
public:
    DibSection(HDC screen, int width, int height)
        : m_dc(CreateCompatibleDC(screen))
        , m_width(width)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        m_bitmap = CreateDIBSection(m_dc, &info, DIB_RGB_COLORS, &m_bits, nullptr, 0);
        if (m_bitmap != nullptr) {
            m_previous = SelectObject(m_dc, m_bitmap);
        }
    }

    ~DibSection()
    {
        if (m_previous != nullptr) {
            SelectObject(m_dc, m_previous);
        }
        if (m_bitmap != nullptr) {
            DeleteObject(m_bitmap);
        }
        DeleteDC(m_dc);
    }

    DibSection(const DibSection &) = delete;
    DibSection &operator=(const DibSection &) = delete;

    [[nodiscard]] bool isValid() const
    {
        return m_bitmap != nullptr && m_bits != nullptr;
    }

    [[nodiscard]] HDC dc() const
    {
        return m_dc;
    }

    [[nodiscard]] const void *bits() const
    {
        return m_bits;
    }

    [[nodiscard]] int stride() const
    {
        return m_width * 4;
    }

private:
    HDC m_dc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_previous = nullptr;
    void *m_bits = nullptr;
    int m_width = 0;
};

// Passes the screen pixels of rect, in virtual-desktop device pixels, to consume as a BGRA DIB.
// Returns false where BitBlt fails, as on the secure desktop and in a locked session.
bool bitBlt(const RECT &rect, const std::function<void(const DibSection &)> &consume)
{
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }
    bool ok = false;
    {
        DibSection dib{screen, width, height};
        // DWM composes every window into the screen DC.
        // CAPTUREBLT makes the pointer flicker on a session without desktop composition.
        if (dib.isValid() && BitBlt(dib.dc(), 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY) != FALSE) {
            GdiFlush();
            consume(dib);
            ok = true;
        }
    }
    ReleaseDC(nullptr, screen);
    return ok;
}

} // namespace

// Owner of every DXGI and D3D object. It lives on WinFrameSource::m_thread, and every member
// function runs on WinFrameSource::m_thread.
class WinCaptureWorker : public QObject
{
public:
    struct Result
    {
        QImage image;
        WinFrameSource::Method method = WinFrameSource::Method::BitBlt;
        QString error;
    };

    WinCaptureWorker()
    {
        m_clock.start();
    }

    Result capture(HMONITOR monitor, const RECT &rect, bool duplicationEnabled)
    {
        armIdleRelease();
        if (duplicationEnabled) {
            if (Output *output = outputFor(monitor)) {
                Result result;
                if (copyFromDuplication(*output, rect, result.image)) {
                    result.method = WinFrameSource::Method::DesktopDuplication;
                    return result;
                }
            }
        }
        return captureWithBitBlt(rect);
    }

    // Also stops m_idle, which the WinFrameSource destructor deletes from the GUI thread after
    // m_thread has finished.
    void release()
    {
        if (m_idle != nullptr) {
            m_idle->stop();
        }
        releaseOutputs();
        m_adapters.clear();
    }

private:
    enum class Failure
    {
        Transient,
        Unsupported,
    };

    struct Adapter
    {
        LUID luid{};
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
    };

    struct Output
    {
        HMONITOR monitor = nullptr;
        RECT desktop{};
        Adapter *adapter = nullptr;
        ComPtr<IDXGIOutputDuplication> duplication;
        // The newest desktop image of the output.
        ComPtr<ID3D11Texture2D> last;
        ComPtr<ID3D11Texture2D> staging;
        int stagingWidth = 0;
        int stagingHeight = 0;
    };

    void releaseOutputs()
    {
        if (!m_outputs.empty()) {
            qCDebug(logCapture) << "releasing" << m_outputs.size() << "desktop duplications";
        }
        m_outputs.clear();
    }

    void deferRetry(HMONITOR monitor, Failure failure)
    {
        const int delayMs = failure == Failure::Unsupported ? kRetryAfterUnsupportedMs : kRetryAfterFailureMs;
        m_retryAfter.insert(monitor, m_clock.elapsed() + delayMs);
    }

    Result captureWithBitBlt(const RECT &rect)
    {
        Result result;
        result.method = WinFrameSource::Method::BitBlt;
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        const bool ok = bitBlt(rect, [&](const DibSection &dib) {
            result.image = opaqueImage(static_cast<const uchar *>(dib.bits()), dib.stride(), width, height);
        });
        if (!ok) {
            const DWORD error = GetLastError();
            qCWarning(logCapture) << "BitBlt capture failed with error" << error;
            result.error = i18n("Screen capture failed: the screen could not be read.");
        }
        return result;
    }

    void armIdleRelease()
    {
        if (m_idle == nullptr) {
            // Created on m_thread at the first grab. The WinCaptureWorker constructor runs on the
            // GUI thread, and a QTimer fires on the thread that created it.
            m_idle = new QTimer(this);
            m_idle->setSingleShot(true);
            m_idle->setInterval(kIdleReleaseMs);
            QObject::connect(m_idle, &QTimer::timeout, this, [this] {
                releaseOutputs();
            });
        }
        m_idle->start();
    }

    Adapter *adapterFor(IDXGIAdapter1 *dxgiAdapter)
    {
        DXGI_ADAPTER_DESC1 description{};
        dxgiAdapter->GetDesc1(&description);
        for (const std::unique_ptr<Adapter> &adapter : m_adapters) {
            if (adapter->luid.LowPart == description.AdapterLuid.LowPart &&
                adapter->luid.HighPart == description.AdapterLuid.HighPart) {
                return adapter.get();
            }
        }
        auto adapter = std::make_unique<Adapter>();
        adapter->luid = description.AdapterLuid;
        const HRESULT created = D3D11CreateDevice(dxgiAdapter,
                                                  D3D_DRIVER_TYPE_UNKNOWN,
                                                  nullptr,
                                                  D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                  nullptr,
                                                  0,
                                                  D3D11_SDK_VERSION,
                                                  &adapter->device,
                                                  nullptr,
                                                  &adapter->context);
        if (FAILED(created)) {
            qCWarning(logCapture) << "D3D11CreateDevice failed with" << Qt::hex << static_cast<quint32>(created);
            return nullptr;
        }
        m_adapters.push_back(std::move(adapter));
        return m_adapters.back().get();
    }

    // The Output of monitor, created on first use, or nullptr where monitor has no duplication.
    Output *outputFor(HMONITOR monitor)
    {
        for (const std::unique_ptr<Output> &output : m_outputs) {
            if (output->monitor == monitor) {
                return output->duplication != nullptr ? output.get() : nullptr;
            }
        }
        const auto retry = m_retryAfter.constFind(monitor);
        if (retry != m_retryAfter.constEnd() && m_clock.elapsed() < *retry) {
            return nullptr;
        }
        Failure failure = Failure::Transient;
        std::unique_ptr<Output> output = duplicate(monitor, failure);
        if (!output) {
            deferRetry(monitor, failure);
            return nullptr;
        }
        m_retryAfter.remove(monitor);
        m_outputs.push_back(std::move(output));
        return m_outputs.back().get();
    }

    // The duplication of monitor, or nullptr with failure set to the failure class.
    std::unique_ptr<Output> duplicate(HMONITOR monitor, Failure &failure)
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return nullptr;
        }
        ComPtr<IDXGIAdapter1> dxgiAdapter;
        for (UINT a = 0; factory->EnumAdapters1(a, &dxgiAdapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            ComPtr<IDXGIOutput> dxgiOutput;
            for (UINT o = 0; dxgiAdapter->EnumOutputs(o, &dxgiOutput) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC description{};
                dxgiOutput->GetDesc(&description);
                if (description.Monitor != monitor) {
                    continue;
                }
                Adapter *adapter = adapterFor(dxgiAdapter.Get());
                if (adapter == nullptr) {
                    return nullptr;
                }
                auto output = std::make_unique<Output>();
                output->monitor = monitor;
                output->desktop = description.DesktopCoordinates;
                output->adapter = adapter;
                if (!startDuplication(*output, dxgiOutput.Get(), failure)) {
                    return nullptr;
                }
                return output;
            }
        }
        // monitor is absent from every adapter's DXGI outputs, as on a hybrid-graphics laptop
        // whose panel belongs to the integrated GPU.
        qCDebug(logCapture) << "no DXGI output drives monitor" << monitor << "; grabs on the monitor use BitBlt";
        failure = Failure::Unsupported;
        return nullptr;
    }

    bool startDuplication(Output &output, IDXGIOutput *dxgiOutput, Failure &failure)
    {
        ID3D11Device *device = output.adapter->device.Get();
        HRESULT result = E_NOINTERFACE;
        // DuplicateOutput1 with DXGI_FORMAT_B8G8R8A8_UNORM as the only accepted format makes DXGI
        // convert an HDR desktop to SDR. DuplicateOutput fails for an output in an FP16 mode.
        ComPtr<IDXGIOutput5> output5;
        if (SUCCEEDED(dxgiOutput->QueryInterface(IID_PPV_ARGS(&output5)))) {
            const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
            result = output5->DuplicateOutput1(device, 0, 1, formats, &output.duplication);
        }
        if (FAILED(result)) {
            ComPtr<IDXGIOutput1> output1;
            if (SUCCEEDED(dxgiOutput->QueryInterface(IID_PPV_ARGS(&output1)))) {
                result = output1->DuplicateOutput(device, &output.duplication);
            }
        }
        if (FAILED(result)) {
            // DuplicateOutput results by condition: E_ACCESSDENIED on the secure desktop,
            // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE where other applications hold every duplication
            // of the output, DXGI_ERROR_UNSUPPORTED in a session such as Remote Desktop.
            qCDebug(logCapture) << "DuplicateOutput failed with" << Qt::hex << static_cast<quint32>(result);
            output.duplication.Reset();
            return false;
        }
        DXGI_OUTDUPL_DESC description{};
        output.duplication->GetDesc(&description);
        if (description.ModeDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
            (description.Rotation != DXGI_MODE_ROTATION_IDENTITY &&
             description.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED)) {
            qCDebug(logCapture) << "the duplication of monitor" << output.monitor << "has format"
                                << description.ModeDesc.Format << "and rotation" << description.Rotation
                                << "; grabs on the monitor use BitBlt";
            output.duplication.Reset();
            failure = Failure::Unsupported;
            return false;
        }
        // The mode size and DXGI_OUTPUT_DESC::DesktopCoordinates differ while a mode change is
        // under way. The BitBlt seed of Output::last copies the desktop rectangle into a texture
        // of the mode size, so both sizes have to match.
        const int width = static_cast<int>(description.ModeDesc.Width);
        const int height = static_cast<int>(description.ModeDesc.Height);
        if (width != output.desktop.right - output.desktop.left ||
            height != output.desktop.bottom - output.desktop.top) {
            qCDebug(logCapture) << "the duplication of monitor" << output.monitor << "is" << width << "x" << height
                                << "against a desktop rectangle of" << output.desktop.right - output.desktop.left << "x"
                                << output.desktop.bottom - output.desktop.top;
            output.duplication.Reset();
            return false;
        }
        const D3D11_TEXTURE2D_DESC texture =
            bgraTexture(description.ModeDesc.Width, description.ModeDesc.Height, D3D11_USAGE_DEFAULT, 0);
        if (FAILED(device->CreateTexture2D(&texture, nullptr, &output.last))) {
            output.duplication.Reset();
            return false;
        }
        // The seed is taken after DuplicateOutput, so a present between DuplicateOutput and the
        // BitBlt reaches the first drain.
        const bool seeded = bitBlt(output.desktop, [&](const DibSection &dib) {
            output.adapter->context->UpdateSubresource(
                output.last.Get(), 0, nullptr, dib.bits(), static_cast<UINT>(dib.stride()), 0);
        });
        if (!seeded) {
            output.duplication.Reset();
            return false;
        }
        qCDebug(logCapture) << "duplicating monitor" << output.monitor << "at" << width << "x" << height;
        return true;
    }

    // Returns false where AcquireNextFrame fails, after destroying output.
    bool drain(Output &output)
    {
        for (;;) {
            DXGI_OUTDUPL_FRAME_INFO info{};
            ComPtr<IDXGIResource> resource;
            const HRESULT result = output.duplication->AcquireNextFrame(0, &info, &resource);
            if (result == DXGI_ERROR_WAIT_TIMEOUT) {
                return true;
            }
            if (FAILED(result)) {
                qCDebug(logCapture) << "AcquireNextFrame failed with" << Qt::hex << static_cast<quint32>(result);
                output.duplication.Reset();
                deferRetry(output.monitor, Failure::Transient);
                std::erase_if(m_outputs, [&](const std::unique_ptr<Output> &entry) {
                    return entry.get() == &output;
                });
                return false;
            }
            // A zero LastPresentTime is a frame carrying a pointer update alone, whose image is
            // undefined.
            if (info.LastPresentTime.QuadPart != 0) {
                ComPtr<ID3D11Texture2D> frame;
                if (SUCCEEDED(resource.As(&frame))) {
                    output.adapter->context->CopyResource(output.last.Get(), frame.Get());
                }
            }
            output.duplication->ReleaseFrame();
        }
    }

    bool copyFromDuplication(Output &output, const RECT &rect, QImage &image)
    {
        if (!drain(output)) {
            return false;
        }
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (rect.left < output.desktop.left || rect.top < output.desktop.top || rect.right > output.desktop.right ||
            rect.bottom > output.desktop.bottom || width <= 0 || height <= 0) {
            return false;
        }
        ID3D11Device *device = output.adapter->device.Get();
        ID3D11DeviceContext *context = output.adapter->context.Get();
        if (width > output.stagingWidth || height > output.stagingHeight) {
            const int stagingWidth = std::max(width, output.stagingWidth);
            const int stagingHeight = std::max(height, output.stagingHeight);
            const D3D11_TEXTURE2D_DESC texture = bgraTexture(static_cast<UINT>(stagingWidth),
                                                             static_cast<UINT>(stagingHeight),
                                                             D3D11_USAGE_STAGING,
                                                             D3D11_CPU_ACCESS_READ);
            output.staging.Reset();
            output.stagingWidth = 0;
            output.stagingHeight = 0;
            if (FAILED(device->CreateTexture2D(&texture, nullptr, &output.staging))) {
                return false;
            }
            output.stagingWidth = stagingWidth;
            output.stagingHeight = stagingHeight;
        }
        const D3D11_BOX box{.left = static_cast<UINT>(rect.left - output.desktop.left),
                            .top = static_cast<UINT>(rect.top - output.desktop.top),
                            .front = 0,
                            .right = static_cast<UINT>(rect.right - output.desktop.left),
                            .bottom = static_cast<UINT>(rect.bottom - output.desktop.top),
                            .back = 1};
        context->CopySubresourceRegion(output.staging.Get(), 0, 0, 0, 0, output.last.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(output.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return false;
        }
        image = opaqueImage(static_cast<const uchar *>(mapped.pData), mapped.RowPitch, width, height);
        context->Unmap(output.staging.Get(), 0);
        return true;
    }

    std::vector<std::unique_ptr<Adapter>> m_adapters;
    std::vector<std::unique_ptr<Output>> m_outputs;
    // Per monitor, the m_clock reading before which no new duplication is attempted.
    QHash<HMONITOR, qint64> m_retryAfter;
    QElapsedTimer m_clock;
    QTimer *m_idle = nullptr;
};

namespace
{

HMONITOR monitorOf(const QScreen *screen)
{
    if (screen == nullptr) {
        return nullptr;
    }
    auto *native = const_cast<QScreen *>(screen)->nativeInterface<QNativeInterface::QWindowsScreen>();
    return native != nullptr ? native->handle() : nullptr;
}

} // namespace

WinFrameSource::WinFrameSource(QObject *parent)
    : FrameSource(parent)
    , m_thread(new QThread)
    , m_worker(new WinCaptureWorker)
{
    m_thread->setObjectName(QStringLiteral("marupop-capture"));
    m_worker->moveToThread(m_thread);
    m_thread->start();
}

WinFrameSource::~WinFrameSource()
{
    QMetaObject::invokeMethod(m_worker, &WinCaptureWorker::release, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
    delete m_thread;
}

QRect WinFrameSource::nativeRect(const QRect &logical, const QRect &screenLogical, const QRect &screenNative)
{
    if (screenLogical.isEmpty() || screenNative.isEmpty()) {
        return {};
    }
    const qreal scaleX = static_cast<qreal>(screenNative.width()) / screenLogical.width();
    const qreal scaleY = static_cast<qreal>(screenNative.height()) / screenLogical.height();
    const auto mapX = [&](int x) {
        return screenNative.x() + static_cast<int>(std::lround((x - screenLogical.x()) * scaleX));
    };
    const auto mapY = [&](int y) {
        return screenNative.y() + static_cast<int>(std::lround((y - screenLogical.y()) * scaleY));
    };
    const int left = mapX(logical.x());
    const int top = mapY(logical.y());
    const int right = mapX(logical.x() + logical.width());
    const int bottom = mapY(logical.y() + logical.height());
    return QRect{QPoint{left, top}, QPoint{right - 1, bottom - 1}}.intersected(screenNative);
}

QRect WinFrameSource::nativeGeometry(const QScreen *screen)
{
    HMONITOR monitor = monitorOf(screen);
    if (monitor == nullptr) {
        return {};
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) {
        return {};
    }
    const RECT &rect = info.rcMonitor;
    return QRect{QPoint{rect.left, rect.top}, QPoint{rect.right - 1, rect.bottom - 1}};
}

QRect WinFrameSource::quantize(const QRect &logical) const
{
    const QScreen *screen = QGuiApplication::screenAt(logical.center());
    if (screen == nullptr) {
        return logical;
    }
    return logical.intersected(screen->geometry());
}

bool WinFrameSource::capturesOwnWindows() const
{
    static const bool captures = !win32::captureExclusionSupported();
    return captures;
}

void WinFrameSource::release()
{
    QMetaObject::invokeMethod(m_worker, &WinCaptureWorker::release, Qt::QueuedConnection);
}

void WinFrameSource::setDesktopDuplicationEnabled(bool enabled)
{
    m_duplicationEnabled = enabled;
}

WinFrameSource::Method WinFrameSource::lastMethod() const
{
    return m_lastMethod;
}

void WinFrameSource::grab(const QRect &logical)
{
    if (const std::optional<QRect> rect = beginRequest(logical)) {
        issue(*rect);
    }
}

void WinFrameSource::issue(const QRect &logical)
{
    const QRect target = quantize(logical);
    const QScreen *screen = QGuiApplication::screenAt(target.center());
    const QRect native = screen != nullptr ? nativeRect(target, screen->geometry(), nativeGeometry(screen)) : QRect{};
    if (native.isEmpty()) {
        // Event-loop delivery keeps latest-wins ordering for a caller that grabs again from its
        // failed() handler.
        QTimer::singleShot(0, this, [this, logical] {
            deliver(
                logical,
                {},
                {},
                1.0,
                Method::BitBlt,
                i18n("Screen capture failed: no screen contains %1,%2.", logical.center().x(), logical.center().y()));
        });
        return;
    }
    const qreal scale = static_cast<qreal>(native.width()) / std::max(1, target.width());
    HMONITOR monitor = monitorOf(screen);
    const RECT rect{
        .left = native.left(), .top = native.top(), .right = native.right() + 1, .bottom = native.bottom() + 1};
    const bool duplicationEnabled = m_duplicationEnabled;
    // The popup rectangle when the grab is issued, taken as the popup position in the image.
    const QRect occluded = capturesOwnWindows() ? currentOcclusion().intersected(target) : QRect{};
    WinCaptureWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker,
        [this, worker, target, occluded, monitor, rect, scale, duplicationEnabled] {
            WinCaptureWorker::Result result = worker->capture(monitor, rect, duplicationEnabled);
            // The worker holds the only reference to the image, so setDevicePixelRatio() modifies
            // the image in place.
            result.image.setDevicePixelRatio(scale);
            // The WinFrameSource destructor waits for m_thread, so the captured this pointer stays
            // valid while the worker lambda runs. Qt discards a queued call whose context object
            // was deleted.
            QMetaObject::invokeMethod(
                this,
                [this, target, occluded, scale, result] {
                    deliver(target, occluded, result.image, scale, result.method, result.error);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void WinFrameSource::deliver(
    const QRect &logical, const QRect &occluded, const QImage &image, qreal scale, Method method, const QString &error)
{
    if (image.isNull()) {
        Q_EMIT failed(error);
        issueNext();
        return;
    }
    m_lastMethod = method;
    Frame frame;
    frame.logicalRect = logical;
    frame.occluded = occluded;
    frame.image = image;
    frame.scale = scale;
    frame.hash = hashImage(frame.image);
    frame.grabMs = requestElapsedMs();
    Q_EMIT frameReady(frame);
    issueNext();
}

void WinFrameSource::issueNext()
{
    if (const std::optional<QRect> next = endRequest()) {
        issue(*next);
    }
}

} // namespace maru::capture
