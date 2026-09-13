// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/hyprsocket.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>

namespace maru::cursor
{

QString hyprSocketPath(const QString &runtimeDir, const QString &instanceSignature)
{
    if (runtimeDir.isEmpty() || instanceSignature.isEmpty()) {
        return {};
    }
    return runtimeDir + QLatin1String("/hypr/") + instanceSignature + QLatin1String("/.socket.sock");
}

QString hyprSocketPath()
{
    return hyprSocketPath(QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")),
                          QString::fromLocal8Bit(qgetenv("HYPRLAND_INSTANCE_SIGNATURE")));
}

std::optional<QPoint> parseCursorPos(const QByteArray &reply)
{
    const QByteArray trimmed = reply.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    if (trimmed.startsWith('{')) {
        const QJsonObject object = QJsonDocument::fromJson(trimmed).object();
        const QJsonValue x = object.value(QLatin1String("x"));
        const QJsonValue y = object.value(QLatin1String("y"));
        if (!x.isDouble() || !y.isDouble()) {
            return std::nullopt;
        }
        return QPoint(static_cast<int>(x.toDouble()), static_cast<int>(y.toDouble()));
    }

    // "<x>, <y>", from std::format("{}, {}", ...). Split on the comma rather than on whitespace,
    // because the space after it is the only whitespace the format produces and a reply carrying
    // none is still well formed.
    const QList<QByteArray> parts = trimmed.split(',');
    if (parts.size() != 2) {
        return std::nullopt;
    }
    bool xOk = false;
    bool yOk = false;
    const int x = parts.at(0).trimmed().toInt(&xOk);
    const int y = parts.at(1).trimmed().toInt(&yOk);
    if (!xOk || !yOk) {
        return std::nullopt;
    }
    return QPoint(x, y);
}

} // namespace maru::cursor
