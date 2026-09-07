# Conversation list

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task L1: Conversation row model — the list's decisions, asserted in --diagnose

**Files:**

Create app/src/App/Views/ConversationRowModel.h; Create app/src/App/Views/ConversationRowModel.cpp; Modify app/src/App/App.vcxproj; Modify app/src/App/Startup.cpp

**Interfaces:**

- Consumes: F2 (Demo/DemoWorld.*, Demo/DemoSwitches.*, Identicon.*, Demo/AdvancedMode.*): urmsg::demo::World, Conversation, ConversationKind, GetWorld() — contract §1. urnw::motion::kStaggerMs, kMaxStaggerSteps (app/src/App/UrMotion.h:45-46). urnw::CollectDiagnostics() (app/src/App/Startup.cpp:178).
- Produces: urmsg::views::ConversationRowModel {name, preview, showTimer, showMuted, unread, timeLabel, groupIdChip}; ConversationRowModel MakeConversationRowModel(demo::Conversation const&); std::wstring ConversationRowAutomationName(demo::Conversation const&, bool selected); int64_t ConversationRowDelayMs(std::size_t index); bool ConversationRowMatches(demo::Conversation const&, std::wstring const& query); std::vector<std::wstring> CollectConversationListDiagnostics()

### L1. Conversation row model — the list's decisions, asserted in `--diagnose`

**Foundation tasks this surface names, once, unambiguously.** Referred to by these labels
everywhere below, never as "Task 1" or "Task 2":

- **F1** — adds `-AppArgs` to `app/tools/verify-render.ps1` (contract §0.2). Nothing in L2–L6
can take a screenshot until it lands.
- **F2** — `Demo/DemoWorld.*`, `Demo/DemoSwitches.*`, `Identicon.*`, `Demo/AdvancedMode.*`
(contract §§1, 2, 3, 5), and D7's `--demo` window size of 1560×900 DIP.

**Why the model is a separate, WinRT-free header.** `main.cpp:168` calls
`urnw::CollectDiagnostics()` inside its first `try` block, **before** `winrt::init_apartment()`
at `main.cpp:180`. So anything asserted from there must construct no WinRT object.
`Views/ConversationRowModel.h` names no XAML type — but be honest about what that buys:
`ConversationRowModel.cpp` starts with `#include "pch.h"`, which pulls in the whole
`winrt/Microsoft.UI.Xaml*` projection, so **nothing in the compiler stops a later edit from
constructing a `Border` here.** The rule is enforced by review, and by step 9's `--diagnose` run
actually completing rather than faulting. It is not a compile-time property and must not be sold
as one.

**Strings.** `app/src/App/Strings/en/Resources.resw` is GENERATED (`Localization.h:3-4`); this
plan adds no key to it. Everything user-visible that this file decides is an English literal, and
the two keys it does reuse (`group_recent`, `search_conversations`) already exist with the values
quoted below.

**Diagnostics are launch-independent, by design.** `CollectDiagnostics()` runs on **every**
launch, not only under `--diagnose` (`main.cpp:168`, with the `WantsDiagnose()` branch at :170
coming after), and `LogDiagnostics` writes it to the app log. That is deliberate here: these
seven lines assert a pure function of seeded data, not app state, so they are as true on a plain
launch as under `--demo`. L2 step 12 checks that a plain launch's log carries them, so the
decision is recorded in a check rather than in a comment.

- [ ] **Step 1: create `app/src/App/Views/ConversationRowModel.h`.**

```cpp
// The conversation row's DECISIONS, separated from its pixels.
//
// main.cpp:168 calls CollectDiagnostics() BEFORE winrt::init_apartment (:180),
// so every invariant --diagnose asserts has to be reachable without a WinRT
// object. This header names no XAML type. That is a CONVENTION enforced by
// review, not by the compiler: the .cpp includes pch.h, which makes the whole
// projection reachable regardless.
//
// It is also the honest split. "The disappearing row shows a timer INSTEAD of
// its preview" is a claim about content, and a claim about content is testable
// without a window.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

struct ConversationRowModel {
  std::wstring name;
  // EMPTY when showTimer is true. Spec C 4.2 / design 6.1: a disappearing
  // conversation shows the timer glyph INSTEAD of a preview. The world's
  // preview is NON-empty on that conversation (contract 1.1 rule 3), so this is
  // a refusal to carry it, not an absence of anything to carry -- which is what
  // makes the invariant falsifiable and stops it leaking back through a filter,
  // an automation name or a tooltip later.
  std::wstring preview;
  bool showTimer = false;
  bool showMuted = false;
  // "" when nothing is unread; "99+" above 99, because three digits do not fit
  // a pill in a 320dip list column.
  std::wstring unread;
  std::wstring timeLabel;
  // Advanced Mode only, and only on a group: at most the first 6 chars of
  // Conversation::groupIdHex. Empty on every DM. Design 6.6's "+ group id chip".
  std::wstring groupIdChip;
};

ConversationRowModel MakeConversationRowModel(demo::Conversation const& c);

// What a screen reader is told. This is selection's THIRD channel: the fill step
// and the 2px accent bar are the other two, and colour alone is never the
// carrier of a state -- kit::SetPaneListRowSelected carries the same three for
// the same reason (UrComponents.h:333-340).
std::wstring ConversationRowAutomationName(demo::Conversation const& c, bool selected);

// The row's entrance delay: kStaggerMs per step, capped at kMaxStaggerSteps.
// int64_t because motion::MakeSplineDouble takes its beginMs as one
// (UrMotion.h:100-101).
int64_t ConversationRowDelayMs(std::size_t index);

// Does the row survive `query`? Case-insensitive substring over the name and,
// for a conversation that is NOT disappearing, its preview. A disappearing
// conversation is searchable BY NAME ONLY: matching on a preview the row refuses
// to draw would put the hidden text back on screen by way of the result set.
bool ConversationRowMatches(demo::Conversation const& c, std::wstring const& query);

// This surface's --diagnose assertions. One PASS/FAIL line per invariant in the
// "  key              : value" shape CollectDiagnostics already uses. Pure C++:
// safe to call before init_apartment.
std::vector<std::wstring> CollectConversationListDiagnostics();

}  // namespace urmsg::views
```

- [ ] **Step 2: create `app/src/App/Views/ConversationRowModel.cpp` with REAL assertions and
      STUB decisions.** The assertions are the test, so they are real from the first line; the
      four functions under test return defaults, so step 6 fails all seven.

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/ConversationRowModel.h"

#include <algorithm>
#include <cwctype>
#include <format>

#include "UrMotion.h"

namespace urmsg::views {
namespace {

std::wstring Lower(std::wstring const& text) {
  std::wstring out = text;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return out;
}

// The diagnostics line shape, measured off the existing output rather than
// guessed: "  build            : x64 ..." is two spaces, a 17-wide key, then
// ": " (Startup.cpp:190).
std::wstring Line(wchar_t const* key, bool pass, std::wstring const& detail) {
  return std::format(L"  {:<17}: {}  {}", key, pass ? L"PASS" : L"FAIL", detail);
}

}  // namespace

// ---- STUBS: replaced in steps 7 and 8 --------------------------------------
ConversationRowModel MakeConversationRowModel(demo::Conversation const&) { return {}; }
std::wstring ConversationRowAutomationName(demo::Conversation const&, bool) { return {}; }
int64_t ConversationRowDelayMs(std::size_t) { return 0; }
bool ConversationRowMatches(demo::Conversation const&, std::wstring const&) { return true; }

std::vector<std::wstring> CollectConversationListDiagnostics() {
  // GetWorld() is built once, seeded, and is pure data (std::wstring /
  // std::vector), so calling it here needs no apartment. Contract 1.
  demo::World const& world = demo::GetWorld();
  const std::size_t total = world.conversations.size();
  std::vector<std::wstring> lines;

  // 1. Every row says who it is and when.
  bool rowsOk = (0 < total);
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    if (m.name.empty() || m.timeLabel.empty()) rowsOk = false;
  }
  lines.push_back(Line(L"demo.list.rows", rowsOk,
                       std::format(L"{} rows, each with a name and a time label", total)));

  // 2. Spec C 4.2: the timer glyph REPLACES the preview. Stated as the claim
  //    actually is, and only in the direction that is falsifiable: the model
  //    drops a preview THAT EXISTS. Contract 1.1 rule 3 guarantees the EPH
  //    conversation's world preview is non-empty, so `dropped` cannot be 0 on a
  //    correct world -- and a 0 here fails rather than passing vacuously.
  //    The old form (m.preview.empty() != c.disappearing) was both vacuous on an
  //    empty EPH preview and a FALSE FAIL on any other empty preview.
  bool timerOk = true;
  std::size_t timerRows = 0;
  std::size_t dropped = 0;
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    if (m.showTimer != c.disappearing) timerOk = false;
    if (!c.disappearing) continue;
    ++timerRows;
    if (!m.preview.empty()) timerOk = false;
    if (!c.preview.empty()) ++dropped;
  }
  if (dropped != timerRows) timerOk = false;
  lines.push_back(Line(L"demo.list.timer", timerOk,
                       std::format(L"{} of {} rows show the timer; each dropped a "
                                   L"non-empty world preview ({} checked)",
                                   timerRows, total, dropped)));

  // 3. The unread pill, plus the clamp. The clamp is asserted on a LOCALLY built
  //    Conversation: DemoWorld has no 100-unread row, and an assertion that only
  //    ever sees 0..9 is not checking a clamp at all.
  bool unreadOk = true;
  std::size_t unreadRows = 0;
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    if (m.unread.empty() != (c.unread <= 0)) unreadOk = false;
    if (!m.unread.empty()) ++unreadRows;
  }
  demo::Conversation loud{};
  loud.unread = 120;
  const bool clampOk = (MakeConversationRowModel(loud).unread == L"99+");
  if (!clampOk) unreadOk = false;
  lines.push_back(Line(L"demo.list.unread", unreadOk,
                       std::format(L"pill shown iff unread > 0 ({} of {} rows); 120 "
                                   L"unread renders \"{}\"",
                                   unreadRows, total,
                                   MakeConversationRowModel(loud).unread)));

  // 4. Selection reaches a screen reader, and a group says how many PEOPLE are
  //    in it (contract 1.1 rule 2 pins memberCount == members.size()).
  bool selectOk = (0 < total);
  std::size_t groupRows = 0;
  for (auto const& c : world.conversations) {
    const std::wstring off = ConversationRowAutomationName(c, false);
    const std::wstring on = ConversationRowAutomationName(c, true);
    if (off == on) selectOk = false;
    if (on.find(L"Selected") == std::wstring::npos) selectOk = false;
    if (off.find(L"Selected") != std::wstring::npos) selectOk = false;
    if (c.kind != demo::ConversationKind::Group) continue;
    ++groupRows;
    if (off.find(std::to_wstring(c.memberCount) + L" members") == std::wstring::npos)
      selectOk = false;
  }
  lines.push_back(Line(L"demo.list.select", selectOk,
                       std::format(L"the selected row's name differs and says "
                                   L"\"Selected\" ({} of {}); {} group rows name "
                                   L"their member count",
                                   total, total, groupRows)));

  // 5. The stagger is CAPPED. Uncapped, a longer list turns its entrance into a
  //    seconds-long wipe; kMaxStaggerSteps is what stops that and nothing else
  //    checks it.
  const int64_t step = urnw::motion::kStaggerMs;
  const int64_t cap = static_cast<int64_t>(urnw::motion::kMaxStaggerSteps) * step;
  const bool staggerOk = ConversationRowDelayMs(0) == 0 &&
                         ConversationRowDelayMs(5) == 5 * step &&
                         ConversationRowDelayMs(6) == cap &&
                         ConversationRowDelayMs(50) == cap;
  lines.push_back(Line(L"demo.list.stagger", staggerOk,
                       std::format(L"{}/{}/{}/{} ms at index 0/5/6/50 ({} ms x max {})",
                                   ConversationRowDelayMs(0), ConversationRowDelayMs(5),
                                   ConversationRowDelayMs(6), ConversationRowDelayMs(50),
                                   step, urnw::motion::kMaxStaggerSteps)));

  // 6. The filter cannot resurrect a hidden preview. leakChecks is asserted to
  //    be non-zero: contract 1.1 rule 3 guarantees the material exists, so a 0
  //    here means the world changed under this assertion, not that it passed.
  bool searchOk = true;
  std::size_t matchAll = 0;
  std::size_t leakChecks = 0;
  for (auto const& c : world.conversations) {
    if (ConversationRowMatches(c, L"")) ++matchAll;
    if (!ConversationRowMatches(c, c.name)) searchOk = false;
    if (!c.disappearing || c.preview.empty()) continue;
    ++leakChecks;
    if (ConversationRowMatches(c, c.preview)) searchOk = false;
  }
  if (matchAll != total || leakChecks == 0) searchOk = false;
  lines.push_back(Line(L"demo.list.search", searchOk,
                       std::format(L"empty query matches {} of {}; every row findable "
                                   L"by name; {} hidden-preview leak check(s)",
                                   matchAll, total, leakChecks)));

  // 7. The Advanced group-id chip: on groups only, and a real prefix of the
  //    world's hex rather than a string this file invented. Contract 1.1 rule 9
  //    guarantees a non-empty groupIdHex on every group.
  bool chipOk = true;
  std::size_t chips = 0;
  std::size_t dmRows = 0;
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    const bool isGroup = (c.kind == demo::ConversationKind::Group);
    if (!isGroup) {
      ++dmRows;
      if (!m.groupIdChip.empty()) chipOk = false;
      continue;
    }
    ++chips;
    if (m.groupIdChip.empty() || 6 < m.groupIdChip.size()) chipOk = false;
    if (c.groupIdHex.rfind(m.groupIdChip, 0) != 0) chipOk = false;
  }
  if (chips == 0) chipOk = false;
  lines.push_back(Line(L"demo.list.group", chipOk,
                       std::format(L"{} group rows carry a <=6-char prefix of their "
                                   L"groupIdHex; {} DM rows carry none",
                                   chips, dmRows)));
  return lines;
}

}  // namespace urmsg::views
```

- [ ] **Step 3: register both files in `app/src/App/App.vcxproj`.** The source lists are
      explicit (there is no wildcard) and `AdditionalIncludeDirectories` already carries
      `$(MSBuildProjectDirectory)` (`App.vcxproj:125`), so `#include "Views/ConversationRowModel.h"`
      resolves from anywhere in the project. One line in each `ItemGroup`, in the existing order:

```xml
    <ClCompile Include="UrMotion.cpp" />
    <ClCompile Include="Views\ConversationRowModel.cpp" />
    <ClCompile Include="WindowReveal.cpp" />
```

```xml
    <ClInclude Include="UrMotion.h" />
    <ClInclude Include="Views\ConversationRowModel.h" />
    <ClInclude Include="WindowReveal.h" />
```

- [ ] **Step 4: call the collector from `CollectDiagnostics()` in `app/src/App/Startup.cpp`.**
      Add the include to the block at `Startup.cpp:16-19`:

```cpp
#include "Strings.h"
#include "Views/ConversationRowModel.h"
```

  and replace the last three lines of `CollectDiagnostics()` (`Startup.cpp:215-218`):

```cpp
  lines.push_back(std::format(L"  fonts            : {}",
                              Presence(dir / L"Assets" / L"Fonts" /
                                       L"pp_neue_bit_bold.ttf")));
  return lines;
```

  with:

```cpp
  lines.push_back(std::format(L"  fonts            : {}",
                              Presence(dir / L"Assets" / L"Fonts" /
                                       L"pp_neue_bit_bold.ttf")));
  // The demo surfaces' own invariants. One collector per surface, and every one
  // of them must be pure C++: this function runs before winrt::init_apartment
  // (main.cpp:168 vs :180), so a WinRT object built from here would die on the
  // line that constructs it, inside the code whose job is to explain deaths.
  // Unconditional on purpose -- these assert a pure function of seeded data, not
  // app state, so they are as true on a plain launch as under --demo.
  for (auto const& line : urmsg::views::CollectConversationListDiagnostics())
    lines.push_back(line);
  return lines;
```

- [ ] **Step 5: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

  Expected last line: `OK in <n>s -> ...\app\build\x64\Release\`.

- [ ] **Step 6: run `--diagnose` and SEE all seven FAIL.**

```
powershell -ExecutionPolicy Bypass -Command "New-Item -ItemType Directory -Force .verify | Out-Null; Start-Process -FilePath 'app\build\x64\Release\URmessage.exe' -ArgumentList '--diagnose' -RedirectStandardOutput '.verify\diagnose.txt' -RedirectStandardError '.verify\diagnose.err.txt' -Wait -NoNewWindow; Select-String -Path '.verify\diagnose.txt' -Pattern 'demo.list'"
```

  Expected — seven lines, every one `FAIL` (the parenthesised figures are reported data, not
  verdicts, and read as zeros while the stubs are in place):

```
  demo.list.rows   : FAIL  8 rows, each with a name and a time label
  demo.list.timer  : FAIL  1 of 8 rows show the timer; each dropped a non-empty world preview (0 checked)
  demo.list.unread : FAIL  pill shown iff unread > 0 (0 of 8 rows); 120 unread renders ""
  demo.list.select : FAIL  the selected row's name differs and says "Selected" (8 of 8); 2 group rows name their member count
  demo.list.stagger: FAIL  0/0/0/0 ms at index 0/5/6/50 (40 ms x max 6)
  demo.list.search : FAIL  empty query matches 8 of 8; every row findable by name; 1 hidden-preview leak check(s)
  demo.list.group  : FAIL  0 group rows carry a <=6-char prefix of their groupIdHex; 6 DM rows carry none
```

  If `demo.list.timer` reports `(0 checked)` after step 9 too, that is F2's world violating
  contract §1.1 rule 3 (the EPH conversation must carry a non-empty preview). Fix F2; do not
  weaken this assertion.

- [ ] **Step 7: implement `MakeConversationRowModel` and `ConversationRowAutomationName`.**
      Replace the first two stub lines with:

```cpp
ConversationRowModel MakeConversationRowModel(demo::Conversation const& c) {
  ConversationRowModel model;
  model.name = c.name;
  model.showTimer = c.disappearing;
  model.showMuted = c.muted;
  model.timeLabel = c.timeLabel;
  // Not "copy it and collapse the TextBlock": the model never carries a preview
  // it is not allowed to draw (Spec C 4.2). The world's preview is non-empty on
  // this conversation, which is what makes the refusal visible in --diagnose.
  if (!c.disappearing) model.preview = c.preview;
  if (0 < c.unread)
    model.unread = (99 < c.unread) ? std::wstring(L"99+") : std::to_wstring(c.unread);
  if (c.kind == demo::ConversationKind::Group && !c.groupIdHex.empty()) {
    // (std::min) parenthesised, matching WindowShell.cpp:166. NOMINMAX IS
    // defined project-wide (app/Directory.Build.props:75), so the bare name
    // would compile -- this is the house form, kept so a TU that ever loses that
    // definition still builds.
    model.groupIdChip =
        c.groupIdHex.substr(0, (std::min)(static_cast<std::size_t>(6), c.groupIdHex.size()));
  }
  return model;
}

std::wstring ConversationRowAutomationName(demo::Conversation const& c, bool selected) {
  const ConversationRowModel model = MakeConversationRowModel(c);
  std::wstring name = model.name;
  if (c.kind == demo::ConversationKind::Group)
    name += L", " + std::to_wstring(c.memberCount) + L" members";
  if (model.showTimer) {
    // The timer FontIcon is AccessibilityView=Raw in the row, so this is the
    // ONLY place a screen reader hears that this conversation disappears.
    name += L", disappearing messages";
  } else {
    name += L", " + model.preview;
  }
  if (!model.unread.empty()) name += L", " + model.unread + L" unread";
  if (model.showMuted) name += L", muted";
  name += L", " + model.timeLabel;
  if (selected) name += L". Selected";
  return name;
}
```

- [ ] **Step 8: implement `ConversationRowDelayMs` and `ConversationRowMatches`.** Replace the
      remaining two stub lines with:

```cpp
int64_t ConversationRowDelayMs(std::size_t index) {
  const std::size_t steps =
      (std::min)(index, static_cast<std::size_t>(urnw::motion::kMaxStaggerSteps));
  return static_cast<int64_t>(steps) * urnw::motion::kStaggerMs;
}

bool ConversationRowMatches(demo::Conversation const& c, std::wstring const& query) {
  const std::wstring needle = Lower(query);
  if (needle.empty()) return true;
  if (Lower(c.name).find(needle) != std::wstring::npos) return true;
  // A disappearing conversation is searchable by NAME ONLY. Matching its preview
  // would put the text the row refuses to draw back on screen by way of the
  // result set -- the same leak, one indirection later.
  if (c.disappearing) return false;
  return Lower(c.preview).find(needle) != std::wstring::npos;
}
```

- [ ] **Step 9: rebuild and run `--diagnose` again.** Same two commands as steps 5 and 6.
      Expected — seven lines, every one `PASS`:

```
  demo.list.rows   : PASS  8 rows, each with a name and a time label
  demo.list.timer  : PASS  1 of 8 rows show the timer; each dropped a non-empty world preview (1 checked)
  demo.list.unread : PASS  pill shown iff unread > 0 (2 of 8 rows); 120 unread renders "99+"
  demo.list.select : PASS  the selected row's name differs and says "Selected" (8 of 8); 2 group rows name their member count
  demo.list.stagger: PASS  0/200/240/240 ms at index 0/5/6/50 (40 ms x max 6)
  demo.list.search : PASS  empty query matches 8 of 8; every row findable by name; 1 hidden-preview leak check(s)
  demo.list.group  : PASS  2 group rows carry a <=6-char prefix of their groupIdHex; 6 DM rows carry none
```

  `8`, `1 of 8`, `2 of 8`, `2 group rows` are F2's numbers, fixed by contract §1.1 rules 1–5 and
  design §5. If any of them reads differently, reconcile it with F2 — do not change an assertion
  to match whatever came out.

  The run completing at all is the second thing this step proves: `CollectDiagnostics()` runs
  before `winrt::init_apartment`, so a WinRT object built in `ConversationRowModel.cpp` would
  fault here rather than at a call site.

- [ ] **Step 10: commit.**

```
git add app/src/App/Views/ConversationRowModel.h app/src/App/Views/ConversationRowModel.cpp app/src/App/App.vcxproj app/src/App/Startup.cpp
git commit -m "demo: the conversation row model, with its Spec C 4.1/4.2 invariants asserted in --diagnose"
git show --stat --oneline HEAD
```

  Expected: `git show --stat` lists exactly those four paths and nothing else. This box drops
  files out of a commit's tree silently, so read the list rather than assuming it.

**Deliverable:** `URmessage.exe --diagnose` prints seven `demo.list.*` lines, all `PASS`. Nothing
on screen has changed.

---

## Task L2: The Spec C §4.1 row, and the demo conversation list on screen

**Files:**

Create app/src/App/Views/ConversationListView.h; Create app/src/App/Views/ConversationListView.cpp; Modify app/src/App/App.vcxproj; Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: L1: urmsg::views::ConversationRowModel, MakeConversationRowModel, ConversationRowAutomationName. F1: verify-render.ps1 -AppArgs. F2: urmsg::demo::GetWorld, ParseDemoOptions, DemoOptions (contract §§1-2); urmsg::MakeIdenticon (contract §3); --demo opening at 1560×900 DIP (D7). Kit: urnw::kit::MakePaneTwoLineRowButton, PaneTwoLineRowButton, MakePaneGroupHeader, MakePaneSearchRow (app/src/App/UrComponents.h:385-432). Colours: urnw::colors::AccentBrush, MutedBrush, FaintBrush, MakeBrush, kInverseText (app/src/App/UrColors.h). x:Names ConversationList, SearchHost, ListPaneCount (MainWindow.xaml:147,153,160).
- Produces: urmsg::views::ConversationListView {winrt::Microsoft::UI::Xaml::FrameworkElement root; std::vector<urnw::kit::PaneTwoLineRowButton> rows;}; inline constexpr double kConversationRowHeight = 64.0; ConversationListView MakeConversationList(urmsg::demo::World const& world, std::function<void(int)> onSelect); MainWindow member urmsg::views::ConversationListView list_; MainWindow::OnConversationSelected(int index)

### L2. The Spec C §4.1 row, and the demo conversation list on screen

**Blocking prerequisite: F1.** Step 10 runs `verify-render.ps1 -AppArgs "--demo=chats"`. On `main`
today that script is `[CmdletBinding()] param($Configuration, $Platform, $SettleMs)` and its
launch line is `$proc = Start-Process -FilePath $exe -PassThru` (`verify-render.ps1:30-35, 156`)
— **no parameter and no forwarding**, and `[CmdletBinding()]` makes an undeclared switch a
terminating binding error, not an ignored one. If step 10 prints
`A parameter cannot be found that matches parameter name 'AppArgs'`, F1 has not landed. Stop and
land F1; do not add a parameter here and do not use `-Args` (it shadows the automatic `$args`).

**Window size, stated once and relied on by every capture below.** A plain launch is **480×760
DIP** (`WindowShell.h:21-25`). Under `--demo` F2/D7 opens **1560×900 DIP**, which is above
`kit::kWideBreakpointDip` (1000, `UrComponents.h:75`), so `MainWindow::ApplyBreakpoint` pins
`ListColumn` to **320 DIP**, and `PaneRule` and `ThreadPane` are **visible**
(`MainWindow.xaml.cpp:184-190`). Every `--demo` capture in L2–L6 is therefore the **two-pane**
layout with a 320-dip list column, not "the list fills the window". Nothing restores an older
size: `urnw::shell::SaveWindowPlacement` is defined at `WindowShell.cpp:346` and **has no caller
in `app/src`**, so `LoadPlacement()` always misses and the size is always the launch default.

**Do not read verify-render's five `pixel probes` lines as a gate under `--demo`.** They sample
x = 200 dip, which is inside the list pane at the 480-dip default but inside the
**NavigationView pane** at 1560 (`PaneDisplayMode="Auto"` gives the 220-dip expanded pane at
≥1008 epx, `MainWindow.xaml:66-78`). Under `--demo` those five numbers say nothing about this
list. Read the PNG.

**Why the row is `kit::MakePaneTwoLineRowButton` and not a hand-built Grid.** Contract §4 fixes
`std::vector<urnw::kit::PaneTwoLineRowButton> rows`, and `MainWindow.xaml:156-159` already says
the same thing in markup. It is also the correct choice on the merits: `kit::MakeTwoLineText`
puts the note in a **vertical** StackPanel (`UrComponents.cpp:334-352`), which is what lets
`UrRowNoteStyle`'s `TextTrimming=CharacterEllipsis` (`App.xaml:1010`) engage. A horizontal
StackPanel measures its children at infinite width, so a preview built that way is clipped
without an ellipsis — the exact defect design §6.1's "one-line ellipsized preview" forbids.

**`StyleByKey` is deliberately not copied.** It is file-local to `UrComponents.cpp:78`. Six
surfaces each copying it is the wrong end state, so this file sets the three FontIcon properties
it needs directly and reads only the one shared resource that must not drift — the icon font
family.

- [ ] **Step 1: record the pre-change baseline for the plain (non-demo) launch.** This is the
      evidence for design §8's "without `--demo` the app behaves exactly as it does today",
      which step 12 re-checks after the change.

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1
powershell -ExecutionPolicy Bypass -Command "Copy-Item .verify\urmessage-window.png .verify\baseline-plain.png -Force"
```

  Expected: a `dips        : 480x760` line, then a five-row `pixel probes` table. **Write those
  five probe values down** — step 12 must reproduce them exactly.

- [ ] **Step 2: create `app/src/App/Views/ConversationListView.h`.**

```cpp
// The conversation list pane (design doc 6.1; Spec C 4.1 row anatomy).
//
// The kit grain, not MVVM: a struct of named elements plus free Make*/Set*
// functions, exactly as UrComponents.h does it. No class with virtuals, no
// observable type, no IDL -- so a list built here and a pane declared in markup
// are the same pane.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"
#include "UrComponents.h"

namespace urmsg::views {

// 64, not the kit's UrPaneRowTallHeight of 44 (App.xaml:1005): a conversation
// row carries a 40dip identicon plus two text lines, and 44 would put 40 of
// identicon inside 44 of row. ONE height for the whole list, passed to every
// MakePaneTwoLineRowButton call -- the property MakePaneRow's comment
// (UrComponents.cpp:153-158) exists to protect.
inline constexpr double kConversationRowHeight = 64.0;

struct ConversationListView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  // rows[i] is world.conversations[i]. Nothing reorders this vector: contract 1
  // limits MutableWorld() to appending a MessageRow and advancing one
  // DeliveryState, so an index stays valid for the life of the view.
  std::vector<urnw::kit::PaneTwoLineRowButton> rows;
};

// The whole list: the RECENT group header, then one row per conversation.
// `onSelect` is called with the row's INDEX -- the same index
// SetConversationSelected takes, and an int captured by value cannot dangle the
// way a reference into world.conversations could.
ConversationListView MakeConversationList(urmsg::demo::World const& world,
                                          std::function<void(int)> onSelect);

// The rest of contract 4's ConversationListView API (SetConversationSelected,
// SetConversationListAdvanced) is declared by the task that IMPLEMENTS it. A
// declaration without a definition is a link error waiting for whoever builds
// next.

}  // namespace urmsg::views
```

- [ ] **Step 3: create `app/src/App/Views/ConversationListView.cpp` — file header, helpers, and
      the two small builders.**

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/ConversationListView.h"

#include <cstdint>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Text.h>

#include "Identicon.h"
#include "Localization.h"
#include "Log.h"
#include "UrColors.h"
#include "Views/ConversationRowModel.h"

using namespace winrt::Microsoft::UI::Xaml;

namespace urmsg::views {
namespace {

// Tags, so the state setters in later tasks can find the three elements this
// file builds without ConversationListView growing a field for each of them
// (contract 4 fixes the struct at root + rows).
constexpr wchar_t kTagSelectionBar[] = L"ur.selbar";
constexpr wchar_t kTagTrailing[] = L"ur.trailing";
constexpr wchar_t kTagCluster[] = L"ur.cluster";
constexpr wchar_t kTagGroupChip[] = L"ur.groupchip";

// Segoe Fluent Icons out of the app dictionary, so this file and App.xaml cannot
// name two different families. FontIcon otherwise defaults to the older Segoe
// MDL2 Assets, whose metrics differ -- UrComponents.cpp:25-29 records the same
// trap. This is NOT a copy of StyleByKey: it reads one resource, by one name.
Media::FontFamily IconFont() {
  if (auto app = Application::Current()) {
    auto key = winrt::box_value(winrt::hstring{L"UrIconFontFamily"});
    if (app.Resources().HasKey(key))
      if (auto family = app.Resources().Lookup(key).try_as<Media::FontFamily>()) return family;
  }
  return Media::FontFamily(L"Segoe Fluent Icons");
}

// A state mark on the row's SECOND line. 12, not UrRowIconStyle's 16
// (App.xaml:523-530): a 16px glyph beside 11px note text outweighs the name
// above it.
Controls::FontIcon MakeRowGlyph(winrt::hstring const& glyph) {
  Controls::FontIcon icon;
  icon.FontFamily(IconFont());
  icon.FontSize(12);
  icon.Glyph(glyph);
  icon.Foreground(urnw::colors::MutedBrush());
  icon.VerticalAlignment(VerticalAlignment::Center);
  // The row's automation name already says "muted" / "disappearing messages".
  Automation::AutomationProperties::SetAccessibilityView(
      icon, Automation::Peers::AccessibilityView::Raw);
  return icon;
}

// The unread pill: UrAccentBrush #EFF7BB with UrInverseTextBrush #101010 text,
// per design 6.1. kProGold appears nowhere, and this is the ONLY chromatic mark
// on a row -- Spec C 4.1: a list row never carries a delivery-state colour.
Controls::Border MakeUnreadPill(std::wstring const& text) {
  Controls::Border pill;
  pill.Background(urnw::colors::AccentBrush());
  // CornerRadius is a plain struct {TopLeft, TopRight, BottomRight, BottomLeft};
  // all four are the same here, so the declaration order cannot be got wrong.
  pill.CornerRadius(CornerRadius{9, 9, 9, 9});
  pill.Height(18);
  pill.MinWidth(18);
  pill.Padding(ThicknessHelper::FromLengths(6, 0, 6, 0));
  pill.VerticalAlignment(VerticalAlignment::Center);

  Controls::TextBlock count;
  count.Text(winrt::hstring{text});
  count.FontSize(11);
  count.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
  count.Foreground(urnw::colors::MakeBrush(urnw::colors::kInverseText));
  count.HorizontalAlignment(HorizontalAlignment::Center);
  count.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetAccessibilityView(
      count, Automation::Peers::AccessibilityView::Raw);
  pill.Child(count);
  return pill;
}

}  // namespace
}  // namespace urmsg::views
```

- [ ] **Step 4: add `MakeConversationRow` to that anonymous namespace** (immediately before its
      closing `}  // namespace`). This is the whole Spec C §4.1 row.

```cpp
urnw::kit::PaneTwoLineRowButton MakeConversationRow(urmsg::demo::Conversation const& c,
                                                    int index,
                                                    std::function<void(int)> const& onSelect) {
  const ConversationRowModel model = MakeConversationRowModel(c);

  auto row = urnw::kit::MakePaneTwoLineRowButton(winrt::hstring{model.name},
                                                 winrt::hstring{model.preview},
                                                 kConversationRowHeight);
  auto grid = row.root ? row.root.Content().try_as<Controls::Grid>() : nullptr;
  if (!grid) return row;  // no kit grid: the row still draws its two lines

  // The kit's three children, read BEFORE anything of ours is appended. At this
  // point grid.Children() is exactly {StackPanel text, TextBlock value, FontIcon
  // chevron} (UrComponents.cpp:446-464), so TYPE identifies each one and no
  // child-ORDER assumption is made.
  Controls::StackPanel text{nullptr};
  Controls::FontIcon chevron{nullptr};
  for (auto const& child : grid.Children()) {
    if (auto panel = child.try_as<Controls::StackPanel>()) text = panel;
    if (auto icon = child.try_as<Controls::FontIcon>()) chevron = icon;
  }

  // 1. The chevron and its column go. Spec C 4.1's row anatomy has no disclosure
  //    mark, and an empty Auto column would still cost the grid's 10dip
  //    ColumnSpacing on each side of it.
  if (chevron) {
    uint32_t at = 0;
    if (grid.Children().IndexOf(chevron, at)) grid.Children().RemoveAt(at);
  }
  if (2 < grid.ColumnDefinitions().Size()) grid.ColumnDefinitions().RemoveAt(2);

  // 2. Selection channel 2, built on EVERY row and transparent until
  //    SetConversationSelected paints it, so a selected row and an unselected one
  //    measure identically -- the reason kit::BuildPaneListRowParts builds its
  //    marker for both row forms (UrComponents.cpp:206-211). Margin -10 is the
  //    kit's own number and lands the bar at the kit's own x: both grids sit
  //    inside UrPaneRowButtonStyle's Padding="12,0" (App.xaml:897), so -10 draws
  //    it 2 dip in from the row's left edge.
  Controls::Border bar;
  bar.Tag(winrt::box_value(winrt::hstring{kTagSelectionBar}));
  bar.Width(2);
  bar.Margin(ThicknessHelper::FromLengths(-10, 0, 0, 0));
  bar.HorizontalAlignment(HorizontalAlignment::Left);
  bar.VerticalAlignment(VerticalAlignment::Stretch);
  bar.Background(urnw::colors::AccentBrush());
  bar.Opacity(0.0);
  Automation::AutomationProperties::SetAccessibilityView(
      bar, Automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(bar);

  // 3. The identicon shares column 0 with the text and is pinned left; the text
  //    is pushed 52 dip clear of it (40 identicon + the pane's 12dip gutter).
  //    Two children in one star cell rather than a fourth ColumnDefinition,
  //    because inserting a column at 0 would renumber every Grid::SetColumn the
  //    kit already set on children this file does not own. MakeIdenticon applies
  //    its own CornerRadius(8) (contract 3) -- do NOT set one here.
  auto identicon = urmsg::MakeIdenticon(c.identityKey, 40);
  identicon.HorizontalAlignment(HorizontalAlignment::Left);
  identicon.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetAccessibilityView(
      identicon, Automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(identicon);
  if (text) text.Margin(ThicknessHelper::FromLengths(52, 0, 0, 0));

  // 4. The trailing column: the time on the title's line, the state marks under
  //    it. row.value is the kit's own trailing TextBlock (UrValueTextStyle,
  //    muted, already AccessibilityView=Raw, UrComponents.cpp:448-474) and is
  //    MOVED here rather than duplicated: a UIElement has exactly one parent, so
  //    it must leave grid.Children() before this Append or the append throws.
  Controls::StackPanel trailing;
  trailing.Tag(winrt::box_value(winrt::hstring{kTagTrailing}));
  trailing.Spacing(4);
  trailing.HorizontalAlignment(HorizontalAlignment::Right);
  trailing.VerticalAlignment(VerticalAlignment::Center);
  Controls::Grid::SetColumn(trailing, 1);
  if (row.value) {
    uint32_t at = 0;
    if (grid.Children().IndexOf(row.value, at)) grid.Children().RemoveAt(at);
    row.value.Text(winrt::hstring{model.timeLabel});
    row.value.HorizontalAlignment(HorizontalAlignment::Right);
    trailing.Children().Append(row.value);
  }

  // The second trailing line. A COLLAPSED child costs a StackPanel no Spacing --
  // it is skipped entirely, which is exactly why kit::SetTextOrCollapse sets
  // Visibility instead of clearing Text (UrComponents.h:99-107) -- so a quiet row
  // shows a clean one-line trailing column.
  Controls::StackPanel cluster;
  cluster.Tag(winrt::box_value(winrt::hstring{kTagCluster}));
  cluster.Orientation(Controls::Orientation::Horizontal);
  cluster.Spacing(6);
  cluster.HorizontalAlignment(HorizontalAlignment::Right);

  // Advanced Mode only, and never on a DM (model.groupIdChip is empty there).
  // Built collapsed on every row; L6's SetConversationListAdvanced is the only
  // thing that ever shows it, which is what keeps that call a DENSITY change and
  // not a rebuild.
  Controls::TextBlock chip;
  chip.Tag(winrt::box_value(winrt::hstring{kTagGroupChip}));
  chip.Text(winrt::hstring{model.groupIdChip});
  chip.FontSize(10);
  chip.Foreground(urnw::colors::FaintBrush());
  chip.VerticalAlignment(VerticalAlignment::Center);
  chip.Visibility(Visibility::Collapsed);
  Automation::AutomationProperties::SetAccessibilityView(
      chip, Automation::Peers::AccessibilityView::Raw);
  cluster.Children().Append(chip);

  auto muted = MakeRowGlyph(L"\uE74F");  // Mute
  muted.Visibility(model.showMuted ? Visibility::Visible : Visibility::Collapsed);
  cluster.Children().Append(muted);

  auto timer = MakeRowGlyph(L"\uE916");  // Stopwatch
  timer.Visibility(model.showTimer ? Visibility::Visible : Visibility::Collapsed);
  cluster.Children().Append(timer);

  auto pill = MakeUnreadPill(model.unread);
  pill.Visibility(model.unread.empty() ? Visibility::Collapsed : Visibility::Visible);
  cluster.Children().Append(pill);

  trailing.Children().Append(cluster);
  grid.Children().Append(trailing);

  // The row's WHOLE announcement. The kit set Name to the title and
  // FullDescription to the note (UrComponents.cpp:475-476); this name already
  // carries both, plus the unread count, the muted state and the time, so the
  // description is cleared rather than left to repeat the preview after it.
  // title/note/value are already AccessibilityView=Raw, so nothing inside the
  // row is announced twice.
  Automation::AutomationProperties::SetName(
      row.root, winrt::hstring{ConversationRowAutomationName(c, false)});
  Automation::AutomationProperties::SetFullDescription(row.root, L"");

  // The index by value, full stop: nothing captured here refers into `world`.
  row.root.Click([index, onSelect](winrt::Windows::Foundation::IInspectable const&,
                                   RoutedEventArgs const&) {
    if (onSelect) onSelect(index);
  });
  return row;
}
```

- [ ] **Step 5: add `MakeConversationList` after the anonymous namespace closes**, inside
      `namespace urmsg::views`.

```cpp
ConversationListView MakeConversationList(urmsg::demo::World const& world,
                                          std::function<void(int)> onSelect) {
  ConversationListView view;

  Controls::StackPanel stack;
  // The 28px group rhythm the pane already uses, with NO count on it.
  // ListPaneCount in the pane header is the ONE "how many rows are on screen"
  // readout and L5's filter updates it; a second count that the filter did not
  // update would be a readout that had stopped being true. group_recent already
  // exists in the generated resw with the value "RECENT", so no key is added.
  auto group = urnw::kit::MakePaneGroupHeader(winrt::hstring{urnw::Localized("group_recent")});
  stack.Children().Append(group.root);

  view.rows.reserve(world.conversations.size());
  for (std::size_t i = 0; i < world.conversations.size(); ++i) {
    auto row = MakeConversationRow(world.conversations[i], static_cast<int>(i), onSelect);
    stack.Children().Append(row.root);
    view.rows.push_back(row);
  }

  view.root = stack;
  urnw::LogInfo("list: built {} conversation rows at {:.0f} dip", view.rows.size(),
                kConversationRowHeight);
  return view;
}
```

- [ ] **Step 6: register both files in `app/src/App/App.vcxproj`**, beside L1's entries:

```xml
    <ClCompile Include="Views\ConversationListView.cpp" />
    <ClCompile Include="Views\ConversationRowModel.cpp" />
```

```xml
    <ClInclude Include="Views\ConversationListView.h" />
    <ClInclude Include="Views\ConversationRowModel.h" />
```

- [ ] **Step 7: add the member and the callback to `app/src/App/MainWindow.xaml.h`.** After
      `#include "UrComponents.h"` (line 21) add:

```cpp
#include "Views/ConversationListView.h"
```

  and inside the `private:` block, after `void BuildConversationList();` (line 48):

```cpp
  // A row was clicked. Takes the row INDEX: ConversationListView::rows[i] is
  // world.conversations[i], and nothing reorders either.
  void OnConversationSelected(int index);
```

  and after `urnw::kit::PaneSearchRow search_{};` (line 60):

```cpp
  // Empty on a non-demo launch: BuildConversationList only fills it under --demo.
  urmsg::views::ConversationListView list_{};
```

- [ ] **Step 8: branch `MainWindow::BuildConversationList()` in
      `app/src/App/MainWindow.xaml.cpp`.** Add to the include block (after `#include "Log.h"`):

```cpp
#include "Demo/DemoSwitches.h"
#include "Demo/DemoWorld.h"
```

  Then insert, immediately after the `list.Clear();` line at `MainWindow.xaml.cpp:143`:

```cpp
  // --demo replaces the placeholder rows with the demo world. Without it, every
  // line below is byte-for-byte what it was: design doc 8, "without --demo the
  // app behaves exactly as it does today", which step 12 verifies in pixels.
  if (urmsg::demo::ParseDemoOptions().enabled) {
    auto const& world = urmsg::demo::GetWorld();
    list_ = urmsg::views::MakeConversationList(world, [weak = get_weak()](int index) {
      if (auto self = weak.get()) self->OnConversationSelected(index);
    });
    list.Append(list_.root);
    ListPaneCount().Text(winrt::to_hstring(static_cast<int>(world.conversations.size())));
    urnw::LogInfo("window: demo conversation list built with {} rows",
                  world.conversations.size());
    return;
  }
```

  and add, after `MainWindow::BuildConversationList`'s closing brace:

```cpp
void MainWindow::OnConversationSelected(int index) {
  // Selection itself lands in L3. This records that a row's click reached the
  // window, which is the half this task owns.
  urnw::LogInfo("window: conversation row {} clicked", index);
}
```

- [ ] **Step 9: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

  Expected: `OK in <n>s -> ...\app\build\x64\Release\`.

- [ ] **Step 10: capture the demo list.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

  Expected: `dips        : 1560x900` (±1 dip at a non-100% display scale, because
  `WindowShell.cpp:268` sizes as `int(dips * scale)`), `foreground  : YES`, then the three
  screenshot paths and `app still running as pid <n>`. Ignore the `pixel probes` table for the
  reason given in this task's preamble.

- [ ] **Step 11: LOOK at `.verify\urmessage-window.png` and check all nine.** Read the image;
      do not infer it from the build succeeding.

  1. **Exactly 8 conversation rows**, under one `RECENT` strip, between the search field and the
     bottom of the list column. Count the hairlines: 8.
  2. Each row is **64 dip tall** — measure the first row's top hairline to its bottom hairline
     and divide by the display scale the script printed (`window dpi / 96`).
  3. Each row opens with a **40×40 identicon with visibly rounded corners**, and the name starts
     **52 dip** right of the identicon's left edge. If two rows show the same identicon pattern,
     that is an F2 finding (`DemoWorld`'s `identityKey` values, or `MakeIdenticon`'s hash), not a
     change here.
  4. The **list column is 320 dip wide**, with the 1px `PaneRule` and the `THREAD` pane to its
     right. The thread body still reads `Select a conversation to read it.` — an acknowledged
     interim state that L3 closes.
  5. **Exactly one row shows a stopwatch glyph and NO preview text**; every other row shows a
     one-line preview. On any row whose preview overruns the column, it ends in an **ellipsis**,
     never a wrap and never a hard clip.
  6. **Exactly two rows carry a pale-yellow pill** at the row's right edge, on the second line,
     with dark text in it.
  7. **Exactly one row carries the mute glyph.**
  8. **No row carries a chevron.**
  9. Outside the 40×40 identicon, **no row carries a red, green or blue mark of any kind** — no
     delivery check, no coloured dot, no state glyph. The pill is `#EFF7BB` and nothing else on
     the row is chromatic (Spec C §4.1: a list row never shows a delivery-state colour). The
     identicon is excluded by construction, not by tolerance: contract §3 states its palette
     never emits `kDanger`, `kUrGreen`, `kToggleAccent`, `kStatusConnecting` or `kProGold`.

  Both glyphs must be **drawn shapes, not hollow rectangles** — a hollow box is Segoe Fluent's
  `.notdef` and means the codepoint is wrong. If `\uE74F` (Mute) boxes, use `\uE7ED`
  (QuietHours); if `\uE916` (Stopwatch) boxes, use `\uE823` (Timer). Change the literal, keep the
  trailing name comment, rebuild, re-capture.

- [ ] **Step 12: prove the plain launch is unchanged, and that the L1 diagnostics ride along.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'demo.list.|conversation list built|breakpoint'"
```

  Expected: `dips        : 480x760`; the five `pixel probes` values **identical** to the ones
  recorded in step 1; `.verify\urmessage-window.png` showing `Sample conversation 1..10` exactly
  as `.verify\baseline-plain.png` does. The log must carry
  `window: conversation list built with 10 placeholder rows`,
  `window: breakpoint -> narrow (480 dip)`, and the seven `demo.list.* : PASS` lines — the
  launch-independence decision recorded in L1, checked here rather than left in a comment.

- [ ] **Step 13: commit.**

```
git add app/src/App/Views/ConversationListView.h app/src/App/Views/ConversationListView.cpp app/src/App/App.vcxproj app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: the Spec C 4.1 conversation row, on kit::MakePaneTwoLineRowButton"
git show --stat --oneline HEAD
```

  Expected: exactly those five paths.

**Deliverable:** `verify-render.ps1 -AppArgs "--demo=chats"` produces a 1560×900 two-pane window
whose list column holds 8 identicon rows, one stopwatch row with no preview, two pills, one mute
glyph and no chevrons; and a plain launch is pixel-identical to step 1's baseline.

---

## Task L3: Selection on three channels, and the row that opens a thread

**Files:**

Modify app/src/App/Views/ConversationListView.h; Modify app/src/App/Views/ConversationListView.cpp; Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: L2: urmsg::views::ConversationListView, MakeConversationList, the kTagSelectionBar-tagged Border on every row, MainWindow::list_ and MainWindow::OnConversationSelected(int). L1: ConversationRowAutomationName. Kit precedent: urnw::kit::SetPaneListRowSelected (app/src/App/UrComponents.h:333-340, UrComponents.cpp:302-310); urnw::kit::MakePaneEmptyLine (UrComponents.h:423). Colours: urnw::colors::CardBrush, MakeBrush (app/src/App/UrColors.h:107,87). x:Names ThreadBody, ThreadPaneTitle (MainWindow.xaml:176,179).
- Produces: void urmsg::views::SetConversationSelected(ConversationListView& v, int index); MainWindow::OnConversationSelected(int) fully implemented; MainWindow member int selectedConversation_ = -1

### L3. Selection on three channels, and the row that opens a thread

**Three channels, and why one of them is deliberately weak.** `kit::SetPaneListRowSelected`
carries selection on a fill step, a 2px accent bar and the automation name, and
`UrComponents.h:333-339` states the reason: `#1C1C1C` over `#101010` is a two-step lift that is
invisible on a dim panel and to anyone with low-contrast vision. This surface copies that shape
exactly, including its known weakness: `UrPaneRowButtonStyle`'s **PointerOver** state sets
`Root.Background = UrCardBrush` (`App.xaml:931-935`), the same `#1C1C1C`, so a merely **hovered**
row is pixel-identical to the selected row on channel 1. That is not a defect to fix here — it is
precisely why channels 2 and 3 exist. Every check below therefore discriminates on the **bar**,
not on the fill.

**Selection at launch, and why.** The agent may not synthesise input, so a selection that only
ever happens on a click is a state no capture can reach. Under `--demo` MainWindow selects
conversation 0 after building the list. Design §6.1 does not forbid it, D3's rationale asks for
exactly this ("a rail populated at launch is screenshottable where a modal is not"), and
contract §2 already requires `--demo=inspect` to pre-select conversation 0.

**The pane title stays `THREAD`.** `ThreadPaneTitle` carries `UrPaneTitleStyle` — FontSize 12,
SemiBold, `CharacterSpacing="60"` (`App.xaml:838-847`) — the small letterspaced **chrome** voice,
whose only two values in this app are `CONVERSATIONS` and `THREAD`. A mixed-case personal name
set in it reads wrong. The conversation's name goes in the thread **body** instead, as one muted
centred line, until the ThreadView surface replaces that body wholesale.

- [ ] **Step 1: declare the setter in `app/src/App/Views/ConversationListView.h`**, after
      `MakeConversationList`:

```cpp
// Paint row `index` as the selected one and every other row as not. Pass -1 for
// "nothing is selected". Three channels, because one is not enough: a fill step,
// a 2px leading accent bar (a SHAPE change, so colour is never the only carrier)
// and the row's automation Name -- the same three kit::SetPaneListRowSelected
// carries, for the same reason (UrComponents.h:333-339).
void SetConversationSelected(ConversationListView& v, int index);
```

- [ ] **Step 2: add the tagged-child lookup to the anonymous namespace of
      `app/src/App/Views/ConversationListView.cpp`**, after `MakeUnreadPill`:

```cpp
// The first DIRECT child of `panel` carrying `tag`. Shallow by design: every
// element this file tags is a direct child of the panel it is looked up from, so
// a recursive walk would only hide a mistake. Null-safe at every hop -- try_as on
// an empty handle dereferences null.
FrameworkElement TaggedChild(Controls::Panel const& panel, wchar_t const* tag) {
  if (!panel) return nullptr;
  for (auto const& child : panel.Children()) {
    auto element = child.try_as<FrameworkElement>();
    if (!element) continue;
    auto value = element.Tag().try_as<winrt::hstring>();
    if (value && *value == tag) return element;
  }
  return nullptr;
}

// The kit row's content Grid. MakePaneTwoLineRowButton ends with
// out.root.Content(grid) (UrComponents.cpp:478), so this is the documented
// shape rather than a guess.
Controls::Grid RowGrid(urnw::kit::PaneTwoLineRowButton const& row) {
  if (!row.root) return nullptr;
  return row.root.Content().try_as<Controls::Grid>();
}
```

- [ ] **Step 3: add `SetConversationSelected` to `ConversationListView.cpp`**, after
      `MakeConversationList`:

```cpp
void SetConversationSelected(ConversationListView& v, int index) {
  auto const& world = urmsg::demo::GetWorld();
  for (std::size_t i = 0; i < v.rows.size(); ++i) {
    auto const& row = v.rows[i];
    if (!row.root) continue;
    const bool selected = (static_cast<int>(i) == index);

    // Channel 1: the fill step. Deliberately the same step the kit uses, and
    // deliberately the same value UrPaneRowButtonStyle paints on PointerOver
    // (App.xaml:931-935) -- a hovered row is indistinguishable from a selected
    // one on this channel alone, which is why the next two exist.
    row.root.Background(selected ? urnw::colors::CardBrush()
                                 : urnw::colors::MakeBrush({0, 0, 0, 0}));

    // Channel 2: the 2px leading accent bar. A SHAPE change, not colour alone.
    if (auto bar = TaggedChild(RowGrid(row), kTagSelectionBar))
      bar.Opacity(selected ? 1.0 : 0.0);

    // Channel 3: the announcement. A fill step and a bar say nothing to a screen
    // reader, so the row's own Name carries the state.
    if (i < world.conversations.size())
      Automation::AutomationProperties::SetName(
          row.root,
          winrt::hstring{ConversationRowAutomationName(world.conversations[i], selected)});
  }
  urnw::LogInfo("list: selection -> row {} of {}", index, v.rows.size());
}
```

- [ ] **Step 4: add the selection state to `app/src/App/MainWindow.xaml.h`**, after `list_`:

```cpp
  // -1 until something is selected. Held on the window because the window is
  // what will also drive the thread and the rail.
  int selectedConversation_ = -1;
```

- [ ] **Step 5: implement `MainWindow::OnConversationSelected` in
      `app/src/App/MainWindow.xaml.cpp`.** Replace the L2 stub body with:

```cpp
void MainWindow::OnConversationSelected(int index) {
  auto const& world = urmsg::demo::GetWorld();
  if (index < 0 || static_cast<std::size_t>(index) <= 0u == false) {
  }
  if (index < 0 || world.conversations.size() <= static_cast<std::size_t>(index)) return;
  selectedConversation_ = index;
  urmsg::views::SetConversationSelected(list_, index);

  // The pane TITLE stays "THREAD": UrPaneTitleStyle is the letterspaced chrome
  // voice (App.xaml:838-847) and a mixed-case name set in it reads wrong. The
  // conversation's name goes in the BODY as one muted centred line -- an
  // acknowledged interim state, replaced wholesale when the ThreadView surface
  // lands. A centred muted line is an empty state, not an affordance, so it does
  // not read as something that looks live and does nothing (design 9.1).
  ThreadBody().Children().Clear();
  ThreadBody().Children().Append(
      urnw::kit::MakePaneEmptyLine(winrt::hstring{world.conversations[index].name}));
  urnw::LogInfo("window: conversation {} selected ({})", index,
                winrt::to_string(winrt::hstring{world.conversations[index].name}));
}
```

  Delete the two dead lines the snippet above opens with if you pasted them — the body is the
  bounds check, the three state writes and the log line, nothing else:

```cpp
void MainWindow::OnConversationSelected(int index) {
  auto const& world = urmsg::demo::GetWorld();
  if (index < 0 || world.conversations.size() <= static_cast<std::size_t>(index)) return;
  selectedConversation_ = index;
  urmsg::views::SetConversationSelected(list_, index);
  ThreadBody().Children().Clear();
  ThreadBody().Children().Append(
      urnw::kit::MakePaneEmptyLine(winrt::hstring{world.conversations[index].name}));
  urnw::LogInfo("window: conversation {} selected ({})", index,
                winrt::to_string(winrt::hstring{world.conversations[index].name}));
}
```

- [ ] **Step 6: select conversation 0 at launch under `--demo`.** In
      `MainWindow::BuildConversationList()`, inside the demo branch added by L2, replace
      `return;` with:

```cpp
    // The agent may not synthesise input, so a selection that only ever happens
    // on a click is a state no capture can reach. Contract 2 already requires
    // --demo=inspect to pre-select conversation 0; --demo=chats does the same so
    // the three selection channels are visible at launch.
    OnConversationSelected(0);
    return;
```

- [ ] **Step 7: build and capture.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

  Expected: `OK in <n>s`, then `dips        : 1560x900` and the screenshot paths.

- [ ] **Step 8: measure the accent bar, not the fill.** Run this in the **PowerShell** tool, not
      Bash — Bash would expand the `$` variables:

```
powershell -ExecutionPolicy Bypass -Command "Add-Type -AssemblyName System.Drawing; $b=[System.Drawing.Bitmap]::FromFile((Resolve-Path '.verify\urmessage-window.png').Path); $xa=[int]($b.Width*0.13); $xb=[int]($b.Width*0.36); $n=0;$x0=$b.Width;$x1=0;$y0=$b.Height;$y1=0; for($y=0;$y -lt $b.Height;$y++){ for($x=$xa;$x -lt $xb;$x++){ $c=$b.GetPixel($x,$y); if($c.R -eq 239 -and $c.G -eq 247 -and $c.B -eq 187){ $n++; if($x -lt $x0){$x0=$x}; if($x -gt $x1){$x1=$x}; if($y -lt $y0){$y0=$y}; if($y -gt $y1){$y1=$y} } } }; 'accent px {0}  x {1}..{2}  y {3}..{4}' -f $n,$x0,$x1,$y0,$y1; $b.Dispose()"
```

  It scans only the list column (13%–36% of the window width) for `#EFF7BB`, and prints the
  count and bounding box in physical pixels. Expected shape of the answer:

  - **`accent px` grows by roughly 128 × scale² over the same command run against L2's capture**
    — a 2 dip × 64 dip bar. (The pills alone came to a few hundred px in L2; the absolute number
    depends on antialiasing and digit widths, so compare the two runs, do not match a constant.)
  - **the low `x` bound drops by about 315 × scale pixels.** In L2 the only accent pixels were
    the two pills at the list's *right* edge; now the leftmost accent pixel is the bar at the
    list's *left* edge, and the list column is 320 dip wide. This is the falsifiable half: if
    the low `x` bound did not move, the bar is not being painted.
  - **the `y` range now spans one 64-dip row contiguously**, at the top of the list.

- [ ] **Step 9: LOOK at `.verify\urmessage-window.png` and check four.**

  1. A **2px pale-yellow vertical bar** appears on **row 1 and no other row**, vertically CENTRED
     rather than full-height -- about 40 dip of the 64 dip row. That is house style, not a
     defect: `UrPaneRowButtonStyle` sets `VerticalContentAlignment="Center"`, so the content
     Grid measures to its natural height and a Stretch bar inside it reaches only that far.
     The kit marker `SetPaneListRowSelected` paints is built by the identical mechanism, and
     the NavigationView's own selection indicator is a short centred bar too. Do not change
     the row geometry to make the bar taller -- that would diverge from the kit. Sitting
     **2 dip in from the row's left edge** — the same x `kit::SetPaneListRowSelected` draws it at,
     because both grids sit inside `UrPaneRowButtonStyle`'s 12-dip padding and both bars carry the
     same −10 offset.
  2. Row 1 shows a fill **one step above the page**. Another row may show the same fill if the
     mouse happens to rest over it — `verify-render.ps1` makes the window topmost without moving
     the cursor (`:183`), and PointerOver is the same `#1C1C1C`. That is expected; the bar is the
     discriminator.
  3. The thread pane header still reads **`THREAD`** in the letterspaced chrome voice, and its
     body now reads **the selected conversation's name**, centred and muted, instead of
     `Select a conversation to read it.`
  4. The row's own text, identicon, pill and glyphs are **unchanged** from L2's capture — selection
     added a bar and a fill, and moved nothing.

- [ ] **Step 10: check the announcement in the log.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'list: selection|conversation 0 selected'"
```

  Expected two lines: `list: selection -> row 0 of 8` and `window: conversation 0 selected (<the
  first conversation's name>)`. Channel 3 itself is asserted by L1's `demo.list.select`, which
  already proves the selected and unselected names differ and that only the selected one says
  `Selected`.

- [ ] **Step 11: commit.**

```
git add app/src/App/Views/ConversationListView.h app/src/App/Views/ConversationListView.cpp app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: conversation selection on three channels — fill, 2px accent bar, automation name"
git show --stat --oneline HEAD
```

  Expected: exactly those four paths.

**Deliverable:** `--demo=chats` opens with row 1 selected, a 2px `#EFF7BB` bar 2 dip in from its
left edge, and the thread pane naming that conversation. The accent-pixel probe's low `x` bound
sits at the list's left edge rather than its right.

---

## Task L4: The staggered row entrance, capped and reduce-motion gated

**Files:**

Modify app/src/App/Views/ConversationListView.h; Modify app/src/App/Views/ConversationListView.cpp; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: L2: urmsg::views::ConversationListView, MakeConversationList, MainWindow::list_. L1: ConversationRowDelayMs (already asserted by demo.list.stagger). Motion: urnw::motion::ShouldAnimate, MakeSplineDouble, kBaseMs, kStandardP1, kStandardP2 (app/src/App/UrMotion.h:84,100,32,53-54); the local-Storyboard precedent at UrMotion.cpp:90-115. MainWindow::StartReveal (MainWindow.xaml.cpp:105); urnw::WindowReveal::Arm/Start (WindowReveal.h:44-60).
- Produces: void urmsg::views::AnimateConversationListEntrance(ConversationListView& v); MakeConversationList additionally writes the entrance start pose (row.root.Opacity(0)) when motion::ShouldAnimate()

### L4. The staggered row entrance, capped and reduce-motion gated

**Arm in the constructor, start after `Activate()` — the codebase's own rule, not a new one.**
`WindowReveal::Arm` writes its start pose synchronously **before** `Window.Activate()` "so the
first composed frame is already the start pose rather than the settled one corrected a frame
later" (`WindowReveal.h:50-54`), and `Start()` runs after (`MainWindow.xaml.cpp:105`, called from
`App::OnLaunched`). The naive reading — "MainWindow builds its tree in its constructor, so an
entrance begun there finishes before the first frame" — is **wrong**: `Activate()` is called
microseconds after the constructor returns and the first frame composes ~16 ms later, while a
490 ms cascade is nowhere near done. So `MakeConversationList` writes `Opacity(0)` (it runs from
the constructor) and `AnimateConversationListEntrance` runs from `StartReveal()`.

**Motion off means motion gone.** `WindowReveal::Arm` computes
`armed_ = enabled && urnw::motion::ShouldAnimate()` (`WindowReveal.cpp:74`), so with animations
turned off in Windows it never touches opacity at all. This entrance must behave the same way: if
`ShouldAnimate()` is false, **no row is ever written to 0**, so there is nothing that can be left
stuck there.

**One property, two elements, no conflict.** `reveal_.Bind` already ramps `ConversationList()`
itself (`MainWindow.xaml.cpp:87`, `WindowReveal.cpp:124`). That is the **parent**; this animates
its grandchildren. Two opacities on two elements multiply; they do not fight. `verify-render`
settles 1200 ms by default — longer than the reveal's ~500 ms plus this cascade's 240 + 250 —
so the default capture is the settled frame.

- [ ] **Step 1: declare the entrance in `app/src/App/Views/ConversationListView.h`**, after
      `SetConversationSelected`:

```cpp
// Start the staggered row entrance. Call AFTER Window.Activate(), from
// MainWindow::StartReveal -- MakeConversationList already wrote the start pose,
// because it runs from the MainWindow constructor and WindowReveal.h:50-54 fixes
// that split for every reveal in this app.
//
// A no-op when motion::ShouldAnimate() is false, in which case the rows were
// never dimmed and there is nothing to settle.
void AnimateConversationListEntrance(ConversationListView& v);
```

- [ ] **Step 2: write the start pose in `MakeConversationList`.** In
      `app/src/App/Views/ConversationListView.cpp`, insert immediately **before**
      `view.root = stack;`:

```cpp
  // The START pose, written here because MakeConversationList runs from the
  // MainWindow CONSTRUCTOR, before Activate(). WindowReveal.h:50-54 is the rule:
  // write the start pose synchronously ahead of the first composed frame and
  // START after Activate -- which AnimateConversationListEntrance does from
  // MainWindow::StartReveal. Gated, so that with animations off in Windows no row
  // is ever written to 0 and none can be left there.
  if (urnw::motion::ShouldAnimate())
    for (auto const& row : view.rows)
      if (row.root) row.root.Opacity(0.0);
```

  and add to the include block, after `#include "UrColors.h"`:

```cpp
#include "UrMotion.h"
```

- [ ] **Step 3: add the entrance itself to `ConversationListView.cpp`**, after
      `SetConversationSelected`:

```cpp
void AnimateConversationListEntrance(ConversationListView& v) {
  namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
  if (!urnw::motion::ShouldAnimate()) {
    urnw::LogInfo("list: entrance skipped ({} rows, motion off)", v.rows.size());
    return;
  }
  // A LOCAL Storyboard, begun and left: the shape every hand-built animation in
  // this app already uses (UrMotion.cpp:90-115), because a running Storyboard is
  // held by the timing manager.
  anim::Storyboard board;
  for (std::size_t i = 0; i < v.rows.size(); ++i) {
    auto const& row = v.rows[i];
    if (!row.root) continue;
    // No new duration and no new curve: kBaseMs on the standard spline
    // (design 7). The stagger is ConversationRowDelayMs, whose cap L1's
    // demo.list.stagger asserts exhaustively at index 0/5/6/50.
    auto fade = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kBaseMs,
                                               ConversationRowDelayMs(i),
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(fade, row.root);
    anim::Storyboard::SetTargetProperty(fade, L"Opacity");
    board.Children().Append(fade);
  }
  board.Begin();
  urnw::LogInfo("list: entrance armed ({} rows, last begins at {} ms)", v.rows.size(),
                ConversationRowDelayMs(v.rows.empty() ? 0 : v.rows.size() - 1));
}
```

- [ ] **Step 4: start it from `MainWindow::StartReveal`** in `app/src/App/MainWindow.xaml.cpp`.
      Replace line 105:

```cpp
void MainWindow::StartReveal() { reveal_.Start(); }
```

  with:

```cpp
void MainWindow::StartReveal() {
  reveal_.Start();
  // After Activate(), beside the window reveal rather than inside it: the reveal
  // ramps ConversationList itself (the parent), this ramps its rows. Two
  // opacities on two elements multiply; they do not fight. A no-op on a
  // non-demo launch, where list_.rows is empty.
  urmsg::views::AnimateConversationListEntrance(list_);
}
```

- [ ] **Step 5: build and capture the SETTLED frame.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

  Expected: `dips        : 1560x900`. In `.verify\urmessage-window.png`, **all 8 rows are at full
  opacity** — the same image L3 produced. A row still dim at 1200 ms is a row stuck at its start
  pose: check that `board.Begin()` ran (the `list: entrance armed` log line) and that the reveal
  ring on `ConversationList` is not still at 0, i.e. that `StartReveal` was reached at all.

- [ ] **Step 6: capture the entrance MID-FLIGHT and see the stagger.** `-SettleMs` is an existing
      parameter of the script (`verify-render.ps1:34`); no new one is needed.

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats" -SettleMs 300
```

  Expected in `.verify\urmessage-window.png`: **rows 1–2 are clearly brighter than rows 7–8**, and
  the brightness decreases monotonically down the list. At 300 ms row 0's fade (begin 0, 250 ms)
  is complete while rows 6–7 (begin 240 ms) are ~20% of the way through theirs. The reveal ring
  fades `ConversationList` uniformly, so any *difference between rows* comes only from this
  stagger.

- [ ] **Step 7: check the branch the log records.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'list: entrance'"
```

  Expected: `list: entrance armed (8 rows, last begins at 240 ms)`.

  **The reduce-motion branch is not verified by this plan.** It needs "Show animations in
  Windows" turned off, which is a machine-global setting this plan will not flip under the owner
  while other work is running. The branch is instead made *self-reporting*: with animations off
  the same log line reads `list: entrance skipped (8 rows, motion off)` and the capture is
  identical to step 5's, because no row was ever dimmed. The owner's one-time manual check is
  Settings → Accessibility → Visual effects → Animation effects **off**, relaunch, read that log
  line, and confirm the list is instantly complete rather than blank.

- [ ] **Step 8: commit.**

```
git add app/src/App/Views/ConversationListView.h app/src/App/Views/ConversationListView.cpp app/src/App/MainWindow.xaml.cpp
git commit -m "demo: the staggered conversation-list entrance, armed in the constructor and started after Activate"
git show --stat --oneline HEAD
```

  Expected: exactly those three paths.

**Deliverable:** at `-SettleMs 300` the list is visibly mid-cascade with the top rows brighter
than the bottom; at the default 1200 ms every row is at full opacity; and the log names which
branch ran.

---

## Task L5: The search row filters the list

**Files:**

Modify app/src/App/Views/ConversationListView.h; Modify app/src/App/Views/ConversationListView.cpp; Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: L1: ConversationRowMatches (already asserted by demo.list.search). L2: ConversationListView, MakeConversationList, MainWindow::list_. Kit: urnw::kit::PaneSearchRow and MainWindow::search_ (UrComponents.h:428-432, MainWindow.xaml.cpp:138); x:Name ListPaneCount (MainWindow.xaml:147).
- Produces: std::size_t urmsg::views::ApplyConversationListFilter(ConversationListView& v, urmsg::demo::World const& world, std::wstring const& query); MainWindow::ApplyConversationFilter()

### L5. The search row filters the list

**Why this task exists at all.** `MainWindow::BuildConversationList` already puts a real,
focusable, editable `kit::MakePaneSearchRow` at the top of the pane (`MainWindow.xaml.cpp:138`).
Design §9.1: "Anything on screen that cannot be clicked must be visibly inert — no affordance
that looks live and does nothing. A dead-looking button is a demo bug." A search box that accepts
text and changes nothing is the worst version of that, because it looks *more* live than a dead
button.

**The decision rule is already tested.** `ConversationRowMatches` is L1's, and L1's
`demo.list.search` already asserts that an empty query matches everything, that every row is
findable by name, and that the disappearing conversation's hidden preview cannot be searched
back onto the screen. This task is the render, not the rule.

**Indices survive filtering.** The filter only changes `Visibility`; `rows[i]` is still
`world.conversations[i]`, so `SetConversationSelected(list_, selectedConversation_)` remains
correct while rows are hidden — which is why selection is index-addressed.

- [ ] **Step 1: declare the filter in `app/src/App/Views/ConversationListView.h`**, after
      `AnimateConversationListEntrance`:

```cpp
// Hide every row whose conversation does not match `query`, show the rest, and
// return how many are visible. Visibility only: a filter that rebuilt the list
// would throw away selection and every row's entrance opacity, and `rows[i]`
// would stop being `world.conversations[i]`.
//
// Returns std::size_t, and the caller uses it: the pane header's count has to
// say how many rows are on screen, not how many exist.
std::size_t ApplyConversationListFilter(ConversationListView& v,
                                        urmsg::demo::World const& world,
                                        std::wstring const& query);
```

- [ ] **Step 2: implement it in `app/src/App/Views/ConversationListView.cpp`**, after
      `AnimateConversationListEntrance`:

```cpp
std::size_t ApplyConversationListFilter(ConversationListView& v,
                                        urmsg::demo::World const& world,
                                        std::wstring const& query) {
  std::size_t visible = 0;
  for (std::size_t i = 0; i < v.rows.size(); ++i) {
    auto const& row = v.rows[i];
    if (!row.root) continue;
    // A row with no conversation behind it (a world that shrank under a built
    // list) is hidden rather than left showing stale text.
    const bool keep = (i < world.conversations.size()) &&
                      ConversationRowMatches(world.conversations[i], query);
    row.root.Visibility(keep ? Visibility::Visible : Visibility::Collapsed);
    if (keep) ++visible;
  }
  urnw::LogInfo("list: filter \"{}\" -> {} of {}", winrt::to_string(winrt::hstring{query}),
                visible, v.rows.size());
  return visible;
}
```

- [ ] **Step 3: declare the window-side handler in `app/src/App/MainWindow.xaml.h`**, after
      `void OnConversationSelected(int index);`:

```cpp
  // Reads search_.box and applies it to list_. One place, so the box's text and
  // the pane header's count cannot disagree.
  void ApplyConversationFilter();
```

- [ ] **Step 4: implement it in `app/src/App/MainWindow.xaml.cpp`**, after
      `MainWindow::OnConversationSelected`:

```cpp
void MainWindow::ApplyConversationFilter() {
  if (!search_.box || !list_.root) return;
  auto const& world = urmsg::demo::GetWorld();
  const std::wstring query{search_.box.Text()};
  const std::size_t visible =
      urmsg::views::ApplyConversationListFilter(list_, world, query);
  // The ONE count of "how many rows are on screen". The RECENT group header
  // deliberately carries no count (MakeConversationList): a second readout this
  // function did not update would be a readout that had stopped being true.
  ListPaneCount().Text(winrt::to_hstring(static_cast<int>(visible)));
}
```

- [ ] **Step 5: subscribe to the box, under `--demo` only.** In
      `MainWindow::BuildConversationList()`, inside the demo branch, insert immediately before
      `OnConversationSelected(0);`:

```cpp
    // TextChanged, not KeyDown: it fires for paste, for undo and for a
    // programmatic Text() write, and the filter must be true of the box's
    // CONTENT rather than of the last key that touched it.
    search_.box.TextChanged([weak = get_weak()](winrt::Windows::Foundation::IInspectable const&,
                                                TextChangedEventArgs const&) {
      if (auto self = weak.get()) self->ApplyConversationFilter();
    });
```

- [ ] **Step 6: build and confirm the unfiltered state is unchanged.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

  Expected: `dips        : 1560x900`; `.verify\urmessage-window.png` shows **all 8 rows**, row 1
  still selected with its accent bar, and the pane header's count still reading **8**. An empty
  query must change nothing — L1's `demo.list.search` asserts `ConversationRowMatches(c, L"")` is
  true for every row, and this is that assertion in pixels.

- [ ] **Step 7: check the handler is wired, without typing into it.**

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'list: filter|demo conversation list built'"
```

  Expected: `window: demo conversation list built with 8 rows`, and **no** `list: filter` line —
  `TextChanged` has not fired because nothing has typed. A `list: filter` line here would mean
  something is writing the box's text at launch, which nothing in this plan does.

  **The filtered render is not verified by this plan.** Reaching it needs a keystroke, and this
  work may not synthesise input; no `--demo` switch carries a query (contract §2 fixes
  `DemoOptions`, and inventing one to make a check pass would be the wrong end of the trade).
  Two things stand in for it: L1's `demo.list.search` proves the decision rule over all 8
  conversations, and `ApplyConversationListFilter` logs every call, so the owner's own check —
  open the app, type `de`, watch rows disappear and the header count fall — leaves
  `list: filter "de" -> <n> of 8` in `<root>\logs\urmessage-app.log` as evidence.

- [ ] **Step 8: commit.**

```
git add app/src/App/Views/ConversationListView.h app/src/App/Views/ConversationListView.cpp app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: the search row filters the conversation list, and the pane count follows it"
git show --stat --oneline HEAD
```

  Expected: exactly those four paths — including `MainWindow.xaml.h`, which step 3 edits.

**Deliverable:** typing in the pane's search box hides non-matching rows and moves the header's
count, selection and entrance state survive it, and every call leaves one
`list: filter "<q>" -> <n> of 8` line in the log.

---

## Task L6: Advanced Mode density: the group-id chip

**Files:**

Modify app/src/App/Views/ConversationListView.h; Modify app/src/App/Views/ConversationListView.cpp; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: L2: ConversationListView, the kTagTrailing / kTagCluster / kTagGroupChip elements built on every row, TaggedChild and RowGrid (added in L3 step 2), MainWindow::list_. L1: ConversationRowModel::groupIdChip (already asserted by demo.list.group). F2: urmsg::AdvancedModeEnabled() (contract §5); urnw::LoadAppPrefs / SaveAppPref (app/src/Common/AppPrefs.h:23-27) and the prefs file at <root>/app_prefs.json (Paths.cpp:54).
- Produces: void urmsg::views::SetConversationListAdvanced(ConversationListView& v, bool advanced); MainWindow::BuildConversationList applies AdvancedModeEnabled() to the freshly built list

### L6. Advanced Mode density: the group-id chip

**Contract §4's last unowned function.** `SetConversationListAdvanced` is fixed by the contract
and design §6.6's table says what it does to this surface: *Conversation list — Normal: name,
preview, time; Advanced: + group id chip.*

**DENSITY ONLY.** Contract §4: "Every `Set*Advanced` changes DENSITY only. It must never run a
mode crossfade." That is why L2 built the chip **on every row, collapsed**, and tagged it: this
function sets one `Visibility` per row and touches nothing else. Re-running the builder to change
density is the bug the contract names — it blanks a visible surface to opacity 0 and fades it
back over itself.

**No view reads the preference.** Contract §5 gives Advanced Mode one owner. This task calls
`urmsg::AdvancedModeEnabled()` **once**, from `MainWindow`, when the list is built. The live
subscription (`OnAdvancedModeChanged`, which re-calls every view's `Set*Advanced` when the toggle
moves) is registered by the wiring surface, per contract §5; this surface supplies the setter it
will call and applies the initial value. Nothing in `ConversationListView.cpp` reads a pref.

- [ ] **Step 1: declare the setter in `app/src/App/Views/ConversationListView.h`**, after
      `ApplyConversationListFilter`:

```cpp
// Show or hide the Advanced-Mode group-id chip on every group row (design 6.6).
// DENSITY ONLY -- one Visibility per row, no rebuild and no crossfade
// (contract 4). The chip exists on every row already, collapsed and empty on a
// DM, so this call cannot change the list's measured height or its row count.
void SetConversationListAdvanced(ConversationListView& v, bool advanced);
```

- [ ] **Step 2: add the `Panel` overload of the tag lookup** to the anonymous namespace of
      `app/src/App/Views/ConversationListView.cpp`, immediately after `TaggedChild` (added in L3
      step 2):

```cpp
// The same lookup, narrowed to a Panel, so a chain of them reads as one
// expression without a null check between every hop.
Controls::Panel TaggedPanel(Controls::Panel const& parent, wchar_t const* tag) {
  auto child = TaggedChild(parent, tag);
  if (!child) return nullptr;
  return child.try_as<Controls::Panel>();
}
```

- [ ] **Step 3: implement the setter in `ConversationListView.cpp`**, after
      `ApplyConversationListFilter`:

```cpp
void SetConversationListAdvanced(ConversationListView& v, bool advanced) {
  int shown = 0;
  int chips = 0;
  for (auto const& row : v.rows) {
    auto cluster = TaggedPanel(TaggedPanel(RowGrid(row), kTagTrailing), kTagCluster);
    auto chip = TaggedChild(cluster, kTagGroupChip);
    if (!chip) continue;
    auto text = chip.try_as<Controls::TextBlock>();
    if (!text) continue;
    ++chips;
    // Empty on every DM (ConversationRowModel::groupIdChip), so Advanced Mode
    // never puts an empty chip and its 6dip of Spacing on a direct message.
    const bool show = advanced && !text.Text().empty();
    text.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
    if (show) ++shown;
  }
  urnw::LogInfo("list: advanced {} -> {} of {} group-id chips visible",
                advanced ? "on" : "off", shown, chips);
}
```

- [ ] **Step 4: apply the initial value in `app/src/App/MainWindow.xaml.cpp`.** Add to the
      include block:

```cpp
#include "Demo/AdvancedMode.h"
```

  and, in `BuildConversationList()`'s demo branch, insert immediately before
  `OnConversationSelected(0);`:

```cpp
    // Contract 5: Advanced Mode has ONE owner and no view reads the preference.
    // This is the initial application; the live subscription that re-calls this
    // on every toggle is registered by the wiring surface, which owns
    // OnAdvancedModeChanged.
    urmsg::views::SetConversationListAdvanced(list_, urmsg::AdvancedModeEnabled());
```

- [ ] **Step 5: build, and capture with Advanced Mode OFF.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

  Expected: `dips        : 1560x900`; `.verify\urmessage-window.png` identical to L5 step 6's —
  **no hex on any row.** The log must read `list: advanced off -> 0 of 8 group-id chips visible`:
  `8`, not `2`, because the chip element exists on every row and only its **text** is
  group-only. A `0 of 0` here means the tag chain did not resolve and nothing was found to toggle.

- [ ] **Step 6: turn Advanced Mode on through the preference and re-capture.** This writes the
      pref directly rather than depending on `--demo-advanced` being wired yet: `verify-render`
      points `%URMESSAGE_APP_ROOT%` at `<repo>\.localstate-verify` (`verify-render.ps1:153`), and
      `urnw::AppPrefsFile()` is `<root>\app_prefs.json` (`Paths.cpp:54`).

```
powershell -ExecutionPolicy Bypass -Command "New-Item -ItemType Directory -Force .localstate-verify | Out-Null; Set-Content -Path .localstate-verify\app_prefs.json -Value '{\"advanced_mode\": true}' -Encoding utf8; Get-Content .localstate-verify\app_prefs.json"
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

  Expected: the file prints back as `{"advanced_mode": true}`, then
  `dips        : 1560x900`, and the log reads
  `list: advanced on -> 2 of 8 group-id chips visible`.

- [ ] **Step 7: LOOK at `.verify\urmessage-window.png` and check four.**

  1. **Exactly two rows** — the two group conversations — now carry a small faint hex string in
     the trailing cluster, immediately left of anything else on that row's second line.
  2. Each chip is **at most 6 characters** and is a **prefix of that group's `groupIdHex`** — the
     property L1's `demo.list.group` already asserts over the whole world; here confirm the two
     strings on screen differ from each other.
  3. **No DM row shows a chip**, and no DM row's second line has shifted: a collapsed child costs
     a StackPanel no Spacing.
  4. **Nothing else changed.** Same 8 rows, same 64-dip height, same identicons, same two pills,
     same mute glyph, same stopwatch row, and row 1 still carries its selection bar. If the list
     is mid-fade or blank in this capture, `SetConversationListAdvanced` is rebuilding or
     animating something and must not be.

- [ ] **Step 8: put the preference back and confirm the chips go.**

```
powershell -ExecutionPolicy Bypass -Command "Set-Content -Path .localstate-verify\app_prefs.json -Value '{\"advanced_mode\": false}' -Encoding utf8"
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.localstate-verify\logs\urmessage-app.log' -Pattern 'list: advanced'"
```

  Expected: the last `list: advanced` line reads `list: advanced off -> 0 of 8 group-id chips
  visible`, and the capture is back to step 5's image with no hex anywhere. Leaving the pref at
  `true` would silently change what every later surface's capture shows, so this step is not
  optional.

- [ ] **Step 9: commit.**

```
git add app/src/App/Views/ConversationListView.h app/src/App/Views/ConversationListView.cpp app/src/App/MainWindow.xaml.cpp
git commit -m "demo: Advanced Mode adds the group-id chip to the conversation list, density only"
git show --stat --oneline HEAD
```

  Expected: exactly those three paths. `.localstate-verify` and `.verify` must NOT appear — if
  they do, they are untracked build output and belong in `.gitignore`, not in this commit.

**Deliverable:** with `advanced_mode` true the two group rows carry a ≤6-char hex prefix and
nothing else on the list changes; with it false the chips are gone and the capture is
byte-comparable to L5's. The log names the count both ways.
