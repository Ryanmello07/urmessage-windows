// SPDX-License-Identifier: MPL-2.0
//
// NO pch.h include, and App.vcxproj compiles this unit with
// PrecompiledHeader=NotUsing: the project pch pulls in every winrt/ header,
// and ParseDemoOptions runs from CollectDiagnostics before init_apartment.
// See DemoWorld.cpp/.h. <windows.h> is included directly instead — it is
// already required by <shellapi.h> below, so this costs nothing.
#include <windows.h>

#include "Demo/DemoSwitches.h"

#include <shellapi.h>  // CommandLineToArgvW

#include <initializer_list>
#include <string_view>

namespace urmsg::demo {
namespace {

constexpr DemoOptions kDefaults{false, DemoScreen::None, false, false, true, 0,
                                DemoComposer::AsIs};

// The ceiling for --demo-stress=N. The switch exists to find the thread's
// knee, and 20000 rows (~200x the fixture's longest conversation) is far
// past any knee the owner will ship near; an unclamped typo would otherwise
// get to decide whether the machine swaps.
constexpr int kMaxStressRows = 20000;

// The count half of --demo-stress=N: digits only, -1 for anything else. A
// non-numeric value is an UNRECOGNISED argument, not a stressed demo — the
// same carve-out as --demo=nope: a typo must never light up the demo.
constexpr int ParseStressCount(std::wstring_view s) {
  if (s.empty()) return -1;
  int v = 0;
  for (wchar_t c : s) {
    if (c < L'0' || c > L'9') return -1;
    v = v * 10 + (c - L'0');
    if (v > kMaxStressRows) v = kMaxStressRows;  // clamp, don't reject: the digits WERE a count
  }
  return v;
}

// The switch BODY with its prefix removed, or the argument unchanged when it
// carries no prefix (the bare form). Same four spellings WantsDiagnose takes.
constexpr std::wstring_view SwitchBody(std::wstring_view arg) {
  if (arg.starts_with(L"--")) return arg.substr(2);
  if (arg.starts_with(L"-")) return arg.substr(1);
  if (arg.starts_with(L"/")) return arg.substr(1);
  return arg;
}

constexpr DemoScreen ScreenFromName(std::wstring_view name) {
  if (name == L"chats") return DemoScreen::Chats;
  if (name == L"thread") return DemoScreen::Thread;
  if (name == L"inspect") return DemoScreen::Inspect;
  if (name == L"network") return DemoScreen::Network;
  if (name == L"settings") return DemoScreen::Settings;
  if (name == L"developer") return DemoScreen::Developer;
  return DemoScreen::None;
}

constexpr void ApplyArg(DemoOptions& o, std::wstring_view arg) {
  const std::wstring_view body = SwitchBody(arg);
  if (body == L"demo") { o.enabled = true; return; }
  if (body.starts_with(L"demo=")) {
    const DemoScreen s = ScreenFromName(body.substr(5));
    // An unknown screen name is NOT a demo switch. Enabling the demo on a
    // typo would open the fabricated world when the operator asked for
    // something else, which is the one failure mode a demo build must not
    // have.
    if (s == DemoScreen::None) return;
    o.enabled = true;
    o.screen = s;
    return;
  }
  if (body == L"demo-autoplay") { o.enabled = true; o.autoplay = true; return; }
  if (body == L"demo-advanced") { o.enabled = true; o.advanced = true; return; }
  if (body.starts_with(L"demo-stress=")) {
    const int n = ParseStressCount(body.substr(12));
    if (n < 0) return;  // not a demo switch: see ParseStressCount
    o.enabled = true;
    o.stressRows = n;
    return;
  }
  // Only "=off" turns the watermark off. Any other value leaves it ON: an
  // unrecognised argument must never be the thing that strips the mark from a
  // screenshot.
  if (body == L"demo-watermark=off") { o.enabled = true; o.watermark = false; return; }
  // --demo-composer=<session|observer> (item 242 R4). Exactly two values, and
  // an unrecognised one is NOT a demo switch - the --demo=nope carve-out, for
  // the same reason: a typo must never light up the demo, and it must never
  // silently leave the composer in a state nobody asked for.
  if (body == L"demo-composer=session") {
    o.enabled = true;
    o.composer = DemoComposer::Session;
    return;
  }
  if (body == L"demo-composer=observer") {
    o.enabled = true;
    o.composer = DemoComposer::Observer;
    return;
  }
}

constexpr DemoOptions ParseArgs(std::initializer_list<std::wstring_view> args) {
  DemoOptions o = kDefaults;
  for (std::wstring_view a : args) ApplyArg(o, a);
  return o;
}

}  // namespace

// THE PARSE TABLE. Checked at compile time, so it costs nothing at runtime
// and cannot rot: change ApplyArg and the build tells you which row moved.
static_assert(!ParseArgs({}).enabled);
static_assert(ParseArgs({}).screen == DemoScreen::None);
static_assert(ParseArgs({}).watermark);
static_assert(!ParseArgs({}).autoplay);
static_assert(!ParseArgs({}).advanced);

static_assert(ParseArgs({L"--demo"}).enabled);
static_assert(ParseArgs({L"-demo"}).enabled);
static_assert(ParseArgs({L"/demo"}).enabled);
static_assert(ParseArgs({L"demo"}).enabled);
static_assert(ParseArgs({L"--demo"}).screen == DemoScreen::None);

static_assert(ParseArgs({L"--demo=chats"}).screen == DemoScreen::Chats);
static_assert(ParseArgs({L"--demo=thread"}).screen == DemoScreen::Thread);
static_assert(ParseArgs({L"--demo=inspect"}).screen == DemoScreen::Inspect);
static_assert(ParseArgs({L"--demo=network"}).screen == DemoScreen::Network);
static_assert(ParseArgs({L"--demo=settings"}).screen == DemoScreen::Settings);
static_assert(ParseArgs({L"--demo=developer"}).screen == DemoScreen::Developer);
static_assert(ParseArgs({L"/demo=inspect"}).screen == DemoScreen::Inspect);
static_assert(ParseArgs({L"demo=network"}).screen == DemoScreen::Network);
static_assert(ParseArgs({L"--demo=inspect"}).enabled);

static_assert(ParseArgs({L"--demo=nope"}).screen == DemoScreen::None);
static_assert(!ParseArgs({L"--demo=nope"}).enabled);

static_assert(ParseArgs({L"--demo-autoplay"}).autoplay);
static_assert(ParseArgs({L"--demo-autoplay"}).enabled);
static_assert(!ParseArgs({L"--demo"}).autoplay);
// Cross-field non-contamination: --demo-autoplay touches ONLY autoplay
// (+ enabled). A mutant that also flips advanced, or clears watermark,
// must fail one of these.
static_assert(ParseArgs({L"--demo-autoplay"}).screen == DemoScreen::None);
static_assert(ParseArgs({L"--demo-autoplay"}).watermark);
static_assert(!ParseArgs({L"--demo-autoplay"}).advanced);

static_assert(ParseArgs({L"--demo-advanced"}).advanced);
static_assert(ParseArgs({L"--demo-advanced"}).enabled);
static_assert(!ParseArgs({L"--demo"}).advanced);
// Cross-field non-contamination: --demo-advanced touches ONLY advanced
// (+ enabled).
static_assert(ParseArgs({L"--demo-advanced"}).screen == DemoScreen::None);
static_assert(ParseArgs({L"--demo-advanced"}).watermark);
static_assert(!ParseArgs({L"--demo-advanced"}).autoplay);

static_assert(!ParseArgs({L"--demo-watermark=off"}).watermark);
static_assert(ParseArgs({L"--demo-watermark=off"}).enabled);
static_assert(ParseArgs({L"--demo-watermark=maybe"}).watermark);
// Cross-field non-contamination: --demo-watermark=off touches ONLY
// watermark (+ enabled).
static_assert(ParseArgs({L"--demo-watermark=off"}).screen == DemoScreen::None);
static_assert(!ParseArgs({L"--demo-watermark=off"}).autoplay);
static_assert(!ParseArgs({L"--demo-watermark=off"}).advanced);

static_assert(ParseArgs({L"--demo-stress=500"}).stressRows == 500);
static_assert(ParseArgs({L"--demo-stress=500"}).enabled);
static_assert(ParseArgs({L"-demo-stress=12"}).stressRows == 12);
static_assert(ParseArgs({L"/demo-stress=7"}).stressRows == 7);
static_assert(ParseArgs({L"demo-stress=3"}).stressRows == 3);
static_assert(ParseArgs({L"--demo-stress=0"}).enabled);   // explicit zero is still demo
static_assert(ParseArgs({L"--demo-stress=0"}).stressRows == 0);
static_assert(ParseArgs({L"--demo-stress=999999"}).stressRows == 20000);  // clamped
static_assert(!ParseArgs({L"--demo-stress=lots"}).enabled);  // not a count, not a demo
static_assert(ParseArgs({L"--demo-stress=lots"}).stressRows == 0);
static_assert(!ParseArgs({L"--demo-stress="}).enabled);
static_assert(ParseArgs({L"--demo"}).stressRows == 0);
// Cross-field non-contamination: --demo-stress=N touches ONLY stressRows
// (+ enabled).
static_assert(ParseArgs({L"--demo-stress=500"}).screen == DemoScreen::None);
static_assert(ParseArgs({L"--demo-stress=500"}).watermark);
static_assert(!ParseArgs({L"--demo-stress=500"}).autoplay);
static_assert(!ParseArgs({L"--demo-stress=500"}).advanced);

static_assert(!ParseArgs({L"--diagnose"}).enabled);

// Combinations. The single-switch rows above each prove what one switch does
// to its own field and (as of the cross-field rows) what it leaves alone;
// these prove the loop in ApplyArg composes them the way a real command line
// would.
//
// Two switches together: both flags land, neither's cross-field claim above
// is undone by the other running in the same pass.
static_assert(ParseArgs({L"--demo-advanced", L"--demo-autoplay"}).advanced);
static_assert(ParseArgs({L"--demo-advanced", L"--demo-autoplay"}).autoplay);
static_assert(ParseArgs({L"--demo-advanced", L"--demo-autoplay"}).screen ==
              DemoScreen::None);

// A repeated switch: last one on the command line wins, because ApplyArg
// runs left-to-right over argv and simply overwrites. Documenting the
// behaviour, not prescribing it.
static_assert(ParseArgs({L"--demo=chats", L"--demo=network"}).screen ==
              DemoScreen::Network);

// A screen and a stress count compose the way the measurement harness
// invokes them.
static_assert(ParseArgs({L"--demo=thread", L"--demo-stress=500"}).screen ==
              DemoScreen::Thread);
static_assert(ParseArgs({L"--demo=thread", L"--demo-stress=500"}).stressRows == 500);

// --demo-composer (item 242 R4). Three rows for the three states the composer
// can be put into, and the cross-field rows that say the switch touches ONE
// field - a mutant that also armed autoplay, stripped the watermark or moved
// the screen fails one of them.
static_assert(ParseArgs({}).composer == DemoComposer::AsIs);
static_assert(ParseArgs({L"--demo"}).composer == DemoComposer::AsIs);
static_assert(ParseArgs({L"--demo-composer=session"}).composer == DemoComposer::Session);
static_assert(ParseArgs({L"--demo-composer=session"}).enabled);
static_assert(ParseArgs({L"--demo-composer=observer"}).composer == DemoComposer::Observer);
static_assert(ParseArgs({L"--demo-composer=observer"}).enabled);
static_assert(ParseArgs({L"-demo-composer=observer"}).composer == DemoComposer::Observer);
static_assert(ParseArgs({L"/demo-composer=observer"}).composer == DemoComposer::Observer);
static_assert(ParseArgs({L"demo-composer=observer"}).composer == DemoComposer::Observer);
// An unknown value is NOT a demo switch and leaves the composer alone: the
// --demo=nope carve-out, applied to a switch whose typo would otherwise draw a
// composer state nobody asked for.
static_assert(ParseArgs({L"--demo-composer=maybe"}).composer == DemoComposer::AsIs);
static_assert(!ParseArgs({L"--demo-composer=maybe"}).enabled);
static_assert(ParseArgs({L"--demo-composer="}).composer == DemoComposer::AsIs);
static_assert(!ParseArgs({L"--demo-composer="}).enabled);
static_assert(ParseArgs({L"--demo-composer=observer"}).screen == DemoScreen::None);
static_assert(ParseArgs({L"--demo-composer=observer"}).watermark);
static_assert(!ParseArgs({L"--demo-composer=observer"}).autoplay);
static_assert(!ParseArgs({L"--demo-composer=observer"}).advanced);
static_assert(ParseArgs({L"--demo-composer=observer"}).stressRows == 0);
// Last one wins, the repeated-switch rule the screen rows already document.
static_assert(ParseArgs({L"--demo-composer=observer", L"--demo-composer=session"}).composer ==
              DemoComposer::Session);
// …and it composes with a screen, which is how the screenshots below are taken.
static_assert(ParseArgs({L"--demo=thread", L"--demo-composer=observer"}).screen ==
              DemoScreen::Thread);
static_assert(ParseArgs({L"--demo=thread", L"--demo-composer=observer"}).composer ==
              DemoComposer::Observer);
// No OTHER switch may move the composer.
static_assert(ParseArgs({L"--demo-autoplay"}).composer == DemoComposer::AsIs);
static_assert(ParseArgs({L"--demo-advanced"}).composer == DemoComposer::AsIs);
static_assert(ParseArgs({L"--demo-watermark=off"}).composer == DemoComposer::AsIs);
static_assert(ParseArgs({L"--demo-stress=500"}).composer == DemoComposer::AsIs);
static_assert(ParseArgs({L"--demo=inspect"}).composer == DemoComposer::AsIs);

// An unrecognised demo-* spelling is not a demo switch, same carve-out as
// --demo=nope above: a typo must never light up the demo.
static_assert(!ParseArgs({L"--demo-foo"}).enabled);

DemoOptions ParseDemoOptions() {
  DemoOptions out = kDefaults;
  int argc = 0;
  wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argv) return out;
  for (int i = 1; i < argc; ++i) ApplyArg(out, std::wstring_view{argv[i]});
  ::LocalFree(argv);
  return out;
}

}  // namespace urmsg::demo
