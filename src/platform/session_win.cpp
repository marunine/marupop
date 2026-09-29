// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "platform/session_p.h"

namespace maru::platform
{

Session detectNative()
{
    return Session::Windows;
}

} // namespace maru::platform
