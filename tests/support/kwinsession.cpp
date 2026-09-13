// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "kwinsession.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QVariant>

namespace maru::test
{

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
