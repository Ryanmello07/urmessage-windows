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
#include "RunMode.h"  // urmsg::RelayDrawerName / ServerKeyStateName

namespace urmsg::views {

// THE DRAWER'S AUTOMATION NAME MOVED to urmsg::RelayDrawerName (RunMode.h) when
// it became a PAIR, and the constant is gone rather than kept beside it — one
// owner, or the two drift. Everything the constant's note said still holds for
// the fabricated arm (d5 §4.4, the d7 audit's S3 override): the drawer states
// three hops and per-node health as fact, carries no other always-visible frame
// off the Network page, and --demo-watermark=off removes the chip, so it frames
// itself. `net framing` still reads the SAME function the view sets.
//
// What the live arm drops is the word "preview": with a live world those three
// nodes are the platform url the library actually dialled and the message
// server's own client_id (Live/LiveWorld.cpp:336-345), so the drawer is showing
// a path rather than previewing what one would look like.

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
