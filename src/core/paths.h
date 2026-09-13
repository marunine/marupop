// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The directories the application writes to, resolved through QStandardPaths so a test running
// under QStandardPaths::setTestModeEnabled(true) lands in $HOME/.qttest rather than in the
// tester's own data directory.
#pragma once

#include <QString>

namespace maru::paths
{

// $XDG_DATA_HOME/marupop, created on demand. The models and the dictionaries live under it,
// rather than under the config directory, because both are downloaded content rather than
// configuration and both reach hundreds of megabytes.
[[nodiscard]] QString dataDir();

// dataDir()/models, created on demand. ocr::ModelStore writes the meikiocr ONNX files here
// when PopSettings::modelDirectory() is empty.
[[nodiscard]] QString modelsDir();

// dataDir()/dictionaries, created on demand. One SQLite database and one key-filter sidecar
// per imported dictionary, plus dictionaries.json listing them.
[[nodiscard]] QString dictionariesDir();

// The KWin script package directory, $XDG_DATA_HOME/kwin/scripts/marupopcursor. KWin's
// KPackage loader searches QStandardPaths::GenericDataLocation for kwin/scripts/<pluginId>,
// so the last path component has to equal the KPlugin Id in the package's metadata.json.
// Not created here: cursor::KWinScriptRelay writes the package and owns the directory.
[[nodiscard]] QString kwinScriptInstallDir();

// A leading ~ replaced with QDir::homePath(), and $VAR and ${VAR} replaced with the value of
// the environment variable. An empty path comes back empty. Applied to the two configured
// directory entries, ScreenAiResourcesDir and ModelDirectory, whose values a user types.
[[nodiscard]] QString expandPath(const QString &path);

} // namespace maru::paths
