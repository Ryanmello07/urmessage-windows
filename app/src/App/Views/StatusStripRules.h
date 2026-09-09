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

}  // namespace urmsg::views
