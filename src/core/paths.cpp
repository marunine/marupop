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
    // On Windows, AppDataLocation is in the roaming profile, which Windows copies from the profile
    // server at every sign-in. On Linux, AppLocalDataLocation and AppDataLocation name one folder.
    return ensure(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
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
    if (result.startsWith(QLatin1StringView("~/")) || result.startsWith(QLatin1Char('~') + QDir::separator())) {
        result = QDir::homePath() + result.mid(1);
    }
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const auto expand = [&](const QRegularExpression &pattern, bool keepUnset) {
        qsizetype offset = 0;
        while (true) {
            const QRegularExpressionMatch match = pattern.matchView(result, offset);
            if (!match.hasMatch()) {
                break;
            }
            QString name;
            for (int group = match.lastCapturedIndex(); group > 0 && name.isEmpty(); --group) {
                name = match.captured(group);
            }
            if (keepUnset && !environment.contains(name)) {
                offset = match.capturedEnd();
                continue;
            }
            const QString value = environment.value(name);
            result.replace(match.capturedStart(), match.capturedLength(), value);
            offset = match.capturedStart() + value.size();
        }
    };
#ifdef Q_OS_WIN
    // One pass expands %VAR%, $VAR and ${VAR}, and the scan resumes after each substituted value.
    // cmd.exe and the Explorer address bar expand %VAR% and keep an unset %VAR% as written.
    // expandPath() keeps an unset $VAR and ${VAR} as written as well, because a Windows folder
    // name can start with $, as $Recycle.Bin does.
    static const QRegularExpression variable(
        QStringLiteral("%([A-Za-z_][A-Za-z0-9_()]*)%|\\$(?:\\{([A-Za-z_][A-Za-z0-9_]*)\\}|([A-Za-z_][A-Za-z0-9_]*))"));
    expand(variable, true);
#else
    // $VAR and ${VAR}; unset variables expand to nothing (shell behavior).
    static const QRegularExpression variable(
        QStringLiteral("\\$(\\{([A-Za-z_][A-Za-z0-9_]*)\\}|([A-Za-z_][A-Za-z0-9_]*))"));
    expand(variable, false);
#endif
    return result;
}

} // namespace maru::paths
