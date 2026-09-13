// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "kwinsession.h"

#include "capture/kwingrabber.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QEventLoop>
#include <QTimer>
#include <QVariant>

#include <cstdlib>

namespace maru::test
{

QImage grabIncludingOwnWindows(const QRect &rect, QString *error)
{
    capture::KWinGrabber grabber;
    capture::KWinGrabber::Options options;
    options.includeOwnWindows = true;
    options.nativeResolution = true;

    QImage image;
    QString message;
    QEventLoop loop;
    capture::KWinGrab *pending = grabber.captureArea(rect, options);
    QObject::connect(pending, &capture::KWinGrab::finished, &loop, [&](const QImage &result) {
        image = result;
        loop.quit();
    });
    QObject::connect(
        pending, &capture::KWinGrab::failed, &loop, [&](capture::KWinGrab::Error /*code*/, const QString &reason) {
            message = reason;
            loop.quit();
        });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    if (error != nullptr) {
        *error = message;
    }
    return image;
}

bool coloursMatch(const QColor &left, const QColor &right, int tolerance)
{
    return std::abs(left.red() - right.red()) <= tolerance && std::abs(left.green() - right.green()) <= tolerance &&
           std::abs(left.blue() - right.blue()) <= tolerance;
}

QString kwinCompositingType()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                       QStringLiteral("/Compositor"),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("Get"));
    call << QStringLiteral("org.kde.kwin.Compositing") << QStringLiteral("compositingType");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().value<QDBusVariant>().variant().toString();
}

QString kwinScreenShotSkipReason()
{
    const QString type = kwinCompositingType();
    if (type.isEmpty() || type.startsWith(QLatin1StringView("gl"))) {
        return {};
    }
    return QStringLiteral("KWin composites with %1 rather than OpenGL, and org.kde.KWin.ScreenShot2 cancels every "
                          "capture without OpenGL compositing; kwin_wayland --virtual needs a DRM render node "
                          "(/dev/dri/renderD*) for OpenGL")
        .arg(type);
}

} // namespace maru::test
