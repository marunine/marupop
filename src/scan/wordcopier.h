// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The copy-word hotkey: which string of a response the user gets, and how it reaches the
// clipboard.
//
// KSystemClipboard rather than QClipboard, because MaruPop never has keyboard focus. A
// Wayland client may only write the clipboard while it is focused; KSystemClipboard writes
// through the data-control protocol instead, which is the same mechanism Klipper uses.
#pragma once

#include "core/enums.h"
#include "lookup/lookuptypes.h"
#include "scan/hitcontext.h"

#include <QString>

namespace maru::scan
{

// The string CopyWordMode names, taken from the first result of the response. An empty
// response falls back to the highlighted span of the recognized paragraph, so the hotkey still
// copies what the popup underlines where the dictionaries found nothing.
[[nodiscard]] QString wordToCopy(const lookup::Response &response, const HitContext &context, CopyWordMode mode);

// Writes text to the clipboard as plain text. An empty string is ignored, which is what a
// hotkey pressed with no popup on screen does.
void copyToClipboard(const QString &text);

} // namespace maru::scan
