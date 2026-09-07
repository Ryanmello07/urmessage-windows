# Settings, Advanced Mode, Developer

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task A2: The Developer surface's two session switches: ambient-activity pause and the motion override

**Files:**

Create app/src/App/Demo/DeveloperSwitches.h; Create app/src/App/Demo/DeveloperSwitches.cpp; Modify app/src/App/UrMotion.h; Modify app/src/App/UrMotion.cpp; Modify app/src/App/App.vcxproj; Modify app/src/App/Startup.cpp

**Interfaces:**

- Consumes: A1's assertion loop in urnw::CollectDiagnostics() (this task appends a second loop beside it) and A1's --diagnose gating rule. urnw::motion::ShouldAnimate() — app/src/App/UrMotion.h:84, impl UrMotion.cpp:35-44. urnw::WantsDiagnose() — Startup.h:73.
- Produces: Header "Demo/DeveloperSwitches.h", namespace urmsg::demo: bool AmbientActivityPaused(); void SetAmbientActivityPaused(bool paused); void OnAmbientPausedChanged(std::function<void(bool)> fn); std::vector<std::wstring> DeveloperSwitchDiagnostics(). In UrMotion.h, namespace urnw::motion: void SetMotionOverride(bool enabled); void ClearMotionOverride(); bool HasMotionOverride().

# A2 — The Developer surface's two session switches: ambient-activity pause and the motion override

Design §6.6 gives Developer an **ambient-activity pause switch** and a **motion switch**. Both are
session-only — neither writes a preference — and both need a home that is *not* the Developer
view, because the code that obeys them is elsewhere: the autoplay timer reads the pause flag, and
every animation in the app reads `ShouldAnimate()`.

**Contract gap, stated rather than papered over.** Contract v2 has no entry for either switch:
§4's `MakeDeveloper(World const&)` takes only the world. Design §6.6 requires both switches, so
this task defines them, in the same shape the contract fixed for Advanced Mode (a free-function
subscription with no token, `On*Changed`). **The ambient flag must exist exactly once in the
build.** If the ambient-activity/autoplay surface exports a pause API of its own, the two are
merged at integration and this one wins by having landed first; the autoplay loop READS
`AmbientActivityPaused()` and does not export a second flag.

The motion override goes in `UrMotion.cpp` and nowhere else: `ShouldAnimate()` is already the one
choke point every animation asks, so an override there reaches all of them and a per-view flag
would reach none. It adds **no duration and no curve** (contract rule 5).

- [ ] **Step 1: declare the motion override.** In `app/src/App/UrMotion.h`, immediately after
  `bool ShouldAnimate();` (line 84), add:

```cpp
// ---- the demo's motion switch (Developer surface, design §6.6) --------------
// The one place that can honour a motion switch is this file: ShouldAnimate()
// is the choke point every animation in the app already goes through, so an
// override here reaches all of them and a per-view flag would reach none.
//
// Windows' own "Show animations" still decides when NO override is set — the
// override is additive, not a replacement — and it is SESSION ONLY: nothing
// here is written to preferences. Adds no duration and no curve.
// UI THREAD ONLY.
void SetMotionOverride(bool enabled);
void ClearMotionOverride();
bool HasMotionOverride();
```

- [ ] **Step 2: implement the override.** In `app/src/App/UrMotion.cpp`, add `#include <optional>`
  under the existing includes (after line 7), then replace lines 27-44 — the namespace opener,
  its anonymous block and `ShouldAnimate()` — with:

```cpp
namespace urnw::motion {
namespace {
namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
using winrt::Microsoft::UI::Xaml::Duration;
using winrt::Microsoft::UI::Xaml::DurationType;
using winrt::Windows::Foundation::TimeSpan;

// The Developer page's motion switch. Unset means "ask Windows", which is what
// every launch starts as.
std::optional<bool> g_motionOverride;
}  // namespace

void SetMotionOverride(bool enabled) { g_motionOverride = enabled; }
void ClearMotionOverride() { g_motionOverride.reset(); }
bool HasMotionOverride() { return g_motionOverride.has_value(); }

bool ShouldAnimate() {
  // The Developer page's motion switch, when it has been set. See UrMotion.h.
  // It returns BEFORE the UISettings call below, which is what makes the
  // --diagnose assertion safe to run before winrt::init_apartment.
  if (g_motionOverride) return *g_motionOverride;
  // "Show animations in Windows" off means the user wants motion GONE, not
  // reduced — same reading ConnectCanvas::AnimationsEnabled already used, now
  // the one place every OTHER animation in the app asks too.
  try {
    return winrt::Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
  } catch (...) {
    return true;
  }
}
```

- [ ] **Step 3: create the header.** Write `app/src/App/Demo/DeveloperSwitches.h`:

```cpp
// The two session-only switches the Developer surface owns (design §6.6).
//
// They live here rather than in DeveloperView because the code that OBEYS them
// is elsewhere: the autoplay timer reads AmbientActivityPaused(), and every
// animation reads urnw::motion::ShouldAnimate(). Developer -> flag -> consumer;
// the consumer never reaches back into the view.
//
// CONTRACT GAP: contract v2 has no entry for either switch. This header is the
// resolution, in the same shape §5 fixed for Advanced Mode — a free-function
// subscription with no token. The ambient flag must exist ONCE in the build:
// the autoplay surface READS it rather than exporting a second one.
//
// Session-only on purpose: nothing here writes a preference. Advanced Mode is
// the one persisted toggle (Demo/AdvancedMode.h) and these are not.
//
// UI THREAD ONLY, for the reason given in Demo/AdvancedMode.h.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace urmsg::demo {

// False at startup. --demo-autoplay decides whether ambient activity is ARMED
// at all; this decides whether an armed loop is currently suspended.
bool AmbientActivityPaused();
void SetAmbientActivityPaused(bool paused);

// Same contract as urmsg::OnAdvancedModeChanged: called with the NEW value,
// synchronously, on the UI thread, in registration order. No unsubscribe.
void OnAmbientPausedChanged(std::function<void(bool)> fn);

// The --diagnose assertions. Restores everything it touches; the half that
// registers a subscriber runs only under urnw::WantsDiagnose(), for the reason
// stated in Demo/AdvancedMode.h.
std::vector<std::wstring> DeveloperSwitchDiagnostics();

}  // namespace urmsg::demo
```

- [ ] **Step 4: create the stub implementation.** Write `app/src/App/Demo/DeveloperSwitches.cpp`.
  As in A1, the anonymous-namespace block is final and step 8 replaces only the three bodies:

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Demo/DeveloperSwitches.h"

#include <format>

#include "Startup.h"
#include "UrMotion.h"

namespace urmsg::demo {
namespace {

bool g_paused = false;

std::vector<std::function<void(bool)>>& Subscribers() {
  static std::vector<std::function<void(bool)>> subscribers;
  return subscribers;
}

// File-static for the same reason as A1's: this API has no unsubscribe.
int g_probeCalls = 0;
bool g_probeSaw = false;

}  // namespace

// ---- STUBS, replaced in step 8 --------------------------------------------
bool AmbientActivityPaused() { return false; }
void SetAmbientActivityPaused(bool) {}
void OnAmbientPausedChanged(std::function<void(bool)>) {}

}  // namespace urmsg::demo
```

- [ ] **Step 5: add the files to the project.** In `app/src/App/App.vcxproj`, inside the `Demo\`
  block A1 opened, after `    <ClCompile Include="Demo\AdvancedMode.cpp" />` add:

```xml
    <ClCompile Include="Demo\DeveloperSwitches.cpp" />
```

  and after `    <ClInclude Include="Demo\AdvancedMode.h" />` add:

```xml
    <ClInclude Include="Demo\DeveloperSwitches.h" />
```

  Alphabetical within the block: `AdvancedMode` < `DeveloperSwitches` < `DemoSwitches`? No —
  `DemoSwitches` and `DemoWorld` sort BEFORE `DeveloperSwitches` (`Dem` < `Dev`), so the
  foundation surface's two files land between `AdvancedMode` and this one at merge time.

- [ ] **Step 6: write the assertions.** Append inside `namespace urmsg::demo` in
  `DeveloperSwitches.cpp`, above its closing brace:

```cpp
std::vector<std::wstring> DeveloperSwitchDiagnostics() {
  std::vector<std::wstring> lines;

  // ---- always on: the motion override AT the choke point ------------------
  // ShouldAnimate() is never called here without an override already set, so
  // this never touches WinRT UISettings and is safe before init_apartment
  // (main.cpp calls CollectDiagnostics at :168, the apartment opens at :183).
  // The no-override fall-through to UISettings is checked instead by step 10's
  // normal-launch screenshot: the window still reveals.
  const bool hadOverride = urnw::motion::HasMotionOverride();
  urnw::motion::SetMotionOverride(false);
  const bool offAnimates = urnw::motion::ShouldAnimate();
  urnw::motion::SetMotionOverride(true);
  const bool onAnimates = urnw::motion::ShouldAnimate();
  urnw::motion::ClearMotionOverride();
  const bool cleared = !urnw::motion::HasMotionOverride();

  const bool motionOk = !hadOverride && !offAnimates && onAnimates && cleared;
  lines.push_back(std::format(
      L"  dev.motion       : {} no override at entry: {}; override(0)->ShouldAnimate "
      L"{}; override(1)->{}; cleared->HasMotionOverride {}; expected no 0 1 0",
      motionOk ? L"PASS" : L"FAIL", hadOverride ? L"NO" : L"no", int(offAnimates),
      int(onAnimates), int(!cleared)));

  // ---- --diagnose only: registers a subscriber that cannot be removed -----
  if (!urnw::WantsDiagnose()) {
    lines.push_back(
        L"  dev.ambient      : SKIPPED — registers a subscriber this API cannot "
        L"remove; runs only under --diagnose");
    return lines;
  }

  const bool original = AmbientActivityPaused();
  g_probeCalls = 0;
  g_probeSaw = original;
  OnAmbientPausedChanged([](bool v) { ++g_probeCalls; g_probeSaw = v; });

  SetAmbientActivityPaused(!original);
  const bool followed = (AmbientActivityPaused() == !original) && (g_probeSaw == !original);
  SetAmbientActivityPaused(!original);      // same value: must be a total no-op
  const int afterNoop = g_probeCalls;
  SetAmbientActivityPaused(original);

  const bool ambientOk = (g_probeCalls == 2) && (afterNoop == 1) && followed &&
                         (AmbientActivityPaused() == original);
  lines.push_back(std::format(
      L"  dev.ambient      : {} subscriber saw {} calls (expected 2); the "
      L"equal-value call fired nothing ({} after it, expected 1); the flag "
      L"followed the setter: {}; restored to {}: {}",
      ambientOk ? L"PASS" : L"FAIL", g_probeCalls, afterNoop,
      followed ? L"yes" : L"NO", int(original),
      (AmbientActivityPaused() == original) ? L"yes" : L"NO"));
  return lines;
}
```

- [ ] **Step 7: call them, build, and SEE THE FAIL.** In `app/src/App/Startup.cpp` add
  `#include "Demo/DeveloperSwitches.h"` beside A1's include, and directly under A1's
  `AdvancedModeDiagnostics()` loop add:

```cpp
  for (auto& line : urmsg::demo::DeveloperSwitchDiagnostics())
    lines.push_back(std::move(line));
```

  Then run:

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
app\build\x64\Release\URmessage.exe --diagnose
```

  Expected — `dev.motion` PASSes (step 2 already implemented it) and `dev.ambient` FAILs:

```
  dev.motion       : PASS no override at entry: no; override(0)->ShouldAnimate 0; override(1)->1; cleared->HasMotionOverride 0; expected no 0 1 0
  dev.ambient      : FAIL subscriber saw 0 calls (expected 2); the equal-value call fired nothing (0 after it, expected 1); the flag followed the setter: NO; restored to 0: yes
```

- [ ] **Step 8: implement the pause flag.** Replace the three stub bodies (the block headed
  `// ---- STUBS, replaced in step 8`) with:

```cpp
bool AmbientActivityPaused() { return g_paused; }

void SetAmbientActivityPaused(bool paused) {
  if (paused == g_paused) return;
  g_paused = paused;
  // Fan out over a copy — same reasoning as urmsg::SetAdvancedModeEnabled.
  const auto snapshot = Subscribers();
  for (auto const& fn : snapshot) fn(paused);
}

void OnAmbientPausedChanged(std::function<void(bool)> fn) {
  if (!fn) return;
  Subscribers().push_back(std::move(fn));
}
```

- [ ] **Step 9: build and SEE THE PASS.** Rerun step 7's two commands. Expected:

```
  dev.motion       : PASS no override at entry: no; override(0)->ShouldAnimate 0; override(1)->1; cleared->HasMotionOverride 0; expected no 0 1 0
  dev.ambient      : PASS subscriber saw 2 calls (expected 2); the equal-value call fired nothing (1 after it, expected 1); the flag followed the setter: yes; restored to 0: yes
```

- [ ] **Step 10: prove the override did not break existing motion.** Run, with no `-AppArgs`:

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1
```

  Window size expected: the default **480×760 DIP** (`WindowShell.h:21-25`) — `--demo` is not
  passed here. Read `.verify/urmessage-window-screen.png`. Expected, concretely: the window is
  fully opaque and settled — the `URmessage` wordmark, the `CONVERSATIONS` pane header, the
  search row and all **ten** sample rows at full opacity, no half-faded element. No override is
  set on a normal launch, so `WindowReveal` must run exactly as before this task.

  Ignore the script's own pixel-probe table: its five coordinates (`verify-render.ps1:262-266`)
  are calibrated to the Chats layout and it prints the Chats expectations beside whatever it
  reads. Read the image, not the probe table.

- [ ] **Step 11: commit.**

```
(git ls-files | Measure-Object -Line).Lines
git add app/src/App/Demo/DeveloperSwitches.h app/src/App/Demo/DeveloperSwitches.cpp app/src/App/UrMotion.h app/src/App/UrMotion.cpp app/src/App/App.vcxproj app/src/App/Startup.cpp
git commit -m @'
demo: ambient-pause flag and a motion override at the ShouldAnimate choke point

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
(git ls-files | Measure-Object -Line).Lines
```

  The counts must differ by exactly **2**.

**Deliverable:** `--diagnose` prints `dev.motion : PASS` and `dev.ambient : PASS`, a normal launch
prints `dev.motion : PASS` and `dev.ambient : SKIPPED`, and the window still reveals.

---

## Task A3: Settings: the pane, its groups, the Advanced Mode toggle, and the --demo=settings deep link

**Files:**

Create app/src/App/Views/SettingsView.h; Create app/src/App/Views/SettingsView.cpp; Modify app/src/App/MainWindow.xaml; Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp; Modify app/src/App/App.vcxproj

**Interfaces:**

- Consumes: Contract §1 Demo/DemoWorld.h — urmsg::demo::GetWorld(), World::myDevices (myDevices[0].isThisComputer == true), World::server, ServerInfo{host,jurisdiction,latencyMs,keyVerified}, DeviceRef::name. Contract §2 Demo/DemoSwitches.h — ParseDemoOptions(), DemoOptions, DemoScreen. A1's Demo/AdvancedMode.h — InitAdvancedMode(bool), AdvancedModeEnabled(), SetAdvancedModeEnabled(bool). Task F1's -AppArgs on app/tools/verify-render.ps1 (contract §0.2). urnw::kit builders in UrComponents.h; App.xaml styles.
- Produces: struct urmsg::views::SettingsView { winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr}; }; urmsg::views::SettingsView urmsg::views::MakeSettings(std::function<void(bool)> onAdvancedChanged, bool advanced); on MainWindow (private): void BuildSettings(); void SelectDemoScreen(urmsg::demo::DemoScreen screen); members urmsg::views::SettingsView settings_{}; urmsg::demo::DemoOptions demoOptions_{}. In MainWindow.xaml: Grid x:Name="SettingsPage".

# A3 — Settings: the pane, its groups, the Advanced Mode toggle, and the `--demo=settings` deep link

Design §6.6: *"Settings renders enough groups to look real, with Appearance, Privacy and Security
populated and the rest present but inert."* §9.1 makes "inert" a construction rule, not a colour:
anything that cannot be clicked must be **visibly inert** — so the inert groups are built from
`Border` rows, never `Button` rows, and get no hover fill, no chevron and no control.

**Three rules this task obeys and does not re-litigate.**

1. **No new localization keys.** `Strings/en/Resources.resw` is generated from
   `urnetwork/localizations` (`Localization.h:3-4`); design §9.4 and contract §0.1 settle it —
   demo copy is **English string literals in this view module**. Only the two keys that already
   exist and already say the right thing are looked up: `nav_settings` ("Settings").
2. **PANE vocabulary, not card.** `UrComponents.h` has a `MakeSettingsCard`; do **not** use it.
   This window is the pane model (`MainWindow.xaml`'s header block).
3. **One row height per list** — `UrPaneRowTallHeight` = 44 (`App.xaml:1005`), including the rows
   with no note.

**A compile trap this file must respect.** The kit builders take `winrt::hstring const&`, not
`winrt::param::hstring`, so a wide literal does NOT convert (two user-defined conversions:
`wchar_t const*` → `wstring_view` → `hstring`). `MainWindow.xaml.cpp:26-31` carries the same note.
Every literal handed to a builder goes through the `S(...)` adapter below.

- [ ] **Step 1: create the view header.** Write `app/src/App/Views/SettingsView.h`:

```cpp
// The Settings pane (design §6.6).
//
// Appearance and Privacy and security are POPULATED — real rows, values read
// from the demo world. Notifications, Storage and data and Account are present
// but VISIBLY INERT: a header that says "not in this demo" and one muted line.
// §9.1 — "a dead-looking button is a demo bug" — is why the inert rows are
// Borders and not Buttons: a Border has no hover fill, no focus rect and no
// invoke, so it cannot promise something it will not do.
//
// PANE vocabulary, not card. UrComponents.h's MakeSettingsCard is the wrong
// tool here despite its name.
//
// Copy is English string literals, NOT localization keys: Strings/en/
// Resources.resw is generated from urnetwork/localizations and this work adds
// no key to it (design §9.4).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace urmsg::views {

// Contract v2 §4: exactly one element, the pane root. Everything the pane keeps
// consistent (the Normal/Advanced meta, the disclosure panel) is captured by
// the handler that changes it, so nothing outside this file needs a handle on
// it — and there is deliberately NO SetSettingsAdvanced, because this view OWNS
// the switch rather than following it.
struct SettingsView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
};

// `onAdvancedChanged` is raised when the user moves the switch; `advanced` is
// the value the pane is BUILT in. MainWindow wires the callback to
// urmsg::SetAdvancedModeEnabled and passes urmsg::AdvancedModeEnabled() — no
// view reads the preference itself (Demo/AdvancedMode.h).
SettingsView MakeSettings(std::function<void(bool)> onAdvancedChanged, bool advanced);

}  // namespace urmsg::views
```

- [ ] **Step 2: implementation head and helpers.** Write `app/src/App/Views/SettingsView.cpp`.
  **`namespace urmsg::views` is deliberately left OPEN here** — steps 3 and 4 append inside it and
  step 4 writes its closing brace:

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/SettingsView.h"

#include <string>
#include <string_view>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.Foundation.h>

#include "Demo/DemoWorld.h"
#include "Localization.h"
#include "UrColors.h"
#include "UrComponents.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace kit = urnw::kit;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;

namespace urmsg::views {
namespace {

// UrComponents.cpp's StyleByKey is file-local (anonymous namespace, line 78) and
// is NOT exported, so this is its own copy rather than a citation of an API that
// does not exist. HasKey BEFORE Lookup: ResourceDictionary::Lookup THROWS
// hresult_out_of_bounds on a missing key, and a missing style must cost the
// style, not the pane.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

// The kit builders take `winrt::hstring const&`, and a wide literal does NOT
// convert to one implicitly — that is two user-defined conversions. See the
// same note beside Loc() in MainWindow.xaml.cpp:26-31. One named adapter rather
// than a hstring{...} at forty call sites.
winrt::hstring S(wchar_t const* text) { return winrt::hstring{text}; }
winrt::hstring Loc(std::string_view key) { return winrt::hstring{urnw::Localized(key)}; }

// ONE row height for the whole list — App.xaml's UrPaneRowTallHeight. A settings
// list is a list of EXPLAINED rows, so the rows with no note simply centre their
// title in the same 44.
constexpr double kRowHeight = 44;

// A populated row: title, one-line note, value hard right. A Border, not a
// Button: there is nothing to open, and a row that lights up under the pointer
// and then does nothing is exactly the affordance §9.1 forbids.
kit::PaneTwoLineRow MakeValueRow(winrt::hstring const& title, winrt::hstring const& note,
                                 winrt::hstring const& value) {
  auto row = kit::MakePaneTwoLineRow(title, note, kRowHeight);
  TextBlock text;
  if (auto style = StyleByKey(L"UrValueTextStyle")) text.Style(style);
  text.Text(value);
  // MakePaneKeyValueRow names its value "key, value" so a screen reader hears
  // one fact rather than two fragments (UrComponents.cpp:190-193). Same shape.
  automation::AutomationProperties::SetName(
      text, winrt::hstring{std::wstring{title} + L", " + std::wstring{value}});
  row.trailing.Children().Append(text);
  return row;
}

// A group that is present but has nothing in it: the header says so on its
// right, and the one row under it is faint and inert.
void AppendInertGroup(StackPanel const& column, winrt::hstring const& title) {
  auto header = kit::MakePaneGroupHeader(title, S(L"not in this demo"));
  column.Children().Append(header.root);
  auto row = kit::MakePaneTwoLineRow(S(L"Not part of this demo."), {}, kRowHeight);
  row.title.Foreground(urnw::colors::FaintBrush());
  column.Children().Append(row.root);
}

}  // namespace

// NOTE: `namespace urmsg::views` is deliberately STILL OPEN. Steps 3 and 4
// append into it and step 4 writes the closing `}  // namespace urmsg::views`.
```

- [ ] **Step 3: the pane shell, Appearance and Privacy and security.** Append to
  `SettingsView.cpp`:

```cpp
SettingsView MakeSettings(std::function<void(bool)> onAdvancedChanged, bool advanced) {
  SettingsView view;

  Grid pane;
  if (auto style = StyleByKey(L"UrPaneStyle")) pane.Style(style);
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  pane.RowDefinitions().Append(headerRow);
  pane.RowDefinitions().Append(bodyRow);

  // The 40px strip every pane opens with. UPPER CASE, like pane_conversations
  // ("CONVERSATIONS") and pane_thread ("THREAD"): UrPaneTitleStyle is small
  // letterspaced chrome that names a column, and XAML has no text-transform, so
  // the case lives in the literal. nav_settings ("Settings") is title case
  // because it is a NavigationViewItem label; the two are not interchangeable.
  Border header;
  if (auto style = StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  Grid headerGrid;
  TextBlock paneTitle;
  if (auto style = StyleByKey(L"UrPaneTitleStyle")) paneTitle.Style(style);
  paneTitle.Text(L"SETTINGS");
  headerGrid.Children().Append(paneTitle);

  TextBlock modeMeta;
  if (auto style = StyleByKey(L"UrPaneMetaStyle")) modeMeta.Style(style);
  modeMeta.HorizontalAlignment(HorizontalAlignment::Right);
  modeMeta.Text(advanced ? L"Advanced" : L"Normal");
  headerGrid.Children().Append(modeMeta);
  header.Child(headerGrid);
  Grid::SetRow(header, 0);
  pane.Children().Append(header);

  StackPanel column;
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroller.Content(column);
  Grid::SetRow(scroller, 1);
  pane.Children().Append(scroller);

  auto rows = column.Children();
  auto const& world = demo::GetWorld();

  // ---- Appearance ---------------------------------------------------------
  rows.Append(kit::MakePaneGroupHeader(S(L"APPEARANCE")).root);
  rows.Append(MakeValueRow(S(L"Theme"), S(L"URmessage is dark only."), S(L"Dark")).root);
  {
    // The accent as the colour ITSELF beside its hex: "pale yellow" is not a
    // value a settings list can honestly render as words. The hex is the text
    // half, so the swatch is never the only carrier (contract rule 6).
    auto row = kit::MakePaneTwoLineRow(S(L"Accent"), S(L"The URnetwork pale yellow."),
                                       kRowHeight);
    StackPanel trailing;
    trailing.Orientation(Orientation::Horizontal);
    trailing.Spacing(8);
    trailing.VerticalAlignment(VerticalAlignment::Center);
    Border swatch;
    swatch.Width(16);
    swatch.Height(16);
    swatch.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
    swatch.Background(urnw::colors::AccentBrush());
    automation::AutomationProperties::SetAccessibilityView(
        swatch, automation::Peers::AccessibilityView::Raw);
    trailing.Children().Append(swatch);
    TextBlock value;
    if (auto style = StyleByKey(L"UrValueTextStyle")) value.Style(style);
    value.Text(L"#EFF7BB");
    automation::AutomationProperties::SetName(value, S(L"Accent, #EFF7BB"));
    trailing.Children().Append(value);
    row.trailing.Children().Append(trailing);
    rows.Append(row.root);
  }
  // Measured against App.xaml rather than remembered: UrHeadingFontFamily is
  // "ABC Gravity Extended" (:184), UrBodyFontFamily is "PP Neue Montreal"
  // (:186), UrWordmarkFontFamily is "PP NeueBit" (:187). There is deliberately
  // NO "message text size" row: UrBodyTextStyle is 14 and message bubbles do
  // not exist yet, so a size row here would be the one screen in the demo that
  // says something false about the app.
  rows.Append(MakeValueRow(S(L"Typefaces"), S(L"Display, body and wordmark."),
                           S(L"ABC Gravity Extended · PP Neue Montreal · PP NeueBit"))
                  .root);

  // ---- Privacy and security ----------------------------------------------
  rows.Append(kit::MakePaneGroupHeader(S(L"PRIVACY AND SECURITY")).root);
  // Contract §1 invariant 8, asserted in --diagnose: myDevices[0].isThisComputer
  // is true. front() IS this computer; there is no ordering guess here.
  rows.Append(MakeValueRow(S(L"This device"), S(L"The device you are reading this on."),
                           world.myDevices.empty()
                               ? S(L"unknown")
                               : winrt::hstring{world.myDevices.front().name})
                  .root);
  rows.Append(MakeValueRow(S(L"Linked devices"),
                           S(L"Devices that would decrypt a message addressed to you."),
                           winrt::to_hstring(static_cast<int>(world.myDevices.size())))
                  .root);
  rows.Append(MakeValueRow(S(L"Message server"), S(L"Host and jurisdiction."),
                           winrt::hstring{world.server.host + L"  ·  " +
                                          world.server.jurisdiction})
                  .root);
  {
    auto row = MakeValueRow(S(L"Server key"),
                            S(L"Whether this client has pinned the server's key."),
                            world.server.keyVerified ? S(L"Verified") : S(L"Not verified"));
    // The one place in Settings where a colour carries meaning — and the word
    // beside it carries the same meaning, so the colour is not carrying it
    // alone (contract rule 6).
    if (auto value = row.trailing.Children().GetAt(0).try_as<TextBlock>()) {
      value.Foreground(world.server.keyVerified
                           ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                           : urnw::colors::MutedBrush());
    }
    rows.Append(row.root);
  }
```

  The function is left **open**; step 4 finishes it.

- [ ] **Step 4: the Advanced toggle, the honesty disclosure, the inert groups — and close the
  file.** Append to `SettingsView.cpp`, continuing inside `MakeSettings`:

```cpp
  // ---- Advanced -----------------------------------------------------------
  // Design §6.6: a MODE, not a destination — one persisted toggle that changes
  // what existing surfaces show.
  rows.Append(kit::MakePaneGroupHeader(S(L"ADVANCED")).root);
  {
    auto row = kit::MakePaneTwoLineRow(
        S(L"Advanced Mode"),
        S(L"Adds epoch, raw ids, per-hop timings and the Developer page."), kRowHeight);
    ToggleSwitch toggle;
    if (auto style = StyleByKey(L"UrSwitchToggleStyle")) toggle.Style(style);
    // UrSwitchToggleStyle sets OnContent/OffContent empty, which leaves the
    // switch NAMELESS to a screen reader; App.xaml's own note on that style
    // (:740-742) says to pair every instance with LabeledBy on the row title.
    automation::AutomationProperties::SetLabeledBy(toggle, row.title);

    // IsOn is written BEFORE the Toggled handler is attached, and that order is
    // load-bearing. ToggleSwitch raises Toggled from IsOn(), so wiring first and
    // seeding second would fire the callback during construction — which under
    // --demo-advanced would call SetAdvancedModeEnabled(true) and WRITE the
    // preference design §8 forbids it to write. The equality early-out inside
    // SetAdvancedModeEnabled is only the second line of defence; this ordering
    // is the first.
    toggle.IsOn(advanced);
    toggle.Toggled([onAdvancedChanged, meta = modeMeta](
                       winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
      const bool on = sender.as<ToggleSwitch>().IsOn();
      // The header meta is this view's own business: Settings owns the switch,
      // so it follows itself and needs no Set*Advanced from outside.
      meta.Text(on ? L"Advanced" : L"Normal");
      if (onAdvancedChanged) onAdvancedChanged(on);
    });
    row.trailing.Children().Append(toggle);
    rows.Append(row.root);
  }

  // ---- This demo ----------------------------------------------------------
  // Design §2's hard constraint, IN the product — cheaper than hoping a
  // presenter says it. A click that opens a panel THIS FILE owns, so it cannot
  // become a dead affordance if another surface slips.
  rows.Append(kit::MakePaneGroupHeader(S(L"THIS DEMO")).root);
  {
    auto row = kit::MakePaneTwoLineRowButton(
        S(L"What this demo does not do"),
        S(L"Read this before drawing a conclusion from any screen."), kRowHeight);
    row.value.Text(L"Show");
    // A Button whose Content is a Panel gets NO automatic name (UrComponents.h
    // says so twice, having paid for it twice).
    automation::AutomationProperties::SetName(row.root, S(L"What this demo does not do"));

    Border panel;
    panel.BorderBrush(urnw::colors::BorderBrush());
    panel.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));
    panel.Padding(ThicknessHelper::FromLengths(12, 8, 12, 14));
    panel.Visibility(Visibility::Collapsed);
    TextBlock body;
    if (auto style = StyleByKey(L"UrCaptionTextStyle")) body.Style(style);
    body.TextWrapping(TextWrapping::Wrap);
    body.Text(L"No protocol, no store, no network and no cryptography are running. "
              L"Every value on these screens is fabricated in one module. Nothing has "
              L"been sent, received, stored, encrypted or decrypted. The inspector "
              L"shows what URmessage will one day say about a real message; it is not "
              L"a statement about one.");
    panel.Child(body);

    // DELIBERATELY INSTANT. Design §7's table is the complete inventory of this
    // work's motion and a settings disclosure is not in it; UrMotion.h's
    // kSoftP1/kSoftP2 ("a gentle disclosure (Advanced Mode reveal)") name the
    // DENSITY change, not this panel. Read as a decision, not an oversight.
    row.root.Click([panel, label = row.value](auto const&, auto const&) {
      const bool open = panel.Visibility() == Visibility::Collapsed;
      panel.Visibility(open ? Visibility::Visible : Visibility::Collapsed);
      label.Text(open ? L"Hide" : L"Show");
    });

    rows.Append(row.root);
    rows.Append(panel);
  }

  // ---- present, and visibly inert (design §6.6, §9.1) ---------------------
  AppendInertGroup(column, S(L"NOTIFICATIONS"));
  AppendInertGroup(column, S(L"STORAGE AND DATA"));
  AppendInertGroup(column, S(L"ACCOUNT"));

  view.root = pane;
  return view;
}

}  // namespace urmsg::views
```

- [ ] **Step 5: add the files to the project, opening the `Views\` block.** In
  `app/src/App/App.vcxproj`, after `    <ClCompile Include="Demo\DeveloperSwitches.cpp" />` add:

```xml
    <!-- Views\ — one file per surface (design D8). ONE CONTIGUOUS BLOCK,
         alphabetical, for the same merge reason as Demo\ above. -->
    <ClCompile Include="Views\SettingsView.cpp" />
```

  and after `    <ClInclude Include="Demo\DeveloperSwitches.h" />` add:

```xml
    <!-- Views\ — see the ClCompile block above. -->
    <ClInclude Include="Views\SettingsView.h" />
```

- [ ] **Step 6: add the destination host to the markup.** In `app/src/App/MainWindow.xaml`,
  replace lines 197-200, exactly:

```xml
                    <Grid x:Name="StubBody" Grid.Row="1" />
                </Grid>

            </Grid>
```

  with:

```xml
                    <Grid x:Name="StubBody" Grid.Row="1" />
                </Grid>

                <!-- SETTINGS. The whole pane is built by
                     urmsg::views::MakeSettings (design D8: view modules, not a
                     growing MainWindow), so this is only the host. -->
                <Grid x:Name="SettingsPage" Visibility="Collapsed" />

            </Grid>
```

- [ ] **Step 7: declare the new members.** In `app/src/App/MainWindow.xaml.h`, add
  `#include "Demo/DemoSwitches.h"` and `#include "Views/SettingsView.h"` beside the existing
  `#include "UrComponents.h"`, then in the private section after `void BuildConversationList();`:

```cpp
  // The Settings destination, built once into SettingsPage. Design §6.6.
  void BuildSettings();

  // --demo=<screen> (design §8). Selecting the nav item is the SAME path a
  // click takes — it raises SelectionChanged, which calls ShowDestination — so
  // a deep link and a click cannot drift apart. Screens this function does not
  // own are left alone, so other surfaces add their own case.
  void SelectDemoScreen(urmsg::demo::DemoScreen screen);
```

  and after `urnw::kit::PaneSearchRow search_{};`:

```cpp
  // Parsed once, in the constructor, and kept: several later steps need it and
  // re-parsing the command line per caller is how two of them disagree.
  urmsg::demo::DemoOptions demoOptions_{};
  urmsg::views::SettingsView settings_{};
```

- [ ] **Step 8: wire it up.** In `app/src/App/MainWindow.xaml.cpp` add `#include
  "Demo/AdvancedMode.h"` beside `#include "Localization.h"`. Then in the constructor replace
  lines 76-77, exactly:

```cpp
  ApplyStrings();
  BuildConversationList();
```

  with:

```cpp
  // Advanced Mode is seeded BEFORE any view is built: MakeSettings takes the
  // current value as an argument.
  //
  // --demo=developer implies Advanced Mode, because the Developer nav item only
  // EXISTS under Advanced Mode (design §6.6) and a deep link to a destination
  // the rail is hiding lands on nothing. The OR lives HERE, at the one call
  // site, rather than inside InitAdvancedMode, whose signature is fixed.
  demoOptions_ = urmsg::demo::ParseDemoOptions();
  urmsg::InitAdvancedMode(demoOptions_.advanced ||
                          demoOptions_.screen == urmsg::demo::DemoScreen::Developer);

  ApplyStrings();
  BuildConversationList();
  BuildSettings();
```

  Then replace the constructor's line 101-102, exactly:

```cpp
  ApplyBreakpoint();
  urnw::LogInfo("window: main window constructed");
```

  with:

```cpp
  ApplyBreakpoint();
  // LAST, and after everything that can change what a destination looks like:
  // A4 inserts ApplyAdvancedMode immediately ABOVE this line, so a deep link
  // never selects a nav item that is still collapsed.
  SelectDemoScreen(demoOptions_.screen);
  urnw::LogInfo("window: main window constructed");
```

  Then add the two definitions immediately after `BuildConversationList()`'s closing brace:

```cpp
void MainWindow::BuildSettings() {
  settings_ = urmsg::views::MakeSettings(
      [](bool on) { urmsg::SetAdvancedModeEnabled(on); },
      urmsg::AdvancedModeEnabled());
  SettingsPage().Children().Clear();
  SettingsPage().Children().Append(settings_.root);
  urnw::LogInfo("window: settings pane built (advanced={})",
                urmsg::AdvancedModeEnabled() ? "on" : "off");
}

void MainWindow::SelectDemoScreen(urmsg::demo::DemoScreen screen) {
  if (screen == urmsg::demo::DemoScreen::Settings) {
    HomeNav().SelectedItem(SettingsNavItem());
  }
}
```

- [ ] **Step 9: route the destination.** Still in `MainWindow.xaml.cpp`, four edits to
  `ShowDestination` (all additive except the last, because the Network destination lands in this
  same function from another surface).

  (a) after `const bool chats = (tag == L"chats");` (line 195) add:

```cpp
  const bool settings = (tag == L"settings");
  SettingsPage().Visibility(settings ? Visibility::Visible : Visibility::Collapsed);
```

  (b) replace line 197 with:

```cpp
  StubPage().Visibility((chats || settings) ? Visibility::Collapsed
                                            : Visibility::Visible);
```

  (c) after the `if (chats) { ... return; }` block (lines 199-202) add:

```cpp
  if (settings) {
    HomeNav().Header(box_value(Loc("nav_settings")));
    return;
  }
```

  (d) **four lines out, two lines in.** This is in `ShowDestination`, lines 203-206 — *not* in
  `ApplyStrings()`, which contains nothing of the kind. Replace, exactly:

```cpp
  const auto label = (tag == L"contacts") ? Loc("nav_contacts")
                                          : Loc("nav_settings");
  HomeNav().Header(box_value(label));
  StubPaneTitle().Text(label);
```

  with:

```cpp
  // Settings returned above, so the stub tail is now reached only by Contacts.
  // A later destination adds its own `if (...) { ... return; }` above this.
  HomeNav().Header(box_value(Loc("nav_contacts")));
  StubPaneTitle().Text(Loc("nav_contacts"));
```

- [ ] **Step 10: build and look.** Run:

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=settings"
```

  `-AppArgs` is task **F1**'s addition to `verify-render.ps1` (contract §0.2). There is no `-Args`
  parameter and there never was; the script is `[CmdletBinding()]`, so `-Args` is a terminating
  parameter-binding error. Disregard the script's own pixel-probe table
  (`verify-render.ps1:262-266`): those five coordinates are calibrated to the Chats layout and it
  prints the Chats expectations beside whatever it reads. Read the image.

  **Window size expected: 1560×900 DIP**, from `--demo` (design D7). If the shell surface's D7
  window sizing has not landed yet the window is the default 480×760 (`WindowShell.h:21-25`); the
  row content below is identical, the pane simply scrolls and the nav rail is an overlay behind
  the toggle rather than an expanded pane.

  Read `.verify/urmessage-window-screen.png`. Expected, concretely:

  - the pane header strip reads `SETTINGS` on the left and `Normal` on the right;
  - **seven** group headers, in this order, all upper case: `APPEARANCE`, `PRIVACY AND SECURITY`,
    `ADVANCED`, `THIS DEMO`, `NOTIFICATIONS`, `STORAGE AND DATA`, `ACCOUNT`;
  - `APPEARANCE` has **3** rows; row 2 shows a 16×16 pale-yellow square immediately left of the
    text `#EFF7BB`; row 3's value reads `ABC Gravity Extended · PP Neue Montreal · PP NeueBit`;
  - `PRIVACY AND SECURITY` has **4** rows; row 4's right-hand value reads `Verified` in green
    `#87FB67` (or `Not verified` in muted `#989898`);
  - `ADVANCED` has **1** row, `Advanced Mode`, with a ToggleSwitch on the right in its **off**
    position — grey track, knob left;
  - `THIS DEMO` has **1** row, `What this demo does not do`, with `Show` and a chevron at its
    right edge and **no** panel under it;
  - the last three headers each carry `not in this demo` on the right and have **exactly one** row
    reading `Not part of this demo.` in faint `#5A5A5A`;
  - every row is the same height and the hairlines are evenly spaced — no 34/44 mixture;
  - **no visible text reads as a snake_case key id.** `Localized()` returns the key when MRT
    cannot resolve it, so a broken lookup shows as `nav_settings`, not as an error;
  - if the shell surface's `DEMO` watermark chip (design D2) has landed, it is in the title bar.
    That is correct and not a regression.

  At 480×760 the pane scrolls: `APPEARANCE` through the first rows of `ADVANCED` are above the
  fold; scroll to check the rest.

  For anything about the **nav rail**, read `.verify/urmessage-window-wide.png` instead — the
  script's final act resizes to 1200×800 DIP and captures again (`verify-render.ps1:274-297`), and
  1200 ≥ 1008 puts `NavigationView` in its expanded pane with labels. Expected there: `Settings`
  is the selected item, with the pale-yellow `#EFF7BB` indicator bar and the `#242424` pill.

- [ ] **Step 11: confirm a normal launch is unchanged.** Run `verify-render.ps1` with **no**
  `-AppArgs`. Window size expected: **480×760**. The window must open on **Chats**, with the
  `CONVERSATIONS` pane, the search row and its ten sample rows exactly as before. `--demo` is
  opt-in (design §8).

- [ ] **Step 12: commit.**

```
(git ls-files | Measure-Object -Line).Lines
git add app/src/App/Views/SettingsView.h app/src/App/Views/SettingsView.cpp app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp app/src/App/App.vcxproj
git commit -m @'
settings: the pane, its groups, the Advanced Mode toggle and --demo=settings

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
(git ls-files | Measure-Object -Line).Lines
```

  The counts must differ by exactly **2**.

**Deliverable:** `--demo=settings` opens straight onto a Settings pane whose seven groups render
as listed above, `--demo=settings --demo-advanced` opens the same pane with `Advanced` in the
header meta and the switch on, and a normal launch still opens on Chats.

---

## Task A4: MainWindow's one Advanced Mode subscription, and the gate that --demo-advanced writes no preference

**Files:**

Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: A1's Demo/AdvancedMode.h — AdvancedModeEnabled(), SetAdvancedModeEnabled(), OnAdvancedModeChanged(); and A1's advanced.notify assertion, which is the positive control for step 6. A3's MainWindow::BuildSettings(), MainWindow::SelectDemoScreen() and the constructor sequence. Task F1's -AppArgs on verify-render.ps1. urnw::Paths — AppPrefsFile() is StorageRoot()/app_prefs.json (Paths.cpp:54); verify-render.ps1:153 sets $env:URMESSAGE_APP_ROOT to <repo>\.localstate-verify and StorageRoot() honours it (Paths.cpp:44-46).
- Produces: On MainWindow (private): void ApplyAdvancedMode(bool advanced) — the ONE place every MainWindow-owned Advanced Mode surface is repainted; A5 extends it with the Developer nav gate and the Developer rebuild.

# A4 — MainWindow's one Advanced Mode subscription, and the gate that `--demo-advanced` writes no preference

Design §9.1: *"every affected surface re-renders live, without a restart."* Contract §5 names the
owner: **`MainWindow` registers one subscriber.** This task is that subscription, plus the gate on
design §8's hard rule — `--demo-advanced` starts with Advanced Mode on *without writing the
preference*.

**What is and is not covered, stated rather than implied.** The agent may not synthesise input, so
no step here claims a click was tested. Three separate things, three instruments:

1. **The fan-out reaches every subscriber** — A1's `advanced.notify` line in `--diagnose`.
2. **The handler's output is the right pixels** — `ApplyAdvancedMode` is called at construction
   with the same argument the subscriber would pass, so a screenshot under `--demo-advanced` is a
   screenshot of that function's output.
3. **The subscriber is attached** — proven only by construction: one unconditional line in the
   constructor. Nothing here fires it at run time, and this task does not pretend otherwise.

- [ ] **Step 1: declare it.** In `app/src/App/MainWindow.xaml.h`, in the private section after
  `void BuildSettings();`, add:

```cpp
  // Every MainWindow-OWNED surface that changes under Advanced Mode is
  // repainted from here, off the ONE subscription contract §5 fixes (design
  // §9.1: live, without a restart). A view module never subscribes on its own
  // account — see Demo/AdvancedMode.h's ownership note.
  void ApplyAdvancedMode(bool advanced);
```

  No token member is declared. `OnAdvancedModeChanged` has no unsubscribe, deliberately: the
  window outlives every subscriber in this process, so there is nothing to remove and no field
  promising a teardown that does not exist.

- [ ] **Step 2: register the subscriber and define the handler.** In
  `app/src/App/MainWindow.xaml.cpp`, in the constructor, immediately after the `BuildSettings();`
  line A3 added, insert:

```cpp
  // THE one subscription (contract §5). get_weak() rather than a raw this: the
  // subscriber list outlives any single window by construction.
  urmsg::OnAdvancedModeChanged([weak = get_weak()](bool advanced) {
    if (auto self = weak.get()) self->ApplyAdvancedMode(advanced);
  });
```

  and add the definition immediately after `MainWindow::BuildSettings()`'s closing brace:

```cpp
void MainWindow::ApplyAdvancedMode(bool advanced) {
  // Settings is NOT repainted from here: it owns the switch and follows itself
  // (Views/SettingsView.h). A5 adds the Developer nav gate and the Developer
  // rebuild to this function; the other surfaces' Set*Advanced calls join it as
  // their tasks land, one line each.
  urnw::LogInfo("window: advanced mode -> {}", advanced ? "on" : "off");
}
```

- [ ] **Step 3: call it once, in the right place.** In the constructor, insert
  **immediately above** the `SelectDemoScreen(demoOptions_.screen);` line A3 added:

```cpp
  // BEFORE SelectDemoScreen, and the order is load-bearing. A5 makes this the
  // function that flips DeveloperNavItem's Visibility, and setting
  // NavigationView.SelectedItem to a COLLAPSED item is not a supported
  // selection path: it may not stick and SelectionChanged may not fire, so
  // --demo=developer would open on Chats. Write the visibility first, select
  // second. Calling it here also writes the state for real on the first pass
  // rather than relying on the markup defaults happening to be right.
  ApplyAdvancedMode(urmsg::AdvancedModeEnabled());
```

  The constructor sequence is now, in full, and every later task in this surface anchors on it:

```
InitializeComponent -> ExtendsContentIntoTitleBar/SetTitleBar
-> ParseDemoOptions -> InitAdvancedMode
-> ApplyStrings -> BuildConversationList -> BuildSettings -> [A5: BuildDeveloper]
-> OnAdvancedModeChanged (the one subscription)
-> reveal_.Bind / reveal_.Arm -> SizeChanged -> ApplyBreakpoint
-> ApplyAdvancedMode -> SelectDemoScreen -> LogInfo
```

  A5 inserts `BuildDeveloper();` immediately after `BuildSettings();`, i.e. **before** the
  subscription, so the subscriber can never run against a default-constructed `DeveloperView`.

- [ ] **Step 4: build and look at the NORMAL state.** Run:

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=settings"
```

  Window size expected: **1560×900 DIP** under `--demo` (480×760 if the shell surface's D7 sizing
  has not landed). Read `.verify/urmessage-window-screen.png`. Expected: identical to A3 step 10 —
  header meta `Normal`, the `ADVANCED` group's single row with its ToggleSwitch **off** (grey
  track, knob left). Then read the log tail to confirm the handler ran once:

```
Get-Content "$env:LOCALAPPDATA\..\..\nul" -ErrorAction SilentlyContinue   # placeholder-free: use the line below
Select-String -Path .\.localstate-verify\logs\urmessage-app.log -Pattern "advanced mode ->"
```

  Expected: exactly one match, `window: advanced mode -> off`. (`verify-render.ps1:153` points
  `URMESSAGE_APP_ROOT` at `<repo>\.localstate-verify`, and `Paths.cpp:52` puts the log at
  `<root>/logs/urmessage-app.log`.)

- [ ] **Step 5: look at the ADVANCED state.** Run:

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=settings --demo-advanced"
```

  Read the same file. Expected, and these are the **only** differences from step 4:

  - the pane header's right meta reads `Advanced`;
  - the `Advanced Mode` switch is **on** — track filled `#638BFC` (`App.xaml:52`,
    `ToggleSwitchFillOn`), knob to the right;
  - nothing else moved: the same seven group headers, the same row order, the same 44-high rows.

  And:

```
Select-String -Path .\.localstate-verify\logs\urmessage-app.log -Pattern "advanced mode ->"
```

  Expected: the newest match reads `window: advanced mode -> on`.

- [ ] **Step 6: the POSITIVE CONTROL — prove the query can find the key.** A gate that reads a
  path nothing ever writes passes no matter what the code does. So first prove the path and the
  query are right, by running the one code path that *does* write it (A1's `advanced.notify`
  block, which runs only under `--diagnose`) into a scratch root:

```
$probe = Join-Path (Get-Location) ".localstate-persist-probe"
Remove-Item -Recurse -Force $probe -ErrorAction SilentlyContinue
$env:URMESSAGE_APP_ROOT = $probe
Start-Process -FilePath "app\build\x64\Release\URmessage.exe" -ArgumentList "--diagnose" -Wait
Select-String -Path (Join-Path $probe "app_prefs.json") -Pattern "advanced_mode"
Remove-Item Env:URMESSAGE_APP_ROOT
```

  Expected: **one match line**, containing `"advanced_mode":false`. If it finds nothing, the query
  or the path is wrong and step 7 is meaningless — fix it here.

- [ ] **Step 7: the GATE — `--demo-advanced` must write no preference.** Design §8. Run:

```
Remove-Item .\.localstate-verify\app_prefs.json -ErrorAction SilentlyContinue
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=settings --demo-advanced"
Test-Path .\.localstate-verify\app_prefs.json
Select-String -Path .\.localstate-verify\app_prefs.json -Pattern "advanced_mode" -ErrorAction SilentlyContinue
```

  Expected: `Test-Path` prints **False**, and `Select-String` prints nothing. (The run under test
  writes its prefs to `<repo>\.localstate-verify\app_prefs.json`, because `verify-render.ps1:153`
  sets `URMESSAGE_APP_ROOT` and `StorageRoot()` honours it, `Paths.cpp:44-46`. The file name is
  `app_prefs.json`, `Paths.cpp:54` — not `prefs.json`, and not under `%LOCALAPPDATA%\URmessage`,
  whose real default root is `%LOCALAPPDATA%\URmessage\app`.)

  If `advanced_mode` is there, one of two things broke: `InitAdvancedMode` is persisting, or the
  `ADVANCED` row's `IsOn` is being written **after** its `Toggled` handler is attached, which
  fires the callback at construction. A3 step 4 seeds `IsOn` first for exactly this reason, and
  A1's equality early-out in `SetAdvancedModeEnabled` is the second line of defence; check both.

- [ ] **Step 8: commit.**

```
(git ls-files | Measure-Object -Line).Lines
git add app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m @'
window: the one Advanced Mode subscription, and the no-persist gate for --demo-advanced

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
(git ls-files | Measure-Object -Line).Lines
```

  The counts must be **identical** — no new files.

**Deliverable:** two screenshots that differ in exactly two places (`Normal`/`Advanced` and the
switch), a log line proving `ApplyAdvancedMode` ran with the right value, and a persistence gate
with a published positive control: `--diagnose` into a scratch root writes `advanced_mode`, and
`--demo=settings --demo-advanced` leaves no `app_prefs.json` at all.

---

## Task A5: Developer: the destination, the demo-state inspector, and the two session switches

**Files:**

Create app/src/App/Views/DeveloperView.h; Create app/src/App/Views/DeveloperView.cpp; Modify app/src/App/MainWindow.xaml; Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp; Modify app/src/App/App.vcxproj

**Interfaces:**

- Consumes: Contract §1 Demo/DemoWorld.h — GetWorld(), World::{conversations,myDevices,currentEpoch}, Conversation::rows. Contract §2 Demo/DemoSwitches.h — ParseDemoOptions(), DemoOptions, DemoScreen. A1's Demo/AdvancedMode.h — AdvancedModeEnabled(). A2's Demo/DeveloperSwitches.h and urnw::motion::{SetMotionOverride,ClearMotionOverride,HasMotionOverride,ShouldAnimate}. A3's MainWindow::SelectDemoScreen, BuildSettings and the ShowDestination shape. A4's MainWindow::ApplyAdvancedMode and the constructor sequence. Task F1's -AppArgs.
- Produces: struct urmsg::views::DeveloperView { winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr}; }; urmsg::views::DeveloperView urmsg::views::MakeDeveloper(urmsg::demo::World const& world); on MainWindow (private): void BuildDeveloper(); member urmsg::views::DeveloperView developer_{}. In MainWindow.xaml: NavigationViewItem x:Name="DeveloperNavItem" (Tag "developer", Visibility Collapsed) and Grid x:Name="DeveloperPage".

# A5 — Developer: the destination, the demo-state inspector, and the two session switches

Design §6.6: *"Developer carries a demo-state inspector, an ambient-activity pause switch, a
motion switch, and a `DemoWorld` dump. It is the honest place to put things that would otherwise
tempt their way into the product surfaces."* The dump is A6; this task is everything else, plus
the nav item and its Advanced-Mode gate.

**Two row species, on purpose.** The demo-state group is a **figures** list — key left, value hard
right — at `MakePaneKeyValueRow`'s default 34. The switches group is a list of **explained rows**
at 44. "One row height per list" is a property of a *list*; these are two groups with two jobs.

**How Developer stays current, decided without a contract deviation.** Contract §4 gives
`DeveloperView` no `Set*Advanced` and only a `root`. So `MakeDeveloper` reads the state it reports
at build time, and `MainWindow::ApplyAdvancedMode` **rebuilds the page** when the mode moves. That
uses only contract API, needs no subscription inside a view module, and cannot go stale — and the
two switches survive a rebuild because both are seeded from process state
(`AmbientActivityPaused()`, `HasMotionOverride()`), not from the widgets.

- [ ] **Step 1: create the view header.** Write `app/src/App/Views/DeveloperView.h`:

```cpp
// The Developer destination (design §6.6): a demo-state inspector, the two
// session switches, and (A6) the DemoWorld dump.
//
// It exists so the things an engineer wants — a state readout, a pause, a
// motion kill switch — have somewhere honest to live instead of leaking into
// the product surfaces an audience is looking at.
//
// Reachable only under Advanced Mode: MainWindow collapses the nav item when
// the mode is off, and --demo=developer forces the mode on for exactly that
// reason (MainWindow's constructor, beside InitAdvancedMode).
//
// Contract §4 gives this view no Set*Advanced, so it reads what it reports at
// BUILD time and MainWindow::ApplyAdvancedMode rebuilds it when the mode moves.
// Both switches survive a rebuild: each is seeded from process state, not from
// the widget.
//
// Copy is English string literals, not localization keys (design §9.4).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

struct DeveloperView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
};

DeveloperView MakeDeveloper(urmsg::demo::World const& world);

}  // namespace urmsg::views
```

- [ ] **Step 2: implementation head and helpers.** Write `app/src/App/Views/DeveloperView.cpp`.
  **`namespace urmsg::views` is deliberately left OPEN** — step 4 writes its closing brace:

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/DeveloperView.h"

#include <string>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.Foundation.h>

#include "Demo/AdvancedMode.h"
#include "Demo/DemoSwitches.h"
#include "Demo/DeveloperSwitches.h"
#include "UrColors.h"
#include "UrComponents.h"
#include "UrMotion.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace kit = urnw::kit;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;

namespace urmsg::views {
namespace {

// Its own copy: UrComponents.cpp's StyleByKey is file-local and not exported.
// HasKey before Lookup — Lookup THROWS on a missing key.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

// The kit builders take `winrt::hstring const&` and a wide literal does not
// convert (two user-defined conversions). Same adapter as SettingsView.cpp.
winrt::hstring S(wchar_t const* text) { return winrt::hstring{text}; }

// MakePaneKeyValueRow bakes the accessible name at CONSTRUCTION —
// UrComponents.cpp:192 sets Name(value) = "key, value" — so a row built with an
// empty value and written afterwards would announce "Motion," with nothing
// after the comma, forever. EVERY deferred write goes through here.
void SetFigure(kit::PaneKeyValueRow const& row, wchar_t const* key,
               winrt::hstring const& value) {
  if (!row.value) return;
  row.value.Text(value);
  automation::AutomationProperties::SetName(
      row.value, winrt::hstring{std::wstring{key} + L", " + std::wstring{value}});
}

// The FIGURES height (MakePaneKeyValueRow's own default) and the EXPLAINED-ROW
// height. Two groups, two jobs — not one list that drifted.
constexpr double kFigureHeight = 34;
constexpr double kSwitchRowHeight = 44;

winrt::hstring ScreenName(demo::DemoScreen screen) {
  switch (screen) {
    case demo::DemoScreen::Chats: return S(L"chats");
    case demo::DemoScreen::Thread: return S(L"thread");
    case demo::DemoScreen::Inspect: return S(L"inspect");
    case demo::DemoScreen::Network: return S(L"network");
    case demo::DemoScreen::Settings: return S(L"settings");
    case demo::DemoScreen::Developer: return S(L"developer");
    case demo::DemoScreen::None: break;
  }
  return S(L"none");
}

// "Ambient activity" is a THREE-state fact, and collapsing it to on/off would
// hide the difference between "never armed" and "armed and held".
winrt::hstring AmbientLabel(bool autoplay) {
  if (!autoplay) return S(L"off (no --demo-autoplay)");
  return demo::AmbientActivityPaused() ? S(L"paused") : S(L"running");
}

winrt::hstring MotionLabel() {
  const bool animates = urnw::motion::ShouldAnimate();
  if (urnw::motion::HasMotionOverride())
    return animates ? S(L"on (override)") : S(L"off (override)");
  return animates ? S(L"on (Windows)") : S(L"off (Windows)");
}

}  // namespace

// NOTE: `namespace urmsg::views` is deliberately STILL OPEN. Steps 3 and 4
// append into it and step 4 writes the closing `}  // namespace urmsg::views`.
```

- [ ] **Step 3: the pane shell and the demo-state inspector.** Append to `DeveloperView.cpp`:

```cpp
DeveloperView MakeDeveloper(urmsg::demo::World const& world) {
  DeveloperView view;

  Grid pane;
  if (auto style = StyleByKey(L"UrPaneStyle")) pane.Style(style);
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  pane.RowDefinitions().Append(headerRow);
  pane.RowDefinitions().Append(bodyRow);

  Border header;
  if (auto style = StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  TextBlock paneTitle;
  if (auto style = StyleByKey(L"UrPaneTitleStyle")) paneTitle.Style(style);
  // Upper case, like pane_conversations and pane_thread. The nav item's label
  // is title case ("Developer"); a pane title and a nav label are two voices.
  paneTitle.Text(L"DEVELOPER");
  header.Child(paneTitle);
  Grid::SetRow(header, 0);
  pane.Children().Append(header);

  StackPanel column;
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroller.Content(column);
  Grid::SetRow(scroller, 1);
  pane.Children().Append(scroller);

  auto rows = column.Children();
  const auto options = demo::ParseDemoOptions();

  size_t messageRows = 0;
  for (auto const& conversation : world.conversations)
    messageRows += conversation.rows.size();

  // ---- DEMO STATE ---------------------------------------------------------
  rows.Append(kit::MakePaneGroupHeader(S(L"DEMO STATE")).root);
  rows.Append(kit::MakePaneKeyValueRow(S(L"Demo world"),
                                       options.enabled ? S(L"on") : S(L"off"),
                                       kFigureHeight).root);
  rows.Append(kit::MakePaneKeyValueRow(S(L"Deep link (--demo=)"),
                                       ScreenName(options.screen), kFigureHeight).root);
  rows.Append(kit::MakePaneKeyValueRow(S(L"Watermark"),
                                       options.watermark ? S(L"on") : S(L"off"),
                                       kFigureHeight).root);
  rows.Append(kit::MakePaneKeyValueRow(
                  S(L"Advanced Mode"),
                  urmsg::AdvancedModeEnabled() ? S(L"on") : S(L"off"), kFigureHeight)
                  .root);

  // The two live figures. Built WITH their value so the accessible name is
  // right from the start; the switches below rewrite both through SetFigure.
  auto ambientRow = kit::MakePaneKeyValueRow(S(L"Ambient activity"),
                                             AmbientLabel(options.autoplay), kFigureHeight);
  rows.Append(ambientRow.root);
  auto motionRow = kit::MakePaneKeyValueRow(S(L"Motion"), MotionLabel(), kFigureHeight);
  rows.Append(motionRow.root);

  rows.Append(kit::MakePaneKeyValueRow(
                  S(L"Conversations"),
                  winrt::to_hstring(static_cast<int>(world.conversations.size())),
                  kFigureHeight).root);
  rows.Append(kit::MakePaneKeyValueRow(
                  S(L"Message rows"),
                  winrt::to_hstring(static_cast<int>(messageRows)), kFigureHeight).root);
  rows.Append(kit::MakePaneKeyValueRow(
                  S(L"Linked devices"),
                  winrt::to_hstring(static_cast<int>(world.myDevices.size())),
                  kFigureHeight).root);
  rows.Append(kit::MakePaneKeyValueRow(
                  S(L"Current epoch"),
                  winrt::to_hstring(world.currentEpoch), kFigureHeight).root);
```

  The function is left **open**; step 4 finishes it.

- [ ] **Step 4: the two switches — and close the file.** Append, continuing inside
  `MakeDeveloper`:

```cpp
  // ---- SWITCHES (session only; nothing here writes a preference) ----------
  rows.Append(kit::MakePaneGroupHeader(S(L"SWITCHES")).root);
  {
    auto row = kit::MakePaneTwoLineRow(
        S(L"Pause ambient activity"),
        S(L"Stops the typing indicator and incoming messages."), kSwitchRowHeight);
    ToggleSwitch toggle;
    if (auto style = StyleByKey(L"UrSwitchToggleStyle")) toggle.Style(style);
    automation::AutomationProperties::SetLabeledBy(toggle, row.title);
    // Seed BEFORE wiring, for the same reason as the Advanced Mode switch:
    // ToggleSwitch raises Toggled from IsOn().
    toggle.IsOn(demo::AmbientActivityPaused());
    const bool autoplay = options.autoplay;
    toggle.Toggled([figure = ambientRow, autoplay](
                       winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
      demo::SetAmbientActivityPaused(sender.as<ToggleSwitch>().IsOn());
      SetFigure(figure, L"Ambient activity", AmbientLabel(autoplay));
    });
    row.trailing.Children().Append(toggle);
    rows.Append(row.root);
  }
  {
    auto row = kit::MakePaneTwoLineRow(
        S(L"Animations"),
        S(L"Overrides Windows' own Show animations setting, for this run only."),
        kSwitchRowHeight);
    ToggleSwitch toggle;
    if (auto style = StyleByKey(L"UrSwitchToggleStyle")) toggle.Style(style);
    automation::AutomationProperties::SetLabeledBy(toggle, row.title);
    // Seeded from what the app WOULD do right now, so the switch starts telling
    // the truth even on a machine with animations off.
    toggle.IsOn(urnw::motion::ShouldAnimate());
    toggle.Toggled([figure = motionRow](
                       winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
      // One choke point: the override lives in UrMotion.cpp and every animation
      // in the app already asks ShouldAnimate(). No new duration, no new curve.
      urnw::motion::SetMotionOverride(sender.as<ToggleSwitch>().IsOn());
      SetFigure(figure, L"Motion", MotionLabel());
    });
    row.trailing.Children().Append(toggle);
    rows.Append(row.root);
  }

  // ---- A6 appends the DEMOWORLD DUMP group here ---------------------------

  view.root = pane;
  return view;
}

}  // namespace urmsg::views
```

- [ ] **Step 5: add the files to the project.** In `app/src/App/App.vcxproj`, inside the `Views\`
  block A3 opened — alphabetical, so **before** `SettingsView`:

```xml
    <ClCompile Include="Views\DeveloperView.cpp" />
```

  immediately above `    <ClCompile Include="Views\SettingsView.cpp" />`, and

```xml
    <ClInclude Include="Views\DeveloperView.h" />
```

  immediately above `    <ClInclude Include="Views\SettingsView.h" />`.

- [ ] **Step 6: add the nav item and the page host.** In `app/src/App/MainWindow.xaml`, replace
  lines 108-114, exactly:

```xml
            <muxc:NavigationView.FooterMenuItems>
                <muxc:NavigationViewItem x:Name="SettingsNavItem" Tag="settings">
                    <muxc:NavigationViewItem.Icon>
                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xE713;" />
                    </muxc:NavigationViewItem.Icon>
                </muxc:NavigationViewItem>
            </muxc:NavigationView.FooterMenuItems>
```

  with:

```xml
            <muxc:NavigationView.FooterMenuItems>
                <muxc:NavigationViewItem x:Name="SettingsNavItem" Tag="settings">
                    <muxc:NavigationViewItem.Icon>
                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xE713;" />
                    </muxc:NavigationViewItem.Icon>
                </muxc:NavigationViewItem>
                <!-- Advanced Mode only (design §6.6). COLLAPSED here rather than
                     absent: MainWindow::ApplyAdvancedMode flips it, and an item
                     created on demand would have no stable identity to compare
                     the current selection against. E713 is Setting, EC7A is
                     DeveloperTools. -->
                <muxc:NavigationViewItem x:Name="DeveloperNavItem" Tag="developer"
                                         Visibility="Collapsed">
                    <muxc:NavigationViewItem.Icon>
                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xEC7A;" />
                    </muxc:NavigationViewItem.Icon>
                </muxc:NavigationViewItem>
            </muxc:NavigationView.FooterMenuItems>
```

  and immediately after the `SettingsPage` grid A3 added:

```xml
                <Grid x:Name="DeveloperPage" Visibility="Collapsed" />
```

- [ ] **Step 7: declare the members.** In `app/src/App/MainWindow.xaml.h`, add
  `#include "Views/DeveloperView.h"` beside the `Views/SettingsView.h` include, then after
  `void BuildSettings();`:

```cpp
  // The Developer destination. Rebuilt by ApplyAdvancedMode, because contract
  // §4 gives DeveloperView no Set*Advanced and a state readout must not go
  // stale. Cheap: a static list of rows.
  void BuildDeveloper();
```

  and after `urmsg::views::SettingsView settings_{};`:

```cpp
  urmsg::views::DeveloperView developer_{};
```

- [ ] **Step 8: wire the destination.** In `app/src/App/MainWindow.xaml.cpp`:

  (a) add `#include "Views/DeveloperView.h"` beside the existing includes;

  (b) in `ApplyStrings()`, after the `SettingsNavItem().Content(...)` line (line 116), add:

```cpp
  // Demo copy, as an English literal: Strings/en/Resources.resw is generated
  // from urnetwork/localizations and this work adds no key (design §9.4).
  // ApplyStrings stays the ONE place a label is written, which is the property
  // that matters here.
  DeveloperNavItem().Content(box_value(winrt::hstring{L"Developer"}));
```

  (c) in the constructor, immediately after `BuildSettings();` and **before** A4's
  `urmsg::OnAdvancedModeChanged(...)` registration, add `BuildDeveloper();`. The full sequence is
  then:

```
InitializeComponent -> ExtendsContentIntoTitleBar/SetTitleBar
-> ParseDemoOptions -> InitAdvancedMode
-> ApplyStrings -> BuildConversationList -> BuildSettings -> BuildDeveloper
-> OnAdvancedModeChanged
-> reveal_.Bind / reveal_.Arm -> SizeChanged -> ApplyBreakpoint
-> ApplyAdvancedMode -> SelectDemoScreen -> LogInfo
```

  `BuildDeveloper` before the subscription, so the subscriber can never run against a
  default-constructed `DeveloperView`; `ApplyAdvancedMode` before `SelectDemoScreen`, so a deep
  link never selects a collapsed item.

  (d) add the definition after `MainWindow::BuildSettings()`:

```cpp
void MainWindow::BuildDeveloper() {
  developer_ = urmsg::views::MakeDeveloper(urmsg::demo::GetWorld());
  DeveloperPage().Children().Clear();
  DeveloperPage().Children().Append(developer_.root);
  urnw::LogInfo("window: developer pane built");
}
```

  (e) extend `ShowDestination`, additively — after the two `settings` lines A3 added:

```cpp
  const bool developer = (tag == L"developer");
  DeveloperPage().Visibility(developer ? Visibility::Visible : Visibility::Collapsed);
```

  change the `StubPage()` line to:

```cpp
  StubPage().Visibility((chats || settings || developer) ? Visibility::Collapsed
                                                        : Visibility::Visible);
```

  and after the `if (settings) { ... }` block add:

```cpp
  if (developer) {
    HomeNav().Header(box_value(winrt::hstring{L"Developer"}));
    return;
  }
```

  (f) extend `SelectDemoScreen`, adding after the Settings case:

```cpp
  if (screen == urmsg::demo::DemoScreen::Developer) {
    HomeNav().SelectedItem(DeveloperNavItem());
  }
```

- [ ] **Step 9: gate the nav item on Advanced Mode.** Replace `MainWindow::ApplyAdvancedMode`
  (A4 step 2) with:

```cpp
void MainWindow::ApplyAdvancedMode(bool advanced) {
  // Settings is NOT repainted from here: it owns the switch and follows itself.
  DeveloperNavItem().Visibility(advanced ? Visibility::Visible : Visibility::Collapsed);
  // The Developer page reports live state and contract §4 gives it no setter,
  // so it is re-snapshotted rather than patched. Both switches survive: each is
  // seeded from process state, not from the widget.
  BuildDeveloper();
  // Hiding the item the window is CURRENTLY on would leave a destination
  // showing with nothing selected in the rail and no way back, so the fallback
  // is written down rather than left to the control.
  if (!advanced) {
    auto selected = HomeNav().SelectedItem().try_as<NavigationViewItem>();
    if (selected && selected == DeveloperNavItem()) {
      HomeNav().SelectedItem(SettingsNavItem());
    }
  }
  urnw::LogInfo("window: advanced mode -> {}", advanced ? "on" : "off");
}
```

- [ ] **Step 10: build and look at the Developer page.** Run:

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=developer"
```

  Window size expected: **1560×900 DIP** under `--demo` (480×760 if the shell surface's D7 sizing
  has not landed — the pane content is the same and simply scrolls). Read
  `.verify/urmessage-window-screen.png`. Expected, concretely:

  - the pane header reads `DEVELOPER`;
  - group `DEMO STATE` with **10** key/value rows, in this order: `Demo world` = `on`,
    `Deep link (--demo=)` = `developer`, `Watermark` = `on`, `Advanced Mode` = `on`,
    `Ambient activity` = `off (no --demo-autoplay)`, `Motion` = `on (Windows)`,
    `Conversations` = `8`, `Message rows` = a number between **30 and 60** (design §5 says ~40
    messages plus day separators and system rows), `Linked devices` = `3`, `Current epoch` = a
    number greater than 0;
  - group `SWITCHES` with **2** rows, each visibly taller than a `DEMO STATE` row (44 against 34):
    `Pause ambient activity` with its switch **off**, `Animations` with its switch **on**, track
    `#638BFC`;
  - no visible text reads as a snake_case key id;
  - the `DEMO` watermark chip, if the shell surface has landed it.

  `Conversations` = 8 and `Linked devices` = 3 are design §5's stated contents and contract §1's
  asserted invariant 1. If either differs, that is a finding against the DemoWorld task, not
  something to relax here.

  If the window opens on **Chats** instead, `DeveloperNavItem` was still collapsed when the
  selection was attempted — check that `ApplyAdvancedMode(...)` precedes
  `SelectDemoScreen(...)` in the constructor (step 8c).

- [ ] **Step 11: prove the Advanced gate, in the capture that can show a nav rail.** Run:

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=settings"
```

  and read `.verify/urmessage-window-wide.png` — the script's final act resizes to 1200×800 DIP
  and captures again (`verify-render.ps1:274-297`), and 1200 ≥ 1008 puts `NavigationView` in its
  expanded pane with labels, which the 480-wide default cannot show. (That capture is
  `PrintWindow`, not screen capture; the warning in the script header is about system backdrops,
  and `MainWindow.xaml:22-25` states this window has none, so it is trustworthy for layout and
  text.) Expected: below `Settings` in the footer there is **no** `Developer` item.

  Then run:

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=settings --demo-advanced"
```

  Expected in `urmessage-window-wide.png`: a `Developer` item **is** present, directly below
  `Settings`, with the DeveloperTools glyph, and `Settings` is still the selected item (pale-yellow
  `#EFF7BB` indicator). Stated as a claim about the Developer item specifically, not as a footer
  count: a Network destination is coming from another surface and would change any count.

- [ ] **Step 12: commit.**

```
(git ls-files | Measure-Object -Line).Lines
git add app/src/App/Views/DeveloperView.h app/src/App/Views/DeveloperView.cpp app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp app/src/App/App.vcxproj
git commit -m @'
developer: the destination, the demo-state inspector and the two session switches

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
(git ls-files | Measure-Object -Line).Lines
```

  The counts must differ by exactly **2**.

**Deliverable:** `--demo=developer` opens a Developer pane with a 10-row state readout and two
switches, and the Developer nav item is present under `--demo-advanced` and absent without it.

---

## Task A6: Developer: the DemoWorld dump, with a completeness property in --diagnose

**Files:**

Modify app/src/App/Views/DeveloperView.h; Modify app/src/App/Views/DeveloperView.cpp; Modify app/src/App/App.xaml; Modify app/src/App/Startup.cpp

**Interfaces:**

- Consumes: Contract §1 Demo/DemoWorld.h in full — World, Conversation, MessageRow, MessageInspect, MemberRef, DeviceRef, RelayNode, ServerInfo, Seed and the five enums. A5's urmsg::views::MakeDeveloper() and its `// ---- A6 appends the DEMOWORLD DUMP group here` marker. A2's diagnostics loop in urnw::CollectDiagnostics() (this task appends a third loop beside it). urnw::kit::MakePaneGroupHeader's `trailing` Grid (UrComponents.h:358); App.xaml's UrPaneActionButtonStyle (:955) and UrIconFontFamily (:520). DataPackage/Clipboard (pch.h:22, pattern at UrComponents.cpp:780-783).
- Produces: std::wstring urmsg::views::DumpDemoWorld(); std::vector<std::wstring> urmsg::views::WorldDumpDiagnostics(). New App.xaml resource key: UrMonoFontFamily (FontFamily).

# A6 — Developer: the `DemoWorld` dump, with a completeness property in `--diagnose`

The last of design §6.6's four Developer items. A text rendering of the seeded world, field by
field, so that when a row on Chats looks wrong this page says whether the **row** is wrong or the
**world** is.

**What it claims, precisely.** It renders **every field contract §1 declares**, with the two 32-byte
`Seed` values as an 8-byte hex prefix so the lines stay readable, and the relay glyphs as their
codepoints rather than as characters. What `--diagnose` then asserts is a *property*: every
conversation and every row appears exactly once, and two calls are byte-identical. It does not
assert "complete", because no count can.

**Pre-apartment, deliberately.** `WorldDumpDiagnostics()` runs from `CollectDiagnostics()`, which
`main.cpp:168` calls on **every** launch and **before** `winrt::init_apartment` (`main.cpp:183`).
That is safe because contract §1 fixes `DemoWorld.h` as PURE C++ — no `winrt/` include, no XAML
type, in the header or its .cpp — for exactly this reason. The stated cost: the seeded world is
built, and its text rendered twice, on every launch. It is `std::wstring` work over ~50 rows and
is kept always-on so the world is checked on the launches an owner actually makes.

**Glyph rule, once, for the whole plan.** Every C++ glyph literal is written as a `\uXXXX` escape
with the icon's name in a trailing comment. Raw Segoe Fluent private-use characters do not survive
being copied through plan text, transcripts or a terminal — which is why `MainWindow.xaml` writes
its glyphs as `&#xE8BD;` and A5 does the same.

- [ ] **Step 1: add the mono face.** The dump is ids, hex and aligned figures; a proportional face
  makes it unreadable. In `app/src/App/App.xaml`, immediately after line 187
  (`<FontFamily x:Key="UrWordmarkFontFamily">...</FontFamily>`), add:

```xml
            <!-- The one monospaced face in the app, for raw ids, hex and dumps.
                 NOT a brand face: nothing user-facing in the product is set in
                 it. Cascadia Mono ships with Windows 11 and Consolas is the
                 floor, so the stack never falls through to a proportional face
                 the way a single unavailable family would. If the inspector
                 rail also wants a mono face for MessageInspect::groupIdHex it
                 REUSES this key — it must not be defined twice. -->
            <FontFamily x:Key="UrMonoFontFamily">Cascadia Mono, Consolas, Courier New</FontFamily>
```

- [ ] **Step 2: declare the dump API.** In `app/src/App/Views/DeveloperView.h`, add `#include
  <string>` and `#include <vector>` to the include block, then after the `MakeDeveloper`
  declaration:

```cpp
// The seeded world as text — every field contract §1 declares, with the two
// 32-byte Seed values as an 8-byte hex prefix and the relay glyphs as their
// codepoints. Pure: it takes nothing and touches no XAML, which is what lets
// --diagnose assert its shape without a window.
std::wstring DumpDemoWorld();

// The --diagnose assertions for the dump. Touches no global state and writes
// nothing, so it runs on every launch. Safe before winrt::init_apartment
// because contract §1 fixes DemoWorld.h as pure C++.
std::vector<std::wstring> WorldDumpDiagnostics();
```

- [ ] **Step 3: write the dump.** Append to `app/src/App/Views/DeveloperView.cpp`, **inside**
  `namespace urmsg::views` and above its closing brace. Add `#include <format>` to the file's
  includes first:

```cpp
namespace {

std::wstring_view KindName(demo::ConversationKind kind) {
  return kind == demo::ConversationKind::Group ? L"group" : L"direct";
}

// The row-line PREFIXES. Three literals, each three letters, so a count of them
// cannot be confused with a continuation line — see WorldDumpDiagnostics.
std::wstring_view RowTag(demo::RowKind kind) {
  switch (kind) {
    case demo::RowKind::Message: return L"msg";
    case demo::RowKind::DaySeparator: return L"day";
    case demo::RowKind::System: return L"sys";
  }
  return L"???";
}

std::wstring_view StateName(demo::DeliveryState state) {
  switch (state) {
    case demo::DeliveryState::Pending: return L"pending";
    case demo::DeliveryState::Sent: return L"sent";
    case demo::DeliveryState::Delivered: return L"delivered";
    case demo::DeliveryState::Read: return L"read";
    case demo::DeliveryState::Failed: return L"failed";
    case demo::DeliveryState::Expired: return L"expired";
  }
  return L"?";
}

std::wstring_view RetentionName(demo::RetentionClass retention) {
  return retention == demo::RetentionClass::Eph ? L"eph" : L"permanent";
}

std::wstring_view ConnectName(demo::ConnectState state) {
  switch (state) {
    case demo::ConnectState::Connected: return L"connected";
    case demo::ConnectState::Connecting: return L"connecting";
    case demo::ConnectState::Offline: return L"offline";
  }
  return L"?";
}

// 8 of the 32 bytes. Enough to tell two identicon seeds apart by eye, short
// enough that a conv line still fits a terminal.
std::wstring HexPrefix(demo::Seed const& seed) {
  std::wstring out;
  for (size_t i = 0; i < 8 && i < seed.size(); ++i)
    out += std::format(L"{:02x}", static_cast<unsigned>(seed[i]));
  return out;
}

// The relay glyphs are Segoe Fluent PRIVATE-USE codepoints. Printed as U+XXXX
// rather than as the character: a PUA glyph renders as a box everywhere this
// dump is pasted, and the codepoint is the fact worth checking. EMPTY is
// printed loudly because contract §1 invariant 7 forbids it.
std::wstring GlyphCode(std::wstring const& glyph) {
  if (glyph.empty()) return L"EMPTY";
  return std::format(L"U+{:04X}", static_cast<uint32_t>(glyph[0]));
}

}  // namespace

std::wstring DumpDemoWorld() {
  auto const& world = demo::GetWorld();
  std::wstring out;

  out += std::format(L"world  epoch={} conversations={} devices={} relay={} state={} "
                     L"session={} records/s={}\n",
                     world.currentEpoch, world.conversations.size(),
                     world.myDevices.size(), world.relayPath.size(),
                     ConnectName(world.connectState), world.sessionMode,
                     world.recordsPerSecond);
  out += std::format(L"server {}  {}  {} ms  key={}\n", world.server.host,
                     world.server.jurisdiction, world.server.latencyMs,
                     world.server.keyVerified ? L"verified" : L"unverified");
  for (auto const& d : world.myDevices) {
    out += std::format(L"device {} \"{}\" owner=\"{}\" key={} {} lastSeen=\"{}\" thisPc={}\n",
                       d.id, d.name, d.ownerName, HexPrefix(d.ownerKey),
                       d.online ? L"online" : L"offline", d.lastSeenLabel,
                       int(d.isThisComputer));
  }
  for (auto const& n : world.relayPath) {
    out += std::format(L"relay  \"{}\" / \"{}\" glyph={} {} ms healthy={}\n", n.label,
                       n.subLabel, GlyphCode(n.glyph), n.hopMs, int(n.healthy));
  }

  for (auto const& c : world.conversations) {
    // Line 1 starts "\nconv "; the continuation starts with FIVE spaces, so
    // neither can be mistaken for a "\n  msg "/"\n  day "/"\n  sys " row line.
    out += std::format(
        L"\nconv {} {} \"{}\" key={} groupId={} unread={} muted={} disappearing={} "
        L"members={}/{} rows={}\n"
        L"     retention=\"{}\" media=\"{}\" preview=\"{}\" time=\"{}\"\n",
        c.id, KindName(c.kind), c.name, HexPrefix(c.identityKey),
        c.groupIdHex.empty() ? std::wstring{L"-"} : c.groupIdHex, c.unread,
        int(c.muted), int(c.disappearing), c.memberCount, c.members.size(),
        c.rows.size(), c.retentionLabel, c.mediaRetentionLabel, c.preview,
        c.timeLabel);
    for (auto const& m : c.members) {
      out += std::format(L"  memb {} \"{}\" key={} admin={} devices={}\n", m.id,
                         m.displayName, HexPrefix(m.identityKey), int(m.admin),
                         m.devices.size());
      for (auto const& d : m.devices) {
        out += std::format(L"    dev {} \"{}\" {} \"{}\"\n", d.id, d.name,
                           d.online ? L"online" : L"offline", d.lastSeenLabel);
      }
    }
    for (auto const& r : c.rows) {
      out += std::format(
          L"  {} {} {} {} sender=\"{}\" key={} \"{}\" t=\"{}\" perm={} sys=\"{}\" "
          L"reason=\"{}\"\n"
          L"       epoch={} leaf={} ret={} size=\"{}\" wire={} att={} cipher=\"{}\" "
          L"gid={} from=\"{}\" sent=\"{}\" recv=\"{}\" delivered={} read={}\n",
          RowTag(r.kind), r.id, r.outgoing ? L"out" : L"in", StateName(r.state),
          r.senderName, HexPrefix(r.senderKey), r.body, r.timeLabel,
          int(r.permanentRecord), r.systemText, r.failureReason, r.inspect.epoch,
          r.inspect.senderLeafIndex, RetentionName(r.inspect.retention),
          r.inspect.sizeBucket, r.inspect.wireSizeBytes,
          int(r.inspect.attestationVerified), r.inspect.cipher,
          r.inspect.groupIdHex.empty() ? std::wstring{L"-"} : r.inspect.groupIdHex,
          r.inspect.senderDisplayName, r.inspect.sentAtLabel,
          r.inspect.receivedAtLabel, r.inspect.deliveredTo.size(),
          r.inspect.readBy.size());
    }
  }
  return out;
}
```

- [ ] **Step 4: write the dump's assertions.** Append to `DeveloperView.cpp`, inside
  `namespace urmsg::views`:

```cpp
std::vector<std::wstring> WorldDumpDiagnostics() {
  std::vector<std::wstring> lines;
  auto const& world = demo::GetWorld();

  size_t expectedRows = 0;
  for (auto const& c : world.conversations) expectedRows += c.rows.size();

  const std::wstring dump = DumpDemoWorld();

  auto count = [&dump](std::wstring_view prefix) {
    size_t n = 0, pos = 0;
    while ((pos = dump.find(prefix, pos)) != std::wstring::npos) {
      ++n;
      pos += prefix.size();
    }
    return n;
  };

  // EXACTLY these three prefixes, and nothing else in the dump begins with
  // them: a conversation's continuation line starts with five spaces, a member
  // line with "  memb " and a device line with "    dev ". Counting "\n  "
  // alone would double-count the continuation lines and the >= that hid it.
  const size_t convLines = count(L"\nconv ");
  const size_t rowLines =
      count(L"\n  msg ") + count(L"\n  day ") + count(L"\n  sys ");

  // The dump is built from the SEEDED world (design §5, contract §1 invariant
  // 10), so two calls in one process must be byte-identical.
  const bool stable = (dump == DumpDemoWorld());

  // EQUALITY, not >=. A dump that silently dropped rows must fail here; that is
  // the whole reason this assertion exists.
  const bool ok = (convLines == world.conversations.size()) &&
                  (rowLines == expectedRows) && stable && !dump.empty();
  lines.push_back(std::format(
      L"  dump.world       : {} {} conv lines == {} conversations; {} row lines == "
      L"{} rows; stable across two calls: {}   [query: occurrences of \"\\nconv \" "
      L"and of \"\\n  msg \"+\"\\n  day \"+\"\\n  sys \"]",
      ok ? L"PASS" : L"FAIL", convLines, world.conversations.size(), rowLines,
      expectedRows, stable ? L"yes" : L"NO"));
  return lines;
}
```

- [ ] **Step 5: run the assertion, and SEE the count.** In `app/src/App/Startup.cpp` add
  `#include "Views/DeveloperView.h"` beside the two `Demo/` includes, and directly under A2's
  `DeveloperSwitchDiagnostics()` loop add:

```cpp
  for (auto& line : urmsg::views::WorldDumpDiagnostics())
    lines.push_back(std::move(line));
```

  Then run:

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
app\build\x64\Release\URmessage.exe --diagnose
```

  Expected — one line, PASS, `8` conversations per design §5, and a row count that is the sum of
  `Conversation::rows.size()` (the same number A5's Developer page shows as `Message rows`):

```
  dump.world       : PASS 8 conv lines == 8 conversations; 41 row lines == 41 rows; stable across two calls: yes   [query: occurrences of "\nconv " and of "\n  msg "+"\n  day "+"\n  sys "]
```

  The two figures in each pair must be **equal**; `41` is whatever the DemoWorld task seeded and
  is not itself the thing under test. A FAIL with the left figure smaller means the dump dropped
  rows; `stable: NO` means `GetWorld()` is not deterministic, which is a DemoWorld defect.

- [ ] **Step 6: build the panel.** In `MakeDeveloper()`, replace the marker line A5 left,
  `  // ---- A6 appends the DEMOWORLD DUMP group here ---------------------------`, with:

```cpp
  // ---- DEMOWORLD DUMP -----------------------------------------------------
  {
    auto group = kit::MakePaneGroupHeader(S(L"DEMOWORLD DUMP"));
    // The group header's `trailing` slot is the documented home for an
    // icon-only command (UrComponents.h:356-359), so the copy button costs no
    // row. UrPaneActionButtonStyle's own note: every instance MUST carry
    // AutomationProperties.Name — a glyph is not a name.
    Button copy;
    if (auto style = StyleByKey(L"UrPaneActionButtonStyle")) copy.Style(style);
    FontIcon glyph;
    if (auto family = FontFamilyByKey(L"UrIconFontFamily")) glyph.FontFamily(family);
    glyph.Glyph(L"");  // Copy — the same codepoint as UrComponents.cpp:773
    glyph.FontSize(14);
    glyph.Foreground(urnw::colors::MutedBrush());
    copy.Content(glyph);
    automation::AutomationProperties::SetName(copy, S(L"Copy the DemoWorld dump"));
    copy.Click([](auto const&, auto const&) {
      winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
      package.SetText(winrt::hstring{DumpDemoWorld()});
      winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    });
    group.trailing.Children().Append(copy);
    rows.Append(group.root);

    TextBlock dump;
    if (auto family = FontFamilyByKey(L"UrMonoFontFamily")) dump.FontFamily(family);
    dump.FontSize(11);
    dump.Foreground(urnw::colors::MutedBrush());
    dump.IsTextSelectionEnabled(true);
    dump.TextWrapping(TextWrapping::NoWrap);
    dump.Text(winrt::hstring{DumpDemoWorld()});

    // The dump is WIDE and the pane must not be. Its own horizontal scroller
    // keeps the long lines readable without widening the destination.
    ScrollViewer dumpScroller;
    dumpScroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    dumpScroller.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
    dumpScroller.HorizontalScrollMode(ScrollMode::Enabled);
    dumpScroller.VerticalScrollMode(ScrollMode::Disabled);
    dumpScroller.Padding(ThicknessHelper::FromLengths(12, 8, 12, 12));
    dumpScroller.Content(dump);
    rows.Append(dumpScroller);
  }
```

  and add `FontFamilyByKey` beside `StyleByKey` in `DeveloperView.cpp`'s anonymous namespace:

```cpp
// The FontFamily twin of StyleByKey, and it exists for the same reason:
// ResourceDictionary::Lookup THROWS hresult_out_of_bounds on a missing key. An
// unguarded Lookup here would throw out of MakeDeveloper, out of
// BuildDeveloper, out of the MainWindow constructor, and kill the app at launch
// with a message about %LOCALAPPDATA%. Guarded, a missing key costs the face
// and nothing else — which is what makes step 7's "renders in a proportional
// face" a true diagnosis.
Media::FontFamily FontFamilyByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Media::FontFamily>();
}
```

  No extra include is needed: `winrt/Microsoft.UI.Xaml.Media.h` is in `pch.h` at line 33, and
  `winrt/Windows.ApplicationModel.DataTransfer.h` at line 22.

- [ ] **Step 7: build and look.** Run:

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=developer"
```

  Window size expected: **1560×900 DIP** under `--demo` (480×760 without the shell surface's D7
  sizing — the dump group is then below the fold and must be scrolled to). Read
  `.verify/urmessage-window-screen.png`. Expected, concretely:

  - a third group header, `DEMOWORLD DUMP`, below `SWITCHES`, with a small copy glyph at its
    right edge — a 28×28 button, not an empty gap;
  - under it, monospaced text at ~11 px in muted grey `#989898`, **left-aligned and not wrapped**
    — every glyph the same width, unmistakably different from the proportional rows above;
  - the first two lines read
    `world  epoch=<n> conversations=8 devices=3 relay=3 state=connected session=direct records/s=<n>`
    and `server <host>  <jurisdiction>  <n> ms  key=verified`, with host and jurisdiction as the
    DemoWorld task seeded them;
  - the first `relay` line ends `glyph=U+E770` (or whichever codepoint that task chose) and
    **never** `glyph=EMPTY` — contract §1 invariant 7;
  - a horizontal scrollbar under the dump, because the `conv` lines are wider than the pane;
  - `DEMO STATE` and `SWITCHES` above are unchanged.

  If the dump renders in a proportional face, `UrMonoFontFamily` did not resolve — the guarded
  lookup returned null rather than throwing, so check the key is spelled identically in `App.xaml`
  and in the `FontFamilyByKey` call.

- [ ] **Step 8: cross-check the page against the world it describes.** Compare the screenshot's
  `DEMO STATE` row `Conversations` with the dump's `conversations=` on line 1, and `Message rows`
  with the `41 row lines` figure from step 5. All three read the same `GetWorld()`; a mismatch
  means one of them is caching, and the page is then lying about the thing it exists to report.

- [ ] **Step 9: commit.**

```
(git ls-files | Measure-Object -Line).Lines
git add app/src/App/Views/DeveloperView.h app/src/App/Views/DeveloperView.cpp app/src/App/App.xaml app/src/App/Startup.cpp
git commit -m @'
developer: the DemoWorld dump, with a completeness property asserted in --diagnose

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
'@
(git ls-files | Measure-Object -Line).Lines
```

  The counts must be **identical** — no new files.

**Deliverable:** `--diagnose` prints `dump.world : PASS` with equal figures on both sides of each
pair and the query printed beside them, and `--demo=developer` renders every contract §1 field of
the seeded world in a monospaced face under a working copy button.
