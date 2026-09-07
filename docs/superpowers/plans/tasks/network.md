# Network page

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task N1: The Network destination: nav item, pane shell, and the --demo=network deep link

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: Task F1 (the harness task): verify-render.ps1's `-AppArgs` parameter. Task 2 (fixed contract §2): app/src/App/Demo/DemoSwitches.h — `urmsg::demo::ParseDemoOptions()`, `DemoOptions::enabled`, `DemoOptions::screen`, `DemoScreen::Network`. The demo-launch task: `--demo` opens the window at 1560x900 DIP (contract §6 rule 8, design D7) — NOT implemented anywhere in this repo today. Existing: `urnw::kit::MakePaneEmptyLine` (UrComponents.h:423), `urnw::Localized` via the file-local `Loc()` adapter (MainWindow.xaml.cpp:31), the existing resw key `destination_not_built` (Resources.resw:104), App.xaml keys `UrPaneStyle` (:823) / `UrPaneHeaderStyle` (:828) / `UrPaneTitleStyle` (:838) / `UrIconFontFamily` (:520).
- Produces: XAML x:Names on MainWindow: `NetworkNavItem` (NavigationViewItem, Tag="network"), `NetworkPage` (Grid), `NetworkPaneTitle` (TextBlock), `NetworkBody` (Grid — every later Network task appends into this). `void MainWindow::ApplyDemoScreen()` (private, no args, called last in the constructor). `MainWindow::ShowDestination` gains a `network` arm. No new files, no resw keys.

## Task N1 — the Network destination: nav item, pane shell, `--demo=network` deep link

The fourth nav destination and the empty pane behind it. Nothing on it yet but the 40px header
strip and one honest line — N3 fills `NetworkBody`.

**Ownership, stated first.** The fixed contract gives this surface `app/src/App/Views/*` (§4) and
names **no owner** for `MainWindow`. The wiring surface's plan mounts destinations there too. So
every `MainWindow` edit below is written twice: what to look for, and the exact block to add
**only when it is absent**. "Already there → add nothing" is a complete instruction; under no
ordering does this task produce a second nav item or a second deep-link dispatcher.

**No new localization keys.** `Strings/en/Resources.resw` is generated from
`urnetwork/localizations` (`Localization.h:3-4`) and the contract forbids adding a key to it
(§0.1). `Network` and `NETWORK` are English literals in the code. The one string taken from the
store is the **existing** `destination_not_built` key (`Resources.resw:104` — "This part of
URmessage has not been built yet.").

- [ ] **Step 1: find out what is already mounted.** From the repo root:

```
git grep -n "NetworkNavItem\|NetworkPage\|NetworkHost\|ApplyDemoScreen" -- app/src/App
```

      Expected on a tree where no other surface has landed: **no output at all** (exit 1). If it
      prints hits in `MainWindow.xaml`, the wiring surface's demo shell is already in: read them,
      add only what is missing in each step below, and wherever this task writes `NetworkBody()`
      use whatever host element that shell named instead (its plan calls it `NetworkHost`).

- [ ] **Step 2: add the NavigationViewItem.** If step 1 found no `NetworkNavItem`, add this to
      `app/src/App/MainWindow.xaml` inside `<muxc:NavigationView.MenuItems>`, after
      `ContactsNavItem` (line 106) and before `</muxc:NavigationView.MenuItems>`:

```xml
                <muxc:NavigationViewItem x:Name="NetworkNavItem" Tag="network">
                    <muxc:NavigationViewItem.Icon>
                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xE701;" />
                    </muxc:NavigationViewItem.Icon>
                </muxc:NavigationViewItem>
```

      `E701` is Segoe Fluent Icons' **NetworkTower**. `FontFamily` is named for the reason the two
      existing items state at line 94: `FontIcon` otherwise falls back to the older Segoe MDL2
      Assets and the rail comes out with two icon weights.

- [ ] **Step 3: add the destination Grid.** If step 1 found no `NetworkPage`, add this to
      `MainWindow.xaml` inside the destinations `<Grid>` (line 120), between the closing `</Grid>`
      of `ChatsPage` (line 181) and the `StubPage` comment (line 183):

```xml
                <!-- NETWORK. Relay path, message server, linked devices, in one
                     scrolling column. PANE vocabulary, not cards: this window
                     uses nothing from the card model (see the file header), so
                     the page is group headers on the 28px rhythm over
                     fixed-height rows. Everything inside NetworkBody is built in
                     code by urmsg::views::MakeNetworkPage (Task N3).

                     HorizontalScrollBarVisibility stays Disabled: that is what
                     constrains the content to the viewport width, and it is why
                     the group headers and rows below stretch to the pane instead
                     of shrinking to their text. The relay diagram carries its
                     OWN horizontal scroller (N3) so it can never be clipped. -->
                <Grid x:Name="NetworkPage" Visibility="Collapsed"
                      Style="{StaticResource UrPaneStyle}">
                    <Grid.RowDefinitions>
                        <RowDefinition Height="Auto" />
                        <RowDefinition Height="*" />
                    </Grid.RowDefinitions>
                    <Border Grid.Row="0" Style="{StaticResource UrPaneHeaderStyle}">
                        <TextBlock x:Name="NetworkPaneTitle"
                                   Style="{StaticResource UrPaneTitleStyle}" />
                    </Border>
                    <ScrollViewer Grid.Row="1" HorizontalScrollBarVisibility="Disabled"
                                  VerticalScrollBarVisibility="Auto">
                        <Grid x:Name="NetworkBody" />
                    </ScrollViewer>
                </Grid>
```

- [ ] **Step 4: set its two strings.** In `MainWindow::ApplyStrings()`
      (`MainWindow.xaml.cpp:107`), add after the `ContactsNavItem().Content(...)` line (:115):

```cpp
  // English literals, not a resw key: Strings/en/Resources.resw is generated
  // from urnetwork/localizations (Localization.h:3-4) and this work adds no
  // localization keys (design §9.4).
  NetworkNavItem().Content(box_value(winrt::hstring{L"Network"}));
```

      and after the `ThreadPaneTitle().Text(...)` line (:119):

```cpp
  NetworkPaneTitle().Text(winrt::hstring{L"NETWORK"});

  // TEMPORARY, deleted by Task N3 the moment MakeNetworkPage lands. "Not built"
  // rather than an empty Grid: an empty pane cannot distinguish "nothing here"
  // from "this failed to load" (MainWindow.xaml:183-186).
  NetworkBody().Children().Clear();
  NetworkBody().Children().Append(
      urnw::kit::MakePaneEmptyLine(Loc("destination_not_built")));
```

- [ ] **Step 5: route the destination.** Replace the whole body of `MainWindow::ShowDestination`
      (`MainWindow.xaml.cpp:194-207`) with this. If step 1 showed the wiring surface has already
      rewritten `ShowDestination`, add only the two `network` lines to whatever is there and
      leave the rest alone.

```cpp
void MainWindow::ShowDestination(std::wstring_view tag) {
  const bool chats = (tag == L"chats");
  const bool network = (tag == L"network");
  ChatsPage().Visibility(chats ? Visibility::Visible : Visibility::Collapsed);
  NetworkPage().Visibility(network ? Visibility::Visible : Visibility::Collapsed);
  // StubPage stands in for every destination that has no page of its own yet.
  StubPage().Visibility((chats || network) ? Visibility::Collapsed : Visibility::Visible);

  if (chats) {
    HomeNav().Header(box_value(Loc("nav_chats")));
    return;
  }
  if (network) {
    HomeNav().Header(box_value(winrt::hstring{L"Network"}));
    return;
  }
  const auto label = (tag == L"contacts") ? Loc("nav_contacts")
                                          : Loc("nav_settings");
  HomeNav().Header(box_value(label));
  StubPaneTitle().Text(label);
}
```

- [ ] **Step 6: declare the deep-link applier.** If step 1 found no `ApplyDemoScreen`, add this to
      the private section of `app/src/App/MainWindow.xaml.h`, right after
      `void ShowDestination(std::wstring_view tag);` (:57):

```cpp
  // --demo=<screen> -> the destination that screen names. Runs LAST in the
  // constructor so it writes over the markup's IsSelected="True" on Chats.
  void ApplyDemoScreen();
```

      If it **is** already declared, skip this step and step 7's function body, and do only the
      one-line edit named at the end of step 7.

- [ ] **Step 7: define it and call it.** In `MainWindow.xaml.cpp`, add
      `#include "Demo/DemoSwitches.h"` beside `#include "Localization.h"` (:15).

      **If `MainWindow::ApplyDemoScreen` did not already exist**, add this function immediately
      before `MainWindow::OnNavSelectionChanged` (:209), and add `ApplyDemoScreen();` as the LAST
      statement of the constructor — after `ApplyBreakpoint();` (:101) and before the closing
      `urnw::LogInfo(...)` (:102):

```cpp
void MainWindow::ApplyDemoScreen() {
  const auto options = urmsg::demo::ParseDemoOptions();
  if (!options.enabled) return;
  switch (options.screen) {
    case urmsg::demo::DemoScreen::Network:
      // Writing SelectedItem raises SelectionChanged, which calls
      // ShowDestination — the deep link goes through exactly the path a click
      // does, so there is no second way to reach a destination.
      HomeNav().SelectedItem(NetworkNavItem());
      urnw::LogInfo("window: demo deep link -> network");
      break;
    default:
      break;
  }
}
```

      **If it already existed**, make exactly one edit: add this arm to its `switch`, and add
      NOTHING to the constructor — two dispatchers writing `SelectedItem` is a race with no owner.

```cpp
    case urmsg::demo::DemoScreen::Network:
      HomeNav().SelectedItem(NetworkNavItem());
      urnw::LogInfo("window: demo deep link -> network");
      break;
```

- [ ] **Step 8: build.** From `C:\Users\ryanm\Downloads\claude_sandbox_message\message-windows`:

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

      Expected, in this order: `toolset  : v14x`, `win sdk  : 10.0.x`, msbuild's summary with
      `0 Error(s)`, then the green `OK in <N>s -> ...\app\build\x64\Release\`. If the compiler
      reports `C1083: Cannot open include file: 'Demo/DemoSwitches.h'`, Task 2 has not landed —
      **stop, do not work around it**.

- [ ] **Step 9: launch deep-linked and capture.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network"
```

      Read the console first. The line that decides everything below is

```
dips        : 1560x900
```

      Under `--demo` the window is **1560x900 DIP** (contract §6 rule 8 / design D7). If it says
      `480x760`, the demo-launch task has not landed — **stop**. At 480 DIP the nav pane is a
      closed overlay (`MainWindow.xaml:66-73`) and the rail check in step 10 is not observable at
      all; do not "work around" it by reading a different image.

      Two more things about this script's output, so neither costs time: (a) its five pixel probes
      are hard-coded for the **Chats** destination (`verify-render.ps1`, the `pane header strip` /
      `group header strip` / `row hairline` rows at x=200), so on a Network capture they read
      different content and their `expected` column does not apply — judge from the image, not the
      probes; (b) the script resizes to 1200x800 at the end and writes that to a **different**
      file, `urmessage-window-wide.png`.

- [ ] **Step 10: read the screenshot.** Open `.verify/urmessage-window.png` — the PrintWindow
      capture at the LAUNCH size. Prefer it over `urmessage-window-screen.png` here: this box runs
      at 125% (the script prints `window dpi : 120`), so a 1560x900 DIP window is 1950x1125
      physical and may extend past the desktop, where `CopyFromScreen` returns black. PrintWindow
      with `PW_RENDERFULLCONTENT` draws off-screen parts, and the script's own header says it is a
      faithful capture for this window precisely because the app has no system backdrop.

      Expected, concretely:
      1. The nav pane is **expanded** (1560 >= 1008, `MainWindow.xaml:67`) and lists **four**
         items with labels, top to bottom: `Chats`, `Contacts`, `Network`, then `Settings` pinned
         at the foot.
      2. **Network is the selected item** — it carries the rail's selection fill and its leading
         accent bar; Chats does not.
      3. The Network item's glyph is a **broadcast tower**, not a hollow rectangle. A hollow
         rectangle means the `E701` codepoint missed and must be fixed before this task is done.
      4. The NavigationView header above the content reads `Network` in the ABC Gravity display
         face (`UrTitleTextStyle`).
      5. The content area opens with a 40px `#151515` strip carrying `NETWORK` letterspaced at the
         12px chrome size, with a 1px hairline along its bottom edge.
      6. Directly under that strip, **horizontally** centred, one faint `#5A5A5A` sentence:
         `This part of URmessage has not been built yet.` It sits near the TOP of the content
         area, not mid-pane: `MakePaneEmptyLine` is `VerticalAlignment::Center`
         (`UrComponents.cpp:562`) but it is inside a `ScrollViewer` that measures with unbounded
         height, so there is nothing to centre it in. That is correct here and N3 deletes the line.

- [ ] **Step 11: confirm a plain launch is untouched.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1
```

      Expected: `dips        : 480x760`, and `.verify/urmessage-window.png` shows the Chats
      destination exactly as before this task — the conversation list with its ten sample rows,
      the nav pane as a closed overlay behind the toggle. Design §8: "Without it the app behaves
      exactly as it does today."

- [ ] **Step 12: commit.** The git index on this machine has silently truncated trees before, so
      count the tracked files first. This task adds no files, so the count must be **63 before and
      63 after**:

```
git ls-files | Measure-Object -Line
git add -A
git ls-files | Measure-Object -Line
git commit -m "demo: the Network destination, its pane shell and the --demo=network deep link"
```

**Deliverable, independently checkable:** `URmessage.exe --demo=network` opens with the Network
destination selected and its `NETWORK` pane header rendered, and clicking Chats / Contacts /
Network / Settings in the rail moves between four destinations with no dead item.

---

## Task N2: The Network page's pure formatters, asserted in --diagnose (fail-first, no XAML)

**Files:**

Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.h, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.cpp. Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/main.cpp

**Interfaces:**

- Consumes: Task 1 (fixed contract §1): app/src/App/Demo/DemoWorld.h — `DeviceRef{name, lastSeenLabel, online, isThisComputer}`, `ServerInfo{host, jurisdiction, latencyMs, keyVerified}`, `RelayNode{label, subLabel, glyph, hopMs, healthy}`, `World::myDevices`, `World::relayPath`, `World::server`, `GetWorld()`. Existing: `urnw::WriteDiagnosticsToConsole` / `urnw::ResourceProbe` and the `if (diagnose)` block at main.cpp:194-202; `AdditionalIncludeDirectories` already carries `$(MSBuildProjectDirectory)` (App.vcxproj:125).
- Produces: Namespace `urmsg::views` in app/src/App/Views/NetworkPageView.h: `std::wstring FormatLatency(int latencyMs)`, `std::wstring FormatKeyState(bool keyVerified)`, `std::wstring FormatDeviceMeta(urmsg::demo::DeviceRef const& device)`, `std::wstring FormatRoundTrip(urmsg::demo::ServerInfo const& server)`, `std::vector<std::wstring> CollectNetworkDiagnostics()`. Five `--diagnose` lines: `net fmt latency`, `net fmt keystate`, `net fmt device`, `net fmt rtt`, `net relay hops`. Two new files in App.vcxproj.

## Task N2 — the page's pure formatters, asserted in `--diagnose`

Every string the Network page renders that is not a raw field of `World` comes out of one of four
pure functions. This repo has **no test framework**; `--diagnose` is where an invariant gets to
fail out loud, so the cycle is FAIL first: stubs go in returning the wrong thing, `--diagnose`
prints four `FAIL` lines, then the bodies land and it prints five `PASS`.

**Why the functions are pure — no XAML, no WinRT, no `Localized()`.** Two measured reasons.
(a) `Localized()` caches its `ResourceLoader` in a function-local static on the **first** call, and
`--diagnose` deliberately makes `urnw::ResourceProbe()` that first caller (`Startup.h:54-61`); a
formatter that reached the store from the diagnose path could move that first call earlier and
make the whole UI render key ids. (b) A formatter with no UI in it can be asserted from anywhere,
so the ordering above never becomes load-bearing.

**Why the assertions do NOT go in `CollectDiagnostics()`.** `main.cpp:168` calls it on **every**
launch, not only under `--diagnose` — the `if (diagnose)` gate is at `:194`. Putting
`GetWorld()` there would build the whole seeded demo world (8 conversations, ~40 messages) on
every normal startup, which contradicts design §8 ("Without it the app behaves exactly as it does
today") and puts demo code on the shipping critical path, which is what D8's module boundary
exists to prevent. These lines go in the `if (diagnose)` block instead, beside `ResourceProbe()`
and `InstanceProbe()`.

- [ ] **Step 1: create the header.** Write `app/src/App/Views/NetworkPageView.h`:

```cpp
// The Network destination (design §6.4): the relay path, the message server and
// this account's linked devices.
//
// The Format* functions below are PURE — no XAML, no WinRT, no localization
// lookup. Localized() caches its ResourceLoader on the FIRST call and
// --diagnose deliberately makes ResourceProbe() that first caller
// (Startup.h:54-61), so a formatter that reached the store from the diagnose
// path could make the entire UI render key ids. Keeping them pure means that
// ordering never has to be remembered again.
//
// They format values DemoWorld already carries as English literals ("2 min
// ago", "Iceland"), so they are consistent with the world they read.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// 18 -> "18 ms". Also formats a RelayNode::hopMs for the Advanced per-hop line.
std::wstring FormatLatency(int latencyMs);

// true -> "Verified", false -> "Not verified"
std::wstring FormatKeyState(bool keyVerified);

// isThisComputer -> "This computer · Online"
// online         -> "Online"
// otherwise      -> "Last seen 3 days ago"
//
// DeviceRef::ownerName is deliberately NOT used: these are the account's OWN
// devices, so repeating the account holder's name on all three says nothing.
std::wstring FormatDeviceMeta(urmsg::demo::DeviceRef const& device);

// 18 -> "18 ms round trip". Advanced Mode only (design §6.6). It is the
// END-TO-END figure: ServerInfo carries one latencyMs and no per-hop split, so
// this must never be presented as a per-hop timing. The per-hop figures come
// from RelayNode::hopMs and are rendered inside the nodes (Task N6).
std::wstring FormatRoundTrip(urmsg::demo::ServerInfo const& server);

// The --diagnose lines for this surface. Pure C++ for the reason above.
// main.cpp pushes these inside its `if (diagnose)` block — NOT into
// CollectDiagnostics(), which runs on every launch (main.cpp:168).
std::vector<std::wstring> CollectNetworkDiagnostics();

}  // namespace urmsg::views
```

- [ ] **Step 2: create the .cpp with WRONG bodies and a REAL assertion block.** Write
      `app/src/App/Views/NetworkPageView.cpp`. The four formatters return empty on purpose, so
      step 6 sees real FAILs rather than a compile error; `CollectNetworkDiagnostics` is written
      for real, because it is the thing doing the testing.

```cpp
// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Views/NetworkPageView.h"

#include <format>
#include <string>
#include <vector>

namespace urmsg::views {
namespace {

// PASS / FAIL with the QUERY printed beside the value it compared. A line that
// says PASS without showing what it asked is not evidence — it is a claim.
const wchar_t* Check(bool ok) { return ok ? L"PASS" : L"FAIL"; }

}  // namespace

std::wstring FormatLatency(int) { return {}; }
std::wstring FormatKeyState(bool) { return {}; }
std::wstring FormatDeviceMeta(urmsg::demo::DeviceRef const&) { return {}; }
std::wstring FormatRoundTrip(urmsg::demo::ServerInfo const&) { return {}; }

std::vector<std::wstring> CollectNetworkDiagnostics() {
  const urmsg::demo::World& world = urmsg::demo::GetWorld();
  std::vector<std::wstring> lines;

  lines.push_back(std::format(
      L"  net fmt latency  : {}  FormatLatency(18) == \"18 ms\" -> \"{}\"",
      Check(FormatLatency(18) == L"18 ms"), FormatLatency(18)));

  lines.push_back(std::format(
      L"  net fmt keystate : {}  FormatKeyState(true|false) -> \"{}\" | \"{}\"",
      Check(FormatKeyState(true) == L"Verified" &&
            FormatKeyState(false) == L"Not verified"),
      FormatKeyState(true), FormatKeyState(false)));

  // A PROPERTY over the whole device list, not one hand-built probe: exactly
  // the device DemoWorld marks isThisComputer names itself, and no other device
  // claims to be this computer. A stub returning "" fails it, and so does a
  // world with two local machines.
  bool deviceOk = !world.myDevices.empty();
  std::wstring thisComputerLine;
  for (auto const& device : world.myDevices) {
    const std::wstring meta = FormatDeviceMeta(device);
    if (device.isThisComputer) thisComputerLine = meta;
    if (meta.starts_with(L"This computer") != device.isThisComputer) deviceOk = false;
  }
  lines.push_back(std::format(
      L"  net fmt device   : {}  exactly the isThisComputer device says so, over "
      L"{} devices -> \"{}\"",
      Check(deviceOk), world.myDevices.size(), thisComputerLine));

  lines.push_back(std::format(
      L"  net fmt rtt      : {}  FormatRoundTrip == FormatLatency + \" round "
      L"trip\" -> \"{}\" vs \"{}\"",
      Check(FormatRoundTrip(world.server) ==
            FormatLatency(world.server.latencyMs) + L" round trip"),
      FormatRoundTrip(world.server),
      FormatLatency(world.server.latencyMs) + L" round trip"));

  // A PRECONDITION on Task 1's world, not on this file's code. The Advanced
  // per-hop line renders FormatLatency(node.hopMs) inside every node, so a zero
  // hop would render "0 ms" three times and look like working UI.
  bool hopsOk = (world.relayPath.size() == 3);
  int hopSum = 0;
  for (auto const& node : world.relayPath) {
    hopSum += node.hopMs;
    if (node.hopMs <= 0 || node.glyph.empty() || node.label.empty()) hopsOk = false;
  }
  lines.push_back(std::format(
      L"  net relay hops   : {}  relayPath.size()==3, every hopMs>0 and "
      L"glyph/label non-empty (nodes {}, hops total {} ms)",
      Check(hopsOk), world.relayPath.size(), hopSum));

  return lines;
}

}  // namespace urmsg::views
```

- [ ] **Step 3: add both files to the project.** In `app/src/App/App.vcxproj`, add to the
      `<!-- Native sources -->` ItemGroup, after the `UrMotion.cpp` line (:183):

```xml
    <ClCompile Include="Views\NetworkPageView.cpp" />
```

      and after the `<ClInclude Include="UrMotion.h" />` line (:195):

```xml
    <ClInclude Include="Views\NetworkPageView.h" />
```

      `AdditionalIncludeDirectories` already carries `$(MSBuildProjectDirectory)` (:125), so
      `#include "Views/NetworkPageView.h"` resolves from anywhere in the App project.

- [ ] **Step 4: push the lines from main.cpp's diagnose block.** In `app/src/App/main.cpp`, add
      `#include "Views/NetworkPageView.h"` beside `#include "Startup.h"` (:36), then insert this
      immediately after `diagnostics.push_back(InstanceProbe());` (:200) and before
      `return urnw::WriteDiagnosticsToConsole(diagnostics);` (:201):

```cpp
    // The Network surface's own invariants. HERE and not in CollectDiagnostics()
    // (:168), which runs on EVERY launch — building the demo world on a normal
    // startup contradicts design §8 and puts demo code on the shipping path.
    // After ResourceProbe() on purpose: Localized() caches its loader on the
    // first call, and ResourceProbe is deliberately that caller.
    for (auto& line : urmsg::views::CollectNetworkDiagnostics())
      diagnostics.push_back(std::move(line));
```

- [ ] **Step 5: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

      Expected: msbuild's `0 Error(s)` and the green `OK in <N>s -> ...\app\build\x64\Release\`.
      A `C1083: 'Demo/DemoWorld.h'` means Task 1 has not landed — stop.

- [ ] **Step 6: run `--diagnose` and SEE THE FAILURES.** A `/SUBSYSTEM:WINDOWS` process has no
      console of its own, so redirect rather than pipe:

```
New-Item -ItemType Directory -Force .verify | Out-Null
Start-Process -FilePath app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -Wait -NoNewWindow -RedirectStandardOutput .verify\diagnose.txt -RedirectStandardError .verify\diagnose.err.txt
Select-String -Path .verify\diagnose.txt -Pattern '^  net '
```

      Expected — **four `FAIL` and one `PASS`**, in this order:

```
  net fmt latency  : FAIL  FormatLatency(18) == "18 ms" -> ""
  net fmt keystate : FAIL  FormatKeyState(true|false) -> "" | ""
  net fmt device   : FAIL  exactly the isThisComputer device says so, over 3 devices -> ""
  net fmt rtt      : FAIL  FormatRoundTrip == FormatLatency + " round trip" -> "" vs " round trip"
  net relay hops   : PASS  relayPath.size()==3, every hopMs>0 and glyph/label non-empty (nodes 3, hops total <N> ms)
```

      `net fmt rtt` fails here and that is the point: the left side is `""` and the right side is
      `"" + " round trip"` = `" round trip"`, which are not equal. `net relay hops` passes because
      it asserts Task 1's world, not this file's code — if it says `FAIL`, the world is wrong,
      not this task; stop and fix that instead.

- [ ] **Step 7: implement the four bodies.** Replace the four stub definitions in
      `app/src/App/Views/NetworkPageView.cpp`:

```cpp
std::wstring FormatLatency(int latencyMs) {
  return std::to_wstring(latencyMs) + L" ms";
}

std::wstring FormatKeyState(bool keyVerified) {
  return keyVerified ? std::wstring(L"Verified") : std::wstring(L"Not verified");
}

std::wstring FormatDeviceMeta(urmsg::demo::DeviceRef const& device) {
  // U+00B7 MIDDLE DOT, the separator the pane rows already use between two
  // facts on one line.
  const std::wstring state =
      device.online ? std::wstring(L"Online") : (L"Last seen " + device.lastSeenLabel);
  return device.isThisComputer ? (L"This computer · " + state) : state;
}

std::wstring FormatRoundTrip(urmsg::demo::ServerInfo const& server) {
  return FormatLatency(server.latencyMs) + L" round trip";
}
```

- [ ] **Step 8: rebuild and SEE FIVE PASSES.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
Start-Process -FilePath app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -Wait -NoNewWindow -RedirectStandardOutput .verify\diagnose.txt -RedirectStandardError .verify\diagnose.err.txt
Select-String -Path .verify\diagnose.txt -Pattern '^  net '
```

      Expected, with `<N>` being whatever `DemoWorld` carries:

```
  net fmt latency  : PASS  FormatLatency(18) == "18 ms" -> "18 ms"
  net fmt keystate : PASS  FormatKeyState(true|false) -> "Verified" | "Not verified"
  net fmt device   : PASS  exactly the isThisComputer device says so, over 3 devices -> "This computer · Online"
  net fmt rtt      : PASS  FormatRoundTrip == FormatLatency + " round trip" -> "<N> ms round trip" vs "<N> ms round trip"
  net relay hops   : PASS  relayPath.size()==3, every hopMs>0 and glyph/label non-empty (nodes 3, hops total <N> ms)
```

      If `net fmt device` prints `"This computer · Last seen now"` instead, DemoWorld marks the
      local machine offline — the assertion still passes and the page is still correct; only the
      sample text above differs.

- [ ] **Step 9: commit.** Two new files, so the tracked count must go **63 -> 65**:

```
git ls-files | Measure-Object -Line
git add -A
git ls-files | Measure-Object -Line
git commit -m "demo: Network page formatters, with --diagnose assertions"
```

**Deliverable, independently checkable:** `URmessage.exe --diagnose` prints five `net *` lines and
every one reads `PASS`; a plain `URmessage.exe` launch prints none of them and builds no demo
world.

---

## Task N3: The relay path: three nodes from World::relayPath, two static wires, mounted in the page

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.h, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.cpp, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: Task N1: the `NetworkBody` Grid and the `--demo=network` deep link. Task N2: Views/NetworkPageView.h/.cpp and their App.vcxproj entries, `FormatLatency`. Task 1: `World::relayPath` (exactly 3 `RelayNode{label, subLabel, glyph, hopMs, healthy}`), `GetWorld()`. Task F1: verify-render.ps1's `-AppArgs`. Existing: App.xaml keys `UrRowTitleStyle` (:989) / `UrRowNoteStyle` (:1010) / `UrBorderStrongBrush` (:262); `urnw::colors::CardBrush/SheetBrush/BorderBrush/MutedBrush/FaintBrush/DangerBrush` (UrColors.h:98-115); `urnw::LogInfo`/`LogWarn` (Common/Log.h); the `StyleByKey`-from-`Application::Current().Resources()` pattern (UrComponents.cpp:78, private there).
- Produces: `struct urmsg::views::NetworkPageView { winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr}; }` and `NetworkPageView MakeNetworkPage(urmsg::demo::World const& world)` — the fixed contract §4 signatures, verbatim. File-local in NetworkPageView.cpp: `struct PageParts`, `Registry()`, `Find(FrameworkElement)`, `MakeNode`, `MakeWire`, `MakeRelayPath(World const&, std::shared_ptr<PageParts> const&)`. `void MainWindow::BuildNetworkPage()` (private) and the member `urmsg::views::NetworkPageView network_{};`.

## Task N3 — the relay path: three nodes, two static wires

This device -> URnetwork -> message server, built **entirely from `World::relayPath`** — label,
sub-label, glyph and hop timing all come from the world. Nothing on this diagram is a literal
invented here, which is what closes the v1 gap where the middle node had no data behind it.

Static in Normal mode. No `Storyboard` exists yet; N6 adds it, because design D6 quarantines the
moving wire behind Advanced Mode.

**Why the page struct has only `root`.** The fixed contract (§4) gives `NetworkPageView` exactly
one member. The elements `SetNetworkPageAdvanced` has to reach therefore live in a **module-local
registry** keyed by the page root, described in step 3. That is deliberate: the page's internals
are private to `NetworkPageView.cpp`.

- [ ] **Step 1: declare the page in the header.** In `app/src/App/Views/NetworkPageView.h`, add
      this include under `#include <vector>`:

```cpp
#include <winrt/Microsoft.UI.Xaml.h>
```

      and append inside `namespace urmsg::views`, after `CollectNetworkDiagnostics()`:

```cpp
// ---- the page (fixed contract §4) ------------------------------------------
// `root` ONLY, verbatim from the contract. The page's elements are private to
// NetworkPageView.cpp, which keeps them in a registry keyed by this root — see
// the PageParts block there. That is why SetNetworkPageAdvanced (Task N6) looks
// its parts up rather than reading them off this struct.
struct NetworkPageView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
};
NetworkPageView MakeNetworkPage(urmsg::demo::World const& world);
```

- [ ] **Step 2: widen the .cpp's include block.** In `app/src/App/Views/NetworkPageView.cpp`,
      replace the include block at the top of the file (everything between `#include "pch.h"` and
      `namespace urmsg::views {`) with:

```cpp
#include "Views/NetworkPageView.h"

#include <format>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include "Log.h"
#include "UrColors.h"
#include "UrComponents.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace shapes = winrt::Microsoft::UI::Xaml::Shapes;
```

      There is deliberately **no** `#include "Localization.h"` and no `Loc()` adapter in this file:
      `Resources.resw` is generated (`Localization.h:3-4`) and this work adds no keys, so every
      string here is either an English literal or a field of `World`.

- [ ] **Step 3: add the module-local parts registry.** Add this to the existing anonymous
      namespace in `NetworkPageView.cpp`, after `Check`:

```cpp
// The page's elements, module-private.
//
// The fixed contract gives NetworkPageView exactly one member (`root`), so the
// elements SetNetworkPageAdvanced has to reach live here instead, in a registry
// keyed by the page root. shared_ptr, not a plain value, because the device
// rows' Click handlers (Task N5) capture it.
struct PageParts {
  FrameworkElement root{nullptr};

  // the relay path
  FrameworkElement pathRoot{nullptr};
  shapes::Rectangle wireA{nullptr};
  shapes::Rectangle wireB{nullptr};
  std::vector<TextBlock> hopLines;   // one per node; Advanced Mode only
};

// One page per window, and the demo has one window; the vector exists so a
// second window would still be correct, not as a growth plan. Entries are never
// removed — a page lives as long as the process that built it.
std::vector<std::shared_ptr<PageParts>>& Registry() {
  static std::vector<std::shared_ptr<PageParts>> registry;
  return registry;
}

std::shared_ptr<PageParts> Find(FrameworkElement const& root) {
  if (!root) return nullptr;
  for (auto const& parts : Registry())
    if (parts->root == root) return parts;
  return nullptr;
}

// A style out of the app dictionary by key, or null if it is missing. Applying
// App.xaml by key rather than by hand is what keeps this page in step with the
// ramp; a missing key must never throw a layout away. UrComponents.cpp:73-86
// carries the same helper in ITS anonymous namespace, so it is not exported —
// this is a second copy by necessity, not by accident.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

Media::Brush BrushByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Media::Brush>();
}

// Named, for the reason UrComponents.cpp:25-29 states: FontIcon defaults to the
// older Segoe MDL2 Assets, whose metrics differ.
Media::FontFamily IconFont() { return Media::FontFamily(L"Segoe Fluent Icons"); }
```

- [ ] **Step 4: add the node builder.** Add to the same anonymous namespace, after `IconFont()`:

```cpp
struct NodeParts {
  Border root{nullptr};
  TextBlock hop{nullptr};   // Advanced Mode only; Collapsed at build time
};

// One node of the relay path: a 20epx glyph over a title, one muted sub-line,
// and the hop figure Advanced Mode reveals.
//
// A PLAIN Border, deliberately NOT UrStatTileStyle. That style is BasedOn
// UrCardStyle and adds CornerRadius 8 (App.xaml:566) — three 8px-radius tiles
// sitting directly above square pane rows is exactly the mixed vocabulary
// MainWindow.xaml's header (lines 10-16) says this window does not use. The node
// keeps the card FILL (#1C1C1C) and the 1px border and drops the radius.
//
// Every string here comes from the RelayNode. Nothing on the diagram is a
// literal invented in this file.
NodeParts MakeNode(urmsg::demo::RelayNode const& node) {
  NodeParts out;
  out.root = Border();
  out.root.Background(urnw::colors::CardBrush());
  out.root.BorderBrush(urnw::colors::BorderBrush());
  out.root.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
  out.root.Padding(ThicknessHelper::FromLengths(16, 12, 16, 12));
  out.root.MinWidth(168);
  out.root.VerticalAlignment(VerticalAlignment::Center);

  StackPanel column;
  column.Orientation(Orientation::Vertical);
  column.Spacing(6);
  column.HorizontalAlignment(HorizontalAlignment::Center);

  FontIcon icon;
  icon.FontFamily(IconFont());
  // From the world. The contract fixes these as E977 (Devices), E774 (Globe)
  // and E968 (Storage/Server) and --diagnose asserts none of them is empty
  // (net relay hops).
  icon.Glyph(winrt::hstring{node.glyph});
  icon.FontSize(20);
  // Colour is not the only carrier of health: an unhealthy node also says so in
  // its automation name below.
  icon.Foreground(node.healthy ? urnw::colors::MutedBrush()
                               : urnw::colors::DangerBrush());
  icon.HorizontalAlignment(HorizontalAlignment::Center);
  Automation::AutomationProperties::SetAccessibilityView(
      icon, Automation::Peers::AccessibilityView::Raw);
  column.Children().Append(icon);

  TextBlock title;
  if (auto style = StyleByKey(L"UrRowTitleStyle")) title.Style(style);
  title.Text(winrt::hstring{node.label});
  title.HorizontalAlignment(HorizontalAlignment::Center);
  column.Children().Append(title);

  TextBlock sub;
  if (auto style = StyleByKey(L"UrRowNoteStyle")) sub.Style(style);
  sub.Text(winrt::hstring{node.subLabel});
  sub.HorizontalAlignment(HorizontalAlignment::Center);
  column.Children().Append(sub);

  // Advanced Mode's per-hop timing (design §6.6). Built HERE and Collapsed;
  // SetNetworkPageAdvanced (Task N6) is the only thing that shows it, and it
  // only ever flips Visibility — it never rebuilds this node.
  out.hop = TextBlock();
  if (auto style = StyleByKey(L"UrRowNoteStyle")) out.hop.Style(style);
  out.hop.Text(winrt::hstring{FormatLatency(node.hopMs)});
  out.hop.Foreground(urnw::colors::FaintBrush());
  out.hop.HorizontalAlignment(HorizontalAlignment::Center);
  out.hop.Visibility(Visibility::Collapsed);
  column.Children().Append(out.hop);

  out.root.Child(column);
  Automation::AutomationProperties::SetName(
      out.root, winrt::hstring{node.label + L", " + node.subLabel +
                               (node.healthy ? L"" : L", not healthy")});
  return out;
}

// One leg of the path. UrBorderStrongBrush (#38FFFFFF, App.xaml:262) is already
// this app's "a hairline you are MEANT to see"; a wire is that, 2px thick.
//
// A FIXED 72 DIP length, not a star column: the diagram then has ONE measurable
// width (3x168 + 2x(72+16) = 680 DIP) at every window size, and cannot be
// squeezed to nothing by a long node label.
shapes::Rectangle MakeWire() {
  shapes::Rectangle wire;
  wire.Width(72);
  wire.Height(2);
  wire.RadiusX(1);
  wire.RadiusY(1);
  wire.VerticalAlignment(VerticalAlignment::Center);
  wire.Margin(ThicknessHelper::FromLengths(8, 0, 8, 0));
  auto fill = BrushByKey(L"UrBorderStrongBrush");
  wire.Fill(fill ? fill : urnw::colors::BorderBrush());
  Automation::AutomationProperties::SetAccessibilityView(
      wire, Automation::Peers::AccessibilityView::Raw);
  return wire;
}
```

- [ ] **Step 5: add the relay-path builder.** Add to the same anonymous namespace, after
      `MakeWire()`:

```cpp
// Three nodes and two wires on one 5-column grid: node, wire, node, wire, node.
// Fills the path fields of `parts` and returns the element to place.
FrameworkElement MakeRelayPath(urmsg::demo::World const& world,
                               std::shared_ptr<PageParts> const& parts) {
  Grid grid;
  grid.HorizontalAlignment(HorizontalAlignment::Center);
  for (int i = 0; i < 5; ++i) {
    ColumnDefinition column;
    column.Width(GridLengthHelper::Auto());
    grid.ColumnDefinitions().Append(column);
  }

  // The contract fixes relayPath at exactly three and --diagnose asserts it
  // (net relay hops). Guarded anyway: a short world must not index past the end
  // of a vector at 1560x900 in front of an audience.
  if (world.relayPath.size() != 3)
    urnw::LogWarn("network: relayPath has {} nodes, expected 3", world.relayPath.size());

  const size_t nodes = world.relayPath.size() < 3 ? world.relayPath.size() : 3;
  for (size_t i = 0; i < nodes; ++i) {
    auto node = MakeNode(world.relayPath[i]);
    Grid::SetColumn(node.root, static_cast<int32_t>(i * 2));
    grid.Children().Append(node.root);
    parts->hopLines.push_back(node.hop);
  }

  parts->wireA = MakeWire();
  Grid::SetColumn(parts->wireA, 1);
  grid.Children().Append(parts->wireA);

  parts->wireB = MakeWire();
  Grid::SetColumn(parts->wireB, 3);
  grid.Children().Append(parts->wireB);

  parts->pathRoot = grid;
  return grid;
}
```

- [ ] **Step 6: add `MakeNetworkPage`.** Append to `namespace urmsg::views` (outside the anonymous
      namespace), at the foot of `NetworkPageView.cpp`:

```cpp
NetworkPageView MakeNetworkPage(urmsg::demo::World const& world) {
  auto parts = std::make_shared<PageParts>();

  StackPanel column;
  column.Orientation(Orientation::Vertical);

  // The diagram sits on the SHEET step (#151515) with a hairline along its
  // bottom edge — the same "this is chrome above the page" reading the pane and
  // group headers use. No radius and no margin: pane vocabulary, not cards.
  Border pathPanel;
  pathPanel.Background(urnw::colors::SheetBrush());
  pathPanel.BorderBrush(urnw::colors::BorderBrush());
  pathPanel.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));
  pathPanel.Padding(ThicknessHelper::FromLengths(24, 28, 24, 28));

  StackPanel pathColumn;
  pathColumn.Orientation(Orientation::Vertical);
  pathColumn.Spacing(16);

  // The diagram is 680 DIP wide and a person can drag the demo window narrower
  // than that (design §6.5a). It gets its OWN horizontal scroller so the PANE's
  // scroller can stay horizontally Disabled — which is what makes the group
  // headers and rows below stretch to the pane width instead of shrinking to
  // their text. A narrowed demo window is a smaller demo, never a clipped one.
  ScrollViewer pathScroll;
  pathScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
  pathScroll.HorizontalScrollMode(ScrollMode::Enabled);
  pathScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
  pathScroll.VerticalScrollMode(ScrollMode::Disabled);
  pathScroll.Content(MakeRelayPath(world, parts));
  pathColumn.Children().Append(pathScroll);

  pathPanel.Child(pathColumn);
  column.Children().Append(pathPanel);

  parts->root = column;
  Registry().push_back(parts);

  NetworkPageView out;
  out.root = column;
  return out;
}
```

- [ ] **Step 7: host it on MainWindow.** In `app/src/App/MainWindow.xaml.h`, add
      `#include "Views/NetworkPageView.h"` beside `#include "UrComponents.h"` (:21), declare in the
      private section after `void BuildConversationList();` (:48):

```cpp
  // The Network destination's whole content, built in code into NetworkBody.
  void BuildNetworkPage();
```

      and add the member beside `search_` (:60):

```cpp
  urmsg::views::NetworkPageView network_{};
```

      If the wiring surface has already added a `network_` member and a builder, add neither —
      skip to step 9 and check its builder calls `urmsg::views::MakeNetworkPage(...)`.

- [ ] **Step 8: build it and delete N1's placeholder.** In `app/src/App/MainWindow.xaml.cpp`, add
      `#include "Demo/DemoWorld.h"` beside `#include "Demo/DemoSwitches.h"`, add this function
      immediately after `MainWindow::BuildConversationList` (ends :162):

```cpp
void MainWindow::BuildNetworkPage() {
  network_ = urmsg::views::MakeNetworkPage(urmsg::demo::GetWorld());
  NetworkBody().Children().Clear();
  NetworkBody().Children().Append(network_.root);
  urnw::LogInfo("window: network page built");
}
```

      call `BuildNetworkPage();` in the constructor immediately after `BuildConversationList();`
      (:77), and then **delete** the three lines Task N1 added to `ApplyStrings()` that clear
      `NetworkBody()` and append `MakePaneEmptyLine(Loc("destination_not_built"))` — `ApplyStrings`
      runs first, so leaving them is dead code that is cleared a moment later.

- [ ] **Step 9: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

      Expected: `0 Error(s)` and the green `OK in <N>s -> ...\app\build\x64\Release\`.

- [ ] **Step 10: launch and capture.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network"
```

      Confirm the console says `dips        : 1560x900` before reading anything. Its five pixel
      probes are calibrated for the Chats destination and do not apply here.

- [ ] **Step 11: read the screenshot.** Open `.verify/urmessage-window.png` (PrintWindow, launch
      size — see N1 step 10 for why that file and not the screen capture).

      Expected, concretely:
      1. Under the `NETWORK` header strip, a full-width `#151515` panel roughly 150 px tall with a
         1px hairline along its **bottom** edge and none along its top (the header strip already
         drew that one).
      2. Centred in it, **five things in one row**, not three: box, rule, box, rule, box.
      3. Each box is `#1C1C1C` with a 1px border and **square corners** — no radius anywhere on
         this page.
      4. Each rule is a 2px horizontal line, 72 DIP long, faint white, with 8 DIP of clear space
         at each end.
      5. Each box shows a muted `#989898` 20epx glyph centred over **exactly two** centred lines
         (the hop figure is built but Collapsed until N6): box 1 `This device` over `This
         computer`; box 2 `URnetwork` over its sub-label; box 3 `Message server` over the host,
         e.g. `urmsg-01.ur.io`.
      6. **None of the three glyphs is a hollow rectangle.** A hollow box means the Segoe Fluent
         codepoint in `RelayNode::glyph` missed — the contract fixes them as E977 (Devices), E774
         (Globe) and E968 (Storage/Server), so fix the world, not this file.
      7. The wires are **static**: uniform faint white, no gradient, and identical to each other.
      8. No horizontal scrollbar under the diagram — 680 DIP fits inside the ~1340 DIP of content
         area at 1560 wide.

- [ ] **Step 12: confirm the diagram survives a narrower window.** Open
      `.verify/urmessage-window-wide.png`, which the same run wrote after resizing to 1200x800
      DIP. Expected: the same five-element row, still uncropped, still with no horizontal
      scrollbar (680 DIP inside ~980 DIP of content). The nav pane is still expanded at 1200.

- [ ] **Step 13: commit.** No new files, so **65 -> 65**:

```
git ls-files | Measure-Object -Line
git add -A
git ls-files | Measure-Object -Line
git commit -m "demo: the Network page's relay path, three nodes and two static wires"
```

**Deliverable, independently checkable:** `--demo=network` renders a three-node relay diagram whose
left, middle and right nodes each show a real glyph and two lines taken from `World::relayPath`,
whose right-hand node shows the real `ServerInfo::host`, and on which nothing moves.

---

## Task N4: Message server group: host, jurisdiction, latency and key-verification state

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.cpp

**Interfaces:**

- Consumes: Task N2: `FormatLatency`, `FormatKeyState`. Task N3: `MakeNetworkPage` and its `column` StackPanel. Task 1: `World::server` (host, jurisdiction, latencyMs, keyVerified). Existing: `urnw::kit::MakePaneGroupHeader` (UrComponents.h:362) returning `PaneGroupHeader{root, title, meta, trailing}`, `urnw::kit::MakePaneKeyValueRow` (UrComponents.h:291) returning `PaneKeyValueRow{root, key, value}` on a fixed 34px row; `urnw::colors::kUrGreen` (UrColors.h:70), `urnw::colors::DangerBrush()` (UrColors.h:101).
- Produces: Four `MakePaneKeyValueRow` rows under one `MakePaneGroupHeader`, appended inside `MakeNetworkPage`'s `column` between the relay panel and `parts->root = column;`. No new public functions, no new files, no resw keys.

## Task N4 — the message server group

Host, jurisdiction, latency, key state — the four facts design §6.4 asks of the message server.

**Pane vocabulary, and now consistently so.** A 28px group header on the sheet step over four
fixed-height key/value rows with hairlines. `MainWindow.xaml`'s header (lines 10-16) states this
window uses nothing from the card model, and after N3 dropped `UrStatTileStyle` from the relay
nodes there is no radius anywhere on this page — the rule and the page now agree.

**English literals, not resw keys.** `Strings/en/Resources.resw` is generated
(`Localization.h:3-4`) and this work adds no localization keys (design §9.4, contract §0.1).

- [ ] **Step 1: append the group header.** In `app/src/App/Views/NetworkPageView.cpp`, in
      `MakeNetworkPage`, add immediately after `column.Children().Append(pathPanel);`:

```cpp
  // ---- message server (design §6.4) ---------------------------------------
  auto serverGroup = urnw::kit::MakePaneGroupHeader(L"MESSAGE SERVER",
                                                    winrt::hstring{world.server.host});
  column.Children().Append(serverGroup.root);
```

- [ ] **Step 2: append the host and jurisdiction rows.** Add straight after:

```cpp
  // MakePaneKeyValueRow is a FIXED 34px row with a bottom hairline and the
  // pane's 12px inset (UrComponents.cpp:153-164). Four calls, so the four rows
  // cannot drift from one another.
  column.Children().Append(
      urnw::kit::MakePaneKeyValueRow(L"Host", winrt::hstring{world.server.host}).root);
  column.Children().Append(
      urnw::kit::MakePaneKeyValueRow(L"Jurisdiction",
                                     winrt::hstring{world.server.jurisdiction}).root);
```

- [ ] **Step 3: append the latency row.** Add straight after:

```cpp
  // The VALUE comes from views::FormatLatency, not from the world as text: it
  // is the same function --diagnose asserts (net fmt latency), so what the page
  // shows and what the gate checks cannot diverge.
  column.Children().Append(
      urnw::kit::MakePaneKeyValueRow(
          L"Latency", winrt::hstring{FormatLatency(world.server.latencyMs)}).root);
```

- [ ] **Step 4: append the key-state row and colour it.** Add straight after:

```cpp
  // The key row is the only one whose VALUE is a state rather than a fact, so it
  // is the only one that gets a colour. Colour is never alone: the words already
  // read "Verified" / "Not verified", so a reader who cannot see the green loses
  // nothing (fixed contract §6 rule 6).
  auto keyRow = urnw::kit::MakePaneKeyValueRow(
      L"Server key", winrt::hstring{FormatKeyState(world.server.keyVerified)});
  keyRow.value.Foreground(world.server.keyVerified
                              ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                              : urnw::colors::DangerBrush());
  column.Children().Append(keyRow.root);
```

- [ ] **Step 5: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

      Expected: `0 Error(s)` and the green `OK in <N>s -> ...\app\build\x64\Release\`.

- [ ] **Step 6: launch and capture.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network"
```

      Confirm `dips        : 1560x900` in the console. Its five pixel probes are calibrated for
      Chats and do not apply on this page.

- [ ] **Step 7: read the screenshot.** Open `.verify/urmessage-window.png`.

      Expected, concretely:
      1. Below the relay-path panel, a 28px `#151515` strip with 1px rules along **both** its top
         and bottom edges (`UrGroupHeaderStyle`, App.xaml:860), reading `MESSAGE SERVER`
         letterspaced at 11px on the left and the host string right-aligned in the same strip.
      2. Under it, **exactly four** rows, each 34px tall with a hairline along its bottom edge and
         a 12px left inset. All four are the same height — a taller row means a value wrapped and
         the row species drifted.
      3. Keys muted `#989898` hard left, values near-white `#F8F8F8` hard right:
         `Host` / the host string; `Jurisdiction` / e.g. `Iceland`; `Latency` / a figure ending in
         ` ms`; `Server key` / `Verified` in green `#87FB67`, or `Not verified` in `#F8523B`.
      4. The `Latency` value is character-for-character the figure `FormatLatency` produced —
         cross-check it against the `net fmt rtt` line in `.verify/diagnose.txt` from N2, which
         prints the same number.

- [ ] **Step 8: commit.** No new files, so **65 -> 65**:

```
git ls-files | Measure-Object -Line
git add -A
git ls-files | Measure-Object -Line
git commit -m "demo: the Network page's message-server group"
```

**Deliverable, independently checkable:** `--demo=network` shows a `MESSAGE SERVER` group of four
rows whose values all come from `DemoWorld`'s single `ServerInfo`, with the key row coloured by
`keyVerified` and worded so the colour is redundant.

---

## Task N5: Your devices: three DeviceRefs with presence, last-seen and a remove that removes

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.h, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.cpp

**Interfaces:**

- Consumes: Task N2: `FormatDeviceMeta`. Task N3: `MakeNetworkPage`, `PageParts`, `StyleByKey`, `IconFont`. Task N4: the four server rows this appends below. Task 1: `World::myDevices` (`DeviceRef{id, name, lastSeenLabel, online, isThisComputer}`). Existing: `urnw::kit::MakePaneGroupHeader` (UrComponents.h:362), `urnw::kit::MakePaneTwoLineRow` (UrComponents.h:379) returning `PaneTwoLineRow{root, title, note, trailing}` where `trailing` is a right-aligned Grid, `urnw::kit::MakePaneEmptyLine` (UrComponents.h:423, paints `FaintBrush()` #5A5A5A), `urnw::kit::SetTextOrCollapse` (UrComponents.h:107); App.xaml key `UrPaneActionButtonStyle` (:955, a 28x24 button).
- Produces: Three new fields on the file-local `PageParts`: `StackPanel deviceList`, `FrameworkElement deviceEmpty`, `TextBlock deviceCount`, `int deviceRemaining`. A populated `YOUR DEVICES` group inside `MakeNetworkPage` with one row per `DeviceRef` and a working remove. No new public functions, no new files, no resw keys.

## Task N5 — your devices: presence, last-seen, and a remove that removes

Design §6.4 asks for the linked devices "with last-seen and a remove affordance". §9.1 forbids an
affordance that looks live and does nothing, so the remove button **actually removes the row** and
updates the group's count. It is view-local: `GetWorld()` hands back a `const World&` and nothing
here mutates it, so relaunching restores all three — which is right for a demo the owner re-opens.

- [ ] **Step 1: add the device fields to `PageParts`.** In `app/src/App/Views/NetworkPageView.cpp`,
      add to the `PageParts` struct in the anonymous namespace, after `hopLines`:

```cpp
  // your devices
  StackPanel deviceList{nullptr};
  FrameworkElement deviceEmpty{nullptr};
  TextBlock deviceCount{nullptr};
  // An EXPLICIT counter. Deriving the count from deviceList.Children().Size()
  // would make it depend on what else anyone ever appends to that panel, with
  // no compile error the day someone does.
  int deviceRemaining = 0;
```

- [ ] **Step 2: append the group header and the list panel.** In `MakeNetworkPage`, add after the
      key-state row from Task N4 and before `parts->root = column;`:

```cpp
  // ---- your devices (design §6.4) -----------------------------------------
  parts->deviceRemaining = static_cast<int>(world.myDevices.size());

  auto devicesGroup = urnw::kit::MakePaneGroupHeader(
      L"YOUR DEVICES", winrt::to_hstring(parts->deviceRemaining));
  parts->deviceCount = devicesGroup.meta;
  column.Children().Append(devicesGroup.root);

  parts->deviceList = StackPanel();
  parts->deviceList.Orientation(Orientation::Vertical);
  column.Children().Append(parts->deviceList);
```

- [ ] **Step 3: append the empty state as a SIBLING of the list.** Add straight after:

```cpp
  // A SIBLING of the list, not its first child. MakePaneEmptyLine centres itself
  // in whatever cell it is given (UrComponents.h:417-423, UrComponents.cpp:562);
  // put inside the list it would render as a line ABOVE the rows rather than in
  // their place, and every count would then have to know it was there.
  parts->deviceEmpty = urnw::kit::MakePaneEmptyLine(
      L"No devices are linked to this account.");
  parts->deviceEmpty.Visibility(world.myDevices.empty() ? Visibility::Visible
                                                        : Visibility::Collapsed);
  column.Children().Append(parts->deviceEmpty);
```

- [ ] **Step 4: build one row per device, with its presence dot.** Add straight after:

```cpp
  for (auto const& device : world.myDevices) {
    // The two-line row species: name on top, one TRIMMED line of state under it,
    // so the height is 44 whatever the string is.
    auto row = urnw::kit::MakePaneTwoLineRow(
        winrt::hstring{device.name}, winrt::hstring{FormatDeviceMeta(device)});

    StackPanel trailing;
    trailing.Orientation(Orientation::Horizontal);
    trailing.Spacing(10);
    trailing.VerticalAlignment(VerticalAlignment::Center);

    shapes::Ellipse dot;
    dot.Width(8);
    dot.Height(8);
    dot.VerticalAlignment(VerticalAlignment::Center);
    dot.Fill(device.online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                           : urnw::colors::FaintBrush());
    // Not colour alone: the row's second line already says "Online" or
    // "Last seen ..." in words (fixed contract §6 rule 6).
    Automation::AutomationProperties::SetAccessibilityView(
        dot, Automation::Peers::AccessibilityView::Raw);
    trailing.Children().Append(dot);
```

      Leave the `for` body open — steps 5 and 6 close it.

- [ ] **Step 5: add the remove button inside that loop.** Add immediately after
      `trailing.Children().Append(dot);`:

```cpp
    Button remove;
    if (auto style = StyleByKey(L"UrPaneActionButtonStyle")) remove.Style(style);
    FontIcon removeGlyph;
    removeGlyph.FontFamily(IconFont());
    removeGlyph.Glyph(L"\uE74D");  // Delete (wastebasket), Segoe Fluent Icons
    removeGlyph.FontSize(14);
    removeGlyph.Foreground(urnw::colors::MutedBrush());
    remove.Content(removeGlyph);
    // A Button whose Content is an element gets NO automatic name — this project
    // has paid for that twice (MakePaneListRowButton's comment,
    // UrComponents.h:315-318) — so an unnamed row reaches a screen reader as
    // "button" and nothing else.
    Automation::AutomationProperties::SetName(
        remove, winrt::hstring{L"Remove " + device.name});
    trailing.Children().Append(remove);

    row.trailing.Children().Append(trailing);
    parts->deviceList.Children().Append(row.root);
```

- [ ] **Step 6: wire the removal and close the loop.** Add immediately after
      `parts->deviceList.Children().Append(row.root);`:

```cpp
    // View-local: nothing here mutates the world, so a relaunch restores all
    // three devices. That is right for a demo the owner re-opens — and it is a
    // REAL click, which §9.1 requires of anything that looks live.
    //
    // The handler captures `parts`, and `parts` reaches this button through the
    // panel, so this is a reference cycle. Deliberate and bounded: Registry()
    // already holds every page for the life of the process (one page per window,
    // and the demo has one window).
    auto rowRoot = row.root;
    remove.Click([parts, rowRoot](auto const&, auto const&) {
      uint32_t index = 0;
      if (!parts->deviceList.Children().IndexOf(rowRoot, index)) return;
      parts->deviceList.Children().RemoveAt(index);
      if (0 < parts->deviceRemaining) --parts->deviceRemaining;
      // SetTextOrCollapse, not .Text(): MakePaneGroupHeader set this TextBlock
      // through it (UrComponents.cpp:385), so its Visibility is part of how the
      // field works, and .Text() alone would leave a collapsed "0" invisible.
      urnw::kit::SetTextOrCollapse(parts->deviceCount,
                                   winrt::to_hstring(parts->deviceRemaining));
      const bool empty = (parts->deviceRemaining <= 0);
      parts->deviceList.Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
      parts->deviceEmpty.Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
      urnw::LogInfo("network: device removed, {} left", parts->deviceRemaining);
    });
  }
```

- [ ] **Step 7: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

      Expected: `0 Error(s)` and the green `OK in <N>s -> ...\app\build\x64\Release\`.

- [ ] **Step 8: launch and capture.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network"
```

      Confirm `dips        : 1560x900`. Leave the app running — step 10 clicks it.

- [ ] **Step 9: read the screenshot.** Open `.verify/urmessage-window.png`.

      Expected, concretely:
      1. Below the four server rows, a second 28px `#151515` strip reading `YOUR DEVICES` on the
         left and `3` right-aligned.
      2. Under it, **exactly three** rows, each 44px tall with a hairline along its bottom edge.
      3. Each row: a near-white 13px device name on the first line, an 11px muted `#989898` second
         line. One reads `This computer · Online`; the other two read `Last seen 2 min ago` and
         `Last seen 3 days ago`.
      4. At the right of each row, two things in this order: an 8px filled circle (green `#87FB67`
         on the online device, faint `#5A5A5A` on the other two), then a 28x24 icon button showing
         a **wastebasket** glyph — not a hollow rectangle. A hollow rectangle means `E74D` missed.
      5. **No** empty-state sentence anywhere on the page.

- [ ] **Step 10: check the removal by hand.** The app is still running — `verify-render.ps1`'s last
      line says `app still running as pid <N>`. Click the wastebasket on the **middle** device row
      with the mouse. Do not synthesise the click.

      Expected: that row disappears, the row below closes the gap, and the group header's
      right-hand figure changes `3` -> `2`. Click the remaining two. Expected after the last one:
      the three rows are gone, the figure reads `0`, and in their place — directly under the
      `YOUR DEVICES` strip, horizontally centred, faint `#5A5A5A`, with 16px of clear space above
      and below it — the sentence `No devices are linked to this account.`

- [ ] **Step 11: confirm the log agrees.**

```
Select-String -Path .localstate-verify\logs\urmessage-app.log -Pattern 'network: device removed'
```

      Expected: exactly three lines, ending `2 left`, `1 left`, `0 left` in that order. A count
      that skips or repeats means the explicit counter and the panel have diverged.

- [ ] **Step 12: commit.** No new files, so **65 -> 65**:

```
git ls-files | Measure-Object -Line
git add -A
git ls-files | Measure-Object -Line
git commit -m "demo: the Network page's linked-device list, with a remove that removes"
```

**Deliverable, independently checkable:** `--demo=network` lists the three `DeviceRef`s with
presence and last-seen, every remove button changes what is on screen, and removing all three
leaves an explicit empty state rather than a blank panel.

---

## Task N6: Advanced Mode on the Network page: per-hop timings, the round-trip figure, and the wire pulse

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.h, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/NetworkPageView.cpp, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h, C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: Task N2: `FormatRoundTrip`. Task N3: `PageParts` (`pathRoot`, `wireA`, `wireB`, `hopLines`), `Find`, `MakeRelayPath`, `MakeNetworkPage`, `MainWindow::BuildNetworkPage`, the `network_` member. Fixed contract §5: app/src/App/Demo/AdvancedMode.h — `urmsg::AdvancedModeEnabled()`, `urmsg::OnAdvancedModeChanged(std::function<void(bool)>)`; and Task 2's `--demo-advanced` reaching it through `InitAdvancedMode`. Existing: `urnw::motion::MakeSplineDouble` / `ShouldAnimate` / `kPulseMs` (1500) / `kBaseMs` (250) / `kStandardP1` / `kStandardP2` (UrMotion.h:41-101); `FrameworkElement::IsLoaded()` and `Loaded`.
- Produces: `void urmsg::views::SetNetworkPageAdvanced(NetworkPageView& v, bool advanced)` — the fixed contract §4 signature verbatim, density-only, never a crossfade. File-local `SetRelayPathAnimated(std::shared_ptr<PageParts> const&, bool)` and the `pulse`/`animate` fields on `PageParts`. `void MainWindow::ApplyAdvancedMode(bool advanced)` — PUBLIC, the one entry point the Advanced-Mode subscriber fans out through — plus the single `urmsg::OnAdvancedModeChanged` registration in the MainWindow constructor.

## Task N6 — Advanced Mode: per-hop timings, the round-trip figure, the wire pulse

Design D6 is why this is its own task: a wire that moves animates a protocol that does not exist,
so it is quarantined behind Advanced Mode on this one page. Everything here is additive to what
N3 built — the hop lines already exist and are Collapsed.

**Design §6.6's Advanced row for this page is "+ animated wires, per-hop timings", and all three
parts are now backed by data.** The per-hop figures are `RelayNode::hopMs`, one per node. The
round-trip figure is `ServerInfo::latencyMs`, centred under the **whole** diagram and worded
"round trip", because `ServerInfo` carries one end-to-end number and attaching it to a single hop
would be a fabricated per-hop timing.

**No new duration and no new curve.** `kPulseMs` on the standard bezier, with wireB offset by
`kBaseMs` through `MakeSplineDouble`'s existing `beginMs` stagger argument.

**Motion cannot be proved from a still, so this task does not try.** The Advanced/Normal
difference is verified from four countable lines that appear; the pulse is verified from the log
and from watching the live window.

- [ ] **Step 1: add the pulse fields to `PageParts`.** In
      `app/src/App/Views/NetworkPageView.cpp`, add `#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>`
      and `#include "UrMotion.h"` to the include block, add
      `namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;` beside the `shapes` alias,
      and add to `PageParts` after `hopLines`:

```cpp
  // Built by MakeRelayPath and left STOPPED. SetRelayPathAnimated is the only
  // caller of Begin().
  anim::Storyboard pulse{nullptr};
  bool animate = false;
  // Advanced-only figure under the diagram.
  TextBlock roundTrip{nullptr};
```

- [ ] **Step 2: build the pulse in `MakeRelayPath`.** Insert immediately before
      `parts->pathRoot = grid;`:

```cpp
  // Built here and left STOPPED (design D6). kPulseMs on the standard curve,
  // with wireB offset by kBaseMs so the path reads as flowing device -> server
  // rather than blinking as one object. Both are existing UrMotion tokens; this
  // adds no seventh duration.
  //
  // AutoReverse DOUBLES the cycle: one full brighten-and-dim is 2 x kPulseMs =
  // 3000 ms, not 1500.
  anim::RepeatBehavior forever{};
  forever.Type = anim::RepeatBehaviorType::Forever;

  anim::Storyboard board;
  auto pulseA = urnw::motion::MakeSplineDouble(0.35, 1.0, urnw::motion::kPulseMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
  pulseA.RepeatBehavior(forever);
  pulseA.AutoReverse(true);
  anim::Storyboard::SetTarget(pulseA, parts->wireA);
  anim::Storyboard::SetTargetProperty(pulseA, L"Opacity");
  board.Children().Append(pulseA);

  auto pulseB = urnw::motion::MakeSplineDouble(0.35, 1.0, urnw::motion::kPulseMs,
                                               urnw::motion::kBaseMs,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
  pulseB.RepeatBehavior(forever);
  pulseB.AutoReverse(true);
  anim::Storyboard::SetTarget(pulseB, parts->wireB);
  anim::Storyboard::SetTargetProperty(pulseB, L"Opacity");
  board.Children().Append(pulseB);

  parts->pulse = board;

  // Started from Loaded, never from the constructor. BuildNetworkPage runs from
  // the MainWindow constructor — before Activate(), and while NetworkPage is
  // still Collapsed — so Begin() there would start a clock against a subtree
  // that has not been loaded. WindowReveal already sets this pattern: arm in the
  // constructor, start after the tree is up.
  grid.Loaded([parts](auto const&, auto const&) {
    if (!parts->animate || !parts->pulse) return;
    parts->pulse.Begin();
    urnw::LogInfo("network: relay pulse begun on Loaded");
  });
```

- [ ] **Step 3: add the file-local `SetRelayPathAnimated`.** Add to the anonymous namespace, after
      `MakeRelayPath`:

```cpp
// Start or stop the wire pulse. File-local on purpose: the fixed contract's
// public API for this surface is MakeNetworkPage + SetNetworkPageAdvanced, and
// nothing outside this file needs to reach a wire.
void SetRelayPathAnimated(std::shared_ptr<PageParts> const& parts, bool animated) {
  if (!parts || !parts->pulse) return;
  // "Show animations in Windows = off" means motion is GONE, not reduced. An
  // Advanced-Mode user with that setting gets the static diagram, correct and
  // instant — which is also the only honest reading of D6 for them.
  parts->animate = animated && urnw::motion::ShouldAnimate();
  const bool loaded = parts->pathRoot && parts->pathRoot.IsLoaded();
  if (parts->animate) {
    // If the tree is not up yet, the Loaded handler in MakeRelayPath starts it.
    if (loaded) parts->pulse.Begin();
  } else {
    parts->pulse.Stop();
    // Written back explicitly rather than trusting Stop() to restore it: the
    // static wire must be the full-strength hairline, not whatever value the
    // timeline happened to be holding when it was stopped.
    if (parts->wireA) parts->wireA.Opacity(1.0);
    if (parts->wireB) parts->wireB.Opacity(1.0);
  }
  urnw::LogInfo("network: relay pulse animate={} shouldAnimate={} loaded={}",
                parts->animate, urnw::motion::ShouldAnimate(), loaded);
}
```

- [ ] **Step 4: add the round-trip line to the page.** In `MakeNetworkPage`, insert immediately
      after `pathColumn.Children().Append(pathScroll);`:

```cpp
  // ADVANCED ONLY. ServerInfo carries ONE end-to-end latency and no per-hop
  // split, so this says "round trip" and is centred under the whole diagram.
  // The PER-HOP figures are the lines inside each node (RelayNode::hopMs) and
  // are the only thing on this page entitled to name a hop.
  parts->roundTrip = TextBlock();
  if (auto style = StyleByKey(L"UrRowNoteStyle")) parts->roundTrip.Style(style);
  parts->roundTrip.Text(winrt::hstring{FormatRoundTrip(world.server)});
  parts->roundTrip.HorizontalAlignment(HorizontalAlignment::Center);
  parts->roundTrip.Visibility(Visibility::Collapsed);
  pathColumn.Children().Append(parts->roundTrip);
```

- [ ] **Step 5: declare and define `SetNetworkPageAdvanced`.** In
      `app/src/App/Views/NetworkPageView.h`, add after `MakeNetworkPage`:

```cpp
// DENSITY ONLY (fixed contract §4). It shows and hides lines that are already
// built and starts or stops the wire pulse. It never rebuilds the page and never
// runs a mode crossfade — a Set*Advanced that faded would blank a visible
// surface to opacity 0 and fade it back over itself.
void SetNetworkPageAdvanced(NetworkPageView& v, bool advanced);
```

      and append the definition to `namespace urmsg::views` at the foot of
      `NetworkPageView.cpp`:

```cpp
void SetNetworkPageAdvanced(NetworkPageView& v, bool advanced) {
  auto parts = Find(v.root);
  if (!parts) return;
  for (auto const& hop : parts->hopLines)
    hop.Visibility(advanced ? Visibility::Visible : Visibility::Collapsed);
  if (parts->roundTrip)
    parts->roundTrip.Visibility(advanced ? Visibility::Visible : Visibility::Collapsed);
  SetRelayPathAnimated(parts, advanced);
  urnw::LogInfo("network: advanced mode -> {}", advanced);
}
```

- [ ] **Step 6: give MainWindow the one public fan-out point.** First look:

```
git grep -n "ApplyAdvancedMode\|MainWindow::SetAdvanced" -- app/src/App
```

      **If it prints nothing**, add to the PUBLIC section of `app/src/App/MainWindow.xaml.h`,
      after `void StartReveal();` (:38):

```cpp
  // Advanced Mode, fanned out to every view. AdvancedMode.h owns the flag
  // (fixed contract §5); this is the ONE subscriber the window registers, so no
  // view reads the preference itself. Public so a Settings surface has a named
  // thing to reach.
  void ApplyAdvancedMode(bool advanced);
```

      **If it prints a hit**, that function already exists: skip this step and step 7's function
      body, and do only the one-line edit named at the end of step 7.

- [ ] **Step 7: define it and register the subscriber.** In `app/src/App/MainWindow.xaml.cpp`, add
      `#include "Demo/AdvancedMode.h"` beside `#include "Demo/DemoWorld.h"`.

      **If `ApplyAdvancedMode` / `SetAdvanced` did not already exist**, add this function
      immediately after `MainWindow::BuildNetworkPage`:

```cpp
void MainWindow::ApplyAdvancedMode(bool advanced) {
  urmsg::views::SetNetworkPageAdvanced(network_, advanced);
}
```

      and add these two statements to the constructor, after `ApplyBreakpoint();` (:101) and
      before `ApplyDemoScreen();`:

```cpp
  // ONE subscriber for the whole window (fixed contract §5). weak, because
  // subscriptions are never removed and a closed window must not be called.
  urmsg::OnAdvancedModeChanged([weak = get_weak()](bool advanced) {
    if (auto self = weak.get())
      winrt::get_self<MainWindow>(self)->ApplyAdvancedMode(advanced);
  });
  ApplyAdvancedMode(urmsg::AdvancedModeEnabled());
```

      **If it already existed**, make exactly one edit — add this line to that function's body and
      register nothing:

```cpp
  urmsg::views::SetNetworkPageAdvanced(network_, advanced);
```

- [ ] **Step 8: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

      Expected: `0 Error(s)` and the green `OK in <N>s -> ...\app\build\x64\Release\`. A
      `C1083: 'Demo/AdvancedMode.h'` means the Advanced-Mode task has not landed — stop.

- [ ] **Step 9: capture Normal mode and confirm nothing changed.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network"
```

      Open `.verify/urmessage-window.png`. Expected: identical to N5's result. Concretely — each
      of the three nodes shows **exactly two** text lines under its glyph, there is **no** figure
      between the diagram and the panel's bottom hairline, and both wires are the same uniform
      faint white.

- [ ] **Step 10: capture Advanced mode.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network --demo-advanced"
```

      Leave the app running — step 12 watches it.

- [ ] **Step 11: read the Advanced screenshot.** Open `.verify/urmessage-window.png`. Expected,
      concretely — **four** lines that were not there in step 9:
      1. Each of the three nodes now shows a **third** line under its sub-label, centred, 11px,
         faint `#5A5A5A`, reading a figure ending in ` ms`. Three nodes, three figures.
      2. Below the diagram and about 16px under it, one centred 11px muted line reading e.g.
         `18 ms round trip` — the same number as the `Latency` row's value in the MESSAGE SERVER
         group, plus that qualifier. Scroll up and compare the two on the one image.
      3. Nothing else on the page moved: the boxes are in the same place, the same size, with the
         same glyphs. The panel grew taller by one line of text.

- [ ] **Step 12: prove the pulse is running — from the log and the live window, not the still.**

```
Select-String -Path .localstate-verify\logs\urmessage-app.log -Pattern 'network: relay pulse|network: advanced mode'
```

      Expected, in this order:

```
network: advanced mode -> true
network: relay pulse animate=true shouldAnimate=true loaded=false
network: relay pulse begun on Loaded
```

      `loaded=false` on the first line is correct and is the whole point of the Loaded handler:
      `BuildNetworkPage` runs from the constructor, before `Activate()`, on a Collapsed subtree.

      Then **look at the running window for five seconds.** Expected: both wires visibly brighten
      and dim over a 3000 ms cycle (1500 ms out, 1500 ms back — `AutoReverse`), with the right
      wire a quarter-second behind the left. Do not try to see this in a screenshot: the standard
      bezier is ~0.99 of the way through by half the duration, so for much of the cycle the two
      wires are pixel-identical, and a still that shows them equal proves nothing either way.

- [ ] **Step 13: confirm the reduce-motion path.** Turn OFF Windows Settings > Accessibility >
      Visual effects > Animation effects, then re-run step 10 and read the image and the log
      again.

      Expected: the three per-hop figures and the round-trip line are **still there**, both wires
      are at full strength and equal, nothing on the page is missing or half-drawn, and the log
      reads `network: relay pulse animate=false shouldAnimate=false loaded=false` with **no**
      `begun on Loaded` line after it. Turn the setting back on.

- [ ] **Step 14: confirm a plain launch still works.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1
```

      Expected: `dips        : 480x760`, the Chats destination renders, and the app does not
      crash — `BuildNetworkPage` and the Advanced subscriber both run on this path too, on a
      window that never shows the Network page.

- [ ] **Step 15: commit.** No new files, so **65 -> 65**:

```
git ls-files | Measure-Object -Line
git add -A
git ls-files | Measure-Object -Line
git commit -m "demo: Advanced Mode on the Network page - per-hop timings, round trip and the wire pulse"
```

**Deliverable, independently checkable:** `--demo=network` and `--demo=network --demo-advanced`
produce pages that differ by exactly four visible lines — three per-hop figures and one round-trip
figure — and `MainWindow::ApplyAdvancedMode`, called by the single `OnAdvancedModeChanged`
subscriber, switches between them live with no restart and no crossfade.
