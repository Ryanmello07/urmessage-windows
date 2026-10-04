// The demo composer's pure decisions, kept OUT of MainWindow so they can be
// checked without a window.
//
// This repo has no test project. `URmessage.exe --diagnose` is where logic in
// this app is asserted (Startup.cpp's CollectDiagnostics), and a breakpoint that
// is only ever exercised by dragging a window edge is a breakpoint nobody has
// checked.
//
// UNITS: LayoutFor takes CONTENT-ROOT dips, not window dips. ApplyBreakpoint
// passes Content().ActualWidth()/ActualHeight(), and the two differ by the
// window frame: the shipped log has `breakpoint -> narrow (466 dip)` for a
// 480-dip window and `wide (1186 dip)` for a 1200-dip one. The constants below
// are therefore CONTENT thresholds, and every assertion on them is a boundary
// test rather than a sampled window size.
//
// This header includes UrComponents.h so 1000 has ONE definition in the app,
// which drags the XAML projection into every TU that includes it. That is fine
// and is stated rather than hidden: nothing here CALLS a XAML API, every
// function is a pure function of its arguments, so it is safe from
// CollectDiagnostics() before winrt::init_apartment().
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string_view>

#include "Demo/DemoSwitches.h"
#include "UrComponents.h"  // urnw::kit::kWideBreakpointDip (UrComponents.h:75)

namespace urmsg::demo {

// The SECOND desktop breakpoint. urnw::kit::kWideBreakpointDip (1000) already
// decides list-only vs list-beside-thread; this one decides whether the
// inspector rail exists at all. Below it message inspect is simply UNAVAILABLE
// (design doc 6.5a) - no sheet, no fallback, no error. A narrowed demo window is
// a smaller demo, not a broken one.
inline constexpr double kRailBreakpointDip = 1500.0;

// The status strip is hidden below this content HEIGHT so it can never eat a
// readable thread at Spec C 1.2's 480dip minimum (design doc 6.5).
inline constexpr double kStripMinHeightDip = 560.0;

// The rail's width. FIXED, not a star weight, for the same reason ListColumn is
// fixed: a proportional rail grows into dead space on a 2000dip window.
inline constexpr double kRailWidthDip = 360.0;

// The list column's FIXED width once the thread sits beside it (d3 section 4):
// 320 up to kListWideBreakpointDip of content, 360 at or above it. A STEP, not
// a star weight - the fixed-not-proportional rule above is about star weights,
// not about steps. The extra 40 buys name+preview room at exactly the widths
// where the rail is absent and the list is the only left-hand content; above
// the rail breakpoint the step stays, so the two flanks read symmetrically
// (list 360 | thread | rail 360). The step is deliberately OUT of LayoutFor's
// Layout struct: that struct is gate-asserted field by field, and this width
// has its own boundary probes in Startup.cpp's DemoLayoutCheck instead.
inline constexpr double kListWidthDip = 320.0;
inline constexpr double kListWideWidthDip = 360.0;
inline constexpr double kListWideBreakpointDip = 1200.0;

// Only meaningful at or above urnw::kit::kWideBreakpointDip; below it the list
// column is a star and ApplyBreakpoint never consults this.
inline constexpr double ListWidthFor(double contentWidthDip) {
  return kListWideBreakpointDip <= contentWidthDip ? kListWideWidthDip : kListWidthDip;
}

struct Layout {
  bool wide;   // the thread pane sits beside the list
  bool rail;   // the 360dip inspector rail exists
  bool strip;  // the status strip exists
};

inline Layout LayoutFor(double contentWidthDip, double contentHeightDip) {
  return Layout{
      /*wide=*/urnw::kit::kWideBreakpointDip <= contentWidthDip,
      /*rail=*/kRailBreakpointDip <= contentWidthDip,
      /*strip=*/kStripMinHeightDip <= contentHeightDip,
  };
}

// What a `--demo=<screen>` deep link means to the composer. `inspect` is not a
// separate screen: it is `thread` with a message pre-selected and the rail
// already in message mode, which is the state a screenshot needs (contract v2
// section 2, design doc 8).
struct DeepLink {
  std::wstring_view navTag;  // "chats" | "network" | "settings" | "developer"
  bool selectConversation;   // open conversation 0
  // ...and pre-select kInspectTargetRowId (c0-r12), the row PickInspectMessage
  // returns - NOT "the newest Message row": that is c0-r23 (Pending), which
  // has no received-at and no device lists (the d7 distillation's ruling).
  bool selectMessage;
  bool forceAdvanced;        // Developer exists only under Advanced Mode
};

inline DeepLink DeepLinkFor(DemoScreen screen) {
  switch (screen) {
    case DemoScreen::Network:
      return DeepLink{L"network", false, false, false};
    case DemoScreen::Settings:
      return DeepLink{L"settings", false, false, false};
    case DemoScreen::Developer:
      // Developer is not a mode; it is a destination that exists only under
      // Advanced Mode (design doc 6.6), so deep-linking to it turns that on -
      // for the session, never as a preference write (AdvancedMode.h).
      return DeepLink{L"developer", false, false, true};
    case DemoScreen::Thread:
      return DeepLink{L"chats", true, false, false};
    case DemoScreen::Inspect:
      return DeepLink{L"chats", true, true, false};
    case DemoScreen::None:
    case DemoScreen::Chats:
      break;
  }
  return DeepLink{L"chats", false, false, false};
}

}  // namespace urmsg::demo
