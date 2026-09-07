// Deterministic identicons (Spec C W9): a 5x5 mirrored pattern and one
// palette colour, derived from a 32-byte identity key by arithmetic alone.
// The property this buys the product for free: when a contact's key changes,
// their avatar visibly changes.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <array>
#include <cstddef>

#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"

namespace urmsg {

// The PURE half. Added beyond contract v2 section 3 and flagged as such:
// MakeIdenticon below constructs a Border, so it cannot be called from
// CollectDiagnostics(), which runs before winrt::init_apartment - and that is
// the only harness this repo has. This is the same pre-apartment gap contract
// section 1 already closed for DemoWorld. MakeIdenticon's signature is
// unchanged; nothing was renamed and no struct gained a field.
struct IdenticonPattern {
  std::array<bool, 25> cells{};  // row-major 5x5, mirrored left-to-right
  size_t colorIndex = 0;
};
IdenticonPattern MakeIdenticonPattern(urmsg::demo::Seed const& seed);

// Applies its own CornerRadius(8) - callers must NOT set one.
// Palette is constrained to hues that cannot be mistaken for state: it NEVER
// emits kDanger, kUrGreen, kToggleAccent, kStatusConnecting or kProGold, and
// the .cpp proves it with a static_assert rather than promising it.
winrt::Microsoft::UI::Xaml::Controls::Border MakeIdenticon(
    urmsg::demo::Seed const& seed, double size = 40);

}  // namespace urmsg
