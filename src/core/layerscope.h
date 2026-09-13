// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The layer-shell namespace of the popup surface, named by two modules that cannot see each
// other.
#pragma once

#include <QLatin1StringView>

namespace maru
{

// The namespace popup::PopupWindow gives its layer surface through
// LayerShellQt::Window::setScope(), and the string a compositor rule has to match to keep the
// card out of a screen copy.
//
// It lives in core/ rather than in popup/ because capture/ names it too, in the configuration
// line it tells the user to add, and capture/ must not depend on popup/ (capture/framesource.h).
// One definition rather than two: a rule naming a namespace the surface does not carry is
// accepted by the compositor and does nothing, which is the failure the two copies of this
// string invited.
constexpr QLatin1StringView popupLayerScope("marupop-popup");

} // namespace maru
