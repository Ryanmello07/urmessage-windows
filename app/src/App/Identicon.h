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
// constexpr (fix round 1): lets Identicon.cpp static_assert the density
// extremes (an all-zero-bits seed, an all-one-bits seed) it can reach, which
// is a compile time check the runtime --diagnose pass over 30 fixed demo
// seeds cannot substitute for - see the two static_asserts below
// MakeIdenticonPattern's definition in the .cpp.
constexpr IdenticonPattern MakeIdenticonPattern(urmsg::demo::Seed const& seed);

// The corner radius, as pure arithmetic so --diagnose can read it without an
// apartment (Startup.cpp asserts it as gate P5). PROPORTIONAL to size — every
// identicon in the app is the same shape whatever surface it lands on, and a
// fixed 8 read as an almost-circle on the rail's 20px member chips while
// under-rounding the 40px list avatars. 20px -> 8 is the value the old fixed
// literal gave every size.
constexpr double IdenticonCornerRadius(double size) { return size / 2.5; }

// Applies its own corner radius (IdenticonCornerRadius) - callers must NOT
// set one.
// Palette is constrained to hues that cannot be mistaken for state: fix
// round 1 replaced an exact-colour-match check (which a Tint() toward an
// achromatic surface can never trip, since mixing with grey cannot change
// hue) with a HUE-DISTANCE minimum against kDanger, kUrGreen,
// kToggleAccent, kStatusConnecting, kProGold AND kStatusIdle (added in the
// same round - it is the idle state of the same three-state connect
// indicator as the other two status colours, and was missing). The .cpp
// proves the distance with a static_assert rather than promising it.
winrt::Microsoft::UI::Xaml::Controls::Border MakeIdenticon(
    urmsg::demo::Seed const& seed, double size = 40);

// Rewrites the plate's alpha IN PLACE on the identicon's own Background
// brush (the WithAlpha(palette hue, 0x33) one MakeIdenticon built). A hover
// lift is a one-step alpha change on that SAME brush, never a new colour:
// the hue stays the compile-time-proven palette entry, so the palette's
// state-colour separation proof is untouched. No-op on a null border or a
// background that is not a solid brush.
void SetIdenticonPlateAlpha(
    winrt::Microsoft::UI::Xaml::Controls::Border const& identicon, uint8_t alpha);

// The EMPTY inverse of an identicon (d3 2.3): the same 5x5 star grid, but no
// seed, no plate and no fill - every cell a 1px rounded-square OUTLINE in
// kTextFaint at 0x33, the frame a person-mark would occupy. Achromatic by
// construction (grey has no hue), so it provably cannot collide with the
// palette's hue-separation proof or any state colour, and no new brand
// colour enters the app. For empty/loading states and decorative echoes -
// never a stand-in for a person. Applies its own corner radius
// (IdenticonCornerRadius) - callers must NOT set one.
winrt::Microsoft::UI::Xaml::Controls::Border MakeIdenticonLattice(double size);

}  // namespace urmsg
