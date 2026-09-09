// The connect indicator (design §6.5, D4).
//
// A 26 DIP strip along the bottom of the WINDOW: state dot, state word, server
// host, lock glyph — and NOTHING else in Normal mode. The owner ruled this a
// connect INDICATOR, not a network readout, so every other fact the app knows
// lives on the Network page (§6.4) or behind Advanced Mode (§6.6). The right
// half stays empty: a right-aligned readout is how a connect indicator creeps
// into a network readout (d5 §4.1).
//
// A struct of named elements plus free Make*/Set* functions: the
// UrComponents.h grain (see PaneListRowButton + SetPaneListRowSelected). No
// classes with virtuals, no MVVM, no observable types, no IDL.
//
// The strip carries no model of its own. `world` is read ONCE, in
// MakeStatusStrip: the demo's connect state, host and epoch are seeded data
// that nothing mutates — ambient activity (§9.2) only appends message rows —
// so there is no Set*Model and nothing to keep in sync afterwards.
//
// The pure half of this surface (the state words, the glyphs, the number
// formats) is Views/StatusStripRules.h, which has no winrt include so that
// --diagnose can assert it before the apartment exists.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// The drawer's automation name (d5 §4.4, the d7 audit's S3 override).
// Prefix-first framing for the reason InspectRailFields.cpp:113 records: the
// drawer states three live hops and per-node health as fact and carries no
// other always-visible frame off the Network page, and --demo-watermark=off
// removes the chip. Declared HERE and not in the .cpp so the `net framing`
// diagnose line reads the SAME constant the view sets.
inline constexpr wchar_t kStatusDrawerName[] = L"Demo model: relay path preview";

struct StatusStripView {
  // the 26 DIP surface: UrStatusStripStyle's #151515 and its top hairline
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  // the whole strip is the activator (design §6.5); MainWindow hangs Click on it
  winrt::Microsoft::UI::Xaml::Controls::Button strip{nullptr};
  // the preview drawer, hosted by MainWindow in the row ABOVE root so it draws
  // over the destination instead of pushing the strip down (d5 §4.4)
  winrt::Microsoft::UI::Xaml::FrameworkElement drawer{nullptr};
};

StatusStripView MakeStatusStrip(urmsg::demo::World const& world);

// DENSITY ONLY. It shows and hides the three Advanced fields MakeStatusStrip
// already built; it never rebuilds the strip and never runs a mode
// crossfade — re-running a builder to change density is what blanks a visible
// scroller to opacity 0 and fades it back over itself.
void SetStatusStripAdvanced(StatusStripView& v, bool advanced);

// Raise or dismiss the preview drawer. Open is CrossfadePageSwap with a null
// outgoing (the case UrMotion documents) plus the d1 rise and the mini-path's
// draw-in; close is kFastMs on the exit curve, one step faster (UrMotion.h's
// second rule). Both fall back to an instant swap when ShouldAnimate() is
// false.
void SetStatusStripDrawerOpen(StatusStripView& v, bool open);

}  // namespace urmsg::views
