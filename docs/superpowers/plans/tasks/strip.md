# Status strip

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task S1: Status strip 1/4 — the pure rules and their `--diagnose` assertions

**Files:**

Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/StatusStripRules.h; Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/StatusStripRules.cpp; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.h; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.cpp

**Interfaces:**

- Consumes: The **DemoWorld task**'s `app/src/App/Demo/DemoWorld.h` — specifically `urmsg::demo::ConnectState` (fixed contract §1). `urnw::CollectDiagnostics()` (declared `Startup.h:44`, defined `Startup.cpp:178`). Nothing else; this task deliberately touches no XAML and no view.
- Produces: namespace `urmsg::views`, in `app/src/App/Views/StatusStripRules.h`:
```cpp
inline constexpr double kStatusStripHeightDip = 26.0;
inline constexpr double kStatusStripCollapseDip = 560.0;
bool ShouldShowStatusStrip(double windowHeightDip);
std::wstring_view StatusStateWord(demo::ConnectState state);
std::wstring StatusLockGlyph(bool keyVerified);
std::wstring StatusEpochValue(uint64_t epoch);
std::wstring StatusRecordsValue(int recordsPerSecond);
```
Plus two new `--diagnose` lines: `strip collapse` and `strip fields`.

# Status strip 1/4 — the pure rules and their `--diagnose` assertions

The strip has three decisions that are arithmetic and text, not layout: **when it collapses**, **which word the state is**, and **which glyph the lock is** (plus the two ways Advanced Mode's numbers read). This repo has no test project, so `--diagnose` is the test runner — see `CollectDiagnostics()` at `app/src/App/Startup.cpp:178`. This task writes those rules and pins them with two assertion lines that read `PASS` or `FAIL`.

**Two traps to know before you start.**

1. `CollectDiagnostics()` is called from `wWinMain` at `app/src/App/main.cpp:168` — **before `winrt::init_apartment()` on `main.cpp:183`**. Nothing these functions do may activate a WinRT type, and none may call `urnw::Localized()`: the resource loader caches on first use, and `ResourceProbe`'s declaration in `Startup.h` records that a lookup made too early leaves the whole UI rendering key ids.
2. `main.cpp:169` then calls `LogDiagnostics(...)` unconditionally, so **every line here lands in a normal user's `urmessage-app.log`**. That is why these two assertions are pure: they build no demo world and name no hostname. Anything needing `GetWorld()` belongs to the DemoWorld task's own invariants (fixed contract §1.1), not here.

---

- [ ] **Step 1: Gate — confirm `DemoWorld.h` carries `ConnectState`.**

Everything below is written against a type the DemoWorld task owns. Check it before writing a line, so this task cannot quietly stand in for it.

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoWorld.h' -Pattern 'enum class ConnectState|ConnectState connectState|sessionMode|recordsPerSecond|struct ServerInfo'"
```

Expected: **at least 5 matching lines** — `enum class ConnectState`, `ConnectState connectState;` on `World`, `std::wstring sessionMode;`, `int recordsPerSecond;`, and `struct ServerInfo`.

If the file does not exist or the output is short, **STOP**. Do not define `ConnectState` here: the Network page (design §6.4) renders the same connect state, and a second definition under `Views/` is exactly how two surfaces drift. Report that the DemoWorld task has not landed.

- [ ] **Step 2: Create `app/src/App/Views/StatusStripRules.h`.**

```cpp
// The status strip's decisions that are arithmetic and text, with no element
// tree behind them (design 6.5, 6.6).
//
// PURE C++. No winrt/ include and no XAML type in this header or its .cpp, for
// the same reason DemoWorld.h has none: Startup.cpp's CollectDiagnostics()
// asserts these, and wWinMain calls that function BEFORE
// winrt::init_apartment() (main.cpp calls CollectDiagnostics at :168 and
// init_apartment at :183). Anything here that activated a WinRT type would
// fault at the one point in the process where a fault is hardest to see.
//
// There is no Localized() call anywhere in this file either, and there is no
// resw key behind StatusStateWord. Strings/en/Resources.resw is GENERATED from
// urnetwork/localizations (Localization.h) and this work adds no key to it;
// demo copy is English string literals (design 9.4) and localization is out of
// scope (design 2).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// Design 6.5's two figures, named once rather than spelled at each use.
inline constexpr double kStatusStripHeightDip = 26.0;
// "hidden below 560 DIP of window height so it can never eat a readable thread
// at Spec C 1.2's 480 DIP minimum".
inline constexpr double kStatusStripCollapseDip = 560.0;

// The collapse rule. True when the strip is drawn at this window height. It says
// nothing about whether the demo is on — that gate is MainWindow's, and keeping
// the two separate is what lets this one be asserted without a world.
bool ShouldShowStatusStrip(double windowHeightDip);

// The state word itself, as an English literal. Static storage duration, so the
// view may hold the view onto it.
std::wstring_view StatusStateWord(demo::ConnectState state);

// Segoe Fluent Icons: e72e closed padlock (Lock), e785 open padlock (Unlock).
// The two states differ in SHAPE, not only in the colour the view paints them,
// which is why key verification does not ride on colour alone.
std::wstring StatusLockGlyph(bool keyVerified);

// The one place the strip decides how a number reads. Thin today; the point is
// that the day epoch renders as hex or rec/s gains a decimal, it is one line to
// change and the assertion below already covers it.
std::wstring StatusEpochValue(uint64_t epoch);
std::wstring StatusRecordsValue(int recordsPerSecond);

}  // namespace urmsg::views
```

`#include "Demo/DemoWorld.h"` resolves because `App.vcxproj:125` puts `$(MSBuildProjectDirectory)` on `/I`; a quoted include is tried against the including file's own directory (`Views/`) first, then that list.

- [ ] **Step 3: Create `app/src/App/Views/StatusStripRules.cpp` with UNIMPLEMENTED stubs.**

This is the step that makes step 7's `FAIL` real. In C++ an assertion against a function that does not exist is a compile error, not a failing test, so the functions exist and return the wrong answer first. Note that even the stubs return `L"?"` and never `L""` — an empty glyph literal is forbidden in this work (contract rule 3) and a stub is not an exemption.

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/StatusStripRules.h"

namespace urmsg::views {

// ---- STUBS. Implemented in step 8; they exist now so the --diagnose
// ---- assertions compile and FAIL against them.

bool ShouldShowStatusStrip(double) { return false; }

std::wstring_view StatusStateWord(demo::ConnectState) { return L"?"; }

std::wstring StatusLockGlyph(bool) { return L"?"; }

std::wstring StatusEpochValue(uint64_t) { return L"?"; }

std::wstring StatusRecordsValue(int) { return L"?"; }

}  // namespace urmsg::views
```

`#include "pch.h"` must be the first line: `App.vcxproj`'s `ItemDefinitionGroup` sets `PrecompiledHeader=Use` for every `ClCompile`. It resolves through `/I $(MSBuildProjectDirectory)` even though this file lives one directory down.

- [ ] **Step 4: Register both files in `app/src/App/App.vcxproj`.**

Add one line to each of the two existing `ItemGroup`s (they start at lines 175 and 187).

Immediately after `<ClCompile Include="UrComponents.cpp" />` (line 182):
```xml
    <ClCompile Include="Views\StatusStripRules.cpp" />
```
Immediately after `<ClInclude Include="UrComponents.h" />` (line 194):
```xml
    <ClInclude Include="Views\StatusStripRules.h" />
```

- [ ] **Step 5: Add the two assertion builders to `Startup.cpp`'s anonymous namespace.**

Add `#include "Views/StatusStripRules.h"` to the include block at the top of `app/src/App/Startup.cpp` (after `#include "Strings.h"` on line 19). Then put these two functions **inside** the anonymous namespace that closes at `}  // namespace` on **line 160**, immediately below `WriteStdout` (which is lines 147–158, inside that namespace).

```cpp
// ---- the status strip's pure rules (design 6.5) -----------------------------
//
// This repo has no test project, so --diagnose IS the test runner: one line per
// invariant, reading PASS or FAIL and carrying the values it compared, so a
// failure says WHAT was wrong rather than only that something was.
//
// Both are PURE: they build no demo world, touch no XAML and name no hostname.
// That is what makes them safe on the path this function is on. wWinMain calls
// CollectDiagnostics() before winrt::init_apartment(), and LogDiagnostics()
// then writes every line into the log file of a NORMAL launch — so an assertion
// that needed the demo world would put fabricated data in a real user's log.
// The world's own invariants belong to the DemoWorld unit, not here.
//
// The field-name column is 19 characters wide, matching every line
// CollectDiagnostics() already writes.
std::wstring StatusStripCollapseAssertion() {
  const bool below = urmsg::views::ShouldShowStatusStrip(559.0);
  const bool at = urmsg::views::ShouldShowStatusStrip(560.0);
  const bool tall = urmsg::views::ShouldShowStatusStrip(900.0);
  if (!below && at && tall)
    return L"  strip collapse   : PASS (559 hidden, 560 shown, 900 shown)";
  return std::format(
      L"  strip collapse   : FAIL (559 -> {}, 560 -> {}, 900 -> {}; "
      L"expected hidden, shown, shown)",
      below ? L"shown" : L"hidden", at ? L"shown" : L"hidden",
      tall ? L"shown" : L"hidden");
}

std::wstring StatusStripFieldsAssertion() {
  const std::wstring_view offline =
      urmsg::views::StatusStateWord(urmsg::demo::ConnectState::Offline);
  const std::wstring_view connecting =
      urmsg::views::StatusStateWord(urmsg::demo::ConnectState::Connecting);
  const std::wstring_view connected =
      urmsg::views::StatusStateWord(urmsg::demo::ConnectState::Connected);
  const std::wstring lockOn = urmsg::views::StatusLockGlyph(true);
  const std::wstring lockOff = urmsg::views::StatusLockGlyph(false);
  const std::wstring epoch = urmsg::views::StatusEpochValue(41);
  const std::wstring records = urmsg::views::StatusRecordsValue(12);

  const bool wordsOk =
      offline == L"Offline" && connecting == L"Connecting" && connected == L"Connected";
  const bool glyphsOk = lockOn == L"\ue72e" && lockOff == L"\ue785";
  const bool valuesOk = epoch == L"41" && records == L"12";
  if (wordsOk && glyphsOk && valuesOk)
    return L"  strip fields     : PASS (3 words, lock e72e/e785, epoch 41, rec 12)";

  // The glyphs are reported as CODE UNITS, not as characters: a private-use
  // codepoint written to a console is an empty box, which is exactly the shape
  // of the defect being looked for.
  const auto code = [](std::wstring const& s) -> unsigned {
    return s.empty() ? 0u : static_cast<unsigned>(s[0]);
  };
  return std::format(
      L"  strip fields     : FAIL (words \"{}\"/\"{}\"/\"{}\", lock {:04x}/{:04x}, "
      L"epoch \"{}\", rec \"{}\")",
      offline, connecting, connected, code(lockOn), code(lockOff), epoch, records);
}
```

- [ ] **Step 6: Push the two lines into `CollectDiagnostics()`, and amend the function's own contract.**

At the end of `CollectDiagnostics()` in `Startup.cpp`, immediately before `return lines;` (after the `fonts` line):

```cpp
  // The surface rules. They run on EVERY launch, not only --diagnose:
  // LogDiagnostics() writes them to the log file, so a PASS/FAIL history exists
  // for a run nobody was watching. That is only acceptable because they are
  // pure — no demo world is constructed and no fabricated hostname is written.
  lines.push_back(StatusStripCollapseAssertion());
  lines.push_back(StatusStripFieldsAssertion());
```

The declaration in `Startup.h` (lines 40–44) currently promises only environment facts. Extend it so the file does not describe a function it no longer is — replace that comment block with:

```cpp
// One line per environment fact the look-alike startup failures are told apart
// by: App Runtime presence + path (the path carries the version), the
// bootstrap dll, resources.pri, the log file. Logged at startup and printed by
// --diagnose. Pure Win32 — safe to call before COM/WinRT is up.
//
// It also carries the app's PURE surface assertions (one PASS/FAIL line each),
// because this repo has no test project and --diagnose is the test runner. The
// bar for adding one here is the bar this function already sets: no COM, no
// WinRT activation, no Localized(), and nothing that fabricates data into the
// log of a normal launch. Anything needing the demo world is gated on the demo
// switch by its own caller instead.
std::vector<std::wstring> CollectDiagnostics();
```

- [ ] **Step 7: Build, then run `--diagnose` and SEE the two FAIL lines.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```
Run from `C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows`. Expected last line, green: `OK in <n>s -> C:\Users\ryanm\Downloads\claude_sandbox_message\message-windows\app\build\x64\Release\`.

```
app\build\x64\Release\URmessage.exe --diagnose
```

Expected, at the foot of the diagnostics block:
```
  strip collapse   : FAIL (559 -> hidden, 560 -> hidden, 900 -> hidden; expected hidden, shown, shown)
  strip fields     : FAIL (words "?"/"?"/"?", lock 003f/003f, epoch "?", rec "?")
```
`003f` is `?`. If either line already says `PASS`, the stub is not a stub — go back to step 3.

- [ ] **Step 8: Implement the rules.**

Replace all five stubs in `StatusStripRules.cpp`:

```cpp
bool ShouldShowStatusStrip(double windowHeightDip) {
  // Design 6.5: hidden BELOW 560, so 560 itself shows. At Spec C 1.2's 480 DIP
  // minimum the strip's 26 DIP would come out of the thread, which is the one
  // column that has to stay readable.
  return kStatusStripCollapseDip <= windowHeightDip;
}

std::wstring_view StatusStateWord(demo::ConnectState state) {
  switch (state) {
    case demo::ConnectState::Connected:
      // This must never be read as a claim that a real session exists: the demo
      // has no protocol (design 2). The DEMO watermark is what carries that,
      // and it is why this word is allowed to be the plain one.
      return L"Connected";
    case demo::ConnectState::Connecting:
      return L"Connecting";
    case demo::ConnectState::Offline:
      break;
  }
  return L"Offline";
}

std::wstring StatusLockGlyph(bool keyVerified) {
  // Segoe Fluent Icons, named explicitly wherever this app draws one (App.xaml's
  // UrIconFontFamily): FontIcon otherwise falls back to Segoe MDL2 Assets, whose
  // metrics differ.
  return keyVerified ? std::wstring{L"\ue72e"}   // Lock
                     : std::wstring{L"\ue785"};  // Unlock
}

std::wstring StatusEpochValue(uint64_t epoch) { return std::to_wstring(epoch); }

std::wstring StatusRecordsValue(int recordsPerSecond) {
  // No unit suffix: the field's caption carries "rec/s", the same division of
  // labour every other field of the strip uses.
  return std::to_wstring(recordsPerSecond);
}
```

- [ ] **Step 9: Build and run `--diagnose` again; see two PASS lines.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
app\build\x64\Release\URmessage.exe --diagnose
```

Expected, exactly:
```
  strip collapse   : PASS (559 hidden, 560 shown, 900 shown)
  strip fields     : PASS (3 words, lock e72e/e785, epoch 41, rec 12)
```
Both lines are exact — there is no `<N>` in either, because neither reads anything seeded.

- [ ] **Step 10: Commit.**

Count the tracked files first — this repo has a recorded failure mode where the git index vanishes on Windows and a commit silently truncates the tree.

```
git ls-files | Measure-Object -Line
git add app/src/App/Views/StatusStripRules.h app/src/App/Views/StatusStripRules.cpp app/src/App/App.vcxproj app/src/App/Startup.h app/src/App/Startup.cpp
git ls-files | Measure-Object -Line
```
Expected: the second count is the first **+ 2** (the two new files). If it is anything else, stop and re-check the index before committing.

```
git commit -m @'
status strip: the pure rules, asserted in --diagnose

The 560 DIP collapse rule, the three state words, the two lock glyphs and the
two Advanced Mode number formats. No winrt include and no Localized() call in
either file: CollectDiagnostics runs from wWinMain before init_apartment, and
a Localized() call from there would be the resource loader first use.

The assertions are pure on purpose. main.cpp logs every diagnostic line on a
normal launch, so an assertion that built the demo world would write a
fabricated message-server hostname into a real user log file. The world own
invariants stay with the world.

Startup.h says so, rather than leaving CollectDiagnostics describing itself as
environment facts only.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
```

**Deliverable:** `URmessage.exe --diagnose` prints two exact `PASS` lines for the status strip, and prints them into `<URMESSAGE_APP_ROOT>/logs/urmessage-app.log` on a normal launch too — with no demo data in either.

---

## Task S2: Status strip 2/4 — the 26 DIP strip, built only under `--demo`, collapsing at 560

**Files:**

Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/StatusStripView.h; Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/StatusStripView.cpp; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: **The verify-render `-AppArgs` task (F1)** — `app/tools/verify-render.ps1` must accept `-AppArgs` AND forward it to `Start-Process -ArgumentList`; five of this surface's six pixel gates die at PowerShell bind time without it. **The `--demo` 1560×900 launch size (design D7)**, owned by the wiring task. Status strip 1/4's `Views/StatusStripRules.h`. The **DemoWorld task**'s `Demo/DemoWorld.h` (`World`, `ServerInfo`, `ConnectState`, `GetWorld`). The **demo-switches task**'s `Demo/DemoSwitches.h` (`ParseDemoOptions`, `DemoOptions::enabled`). `urnw::kit::MakeStatusField` / `SetStatusFieldValue` / `MakeStatusSeparator` / `StatusField` (`UrComponents.h:231/250/261/265`). `urnw::motion::ShouldAnimate` / `MakeSplineDouble` / `kPulseMs` / `kStandardP1,P2` (`UrMotion.h`). `urnw::colors` (`UrColors.h`). App.xaml `UrStatusStripStyle` (:581), `UrPaneRowButtonStyle` (:891), `UrRowIconStyle` (:523). `MainWindow::ApplyBreakpoint` (`MainWindow.xaml.cpp:164`) and `breakpointApplied_`.
- Produces: namespace `urmsg::views`, in `app/src/App/Views/StatusStripView.h` — the fixed contract §4 block, verbatim:
```cpp
struct StatusStripView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Button strip{nullptr};
  winrt::Microsoft::UI::Xaml::FrameworkElement drawer{nullptr};
};
StatusStripView MakeStatusStrip(urmsg::demo::World const& world);
void SetStatusStripAdvanced(StatusStripView& v, bool advanced);   // no-op until 4/4
void SetStatusStripDrawerOpen(StatusStripView& v, bool open);     // no-op until 3/4
```
On `winrt::URmessage::implementation::MainWindow` (`MainWindow.xaml.h`, private):
```cpp
void BuildStatusStrip();
urmsg::views::StatusStripView statusStrip_{};
bool stripVisible_ = false;
urmsg::demo::DemoOptions demo_{};   // parsed ONCE in the constructor
```
XAML: `RevealRoot` gains a third `Auto` row; `<Grid x:Name="StatusStripHost" Grid.Row="2" />`.

# Status strip 2/4 — the 26 DIP strip, built only under `--demo`, collapsing at 560

Builds the Normal density and nothing else: **state dot, state word, server host, lock glyph**. The strip is a `Border` carrying `UrStatusStripStyle` with a `Button` on `UrPaneRowButtonStyle` inside it — the Border is the chrome, the Button is the hit target and the hover fill (3/4 gives it a Click handler).

**Two things to know before you start.**

1. `UrStatusStripStyle` (App.xaml:581) exists and is the right chrome — `#151515`, `BorderThickness 0,1,0,0` — but it carries `Padding="16,7"` and **no Height**. A 12sp value line inside 7+7 of padding measures ~30 DIP, not design §6.5's 26. So the style is applied and then two things are overridden locally, on purpose, with the reason written next to them.
2. The strip is **demo-only**. Design §8: without `--demo` "the app behaves exactly as it does today". A build with no protocol must not grow a bottom strip reading `Connected | server urmsg-01.ur.io | 🔒` on a plain double-click — that is design §2's hard constraint and §11's "overstating what exists". The gate is in `BuildStatusStrip()` and in `ApplyBreakpoint()`, and step 14 is the check that it holds.

---

- [ ] **Step 1: Gate A — confirm `verify-render.ps1` can forward arguments.**

Five of this surface's pixel gates run `verify-render.ps1 -AppArgs "..."`. The script is `[CmdletBinding()]`, so an undeclared parameter is a **terminating** bind error, and the version in `main` today also launches the exe with no arguments at all.

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/tools/verify-render.ps1' -Pattern 'AppArgs|ArgumentList'"
```

Expected: **at least two lines** — an `$AppArgs` parameter in the `param(...)` block, and an `ArgumentList` on the `Start-Process` call. Both halves matter: a parameter that is accepted and never forwarded is the same failure one step later.

If either is missing, **STOP**. The passthrough is task F1's and is a hard prerequisite (design §9.3). Do not add it here and do not work around it by launching the exe by hand — the harness's PerMonitorV2 DPI awareness, `EnumWindows`+`GetClassNameW` window lookup and screen capture are the reasons its measurements can be trusted.

- [ ] **Step 2: Gate B — confirm the demo switches landed.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoSwitches.h' -Pattern 'struct DemoOptions|bool enabled|DemoOptions ParseDemoOptions'"
```

Expected: three lines — `struct DemoOptions`, `bool enabled;`, `DemoOptions ParseDemoOptions();`.

If missing, **STOP**: without `DemoOptions::enabled` there is no way to keep this surface out of a normal launch, and building it ungated is the one outcome this task must not produce.

- [ ] **Step 3: Create `app/src/App/Views/StatusStripView.h`.**

The API below is the fixed contract's, verbatim. Do not add a member and do not change a signature.

```cpp
// The connect indicator (design 6.5, D4).
//
// A 26 DIP strip along the bottom of the WINDOW: state dot, state word, server
// host, lock glyph -- and NOTHING else in Normal mode. The owner ruled this a
// connect INDICATOR, not a network readout, so every other fact the app knows
// lives on the Network page (6.4) or behind Advanced Mode (6.6).
//
// A struct of named elements plus free Make*/Set* functions: the UrComponents.h
// grain (see PaneListRowButton + SetPaneListRowSelected). No classes with
// virtuals, no MVVM, no observable types, no IDL.
//
// The strip carries no model of its own. `world` is read ONCE, in
// MakeStatusStrip: the demo's connect state, host and epoch are seeded data
// that nothing mutates -- ambient activity (9.2) only appends message rows --
// so there is no Set*Model and nothing to keep in sync afterwards.
//
// The pure half of this surface (the collapse threshold, the state words, the
// glyphs, the number formats) is Views/StatusStripRules.h, which has no winrt
// include so that --diagnose can assert it before the apartment exists.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

struct StatusStripView {
  // the 26 DIP surface: UrStatusStripStyle's #151515 and its top hairline
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  // the whole strip is the activator (design 6.5); 3/4 hangs Click on it
  winrt::Microsoft::UI::Xaml::Controls::Button strip{nullptr};
  // the preview drawer, hosted by MainWindow in the row ABOVE root so it draws
  // over the destination instead of pushing the strip down (3/4)
  winrt::Microsoft::UI::Xaml::FrameworkElement drawer{nullptr};
};

StatusStripView MakeStatusStrip(urmsg::demo::World const& world);

// DENSITY ONLY. It shows and hides the three Advanced fields MakeStatusStrip
// already built; it never rebuilds the strip and never runs a mode crossfade --
// re-running a builder to change density is what blanks a visible scroller to
// opacity 0 and fades it back over itself. Implemented in Status strip 4/4.
void SetStatusStripAdvanced(StatusStripView& v, bool advanced);

// Raise or dismiss the preview drawer. Implemented in Status strip 3/4.
void SetStatusStripDrawerOpen(StatusStripView& v, bool open);

}  // namespace urmsg::views
```

- [ ] **Step 4: Create `app/src/App/Views/StatusStripView.cpp` — includes and file-local helpers.**

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/StatusStripView.h"

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include "UrColors.h"
#include "UrComponents.h"
#include "UrMotion.h"
#include "Views/StatusStripRules.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace urmsg::views {
namespace {

namespace kit = urnw::kit;
namespace shapes = winrt::Microsoft::UI::Xaml::Shapes;

// Demo copy is English string literals, NOT localization keys.
// Strings/en/Resources.resw is GENERATED from urnetwork/localizations
// (Localization.h) and this work adds no key to it; localization is out of
// scope (design 2, 9.4).
constexpr wchar_t kServerCaption[] = L"server";
constexpr wchar_t kStateName[] = L"Connection";
// The strip's accessible name. A Button whose Content is a Panel gets NO
// automatic name -- this project has paid for that twice (UrComponents.h on
// PaneListRowButton) -- and the name has to say what activating it DOES,
// because the drawer it raises is not visible until it does.
//
// app/tools/verify-drawer.ps1 (3/4) finds the button by exactly this string, so
// it is also the one machine-checked accessibility fact on this surface.
constexpr wchar_t kStripName[] = L"Connection status. Activate to show the relay path.";
constexpr wchar_t kKeyVerifiedName[] = L"Server key verified";
constexpr wchar_t kKeyUnverifiedName[] = L"Server key not verified";

// A style out of the app dictionary, by key, or null if it is missing. Applying
// styles by key rather than by hand is what keeps this view in step with
// App.xaml, and a missing key must not throw a layout away.
//
// UrComponents.cpp carries an identical copy in its own anonymous namespace
// (UrComponents.cpp:79). Duplicating six lines here beats editing the shared
// UrComponents.h while six other surface tasks are in flight against it;
// promoting it is a follow-up, not this task's change.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

// The dot's fill. Spends the connect-status ramp UrColors.h already carries for
// exactly this dot. NOT kProGold (the Pro entitlement's, and reachable from
// nowhere on this surface) and NOT kAccent: UrColors.h records that the
// connecting dot is Yellow400 #E6EA23 from android's circle_indicator_yellow,
// and that the pale kAccent is a different ramp step.
//
// The colour is never the only carrier here -- StatusStateWord sits 6 DIP to
// its right and says the same thing in words.
winrt::Windows::UI::Color StatusStateColor(demo::ConnectState state) {
  switch (state) {
    case demo::ConnectState::Connected:
      return urnw::colors::kUrGreen;
    case demo::ConnectState::Connecting:
      return urnw::colors::kStatusConnecting;
    case demo::ConnectState::Offline:
      break;
  }
  return urnw::colors::kTextFaint;
}

}  // namespace
}  // namespace urmsg::views
```

- [ ] **Step 5: Add the connect dot and its pulse, in the same anonymous namespace.**

Insert both above the closing `}  // namespace`.

```cpp
// A 20x20 host holding an 8x8 dot with a same-size ring BEHIND it (a Grid, so
// the ring is drawn first and the dot sits on top).
//
// The arithmetic, stated so the next reader can check it rather than trust it:
// the ring is 8 DIP and scales to 2.0, i.e. 16 DIP at its peak, inside a 20 DIP
// host -- 2 DIP of clear space on every side. It must not reach the state word,
// which begins at the host's right edge.
//
// The host is 20 rather than 8 for a second reason: the 8 DIP dot is centred in
// it, which leaves exactly 6 DIP to its right, and 6 is the dot-to-value gap
// kit::MakeStatusField uses inside every other field of the strip
// (row.Spacing(6), UrComponents.cpp). So the state field takes NO left margin;
// the host's own slack already supplies the house spacing.
//
// The ring starts invisible, so a strip that never pulses (offline, or motion
// off) shows exactly the 8 DIP dot and nothing else.
Controls::Grid MakeConnectDot(shapes::Ellipse& dot, shapes::Ellipse& ring) {
  Controls::Grid host;
  host.Width(20);
  host.Height(20);
  host.VerticalAlignment(VerticalAlignment::Center);

  ring = shapes::Ellipse();
  ring.Width(8);
  ring.Height(8);
  ring.HorizontalAlignment(HorizontalAlignment::Center);
  ring.VerticalAlignment(VerticalAlignment::Center);
  ring.Opacity(0.0);
  // CenterX/Y = half of 8, or the ring grows to the right and down instead of
  // out of the dot.
  Media::ScaleTransform scale;
  scale.CenterX(4);
  scale.CenterY(4);
  ring.RenderTransform(scale);
  host.Children().Append(ring);

  dot = shapes::Ellipse();
  dot.Width(8);
  dot.Height(8);
  dot.HorizontalAlignment(HorizontalAlignment::Center);
  dot.VerticalAlignment(VerticalAlignment::Center);
  host.Children().Append(dot);

  // The colour IS the information and the word beside it says the same thing,
  // so both shapes are decoration to a screen reader -- the same treatment, for
  // the same reason, kit::MakeStatusField gives its own dot.
  Automation::AutomationProperties::SetAccessibilityView(
      host, Automation::Peers::AccessibilityView::Raw);
  return host;
}

// Design 7: the connect dot pulses at kPulseMs -- an expanding ring at
// decreasing alpha. No new duration and no new curve: kPulseMs is UrMotion.h's
// "idle invitation burst" and kStandardP1/P2 is its default ease.
//
// Started once and never stopped, and therefore no Storyboard is held. The
// demo's connect state is seeded data that nothing mutates (ambient activity,
// 9.2, only appends message rows), so there is no state change to restart it on.
void StartStatusDotPulse(shapes::Ellipse const& ring, demo::ConnectState state) {
  namespace anim = Media::Animation;
  if (!ring) return;
  // Only the connected state breathes. A pulsing OFFLINE dot says the app is
  // doing something, which is the one thing it is not.
  if (state != demo::ConnectState::Connected) return;
  // Motion is GONE, not reduced, when the OS says so (UrMotion.h). The strip has
  // to be correct and instant in that state, which here means a plain 8 DIP dot.
  if (!urnw::motion::ShouldAnimate()) return;

  anim::Storyboard sb;
  auto grow = [&sb, &ring](wchar_t const* path) {
    auto track = urnw::motion::MakeSplineDouble(1.0, 2.0, urnw::motion::kPulseMs, 0,
                                                urnw::motion::kStandardP1,
                                                urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(track, ring);
    anim::Storyboard::SetTargetProperty(track, path);
    sb.Children().Append(track);
  };
  grow(L"(UIElement.RenderTransform).(ScaleTransform.ScaleX)");
  grow(L"(UIElement.RenderTransform).(ScaleTransform.ScaleY)");

  // 0.55 -> 0: the ring is a halo, not a second dot. It must never be bright
  // enough to be read as its own state.
  auto fade = urnw::motion::MakeSplineDouble(0.55, 0.0, urnw::motion::kPulseMs, 0,
                                             urnw::motion::kStandardP1,
                                             urnw::motion::kStandardP2);
  anim::Storyboard::SetTarget(fade, ring);
  anim::Storyboard::SetTargetProperty(fade, L"Opacity");
  sb.Children().Append(fade);

  sb.RepeatBehavior(anim::RepeatBehaviorHelper::Forever());
  sb.Begin();
}
```

Opacity and RenderTransform are the two independent (composition-driven) property families, so neither needs `EnableDependentAnimation`. **If no halo ever appears in step 13's screenshot, the property path did not resolve** — Storyboard silently ignores an unresolvable one. Check that `ring.RenderTransform(scale)` runs before `sb.Begin()`.

- [ ] **Step 6: Implement `MakeStatusStrip()`.**

Add below the anonymous namespace, inside `namespace urmsg::views`.

```cpp
StatusStripView MakeStatusStrip(urmsg::demo::World const& world) {
  StatusStripView v;

  // The surface. UrStatusStripStyle (App.xaml:581) already IS this chrome --
  // #151515 with one hairline along its TOP edge -- so it is applied, not
  // re-spelled. Two local overrides on top of it, both deliberate:
  //   Height 26   design 6.5's figure. The style carries no height.
  //   Padding 0   the style's "16,7" needs ~30 DIP for a 12sp line and would
  //               clip at 26. The 16 DIP inset moves onto the Button below, so
  //               the hover fill spans the WHOLE strip instead of stopping at
  //               the text.
  Controls::Border root;
  if (auto style = StyleByKey(L"UrStatusStripStyle")) root.Style(style);
  root.Height(kStatusStripHeightDip);
  root.Padding(ThicknessHelper::FromUniformLength(0));
  root.VerticalAlignment(VerticalAlignment::Bottom);

  // The whole strip is the activator (design 6.5). UrPaneRowButtonStyle brings
  // the hover / pressed fill and the focus visual; its 40 MinHeight, its bottom
  // hairline and its 12 padding are a pane row's and are overridden here.
  //
  // No Height and no MinHeight of its own. A Border's Height is its TOTAL layout
  // height, so this Border's 26 includes its own 1 DIP top hairline and its
  // content box is 25; a Button pinned to 26 would overflow its parent by a
  // pixel. Stretch fills whatever the content box turns out to be.
  //
  // Background is NOT set -- the style's Transparent is what lets the Border's
  // #151515 show through, and the template's PointerOver setter paints
  // UrCardBrush over it.
  v.strip = Controls::Button();
  if (auto style = StyleByKey(L"UrPaneRowButtonStyle")) v.strip.Style(style);
  v.strip.BorderThickness(ThicknessHelper::FromUniformLength(0));
  v.strip.MinHeight(0);
  v.strip.VerticalAlignment(VerticalAlignment::Stretch);
  v.strip.Padding(ThicknessHelper::FromLengths(16, 0, 16, 0));
  v.strip.HorizontalContentAlignment(HorizontalAlignment::Left);
  Automation::AutomationProperties::SetName(v.strip, winrt::hstring{kStripName});

  Controls::StackPanel fields;
  fields.Orientation(Controls::Orientation::Horizontal);
  // 0, not a Spacing: MakeStatusSeparator already carries its own 14/14 margins
  // (UrComponents.cpp), and a panel Spacing would add to it at every rule.
  fields.Spacing(0);
  fields.VerticalAlignment(VerticalAlignment::Center);

  shapes::Ellipse dot{nullptr};
  shapes::Ellipse ring{nullptr};
  auto dotHost = MakeConnectDot(dot, ring);
  const auto stateBrush = urnw::colors::MakeBrush(StatusStateColor(world.connectState));
  dot.Fill(stateBrush);
  ring.Fill(stateBrush);
  fields.Children().Append(dotHost);

  // withDot=false: the dot above IS this field's, drawn one element to the left
  // so it can carry the ring. Empty label + accessibleName is the shape
  // MakeStatusField documents for a field whose value speaks for itself -- a
  // coloured dot and the word "Connected" need no caption saying "Status" -- and
  // SetStatusFieldValue then announces it as "Connection, Connected".
  //
  // No Margin: see MakeConnectDot's note. The 20 DIP host already leaves the
  // kit's 6 DIP gap to the right of the dot.
  auto state = kit::MakeStatusField(winrt::hstring{L""}, /*withDot=*/false,
                                    winrt::hstring{kStateName});
  kit::SetStatusFieldValue(state,
                           winrt::hstring{StatusStateWord(world.connectState)});
  fields.Children().Append(state.root);

  fields.Children().Append(kit::MakeStatusSeparator());

  auto host = kit::MakeStatusField(winrt::hstring{kServerCaption});
  kit::SetStatusFieldValue(host, winrt::hstring{world.server.host});
  fields.Children().Append(host.root);
  // Deliberately NOT world.server.jurisdiction or latencyMs. The owner ruled
  // this a connect indicator, not a network readout (design D4); those two are
  // the Network page's (6.4) and the drawer's.

  fields.Children().Append(kit::MakeStatusSeparator());

  // 14, one step under UrRowIconStyle's 16: the strip must never compete with
  // the page, which is the same reason its label/value styles are smaller than
  // UrStatLabel/UrStatValue.
  Controls::FontIcon lock;
  if (auto style = StyleByKey(L"UrRowIconStyle")) lock.Style(style);
  lock.FontSize(14);
  lock.Glyph(winrt::hstring{StatusLockGlyph(world.server.keyVerified)});
  lock.Foreground(world.server.keyVerified ? urnw::colors::MutedBrush()
                                           : urnw::colors::DangerBrush());
  // UrRowIconStyle marks its glyph Raw, because a leading mark normally sits
  // beside a label that already says the same word. This one does not: it is the
  // ONLY statement of key verification anywhere on the strip, so it is content,
  // and it is named. The glyph also changes SHAPE between the two states
  // (e72e closed padlock / e785 open padlock), so the fact never rides on colour.
  Automation::AutomationProperties::SetAccessibilityView(
      lock, Automation::Peers::AccessibilityView::Content);
  Automation::AutomationProperties::SetName(
      lock, winrt::hstring{world.server.keyVerified ? kKeyVerifiedName
                                                    : kKeyUnverifiedName});
  fields.Children().Append(lock);

  v.strip.Content(fields);
  root.Child(v.strip);
  v.root = root;

  StartStatusDotPulse(ring, world.connectState);
  return v;
}

// Implemented in Status strip 4/4. Defined now so the header can carry the whole
// fixed contract from its first commit rather than growing into it.
void SetStatusStripAdvanced(StatusStripView&, bool) {}

// Implemented in Status strip 3/4.
void SetStatusStripDrawerOpen(StatusStripView&, bool) {}
```

- [ ] **Step 7: Register both files in `app/src/App/App.vcxproj`.**

Immediately after `<ClCompile Include="Views\StatusStripRules.cpp" />`:
```xml
    <ClCompile Include="Views\StatusStripView.cpp" />
```
Immediately after `<ClInclude Include="Views\StatusStripRules.h" />`:
```xml
    <ClInclude Include="Views\StatusStripView.h" />
```

- [ ] **Step 8: Add the host row to `MainWindow.xaml`.**

In `<Grid x:Name="RevealRoot">` (line 37), add a third row definition:
```xml
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto" />
            <RowDefinition Height="*" />
            <RowDefinition Height="Auto" />
        </Grid.RowDefinitions>
```

and, immediately before the closing `</Grid>` of `RevealRoot` (after `</muxc:NavigationView>` on line 201):
```xml
        <!-- The connect indicator (design 6.5). A 26 DIP strip along the bottom
             of the WINDOW, under the nav pane as well as the destination: it is
             window chrome, like the title bar, not part of any one page. Built
             by MainWindow::BuildStatusStrip, and ONLY under --demo; shown and
             hidden by ApplyBreakpoint, which is the one place in this app that
             decides layout from window size. Collapsed here because the markup
             default must not be what makes the window correct -- ApplyBreakpoint
             writes this on its first pass either way. -->
        <Grid x:Name="StatusStripHost" Grid.Row="2" Visibility="Collapsed" />
```

- [ ] **Step 9: Hold the strip in `MainWindow.xaml.h`.**

First decide whether `demo_` already exists — another surface may have landed it:

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h' -Pattern 'demo_'"
```

If it prints a line, **skip the `demo_` member below and skip the `demo_ =` line in step 10** — the window already parses the switches once and this surface reads that copy. If it prints nothing, add everything.

Add beside the existing `#include "UrComponents.h"` (line 21):
```cpp
#include "Demo/DemoSwitches.h"
#include "Views/StatusStripView.h"
```

Add to the private section, after `void BuildConversationList();` (line 48):
```cpp
  // The connect indicator (design 6.5). Built ONCE and never rebuilt: the strip
  // is window chrome, so it outlives every destination change. Returns without
  // building anything when the demo is off -- design 8: "Without it the app
  // behaves exactly as it does today", and a build with no protocol must not
  // show a message-server hostname as chrome on every launch (design 2, 11).
  void BuildStatusStrip();
```

and to the member block, after `urnw::kit::PaneSearchRow search_{};` (line 60):
```cpp
  urmsg::views::StatusStripView statusStrip_{};
  // Whether the strip is currently drawn. Part of ApplyBreakpoint's early-out
  // for the same reason breakpointApplied_ is: the early-out has to test every
  // state the function applies, or a resize that changes only the HEIGHT is
  // silently dropped.
  bool stripVisible_ = false;
  // The launch switches, parsed ONCE. ParseDemoOptions() reads the process
  // command line, so calling it per surface is the same answer computed seven
  // times; every surface reads this copy.
  urmsg::demo::DemoOptions demo_{};
```

- [ ] **Step 10: Build the strip in `MainWindow.xaml.cpp`.**

Add to the include block (after `#include "Log.h"`, line 16):
```cpp
#include "Demo/DemoWorld.h"
#include "Views/StatusStripRules.h"
```

In the constructor, add **before** `BuildConversationList();` (line 77) — omit this line if step 9's grep found `demo_`:
```cpp
  demo_ = urmsg::demo::ParseDemoOptions();
```
and **after** `BuildConversationList();`:
```cpp
  BuildStatusStrip();
```

Add the function after `BuildConversationList()` (i.e. after line 162):
```cpp
void MainWindow::BuildStatusStrip() {
  // The whole surface is demo-only. Design 8: without --demo "the app behaves
  // exactly as it does today", and design 2's hard constraint is that nothing
  // may overstate what exists -- a strip reading "Connected | server
  // urmsg-01.ur.io" on a plain double-click of a build with no protocol would
  // do exactly that. This early return is also why demo::GetWorld() is never
  // constructed on a normal launch.
  if (!demo_.enabled) {
    urnw::LogInfo("window: status strip not built (demo off)");
    return;
  }
  statusStrip_ = urmsg::views::MakeStatusStrip(urmsg::demo::GetWorld());
  StatusStripHost().Children().Clear();
  StatusStripHost().Children().Append(statusStrip_.root);
  urnw::LogInfo("window: status strip built");
}
```

- [ ] **Step 11: Extend `ApplyBreakpoint()` with the collapse rule.**

In `MainWindow::ApplyBreakpoint()` (line 164), after `if (width <= 0) return;`:
```cpp
  const double height = root.ActualHeight();
```
Change the state block (lines 170–178) to:
```cpp
  const bool wide = urnw::kit::kWideBreakpointDip <= width;
  // Design 6.5's collapse rule, decided in the SAME function that decides what
  // "wide" means: there is exactly one place in this app where window size
  // becomes layout. The demo gate is ANDed in HERE rather than left to the
  // host's markup default, so the strip's visibility has one owner and not two.
  const bool strip = demo_.enabled && urmsg::views::ShouldShowStatusStrip(height);
  if (breakpointApplied_ && wide == wide_ && strip == stripVisible_) return;
  wide_ = wide;
  stripVisible_ = strip;
  breakpointApplied_ = true;
```
and at the end of the function, after the existing `LogInfo` (line 191):
```cpp
  StatusStripHost().Visibility(strip ? Visibility::Visible : Visibility::Collapsed);
  // Both reasons a strip can be absent are named, because "hidden (760 dip)"
  // alone would read as the collapse rule firing at a height where it does not.
  urnw::LogInfo("window: status strip -> {} ({:.0f} dip high, demo {})",
                strip ? "shown" : "hidden", height, demo_.enabled ? "on" : "off");
```

- [ ] **Step 12: Build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```
Expected: `OK in <n>s -> ...\app\build\x64\Release\`.

- [ ] **Step 13: Launch deep-linked and LOOK at the pixels.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

**Read the harness's own measurement first.** It prints a `dips        : WxH` line. Expected `dips        : 1560x900` — the `--demo` launch size (design D7).
- If it prints `480x760`, `--demo` did not reach the window sizing: D7 is the wiring task's and has not landed. **STOP** and report; every expectation below is written against 1560×900.
- If it prints some third size, a saved placement was restored. `WindowShell.cpp` keeps placement in **HKCU (the registry), not in `.localstate-verify`**, so clearing the worktree state does not clear it; note the size the harness actually printed and read the expectations against that width.

Then read `C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.verify/urmessage-window-screen.png` (the CopyFromScreen capture, taken at the launch size).

Expected, at 1560×900 DIP:
- A strip **26 DIP tall** pinned to the very bottom edge of the window, spanning its **full width** — under the NavigationView pane as well as the destination — one step lighter than the page (`#151515` on `#101010`), with a 1 px hairline along its **top** edge only.
- Inside it, left to right, the first mark **16 DIP** from the window's left edge: an **8 DIP green dot** (`#87FB67`); **6 DIP** later the word **`Connected`**; a 1 px vertical hairline; the faint 11sp caption **`server`** then **`urmsg-01.ur.io`**; a second vertical hairline; a **closed padlock**.
- **Exactly 4 marks and 2 hairlines. Nothing else** — no epoch, no session mode, no rec/s, no latency, no jurisdiction. Those are Advanced Mode (4/4) and the Network page.
- A faint green halo may surround the dot: that is the pulse ring caught mid-cycle and is expected. It must **not reach the word `Connected`** — the ring peaks at 16 DIP inside a 20 DIP host, so 2 DIP of clear space stands between them.
- The conversation list's scroll area **ends 26 DIP above the window's bottom edge**; the strip is not drawn over the last row.

Troubleshooting, in order of likelihood: if the text is clipped top and bottom, either step 6's `Padding(0)` did not take (the style's `16,7` is still winning) **or** the Button kept a `Height`/`MinHeight` and overflowed the Border's 25 DIP content box — check both. If the dot is grey and the word reads `Offline`, the strip is correct and `world.connectState` is what is seeded; that is a DemoWorld question, not a defect here.

- [ ] **Step 14: Launch with NO switches and prove the strip is absent.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1
```

Expected: the harness prints `dips        : 480x760` — the repo's real default (`WindowShell.h:21-25`).

In `.verify/urmessage-window-screen.png`: **no strip.** The conversation list runs to the window's bottom edge, and there is no `#151515` band and no padlock anywhere in the image.

Then:
```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'status strip' | Select-Object -Last 2"
```
Expected exactly these two lines, in this order:
```
window: status strip not built (demo off)
window: status strip -> hidden (760 dip high, demo off)
```
760 is **above** the 560 collapse threshold: what hides the strip here is the demo gate, not the collapse rule, and the log line says which. If the second line reads `shown`, `ApplyBreakpoint` is missing its `demo_.enabled &&` term and a normal launch is displaying a fabricated hostname.

- [ ] **Step 15: Prove the collapse branch with the app's own measurement.**

`verify-render.ps1` has no window-height control, so the hidden branch is checked by the log line step 11 writes, after resizing the window it leaves running.

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
powershell -ExecutionPolicy Bypass -Command "Add-Type -Name W -Namespace S -MemberDefinition '[DllImport(\"user32.dll\")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);'; $exe=(Resolve-Path 'app\build\x64\Release\URmessage.exe').Path; $p=Get-Process | Where-Object { try { $_.MainModule.FileName -eq $exe } catch { $false } }; [void][S.W]::SetWindowPos($p.MainWindowHandle,[IntPtr]::Zero,100,100,1200,500,0x14)"
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'status strip ->' | Select-Object -Last 2"
```
(The process is selected by **executable path**, never by name — `verify-render.ps1`'s own rule 3: names are identical across worktrees.)

Expected exactly two lines for this run:
```
window: status strip -> shown (900 dip high, demo on)
window: status strip -> hidden (NNN dip high, demo on)
```
The first line's height must equal the height the harness printed in step 13. `NNN` must be **below 560** — 500 physical pixels is under 560 DIP at every scale factor, and the window's own 480 DIP minimum keeps the clamped result under the threshold too. The harness's own mid-run resize to 1200×800 dip produces no third line, because 800 is still above 560 and 1200 is still wide: the early-out is doing its job.

- [ ] **Step 16: Commit.**

```
git ls-files | Measure-Object -Line
git add app/src/App/Views/StatusStripView.h app/src/App/Views/StatusStripView.cpp app/src/App/App.vcxproj app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git ls-files | Measure-Object -Line
```
Expected: the second count is the first **+ 2**. Then:
```
git commit -m @'
status strip: the 26 DIP connect indicator, demo-only and collapsing

State dot, state word, server host, lock glyph, and nothing else -- design
6.5 connect indicator, not a network readout.

Built ONLY under --demo. Design 8 says the app without the switch behaves
exactly as it does today, so BuildStatusStrip returns early when the demo is
off and ApplyBreakpoint ANDs the same gate into its visibility rule. A build
with no protocol must not show a message-server hostname as window chrome, and
GetWorld() is never constructed on a normal launch.

UrStatusStripStyle carries the chrome and UrPaneRowButtonStyle the hit target
and hover fill. Height 26 and Padding 0 are local overrides because the style
has no height and its 16,7 padding needs ~30 DIP; the Button takes neither, so
it fills the Border 25 DIP content box instead of overflowing it by the top
hairline.

The pulse ring is the one element UrComponents does not already have:
MakeStatusField puts its dot in a StackPanel, so there is nowhere behind it to
draw a ring. 8 DIP scaling to 2.0 = 16 DIP peak inside a 20 DIP host, kPulseMs,
standard curve, gated on ShouldAnimate() and on the connected state.

ApplyBreakpoint gains the 560 DIP collapse rule and stripVisible_ in its
early-out, so a resize that changes only the height is not dropped.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
```

**Deliverable:** three checks that do not overlap — a screenshot at 1560×900 showing the four-mark strip along the bottom, a screenshot at 480×760 with no strip at all and a log line saying why, and a log line proving the strip hides below 560 DIP.

---

## Task S3: Status strip 3/4 — the preview drawer the strip raises

**Files:**

Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/StatusStripView.cpp; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp; Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/tools/verify-drawer.ps1

**Interfaces:**

- Consumes: Status strip 2/4's `StatusStripView`, `MakeStatusStrip`, `SetStatusStripDrawerOpen` (the no-op this task replaces), `MainWindow::BuildStatusStrip`, `statusStrip_`, `demo_`, and the strip Button's accessible name `"Connection status. Activate to show the relay path."`. The **verify-render `-AppArgs` task (F1)**. The **DemoWorld task**'s `demo::RelayNode` and `World::relayPath` (fixed contract §1) plus its `--diagnose` invariant 7 (`relayPath.size() == 3`, no empty glyph). `urnw::kit::MakePaneGroupHeader` (`UrComponents.h:362`), `MakePaneListRow` (`UrComponents.h:304`). `urnw::motion::CrossfadePageSwap` / `ShouldAnimate` / `MakeSplineDouble` / `kFastMs` / `kExitP1,P2` (`UrMotion.h`). `urnw::colors::SheetBrush` / `BorderBrush` / `MakeBrush` / `kUrGreen` / `kTextFaint`.
- Produces: `SetStatusStripDrawerOpen(StatusStripView& v, bool open)` implemented (signature unchanged from the fixed contract); `MakeStatusStrip` now also populates `v.drawer`.
On `MainWindow` (`MainWindow.xaml.h`, private): `void ToggleStatusDrawer();`
XAML: `<Grid x:Name="StatusDrawerHost" Grid.Row="1" VerticalAlignment="Bottom" HorizontalAlignment="Left" />`
New tool: `app/tools/verify-drawer.ps1 -Name <accessible name> [-Out <png>] [-SettleMs <n>]` — UIA-invokes one named element of the already-running window and captures it.

# Status strip 3/4 — the preview drawer the strip raises

Design §6.5: *"Activating it raises a preview drawer with the node list."* Clicking the strip raises it; clicking again dismisses it. It lists the three relay nodes — this device → URnetwork → message server — and nothing more; the full map, the per-hop timings and the device list are the Network page's (§6.4).

**No `Popup` and no `Flyout`.** The drawer is a plain `Border` in the window's own tree, in the NavigationView's row and declared after it, so it draws over the destination and stands on the strip's top edge. A `Popup` is a second element tree that `verify-render.ps1`'s `PrintWindow` capture does not composite, and this surface exists to be screenshotted.

**How the open state gets verified.** There is no `--demo-drawer` switch — the fixed contract's `DemoOptions` has none — and this plan may not synthesise mouse or keyboard input. So step 8 builds `app/tools/verify-drawer.ps1`, which invokes the strip through **UI Automation's `InvokePattern`**. That is not input synthesis: it sends nothing to any input queue, it calls the control's own automation peer, which is what a screen reader does. It also turns the strip's accessible name into a machine-checked fact — an unnamed Button simply cannot be found by it.

---

- [ ] **Step 1: Gate — confirm the relay path landed, and that the world asserts it.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoWorld.h' -Pattern 'struct RelayNode|relayPath|subLabel|bool healthy'"
app\build\x64\Release\URmessage.exe --diagnose
```

Expected from the grep: `struct RelayNode`, `std::wstring subLabel;`, `bool healthy;`, and `std::vector<RelayNode> relayPath;` on `World`.
Expected from `--diagnose`: the DemoWorld task's own line asserting **exactly 3** relay nodes with no empty glyph (fixed contract §1.1, invariant 7), reading `PASS`.

If `RelayNode` is missing, **STOP**. Do not define it here: the Network page draws the same three nodes, and a second definition under `Views/` is how the two surfaces drift. If invariant 7 is missing or `FAIL`s, the drawer's input is unproven — report that rather than adding the assertion here; it is the world's invariant, not the view's.

- [ ] **Step 2: Add the drawer host to `MainWindow.xaml`.**

Immediately after `</muxc:NavigationView>` and **before** the `StatusStripHost` Grid added in 2/4:
```xml
        <!-- The strip's preview drawer (design 6.5). Grid.Row="1" -- the SAME
             row as the NavigationView, declared after it, so it draws OVER the
             destination rather than displacing it; bottom-aligned so it stands
             on the strip's top edge. Left-aligned, because it belongs to the
             connect dot at the strip's left end, not to the window. -->
        <Grid x:Name="StatusDrawerHost" Grid.Row="1"
              VerticalAlignment="Bottom" HorizontalAlignment="Left" />
```

- [ ] **Step 3: Add the drawer builder to `StatusStripView.cpp`'s anonymous namespace.**

Add `#include <vector>` to the file's includes. Add the string beside the other literals:
```cpp
constexpr wchar_t kDrawerName[] = L"Relay path preview";
constexpr wchar_t kRelayHeader[] = L"RELAY PATH";
```

Then, above the closing `}  // namespace`:
```cpp
// The preview drawer the strip raises (design 6.5): the relay node LIST only.
// The relay MAP -- three nodes and two wires, per-hop timings, the linked-device
// list -- is the Network page's (6.4).
//
// A plain Border in the window's own tree, not a Popup and not a Flyout: a Popup
// is a second element tree and verify-render.ps1's PrintWindow capture does not
// composite one. This surface exists to be screenshotted.
Controls::Border MakeStatusDrawer(std::vector<demo::RelayNode> const& path) {
  Controls::Border drawer;
  // 320 -- the pane rail width this app already uses (ApplyBreakpoint gives the
  // conversation list exactly 320 at or above kWideBreakpointDip), so the drawer
  // is one of the window's two established column widths rather than a third
  // measurement. It is NOT aligned with that column: the drawer belongs to the
  // connect dot at the strip's left end, and the strip runs under the
  // NavigationView pane, so its left edge is the STRIP's 16 DIP inset.
  drawer.Width(320);
  // Sheets sit ABOVE the page, never flush with it (UrColors.h): #151515 over
  // the #101010 destination. No radius, no shadow, no margin -- this window is
  // the PANE vocabulary (MainWindow.xaml's header block), and a rounded card
  // floating over a pane is the card model it deliberately does not use.
  drawer.Background(urnw::colors::SheetBrush());
  drawer.BorderBrush(urnw::colors::BorderBrush());
  // Three edges. The fourth is the strip's own top hairline, immediately below.
  drawer.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 0));
  drawer.HorizontalAlignment(HorizontalAlignment::Left);
  drawer.VerticalAlignment(VerticalAlignment::Bottom);
  // 16: the strip's own content inset, so the drawer's left edge lines up with
  // the connect dot that raised it.
  drawer.Margin(ThicknessHelper::FromLengths(16, 0, 0, 0));
  drawer.Visibility(Visibility::Collapsed);
  // MakePaneGroupHeader announces its title as a level-3 heading
  // (UrComponents.cpp's SetHeadingLevel), so the group is reachable by heading
  // navigation. The drawer's own container still needs a name: a heading INSIDE
  // a container does not name the container.
  Automation::AutomationProperties::SetName(drawer, winrt::hstring{kDrawerName});

  Controls::StackPanel nodes;

  auto header = kit::MakePaneGroupHeader(
      winrt::hstring{kRelayHeader},
      winrt::to_hstring(static_cast<int>(path.size())));
  nodes.Children().Append(header.root);

  // Every node is one MakePaneListRow, which is what stops the list acquiring a
  // 52px row the day someone gives one of them a second line.
  //
  // RelayNode::glyph is deliberately NOT drawn here. MakePaneListRow's row is a
  // dot, a title and a right-aligned meta, with no icon slot, and inventing a
  // fourth row species for three rows is exactly the drift "one row species per
  // pane layout" exists to stop. The glyphs are the Network page's, where 6.4
  // draws the path as nodes and wires rather than as a list.
  for (size_t i = 0; i < path.size(); ++i) {
    auto const& node = path[i];
    auto row = kit::MakePaneListRow();
    row.dot.Fill(urnw::colors::MakeBrush(node.healthy ? urnw::colors::kUrGreen
                                                      : urnw::colors::kTextFaint));
    row.title.Text(winrt::hstring{node.label});
    // The state is in WORDS as well as in the dot's colour. MakePaneListRow marks
    // its dot Raw on the explicit premise that "the colour is a restatement of
    // what the row's text already says" (UrComponents.cpp) -- here that premise
    // holds only because this line makes it hold.
    row.meta.Text(winrt::hstring{node.healthy ? node.subLabel
                                              : node.subLabel + L"  offline"});
    // ...and the row is named, so the whole fact reaches a screen reader as one
    // item rather than as three fragments with a colour nobody is told about.
    Automation::AutomationProperties::SetName(
        row.root,
        winrt::hstring{node.label + L", " + node.subLabel + L", " +
                       (node.healthy ? L"online" : L"offline")});
    // The last row's bottom hairline would land one DIP above the strip's own
    // top hairline and read as a 2 px rule. The row species is unchanged; only
    // this instance's trailing edge goes.
    if (i + 1 == path.size())
      row.root.BorderThickness(ThicknessHelper::FromUniformLength(0));
    nodes.Children().Append(row.root);
  }

  drawer.Child(nodes);
  return drawer;
}
```

- [ ] **Step 4: Populate `v.drawer` in `MakeStatusStrip()`.**

In `MakeStatusStrip`, immediately before `StartStatusDotPulse(ring, world.connectState);`:
```cpp
  // Built with the strip and hosted separately by MainWindow: the drawer must
  // draw OVER the destination, and a child of the strip's own Auto-height row
  // would grow that row instead.
  v.drawer = MakeStatusDrawer(world.relayPath);
```

- [ ] **Step 5: Implement `SetStatusStripDrawerOpen()`.**

Replace 2/4's no-op definition with:
```cpp
void SetStatusStripDrawerOpen(StatusStripView& v, bool open) {
  namespace anim = Media::Animation;
  if (!v.drawer) return;

  if (open) {
    // The drawer's entrance IS a page transition with no outgoing page -- the
    // case CrossfadePageSwap documents and already handles ("that is what the
    // three former ConnectPage::AnimateDrawerIn call sites become", UrMotion.h,
    // and UrMotion.cpp really does branch on a null outgoing). It is that call,
    // not a fourth way to fade an element in, and it carries the ShouldAnimate()
    // gate with it.
    urnw::motion::CrossfadePageSwap(nullptr, v.drawer);
    return;
  }

  if (!urnw::motion::ShouldAnimate()) {
    v.drawer.Visibility(Visibility::Collapsed);
    v.drawer.Opacity(1.0);
    return;
  }

  // Exits run one step faster than entrances, on the exit curve -- UrMotion.h's
  // second rule, and the same pair RunCrossfade spends. No new token.
  anim::Storyboard sb;
  auto fadeOut = urnw::motion::MakeSplineDouble(1.0, 0.0, urnw::motion::kFastMs, 0,
                                                urnw::motion::kExitP1,
                                                urnw::motion::kExitP2);
  anim::Storyboard::SetTarget(fadeOut, v.drawer);
  anim::Storyboard::SetTargetProperty(fadeOut, L"Opacity");
  sb.Children().Append(fadeOut);
  auto drawer = v.drawer;
  sb.Completed([drawer](auto const&, auto const&) {
    // Guard against a re-open landing before this fires: only collapse it if it
    // is still the one that faded out. Same guard, same reason, as
    // UrMotion.cpp's RunCrossfade.
    if (drawer.Opacity() <= 0.01) {
      drawer.Visibility(Visibility::Collapsed);
      drawer.Opacity(1.0);  // restored for its NEXT entrance
    }
  });
  sb.Begin();
}
```

- [ ] **Step 6: Declare `ToggleStatusDrawer` in `MainWindow.xaml.h`.**

In the private section, after `void BuildStatusStrip();`:
```cpp
  // Raise or dismiss the strip's preview drawer. The ONLY thing that opens it is
  // an activation of the strip -- design 9.2: the autoplay loop "never opens or
  // closes the rail or the drawer", and everything in the demo that is not
  // ambient activity happens because a person clicked it.
  //
  // Click-outside and Escape dismissal are deliberately NOT built, and that is a
  // decision rather than an omission: the strip is a toggle, so the same control
  // both raises and dismisses, it is reachable by Tab and invoked by Enter or
  // Space, and the drawer is chrome rather than a modal -- nothing behind it is
  // blocked while it stands. ApplyBreakpoint closes it when the strip collapses,
  // which is the one case where the toggle would otherwise become unreachable.
  // Click-outside would be a RevealRoot-level pointer handler and belongs to
  // whoever owns RevealRoot's input, not to this surface.
  void ToggleStatusDrawer();
```

- [ ] **Step 7: Host the drawer and wire the click in `MainWindow.xaml.cpp`.**

In `BuildStatusStrip()`, after `StatusStripHost().Children().Append(statusStrip_.root);` and before the `LogInfo`:
```cpp
  StatusDrawerHost().Children().Clear();
  StatusDrawerHost().Children().Append(statusStrip_.drawer);
  statusStrip_.strip.Click([weak = get_weak()](auto const&, auto const&) {
    if (auto self = weak.get()) self->ToggleStatusDrawer();
  });
```

Add the function immediately after `BuildStatusStrip()`:
```cpp
void MainWindow::ToggleStatusDrawer() {
  if (!statusStrip_.drawer) return;
  // Visibility IS the state; there is no second bool to disagree with the tree.
  // During the 150 ms dismiss the drawer is still Visible, so a second
  // activation inside that window re-reads "open" and dismisses again -- which
  // is the same thing the user asked for, and cheaper than a state machine.
  const bool open = statusStrip_.drawer.Visibility() != Visibility::Visible;
  urmsg::views::SetStatusStripDrawerOpen(statusStrip_, open);
  urnw::LogInfo("window: status drawer -> {}", open ? "open" : "closed");
}
```

In `ApplyBreakpoint()`, immediately after the `StatusStripHost().Visibility(...)` line:
```cpp
  // A drawer standing on a strip that has just collapsed would be left on the
  // window's bottom edge with its deliberately-missing fourth hairline and
  // nothing beneath it. The strip going away takes its drawer with it.
  if (!strip) urmsg::views::SetStatusStripDrawerOpen(statusStrip_, false);
```

- [ ] **Step 8: Create `app/tools/verify-drawer.ps1`.**

```powershell
# Invoke ONE named element of the already-running URmessage window, then capture
# the window.
#
# Why this exists. The status strip's preview drawer opens on an activation and
# there is no launch switch for it -- design 8's table has none -- and the agent
# building this may not synthesise mouse or keyboard input. UI Automation's
# InvokePattern is neither: it sends nothing to any input queue, it calls the
# control's own automation peer, which is what a screen reader does. It also
# turns the strip's accessible name into a machine-checked fact, because an
# unnamed Button cannot be found here at all.
#
# It does NOT launch and does NOT kill anything. Run verify-render.ps1 first --
# its last line is "app still running as pid N" -- and this attaches to that
# process. Note that verify-render leaves the window at ITS final size, 1200x800
# dip, not at the size it launched with.
#
# The DPI rule from verify-render.ps1 applies verbatim: the app is PerMonitorV2,
# so a harness that is not itself DPI-aware reads a virtualised rect and then
# "proves" the wrong size. SetProcessDpiAwarenessContext is the FIRST call.
# So does its rule 3: processes are selected by EXECUTABLE PATH, never by name.
#
# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Name,
  [string]$Out = "urmessage-invoked.png",
  [string]$Configuration = "Release",
  [string]$Platform = "x64",
  [int]$SettleMs = 700
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$exe = Join-Path $repo "app\build\$Platform\$Configuration\URmessage.exe"
if (-not (Test-Path $exe)) { throw "not built: $exe" }
$exe = (Resolve-Path $exe).Path

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Dpi {
  [DllImport("user32.dll", SetLastError=true)]
  public static extern int SetProcessDpiAwarenessContext(IntPtr value);
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int left, top, right, bottom; }
  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
  [DllImport("user32.dll")]
  public static extern uint GetDpiForWindow(IntPtr hwnd);
}
"@

# FIRST, before anything measures anything. -4 = PER_MONITOR_AWARE_V2.
[void][Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4))

$proc = Get-Process -ErrorAction SilentlyContinue | Where-Object {
  $p = $null
  try { $p = $_.MainModule.FileName } catch { }
  $p -and ($p -eq $exe)
} | Select-Object -First 1
if (-not $proc) { throw "URmessage is not running - run verify-render.ps1 first ($exe)" }
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw "pid $($proc.Id) has no main window yet" }

$win = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
$cond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::NameProperty, $Name)
$el = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
if (-not $el) {
  throw "no element named '$Name' in the window. An unnamed Button is invisible to UI Automation, which is itself the defect - check AutomationProperties::SetName."
}
$el.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
Write-Host "invoked : $Name" -ForegroundColor Cyan
Start-Sleep -Milliseconds $SettleMs

$r = New-Object Dpi+RECT
[void][Dpi]::GetWindowRect($hwnd, [ref]$r)
$w = $r.right - $r.left
$h = $r.bottom - $r.top
$dpi = [Dpi]::GetDpiForWindow($hwnd)
$outDir = Join-Path $repo ".verify"
New-Item -ItemType Directory -Force $outDir | Out-Null
$path = Join-Path $outDir $Out

$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.left, $r.top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$g.Dispose()
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

Write-Host "window rect : ${w}x${h} at ($($r.left),$($r.top))  [PHYSICAL pixels]"
Write-Host "dips        : $([int]($w*96/$dpi))x$([int]($h*96/$dpi))"
Write-Host "screenshot  : $path" -ForegroundColor Green
```

- [ ] **Step 9: Build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```
Expected: `OK in <n>s -> ...\app\build\x64\Release\`.

- [ ] **Step 10: Screenshot the DEFAULT state and confirm the drawer is closed.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```
Expected: `dips        : 1560x900`. In `.verify/urmessage-window-screen.png`: **no drawer.** The conversation list runs unbroken down to the strip, and the strip itself is unchanged — dot / `Connected` / `server urmsg-01.ur.io` / padlock. A demo that opens with a panel nobody raised is a demo that fights the person driving it.

Leave the app running; step 11 attaches to it.

- [ ] **Step 11: Raise the drawer through UI Automation and LOOK.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-drawer.ps1 -Name "Connection status. Activate to show the relay path." -Out "urmessage-drawer-open.png"
```

Expected on the console:
```
invoked : Connection status. Activate to show the relay path.
dips        : 1200x800
```
**1200×800, not 1560×900** — `verify-render.ps1` resizes the window to 1200×800 dip as its last act, and this script attaches after that. If the script throws `no element named ...`, the strip Button has no accessible name: that is a defect in 2/4's `SetName`, not in this script.

Read `.verify/urmessage-drawer-open.png`. Expected:
- A **320 DIP wide** panel standing directly **on top of the status strip**, its left edge **16 DIP** from the window's left edge — flush with the connect dot below it, and therefore over the NavigationView pane rather than aligned with the conversation-list column.
- It is `#151515`, one step lighter than the `#101010` destination behind it, with a 1 px hairline on its **left, top and right** edges and **none** along its bottom. **No corner radius and no shadow.**
- Top row: a **28 DIP** header reading **`RELAY PATH`**, letterspaced, on the left, with **`3`** right-aligned.
- Below it, **exactly 3 rows, each 36 DIP tall**: a leading state dot, a node name, and a right-aligned sub-label.
- **Total drawer height 137 DIP** = 1 (the drawer's own top hairline) + 28 (header) + 3 × 36 (rows). The header's and the rows' own hairlines are *inside* their stated heights, so they add nothing.
- The check that needs no ruler: the drawer's bottom edge is **flush with the strip's top hairline**, and there is **exactly one hairline** between the last node row and the strip — no double rule.
- The three titles are `world.relayPath[i].label`. Per the fixed contract's `RelayNode` those read **`This device` / `URnetwork` / `Message server`**, with sub-labels **`This computer` / `3 hops` / `urmsg-01.ur.io``**. If the DemoWorld task seeded different words, the load-bearing checks are the ones that do not depend on them: exactly 3 rows, every title non-empty, every sub-label non-empty, all three dots green, and the third sub-label equal to the host the strip already shows.
- The strip underneath is unchanged.

- [ ] **Step 12: Confirm the toggle logged, and that nothing else opened it.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'status drawer ->' | Select-Object -Last 3"
```
Expected: **exactly one** line for this run —
```
window: status drawer -> open
```
One, not two: the drawer must have been closed before step 11 invoked it. If a `-> closed` line appears first, something opened the drawer at launch, which nothing in this surface is allowed to do (design §9.2).

- [ ] **Step 13: Commit.**

```
git ls-files | Measure-Object -Line
git add app/src/App/Views/StatusStripView.cpp app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp app/tools/verify-drawer.ps1
git ls-files | Measure-Object -Line
```
Expected: the second count is the first **+ 1** (`verify-drawer.ps1`). Then:
```
git commit -m @'
status strip: the preview drawer the strip raises

The three relay nodes as MakePaneListRow rows under a MakePaneGroupHeader, on a
sheet-coloured Border standing on the strip. A plain Border in the window tree,
not a Popup: PrintWindow does not composite a second element tree, and this
surface exists to be screenshotted.

Each row states its node health in WORDS as well as in the dot colour, and
carries an automation name, because MakePaneListRow marks its dot Raw on the
premise that the row text already says what the colour says.

Raise is CrossfadePageSwap with a null outgoing -- the case UrMotion already
documents -- and dismiss is kFastMs on the exit curve, one step faster, per the
same file. No new duration and no new curve. ApplyBreakpoint closes the drawer
whenever the strip collapses, so it is never left standing on the window edge
with no strip beneath it.

verify-drawer.ps1 opens it for a screenshot through UI Automation InvokePattern,
not through synthesised input: there is no launch switch for the drawer, and
Invoke also proves the strip carries an accessible name at all.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
```

**Deliverable:** two screenshots — `.verify/urmessage-window-screen.png` with no drawer at 1560×900, and `.verify/urmessage-drawer-open.png` with the 320×137 DIP three-node drawer standing on the strip at 1200×800 — plus a single `status drawer -> open` log line, and a committed tool the Network page's task can reuse for its own click-only affordances.

---

## Task S4: Status strip 4/4 — Advanced Mode adds epoch, session and rec/s inline

**Files:**

Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/StatusStripView.cpp; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: Status strip 1/4's `StatusEpochValue` / `StatusRecordsValue`. Status strip 2/4's `MakeStatusStrip`, `SetStatusStripAdvanced` (the no-op this task replaces), `MainWindow::BuildStatusStrip`, `statusStrip_`, `demo_`. The **Advanced Mode task**'s `app/src/App/Demo/AdvancedMode.h` (fixed contract §5: `AdvancedModeEnabled`, `SetAdvancedModeEnabled`, `InitAdvancedMode`, `OnAdvancedModeChanged`). The **demo-switches task**'s `DemoOptions::advanced`. The **verify-render `-AppArgs` task (F1)**. The **DemoWorld task**'s `World::currentEpoch`, `World::sessionMode`, `World::recordsPerSecond`. `urnw::kit::MakeStatusField` / `SetStatusFieldValue` / `MakeStatusSeparator`.
- Produces: `SetStatusStripAdvanced(StatusStripView& v, bool advanced)` implemented (signature unchanged from the fixed contract) — a Visibility walk over the elements `MakeStatusStrip` tagged `L"ur.status.advanced"`, with no rebuild and no crossfade.
`MainWindow` gains, in its constructor, the app's `urmsg::InitAdvancedMode(demo_.advanced)` call and its single `urmsg::OnAdvancedModeChanged(...)` subscriber (created here if no other surface has landed it yet), plus one seeding call and one `window: status strip advanced -> on|off` log line in `BuildStatusStrip()`.

# Status strip 4/4 — Advanced Mode adds epoch, session and rec/s inline

Design §6.6's table, first row: *Status strip — Normal: dot, state, server, lock. Advanced: + epoch, session mode, records/s.* Same row, three more fields, no new layout — which is exactly what `UrComponents.h`'s note on `StatusField` says the shape was built for: *"Four more fields must cost four more calls to this function and no layout change."*

**Two rules this task must not break.**

1. `Set*Advanced` changes **density only**. It must never re-run a builder and never run a mode crossfade: a builder re-run to change density is what blanks a visible element to opacity 0 and fades it back over itself.
2. **No view reads the preference.** The fixed contract §5 makes `Demo/AdvancedMode.h` the single owner; `MainWindow` registers **one** subscriber that calls each view's `Set*Advanced`. Two writers of `advanced_mode` is two states that disagree until a restart.

---

- [ ] **Step 1: Gate — confirm the Advanced Mode owner landed.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/AdvancedMode.h' -Pattern 'AdvancedModeEnabled|SetAdvancedModeEnabled|InitAdvancedMode|OnAdvancedModeChanged'"
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoWorld.h' -Pattern 'currentEpoch|sessionMode|recordsPerSecond'"
```

Expected: four declarations from the first, three fields from the second.

If `AdvancedMode.h` is missing, **STOP**. Do not read `LoadAppPrefs()` from this surface and do not call `SaveAppPref("advanced_mode", ...)` here — that is precisely the two-writers failure the contract's §5 exists to prevent. Report that the Advanced Mode task has not landed.

- [ ] **Step 2: Add the marker constant to `StatusStripView.cpp`.**

Beside the other literals in the anonymous namespace:
```cpp
// The marker SetStatusStripAdvanced looks for. A Tag rather than a list of six
// named members: a separator has no identity beyond "the rule in front of that
// field", and a list of names is how one of them gets forgotten and a hairline
// is left floating at the end of the Normal strip. Namespaced so it cannot
// collide with a Tag anything else in the app sets.
constexpr wchar_t kAdvancedTag[] = L"ur.status.advanced";
```

- [ ] **Step 3: Append the three Advanced fields in `MakeStatusStrip()`.**

In `StatusStripView.cpp`, after `fields.Children().Append(lock);` and before `v.strip.Content(fields);`:

```cpp
  // Advanced Mode's three, in 6.6's order ("+ epoch, session mode, records/s").
  // Each is a separator then a field, appended to the SAME horizontal panel --
  // "four more fields must cost four more calls to this function and no layout
  // change" (UrComponents.h on StatusField). They come AFTER the padlock, so the
  // Normal marks never change position when the mode changes.
  //
  // Built ONCE, here, and shown or hidden later -- never built and torn down on
  // a toggle. The mode changes live (9.1) and a strip that re-lays out from
  // scratch visibly jumps; building the values here is also what lets
  // SetStatusStripAdvanced be a Visibility change with nothing to re-read.
  //
  // Collapsed HERE rather than by the caller: a strip that flashes six fields
  // for one frame before the mode is applied is a strip that lies about what
  // Normal mode shows.
  auto addAdvanced = [&fields](winrt::hstring const& caption,
                               winrt::hstring const& value) {
    auto rule = kit::MakeStatusSeparator();
    rule.Tag(winrt::box_value(winrt::hstring{kAdvancedTag}));
    rule.Visibility(Visibility::Collapsed);
    fields.Children().Append(rule);

    auto field = kit::MakeStatusField(caption);
    kit::SetStatusFieldValue(field, value);
    field.root.Tag(winrt::box_value(winrt::hstring{kAdvancedTag}));
    field.root.Visibility(Visibility::Collapsed);
    fields.Children().Append(field.root);
  };
  // Lower case, like "server": strip captions are 11sp chrome that name a value,
  // not headings. "rec/s" is abbreviated because the strip is one row and this is
  // its sixth field.
  addAdvanced(winrt::hstring{L"epoch"},
              winrt::hstring{StatusEpochValue(world.currentEpoch)});
  addAdvanced(winrt::hstring{L"session"}, winrt::hstring{world.sessionMode});
  addAdvanced(winrt::hstring{L"rec/s"},
              winrt::hstring{StatusRecordsValue(world.recordsPerSecond)});
```

- [ ] **Step 4: Implement `SetStatusStripAdvanced()`.**

Replace 2/4's no-op definition with:
```cpp
void SetStatusStripAdvanced(StatusStripView& v, bool advanced) {
  // DENSITY ONLY. No rebuild and no crossfade: the strip on screen is the strip
  // that stays on screen, with three more fields showing. Cheap and idempotent,
  // so it is safe to call on every toggle and once at startup.
  if (!v.strip) return;
  auto fields = v.strip.Content().try_as<Controls::StackPanel>();
  if (!fields) return;
  const auto visibility = advanced ? Visibility::Visible : Visibility::Collapsed;
  const auto marker = winrt::hstring{kAdvancedTag};
  for (auto const& child : fields.Children()) {
    auto element = child.try_as<FrameworkElement>();
    if (!element) continue;
    auto tag = element.Tag().try_as<winrt::hstring>();
    if (!tag || *tag != marker) continue;
    element.Visibility(visibility);
  }
}
```

- [ ] **Step 5: Wire the one subscriber in `MainWindow.xaml.cpp`.**

First decide whether another surface already landed the window's single subscriber:

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path 'C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp' -Pattern 'OnAdvancedModeChanged|InitAdvancedMode'"
```

**If it prints nothing** — add `#include "Demo/AdvancedMode.h"` to the include block, and put this in the constructor immediately after `demo_ = urmsg::demo::ParseDemoOptions();`:
```cpp
  // --demo-advanced is SESSION ONLY and must not write the preference (design 8).
  // InitAdvancedMode is the one function that knows that; nothing in this window
  // calls SaveAppPref, and no view reads the preference itself.
  urmsg::InitAdvancedMode(demo_.advanced);
  // ONE subscriber for the whole window. Every surface with a density adds its
  // Set*Advanced line to THIS lambda; there is never a second subscriber and
  // never a second reader of the preference.
  urmsg::OnAdvancedModeChanged([weak = get_weak()](bool on) {
    auto self = weak.get();
    if (!self) return;
    urmsg::views::SetStatusStripAdvanced(self->statusStrip_, on);
  });
```

**If it prints one or more lines** — another surface landed both already. Do **not** add a second `InitAdvancedMode` call and do **not** register a second subscriber; add exactly this one line inside the existing lambda's body, beside the other surfaces' calls:
```cpp
    urmsg::views::SetStatusStripAdvanced(self->statusStrip_, on);
```
and add `#include "Demo/AdvancedMode.h"` only if it is not already there.

Either way, add this to `BuildStatusStrip()` as the last statement before its closing `LogInfo`:
```cpp
  // Seed the density once, from the mode's value at startup. The subscriber
  // fires on CHANGES, and --demo-advanced is not a change -- InitAdvancedMode
  // runs before anything is subscribed.
  const bool advanced = urmsg::AdvancedModeEnabled();
  urmsg::views::SetStatusStripAdvanced(statusStrip_, advanced);
  urnw::LogInfo("window: status strip advanced -> {}", advanced ? "on" : "off");
```

- [ ] **Step 6: Build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```
Expected: `OK in <n>s -> ...\app\build\x64\Release\`.

- [ ] **Step 7: Screenshot Advanced Mode and LOOK.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats --demo-advanced"
```
Expected: `dips        : 1560x900`. Read `.verify/urmessage-window-screen.png`.

Expected on the strip, left to right, **six marks separated by five vertical hairlines, still in ONE 26 DIP row**:

`● Connected │ server urmsg-01.ur.io │ 🔒 │ epoch <N> │ session <M> │ rec/s <R>`

- The first four marks are **unmoved** from the Normal screenshot: the dot is still 8 DIP at 16 from the left edge, and the padlock is still the third mark. The three new fields are appended after it.
- The three new captions (`epoch`, `session`, `rec/s`) are in the **faint** `#5A5A5A` 11sp voice and their values in the **muted** `#989898` 12sp voice — visibly quieter than the state word, because the strip must never compete with the page.
- `<N>`, `<M>` and `<R>` are whatever the DemoWorld task seeded. The check is that all three are **present and non-empty**, not what they say — the strip does not decide those values.
- The strip is still **26 DIP** and has **not wrapped** to two rows. (Six fields plus five rules measure ~590 DIP at this scale, well inside 1560.)

- [ ] **Step 8: Screenshot Normal Mode and confirm nothing leaked.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```
Expected in `.verify/urmessage-window-screen.png`: back to **4 marks and 2 hairlines** — dot / `Connected` / rule / `server urmsg-01.ur.io` / rule / padlock.

**No trailing hairline after the padlock.** If one is there, a separator was tagged but its field was not (or vice versa) — step 3's `addAdvanced` tags and collapses both, so a stray rule means one of the pair was appended outside it.

- [ ] **Step 9: Confirm the switch did not write the preference.**

```
powershell -ExecutionPolicy Bypass -Command "Get-Content '.localstate-verify\app_prefs.json' -ErrorAction SilentlyContinue"
```
Expected: the file either does not exist, or exists **without** an `advanced_mode` key. `--demo-advanced` turns the mode on for the session only (design §8): `InitAdvancedMode` does not persist, and nothing on this surface calls `SetAdvancedModeEnabled`. If `advanced_mode` is in there, step 5 wired the constructor through `SetAdvancedModeEnabled` instead of `InitAdvancedMode`.

(`.localstate-verify` is the per-worktree root `verify-render.ps1` sets via `%URMESSAGE_APP_ROOT%`, and `app_prefs.json` sits at its top level — `Paths.cpp`.)

- [ ] **Step 10: Confirm the density was seeded and logged both ways.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'status strip advanced ->' | Select-Object -Last 2"
```
Expected, in this order:
```
window: status strip advanced -> on
window: status strip advanced -> off
```
`on` from step 7's run, `off` from step 8's. If both read `off`, `--demo-advanced` is not reaching `InitAdvancedMode`; if both read `on`, the switch is being persisted somewhere, and step 9 will have caught it.

- [ ] **Step 11: Commit.**

```
git ls-files | Measure-Object -Line
git add app/src/App/Views/StatusStripView.cpp app/src/App/MainWindow.xaml.cpp
git ls-files | Measure-Object -Line
```
Expected: the two counts are **identical** (no new files). Then:
```
git commit -m @'
status strip: Advanced Mode adds epoch, session and rec/s inline

Three more MakeStatusField calls on the same horizontal panel and no layout
change, which is the shape UrComponents.h says StatusField was built for. Built
once with the strip and shown or hidden, never rebuilt and never crossfaded:
the mode changes live and a strip that re-lays out visibly jumps.

The three pairs carry a Tag rather than six named members, because a separator
has no identity beyond "the rule in front of that field" and a list of names is
how one gets forgotten and a hairline is left floating at the end of the Normal
strip.

The window registers ONE OnAdvancedModeChanged subscriber and reads the mode
through Demo/AdvancedMode.h. No view reads the preference and nothing here
calls SaveAppPref. --demo-advanced goes through InitAdvancedMode, which is
session-only, so app_prefs.json is left without an advanced_mode key.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
```

**Deliverable:** two screenshots of the same 26 DIP strip — six marks and five hairlines under `--demo-advanced`, four and two without — plus a prefs file with no `advanced_mode` key, proving the switch is session-only.
