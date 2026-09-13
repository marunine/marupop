// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "keynorm.h"

#include "jp/japanese.h"

namespace maru::dict
{

QString normalizeKey(QStringView text)
{
    return jp::normalizeText(text);
}

} // namespace maru::dict
