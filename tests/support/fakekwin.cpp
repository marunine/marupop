// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "fakekwin.h"

#include "privatebus.h"

#include <QDBusConnection>
#include <QDBusMessage>

#include <algorithm>
#include <cerrno>
#include <unistd.h>

namespace maru::test
{

namespace
{

constexpr QLatin1StringView kService("org.kde.KWin");
constexpr QLatin1StringView kScreenShotPath("/org/kde/KWin/ScreenShot2");
constexpr QLatin1StringView kScriptingPath("/Scripting");
constexpr QLatin1StringView kCorePath("/KWin");

// A 64x64 Format_RGBA8888 image whose red channel is the column index and whose green channel is
// the row index. A test asserts on pixel(x, y) without a fixture file, and the value differs per
// pixel, so a stride error is visible as a wrong value rather than as a wrong size.
QImage defaultImage()
{
    QImage image(64, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(x * 4, y * 4, 128, 255));
        }
    }
    return image;
}

// Writes every row of image to fd, less withhold bytes of the tail. The write blocks while the
// pipe is full, which is safe because KWinGrab drains the read end from a QtConcurrent thread
// while this call runs (src/capture/kwingrabber.cpp, the pixelWatcher future).
void writeImage(int fd, const QImage &image, qsizetype withhold)
{
    const qsizetype stride = image.bytesPerLine();
    const qsizetype total = stride * image.height();
    const qsizetype target = total > withhold ? total - withhold : 0;

    qsizetype written = 0;
    while (written < target) {
        const auto *row = image.constScanLine(static_cast<int>(written / stride));
        const qsizetype offsetInRow = written % stride;
        const qsizetype remaining = std::min(stride - offsetInRow, target - written);
        const ssize_t count = ::write(fd, row + offsetInRow, static_cast<size_t>(remaining));
        if (count > 0) {
            written += count;
        } else if (count < 0 && errno != EINTR) {
            return; // the reader closed its end, which a cancellation test produces
        }
    }
}

} // namespace

FakeScreenShot2::FakeScreenShot2(QObject *parent)
    : QObject(parent)
    , image(defaultImage())
{}

FakeScreenShot2::~FakeScreenShot2() = default;

QString FakeScreenShot2::lastMethod() const
{
    return m_lastMethod;
}

QVariantList FakeScreenShot2::lastArguments() const
{
    return m_lastArguments;
}

QVariantMap FakeScreenShot2::lastOptions() const
{
    return m_lastOptions;
}

int FakeScreenShot2::callCount() const
{
    return m_callCount;
}

QVariantMap FakeScreenShot2::serve(const QString &method,
                                   const QVariantList &arguments,
                                   const QVariantMap &options,
                                   const QDBusUnixFileDescriptor &pipe)
{
    m_lastMethod = method;
    m_lastArguments = arguments;
    m_lastOptions = options;
    ++m_callCount;

    switch (failure) {
    case Failure::NoAuthorized:
        // The exact error name KWin raises for a caller whose executable matches no installed
        // desktop entry, which KWinGrab maps to Error::PermissionDenied.
        sendErrorReply(QStringLiteral("org.kde.KWin.ScreenShot2.Error.NoAuthorized"),
                       QStringLiteral("The process is not permitted to take a screenshot"));
        return {};
    case Failure::Cancelled:
        sendErrorReply(QStringLiteral("org.kde.KWin.ScreenShot2.Error.Cancelled"),
                       QStringLiteral("Screenshot got cancelled"));
        return {};
    case Failure::Unknown:
        sendErrorReply(QStringLiteral("org.kde.KWin.ScreenShot2.Error.Failed"), QStringLiteral("Failed"));
        return {};
    case Failure::None:
        break;
    }

    writeImage(pipe.fileDescriptor(), image, withholdBytes);

    // The five keys KWinGrab::maybeFinish() reads. "format" is the QImage::Format value, which
    // is how KWin reports it (kwin src/plugins/screenshot/screenshotdbusinterface2.cpp).
    return QVariantMap{
        {QStringLiteral("width"), static_cast<uint>(image.width())},
        {QStringLiteral("height"), static_cast<uint>(image.height())},
        {QStringLiteral("stride"), static_cast<uint>(image.bytesPerLine())},
        {QStringLiteral("format"), static_cast<uint>(image.format())},
        {QStringLiteral("scale"), scale},
    };
}

QVariantMap FakeScreenShot2::CaptureArea(
    int x, int y, uint width, uint height, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    return serve(QStringLiteral("CaptureArea"), {x, y, width, height}, options, pipe);
}

QVariantMap FakeScreenShot2::CaptureWorkspace(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    return serve(QStringLiteral("CaptureWorkspace"), {}, options, pipe);
}

QVariantMap
FakeScreenShot2::CaptureScreen(const QString &name, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    return serve(QStringLiteral("CaptureScreen"), {name}, options, pipe);
}

QVariantMap FakeScreenShot2::CaptureActiveScreen(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    return serve(QStringLiteral("CaptureActiveScreen"), {}, options, pipe);
}

QVariantMap
FakeScreenShot2::CaptureWindow(const QString &handle, const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    return serve(QStringLiteral("CaptureWindow"), {handle}, options, pipe);
}

QVariantMap FakeScreenShot2::CaptureActiveWindow(const QVariantMap &options, const QDBusUnixFileDescriptor &pipe)
{
    return serve(QStringLiteral("CaptureActiveWindow"), {}, options, pipe);
}

namespace
{

// One loaded script at /Scripting/Script<id>, which is the path org.kde.kwin.Script.run() is
// addressed to.
class FakeScript : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Script")

public:
    FakeScript(FakeScripting *owner, QString path)
        : QObject(owner)
        , m_owner(owner)
        , m_path(std::move(path))
    {}

public Q_SLOTS:

    Q_SCRIPTABLE void run()
    {
        m_owner->recordRun(m_path);
    }

    Q_SCRIPTABLE void stop() {}

private:
    FakeScripting *m_owner;
    QString m_path;
};

} // namespace

FakeScripting::FakeScripting(QObject *parent)
    : QObject(parent)
{}

FakeScripting::~FakeScripting() = default;

QStringList FakeScripting::loadedScripts() const
{
    return m_loadCalls;
}

QStringList FakeScripting::runScripts() const
{
    return m_run;
}

QStringList FakeScripting::unloadedScripts() const
{
    return m_unloaded;
}

void FakeScripting::markLoaded(const QString &pluginName)
{
    if (!m_loaded.contains(pluginName)) {
        m_loaded.append(pluginName);
    }
}

void FakeScripting::recordRun(const QString &path)
{
    m_run.append(path);
}

int FakeScripting::loadScript(const QString &filePath, const QString &pluginName)
{
    m_loadCalls.append(pluginName);
    if (acceptsLoad) {
        markLoaded(pluginName);
    }

    // Each script registers itself at /Scripting/Script<id> with org.kde.kwin.Script, which is
    // the path KWinScriptRelay::runScript() addresses run() to. The object is registered
    // explicitly rather than through ExportChildObjects on /Scripting: that flag makes the
    // parent resolve a child path by QObject::objectName, and a name that does not match leaves
    // run() answering org.freedesktop.DBus.Error.UnknownObject.
    const int id = m_nextId++;
    const QString path = QStringLiteral("/Scripting/Script%1").arg(id);
    QDBusConnection::sessionBus().registerObject(
        path, new FakeScript(this, path), QDBusConnection::ExportScriptableSlots);
    Q_UNUSED(filePath)
    return id;
}

bool FakeScripting::isScriptLoaded(const QString &pluginName)
{
    return m_loaded.contains(pluginName);
}

bool FakeScripting::unloadScript(const QString &pluginName)
{
    m_unloaded.append(pluginName);
    return m_loaded.removeAll(pluginName) > 0;
}

FakeKWinCore::FakeKWinCore(QObject *parent)
    : QObject(parent)
{}

FakeKWinCore::~FakeKWinCore() = default;

int FakeKWinCore::reconfigureCount() const
{
    return m_reconfigureCount;
}

void FakeKWinCore::reconfigure()
{
    ++m_reconfigureCount;
}

FakeKWin::FakeKWin(QObject *parent)
    : QObject(parent)
    , m_screenShot(new FakeScreenShot2(this))
    , m_scripting(new FakeScripting(this))
    , m_core(new FakeKWinCore(this))
{
    if (!privateBusAvailable()) {
        m_skipReason = privateBusSkipReason();
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    constexpr auto exportFlags = QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals;
    if (!bus.registerObject(kScreenShotPath, m_screenShot, exportFlags) ||
        !bus.registerObject(kScriptingPath, m_scripting, exportFlags) ||
        !bus.registerObject(kCorePath, m_core, exportFlags)) {
        m_skipReason = QStringLiteral("an object path of the fake org.kde.KWin was already registered");
        return;
    }
    if (!bus.registerService(kService)) {
        m_skipReason = QStringLiteral("the bus refused the name org.kde.KWin, which another process owns");
        return;
    }
    m_registered = true;
}

FakeKWin::~FakeKWin()
{
    if (m_registered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterService(kService);
        bus.unregisterObject(kScreenShotPath);
        bus.unregisterObject(kScriptingPath, QDBusConnection::UnregisterTree);
        bus.unregisterObject(kCorePath);
    }
}

bool FakeKWin::isRegistered() const
{
    return m_registered;
}

QString FakeKWin::skipReason() const
{
    return m_skipReason;
}

FakeScreenShot2 *FakeKWin::screenShot() const
{
    return m_screenShot;
}

FakeScripting *FakeKWin::scripting() const
{
    return m_scripting;
}

FakeKWinCore *FakeKWin::core() const
{
    return m_core;
}

} // namespace maru::test

#include "fakekwin.moc"
