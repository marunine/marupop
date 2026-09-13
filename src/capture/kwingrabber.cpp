// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/kwingrabber.h"

#include "core/logging.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QtConcurrentRun>

#include <fcntl.h>
#include <unistd.h>

namespace maru::capture
{

namespace
{

constexpr QLatin1StringView kService("org.kde.KWin");
constexpr QLatin1StringView kPath("/org/kde/KWin/ScreenShot2");
constexpr QLatin1StringView kInterface("org.kde.KWin.ScreenShot2");
constexpr int kTimeoutMs = 60000;

// The capacity pipe2() gives a new pipe, and the largest an unprivileged F_SETPIPE_SZ may ask
// for. A user who is over fs.pipe-user-pages-soft gets two pages instead of the usual sixteen
// and F_SETPIPE_SZ refused with EPERM, which is why the call below is advisory.
constexpr int kDefaultPipeSize = 65536;

// The most KWin can lose at the end of one image: Qt's QIODevice write-buffer size. See
// enlargePipe() and maybeFinish().
constexpr qsizetype kMaxLostTail = 16384;

int pipeMaxSize()
{
    QFile file{QStringLiteral("/proc/sys/fs/pipe-max-size")};
    if (!file.open(QIODevice::ReadOnly)) {
        return kDefaultPipeSize;
    }
    bool ok = false;
    const int value = file.readAll().trimmed().toInt(&ok);
    return ok ? value : kDefaultPipeSize;
}

// Asks for the largest pipe the kernel allows. KWin writes the pixels through a QFile on a
// non-blocking fd and returns as soon as the last bytes are in that QFile's 16 KiB write
// buffer; QFile::close() then flushes that tail, and a flush onto a pipe with no room fails
// without reporting anything back (kwin src/plugins/screenshot/screenshotdbusinterface2.cpp,
// ScreenShotWriter2::run). The bigger the pipe, the smaller the window in which it is full at
// that moment. maybeFinish() covers the tail that is lost anyway.
void enlargePipe(int fd)
{
    static const int maxSize = pipeMaxSize();
    if (maxSize > kDefaultPipeSize) {
        // EPERM where the user is over fs.pipe-user-pages-soft, which leaves the default.
        (void)::fcntl(fd, F_SETPIPE_SZ, maxSize);
    }
}

QByteArray drainPipe(int fd)
{
    QByteArray data;
    char buffer[65536];
    while (true) {
        const ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            data.append(buffer, count);
        } else if (count == 0) {
            break; // writer closed
        } else if (errno != EINTR) {
            ::close(fd);
            return {}; // null distinguishes read failure from short data
        }
    }
    ::close(fd);
    return data.isNull() ? QByteArray("") : data;
}

} // namespace

KWinGrab::KWinGrab(QObject *parent)
    : QObject(parent)
{}

QVariantMap KWinGrab::results() const
{
    return m_results;
}

void KWinGrab::start(const QString &method, const QVariantList &arguments, const QVariantMap &options)
{
    int fds[2] = {-1, -1};
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        // Queued so the caller can connect after the factory returns.
        QMetaObject::invokeMethod(
            this,
            [this] {
                failOnce(Error::Read, QStringLiteral("pipe2 failed"));
            },
            Qt::QueuedConnection);
        return;
    }

    // A pipe as large as the kernel allows, which on Linux is /proc/sys/fs/pipe-max-size and 1
    // MiB by default against a 64 KiB pipe. KWin writes the pixels through a QFile on a
    // non-blocking fd and returns as soon as the last bytes are in that QFile's 16 KiB write
    // buffer; QFile::close() then flushes the tail, and a flush onto a full pipe fails without
    // reporting anything (kwin screenshotdbusinterface2.cpp, ScreenShotWriter2::run). The
    // reply still describes the whole image, so the grab arrives up to 16 KiB short and the
    // size check below rejects it. A machine under load -- where the reader thread is
    // scheduled late and the pipe stays full -- reproduces it on every grab. The larger pipe
    // holds the whole image up to 512x512, and shortens the window in which the pipe is full
    // for every larger one. The call is advisory: a kernel that refuses it leaves the default.
    enlargePipe(fds[0]);

    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    QVariantList allArguments = arguments;
    allArguments << options << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
    message.setArguments(allArguments);
    ::close(fds[1]); // QDBusUnixFileDescriptor duplicated it

    // Drain concurrently with the D-Bus reply: KWin writes the pixels from a QRunnable on the
    // global thread pool and sends the reply before that write starts, so a reply-then-read
    // sequence blocks on a full pipe buffer for an image larger than the buffer. Every error
    // path closes the duplicate write end, so the reader always reaches EOF.
    auto *pixelWatcher = new QFutureWatcher<QByteArray>(this);
    connect(pixelWatcher, &QFutureWatcher<QByteArray>::finished, this, [this, pixelWatcher] {
        m_pixelData = pixelWatcher->result();
        m_pixelsReceived = true;
        pixelWatcher->deleteLater();
        maybeFinish();
    });
    pixelWatcher->setFuture(QtConcurrent::run(drainPipe, fds[0]));

    // pixelWatcher is parented to this KWinGrab and destroyed with it, which the analyzer does
    // not model: it reads the `new` above as an allocation nothing frees and anchors the report
    // on the first statement after it that can leave the function. The suppression covers that
    // one pointer, so an allocation added to this function that no parent owns still needs its
    // own release.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    const QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(message, kTimeoutMs);
    auto *replyWatcher = new QDBusPendingCallWatcher(call, this);
    connect(replyWatcher, &QDBusPendingCallWatcher::finished, this, [this, replyWatcher] {
        const QDBusPendingReply<QVariantMap> reply = *replyWatcher;
        replyWatcher->deleteLater();
        if (reply.isError()) {
            const QString errorName = reply.error().name();
            if (errorName == QLatin1StringView("org.kde.KWin.ScreenShot2.Error.Cancelled")) {
                failOnce(Error::Cancelled, reply.error().message());
            } else if (errorName == QLatin1StringView("org.kde.KWin.ScreenShot2.Error.NoAuthorized")) {
                failOnce(Error::PermissionDenied,
                         QStringLiteral("KWin rejected the caller; the desktop entry with "
                                        "X-KDE-DBUS-Restricted-Interfaces must match the running binary"));
            } else {
                failOnce(Error::DBus, QStringLiteral("%1: %2").arg(errorName, reply.error().message()));
            }
            return;
        }
        m_results = reply.value();
        m_replyReceived = true;
        maybeFinish();
    });
    // replyWatcher is parented the same way and reported the same way, anchored on the closing
    // brace instead.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
}

void KWinGrab::maybeFinish()
{
    if (m_done || !m_replyReceived || !m_pixelsReceived) {
        return;
    }

    const uint width = m_results.value(QStringLiteral("width")).toUInt();
    const uint height = m_results.value(QStringLiteral("height")).toUInt();
    const uint stride = m_results.value(QStringLiteral("stride")).toUInt();
    const auto format = static_cast<QImage::Format>(m_results.value(QStringLiteral("format")).toUInt());
    const qreal scale = m_results.value(QStringLiteral("scale"), 1.0).toReal();

    const qsizetype expected = static_cast<qsizetype>(stride) * height;
    const qsizetype missing = expected - m_pixelData.size();
    if (width == 0 || height == 0 || format <= QImage::Format_Invalid || format >= QImage::NImageFormats ||
        missing > kMaxLostTail) {
        failOnce(Error::Read,
                 QStringLiteral("invalid image reply: %1x%2 stride %3 format %4, %5 bytes")
                     .arg(width)
                     .arg(height)
                     .arg(stride)
                     .arg(static_cast<int>(format))
                     .arg(m_pixelData.size()));
        return;
    }
    if (missing > 0) {
        // The tail KWin buffered and could not flush (see enlargePipe). It is at most 16 KiB,
        // which is the last eight rows of a 480-pixel-wide grab and the last two of a
        // 1600-pixel-wide one, so the frame is still worth recognizing; the alternative is to
        // fail every grab on a machine whose pipes are capped at two pages. Reported once per
        // process, because in that state it happens on every grab.
        static bool reported = false;
        if (!reported) {
            reported = true;
            qCWarning(logKWinGrabber)
                << "KWin delivered" << missing
                << "bytes fewer than the image it described; the missing rows at the bottom are filled with black. "
                   "This is the compositor losing the tail its screenshot writer buffered, and it happens while "
                   "the session is over fs.pipe-user-pages-soft, which caps new pipes at two pages.";
        }
        m_pixelData.append(missing, '\0');
    }
    m_done = true;

    const QImage wrapper(reinterpret_cast<const uchar *>(m_pixelData.constData()),
                         static_cast<int>(width),
                         static_cast<int>(height),
                         static_cast<qsizetype>(stride),
                         format);
    QImage image = wrapper.copy();
    image.setDevicePixelRatio(scale);
    Q_EMIT finished(image);
    deleteLater();
}

void KWinGrab::failOnce(Error error, const QString &message)
{
    if (m_done) {
        return;
    }
    m_done = true;
    if (error != Error::Cancelled) {
        // Once per distinct message: a grab that fails for a standing reason -- no compositor,
        // an unauthorized binary -- fails identically for every poll tick.
        static QString lastMessage;
        if (message != lastMessage) {
            lastMessage = message;
            qCWarning(logKWinGrabber) << "grab failed:" << static_cast<int>(error) << message;
        }
    }
    Q_EMIT failed(error, message);
    deleteLater();
}

KWinGrabber::KWinGrabber(QObject *parent)
    : QObject(parent)
{}

KWinGrab *KWinGrabber::startGrab(const QString &method, const QVariantList &arguments, const Options &options)
{
    QVariantMap optionMap;
    optionMap.insert(QStringLiteral("include-cursor"), options.includeCursor);
    optionMap.insert(QStringLiteral("include-decoration"), options.includeDecoration);
    optionMap.insert(QStringLiteral("include-shadow"), options.includeShadow);
    // The one option whose polarity flips here: ScreenShot2 asks which windows to leave out,
    // and every caller above asks which windows to keep.
    optionMap.insert(QStringLiteral("hide-caller-windows"), !options.includeOwnWindows);
    optionMap.insert(QStringLiteral("native-resolution"), options.nativeResolution);

    auto *grab = new KWinGrab(this);
    grab->start(method, arguments, optionMap);
    return grab;
}

KWinGrab *KWinGrabber::captureWorkspace(const Options &options)
{
    return startGrab(QStringLiteral("CaptureWorkspace"), {}, options);
}

KWinGrab *KWinGrabber::captureArea(const QRect &area, const Options &options)
{
    return startGrab(QStringLiteral("CaptureArea"),
                     {area.x(), area.y(), static_cast<uint>(area.width()), static_cast<uint>(area.height())},
                     options);
}

KWinGrab *KWinGrabber::captureScreen(const QString &name, const Options &options)
{
    return startGrab(QStringLiteral("CaptureScreen"), {name}, options);
}

KWinGrab *KWinGrabber::captureActiveScreen(const Options &options)
{
    return startGrab(QStringLiteral("CaptureActiveScreen"), {}, options);
}

KWinGrab *KWinGrabber::captureActiveWindow(const Options &options)
{
    return startGrab(QStringLiteral("CaptureActiveWindow"), {}, options);
}

KWinGrab *KWinGrabber::captureWindow(const QString &uuid, const Options &options)
{
    return startGrab(QStringLiteral("CaptureWindow"), {uuid}, options);
}

bool KWinGrabber::serviceAvailable()
{
    const auto *interface = QDBusConnection::sessionBus().interface();
    return interface != nullptr && interface->isServiceRegistered(kService);
}

} // namespace maru::capture
