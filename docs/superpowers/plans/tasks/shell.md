# The demo shell

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task W1: DemoShellState.h: the composer's two pure decisions, asserted as BOUNDARIES in --diagnose

**Files:**

Create app/src/App/Demo/DemoShellState.h; Modify app/src/App/App.vcxproj; Modify app/src/App/Startup.cpp

**Interfaces:**

- Consumes: app/src/App/Demo/DemoSwitches.h — `urmsg::demo::DemoScreen` (switches surface, contract v2 §2); app/src/App/UrComponents.h:75 — `urnw::kit::kWideBreakpointDip` (= 1000.0, verified); app/src/App/Startup.cpp:178 `urnw::CollectDiagnostics()` and its anonymous namespace
- Produces: `inline constexpr double urmsg::demo::kRailBreakpointDip = 1500.0;` `inline constexpr double urmsg::demo::kStripMinHeightDip = 560.0;` `inline constexpr double urmsg::demo::kRailWidthDip = 360.0;` `struct urmsg::demo::Layout { bool wide; bool rail; bool strip; };` `urmsg::demo::Layout urmsg::demo::LayoutFor(double contentWidthDip, double contentHeightDip);` `struct urmsg::demo::DeepLink { std::wstring_view navTag; bool selectConversation; bool selectMessage; bool forceAdvanced; };` `urmsg::demo::DeepLink urmsg::demo::DeepLinkFor(urmsg::demo::DemoScreen screen);`

## Task W1: the composer's pure decisions

`MainWindow` makes exactly two decisions that are not about pixels: what the current size
means, and where `--demo=<screen>` lands. Both go in a header of their own as pure
functions so `URmessage.exe --diagnose` can assert them without a window. There is no test
project in this repo; a breakpoint only ever exercised by dragging a window edge is a
breakpoint nobody has checked.

**The unit is CONTENT-ROOT dips, not window dips.** `ApplyBreakpoint` feeds
`Content().ActualWidth()`. The shipped log at `.localstate-verify/logs/urmessage-app.log`
proves the gap: a 480-dip window logs `breakpoint -> narrow (466 dip)` and the 1200-dip
window `verify-render.ps1` resizes to logs `wide (1186 dip)` — about 14 dip of frame. So
the assertions below test the **boundaries of the function**, not four sampled window
sizes: a boundary property survives the window/content distinction, and a sampled size does
not.

Steps 1–5 are the RED half: the header ships deliberately-wrong stubs and both assertions
print FAIL. Step 6 makes them PASS. No stub survives this task.

**Every `--diagnose` run in this plan sets `URMESSAGE_APP_ROOT` first**, so an assertion can
never read or write `%LOCALAPPDATA%\URmessage\app`. That is the same redirection
`verify-render.ps1:153` performs.

- [ ] **Step 1: Create `app/src/App/Demo/DemoShellState.h` with the types and WRONG stubs.**

```cpp
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

struct Layout {
  bool wide;   // the thread pane sits beside the list
  bool rail;   // the 360dip inspector rail exists
  bool strip;  // the status strip exists
};

inline Layout LayoutFor(double contentWidthDip, double contentHeightDip) {
  // STUB - deliberately wrong, replaced in step 6.
  (void)contentWidthDip;
  (void)contentHeightDip;
  return Layout{false, false, false};
}

// What a `--demo=<screen>` deep link means to the composer. `inspect` is not a
// separate screen: it is `thread` with a message pre-selected and the rail
// already in message mode, which is the state a screenshot needs (contract v2
// section 2, design doc 8).
struct DeepLink {
  std::wstring_view navTag;  // "chats" | "network" | "settings" | "developer"
  bool selectConversation;   // open conversation 0
  bool selectMessage;        // and pre-select its newest Message row
  bool forceAdvanced;        // Developer exists only under Advanced Mode
};

inline DeepLink DeepLinkFor(DemoScreen screen) {
  // STUB - deliberately wrong, replaced in step 6.
  (void)screen;
  return DeepLink{L"", false, false, false};
}

}  // namespace urmsg::demo
```

- [ ] **Step 2: Register the header in `app/src/App/App.vcxproj`.**

In the `<ItemGroup>` that lists `<ClInclude>` entries (App.vcxproj:188-198), immediately
after `<ClInclude Include="Localization.h" />`, add:

```xml
    <ClInclude Include="Demo\DemoShellState.h" />
```

`$(MSBuildProjectDirectory)` is already on `AdditionalIncludeDirectories`
(App.vcxproj:125), so `#include "Demo/DemoShellState.h"` resolves with no further project
change.

- [ ] **Step 3: Add the include and the two boundary checks to `app/src/App/Startup.cpp`.**

Add to the include block, after `#include "Strings.h"`:

```cpp
#include "Demo/DemoShellState.h"
```

Then, inside the file's existing anonymous `namespace { ... }` (it closes at Startup.cpp:160
with `}  // namespace`), just above that closing brace, add:

```cpp
// ---- demo composer invariants ----------------------------------------------
//
// This repo has no test project, so these lines ARE the tests for the composer's
// pure decisions. Each prints PASS or FAIL and prints the QUERY beside it, so a
// reader can tell what was actually checked rather than trusting the word PASS.
//
// BOUNDARIES, not samples. LayoutFor is fed CONTENT-root dips at runtime and a
// sampled window size ("1560x900 -> rail") would be asserting a different
// quantity from the one the app computes. `f(t) && !f(t - epsilon)` is true of
// the function whatever the frame inset is.
std::wstring DemoLayoutCheck() {
  using urmsg::demo::LayoutFor;
  const bool wideEdge = LayoutFor(1000.0, 800.0).wide && !LayoutFor(999.9, 800.0).wide;
  const bool railEdge = LayoutFor(1500.0, 800.0).rail && !LayoutFor(1499.9, 800.0).rail;
  const bool stripEdge = LayoutFor(1200.0, 560.0).strip && !LayoutFor(1200.0, 559.9).strip;
  // width and height decide different things and must not leak into each other.
  const bool axes = LayoutFor(1600.0, 400.0).rail && !LayoutFor(1600.0, 400.0).strip &&
                    LayoutFor(800.0, 900.0).strip && !LayoutFor(800.0, 900.0).wide;
  const bool ok = wideEdge && railEdge && stripEdge && axes;
  return std::format(
      L"  demo layout      : {}  (CONTENT dips. wide@1000 {} | rail@1500 {} | "
      L"strip@560h {} | width/height independent {})",
      ok ? L"PASS" : L"FAIL", wideEdge, railEdge, stripEdge, axes);
}

std::wstring DemoDeepLinkCheck() {
  using urmsg::demo::DeepLinkFor;
  using urmsg::demo::DemoScreen;
  int checked = 0;
  auto tag = [&](DemoScreen s, std::wstring_view expect) {
    ++checked;
    return DeepLinkFor(s).navTag == expect;
  };
  const bool tags = tag(DemoScreen::None, L"chats") && tag(DemoScreen::Chats, L"chats") &&
                    tag(DemoScreen::Thread, L"chats") && tag(DemoScreen::Inspect, L"chats") &&
                    tag(DemoScreen::Network, L"network") &&
                    tag(DemoScreen::Settings, L"settings") &&
                    tag(DemoScreen::Developer, L"developer");
  // inspect is thread PLUS a message; thread is not.
  const bool ladder = !DeepLinkFor(DemoScreen::Chats).selectConversation &&
                      DeepLinkFor(DemoScreen::Thread).selectConversation &&
                      !DeepLinkFor(DemoScreen::Thread).selectMessage &&
                      DeepLinkFor(DemoScreen::Inspect).selectConversation &&
                      DeepLinkFor(DemoScreen::Inspect).selectMessage;
  // Developer is the ONLY screen that turns Advanced Mode on by existing.
  const bool advanced = DeepLinkFor(DemoScreen::Developer).forceAdvanced &&
                        !DeepLinkFor(DemoScreen::Network).forceAdvanced &&
                        !DeepLinkFor(DemoScreen::Settings).forceAdvanced &&
                        !DeepLinkFor(DemoScreen::Inspect).forceAdvanced;
  const bool ok = tags && ladder && advanced;
  return std::format(
      L"  demo deep link   : {}  ({} nav tags checked {} | thread=conv, "
      L"inspect=conv+msg {} | only developer forces advanced {})",
      ok ? L"PASS" : L"FAIL", checked, tags, ladder, advanced);
}
```

- [ ] **Step 4: Emit the two lines from `CollectDiagnostics()`.**

In `app/src/App/Startup.cpp`, immediately before the `return lines;` that closes
`CollectDiagnostics()` (after the `fonts` push_back), add:

```cpp
  lines.push_back(DemoLayoutCheck());
  lines.push_back(DemoDeepLinkCheck());
```

- [ ] **Step 5: Build and SEE both lines FAIL.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

then, in PowerShell from the repo root:

```powershell
$repo = (Resolve-Path .).Path
$env:URMESSAGE_APP_ROOT = "$repo\.localstate-verify"
New-Item -ItemType Directory -Force "$repo\.verify" | Out-Null
Start-Process -FilePath "$repo\app\build\x64\Release\URmessage.exe" -ArgumentList '--diagnose' -Wait -RedirectStandardOutput "$repo\.verify\diagnose.txt" -RedirectStandardError "$repo\.verify\diagnose.err.txt"
Get-Content "$repo\.verify\diagnose.txt" | Select-String 'demo '
```

Expected (the RED step — both must say FAIL, and the query text names what failed):

```
  demo layout      : FAIL  (CONTENT dips. wide@1000 false | rail@1500 false | strip@560h false | width/height independent false)
  demo deep link   : FAIL  (7 nav tags checked false | thread=conv, inspect=conv+msg false | only developer forces advanced false)
```

- [ ] **Step 6: Replace both stubs in `Demo/DemoShellState.h` with the real bodies.**

```cpp
inline Layout LayoutFor(double contentWidthDip, double contentHeightDip) {
  return Layout{
      /*wide=*/urnw::kit::kWideBreakpointDip <= contentWidthDip,
      /*rail=*/kRailBreakpointDip <= contentWidthDip,
      /*strip=*/kStripMinHeightDip <= contentHeightDip,
  };
}

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
```

- [ ] **Step 7: Rebuild and SEE both PASS.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

then the same PowerShell block as step 5. Expected, verbatim:

```
  demo layout      : PASS  (CONTENT dips. wide@1000 true | rail@1500 true | strip@560h true | width/height independent true)
  demo deep link   : PASS  (7 nav tags checked true | thread=conv, inspect=conv+msg true | only developer forces advanced true)
```

- [ ] **Step 8: Commit.**

```
git add app/src/App/Demo/DemoShellState.h app/src/App/App.vcxproj app/src/App/Startup.cpp
git commit -m "demo: composer layout and deep-link decisions, asserted as boundaries in --diagnose"
```

**Deliverable:** `--diagnose` prints two PASS lines that both printed FAIL one edit ago, the
lines print the QUERY beside the verdict, and no stub remains in `DemoShellState.h`.

---

## Task W3: MainWindow demo shell: hosts, the DEMO chip, the two new destinations, and the 1560x900 launch size

**Files:**

Modify app/src/App/MainWindow.xaml; Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp; Modify app/src/App/App.xaml.cpp

**Interfaces:**

- Consumes: `urmsg::demo::ParseDemoOptions()`, `urmsg::demo::DemoOptions`, `urmsg::demo::DemoScreen` (Demo/DemoSwitches.h, switches surface, contract v2 §2); `urmsg::demo::DeepLinkFor` (W1); `urmsg::InitAdvancedMode`, `urmsg::AdvancedModeEnabled` (W2); `urnw::motion::CrossfadePageSwap` (UrMotion.h:120, verified safe with outgoing==incoming at UrMotion.cpp:123); `urnw::LogInfo` (Common/Log.h:36); `urnw::Narrow` (Common/Strings.h); `winrt::Microsoft::UI::GetWindowIdFromWindow` / `Microsoft::UI::Windowing::AppWindow::GetFromWindowId` (used already at WindowShell.cpp:256-257)
- Produces: XAML names on MainWindow: `ListScaffold`, `ListHost`, `ThreadHost`, `RailRule`, `RailHost`, `RailRuleColumn`, `RailColumn`, `NetworkHost`, `SettingsHost`, `DeveloperHost`, `StatusStripHost`, `NetworkNavItem`, `DeveloperNavItem`, `DemoChip`, `DemoChipText`. On `implementation::MainWindow`: public `urmsg::demo::DemoOptions const& DemoOptions() const;` private `void EnterDemoMode();` `void SelectNavTag(std::wstring_view tag);` `void DrainDeepLink();` `void ShowDestination(std::wstring_view tag);` (rewritten); members `options_`, `advanced_`, `currentTag_`, `currentPage_`, `pendingLink_`, `pendingLinkArmed_`

## Task W3: the frame the views hang in

`MainWindow` becomes the thin composer of design doc §4: empty hosts, the two nav
destinations the demo adds, the `DEMO` watermark chip, and the deep link that reaches a
surface. No view is built yet — this task is verifiable on its own because an empty frame
at 1560×900 with four nav items either renders or does not.

Every demo host defaults to `Collapsed`. A launch without `--demo` must land on the same
settled frame it lands on today.

**Three conventions this surface uses everywhere, stated once here.**

1. **`-AppArgs`.** `verify-render.ps1` has *no* argument passthrough today (its param block
   is `[CmdletBinding()] param($Configuration,$Platform,$SettleMs)` and line 156 is
   `Start-Process -FilePath $exe -PassThru`). **Task F1 of the switches/tooling surface adds
   `-AppArgs`** — never `-Args`, which shadows the automatic `$args`. Every capture below
   passes ONE string; two switches go in that one string separated by a space, e.g.
   `-AppArgs "--demo=inspect --demo-advanced"`, which `-ArgumentList` splits. **If F1 has
   not landed, these commands fail with a parameter-binding error and there is nothing to
   work around in this surface.**
2. **Evidence paths.** `verify-render.ps1:153` sets
   `$env:URMESSAGE_APP_ROOT = <repo>\.localstate-verify`, so under the harness the log is
   `.localstate-verify\logs\urmessage-app.log` and prefs are
   `.localstate-verify\app_prefs.json` (`Paths.cpp:35-54`, `Ids.h:72`). Not
   `%LOCALAPPDATA%`, and not `app.log`. All grep commands are PowerShell `Select-String
   -SimpleMatch` — `findstr` ORs a space-separated argument, and `->` is a regex to
   `Select-String` without `-SimpleMatch`.
3. **Which capture to read.** `.verify\urmessage-window.png` is `PrintWindow`
   (verify-render.ps1:227) — immune to z-order and to a `Shell_SystemDialogProxy` dimming
   the desktop, and faithful for this app because it has no system backdrop. **Read it for
   every colour claim and every A/B comparison.** `.verify\urmessage-window-screen.png` is
   `CopyFromScreen` and is only trustworthy when the console printed
   `foreground  : YES`. `.verify\urmessage-window-wide.png` is the second capture the same
   run takes after resizing to a 1200×800 **window** (≈1186×792 content dips).

- [ ] **Step 1: Add the rail columns to `ChatsPage` in `app/src/App/MainWindow.xaml`.**

Replace `ChatsPage`'s column definitions (MainWindow.xaml:129-133) with:

```xml
                    <!-- Five columns, two breakpoints. 0/1/2 are the shipped
                         list | rule | thread. 3/4 are the demo's inspector rail
                         (design doc 6.3), which exists only at or above
                         urmsg::demo::kRailBreakpointDip = 1500 CONTENT dips. -->
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition x:Name="ListColumn" Width="*" />
                        <ColumnDefinition x:Name="RuleColumn" Width="Auto" />
                        <ColumnDefinition x:Name="ThreadColumn" Width="0" />
                        <ColumnDefinition x:Name="RailRuleColumn" Width="Auto" />
                        <ColumnDefinition x:Name="RailColumn" Width="0" />
                    </Grid.ColumnDefinitions>
```

- [ ] **Step 2: Name the shipped list pane and add `ListHost` beside it (same file).**

Change MainWindow.xaml:136 from

```xml
                    <Grid Grid.Column="0" Style="{StaticResource UrPaneStyle}">
```

to

```xml
                    <Grid x:Name="ListScaffold" Grid.Column="0" Style="{StaticResource UrPaneStyle}">
```

Then, immediately after that Grid's closing `</Grid>` (MainWindow.xaml:162) and before
`<Border x:Name="PaneRule" ...>`, add:

```xml
                    <!-- The demo's ConversationListView lands here and
                         ListScaffold collapses. Two siblings in one cell rather
                         than a rebuilt pane: a launch without --demo must draw
                         exactly what it draws today. -->
                    <Grid x:Name="ListHost" Grid.Column="0" Visibility="Collapsed"
                          Style="{StaticResource UrPaneStyle}" />
```

- [ ] **Step 3: Add the thread and rail hosts (same file).**

After the closing `</Grid>` of `<Grid x:Name="ThreadPane" Grid.Column="2" ...>`
(MainWindow.xaml:180) and before `ChatsPage`'s own `</Grid>`, add:

```xml
                    <!-- the demo's ThreadView -->
                    <Grid x:Name="ThreadHost" Grid.Column="2" Visibility="Collapsed"
                          Style="{StaticResource UrPaneStyle}" />

                    <!-- the inspector rail (design doc 6.3, D3) -->
                    <Border x:Name="RailRule" Grid.Column="3"
                            Style="{StaticResource UrPaneVRuleStyle}"
                            Visibility="Collapsed" />
                    <Grid x:Name="RailHost" Grid.Column="4" Visibility="Collapsed"
                          Style="{StaticResource UrPaneStyle}" />
```

- [ ] **Step 4: Add the three demo destination hosts (same file).**

Immediately after the closing `</Grid>` of `<Grid x:Name="StubPage" ...>`
(MainWindow.xaml:198), add:

```xml
                <!-- The demo destinations. Each is one empty pane that its view
                     fills; MainWindow never draws into them itself. -->
                <Grid x:Name="NetworkHost" Visibility="Collapsed"
                      Style="{StaticResource UrPaneStyle}" />
                <Grid x:Name="SettingsHost" Visibility="Collapsed"
                      Style="{StaticResource UrPaneStyle}" />
                <Grid x:Name="DeveloperHost" Visibility="Collapsed"
                      Style="{StaticResource UrPaneStyle}" />
```

- [ ] **Step 5: Add the status-strip row at window level (same file).**

Replace `RevealRoot`'s row definitions (MainWindow.xaml:38-41) with:

```xml
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto" />
            <RowDefinition Height="*" />
            <RowDefinition Height="Auto" />
        </Grid.RowDefinitions>
```

and immediately after the `</muxc:NavigationView>` closing tag (MainWindow.xaml:201) add:

```xml
        <!-- The persistent connect indicator (design doc 6.5, D4). Window level,
             not per-destination: it is one strip along the bottom of the app,
             and its preview drawer opens ABOVE it inside this same host. -->
        <Grid x:Name="StatusStripHost" Grid.Row="2" Visibility="Collapsed" />
```

- [ ] **Step 6: Add the two demo nav items and the DEMO chip (same file).**

After `ContactsNavItem`'s closing `</muxc:NavigationViewItem>` (MainWindow.xaml:106),
inside `<muxc:NavigationView.MenuItems>`, add:

```xml
                <!-- Demo destinations, hidden on a normal launch. Segoe Fluent
                     Icons through UrIconFontFamily like every other glyph in
                     this file: E839 is Ethernet, E943 is Code. -->
                <muxc:NavigationViewItem x:Name="NetworkNavItem" Tag="network"
                                         Visibility="Collapsed">
                    <muxc:NavigationViewItem.Icon>
                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xE839;" />
                    </muxc:NavigationViewItem.Icon>
                </muxc:NavigationViewItem>
```

Inside `<muxc:NavigationView.FooterMenuItems>`, BEFORE `SettingsNavItem`
(MainWindow.xaml:109), add:

```xml
                <muxc:NavigationViewItem x:Name="DeveloperNavItem" Tag="developer"
                                         Visibility="Collapsed">
                    <muxc:NavigationViewItem.Icon>
                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xE943;" />
                    </muxc:NavigationViewItem.Icon>
                </muxc:NavigationViewItem>
```

And inside `AppTitleBar`'s StackPanel, immediately after `<TextBlock x:Name="BrandText" ...>`
(MainWindow.xaml:62), add the watermark:

```xml
                <!-- D2. It exists so an unpatched screenshot cannot be mistaken
                     for a shipping product; --demo-watermark=off removes it for
                     clean capture. The parent StackPanel is
                     IsHitTestVisible=False, so this cannot eat the drag region.
                     Text is set from code as an English literal: Resources.resw
                     is GENERATED from urnetwork/localizations and no task here
                     may add a key to it. -->
                <Border x:Name="DemoChip" Visibility="Collapsed"
                        Background="{StaticResource UrSheetBrush}"
                        BorderBrush="{StaticResource UrBorderBrush}"
                        BorderThickness="1" CornerRadius="4" Padding="6,1"
                        Margin="4,0,0,0" VerticalAlignment="Center">
                    <TextBlock x:Name="DemoChipText"
                               Style="{StaticResource UrCaptionTextStyle}" />
                </Border>
```

- [ ] **Step 7: Declare the members and methods in `app/src/App/MainWindow.xaml.h`.**

Add to the include block, after `#include "UrComponents.h"`:

```cpp
#include "Demo/AdvancedMode.h"
#include "Demo/DemoShellState.h"
#include "Demo/DemoSwitches.h"
```

Add to the **public** section, immediately after `void StartReveal();`:

```cpp
  // Read back by App::OnLaunched to choose the launch size, so the command line
  // is parsed EXACTLY ONCE. Not in MainWindow.idl: App.xaml.cpp already reaches
  // the implementation through winrt::get_self for StartReveal().
  urmsg::demo::DemoOptions const& DemoOptions() const { return options_; }
```

Add to `private:`, above `urnw::WindowReveal reveal_;`:

```cpp
  // The demo composer. Everything below is inert without --demo: the hosts stay
  // collapsed and the window draws what it drew before.
  void EnterDemoMode();
  void SelectNavTag(std::wstring_view tag);
  // The deep link runs LATE, not from the constructor: EnterDemoMode runs before
  // any layout pass (Content().ActualWidth() is 0, so ApplyBreakpoint has never
  // written the layout) and before App::OnLaunched resizes to 1560x900, and
  // NavigationView re-asserts the markup's IsSelected when it loads. Draining it
  // from the first SizeChanged puts it after all three.
  void DrainDeepLink();

  urmsg::demo::DemoOptions options_{};
  bool advanced_ = false;
  std::wstring currentTag_ = L"chats";
  winrt::Microsoft::UI::Xaml::FrameworkElement currentPage_{nullptr};
  urmsg::demo::DeepLink pendingLink_{};
  bool pendingLinkArmed_ = false;
```

- [ ] **Step 8: Wire the constructor in `app/src/App/MainWindow.xaml.cpp`.**

Add to the include block, after `#include "UrColors.h"`:

```cpp
#include "Demo/AdvancedMode.h"
#include "Demo/DemoShellState.h"
#include "Demo/DemoSwitches.h"
#include "Strings.h"
#include "UrMotion.h"
```

Replace, in the constructor (MainWindow.xaml.cpp:76-77):

```cpp
  ApplyStrings();
  BuildConversationList();
```

with:

```cpp
  options_ = urmsg::demo::ParseDemoOptions();
  ApplyStrings();
  // Seeded before anything can navigate: CrossfadePageSwap with a null outgoing
  // fades the incoming page in and never collapses the old one, so both would be
  // drawn on top of each other.
  currentPage_ = ChatsPage();
  if (options_.enabled) {
    EnterDemoMode();
  } else {
    BuildConversationList();
  }
```

Change the reveal binding (MainWindow.xaml.cpp:84-90) so its second ring rides the host
that is actually on screen:

```cpp
  const FrameworkElement listRing = options_.enabled
                                        ? ListHost().as<FrameworkElement>()
                                        : ConversationList().as<FrameworkElement>();
  reveal_.Bind(WindowPlate(), RevealRoot(),
               {
                   {ListPaneTitle(), 0},
                   {listRing, 0},
                   {HomeNav(), 40},
                   {AppTitleBar(), 80},
               });
```

And replace the SizeChanged registration (MainWindow.xaml.cpp:96-100) with:

```cpp
  if (auto root = Content().try_as<FrameworkElement>()) {
    root.SizeChanged([weak = get_weak()](auto const&, auto const&) {
      auto self = weak.get();
      if (!self) return;
      self->ApplyBreakpoint();
      // AFTER ApplyBreakpoint, never before: the deep link's message half needs
      // the rail to exist, and the rail is decided by the line above.
      self->DrainDeepLink();
    });
  }
```

- [ ] **Step 9: Write `EnterDemoMode()`, `SelectNavTag()` and `DrainDeepLink()` in `MainWindow.xaml.cpp`.**

Add to the file's anonymous namespace, under the `Loc` helper (MainWindow.xaml.cpp:31):

```cpp
// Demo-only labels. Strings/en/Resources.resw is GENERATED from the separate
// urnetwork/localizations repository (Localization.h:3-4), so a demo surface
// cannot add keys to it and must not hand-edit it. These literals ship only
// behind --demo and sit in one block for the same reason the demo world is one
// directory: the diff that deletes the demo must be short.
constexpr const wchar_t* kDemoNavNetwork = L"Network";
constexpr const wchar_t* kDemoNavDeveloper = L"Developer";
constexpr const wchar_t* kDemoWatermark = L"DEMO";
```

Add these three methods after `BuildConversationList()`:

```cpp
void MainWindow::EnterDemoMode() {
  const auto link = urmsg::demo::DeepLinkFor(options_.screen);

  // Advanced Mode resolves FIRST: the Developer nav item and every view built
  // below depends on it. --demo-advanced and a --demo=developer deep link are
  // both session-only and write nothing (AdvancedMode.h).
  urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced);
  advanced_ = urmsg::AdvancedModeEnabled();

  // The shipped scaffold steps aside; the hosts take over.
  ListScaffold().Visibility(Visibility::Collapsed);
  ThreadPane().Visibility(Visibility::Collapsed);
  ListHost().Visibility(Visibility::Visible);

  NetworkNavItem().Content(box_value(hstring{kDemoNavNetwork}));
  DeveloperNavItem().Content(box_value(hstring{kDemoNavDeveloper}));
  NetworkNavItem().Visibility(Visibility::Visible);
  DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
                                          : Visibility::Collapsed);

  DemoChipText().Text(kDemoWatermark);
  DemoChip().Visibility(options_.watermark ? Visibility::Visible
                                           : Visibility::Collapsed);

  // Armed, not run. See DrainDeepLink.
  pendingLink_ = link;
  pendingLinkArmed_ = true;

  urnw::LogInfo("window: demo mode on (screen tag {}, autoplay {}, advanced {}, watermark {})",
                urnw::Narrow(std::wstring{link.navTag}), options_.autoplay, advanced_,
                options_.watermark);
}

void MainWindow::SelectNavTag(std::wstring_view tag) {
  // Setting SelectedItem raises SelectionChanged, so the destination swap runs
  // through the one path OnNavSelectionChanged already owns.
  auto match = [&](auto const& items) -> bool {
    for (auto const& entry : items) {
      auto item = entry.template try_as<NavigationViewItem>();
      if (!item) continue;
      auto itemTag = item.Tag().template try_as<hstring>();
      if (itemTag && std::wstring_view{*itemTag} == tag) {
        HomeNav().SelectedItem(item);
        return true;
      }
    }
    return false;
  };
  if (!match(HomeNav().MenuItems())) match(HomeNav().FooterMenuItems());
}

void MainWindow::DrainDeepLink() {
  // One-shot, and only once the layout has actually been written. breakpoint
  // state, window size and NavigationView's own load-time selection are all
  // settled by the first SizeChanged; running from the constructor instead put
  // the nav selection before NavigationView loaded (where the markup's
  // IsSelected="True" on ChatsNavItem wins) and the message selection before the
  // rail existed.
  if (!pendingLinkArmed_ || !breakpointApplied_) return;
  pendingLinkArmed_ = false;
  SelectNavTag(pendingLink_.navTag);
  urnw::LogInfo("window: demo deep link -> tag={} conversation={} message={}",
                urnw::Narrow(std::wstring{pendingLink_.navTag}),
                pendingLink_.selectConversation, pendingLink_.selectMessage);
}
```

- [ ] **Step 10: Extend `ShowDestination` in `MainWindow.xaml.cpp` to the six tags.**

Replace the whole existing `MainWindow::ShowDestination` body (MainWindow.xaml.cpp:194-207)
with:

```cpp
void MainWindow::ShowDestination(std::wstring_view tag) {
  // There is no Frame in this window - sibling Grids toggled by Visibility - so
  // the page change goes through motion::CrossfadePageSwap (UrMotion.h:120), the
  // app's standing replacement for NavigationTransitionInfo and design doc 7's
  // page-change row. It owns the Visibility of BOTH elements and falls back to
  // an instant swap when ShouldAnimate() is false, so nothing here writes
  // Visibility itself.
  FrameworkElement incoming = StubPage();
  hstring header = Loc("nav_settings");

  if (tag == L"chats") {
    incoming = ChatsPage();
    header = Loc("nav_chats");
  } else if (tag == L"contacts") {
    incoming = StubPage();
    header = Loc("nav_contacts");
  } else if (tag == L"network" && options_.enabled) {
    incoming = NetworkHost();
    header = hstring{kDemoNavNetwork};
  } else if (tag == L"developer" && options_.enabled) {
    incoming = DeveloperHost();
    header = hstring{kDemoNavDeveloper};
  } else if (tag == L"settings" && options_.enabled) {
    incoming = SettingsHost();
    header = Loc("nav_settings");
  }

  HomeNav().Header(box_value(header));
  if (incoming == StubPage()) StubPaneTitle().Text(header);

  currentTag_ = std::wstring{tag};
  urnw::motion::CrossfadePageSwap(currentPage_, incoming);
  currentPage_ = incoming;
}
```

- [ ] **Step 11: Open at 1560x900 under `--demo`, in `app/src/App/App.xaml.cpp`.**

Add to the include block, after `#include "Startup.h"`:

```cpp
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
```

Replace App.xaml.cpp:76:

```cpp
      if (hwnd) urnw::shell::ApplyNativeShell(window_, hwnd);
```

with:

```cpp
      if (hwnd) {
        urnw::shell::ApplyNativeShell(window_, hwnd);
        // D7: demo mode opens at 1560x900 DIP, because the rail exists only at
        // or above 1500 CONTENT dips (a 1560-dip window measures ~1546 of
        // content) and the rail must be live on launch - it is the only way an
        // agent can see that state without synthesising input. AFTER
        // ApplyNativeShell deliberately: that function ends in MoveAndResize and
        // would otherwise win. The options are READ BACK from the window that
        // already parsed them, so the command line has one parser.
        bool demo = false;
        if (auto self = window_.try_as<URmessage::MainWindow>())
          demo = winrt::get_self<MainWindow>(self)->DemoOptions().enabled;
        if (demo) {
          const UINT dpi = ::GetDpiForWindow(hwnd);
          const double scale = (dpi ? dpi : 96u) / 96.0;
          const auto id = winrt::Microsoft::UI::GetWindowIdFromWindow(hwnd);
          if (auto appWindow =
                  winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(id)) {
            appWindow.Resize({static_cast<int32_t>(1560 * scale),
                              static_cast<int32_t>(900 * scale)});
            urnw::LogInfo("app: demo launch size 1560x900 dip (dpi {})", dpi);
          }
        }
      }
```

- [ ] **Step 12: Build, launch deep-linked to a destination, and look.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network"
```

Expected console: `dips        : 1560x900` and `foreground  : YES`.

Read `.verify\urmessage-window.png`. Expected: a 48px title bar carrying the URmessage
wordmark and, immediately right of it, a small bordered chip reading `DEMO`; the nav pane
lists exactly three items — Chats, Contacts, Network — with **Network selected** and
Settings alone at the pane's foot and no Developer item; the header reads `Network`; the
content area right of the nav pane is entirely `#101010` (the empty `NetworkHost`).

- [ ] **Step 13: Prove the deep link was not undone by NavigationView's markup selection.**

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
Select-String -Path $log -SimpleMatch 'window: destination ->' | Select-Object -Last 3
Select-String -Path $log -SimpleMatch 'window: demo deep link ->' | Select-Object -Last 1
```

Expected: the LAST `window: destination ->` line reads `-> network`, and it is timestamped
after the `window: demo deep link -> tag=network conversation=false message=false` line.
A final `-> chats` after the deep-link line means the markup's `IsSelected="True"` won and
the drain is running too early — the fix is the drain point, not a retry.

- [ ] **Step 14: Confirm the watermark switch actually does something.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network --demo-watermark=off"
```

Read `.verify\urmessage-window.png`. Expected: identical to step 12 except the `DEMO` chip
is gone and the wordmark is the only thing in the title bar. `options_.watermark` is now a
switch that changes a pixel, not a switch that changes a log line.

- [ ] **Step 15: Confirm a normal launch is unchanged.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1
```

Expected console: `dips        : 480x760`, exactly. That number is deterministic:
`SaveWindowPlacement` is defined at `WindowShell.cpp:346` and declared at `WindowShell.h:56`
but **has no call site anywhere in `app/src/App`**, so no placement is ever saved,
`ApplyNativeShell` always takes the centred-default branch, and any other size is a
regression this task caused.

Read `.verify\urmessage-window.png`. Expected: the ten `Sample conversation N` rows under a
`Recent 10` group header, two nav items plus Settings, no `DEMO` chip, no Network item.

- [ ] **Step 16: Commit.**

```
git add app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp app/src/App/App.xaml.cpp
git commit -m "demo: MainWindow hosts, DEMO chip, Network/Developer destinations, deferred deep link, 1560x900"
```

**Deliverable:** `--demo=network` opens a 1560×900 window whose header and selected nav item
are Network (proved in the log to be the last destination change, not one that was
overwritten), the `DEMO` chip appears and `--demo-watermark=off` removes it, and a launch
without `--demo` still reports `dips : 480x760` with the ten sample rows.

---

## Task W4: One window-level breakpoint: rail at 1500 content dips, strip collapse at 560 content height

**Files:**

Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: `urmsg::demo::LayoutFor`, `urmsg::demo::Layout`, `urmsg::demo::kRailWidthDip` (W1); the `ListHost`/`ThreadHost`/`RailHost`/`RailRule`/`StatusStripHost` hosts and `RailColumn` from W3; `urnw::kit::kWideBreakpointDip` (UrComponents.h:75)
- Produces: `void MainWindow::ApplyBreakpoint();` rewritten on `LayoutFor`; members `urmsg::demo::Layout MainWindow::layout_;` and `bool MainWindow::layoutApplied_;` (replacing `wide_` and `breakpointApplied_`)

## Task W4: one function decides three thresholds

`AdaptiveTrigger` and `VisualStateManager` are unusable in this shell — `Window.Current` is
null in a WinUI 3 desktop app and `VisualStateGroup`s on a plain layout `Grid` are never
processed. Both are measured facts recorded in `UrComponents.h:62-74`. So there is exactly
ONE function that decides what "wide" means, and this task widens it from one threshold to
three.

The decision itself already lives in `LayoutFor` and is already asserted at its boundaries
by `--diagnose` (W1). This task is only the writing-to-XAML half, and the log line it emits
states **content** dips so the numbers in the log and the numbers in `LayoutFor` are the
same quantity.

- [ ] **Step 1: Replace the two flags in `app/src/App/MainWindow.xaml.h`.**

Delete `bool wide_ = false;` (MainWindow.xaml.h:61) together with `bool
breakpointApplied_ = false;` (line 68) and its comment block (lines 62-67), and put in
their place:

```cpp
  // The whole layout answer, not one bool: three thresholds now (list beside
  // thread at 1000, rail at 1500, strip at 560 of HEIGHT), all in CONTENT-root
  // dips, which is what ActualWidth/ActualHeight of Content() report.
  urmsg::demo::Layout layout_{};
  // Whether ApplyBreakpoint has ever actually WRITTEN the layout. Without it,
  // the first pass early-outs whenever the initial size is narrow (layout_ is
  // already all-false), and the window is only correct because the markup
  // defaults happen to spell the narrow state - an invariant nothing enforces,
  // living in two files. The VPN client carries the same flag for the same
  // reason. It is also what DrainDeepLink waits on.
  bool layoutApplied_ = false;
```

- [ ] **Step 2: Rewrite `MainWindow::ApplyBreakpoint()` in `app/src/App/MainWindow.xaml.cpp`.**

Replace the entire existing body (MainWindow.xaml.cpp:164-192) with:

```cpp
void MainWindow::ApplyBreakpoint() {
  auto root = Content().try_as<FrameworkElement>();
  if (!root) return;
  // CONTENT-root dips, not window dips: the frame costs about 14 of them (the
  // shipped log has 466 for a 480-dip window and 1186 for a 1200-dip one). The
  // thresholds in DemoShellState.h are content thresholds for this reason, and
  // the log line below says `content` so the two can be compared.
  const double width = root.ActualWidth();
  const double height = root.ActualHeight();
  if (width <= 0 || height <= 0) return;

  const auto next = urmsg::demo::LayoutFor(width, height);
  if (layoutApplied_ && next.wide == layout_.wide && next.rail == layout_.rail &&
      next.strip == layout_.strip)
    return;
  layout_ = next;
  layoutApplied_ = true;

  // Wide: a fixed 320dip list rail and the thread takes what is left. Narrow:
  // the list IS the window and the thread does not exist.
  ListColumn().Width(layout_.wide
                         ? GridLengthHelper::FromPixels(320)
                         : GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  ThreadColumn().Width(layout_.wide
                           ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                           : GridLengthHelper::FromPixels(0));
  const auto threadVisibility =
      layout_.wide ? Visibility::Visible : Visibility::Collapsed;
  PaneRule().Visibility(threadVisibility);
  // Exactly one of the two thread surfaces is ever live: the shipped scaffold,
  // or the demo's ThreadView host.
  ThreadPane().Visibility(options_.enabled ? Visibility::Collapsed : threadVisibility);
  ThreadHost().Visibility(options_.enabled ? threadVisibility : Visibility::Collapsed);

  // The rail is demo-only and exists only at or above kRailBreakpointDip. Below
  // it, message inspect is simply unavailable - no sheet, no fallback, no error
  // (design doc 6.5a).
  const bool rail = options_.enabled && layout_.rail;
  RailColumn().Width(rail ? GridLengthHelper::FromPixels(urmsg::demo::kRailWidthDip)
                          : GridLengthHelper::FromPixels(0));
  const auto railVisibility = rail ? Visibility::Visible : Visibility::Collapsed;
  RailRule().Visibility(railVisibility);
  RailHost().Visibility(railVisibility);

  // The strip is hidden below kStripMinHeightDip of content HEIGHT so it can
  // never eat a readable thread at Spec C 1.2's 480dip minimum.
  StatusStripHost().Visibility(options_.enabled && layout_.strip
                                   ? Visibility::Visible
                                   : Visibility::Collapsed);

  urnw::LogInfo("window: layout wide={} rail={} strip={} (content {:.0f}x{:.0f} dip)",
                layout_.wide, layout_.rail, layout_.strip, width, height);
}
```

- [ ] **Step 3: Point `DrainDeepLink` at the new flag (same file).**

In `MainWindow::DrainDeepLink()`, change

```cpp
  if (!pendingLinkArmed_ || !breakpointApplied_) return;
```

to

```cpp
  if (!pendingLinkArmed_ || !layoutApplied_) return;
```

and extend its log line so the ordering gate has something to read:

```cpp
  urnw::LogInfo("window: demo deep link -> tag={} conversation={} message={} (rail={})",
                urnw::Narrow(std::wstring{pendingLink_.navTag}),
                pendingLink_.selectConversation, pendingLink_.selectMessage,
                layout_.rail);
```

- [ ] **Step 4: Build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

Expected: `Build succeeded` with 0 errors. Any surviving `wide_` or `breakpointApplied_`
reference elsewhere in the file appears here as C2065 and must be removed, not reinstated.

- [ ] **Step 5: Launch wide and look at the rail band.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

Expected console: `dips        : 1560x900`.

Read `.verify\urmessage-window.png`. Expected: TWO vertical hairlines cross the content
area — one 320 dip right of the nav pane's edge (`PaneRule`), and a second 360 dip in from
the window's RIGHT edge (`RailRule`). The 360-dip band between the second hairline and the
right edge is flat `#101010`: the rail host exists and is empty, because no view is mounted
yet.

- [ ] **Step 6: Look at the no-rail state the same run already captured.**

Read `.verify\urmessage-window-wide.png` — `verify-render.ps1:281-295` resizes the window
to 1200×800 dip and captures again with `PrintWindow`. Expected: ONE vertical hairline
only, 320 dip right of the nav pane's edge. There is NO second hairline near the right
edge, because 1200 dip of window is ~1186 dip of content, below `kRailBreakpointDip`. The
console line for that capture reads `screenshot (wide 1200x800 dip)`.

- [ ] **Step 7: Confirm the log agrees with the pixels — on flags, not on a dip pair.**

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
Select-String -Path $log -SimpleMatch 'window: layout' | Select-Object -Last 4
```

Expected: a line reading `window: layout wide=true rail=true strip=true (content NNNNxNNN
dip)` whose width is between 1500 and 1560 — on a 100% display it reads `content 1546x886
dip` — followed by a later line `window: layout wide=true rail=false strip=true (content
NNNNxNNN dip)` with a width between 1150 and 1200 (`content 1186x792 dip`). **Do not check
for the literal `1560x900`**: the function is fed content dips and would never print it.

- [ ] **Step 8: Prove the deep link now runs after the layout, not before it.**

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
$layout = (Select-String -Path $log -SimpleMatch 'window: layout wide=true rail=true' | Select-Object -First 1).LineNumber
$link = (Select-String -Path $log -SimpleMatch 'window: demo deep link ->' | Select-Object -First 1).LineNumber
"layout line $layout, deep link line $link, ordered: $($layout -lt $link)"
```

Expected: `ordered: True`, and the deep-link line itself ends `(rail=true)`. That pair is
the evidence that a message deep link (task W5) can reach a live rail rather than
early-outing on `!layout_.rail`.

- [ ] **Step 9: Commit.**

```
git add app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: ApplyBreakpoint on LayoutFor - rail at 1500, strip at 560, in content dips"
```

**Deliverable:** one function decides three thresholds in the same unit the assertion tests;
the 1560 capture shows two hairlines and the 1200 capture shows one; the log proves the
layout is written before the deep link drains.
