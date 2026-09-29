// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The platform half of platform::detect(), defined once per platform family.
#pragma once

#include "platform/session.h"

namespace maru::platform
{

// The session the platform probes answer. detect() applies MARUPOP_PLATFORM and the cache
// around the call.
[[nodiscard]] Session detectNative();

} // namespace maru::platform
