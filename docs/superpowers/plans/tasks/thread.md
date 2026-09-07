# Thread

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task T1: Thread layout rules — pure C++ in Demo/ThreadLayout.h, asserted in --diagnose

**Files:**

Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/ThreadLayout.h; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.cpp

**Interfaces:**

- Consumes: F2 — `Demo/DemoWorld.h`: `urmsg::demo::MessageRow` (fields `kind`, `senderName`, `senderKey`, `body`, `timeLabel`, `outgoing`, `state`, `failureReason`), `Conversation` (`kind`, `rows`), `RowKind`, `DeliveryState`, `ConversationKind`, `GetWorld()`. F3 — the `--diagnose` block already appended in `CollectDiagnostics()` (`Startup.cpp:217`, before `return lines;`) and its PASS/FAIL-with-a-count line shape.
- Produces: Header `app/src/App/Demo/ThreadLayout.h`, namespace `urmsg::views`, PURE C++ (no `winrt/` include, no `pch.h`), header-only/`inline`:
`inline constexpr double kBubbleColumnFraction = 0.68;`
`inline constexpr double kBubbleMaxWidthDip = 640.0;`
`double BubbleMaxWidthDip(double columnWidthDip);`
`bool StartsRun(urmsg::demo::MessageRow const* prev, urmsg::demo::MessageRow const& cur);`
`bool ShowsSenderHeader(urmsg::demo::MessageRow const* prev, urmsg::demo::MessageRow const& cur, bool group);`
`bool CarriesDeliveryGlyph(urmsg::demo::MessageRow const& cur, urmsg::demo::MessageRow const* next);`
`std::wstring DaySeparatorLabel(urmsg::demo::MessageRow const& row);`
`struct DaySeparatorAudit { int separators; int unlabelled; int adjacent; int trailing; };`
`DaySeparatorAudit AuditDaySeparators(std::vector<urmsg::demo::MessageRow> const& rows);`
Four new `--diagnose` lines: `thread T1 width`, `thread T1 header`, `thread T1 glyph`, `thread T1 days`.

The thread's four decisions, with no XAML in them, so `--diagnose` can hold them to account before a single pixel exists.

Two of these are cross-checks rather than restatements, and that is the point. `ShowsSenderHeader` is asserted to agree with `MessageRow::senderName` on every row of the real world — a rule that disagrees with the data it renders fails, and so does a world that stopped emitting the field. `CarriesDeliveryGlyph` is asserted against an independently counted number of outgoing runs, so `return true` cannot pass.

**Path note.** The header lives in `Demo/` (this task's brief fixes that) but takes namespace `urmsg::views`, matching the plan's other pure view-rule modules (`Views/StatusStripRules.h`, `Views/InspectRailFields.h`, `Views/ConversationRowModel.h`). Do not move it and do not rename the namespace.

- [ ] **Step 1: create `app/src/App/Demo/ThreadLayout.h`.**

  ```cpp
  // Thread layout rules: what the thread surface decides about ROWS, with no
  // XAML in it.
  //
  // PURE C++, like Demo/DemoWorld.h beside it and for the same reason. Every
  // function here is called from CollectDiagnostics() (Startup.cpp), which
  // wWinMain runs at main.cpp:168 — BEFORE winrt::init_apartment() at
  // main.cpp:181. A winrt type in this header would be constructed with no
  // apartment, and pch.h (which pulls in every winrt/ header) must not appear.
  //
  // Namespace is urmsg::views, not urmsg::demo: these are PRESENTATION rules
  // about demo data — the pure half of Views/ThreadView.*, the same split as
  // Views/StatusStripRules.h. Only the PATH is in Demo/, beside the data.
  //
  // SPDX-License-Identifier: MPL-2.0
  #pragma once

  #include <cstddef>
  #include <string>
  #include <vector>

  #include "Demo/DemoWorld.h"

  namespace urmsg::views {

  // ---- bubble width (design §6.2, Spec C §5.2) -----------------------------
  // 68% of the column, capped at 640 DIP.
  inline constexpr double kBubbleColumnFraction = 0.68;
  inline constexpr double kBubbleMaxWidthDip = 640.0;

  // A column that has not been measured yet — ActualWidth is 0 on the first
  // layout pass — must NOT collapse every bubble to zero width. It gets the
  // cap, so the first composed frame is a normal thread that the next pass
  // narrows, rather than a column of slivers.
  //
  // No std::min: pch.h defines WIN32_LEAN_AND_MEAN but not NOMINMAX, so
  // windows.h's min/max macros are live in every TU that includes this.
  inline double BubbleMaxWidthDip(double columnWidthDip) {
    if (columnWidthDip <= 0.0) return kBubbleMaxWidthDip;
    const double w = columnWidthDip * kBubbleColumnFraction;
    return w > kBubbleMaxWidthDip ? kBubbleMaxWidthDip : w;
  }

  // ---- sender runs ---------------------------------------------------------
  // A run is consecutive Message rows from the same sender. The FIRST bubble of
  // a run carries the sender's name and identicon; continuations carry neither.
  //
  // `prev` is the row immediately above `cur` in Conversation::rows, or nullptr
  // for the first row. A day separator, a system row and a direction change all
  // end a run — which is exactly what DemoWorld does when it clears its
  // previousSender, so ShowsSenderHeader() below is checkable against
  // MessageRow::senderName on real data instead of only against itself.
  inline bool StartsRun(demo::MessageRow const* prev, demo::MessageRow const& cur) {
    if (cur.kind != demo::RowKind::Message) return false;
    if (prev == nullptr) return true;
    if (prev->kind != demo::RowKind::Message) return true;  // a separator/system row breaks it
    if (prev->outgoing) return true;   // DemoWorld clears previousSender on an outgoing row
    if (cur.outgoing) return true;
    return prev->senderKey != cur.senderKey;
  }

  // Does this bubble draw a sender name AND an identicon? Groups only —
  // never in a DM (there is exactly one other person and the pane header
  // already names them), never on an outgoing row.
  inline bool ShowsSenderHeader(demo::MessageRow const* prev, demo::MessageRow const& cur,
                                bool group) {
    return group && !cur.outgoing && StartsRun(prev, cur);
  }

  // ---- the delivery glyph --------------------------------------------------
  // Design §6.2: right-aligned under the LAST outgoing bubble of a run — one
  // reading per run, not a column of ticks.
  //
  // Plus one addition, stated rather than smuggled: a Failed row ALWAYS carries
  // it, wherever it sits. "Last of run" alone hides a failure the moment
  // someone sends again after it, and a silent failure is the one delivery
  // state the demo must never lose.
  inline bool CarriesDeliveryGlyph(demo::MessageRow const& cur, demo::MessageRow const* next) {
    if (cur.kind != demo::RowKind::Message || !cur.outgoing) return false;
    if (cur.state == demo::DeliveryState::Failed) return true;
    const bool nextIsOutgoingMessage =
        next != nullptr && next->kind == demo::RowKind::Message && next->outgoing;
    return !nextIsOutgoingMessage;
  }

  // ---- day separators ------------------------------------------------------
  // DemoWorld puts the day label in MessageRow::body — F2's AppendRows sets
  // `r.body = s.body` for every row and clears it only for a System row, so a
  // DaySeparator keeps its label there. systemText is System-only (contract §1).
  //
  // Returns empty for anything that is not a labelled separator, so a malformed
  // row draws NOTHING rather than an empty pill.
  inline std::wstring DaySeparatorLabel(demo::MessageRow const& row) {
    if (row.kind != demo::RowKind::DaySeparator) return {};
    return row.body;
  }

  // Well-formedness of one conversation's separator sequence. Counted, not
  // boolean, so the --diagnose line prints what it actually looked at.
  struct DaySeparatorAudit {
    int separators = 0;   // rows with kind == DaySeparator
    int unlabelled = 0;   // ...whose DaySeparatorLabel() is empty
    int adjacent = 0;     // ...immediately preceded by another separator
    int trailing = 0;     // ...that are the last row, so they separate nothing
  };

  inline DaySeparatorAudit AuditDaySeparators(std::vector<demo::MessageRow> const& rows) {
    DaySeparatorAudit a;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].kind != demo::RowKind::DaySeparator) continue;
      ++a.separators;
      if (DaySeparatorLabel(rows[i]).empty()) ++a.unlabelled;
      if (i > 0 && rows[i - 1].kind == demo::RowKind::DaySeparator) ++a.adjacent;
      if (i + 1 == rows.size()) ++a.trailing;
    }
    return a;
  }

  }  // namespace urmsg::views
  ```

- [ ] **Step 2: register the header in `app/src/App/App.vcxproj`.** In the `ClInclude` ItemGroup (line 188–198), after the line `<ClInclude Include="Demo/DemoWorld.h" />` that F2 added:

  ```xml
    <ClInclude Include="Demo/ThreadLayout.h" />
  ```

  Header-only, so there is no `ClCompile` entry and no `PrecompiledHeader` setting to get wrong.

- [ ] **Step 3: add the four assertions to `CollectDiagnostics()`.** In `app/src/App/Startup.cpp`, add `#include <cmath>` beside the existing `#include <format>` (line 12) and `#include "Demo/ThreadLayout.h"` beside F3's `#include "Demo/DemoWorld.h"`. Then insert this block immediately **before** `return lines;` (line 217), after F3's world invariants:

  ```cpp
  // ---- thread layout rules (Demo/ThreadLayout.h) ---------------------------
  {
    namespace demo = urmsg::demo;
    namespace views = urmsg::views;
    auto const& world = demo::GetWorld();

    // 1. Width. The probes bracket the cap crossover (640 / 0.68 = 941.18), so
    //    neither `return 640;` nor `return 0.68 * w;` survives, and 0 proves the
    //    unmeasured-column guard.
    struct WidthProbe { double column; double expect; };
    static constexpr WidthProbe kProbes[] = {
        {0.0, 640.0}, {400.0, 272.0}, {941.0, 639.88}, {942.0, 640.0}, {1560.0, 640.0}};
    int widthOk = 0;
    for (auto const& p : kProbes)
      if (std::fabs(views::BubbleMaxWidthDip(p.column) - p.expect) < 0.01) ++widthOk;
    lines.push_back(std::format(
        L"  thread T1 width  : {} - {}/5 probes at 0/400/941/942/1560 dip",
        widthOk == 5 ? L"PASS" : L"FAIL", widthOk));

    // 2. Sender header. The view rule must agree with DemoWorld's own
    //    run-continuation rule on EVERY message row: senderName is non-empty
    //    exactly when the bubble draws a name and an identicon.
    int msgRows = 0, headers = 0, disagree = 0;
    for (auto const& c : world.conversations) {
      const bool group = (c.kind == demo::ConversationKind::Group);
      for (std::size_t i = 0; i < c.rows.size(); ++i) {
        auto const& row = c.rows[i];
        if (row.kind != demo::RowKind::Message) continue;
        ++msgRows;
        demo::MessageRow const* prev = (i > 0) ? &c.rows[i - 1] : nullptr;
        const bool shows = views::ShowsSenderHeader(prev, row, group);
        if (shows) ++headers;
        if (shows != !row.senderName.empty()) ++disagree;
      }
    }
    lines.push_back(std::format(
        L"  thread T1 header : {} - {} message rows, {} draw a sender header, "
        L"{} disagree with DemoWorld::senderName",
        (disagree == 0 && headers > 0) ? L"PASS" : L"FAIL", msgRows, headers, disagree));

    // 3. Delivery glyph. runStarts is counted from the run TRANSITION, i.e.
    //    independently of CarriesDeliveryGlyph, so `return true` (carriers ==
    //    outgoing) and a rule that forgot the Failed exception both fail.
    int outgoing = 0, runStarts = 0, carriers = 0, failed = 0, failedMidRun = 0,
        failedCarrying = 0;
    for (auto const& c : world.conversations) {
      bool inRun = false;
      for (std::size_t i = 0; i < c.rows.size(); ++i) {
        auto const& row = c.rows[i];
        const bool isOut = (row.kind == demo::RowKind::Message && row.outgoing);
        if (isOut && !inRun) ++runStarts;
        inRun = isOut;
        if (!isOut) continue;
        ++outgoing;
        demo::MessageRow const* next = (i + 1 < c.rows.size()) ? &c.rows[i + 1] : nullptr;
        const bool lastOfRun =
            !(next && next->kind == demo::RowKind::Message && next->outgoing);
        const bool isFailed = (row.state == demo::DeliveryState::Failed);
        if (isFailed) { ++failed; if (!lastOfRun) ++failedMidRun; }
        if (views::CarriesDeliveryGlyph(row, next)) {
          ++carriers;
          if (isFailed) ++failedCarrying;
        }
      }
    }
    const bool glyphOk = (carriers == runStarts + failedMidRun) && (failed > 0) &&
                         (failedCarrying == failed) && (outgoing > runStarts);
    lines.push_back(std::format(
        L"  thread T1 glyph  : {} - {} outgoing rows in {} runs, {} carry a glyph, "
        L"{}/{} failed carry one ({} mid-run)",
        glyphOk ? L"PASS" : L"FAIL", outgoing, runStarts, carriers, failedCarrying,
        failed, failedMidRun));

    // 4. Day separators. Never unlabelled, never doubled, never last.
    views::DaySeparatorAudit total;
    int convsWithSeparators = 0;
    for (auto const& c : world.conversations) {
      const auto a = views::AuditDaySeparators(c.rows);
      if (a.separators > 0) ++convsWithSeparators;
      total.separators += a.separators;
      total.unlabelled += a.unlabelled;
      total.adjacent += a.adjacent;
      total.trailing += a.trailing;
    }
    const bool daysOk = total.separators >= 2 && total.unlabelled == 0 &&
                        total.adjacent == 0 && total.trailing == 0;
    lines.push_back(std::format(
        L"  thread T1 days   : {} - {} separators across {} conversations; "
        L"{} unlabelled, {} adjacent, {} trailing",
        daysOk ? L"PASS" : L"FAIL", total.separators, convsWithSeparators,
        total.unlabelled, total.adjacent, total.trailing));
  }
  ```

- [ ] **Step 4: build.**

  ```
  powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
  ```

  Expected: the script ends with the MSBuild summary `0 Error(s)` and prints the path to `app\build\x64\Release\URmessage.exe`.

- [ ] **Step 5: run `--diagnose` and read the four lines.** Run this from the **Bash** tool, not PowerShell: PowerShell's `&` does not wait on a GUI-subsystem process, so `cat` would race the write. Bash waits on the child.

  ```
  cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows && mkdir -p .verify && ./app/build/x64/Release/URmessage.exe --diagnose > .verify/diag.txt 2>&1 ; cat .verify/diag.txt
  ```

  Expected — four lines, all `PASS`, every count non-zero:

  ```
    thread T1 width  : PASS - 5/5 probes at 0/400/941/942/1560 dip
    thread T1 header : PASS - 42 message rows, 6 draw a sender header, 0 disagree with DemoWorld::senderName
    thread T1 glyph  : PASS - 14 outgoing rows in 8 runs, 9 carry a glyph, 1/1 failed carry one (1 mid-run)
    thread T1 days   : PASS - 3 separators across 2 conversations; 0 unlabelled, 0 adjacent, 0 trailing
  ```

  The exact totals depend on F2's world; what must hold is every line `PASS`, `header` reporting more than 0 headers and 0 disagreements, `glyph` reporting more outgoing rows than runs, and `days` reporting at least 2 separators with three zeros after them. **A count of 0 anywhere on the `header` or `days` line is a vacuous pass — treat it as a FAIL and fix F2's world or the rule, not the assertion.**

- [ ] **Step 6: prove the width line can fail.** Temporarily change `kBubbleColumnFraction` to `0.60` in `Demo/ThreadLayout.h`, rebuild (Step 4) and re-run Step 5.

  Expected: `thread T1 width  : FAIL - 3/5 probes at 0/400/941/942/1560 dip` (the 0, 942 and 1560 probes still land on the cap; 400 and 941 move). Then restore `0.68`, rebuild, and confirm Step 5 prints `PASS - 5/5` again. This is the only falsification step in the task; do not skip it, and do not add more.

---

## Task T2: The bubble: direction fills, the body face, one new chromeless Button style, and its accessible name

**Files:**

Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/ThreadView.h; Create C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/ThreadView.cpp; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.xaml; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/ThreadLayout.h; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.cpp

**Interfaces:**

- Consumes: T1 — `Demo/ThreadLayout.h` (`BubbleMaxWidthDip`). F2 — `urmsg::demo::MessageRow`, `DeliveryState`, `Conversation`, `GetWorld()`. F5 — `Identicon.h`: `winrt::Microsoft::UI::Xaml::Controls::Border urmsg::MakeIdenticon(urmsg::demo::Seed const&, double size = 40)`, which applies its OWN `CornerRadius(8)` (callers must not set one). Existing: `App.xaml` keys `UrCardBrush`, `UrCardHoverBrush`, `UrBorderBrush`, `UrBorderStrongBrush`, `UrBodyTextStyle`, `UrCaptionTextStyle`, `UrBodyFontFamily`, `UrIconFontFamily`; `UrColors.h` `kCard`/`kCardHover`/`kBorder` and `DangerBrush()`/`MutedBrush()`/`TextBrush()`/`FaintBrush()`.
- Produces: `App.xaml`: ONE new key, `UrBubbleButtonStyle` (Button).
Header `app/src/App/Views/ThreadView.h`, namespace `urmsg::views` — contract §4's `ThreadBubble`/`ThreadView` block verbatim plus the thread's internal builder:
`inline constexpr double kThreadGutterDip = 36.0;`
`inline constexpr double kThreadIdenticonDip = 28.0;`
`struct BubbleRow { winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr}; ThreadBubble bubble; };`
`BubbleRow MakeBubbleRow(urmsg::demo::MessageRow const& row, bool group, bool showSenderHeader, bool carriesDeliveryGlyph);`
Added to `Demo/ThreadLayout.h` (pure): `std::wstring DeliveryGlyph(urmsg::demo::DeliveryState);`, `std::wstring DeliveryWord(urmsg::demo::DeliveryState);`, `std::wstring BubbleAutomationName(urmsg::demo::MessageRow const& row, bool group);`
Two new `--diagnose` lines: `thread T2 glyphs`, `thread T2 names`.

**Why one new style key, measured.** Every Button style in `App.xaml` repaints the fill on hover, and the bubble's fill is what says *which direction this message went* — so every one of them is disqualified, not merely inconvenient:

| Key | What PointerOver does | Why a bubble cannot use it |
|---|---|---|
| `UrCardButtonStyle` (`:632`), `UrCardRowButtonStyle` (`:709`) | sets `Root.Background` to `UrCardHoverBrush` **#242424** | that is the OUTGOING fill; hovering an incoming bubble makes it look outgoing |
| `UrPaneRowButtonStyle` (`:891`), `UrPaneActionButtonStyle` (`:955`) | sets `Root.Background` to `UrCardBrush` **#1C1C1C** | that is the INCOMING fill; and hovering an incoming bubble changes nothing at all, so it has no affordance |
| `UrButtonBaseStyle` (`:305`) and its `UrPrimary`/`UrSecondary`/`UrPaneActionPrimary`/`UrPaneActionSecondary` children | washes a `StateLayer` of the Foreground colour over the fill at 8%/12% | plus `HorizontalAlignment=Stretch` (a bubble must hug its content and align left or right), `MinHeight=48`, and the **wordmark face at 24 Bold** — which this task is forbidden to use |

So: one new key, `UrBubbleButtonStyle`. Its hover is an **edge**, drawn by a dedicated overlay layer, and it never touches `Root.Background` **or** `Root.BorderBrush` — which also leaves `BorderBrush`/`BorderThickness` free for the later `SetThreadSelectedMessage` to own, so the `UrAccentBrush` selection outline and the hover edge compose instead of fighting.

**A documented deviation from design §6.2.** The design asks for "two outline checks" and "two filled checks". Segoe Fluent Icons has no double-checkmark in the codepoint range this repo already proves out (`E713`, `E721`, `E76C`, `E77B`, `E8BD`, `E8C8` are all MDL2-era), and faking it with two overlapped `E73E` glyphs would leave Delivered and Read distinguishable only by colour — which contract rule 6 forbids. Delivered and Read therefore get two *different shapes*: an outline circled check and a solid one. Five states, five distinct shapes; colour is the second channel, never the only one.

- [ ] **Step 1: add the one new style key to `app/src/App/App.xaml`.** Insert immediately **before** the line `<!-- ==== UrSwitchToggle (Components/UrSwitchToggle.swift) ==========` (line 732), i.e. after the `UrCardRowStyle` block closes:

  ```xml
            <!-- ==== the message bubble ======================================
                 The ONE new button style this work adds, and it exists because
                 no existing one can be used: every Button style in this file
                 repaints the FILL on hover, and a bubble's fill is what says
                 which direction the message went. UrCardButtonStyle hovers to
                 #242424 (the outgoing fill, so an incoming bubble becomes an
                 outgoing one under the pointer); UrPaneRowButtonStyle hovers to
                 #1C1C1C (the incoming fill, and no change at all on an incoming
                 bubble); UrButtonBaseStyle washes the foreground over the fill
                 and is Stretch/48-tall/NeueBit-24.

                 So hover here is an EDGE, on its own layer. Root.Background and
                 Root.BorderBrush are never touched by a visual state, which is
                 what lets the caller own the direction fill and the 1px edge,
                 and lets the selection outline (UrAccentBrush, set on
                 BorderBrush by the selection task) survive a hover instead of
                 being overwritten by it.

                 BorderThickness is 1 for BOTH directions - incoming's brush is
                 Transparent - so the two have identical inner metrics and the
                 hover edge lands in the same place on both. -->
            <Style x:Key="UrBubbleButtonStyle" TargetType="Button">
                <Setter Property="Background" Value="{StaticResource UrCardBrush}" />
                <Setter Property="Foreground" Value="{StaticResource UrTextBrush}" />
                <Setter Property="BorderBrush" Value="Transparent" />
                <Setter Property="BorderThickness" Value="1" />
                <Setter Property="CornerRadius" Value="12" />
                <Setter Property="Padding" Value="12,8" />
                <Setter Property="MinHeight" Value="0" />
                <Setter Property="MinWidth" Value="0" />
                <Setter Property="HorizontalAlignment" Value="Left" />
                <Setter Property="HorizontalContentAlignment" Value="Stretch" />
                <Setter Property="VerticalContentAlignment" Value="Top" />
                <Setter Property="FontFamily" Value="{StaticResource UrBodyFontFamily}" />
                <Setter Property="FontSize" Value="14" />
                <Setter Property="UseSystemFocusVisuals" Value="True" />
                <Setter Property="Template">
                    <Setter.Value>
                        <ControlTemplate TargetType="Button">
                            <Grid x:Name="Root"
                                  CornerRadius="{TemplateBinding CornerRadius}"
                                  Background="{TemplateBinding Background}"
                                  BorderBrush="{TemplateBinding BorderBrush}"
                                  BorderThickness="{TemplateBinding BorderThickness}">
                                <!-- the hover/press edge, on its own layer so no
                                     visual state ever writes Root.Background or
                                     Root.BorderBrush -->
                                <Grid x:Name="EdgeLayer" Opacity="0"
                                      CornerRadius="{TemplateBinding CornerRadius}"
                                      BorderBrush="{StaticResource UrBorderStrongBrush}"
                                      BorderThickness="1" />
                                <!-- the three font properties are template-bound
                                     for the reason UrButtonBaseStyle spells out:
                                     ContentPresenter declares its own defaults,
                                     so a Style setter never reaches the content. -->
                                <ContentPresenter x:Name="Presenter"
                                                  Content="{TemplateBinding Content}"
                                                  ContentTemplate="{TemplateBinding ContentTemplate}"
                                                  Padding="{TemplateBinding Padding}"
                                                  Foreground="{TemplateBinding Foreground}"
                                                  FontFamily="{TemplateBinding FontFamily}"
                                                  FontSize="{TemplateBinding FontSize}"
                                                  FontWeight="{TemplateBinding FontWeight}"
                                                  HorizontalContentAlignment="{TemplateBinding HorizontalContentAlignment}"
                                                  VerticalContentAlignment="{TemplateBinding VerticalContentAlignment}" />
                                <VisualStateManager.VisualStateGroups>
                                    <VisualStateGroup x:Name="CommonStates">
                                        <VisualStateGroup.Transitions>
                                            <VisualTransition GeneratedDuration="0:0:0.15" />
                                        </VisualStateGroup.Transitions>
                                        <VisualState x:Name="Normal" />
                                        <VisualState x:Name="PointerOver">
                                            <VisualState.Setters>
                                                <Setter Target="EdgeLayer.Opacity" Value="1" />
                                            </VisualState.Setters>
                                        </VisualState>
                                        <VisualState x:Name="Pressed">
                                            <VisualState.Setters>
                                                <Setter Target="EdgeLayer.Opacity" Value="1" />
                                                <Setter Target="Root.Opacity" Value="0.92" />
                                            </VisualState.Setters>
                                        </VisualState>
                                        <VisualState x:Name="Disabled">
                                            <VisualState.Setters>
                                                <Setter Target="Root.Opacity" Value="0.38" />
                                            </VisualState.Setters>
                                        </VisualState>
                                    </VisualStateGroup>
                                </VisualStateManager.VisualStateGroups>
                            </Grid>
                        </ControlTemplate>
                    </Setter.Value>
                </Setter>
            </Style>
  ```

  The `0:0:0.15` transition is `UrMotion.h`'s `kFastMs` written in the form every other style in this file already uses; no new token.

- [ ] **Step 2: add the three pure string rules to `app/src/App/Demo/ThreadLayout.h`.** Append inside `namespace urmsg::views`, before the closing brace:

  ```cpp
  // ---- delivery vocabulary -------------------------------------------------
  // Segoe Fluent Icons codepoints. FIVE DISTINCT SHAPES, because Delivered and
  // Read must not differ by colour alone (contract rule 6) and this font has no
  // double-checkmark: design §6.2's "two outline checks / two filled checks"
  // becomes an outline circled check and a solid one. Never an empty literal
  // (contract rule 3) — every arm names its icon.
  inline std::wstring DeliveryGlyph(demo::DeliveryState s) {
    switch (s) {
      case demo::DeliveryState::Pending:   return L"";  // Stopwatch
      case demo::DeliveryState::Sent:      return L"";  // CheckMark
      case demo::DeliveryState::Delivered: return L"";  // Completed (outline circled check)
      case demo::DeliveryState::Read:      return L"";  // CompletedSolid (filled circled check)
      case demo::DeliveryState::Failed:    return L"";  // Error
      case demo::DeliveryState::Expired:   return L"";  // Delete
    }
    return L"";  // CheckMark
  }

  // The same six states as WORDS. This is the channel that keeps delivery state
  // out of colour-alone: it is what a screen reader hears, via
  // BubbleAutomationName below.
  inline std::wstring DeliveryWord(demo::DeliveryState s) {
    switch (s) {
      case demo::DeliveryState::Pending:   return L"Sending";
      case demo::DeliveryState::Sent:      return L"Sent";
      case demo::DeliveryState::Delivered: return L"Delivered";
      case demo::DeliveryState::Read:      return L"Read";
      case demo::DeliveryState::Failed:    return L"Failed";
      case demo::DeliveryState::Expired:   return L"Expired";
    }
    return L"Sent";
  }

  // What a screen reader hears when it reaches a bubble.
  //
  // A Button whose Content is a Panel gets NO automatic automation name —
  // UrComponents.h records that this project has paid for the lesson twice — so
  // without this the whole thread reads as a column of "button".
  //
  // On a run CONTINUATION senderName is empty by design (the bubble draws no
  // name), so the sender comes from inspect.senderDisplayName, which contract §1
  // guarantees is always populated. The visual omits it; the announcement
  // must not.
  //
  //   incoming, group : "Bo Nakamura, 14:22. Slide 4 is the one."
  //   incoming, DM    : "14:22. Slide 4 is the one."
  //   outgoing        : "You, 14:22, Read. On it."
  //   outgoing failed : "You, 14:22, Failed: no route to recipient. On it."
  inline std::wstring BubbleAutomationName(demo::MessageRow const& row, bool group) {
    std::wstring name;
    if (row.outgoing) {
      name = L"You, ";
    } else if (group) {
      const std::wstring& who =
          row.senderName.empty() ? row.inspect.senderDisplayName : row.senderName;
      if (!who.empty()) name = who + L", ";
    }
    name += row.timeLabel;
    if (row.outgoing) {
      name += L", " + DeliveryWord(row.state);
      if (row.state == demo::DeliveryState::Failed && !row.failureReason.empty())
        name += L": " + row.failureReason;
    }
    name += L". " + row.body;
    return name;
  }
  ```

- [ ] **Step 3: create `app/src/App/Views/ThreadView.h`.** This is contract §4's block verbatim, plus the thread's own internal builder. `MakeThread` / `SetThreadConversation` / `SetThreadSelectedMessage` / `SetThreadTyping` / `AppendThreadRow` are declared here and defined by later tasks; nothing calls them yet, so the link is clean.

  ```cpp
  // The thread surface: bubbles, day separators, system rows, the column.
  //
  // Structure follows the UrComponents.h grain — a struct of named elements plus
  // free Make*/Set* functions. No classes with virtuals, no MVVM, no IDL.
  // The pure half (what decides, as opposed to what draws) is
  // Demo/ThreadLayout.h, which has no winrt in it.
  //
  // SPDX-License-Identifier: MPL-2.0
  #pragma once

  #include <functional>
  #include <string>
  #include <vector>

  #include <winrt/Microsoft.UI.Xaml.Controls.h>
  #include <winrt/Microsoft.UI.Xaml.h>

  #include "Demo/DemoWorld.h"

  namespace urmsg::views {

  // ---- the fixed contract §4 block, verbatim -------------------------------
  struct ThreadBubble {
    std::wstring id;
    winrt::Microsoft::UI::Xaml::Controls::Button root{nullptr};
  };
  struct ThreadView {
    winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroller{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel stack{nullptr};
    std::vector<ThreadBubble> bubbles;
  };
  ThreadView MakeThread(std::function<void(std::wstring)> onSelectMessage,
                        std::function<void()> onDeselect);
  void SetThreadConversation(ThreadView& v, urmsg::demo::Conversation const& c);
  void SetThreadSelectedMessage(ThreadView& v, std::wstring const& id);
  void SetThreadTyping(ThreadView& v, bool typing);
  void AppendThreadRow(ThreadView& v, urmsg::demo::MessageRow const& row);

  // ---- the thread's own internals (NOT in the contract) --------------------
  // The identicon gutter. 28 + 8 of air: Spec C §W9's 40x40 is the LIST row's
  // avatar, and 40 beside a 20 DIP line of body text in a thread is a portrait,
  // not an avatar. Continuation bubbles draw nothing in the gutter but still
  // reserve it, so a run keeps one left edge.
  inline constexpr double kThreadGutterDip = 36.0;
  inline constexpr double kThreadIdenticonDip = 28.0;

  // One row of the thread stack for a MESSAGE row: the identicon gutter, the
  // bubble Button, and — when this row carries it — the delivery glyph under the
  // bubble. Two elements come back because they have different owners: `root`
  // goes into ThreadView::stack, `bubble` goes into ThreadView::bubbles, and
  // ThreadBubble::root stays the Button exactly as the contract requires (the
  // identicon must sit OUTSIDE it, or the bubble's fill would paint the gutter).
  struct BubbleRow {
    winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
    ThreadBubble bubble;
  };
  BubbleRow MakeBubbleRow(urmsg::demo::MessageRow const& row, bool group,
                          bool showSenderHeader, bool carriesDeliveryGlyph);

  }  // namespace urmsg::views
  ```

- [ ] **Step 4: create `app/src/App/Views/ThreadView.cpp` with the bubble builder.**

  ```cpp
  // SPDX-License-Identifier: MPL-2.0
  #include "pch.h"  // /Yu"pch.h": every normal TU includes it FIRST

  #include "Views/ThreadView.h"

  #include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
  #include <winrt/Microsoft.UI.Xaml.Automation.h>
  #include <winrt/Microsoft.UI.Xaml.Media.h>

  #include "Demo/ThreadLayout.h"
  #include "Identicon.h"
  #include "UrColors.h"

  using namespace winrt::Microsoft::UI::Xaml;
  using namespace winrt::Microsoft::UI::Xaml::Controls;

  namespace urmsg::views {
  namespace {

  namespace demo = urmsg::demo;

  // UrComponents.cpp's StyleByKey/MetricByKey are file-local to that unit, so
  // this one needs its own pair. Applying styles by KEY rather than by hand is
  // what keeps the bubble in step with App.xaml; a missing key must not throw a
  // layout away.
  Style StyleByKey(wchar_t const* key) {
    auto app = Application::Current();
    if (!app) return nullptr;
    auto boxed = winrt::box_value(winrt::hstring{key});
    if (!app.Resources().HasKey(boxed)) return nullptr;
    return app.Resources().Lookup(boxed).try_as<Style>();
  }

  Media::Brush BrushByKey(wchar_t const* key, winrt::Windows::UI::Color fallback) {
    auto app = Application::Current();
    if (app) {
      auto boxed = winrt::box_value(winrt::hstring{key});
      if (app.Resources().HasKey(boxed))
        if (auto b = app.Resources().Lookup(boxed).try_as<Media::Brush>()) return b;
    }
    return urnw::colors::MakeBrush(fallback);
  }

  void MarkRaw(UIElement const& e) {
    Automation::AutomationProperties::SetAccessibilityView(
        e, Automation::Peers::AccessibilityView::Raw);
  }

  // The delivery reading under the last outgoing bubble of a run. Right-aligned,
  // 12px, and for the one state that must never rest on a 12px glyph the word
  // is beside it in the danger brush — colour is a second channel here, never
  // the only one.
  FrameworkElement MakeDeliveryLine(demo::MessageRow const& row) {
    StackPanel line;
    line.Orientation(Orientation::Horizontal);
    line.Spacing(4);
    line.HorizontalAlignment(HorizontalAlignment::Right);
    line.Margin(ThicknessHelper::FromLengths(0, 2, 2, 0));

    const bool failed = (row.state == demo::DeliveryState::Failed);

    FontIcon icon;
    icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
    icon.FontSize(12);
    icon.Glyph(winrt::hstring{DeliveryGlyph(row.state)});
    icon.Foreground(failed ? urnw::colors::DangerBrush()
                           : (row.state == demo::DeliveryState::Read
                                  ? urnw::colors::TextBrush()
                                  : urnw::colors::MutedBrush()));
    icon.VerticalAlignment(VerticalAlignment::Center);
    // the bubble's automation name already says the delivery WORD; announcing
    // the glyph again would put a second item beside the thing it describes
    MarkRaw(icon);
    line.Children().Append(icon);

    if (failed) {
      TextBlock word;
      word.Text(winrt::hstring{DeliveryWord(row.state)});
      if (auto s = StyleByKey(L"UrCaptionTextStyle")) word.Style(s);
      word.FontSize(11);
      word.Foreground(urnw::colors::DangerBrush());
      word.VerticalAlignment(VerticalAlignment::Center);
      MarkRaw(word);
      line.Children().Append(word);
    }
    return line;
  }

  }  // namespace

  BubbleRow MakeBubbleRow(demo::MessageRow const& row, bool group, bool showSenderHeader,
                          bool carriesDeliveryGlyph) {
    BubbleRow out;
    out.bubble.id = row.id;

    // ---- the bubble ---------------------------------------------------------
    Button bubble;
    if (auto style = StyleByKey(L"UrBubbleButtonStyle")) bubble.Style(style);

    // Design §6.2: incoming UrCardBrush #1C1C1C left, outgoing UrCardHoverBrush
    // #242424 with a 1px UrBorderBrush right. UrAccentBrush is NEVER a bubble
    // fill — it is the send button and the selection outline only.
    bubble.Background(row.outgoing ? BrushByKey(L"UrCardHoverBrush", urnw::colors::kCardHover)
                                   : BrushByKey(L"UrCardBrush", urnw::colors::kCard));
    bubble.BorderThickness(ThicknessHelper::FromUniformLength(1));
    bubble.BorderBrush(row.outgoing
                           ? BrushByKey(L"UrBorderBrush", urnw::colors::kBorder)
                           : Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
    bubble.HorizontalAlignment(row.outgoing ? HorizontalAlignment::Right
                                            : HorizontalAlignment::Left);
    // The cap now; the thread column narrows it on SizeChanged (the column task
    // owns that walk). Never 0 here — a bubble built before the column has been
    // measured must still be a bubble.
    bubble.MaxWidth(BubbleMaxWidthDip(0.0));

    StackPanel column;
    column.Spacing(2);

    if (showSenderHeader && !row.senderName.empty()) {
      TextBlock name;
      name.Text(winrt::hstring{row.senderName});
      if (auto s = StyleByKey(L"UrCaptionTextStyle")) name.Style(s);
      name.Foreground(urnw::colors::MutedBrush());
      name.TextTrimming(TextTrimming::CharacterEllipsis);
      MarkRaw(name);
      column.Children().Append(name);
    }

    // THE BODY FACE. UrBodyTextStyle is UrBodyFontFamily at 14/20 — never
    // UrHeadingFontFamily, which is the display face for titles and the hero.
    TextBlock body;
    body.Text(winrt::hstring{row.body});
    if (auto s = StyleByKey(L"UrBodyTextStyle")) body.Style(s);
    body.TextWrapping(TextWrapping::Wrap);
    MarkRaw(body);
    column.Children().Append(body);

    TextBlock time;
    time.Text(winrt::hstring{row.timeLabel});
    if (auto s = StyleByKey(L"UrCaptionTextStyle")) time.Style(s);
    time.FontSize(11);
    time.Foreground(urnw::colors::FaintBrush());
    time.HorizontalAlignment(HorizontalAlignment::Right);
    MarkRaw(time);
    column.Children().Append(time);

    bubble.Content(column);
    // A Button whose Content is a Panel gets NO automatic name.
    Automation::AutomationProperties::SetName(
        bubble, winrt::hstring{BubbleAutomationName(row, group)});
    out.bubble.root = bubble;

    // ---- the gutter + bubble row -------------------------------------------
    Grid gutterRow;
    ColumnDefinition gutter, content;
    const bool wantsGutter = group && !row.outgoing;
    gutter.Width(GridLengthHelper::FromPixels(wantsGutter ? kThreadGutterDip : 0.0));
    content.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    gutterRow.ColumnDefinitions().Append(gutter);
    gutterRow.ColumnDefinitions().Append(content);

    if (wantsGutter && showSenderHeader) {
      // MakeIdenticon applies its OWN CornerRadius(8) — do not set one here.
      auto ident = urmsg::MakeIdenticon(row.senderKey, kThreadIdenticonDip);
      ident.VerticalAlignment(VerticalAlignment::Top);
      ident.HorizontalAlignment(HorizontalAlignment::Left);
      MarkRaw(ident);
      Grid::SetColumn(ident, 0);
      gutterRow.Children().Append(ident);
    }
    Grid::SetColumn(bubble, 1);
    gutterRow.Children().Append(bubble);

    StackPanel rowRoot;
    rowRoot.Spacing(0);
    rowRoot.Children().Append(gutterRow);
    if (carriesDeliveryGlyph) rowRoot.Children().Append(MakeDeliveryLine(row));
    out.root = rowRoot;
    return out;
  }

  }  // namespace urmsg::views
  ```

  The delivery glyph is **static** here: state maps to a shape, and nothing animates. The `kFastMs` cross-dissolve of design §7 is additive on top of this and belongs to the delivery-morph task; it is the only thread motion this task leaves out, and it leaves it out deliberately rather than by omission.

- [ ] **Step 5: register the two files in `app/src/App/App.vcxproj`.** In the `ClCompile` ItemGroup (after line 185), and the `ClInclude` ItemGroup (after the `Demo/ThreadLayout.h` line T1 added):

  ```xml
    <ClCompile Include="Views\ThreadView.cpp" />
  ```

  ```xml
    <ClInclude Include="Views\ThreadView.h" />
  ```

  No `PrecompiledHeader` override: this is a normal TU that uses the project pch. (`Demo/DemoWorld.cpp` is the unit that must **not**.)

- [ ] **Step 6: add the two assertions to `CollectDiagnostics()`.** In `app/src/App/Startup.cpp`, inside the `{ ... }` block T1 added, immediately before its closing brace:

  ```cpp
    // 5. Delivery vocabulary. Six states, six NON-EMPTY, six DISTINCT glyphs —
    //    the empty-glyph-literal defect and the "two states differ only by
    //    colour" defect are the same check.
    static constexpr demo::DeliveryState kStates[] = {
        demo::DeliveryState::Pending, demo::DeliveryState::Sent,
        demo::DeliveryState::Delivered, demo::DeliveryState::Read,
        demo::DeliveryState::Failed, demo::DeliveryState::Expired};
    std::vector<std::wstring> glyphs, words;
    for (auto s : kStates) { glyphs.push_back(views::DeliveryGlyph(s));
                             words.push_back(views::DeliveryWord(s)); }
    int nonEmpty = 0, distinct = 0, distinctWords = 0;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
      if (!glyphs[i].empty()) ++nonEmpty;
      bool dupG = false, dupW = false;
      for (std::size_t j = 0; j < i; ++j) {
        if (glyphs[j] == glyphs[i]) dupG = true;
        if (words[j] == words[i]) dupW = true;
      }
      if (!dupG) ++distinct;
      if (!dupW) ++distinctWords;
    }
    lines.push_back(std::format(
        L"  thread T2 glyphs : {} - 6 states, {} non-empty, {} distinct glyphs, {} distinct words",
        (nonEmpty == 6 && distinct == 6 && distinctWords == 6) ? L"PASS" : L"FAIL",
        nonEmpty, distinct, distinctWords));

    // 6. Bubble names. Every bubble is named; every name carries its time; every
    //    outgoing name carries its delivery WORD (so state is never colour-only);
    //    the failed one carries its reason; and in a group every incoming name
    //    carries a sender EVEN ON A CONTINUATION, where the bubble draws none.
    int named = 0, empty = 0, missingTime = 0, outNamed = 0, missingWord = 0;
    int groupIncoming = 0, missingSender = 0, failedRows = 0, withReason = 0;
    for (auto const& c : world.conversations) {
      const bool group = (c.kind == demo::ConversationKind::Group);
      for (auto const& row : c.rows) {
        if (row.kind != demo::RowKind::Message) continue;
        const std::wstring n = views::BubbleAutomationName(row, group);
        ++named;
        if (n.empty()) ++empty;
        if (!row.timeLabel.empty() && n.find(row.timeLabel) == std::wstring::npos)
          ++missingTime;
        if (row.outgoing) {
          ++outNamed;
          if (n.find(views::DeliveryWord(row.state)) == std::wstring::npos) ++missingWord;
          if (row.state == demo::DeliveryState::Failed) {
            ++failedRows;
            if (!row.failureReason.empty() &&
                n.find(row.failureReason) != std::wstring::npos) ++withReason;
          }
        } else if (group) {
          ++groupIncoming;
          if (!row.inspect.senderDisplayName.empty() &&
              n.find(row.inspect.senderDisplayName) == std::wstring::npos) ++missingSender;
        }
      }
    }
    const bool namesOk = named > 0 && empty == 0 && missingTime == 0 && outNamed > 0 &&
                         missingWord == 0 && groupIncoming > 0 && missingSender == 0 &&
                         failedRows > 0 && withReason == failedRows;
    lines.push_back(std::format(
        L"  thread T2 names  : {} - {} bubbles named ({} empty, {} missing time); "
        L"{} outgoing, {} missing a delivery word; {} group incoming, {} missing a "
        L"sender; {}/{} failed carry a reason",
        namesOk ? L"PASS" : L"FAIL", named, empty, missingTime, outNamed, missingWord,
        groupIncoming, missingSender, withReason, failedRows));
  ```

- [ ] **Step 7: build.**

  ```
  powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
  ```

  Expected: `0 Error(s)`. `MakeBubbleRow` is not called yet — the column task calls it — so this proves the style key resolves at compile time, the two new files are in the project, and `MakeIdenticon`'s signature matches.

- [ ] **Step 8: run `--diagnose` and read the two new lines** (Bash, for the reason T1 Step 5 gives):

  ```
  cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows && ./app/build/x64/Release/URmessage.exe --diagnose > .verify/diag.txt 2>&1 ; cat .verify/diag.txt
  ```

  Expected, alongside T1's four still-`PASS` lines:

  ```
    thread T2 glyphs : PASS - 6 states, 6 non-empty, 6 distinct glyphs, 6 distinct words
    thread T2 names  : PASS - 42 bubbles named (0 empty, 0 missing time); 14 outgoing, 0 missing a delivery word; 12 group incoming, 0 missing a sender; 1/1 failed carry a reason
  ```

  The totals follow F2's world; what must hold is both lines `PASS`, `6 non-empty / 6 distinct / 6 distinct`, and every "missing" count 0 with its population count above 0. **`0 outgoing` or `0 group incoming` is a vacuous pass — treat it as FAIL.** The glyphs themselves are only *codepoints* here; that they are real shapes rather than tofu boxes is proved by the column task's screenshot.

---

## Task T3: The thread column: ScrollViewer + StackPanel, day and system rows, MakeThread / SetThreadConversation, mounted and screenshotted

**Files:**

Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Views/ThreadView.cpp; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h; Modify C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: T1 — `ShowsSenderHeader`, `CarriesDeliveryGlyph`, `DaySeparatorLabel`, `BubbleMaxWidthDip`. T2 — `Views/ThreadView.h` (the contract §4 block, `BubbleRow`, `MakeBubbleRow`, `kThreadGutterDip`), `UrBubbleButtonStyle`. F1 — `verify-render.ps1 -AppArgs`. F2 — `GetWorld()`, `Conversation`, `MessageRow`. F4 — `Demo/DemoSwitches.h`: `urmsg::demo::ParseDemoOptions()` and `DemoOptions::enabled`. F7 — the 1560x900 `--demo` window size. Existing MainWindow: the `ThreadBody` Grid (`MainWindow.xaml`, row 1 of `ThreadPane`) and `ThreadPaneTitle`.
- Produces: Defined in `app/src/App/Views/ThreadView.cpp` — the fixed contract §4 signatures, verbatim:
`ThreadView urmsg::views::MakeThread(std::function<void(std::wstring)> onSelectMessage, std::function<void()> onDeselect);`
`void urmsg::views::SetThreadConversation(ThreadView& v, urmsg::demo::Conversation const& c);`
File-local in the same unit: `struct ThreadParts`, `Registry()`, `Find(FrameworkElement const&)`, `MakeDaySeparator(std::wstring const&)`, `MakeSystemRow(urmsg::demo::MessageRow const&)`, `ApplyColumnWidth(std::shared_ptr<ThreadParts> const&)`.
On `implementation::MainWindow` (`MainWindow.xaml.h`, private): `void BuildThread();` and member `urmsg::views::ThreadView thread_;`

The column the bubbles live in, and the first time anyone looks at this surface.

**Why a parts registry.** `MakeThread` returns `ThreadView` **by value**, so a `SizeChanged` lambda capturing `&v` would dangle the moment the caller copies or moves it. The unit therefore keeps its live state in a `shared_ptr<ThreadParts>` held in a file-local registry keyed on `v.root`, and every setter looks its parts up from the element. This is the same shape `MakeNetworkPage` uses in the Network task; do not invent a second mechanism.

**What this task deliberately does not build:** `SetThreadSelectedMessage`, `SetThreadTyping`, `AppendThreadRow` (declared by T2, defined by later tasks), the composer, and the delivery-state morph. It does render **every** row kind, including `System`, so no row of the world is silently dropped — the fuller key-change treatment of a `permanentRecord` row is a later task's, and this one gives it a lock glyph and an edge so it is already distinguishable.

- [ ] **Step 1: confirm the mount point's `x:Name`.**

  ```
  grep -n 'x:Name="Thread' app/src/App/MainWindow.xaml
  ```

  Expected today: `ThreadPane`, `ThreadPaneTitle`, `ThreadBody`. If the shell task has landed first and renamed the body host to `ThreadHost`, that grep says so — use whichever name it prints in Step 4, changing only the two `ThreadBody()` call sites. Nothing else in this task depends on the name.

- [ ] **Step 2: add the column, the row species and the two contract functions to `app/src/App/Views/ThreadView.cpp`.** Add `#include <map>` and `#include <memory>` to the includes, then append this **after** `MakeBubbleRow` and before the closing `}  // namespace urmsg::views`:

  ```cpp
  namespace {

  // The thread pane's inset. The bubble cap is 68% of the CONTENT width, so the
  // padding comes off before BubbleMaxWidthDip sees it.
  constexpr double kThreadPadDip = 16.0;

  // Live state for one built thread. MakeThread returns ThreadView BY VALUE, so
  // a SizeChanged lambda capturing &v would dangle on the first copy; the parts
  // live here in a shared_ptr instead, and every setter finds them from v.root.
  struct ThreadParts {
    ScrollViewer scroller{nullptr};
    StackPanel stack{nullptr};
    std::function<void(std::wstring)> onSelect;
    std::function<void()> onDeselect;
    std::vector<Button> bubbles;   // for the width walk; ThreadView owns the public list
    double columnWidth = 0.0;
  };

  std::map<void const*, std::shared_ptr<ThreadParts>>& Registry() {
    static std::map<void const*, std::shared_ptr<ThreadParts>> map;
    return map;
  }

  std::shared_ptr<ThreadParts> Find(FrameworkElement const& root) {
    if (!root) return nullptr;
    auto it = Registry().find(winrt::get_abi(root));
    return it == Registry().end() ? nullptr : it->second;
  }

  void ApplyColumnWidth(std::shared_ptr<ThreadParts> const& parts) {
    if (!parts) return;
    const double cap = BubbleMaxWidthDip(parts->columnWidth - kThreadPadDip * 2.0);
    for (auto const& b : parts->bubbles)
      if (b) b.MaxWidth(cap);
  }

  // The day separator: a centred pill, not a rule with text on it. 11px
  // letterspaced muted is UrGroupHeaderTextStyle — the same voice every group
  // header in the app already speaks, so the thread does not grow a caption
  // species of its own.
  FrameworkElement MakeDaySeparator(std::wstring const& label) {
    Border pill;
    pill.Background(BrushByKey(L"UrCardBrush", urnw::colors::kCard));
    pill.BorderBrush(BrushByKey(L"UrBorderBrush", urnw::colors::kBorder));
    pill.BorderThickness(ThicknessHelper::FromUniformLength(1));
    pill.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
    pill.Padding(ThicknessHelper::FromLengths(10, 2, 10, 3));
    pill.HorizontalAlignment(HorizontalAlignment::Center);
    pill.Margin(ThicknessHelper::FromLengths(0, 14, 0, 6));

    TextBlock text;
    text.Text(winrt::hstring{label});
    if (auto s = StyleByKey(L"UrGroupHeaderTextStyle")) text.Style(s);
    text.Foreground(urnw::colors::MutedBrush());
    pill.Child(text);
    Automation::AutomationProperties::SetName(pill, winrt::hstring{label});
    return pill;
  }

  // A system row: centred, muted, and NOT a bubble — it did not come from a
  // person. A permanent record (the key-change line, Spec C §7.4) additionally
  // carries a lock glyph and an edge, so "this one cannot be dismissed" is a
  // shape and not a shade.
  FrameworkElement MakeSystemRow(demo::MessageRow const& row) {
    StackPanel line;
    line.Orientation(Orientation::Horizontal);
    line.Spacing(6);
    line.HorizontalAlignment(HorizontalAlignment::Center);

    if (row.permanentRecord) {
      FontIcon lock;
      lock.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
      lock.Glyph(L"");  // Lock
      lock.FontSize(12);
      lock.Foreground(urnw::colors::MutedBrush());
      lock.VerticalAlignment(VerticalAlignment::Center);
      MarkRaw(lock);
      line.Children().Append(lock);
    }

    TextBlock text;
    text.Text(winrt::hstring{row.systemText});
    if (auto s = StyleByKey(L"UrCaptionTextStyle")) text.Style(s);
    text.Foreground(urnw::colors::MutedBrush());
    text.TextWrapping(TextWrapping::Wrap);
    text.TextAlignment(TextAlignment::Center);
    text.MaxWidth(420);
    MarkRaw(text);
    line.Children().Append(text);

    Border box;
    box.Child(line);
    box.HorizontalAlignment(HorizontalAlignment::Center);
    box.Margin(ThicknessHelper::FromLengths(0, 8, 0, 8));
    if (row.permanentRecord) {
      box.BorderBrush(BrushByKey(L"UrBorderBrush", urnw::colors::kBorder));
      box.BorderThickness(ThicknessHelper::FromUniformLength(1));
      box.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
      box.Padding(ThicknessHelper::FromLengths(10, 4, 10, 4));
    }
    Automation::AutomationProperties::SetName(box, winrt::hstring{row.systemText});
    return box;
  }

  }  // namespace

  ThreadView MakeThread(std::function<void(std::wstring)> onSelectMessage,
                        std::function<void()> onDeselect) {
    ThreadView v;
    auto parts = std::make_shared<ThreadParts>();
    parts->onSelect = std::move(onSelectMessage);
    parts->onDeselect = std::move(onDeselect);

    Grid root;
    root.Background(BrushByKey(L"UrBackgroundBrush", urnw::colors::kBackground));

    ScrollViewer scroller;
    scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scroller.Padding(ThicknessHelper::FromLengths(kThreadPadDip, 8, kThreadPadDip, 12));

    StackPanel stack;
    stack.Spacing(6);
    scroller.Content(stack);
    root.Children().Append(scroller);

    parts->scroller = scroller;
    parts->stack = stack;
    v.root = root;
    v.scroller = scroller;
    v.stack = stack;
    Registry()[winrt::get_abi(v.root)] = parts;

    // The bubble cap tracks the column. One walk over <= ~45 buttons on a
    // resize is cheaper than a per-bubble binding and is the only way the 68%
    // rule can be true at more than one window width.
    scroller.SizeChanged([parts](auto const&, SizeChangedEventArgs const& e) {
      parts->columnWidth = e.NewSize().Width;
      ApplyColumnWidth(parts);
    });

    // Design §9.1: a click in empty thread space deselects. A Button handles its
    // own pointer events, so a bubble click does not reach this.
    root.Tapped([parts](auto const&, auto const&) {
      if (parts->onDeselect) parts->onDeselect();
    });
    return v;
  }

  void SetThreadConversation(ThreadView& v, demo::Conversation const& c) {
    auto parts = Find(v.root);
    if (!parts) return;

    parts->stack.Children().Clear();
    parts->bubbles.clear();
    v.bubbles.clear();

    const bool group = (c.kind == demo::ConversationKind::Group);
    for (std::size_t i = 0; i < c.rows.size(); ++i) {
      auto const& row = c.rows[i];
      demo::MessageRow const* prev = (i > 0) ? &c.rows[i - 1] : nullptr;
      demo::MessageRow const* next = (i + 1 < c.rows.size()) ? &c.rows[i + 1] : nullptr;

      switch (row.kind) {
        case demo::RowKind::DaySeparator: {
          const std::wstring label = DaySeparatorLabel(row);
          // An unlabelled separator draws NOTHING rather than an empty pill.
          if (!label.empty()) parts->stack.Children().Append(MakeDaySeparator(label));
          break;
        }
        case demo::RowKind::System:
          parts->stack.Children().Append(MakeSystemRow(row));
          break;
        case demo::RowKind::Message: {
          auto built = MakeBubbleRow(row, group, ShowsSenderHeader(prev, row, group),
                                     CarriesDeliveryGlyph(row, next));
          built.bubble.root.Click([parts, id = row.id](auto const&, auto const&) {
            if (parts->onSelect) parts->onSelect(id);
          });
          parts->bubbles.push_back(built.bubble.root);
          v.bubbles.push_back(built.bubble);
          parts->stack.Children().Append(built.root);
          break;
        }
      }
    }

    ApplyColumnWidth(parts);
    // A thread opens at its newest row. ScrollableHeight is 0 until the stack
    // has been measured, so lay out first; disableAnimation is true because
    // this is a jump to a position, not a motion the user asked for.
    parts->scroller.UpdateLayout();
    parts->scroller.ChangeView(nullptr, winrt::box_value(parts->scroller.ScrollableHeight())
                                            .try_as<winrt::Windows::Foundation::IReference<double>>(),
                              nullptr, true);
  }
  ```

  If `ChangeView`'s boxed-`IReference` form does not compile against this SDK, use the plain overload instead — `parts->scroller.ChangeView(nullptr, parts->scroller.ScrollableHeight(), nullptr, true);` — which C++/WinRT accepts because `IReference<double>` converts from `double`. Take whichever the compiler accepts and keep the `true`.

- [ ] **Step 3: declare the member and the builder in `app/src/App/MainWindow.xaml.h`.** Add `#include "Views/ThreadView.h"` beside the existing `#include "UrComponents.h"`, then in the private section after `void BuildConversationList();`:

  ```cpp
    // The demo thread. Built only under --demo: a normal launch is 480x760 and
    // must behave exactly as it does today (design D7), so the pane keeps its
    // "nothing selected" line. The conversation this opens on, and the two
    // callbacks, become the click graph's when that task wires the rail.
    void BuildThread();

    urmsg::views::ThreadView thread_{};
  ```

- [ ] **Step 4: mount it in `app/src/App/MainWindow.xaml.cpp`.** Add `#include "Demo/DemoSwitches.h"`, `#include "Demo/DemoWorld.h"` and `#include "Views/ThreadView.h"` to the includes; add `BuildThread();` immediately after the existing `BuildConversationList();` in the constructor; and add the definition after `MainWindow::BuildConversationList`:

  ```cpp
  void MainWindow::BuildThread() {
    // Gated on --demo. Without it this window is the 480x760 shell it is today,
    // where ApplyBreakpoint collapses ThreadPane below 1000 dip anyway, and
    // mounting fabricated messages into a normal launch would change the one
    // thing design D7 says not to change.
    if (!urmsg::demo::ParseDemoOptions().enabled) return;

    auto const& world = urmsg::demo::GetWorld();
    if (world.conversations.empty()) return;
    auto const& open = world.conversations.front();

    // Both callbacks are seams. The rail does not exist yet, so they log and
    // return; the click-graph task replaces them with SelectMessage /
    // ClearMessageSelection without touching this file's structure.
    thread_ = urmsg::views::MakeThread(
        [](std::wstring id) { urnw::LogInfo("thread: bubble selected {}", winrt::to_string(id)); },
        []() { urnw::LogInfo("thread: deselected"); });

    ThreadBody().Children().Clear();
    ThreadBody().Children().Append(thread_.root);
    urmsg::views::SetThreadConversation(thread_, open);
    ThreadPaneTitle().Text(winrt::hstring{open.name});
    urnw::LogInfo("thread: mounted {} rows, {} bubbles", open.rows.size(),
                  thread_.bubbles.size());
  }
  ```

- [ ] **Step 5: build.**

  ```
  powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
  ```

  Expected: `0 Error(s)`.

- [ ] **Step 6: re-run `--diagnose` and confirm nothing regressed** (Bash):

  ```
  cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows && ./app/build/x64/Release/URmessage.exe --diagnose > .verify/diag.txt 2>&1 ; cat .verify/diag.txt
  ```

  Expected: the four `thread T1` lines and the two `thread T2` lines all still `PASS`, with the same counts as before. This task adds no assertion of its own — it adds a picture.

- [ ] **Step 7: screenshot the thread and LOOK at it.**

  ```
  powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
  ```

  Then open `.verify/urmessage-window-screen.png` and read it against this list. (`--demo=thread` implies `--demo`, so the window is 1560x900 DIP and the 1000 DIP breakpoint has already opened the thread pane — this works whether or not the deep-link task has landed, because Chats is the default destination.)

  The script's own output must say `window rect : 1560x900` at scale 1.0 (or the physical equivalent at this box's DPI — the line states it is in physical pixels and prints the scale). Then, in the image:

  1. The right-hand pane is a **column of bubbles**, not the single grey "nothing selected" line. Count at least 8 of them.
  2. **Incoming bubbles hug the left edge of the thread pane; outgoing bubbles hug the right.** No bubble spans the pane.
  3. Outgoing bubbles are visibly **lighter** than incoming ones (#242424 vs #1C1C1C) **and** carry a hairline edge that incoming ones do not. Two channels, not one.
  4. **No bubble is pale yellow.** `UrAccentBrush` appears nowhere in this frame.
  5. At least one **centred pill** with a day label ("Today", "Yesterday", …), boxed and letterspaced, sitting between two groups of bubbles.
  6. At least one **centred, non-bubble system line**, and the key-change one inside a thin rounded box with a padlock in front of it.
  7. Under the last outgoing bubble of a run, a small right-aligned glyph. Somewhere in the thread, **one red glyph with the word `Failed` beside it**.
  8. **No hollow rectangle (tofu) anywhere in the delivery column or in front of the system line.** A tofu box means a codepoint in `DeliveryGlyph` or the `` lock is not in Segoe Fluent Icons — fix the codepoint, do not delete the glyph.
  9. The thread is scrolled to its **bottom**: the newest row is at the foot of the pane and there is no blank space under it.
  10. In a group conversation, the first bubble of a run has a **28px rounded identicon** to its left and the bubbles after it in the same run line up on that same left edge with an empty gutter.

  If any of 1–10 is wrong, fix it and repeat this step. Do not proceed on "it launched": the previous round of this work passed a gate that never looked at the image.

---

## Task T4: System rows and the permanent key-change record

**Files:**

app/src/App/Views/ThreadLayout.h (new), app/src/App/Views/ThreadLayout.cpp (new), app/src/App/Views/ThreadView.cpp (edit), app/src/App/App.vcxproj (edit), app/src/App/Startup.cpp (edit)

**Interfaces:**

- Consumes: urmsg::demo::GetWorld() -> World const&; urmsg::demo::{Conversation, MessageRow, RowKind, ConversationKind, DeliveryState} (CONTRACT-V2 §1); urmsg::views::ThreadView + MakeThread/SetThreadConversation (CONTRACT-V2 §4, built by T1-T3); urnw::colors::{MutedBrush, TextBrush, DangerBrush, BorderBrush, CardBrush} (UrColors.h); urnw::CollectDiagnostics() (Startup.cpp:178, anchor `return lines;` at :217)
- Produces: enum class urmsg::views::ThreadRowShape { IncomingBubble, OutgoingBubble, DaySeparator, SystemLine, SystemPermanentRecord }; struct urmsg::views::ThreadRowPlan { size_t rowIndex; ThreadRowShape shape; bool showSenderHeader; bool endsOutgoingRun; }; std::vector<urmsg::views::ThreadRowPlan> urmsg::views::PlanThreadRows(urmsg::demo::Conversation const& c);

# T4 — system rows and the permanent key-change record

`RowKind::System` currently has no rendering. This task adds the centred, muted, non-bubble line, and the Spec C §7.4 permanent key-change record, and introduces the **pure-C++ layout planner** that T5 and T6 both build on.

The planner is not decoration. `CollectDiagnostics()` runs in `wWinMain` **before `winrt::init_apartment()`** (CONTRACT-V2 §1), so the only thing `--diagnose` can assert is pure C++. Every row decision therefore lives in `ThreadLayout.{h,cpp}` and the XAML builder renders the plan 1:1.

---

- [ ] **Step 1: create the pure planner header.**

Write `app/src/App/Views/ThreadLayout.h`:

```cpp
// The thread's layout DECISIONS, as pure data.
//
// PURE C++. No winrt/ include here, and none may be added: CollectDiagnostics()
// calls PlanThreadRows() from wWinMain BEFORE winrt::init_apartment(), which is
// the same reason DemoWorld.h is pure (CONTRACT-V2 §1). Keeping the decisions
// here and the pixels in ThreadView.cpp is what makes a --diagnose assertion
// about this surface possible at all; a builder that decided inline could only
// ever be checked by looking at it.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

enum class ThreadRowShape {
  IncomingBubble,
  OutgoingBubble,
  DaySeparator,
  SystemLine,             // centred, muted, NOT a bubble (design §6.2)
  SystemPermanentRecord,  // Spec C §7.4: 2px UrDangerBrush left rule, non-dismissible
};

struct ThreadRowPlan {
  std::size_t rowIndex = 0;      // index into Conversation::rows
  ThreadRowShape shape = ThreadRowShape::SystemLine;
  bool showSenderHeader = false; // first bubble of an INCOMING run, groups only
  bool endsOutgoingRun = false;  // T5 hangs the delivery cluster under THIS row
};

// One entry per row, in order, always. Never throws.
std::vector<ThreadRowPlan> PlanThreadRows(demo::Conversation const& c);

}  // namespace urmsg::views
```

---

- [ ] **Step 2: implement the planner.**

Write `app/src/App/Views/ThreadLayout.cpp`:

```cpp
// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader> so that "pure C++, no winrt"
// is enforced by the compiler rather than by a comment at the top of a file.
#include "Views/ThreadLayout.h"

namespace urmsg::views {
namespace {

bool IsMessage(demo::MessageRow const& r) { return r.kind == demo::RowKind::Message; }

}  // namespace

std::vector<ThreadRowPlan> PlanThreadRows(demo::Conversation const& c) {
  std::vector<ThreadRowPlan> plan;
  plan.reserve(c.rows.size());

  for (std::size_t i = 0; i < c.rows.size(); ++i) {
    demo::MessageRow const& r = c.rows[i];
    ThreadRowPlan p;
    p.rowIndex = i;
    switch (r.kind) {
      case demo::RowKind::DaySeparator:
        p.shape = ThreadRowShape::DaySeparator;
        break;
      case demo::RowKind::System:
        // permanentRecord is the ONLY field in the fixed contract that
        // distinguishes the key-change record from the other system lines, so
        // it is the only thing this switches on. Classifying by matching words
        // inside systemText would break the first time the copy changed.
        p.shape = r.permanentRecord ? ThreadRowShape::SystemPermanentRecord
                                    : ThreadRowShape::SystemLine;
        break;
      case demo::RowKind::Message:
        p.shape = r.outgoing ? ThreadRowShape::OutgoingBubble
                             : ThreadRowShape::IncomingBubble;
        break;
    }
    plan.push_back(p);
  }

  // A RUN is a maximal stretch of consecutive Message rows in the same
  // direction. A day separator or a system row BREAKS it, deliberately: the
  // delivery cluster belongs under the last bubble the reader actually saw, not
  // under one that is three rows further down past a "Ana's safety number
  // changed" record.
  for (std::size_t i = 0; i < plan.size(); ++i) {
    demo::MessageRow const& r = c.rows[plan[i].rowIndex];
    if (!IsMessage(r)) continue;

    const bool startsRun = (i == 0) || !IsMessage(c.rows[plan[i - 1].rowIndex]) ||
                           c.rows[plan[i - 1].rowIndex].outgoing != r.outgoing;
    const bool endsRun = (i + 1 == plan.size()) || !IsMessage(c.rows[plan[i + 1].rowIndex]) ||
                         c.rows[plan[i + 1].rowIndex].outgoing != r.outgoing;

    // Spec C §5.2: sender name + identicon on the first bubble of a run, in
    // GROUPS only. Never in a DM (the other name is the window title) and never
    // on your own run (it is you).
    plan[i].showSenderHeader =
        startsRun && !r.outgoing && c.kind == demo::ConversationKind::Group;
    plan[i].endsOutgoingRun = endsRun && r.outgoing;
  }
  return plan;
}

}  // namespace urmsg::views
```

---

- [ ] **Step 3: register both files in the project.**

In `app/src/App/App.vcxproj`, add to the two existing `<ItemGroup>`s (the ones at :176 and :188). The `NotUsing` metadata is what keeps Step 2's comment true:

```xml
    <ClCompile Include="Views\ThreadLayout.cpp"><PrecompiledHeader>NotUsing</PrecompiledHeader></ClCompile>
```

```xml
    <ClInclude Include="Views\ThreadLayout.h" />
```

`$(MSBuildProjectDirectory)` is already on `AdditionalIncludeDirectories` (App.vcxproj:125), so `#include "Views/ThreadLayout.h"` resolves from any unit in this project.

---

- [ ] **Step 4: build the two system-row shapes.**

In `app/src/App/Views/ThreadView.cpp`, add `#include "Views/ThreadLayout.h"` and, inside the file's anonymous namespace, these two builders. `IconFont()` is the same one-liner `UrComponents.cpp:29` uses — FontIcon defaults to the older *Segoe MDL2 Assets*, whose metrics differ, so the family is always named.

```cpp
namespace xaml = winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace Automation = winrt::Microsoft::UI::Xaml::Automation;

Media::FontFamily IconFont() { return Media::FontFamily(L"Segoe Fluent Icons"); }

// design §6.2: centred, muted, NON-bubble. No fill, no border, no 68% cap.
// Nobody SAID a system line, so giving it a bubble would be the mock claiming
// a group changed its own timer.
FrameworkElement MakeSystemLine(winrt::hstring const& text) {
  TextBlock line;
  line.Text(text);
  line.FontSize(12);
  line.Foreground(urnw::colors::MutedBrush());
  line.TextWrapping(TextWrapping::Wrap);
  line.TextAlignment(xaml::TextAlignment::Center);
  line.HorizontalAlignment(HorizontalAlignment::Center);
  line.MaxWidth(420);
  line.Margin(ThicknessHelper::FromLengths(0, 10, 0, 10));
  return line;
}

// Spec C §7.4: the permanent key-change record. A 2px UrDangerBrush rule on the
// LEADING edge, a key glyph, the record's own copy, and [ Review ].
//
// Non-dismissible is rendered by CONSTRUCTION: there is no close, no X and no
// collapse on this element, and none may be added. A demo that offered one
// would be advertising a control §7.5 forbids the product to ship.
//
// The rule is a SHAPE, so the record still reads as "this one is different"
// with colour taken away.
FrameworkElement MakeKeyChangeRecord(winrt::hstring const& text) {
  Grid root;
  root.HorizontalAlignment(HorizontalAlignment::Center);
  root.MaxWidth(520);
  root.Margin(ThicknessHelper::FromLengths(0, 12, 0, 12));
  {
    ColumnDefinition ruleCol;
    ruleCol.Width(GridLengthHelper::FromPixels(2));
    ColumnDefinition bodyCol;
    bodyCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    root.ColumnDefinitions().Append(ruleCol);
    root.ColumnDefinitions().Append(bodyCol);
  }

  Border rule;
  rule.Width(2);
  rule.Background(urnw::colors::DangerBrush());
  rule.VerticalAlignment(VerticalAlignment::Stretch);
  root.Children().Append(rule);

  StackPanel column;
  column.Spacing(6);
  column.Margin(ThicknessHelper::FromLengths(12, 2, 0, 2));
  Grid::SetColumn(column, 1);

  StackPanel head;
  head.Orientation(Orientation::Horizontal);
  head.Spacing(8);

  FontIcon key;
  key.FontFamily(IconFont());
  key.Glyph(L"");  // Segoe Fluent "Permissions" — the key glyph
  key.FontSize(14);
  key.Foreground(urnw::colors::DangerBrush());
  key.VerticalAlignment(VerticalAlignment::Top);
  // decoration beside a line that already carries the words
  Automation::AutomationProperties::SetAccessibilityView(
      key, winrt::Microsoft::UI::Xaml::Automation::Peers::AccessibilityView::Raw);
  head.Children().Append(key);

  TextBlock body;
  body.Text(text);
  body.FontSize(12);
  body.TextWrapping(TextWrapping::Wrap);
  body.Foreground(urnw::colors::TextBrush());
  head.Children().Append(body);
  column.Children().Append(head);

  // design §9.1: the modal behind [ Review ] is not built (design §2), so the
  // button is DISABLED rather than live-but-dead.
  Button review;
  review.Content(winrt::box_value(winrt::hstring{L"Review"}));
  review.HorizontalAlignment(HorizontalAlignment::Left);
  review.FontSize(12);
  review.Padding(ThicknessHelper::FromLengths(10, 3, 10, 3));
  review.MinHeight(26);
  review.IsEnabled(false);
  Automation::AutomationProperties::SetName(
      review, L"Review the safety-number change (not available in the demo)");
  column.Children().Append(review);

  root.Children().Append(column);
  Automation::AutomationProperties::SetName(
      root, winrt::hstring{L"Permanent record, cannot be dismissed. "} + text);
  return root;
}
```

---

- [ ] **Step 5: route `SetThreadConversation` through the plan.**

Replace the row loop in `SetThreadConversation` so the planner, not the builder, decides shape. Keep whatever T1-T3 wrote for the bubble and day-separator branches — only the dispatch changes:

```cpp
void SetThreadConversation(ThreadView& v, demo::Conversation const& c) {
  if (!v.stack) return;
  v.stack.Children().Clear();
  v.bubbles.clear();

  // The builder renders the plan 1:1 and decides nothing. Every branch below is
  // reachable from --diagnose because PlanThreadRows() is pure.
  for (auto const& p : PlanThreadRows(c)) {
    demo::MessageRow const& r = c.rows[p.rowIndex];
    switch (p.shape) {
      case ThreadRowShape::SystemLine:
        v.stack.Children().Append(MakeSystemLine(winrt::hstring{r.systemText}));
        break;
      case ThreadRowShape::SystemPermanentRecord:
        v.stack.Children().Append(MakeKeyChangeRecord(winrt::hstring{r.systemText}));
        break;
      case ThreadRowShape::DaySeparator:
        v.stack.Children().Append(MakeDaySeparator(r));            // T1-T3
        break;
      case ThreadRowShape::IncomingBubble:
      case ThreadRowShape::OutgoingBubble: {
        ThreadBubble b = MakeBubble(c, r, p.showSenderHeader);     // T1-T3
        v.stack.Children().Append(b.root);
        v.bubbles.push_back(b);
        break;
      }
    }
  }
}
```

If T1-T3 named those two builders differently, keep their names — only the `switch` on `p.shape` and the two new `case`s are this task's.

---

- [ ] **Step 6: three `--diagnose` invariants.**

In `app/src/App/Startup.cpp`, add `#include "Views/ThreadLayout.h"` and insert this block immediately **before** `return lines;` (:217). Each line carries counts, so a stub that returns an empty plan fails visibly rather than passing vacuously:

```cpp
  // ---- T4: the thread layout planner -------------------------------------
  {
    std::size_t convs = 0, rows = 0, planned = 0, system = 0, plain = 0,
                permanent = 0, emptyText = 0;
    for (auto const& c : urmsg::demo::GetWorld().conversations) {
      ++convs;
      rows += c.rows.size();
      for (auto const& r : c.rows)
        if (r.kind == urmsg::demo::RowKind::System) ++system;
      for (auto const& p : urmsg::views::PlanThreadRows(c)) {
        ++planned;
        if (p.shape == urmsg::views::ThreadRowShape::SystemLine) ++plain;
        if (p.shape == urmsg::views::ThreadRowShape::SystemPermanentRecord) {
          ++permanent;
          if (c.rows[p.rowIndex].systemText.empty()) ++emptyText;
        }
      }
    }
    lines.push_back(std::format(
        L"  T4 plan covers rows  : {} — planned {} of {} rows over {} conversations",
        (planned == rows && 0 < rows) ? L"PASS" : L"FAIL", planned, rows, convs));
    lines.push_back(std::format(
        L"  T4 system rows       : {} — {} system rows -> {} plain + {} permanent",
        (plain + permanent == system && 1 <= permanent) ? L"PASS" : L"FAIL",
        system, plain, permanent));
    lines.push_back(std::format(
        L"  T4 permanent record  : {} — {} record(s), {} with empty systemText",
        (1 <= permanent && emptyText == 0) ? L"PASS" : L"FAIL", permanent, emptyText));
  }
```

---

- [ ] **Step 7: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

Expected: the build completes and reports `0 Error(s)`.

---

- [ ] **Step 8: run the invariants.**

```
app/build/x64/Release/URmessage.exe --diagnose
```

Expected — three lines, all `PASS`, with non-zero counts:

```
  T4 plan covers rows  : PASS — planned 47 of 47 rows over 8 conversations
  T4 system rows       : PASS — 3 system rows -> 2 plain + 1 permanent
  T4 permanent record  : PASS — 1 record(s), 0 with empty systemText
```

The exact totals are DemoWorld's; what must hold is `planned == rows`, `plain + permanent == system`, `permanent >= 1`, `emptyText == 0`. A zero in the `system` slot is a FAIL to chase in DemoWorld, not a pass.

---

- [ ] **Step 9: look at the thread.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

Open `.verify/urmessage-window-screen.png`. The window is **1560×900** (design D7). Concretely expect, in the thread column:

1. At least one **centred** grey line of ~12px text that touches neither the left nor the right edge of the column and sits on the page fill — no card, no border, no rounded box around it.
2. Exactly one row with a **2px vertical red rule** (#F8523B) down its left edge, a small red key glyph, wrapped white text, and a greyed-out `Review` button under it.
3. **No X, close or dismiss control anywhere on that record.**
4. No glyph anywhere in the column renders as a `□` replacement box.

If (1) reads as a bubble, the `SystemLine` branch fell through to the bubble builder. If (2) has no red rule, `permanentRecord` is not reaching `MakeKeyChangeRecord`.

---

## Task T5: Delivery glyphs and bubble selection

**Files:**

app/src/App/Views/ThreadLayout.h (edit), app/src/App/Views/ThreadLayout.cpp (edit), app/src/App/Views/ThreadView.cpp (edit), app/src/App/Startup.cpp (edit)

**Interfaces:**

- Consumes: urmsg::views::{ThreadRowPlan, ThreadRowShape, PlanThreadRows} (T4); urmsg::views::{ThreadView, ThreadBubble} + MakeThread(std::function<void(std::wstring)>, std::function<void()>) / SetThreadSelectedMessage (CONTRACT-V2 §4); urmsg::demo::{DeliveryState, MessageRow}; urnw::colors::{MutedBrush, TextBrush, DangerBrush, BorderBrush, AccentBrush}
- Produces: enum class urmsg::views::DeliveryCue { Clock, OneCheck, TwoOutlineChecks, TwoFilledChecks, Alert, Timer }; struct urmsg::views::DeliveryBadge { DeliveryCue cue; wchar_t const* glyph; int repeat; wchar_t const* word; bool danger; bool solid; }; urmsg::views::DeliveryBadge urmsg::views::BadgeFor(urmsg::demo::DeliveryState s); int urmsg::views::SelectedBubbleIndex(std::vector<std::wstring> const& ids, std::wstring const& id); void urmsg::views::SetThreadSelectedMessage(ThreadView& v, std::wstring const& id)

# T5 — delivery glyphs and selection

Two halves. The delivery cluster hangs under the row T4's planner marked `endsOutgoingRun`. Selection paints the bubble T1-T3 built.

**Glyph provenance.** Every codepoint below was read out of `C:\Windows\Fonts\SegoeIcons.ttf` with fontTools and rendered at 13 px before it was written down. `E930` (an outline ring around a check) and `EC61` (a solid disc with a knocked-out check) are the font's **only** true outline/filled check pair — bare check marks `E10B`, `E001`, `E0E7`, `E73E` and `E8FB` are indistinguishable from one another at 13 px, so "outline vs filled" cannot be carried by choosing between them. Two rings and two discs, at their natural 13 px advance, read cleanly as two.

**State is carried on three channels, never on colour.** The glyph COUNT (one vs two), the glyph SHAPE (ring vs disc), and the WORD beside it.

---

- [ ] **Step 1: check the two assumptions this task rests on.**

Read `app/src/App/Views/ThreadView.cpp` and confirm both, then fix whichever is false before going on:

1. Bubbles set `HorizontalAlignment::Right` when outgoing and `Left` when incoming (Spec C §5.2). `SetThreadSelectedMessage` reads the alignment back to know what edge to restore, because `ThreadBubble` is fixed by CONTRACT-V2 §4 and cannot grow a field.
2. The bubble `Button`'s Style template-binds `BorderBrush` and `BorderThickness` onto its template root. `UrCardButtonStyle` (App.xaml:632) and `UrCardRowButtonStyle` (:709) both do; a style that does not must be swapped for one that does, or the selection edge never paints.

Also find where `MakeThread` keeps its two callbacks. If it does not keep them, add this to the file's anonymous namespace — there is exactly one thread view in this window:

```cpp
// The two callbacks and the parts Set* functions have to reach again.
// CONTRACT-V2 §4 fixes every signature, so there is no parameter to thread them
// through; MainWindow builds exactly one ThreadView and holds it for the
// window's life. If a second one is ever built, THIS is the line that changes.
struct ThreadParts {
  std::function<void(std::wstring)> onSelectMessage;
  std::function<void()> onDeselect;
};
ThreadParts& Parts() {
  static ThreadParts parts;
  return parts;
}
```

---

- [ ] **Step 2: add the badge table and the selection helper to the pure header.**

Append to `app/src/App/Views/ThreadLayout.h`, inside `namespace urmsg::views`:

```cpp
#include <string>

enum class DeliveryCue { Clock, OneCheck, TwoOutlineChecks, TwoFilledChecks, Alert, Timer };

struct DeliveryBadge {
  DeliveryCue cue;
  wchar_t const* glyph;  // ONE Segoe Fluent codepoint, never empty
  int repeat;            // 1 or 2 — "two checks" (Spec C §5.3) is a COUNT
  wchar_t const* word;   // the non-colour channel: the state says itself
  bool danger;           // UrDangerBrush
  bool solid;            // UrTextBrush rather than UrTextMutedBrush
};

// Total over the closed set of Spec C §5.3. Pure, so --diagnose can walk it.
DeliveryBadge BadgeFor(demo::DeliveryState s);

// -1 when `id` is empty or matches nothing. Deselection is a real state and
// must not fall through to bubble 0 — which is what an index of 0 for "no
// match" would do, and it would be invisible in every screenshot where bubble 0
// happened to be the selected one.
int SelectedBubbleIndex(std::vector<std::wstring> const& ids, std::wstring const& id);
```

---

- [ ] **Step 3: implement both.**

Append to `app/src/App/Views/ThreadLayout.cpp`, inside `namespace urmsg::views`:

```cpp
DeliveryBadge BadgeFor(demo::DeliveryState s) {
  // Codepoints verified against C:\Windows\Fonts\SegoeIcons.ttf and rendered at
  // 13px on #101010 before being written here. E930/EC61 are the font's only
  // outline/filled check pair; bare checks (E10B, E001, E0E7, E73E, E8FB) are
  // indistinguishable from each other at this size.
  switch (s) {
    case demo::DeliveryState::Pending:
      return {DeliveryCue::Clock, L"\uE121", 1, L"Sending", false, false};  // "Clock"
    case demo::DeliveryState::Sent:
      return {DeliveryCue::OneCheck, L"\uE930", 1, L"Sent", false, false};  // "Completed" — check in a ring
    case demo::DeliveryState::Delivered:
      return {DeliveryCue::TwoOutlineChecks, L"\uE930", 2, L"Delivered", false, false};  // "Completed"
    case demo::DeliveryState::Read:
      return {DeliveryCue::TwoFilledChecks, L"\uEC61", 2, L"Read", false, true};  // "CompletedSolid" — check in a disc
    case demo::DeliveryState::Failed:
      return {DeliveryCue::Alert, L"\uE783", 1, L"Not sent", true, true};  // "Error" — exclamation in a ring
    case demo::DeliveryState::Expired:
      return {DeliveryCue::Timer, L"\uE916", 1, L"Expired", false, false};  // "Timer"
  }
  return {DeliveryCue::Clock, L"\uE121", 1, L"Sending", false, false};  // "Clock"
}

int SelectedBubbleIndex(std::vector<std::wstring> const& ids, std::wstring const& id) {
  if (id.empty()) return -1;
  for (std::size_t i = 0; i < ids.size(); ++i)
    if (ids[i] == id) return static_cast<int>(i);
  return -1;
}
```

---

- [ ] **Step 4: build the delivery cluster.**

In `ThreadView.cpp`'s anonymous namespace:

```cpp
// design §6.2: right-aligned, under the LAST outgoing bubble of a run.
//
// Nothing here is carried by colour alone. Sent -> Delivered is a COUNT change
// (one ring, two rings); Delivered -> Read is a SHAPE change (rings to discs);
// and every state prints its own word, which is also what a screen reader gets
// and what survives a greyscale screenshot.
FrameworkElement MakeDeliveryCluster(demo::MessageRow const& row) {
  const DeliveryBadge badge = BadgeFor(row.state);
  Media::Brush brush = badge.danger ? urnw::colors::DangerBrush()
                                    : (badge.solid ? urnw::colors::TextBrush()
                                                   : urnw::colors::MutedBrush());

  StackPanel cluster;
  cluster.Orientation(Orientation::Horizontal);
  cluster.Spacing(4);
  cluster.HorizontalAlignment(HorizontalAlignment::Right);
  cluster.Margin(ThicknessHelper::FromLengths(0, 2, 2, 6));

  StackPanel glyphs;
  glyphs.Orientation(Orientation::Horizontal);
  // Spacing 0: a 13px Segoe Fluent icon's own advance already puts the two
  // rings ~13 DIP apart, which is where the pair reads as two. Overlapping them
  // turns it into a blot — measured at 8 DIP before this was settled.
  glyphs.Spacing(0);
  for (int i = 0; i < badge.repeat; ++i) {
    FontIcon g;
    g.FontFamily(IconFont());
    g.Glyph(badge.glyph);
    g.FontSize(13);
    g.Foreground(brush);
    Automation::AutomationProperties::SetAccessibilityView(
        g, winrt::Microsoft::UI::Xaml::Automation::Peers::AccessibilityView::Raw);
    glyphs.Children().Append(g);
  }
  cluster.Children().Append(glyphs);

  TextBlock word;
  word.Text(winrt::hstring{badge.word});
  word.FontSize(11);
  word.Foreground(brush);
  word.VerticalAlignment(VerticalAlignment::Center);
  cluster.Children().Append(word);

  if (row.state != demo::DeliveryState::Failed) return cluster;

  // Spec C §5.3: failed carries a Reason and a retry. Both render INLINE rather
  // than behind a tap — the sheet the product would open is not built (design
  // §2), and a tap target that opens nothing is exactly what design §9.1 bans.
  StackPanel column;
  column.HorizontalAlignment(HorizontalAlignment::Right);
  column.Spacing(3);
  column.Margin(ThicknessHelper::FromLengths(0, 0, 2, 6));
  cluster.Margin(ThicknessHelper::FromUniformLength(0));
  column.Children().Append(cluster);

  TextBlock reason;
  reason.Text(winrt::hstring{row.failureReason});
  reason.FontSize(11);
  reason.Foreground(urnw::colors::DangerBrush());
  reason.TextWrapping(TextWrapping::Wrap);
  reason.TextAlignment(xaml::TextAlignment::Right);
  reason.MaxWidth(320);
  column.Children().Append(reason);

  // Retry would have to mutate the world, and CONTRACT-V2 §1 gives ambient
  // activity the only key to MutableWorld(). So it cannot act, and per design
  // §9.1 it is disabled rather than live-but-dead. The composer's one honest
  // line (T6) is where the demo says why, once, instead of five times.
  Button retry;
  retry.Content(winrt::box_value(winrt::hstring{L"Try again"}));
  retry.HorizontalAlignment(HorizontalAlignment::Right);
  retry.FontSize(11);
  retry.Padding(ThicknessHelper::FromLengths(10, 2, 10, 2));
  retry.MinHeight(24);
  retry.IsEnabled(false);
  Automation::AutomationProperties::SetName(
      retry, L"Try again: resend this message (not available in the demo)");
  column.Children().Append(retry);
  return column;
}
```

Then, in `SetThreadConversation`'s bubble branch (T4 Step 5), append the cluster after the bubble when the plan says so:

```cpp
      case ThreadRowShape::IncomingBubble:
      case ThreadRowShape::OutgoingBubble: {
        ThreadBubble b = MakeBubble(c, r, p.showSenderHeader);
        v.stack.Children().Append(b.root);
        v.bubbles.push_back(b);
        if (p.endsOutgoingRun) v.stack.Children().Append(MakeDeliveryCluster(r));
        break;
      }
```

---

- [ ] **Step 5: wire selection on the bubble and deselection on the column.**

In `MakeBubble` (or wherever T1-T3 create the `Button`), add:

```cpp
  b.root.Click([id = r.id](auto const&, auto const&) {
    if (Parts().onSelectMessage) Parts().onSelectMessage(id);
  });
  // Belt and braces: ButtonBase already marks the pointer events handled, so a
  // click on a bubble does not generate a Tapped for the background below it.
  // Saying so explicitly means the deselect handler cannot start firing on
  // bubble clicks because of a style change three months from now.
  b.root.Tapped([](auto const&, auto const& args) { args.Handled(true); });
```

In `MakeThread`, store the callbacks and give the column a background that can be clicked:

```cpp
ThreadView MakeThread(std::function<void(std::wstring)> onSelectMessage,
                      std::function<void()> onDeselect) {
  Parts().onSelectMessage = std::move(onSelectMessage);
  Parts().onDeselect = std::move(onDeselect);
  ...
  // The scroller's content is a Grid holding `stack`. A Transparent background
  // is what makes the EMPTY part of the column hit-testable at all — a null
  // Background is not hit-tested, so with one the deselect click would only
  // ever land in the 1px gaps between bubbles.
  Grid surface;
  surface.Background(urnw::colors::MakeBrush(
      urnw::colors::WithAlpha(urnw::colors::kBackground, 0)));
  surface.Children().Append(v.stack);
  surface.Tapped([](auto const&, auto const& args) {
    args.Handled(true);
    if (Parts().onDeselect) Parts().onDeselect();
  });
  v.scroller.Content(surface);
  ...
}
```

---

- [ ] **Step 6: implement `SetThreadSelectedMessage`.**

```cpp
void SetThreadSelectedMessage(ThreadView& v, std::wstring const& id) {
  std::vector<std::wstring> ids;
  ids.reserve(v.bubbles.size());
  for (auto const& b : v.bubbles) ids.push_back(b.id);
  const int selected = SelectedBubbleIndex(ids, id);

  for (std::size_t i = 0; i < v.bubbles.size(); ++i) {
    auto const& b = v.bubbles[i];
    if (!b.root) continue;
    const bool on = (static_cast<int>(i) == selected);
    // Spec C §5.2 gives the RESTING edge: 1px UrBorderBrush on an outgoing
    // bubble, none on an incoming one. This function owns the edge outright
    // rather than remembering what it was, so there is nothing to get stale;
    // direction comes from the alignment, which §5.2 also fixes.
    const bool outgoing = (b.root.HorizontalAlignment() == HorizontalAlignment::Right);

    // Three channels, the same rule SetPaneListRowSelected already follows
    // (UrComponents.h): the accent EDGE, a 1px -> 2px thickness (a SHAPE change,
    // so selection survives colour being removed), and the automation name.
    // UrAccentBrush is the selection outline and the send button, and nothing
    // else on this surface — never a bubble fill.
    b.root.BorderThickness(ThicknessHelper::FromUniformLength(on ? 2.0 : (outgoing ? 1.0 : 0.0)));
    b.root.BorderBrush(on ? urnw::colors::AccentBrush()
                          : (outgoing ? urnw::colors::BorderBrush() : nullptr));

    auto name = Automation::AutomationProperties::GetName(b.root);
    std::wstring base{name};
    const std::wstring mark = L", selected";
    if (base.size() >= mark.size() && base.compare(base.size() - mark.size(), mark.size(), mark) == 0)
      base.erase(base.size() - mark.size());
    Automation::AutomationProperties::SetName(b.root, winrt::hstring{on ? base + mark : base});
  }
}
```

Call `SetThreadSelectedMessage(v, L"")` at the end of `SetThreadConversation`, so a freshly built thread's resting edges are written by the one function that owns them.

---

- [ ] **Step 7: three `--diagnose` invariants.**

Insert before `return lines;` in `Startup.cpp`, after T4's block:

```cpp
  // ---- T5: delivery cues and selection ------------------------------------
  {
    std::size_t outgoing = 0, marks = 0, convsWithOutgoing = 0, convsWithMark = 0;
    for (auto const& c : urmsg::demo::GetWorld().conversations) {
      bool anyOut = false, anyMark = false;
      for (auto const& p : urmsg::views::PlanThreadRows(c)) {
        if (c.rows[p.rowIndex].kind == urmsg::demo::RowKind::Message &&
            c.rows[p.rowIndex].outgoing) { ++outgoing; anyOut = true; }
        if (p.endsOutgoingRun) { ++marks; anyMark = true; }
      }
      if (anyOut) ++convsWithOutgoing;
      if (anyMark) ++convsWithMark;
    }
    lines.push_back(std::format(
        L"  T5 run ends          : {} — {} outgoing rows, {} end-of-run marks; "
        L"{} of {} conversations with an outgoing row carry one",
        (marks <= outgoing && 1 <= marks && convsWithMark == convsWithOutgoing) ? L"PASS" : L"FAIL",
        outgoing, marks, convsWithMark, convsWithOutgoing));

    const urmsg::demo::DeliveryState kStates[] = {
        urmsg::demo::DeliveryState::Pending,   urmsg::demo::DeliveryState::Sent,
        urmsg::demo::DeliveryState::Delivered, urmsg::demo::DeliveryState::Read,
        urmsg::demo::DeliveryState::Failed,    urmsg::demo::DeliveryState::Expired};
    std::vector<std::wstring> keys;
    int danger = 0, emptyGlyph = 0;
    for (auto s : kStates) {
      const auto b = urmsg::views::BadgeFor(s);
      if (b.glyph == nullptr || *b.glyph == L'\0') ++emptyGlyph;
      if (b.danger) ++danger;
      keys.push_back(std::format(L"{}x{}|{}", b.glyph ? b.glyph : L"", b.repeat, b.word));
    }
    std::sort(keys.begin(), keys.end());
    const std::size_t distinct = std::size_t(std::unique(keys.begin(), keys.end()) - keys.begin());
    lines.push_back(std::format(
        L"  T5 delivery badges   : {} — 6 states -> {} distinct (glyph,count,word); "
        L"{} danger, {} empty glyph(s)",
        (distinct == 6 && danger == 1 && emptyGlyph == 0) ? L"PASS" : L"FAIL",
        distinct, danger, emptyGlyph));

    const std::vector<std::wstring> ids{L"m-1", L"m-2", L"m-3"};
    int hits = 0;
    for (int i = 0; i < 3; ++i)
      if (urmsg::views::SelectedBubbleIndex(ids, ids[std::size_t(i)]) == i) ++hits;
    const bool deselects = urmsg::views::SelectedBubbleIndex(ids, L"") == -1 &&
                           urmsg::views::SelectedBubbleIndex(ids, L"nope") == -1;
    lines.push_back(std::format(
        L"  T5 selection index   : {} — {} of 3 ids resolve to their own index; "
        L"empty and unknown -> {}",
        (hits == 3 && deselects) ? L"PASS" : L"FAIL", hits, deselects ? L"-1" : L"NOT -1"));
  }
```

Add `#include <algorithm>` to `Startup.cpp` for `sort`/`unique`.

---

- [ ] **Step 8: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

Expected: `0 Error(s)`.

---

- [ ] **Step 9: run the invariants.**

```
app/build/x64/Release/URmessage.exe --diagnose
```

Expected — three `PASS` lines with non-zero counts, e.g.:

```
  T5 run ends          : PASS — 14 outgoing rows, 5 end-of-run marks; 2 of 2 conversations with an outgoing row carry one
  T5 delivery badges   : PASS — 6 states -> 6 distinct (glyph,count,word); 1 danger, 0 empty glyph(s)
  T5 selection index   : PASS — 3 of 3 ids resolve to their own index; empty and unknown -> -1
```

`6 distinct` is the load-bearing number: a table that mapped two states to the same glyph+count+word would report 5 and FAIL.

---

- [ ] **Step 10: look at the delivery column.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

Open `.verify/urmessage-window-screen.png` at **1560×900**. Concretely expect:

1. Under the **last** bubble of each outgoing run, and under no other bubble, a right-aligned pair: glyph(s) then a small word.
2. Somewhere in the thread, all four normal cues are visible and tell apart at 100%: a **clock + "Sending"**, **one ring-check + "Sent"**, **two ring-checks + "Delivered"**, **two solid-disc checks + "Read"**.
3. Exactly one **red** `(!)` ring with the word **"Not sent"**, a red reason line under it, and a greyed `Try again` button.
4. **No bubble is pale yellow.** `UrAccentBrush` #EFF7BB appears on no bubble fill anywhere in the image.
5. No `□` replacement box in the delivery column — every codepoint resolved.

If the two ring-checks read as one blot, the glyph panel picked up a negative margin; `Spacing(0)` and the icon's natural 13 DIP advance is the measured setting.

---

## Task T6: Composer, typing indicator and bubble entrance motion

**Files:**

app/src/App/Views/ThreadLayout.h (edit), app/src/App/Views/ThreadLayout.cpp (edit), app/src/App/Views/ThreadView.cpp (edit), app/src/App/Startup.cpp (edit)

**Interfaces:**

- Consumes: urmsg::views::{ThreadView, ThreadBubble, PlanThreadRows, BadgeFor} (T4, T5); ThreadParts()/Parts() (T5 Step 1); urnw::motion::{ShouldAnimate, MakeSplineDouble, Ms, kBaseMs, kFastMs, kMicroMs, kPulseMs, kStandardP1, kStandardP2, kExitP1, kExitP2} (UrMotion.h); urmsg::demo::{ParseDemoOptions, DemoOptions, DemoScreen} (CONTRACT-V2 §2); winrt::Microsoft::UI::Xaml::Media::CompositeTransform; winrt::Microsoft::UI::Xaml::Media::Animation::RepeatBehaviorHelper::Forever()
- Produces: inline constexpr double urmsg::views::kBubbleRiseDip = 10.0; inline constexpr double urmsg::views::kBubbleFromScale = 0.96; inline constexpr int urmsg::views::kTypingDots = 3; inline constexpr int64_t urmsg::views::kTypingPhaseMs = 140; int64_t urmsg::views::TypingDotPhaseMs(int dot); int urmsg::views::EntranceTimelineCount(bool animate); int urmsg::views::TypingTimelineCount(bool animate); void urmsg::views::SetThreadTyping(ThreadView& v, bool typing); void urmsg::views::AppendThreadRow(ThreadView& v, urmsg::demo::MessageRow const& row)

# T6 — composer, typing indicator, entrance motion

Three things that all sit on the same rule: **every animation is gated on `urnw::motion::ShouldAnimate()`, and "off" means the motion is gone, not shortened** (design §7). The reduce-motion path must leave a correct, instantly-final UI — which is also why the typing indicator carries a word and not only three dots.

No new duration or curve token: everything below spends `UrMotion.h`'s. The one bare number is the design table's own **140 ms phase offset**, which is an offset between three copies of one timeline, not a seventh duration.

---

- [ ] **Step 1: add the motion constants and their two gate functions to the pure header.**

Append to `app/src/App/Views/ThreadLayout.h`, inside `namespace urmsg::views`:

```cpp
#include <cstdint>

// design §7, "Bubble entrance": fade + 10 DIP rise + 0.96 -> 1.0 scale at
// motion::kBaseMs on the standard curve. The numbers live here rather than in
// the builder so --diagnose can read them without an apartment.
inline constexpr double kBubbleRiseDip = 10.0;
inline constexpr double kBubbleFromScale = 0.96;

// design §7, "Typing indicator": three dots at kPulseMs, 140 ms phase offset.
// 140 is an OFFSET between three copies of one timeline, not a new duration.
inline constexpr int kTypingDots = 3;
inline constexpr int64_t kTypingPhaseMs = 140;

// 0, 140, 280. -1 for a dot outside [0, kTypingDots).
int64_t TypingDotPhaseMs(int dot);

// How many timelines each effect starts. FOUR for an entrance (opacity,
// TranslateY, ScaleX, ScaleY) and THREE for the typing wave when motion is on —
// and ZERO for both when it is off. The zero is the half worth asserting: a
// gate that returns the same count either way is not a gate.
int EntranceTimelineCount(bool animate);
int TypingTimelineCount(bool animate);
```

---

- [ ] **Step 2: implement them.**

Append to `app/src/App/Views/ThreadLayout.cpp`, inside `namespace urmsg::views`:

```cpp
int64_t TypingDotPhaseMs(int dot) {
  if (dot < 0 || kTypingDots <= dot) return -1;
  return kTypingPhaseMs * dot;
}

int EntranceTimelineCount(bool animate) { return animate ? 4 : 0; }
int TypingTimelineCount(bool animate) { return animate ? kTypingDots : 0; }
```

---

- [ ] **Step 3: the bubble entrance.**

In `ThreadView.cpp`'s anonymous namespace:

```cpp
namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;

// design §7: fade + 10 DIP rise + 0.96 -> 1.0 scale, kBaseMs, standard curve.
// Four timelines in one Storyboard so they finish on the same frame — two
// independent storyboards can land a frame apart, which reads as a hitch
// exactly when the bubble settles (the same reason CrossfadePageSwap shares one
// storyboard, UrMotion.cpp).
void RunBubbleEntrance(FrameworkElement const& el) {
  if (!el) return;
  if (!urnw::motion::ShouldAnimate()) {
    // Motion GONE, not reduced: the final pose, immediately, and no transform
    // left on the element for a later layout pass to trip over.
    el.Opacity(1.0);
    el.RenderTransform(nullptr);
    return;
  }

  Media::CompositeTransform t;
  t.TranslateY(kBubbleRiseDip);
  t.ScaleX(kBubbleFromScale);
  t.ScaleY(kBubbleFromScale);
  el.RenderTransform(t);
  // The bubble grows from where it will end up, not from its own middle.
  el.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 1.0f});
  el.Opacity(0.0);

  anim::Storyboard sb;
  auto add = [&sb, &el](anim::DoubleAnimationUsingKeyFrames const& a, wchar_t const* path) {
    anim::Storyboard::SetTarget(a, el);
    anim::Storyboard::SetTargetProperty(a, path);
    sb.Children().Append(a);
  };
  using urnw::motion::kBaseMs;
  using urnw::motion::kStandardP1;
  using urnw::motion::kStandardP2;
  using urnw::motion::MakeSplineDouble;
  add(MakeSplineDouble(0.0, 1.0, kBaseMs, 0, kStandardP1, kStandardP2), L"Opacity");
  add(MakeSplineDouble(kBubbleRiseDip, 0.0, kBaseMs, 0, kStandardP1, kStandardP2),
      L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)");
  add(MakeSplineDouble(kBubbleFromScale, 1.0, kBaseMs, 0, kStandardP1, kStandardP2),
      L"(UIElement.RenderTransform).(CompositeTransform.ScaleX)");
  add(MakeSplineDouble(kBubbleFromScale, 1.0, kBaseMs, 0, kStandardP1, kStandardP2),
      L"(UIElement.RenderTransform).(CompositeTransform.ScaleY)");
  sb.Begin();
}
```

---

- [ ] **Step 4: the typing indicator.**

Add to `ThreadParts` (T5 Step 1):

```cpp
  FrameworkElement typingRow{nullptr};
  std::vector<winrt::Microsoft::UI::Xaml::Shapes::Ellipse> typingDots;
  winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard typingStory{nullptr};
```

Builder, in the anonymous namespace:

```cpp
FrameworkElement MakeTypingIndicator() {
  StackPanel row;
  row.Orientation(Orientation::Horizontal);
  row.Spacing(6);
  row.VerticalAlignment(VerticalAlignment::Center);
  row.Margin(ThicknessHelper::FromLengths(20, 0, 0, 6));
  row.Visibility(Visibility::Collapsed);

  StackPanel dots;
  dots.Orientation(Orientation::Horizontal);
  dots.Spacing(5);
  dots.VerticalAlignment(VerticalAlignment::Center);
  Parts().typingDots.clear();
  for (int i = 0; i < kTypingDots; ++i) {
    winrt::Microsoft::UI::Xaml::Shapes::Ellipse dot;
    dot.Width(6);
    dot.Height(6);
    dot.Fill(urnw::colors::MutedBrush());
    dot.Opacity(0.30);
    Parts().typingDots.push_back(dot);
    dots.Children().Append(dot);
  }
  row.Children().Append(dots);

  // The word is not decoration. With "Show animations in Windows" off the dots
  // do not move at all, and three motionless grey dots say nothing; this line
  // is what carries the state in that case, and it is also the only thing a
  // screen reader gets (the dots are Raw).
  TextBlock says;
  says.Text(L"Typing…");
  says.FontSize(11);
  says.Foreground(urnw::colors::MutedBrush());
  says.VerticalAlignment(VerticalAlignment::Center);
  row.Children().Append(says);

  Parts().typingRow = row;
  return row;
}
```

```cpp
void SetThreadTyping(ThreadView& v, bool typing) {
  auto& parts = Parts();
  if (!parts.typingRow) return;
  parts.typingRow.Visibility(typing ? Visibility::Visible : Visibility::Collapsed);

  if (parts.typingStory) {
    parts.typingStory.Stop();
    parts.typingStory = nullptr;
  }
  // Full opacity when still, so the reduce-motion reading is three solid dots
  // and a word rather than three dots frozen mid-fade at 30%.
  for (auto const& d : parts.typingDots) d.Opacity(typing ? 1.0 : 0.30);
  if (!typing || !urnw::motion::ShouldAnimate()) return;

  anim::Storyboard sb;
  for (int i = 0; i < kTypingDots; ++i) {
    // Half a cycle out, AutoReverse back = kPulseMs per dot, offset by design
    // §7's 140 ms so the three read as a wave rather than one blink.
    auto a = urnw::motion::MakeSplineDouble(0.30, 1.0, urnw::motion::kPulseMs / 2,
                                            TypingDotPhaseMs(i), urnw::motion::kStandardP1,
                                            urnw::motion::kStandardP2);
    a.AutoReverse(true);
    a.RepeatBehavior(anim::RepeatBehaviorHelper::Forever());
    anim::Storyboard::SetTarget(a, parts.typingDots[std::size_t(i)]);
    anim::Storyboard::SetTargetProperty(a, L"Opacity");
    sb.Children().Append(a);
  }
  parts.typingStory = sb;
  sb.Begin();
}
```

---

- [ ] **Step 5: the composer.**

```cpp
// = App.xaml's UrBorderStrongBrush (#38FFFFFF). Written as a literal rather
// than derived from kText, which would give #38F8F8F8 — near enough to look
// right and wrong enough to be a second edge token.
constexpr winrt::Windows::UI::Color kBorderStrong{0x38, 0xFF, 0xFF, 0xFF};

// Same helper UrComponents.cpp:76 uses; that one is in an anonymous namespace,
// so it cannot be shared. Null-safe: a missing key must not throw a layout away.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

// design §9.1: a control that cannot act is DISABLED, so the platform draws it
// at 0.38 opacity and it never takes focus. An enabled-looking button that eats
// a click is the demo bug that rule exists to stop.
Button MakeInertIconButton(wchar_t const* glyph, wchar_t const* name) {
  Button b;
  FontIcon g;
  g.FontFamily(IconFont());
  g.Glyph(glyph);
  g.FontSize(16);
  b.Content(g);
  b.Background(nullptr);
  b.BorderThickness(ThicknessHelper::FromUniformLength(0));
  b.Padding(ThicknessHelper::FromUniformLength(6));
  b.MinWidth(0);
  b.IsEnabled(false);
  Automation::AutomationProperties::SetName(b, winrt::hstring{name});
  return b;
}

FrameworkElement MakeComposer() {
  Border bar;
  bar.Background(urnw::colors::CardBrush());
  bar.BorderBrush(urnw::colors::BorderBrush());
  bar.BorderThickness(ThicknessHelper::FromLengths(0, 1, 0, 0));
  bar.Padding(ThicknessHelper::FromLengths(12, 8, 12, 10));

  StackPanel column;
  column.Spacing(6);

  Grid row;
  row.ColumnSpacing(4);
  for (int i = 0; i < 5; ++i) {
    ColumnDefinition c;
    c.Width(i == 3 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                   : GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
    row.ColumnDefinitions().Append(c);
  }

  auto attach = MakeInertIconButton(L"",  // Segoe Fluent "Attach" — paperclip
                                    L"Attach a file (not available in the demo)");
  auto emoji = MakeInertIconButton(L"",   // Segoe Fluent "Emoji" — outline smiley
                                   L"Insert an emoji (not available in the demo)");
  Grid::SetColumn(attach, 0);
  Grid::SetColumn(emoji, 1);
  row.Children().Append(attach);
  row.Children().Append(emoji);

  // The disappearing-timer chip: a glyph AND its value, because a clock face
  // alone cannot say "24 hours" and this is the one control whose whole point is
  // the number on it.
  Button timer;
  {
    StackPanel chip;
    chip.Orientation(Orientation::Horizontal);
    chip.Spacing(5);
    FontIcon g;
    g.FontFamily(IconFont());
    g.Glyph(L"");  // Segoe Fluent "Timer"
    g.FontSize(14);
    chip.Children().Append(g);
    TextBlock t;
    t.Text(L"24h");
    t.FontSize(12);
    chip.Children().Append(t);
    timer.Content(chip);
  }
  timer.Background(nullptr);
  timer.BorderBrush(urnw::colors::BorderBrush());
  timer.BorderThickness(ThicknessHelper::FromUniformLength(1));
  timer.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(12));
  timer.Padding(ThicknessHelper::FromLengths(8, 3, 8, 3));
  timer.MinWidth(0);
  timer.IsEnabled(false);
  Automation::AutomationProperties::SetName(
      timer, L"Disappearing messages: 24 hours (not available in the demo)");
  Grid::SetColumn(timer, 2);
  row.Children().Append(timer);

  // The ONE live control. Focus and typing are things this demo can honestly
  // do, so they are not taken away.
  TextBox box;
  box.PlaceholderText(L"Message");
  box.AcceptsReturn(false);
  box.TextWrapping(TextWrapping::Wrap);
  box.MaxHeight(96);
  box.BorderThickness(ThicknessHelper::FromUniformLength(0));
  box.Background(nullptr);
  box.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetName(box, L"Message (the demo does not send)");
  Grid::SetColumn(box, 3);
  row.Children().Append(box);

  // UrAccentBrush's one legitimate home on this surface (the selection outline
  // is the other). AccentButtonStyle is what spends App.xaml's
  // AccentButtonBackground* keys, which are already the pale yellow.
  Button send;
  {
    FontIcon plane;
    plane.FontFamily(IconFont());
    plane.Glyph(L"");  // Segoe Fluent "Send" — outline paper plane
    plane.FontSize(16);
    send.Content(plane);
  }
  if (auto s = StyleByKey(L"AccentButtonStyle")) {
    send.Style(s);
  } else {
    send.Background(urnw::colors::AccentBrush());
    send.Foreground(urnw::colors::MakeBrush(urnw::colors::kInverseText));
  }
  send.MinWidth(40);
  send.Padding(ThicknessHelper::FromLengths(10, 6, 10, 6));
  send.IsEnabled(false);  // design §9.1 — the disabled accent reads as inert
  Automation::AutomationProperties::SetName(send, L"Send (not available in the demo)");
  Grid::SetColumn(send, 4);
  row.Children().Append(send);
  column.Children().Append(row);

  // The focus channel. Not the accent — that is the send button and the
  // selection outline only — so focus lifts a 1px rule from nothing to
  // UrBorderStrongBrush, which is the same edge step UrCardButtonStyle's hover
  // state already spends.
  Border focusRule;
  focusRule.Height(1);
  focusRule.HorizontalAlignment(HorizontalAlignment::Stretch);
  focusRule.Background(urnw::colors::MakeBrush(kBorderStrong));
  focusRule.Opacity(0.0);
  column.Children().Append(focusRule);

  box.GotFocus([focusRule](auto const&, auto const&) { FadeFocusRule(focusRule, true); });
  box.LostFocus([focusRule](auto const&, auto const&) { FadeFocusRule(focusRule, false); });

  // Said ONCE, here, instead of on every inert control in the window: the
  // failed message's [ Try again ], the [ Review ] on the key-change record and
  // these four all point at the same fact.
  TextBlock note;
  note.Text(L"Demo — nothing is sent, and no message leaves this window.");
  note.FontSize(11);
  note.Foreground(urnw::colors::FaintBrush());
  column.Children().Append(note);

  bar.Child(column);
  return bar;
}
```

And the focus fade, above `MakeComposer`:

```cpp
// Entrance kFastMs on the standard curve, exit one step faster on the exit
// curve — UrMotion's own rule ("exits run one step faster than entrances"),
// no new token.
void FadeFocusRule(FrameworkElement const& rule, bool on) {
  if (!rule) return;
  if (!urnw::motion::ShouldAnimate()) {
    rule.Opacity(on ? 1.0 : 0.0);
    return;
  }
  anim::Storyboard sb;
  auto a = on ? urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kFastMs, 0,
                                               urnw::motion::kStandardP1, urnw::motion::kStandardP2)
              : urnw::motion::MakeSplineDouble(1.0, 0.0, urnw::motion::kMicroMs, 0,
                                               urnw::motion::kExitP1, urnw::motion::kExitP2);
  anim::Storyboard::SetTarget(a, rule);
  anim::Storyboard::SetTargetProperty(a, L"Opacity");
  sb.Children().Append(a);
  sb.Begin();
}
```

---

- [ ] **Step 6: put the typing row and the composer under the thread, and seed the indicator for capture.**

In `MakeThread`, make `v.root` a three-row Grid — scroller, typing row, composer:

```cpp
  Grid shell;
  for (int i = 0; i < 3; ++i) {
    RowDefinition rd;
    rd.Height(i == 0 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                     : GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
    shell.RowDefinitions().Append(rd);
  }
  Grid::SetRow(v.scroller, 0);
  shell.Children().Append(v.scroller);
  auto typingRow = MakeTypingIndicator();
  Grid::SetRow(typingRow, 1);
  shell.Children().Append(typingRow);
  auto composer = MakeComposer();
  Grid::SetRow(composer, 2);
  shell.Children().Append(composer);
  v.root = shell;

  // --demo=thread is the ONLY way an agent can reach a state without
  // synthesising input (design §9.3), and the typing indicator is otherwise
  // unreachable until the ambient loop's ~40 s timer fires. So autoplay SEEDS
  // it on at build time and the ambient loop takes it from there — a presenter
  // sees the indicator immediately instead of waiting out a minute of silence.
  // Without --demo-autoplay the indicator stays collapsed.
  {
    const auto opts = demo::ParseDemoOptions();
    if (opts.autoplay && opts.screen == demo::DemoScreen::Thread) SetThreadTyping(v, true);
  }
```

---

- [ ] **Step 7: `AppendThreadRow`.**

```cpp
// Ambient activity's one entry point (design §9.2). It gets a single row with
// no conversation and no plan around it, so the shape is decided from the row
// alone: a continuation (no sender header — an arriving message is never the
// first of its run in practice), and its own delivery cluster when it is
// outgoing, which is by definition the end of a run of one.
void AppendThreadRow(ThreadView& v, demo::MessageRow const& row) {
  if (!v.stack) return;

  FrameworkElement added{nullptr};
  switch (row.kind) {
    case demo::RowKind::DaySeparator:
      added = MakeDaySeparator(row);
      break;
    case demo::RowKind::System:
      added = row.permanentRecord ? MakeKeyChangeRecord(winrt::hstring{row.systemText})
                                  : MakeSystemLine(winrt::hstring{row.systemText});
      break;
    case demo::RowKind::Message: {
      ThreadBubble b = MakeBubbleForAppend(row);  // T1-T3's builder, no run header
      added = b.root;
      v.stack.Children().Append(b.root);
      v.bubbles.push_back(b);
      RunBubbleEntrance(b.root);
      if (row.outgoing) {
        auto cluster = MakeDeliveryCluster(row);
        v.stack.Children().Append(cluster);
        RunBubbleEntrance(cluster);
      }
      added = nullptr;  // already appended and animated above
      break;
    }
  }
  if (added) {
    v.stack.Children().Append(added);
    RunBubbleEntrance(added);
  }

  // Follow the bottom only if the reader was already there. Yanking someone
  // back down while they are reading older messages is the ambient loop
  // fighting the person driving, which design §9.2 forbids.
  if (v.scroller) {
    const double slack = v.scroller.ScrollableHeight() - v.scroller.VerticalOffset();
    if (slack < 40.0) {
      v.scroller.UpdateLayout();
      v.scroller.ChangeView(nullptr, v.scroller.ScrollableHeight(), nullptr);
    }
  }
}
```

---

- [ ] **Step 8: two `--diagnose` invariants.**

Insert before `return lines;` in `Startup.cpp`, after T5's block, and add `#include "UrMotion.h"`:

```cpp
  // ---- T6: the motion gate -------------------------------------------------
  {
    const int onCount = urmsg::views::EntranceTimelineCount(true);
    const int offCount = urmsg::views::EntranceTimelineCount(false);
    lines.push_back(std::format(
        L"  T6 bubble entrance   : {} — {} timelines with motion on, {} with it off; "
        L"rise {:.1f} dip, scale {:.2f}->1.00, {} ms",
        (onCount == 4 && offCount == 0 && urmsg::views::kBubbleRiseDip == 10.0 &&
         urmsg::views::kBubbleFromScale == 0.96 && urnw::motion::kBaseMs == 250)
            ? L"PASS" : L"FAIL",
        onCount, offCount, urmsg::views::kBubbleRiseDip, urmsg::views::kBubbleFromScale,
        urnw::motion::kBaseMs));

    std::wstring phases;
    bool phasesOk = (urmsg::views::TypingDotPhaseMs(-1) == -1 &&
                     urmsg::views::TypingDotPhaseMs(urmsg::views::kTypingDots) == -1);
    for (int i = 0; i < urmsg::views::kTypingDots; ++i) {
      const int64_t p = urmsg::views::TypingDotPhaseMs(i);
      if (p != urmsg::views::kTypingPhaseMs * i || urnw::motion::kPulseMs <= p) phasesOk = false;
      phases += (i ? L"/" : L"") + std::to_wstring(p);
    }
    lines.push_back(std::format(
        L"  T6 typing indicator  : {} — {} dots at {} ms (offset {}), {} timelines on / "
        L"{} off, all phases < kPulseMs {}",
        (phasesOk && urmsg::views::TypingTimelineCount(true) == urmsg::views::kTypingDots &&
         urmsg::views::TypingTimelineCount(false) == 0) ? L"PASS" : L"FAIL",
        urmsg::views::kTypingDots, phases, urmsg::views::kTypingPhaseMs,
        urmsg::views::TypingTimelineCount(true), urmsg::views::TypingTimelineCount(false),
        urnw::motion::kPulseMs));
  }
```

---

- [ ] **Step 9: build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

Expected: `0 Error(s)`.

---

- [ ] **Step 10: run the invariants.**

```
app/build/x64/Release/URmessage.exe --diagnose
```

Expected, verbatim:

```
  T6 bubble entrance   : PASS — 4 timelines with motion on, 0 with it off; rise 10.0 dip, scale 0.96->1.00, 250 ms
  T6 typing indicator  : PASS — 3 dots at 0/140/280 ms (offset 140), 3 timelines on / 0 off, all phases < kPulseMs 1500
```

The two zeroes are the point: a gate that returned 4 and 3 regardless of `animate` would print `4 with it off` and FAIL.

---

- [ ] **Step 11: look at the composer, and at where the entrance ended.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

Open `.verify/urmessage-window-screen.png` at **1560×900**. The capture is taken after a 1200 ms settle, i.e. well past `kBaseMs` = 250 ms, so the entrance must be *finished*. Concretely expect:

1. A composer bar across the bottom of the thread column, on `#1C1C1C`, with a 1px top hairline: **paperclip, smiley, a rounded `24h` timer chip**, then a `Message` placeholder, then a **pale-yellow send pill on the right that is visibly faint rather than solid #EFF7BB** (WinUI's `AccentButtonBackgroundDisabled` = `#33EFF7BB`). All four are dimmed.
2. The line `Demo — nothing is sent, and no message leaves this window.` under the row, in `#5A5A5A`.
3. Every bubble at **full opacity, upright and full size** — no half-faded bubble, no bubble sitting 10 DIP low, no bubble at 96%. Any of those means a timeline did not complete or `RenderTransform` was left on.
4. **No typing indicator** (no dots, no `Typing…`): without `--demo-autoplay` it is collapsed.
5. No `□` replacement box on the paperclip, smiley, timer or send glyph.

---

- [ ] **Step 12: look at the typing indicator.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread --demo-autoplay"
```

Open `.verify/urmessage-window-screen.png`. Concretely expect, between the last bubble and the composer: **three 6 DIP grey dots at 5 DIP apart followed by the word `Typing…`**, indented ~20 DIP from the left edge of the thread column. The dots will be caught at different opacities — that *is* the 140 ms phase offset, and three dots at identical opacity means all three timelines started together and the offset was dropped.
