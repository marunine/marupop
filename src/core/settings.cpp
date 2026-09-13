// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "core/settings.h"

#include "core/paths.h"

namespace maru::settings
{

QSize initialScanSize()
{
    return QSize{PopSettings::initialScanWidth(), PopSettings::initialScanHeight()};
}

QSize maxScanSize()
{
    return QSize{PopSettings::maxScanWidth(), PopSettings::maxScanHeight()};
}

QString modelDirectory()
{
    const QString configured = PopSettings::modelDirectory();
    if (configured.isEmpty()) {
        return paths::modelsDir();
    }
    return paths::expandPath(configured);
}

QString screenAiResourcesDir()
{
    return paths::expandPath(PopSettings::screenAiResourcesDir());
}

void persistScanningEnabled(bool enabled)
{
    if (PopSettings::scanningEnabled() == enabled) {
        return;
    }
    PopSettings::setScanningEnabled(enabled);
    PopSettings::self()->save();
}

} // namespace maru::settings
