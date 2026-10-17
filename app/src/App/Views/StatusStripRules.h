// The status strip's decisions that are arithmetic and text, with no element
// tree behind them (design §6.5, §6.6).
//
// PURE C++. No winrt/ include and no XAML type in this header or its .cpp, for
// the same reason DemoWorld.h has none: Startup.cpp's CollectDiagnostics()
// asserts these, and wWinMain calls that function BEFORE
// winrt::init_apartment() (main.cpp calls CollectDiagnostics at :176 and
// init_apartment at :191). Anything here that activated a WinRT type would
// fault at the one point in the process where a fault is hardest to see.
//
// There is no Localized() call anywhere in this file either, and there is no
// resw key behind StatusStateWord. Strings/en/Resources.resw is GENERATED from
// urnetwork/localizations (Localization.h) and this work adds no key to it;
// demo copy is English string literals (design §9.4) and localization is out
// of scope (design §2).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// Design §6.5's strip height, named once rather than spelled at each use.
//
// The 560 content-dip COLLAPSE rule is deliberately NOT declared here: the d7
// audit's S1 override rules that urmsg::demo::kStripMinHeightDip
// (Demo/DemoShellState.h:41) and LayoutFor().strip already own it,
// ApplyBreakpoint already consumes them, and Startup.cpp's DemoLayoutCheck
// stripEdge term already asserts that exact boundary. This file ships no
// kStatusStripCollapseDip, no ShouldShowStatusStrip and no `strip collapse`
// line — a second owner of one threshold is how two surfaces drift.
inline constexpr double kStatusStripHeightDip = 26.0;

// The state word itself, as an English literal. Static storage duration, so
// the view may hold the view onto it. Covers all three enum values even though
// the fixture seeds only Connected — the words are this surface's asserted
// contract (d5 §4.2).
std::wstring_view StatusStateWord(demo::ConnectState state);

// Segoe Fluent Icons: e72e closed padlock (Lock), e785 open padlock (Unlock).
// The two states differ in SHAPE, not only in the colour the view paints them,
// which is why key verification does not ride on colour alone.
std::wstring StatusLockGlyph(bool keyVerified);

// The one place the strip decides how a number reads. Thin today; the point is
// that the day epoch renders as hex or rec/s gains a decimal, it is one line
// to change and the `strip fields` assertion already covers it.
std::wstring StatusEpochValue(uint64_t epoch);
std::wstring StatusRecordsValue(int recordsPerSecond);

// ---- S3: the relay drawer's rows and the strip fit their own width -----------------------------
//
// THE DEFECT, from the owner's screenshot of build 03. A drawer row is a kit::PaneListRow, whose
// species gives the title the rest of the row and the meta its natural width: right for a long
// name beside a short figure, and wrong here, where the title is a short label and the meta is
// the message server's 36-character client_id. The value took its whole width and the label was
// squeezed to "M…". Separately, the strip is one horizontal panel, so at narrow widths its text
// ran off its own right edge ("URnetwork exit · United States" was cut by the window).
//
// The decisions are here, as data and arithmetic, so --diagnose asserts them before the apartment
// exists; the view only turns them into XAML. Each mutant that undoes one of them turns its
// --diagnose line FAIL.

// How a text is cut when it is wider than its room. The view maps these one to one onto
// TextTrimming::None and TextTrimming::CharacterEllipsis.
enum class TextCut { None, Ellipsis };

// A relay drawer row: the hop's LABEL is never cut and sizes to its content; its VALUE takes the
// rest of the row and is cut with an ellipsis.
struct RelayRowFit {
  TextCut label = TextCut::None;
  TextCut value = TextCut::Ellipsis;
  bool labelSizesToContent = true;  // the label's column is Auto, the value's Star
};
RelayRowFit RelayDrawerRowFit();

// What a drawer row draws as its value. Its tooltip and its help text are this same string, so a
// value the row has to cut is still there WHOLE (the drawn TextBlock's own text is whole too: the
// cut is only how it is painted).
std::wstring RelayRowValue(std::wstring_view subLabel, bool healthy);
// The row's automation name: its label, its whole value and its state, as one fact.
std::wstring RelayRowName(std::wstring_view label, std::wstring_view subLabel, bool healthy);

// THE STRIP NEVER DRAWS PAST ITS OWN EDGE, at any width from the window's 400 DIP minimum up.
// Each item of the strip, left to right, with its natural width in DIPs:
enum class StripPart {
  Fixed,     // the dot, the state word, the separators and the padlock: never cut, never dropped
  Caption,   // the "server" caption: dropped, once every Optional is gone
  Value,     // the server's value: then cut with an ellipsis, to no narrower than its floor
  Optional,  // an Advanced Mode field with its separator: dropped first, rightmost first
};
struct StripItem {
  StripPart part = StripPart::Fixed;
  double width = 0;  // natural width, separator included for an Optional
  double floor = 0;  // a Value's narrowest cut
};
enum class StripItemFit { Whole, Cut, Dropped };
struct StripFit {
  std::vector<StripItemFit> fit;  // one per item, in order
  std::vector<double> width;      // the width each item is given; 0 when dropped
  double total = 0;
  bool fits = true;               // total <= room
};
StripFit FitStatusStrip(std::vector<StripItem> const& items, double room);

// What the strip's tooltip and help text say: every fact it carries, whole, one per line, so a
// fact the fit dropped or cut is still reachable.
std::wstring StatusStripFacts(std::vector<std::wstring> const& facts);

}  // namespace urmsg::views
