// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "core/paths.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace maru::paths
{

namespace
{

// Creates the directory and returns it either way. A failed mkpath leaves the caller a path
// its own open or write reports on, which is where the error names the file that failed.
QString ensure(const QString &path)
{
    QDir{}.mkpath(path);
    return path;
}

} // namespace

QString dataDir()
{
    return ensure(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

QString modelsDir()
{
    return ensure(dataDir() + QStringLiteral("/models"));
}

QString dictionariesDir()
{
    return ensure(dataDir() + QStringLiteral("/dictionaries"));
}

QString kwinScriptInstallDir()
{
    // GenericDataLocation rather than AppDataLocation: the package belongs to KWin's plugin
    // tree, which KPackage::PackageLoader::listPackages("KWin/Script", "kwin/scripts/") reads
    // from $XDG_DATA_HOME and from the system data directories.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           QStringLiteral("/kwin/scripts/marupopcursor");
}

QString expandPath(const QString &path)
{
    QString result = path;
    if (result == QLatin1StringView("~")) {
        return QDir::homePath();
    }
    if (result.startsWith(QLatin1StringView("~/"))) {
        result = QDir::homePath() + result.mid(1);
    }
    // $VAR and ${VAR}; unset variables expand to nothing (shell behavior).
    static const QRegularExpression variable(
        QStringLiteral("\\$(\\{([A-Za-z_][A-Za-z0-9_]*)\\}|([A-Za-z_][A-Za-z0-9_]*))"));
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    qsizetype offset = 0;
    while (true) {
        const QRegularExpressionMatch match = variable.matchView(result, offset);
        if (!match.hasMatch()) {
            break;
        }
        const QString name = match.captured(2).isEmpty() ? match.captured(3) : match.captured(2);
        const QString value = environment.value(name);
        result.replace(match.capturedStart(), match.capturedLength(), value);
        offset = match.capturedStart() + value.size();
    }
    return result;
}

} // namespace maru::paths
