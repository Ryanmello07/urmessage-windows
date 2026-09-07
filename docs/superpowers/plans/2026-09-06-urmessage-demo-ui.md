# URmessage Demo UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the URmessage WinUI 3 scaffold into a click-through demo of an URnetwork-themed
encrypted messenger — conversation list, thread, message-inspect rail, network page, connect
strip, Advanced Mode and a developer surface — all driven by one fabricated, seeded data module,
with no protocol, store or cryptography behind it.

**Architecture:** A single `Demo/DemoWorld` module holds the entire fabricated world behind
`GetWorld()`, so the day a real protocol arrives the diff that deletes the demo is one directory
and one switch. Each surface is a view module in `Views/` following the existing `UrComponents.h`
grain — a struct of named elements plus free `Make*`/`Set*` functions, no MVVM, no IDL, no
observable types — and `MainWindow` stays a thin composer that owns the breakpoint and the click
graph. Spec C §0.2 W1 records that the VPN client's `MainWindow.xaml.cpp` reached 2,128 lines and
became the collision point for parallel UI work; these boundaries exist to avoid repeating that.

**Tech Stack:** WinUI 3 / C++/WinRT, C++20, Windows App SDK 2.2.0, unpackaged desktop app, MSVC.
No test framework. `nlohmann/json` vendored. Build:
`powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1` (~6 s incremental, ~60 s
clean). `pwsh` does not exist on the development box; `powershell` does.

**Design doc:** [`../specs/2026-09-06-urmessage-demo-ui-design.md`](../specs/2026-09-06-urmessage-demo-ui-design.md)

---

## Global Constraints

Every task's requirements implicitly include this section. **Where a task step conflicts with
anything here, this section wins.**

### G1 — Ground truth about this repository

Measured, not assumed. Three of these were asserted wrongly in an earlier draft of the design doc
and the errors propagated into every task brief.

| Fact | Value | Source |
|---|---|---|
| Default window | **480×760 DIP**, min **400×480** | `WindowShell.h:21-25` |
| Window under `--demo` | **1560×900 DIP** (task F7) | design D7 |
| Wide breakpoint | 1000 DIP | `UrComponents.h:75` |
| Third-pane breakpoint | 1500 DIP (`kMessageThirdPaneDip`, added by F7) | design §6.5a |
| Storage root | `%URMESSAGE_APP_ROOT%`, else `%LOCALAPPDATA%\URmessage\app` | `Paths.cpp:35-50` |
| Log file | `<root>/logs/urmessage-app.log` | `Paths.cpp:52` |
| Prefs file | `<root>/app_prefs.json` | `Paths.cpp:54` |
| `Resources.resw` | **GENERATED — never hand-edit** | `Localization.h:3-4` |
| `advanced_mode` pref | Does **not** exist here; F6 introduces it | `git grep advanced_mode` |
| `CollectDiagnostics()` | Runs **before** `winrt::init_apartment()` | `main.cpp:168` vs `:183` |

### G2 — The verification standard

This repository has **no test framework**. Do not add one. An earlier round of this plan invented
its own gates and they did not work; these three are the only permitted ones.

1. **A `--diagnose` assertion.** Add lines to `CollectDiagnostics()` (before `return lines;` at
   `Startup.cpp:217`), run `URmessage.exe --diagnose`, and read the `PASS`/`FAIL` lines. **Every
   assertion line must carry a count**, so a vacuous pass is visible.
2. **The build succeeding**, with `0 Error(s)`.
3. **A screenshot, looked at.** Run
   `powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=<screen>"`,
   then read `.verify/urmessage-window-screen.png` against a **concrete** expectation — countable
   rows, a named glyph, a measured inset. Never "looks correct".

**Forbidden, because they were measured not to work:**

- **Grepping the app log for expected lines.** The log is append-mode, so a "last N lines" check
  reads a previous run's output and passes on stale evidence. If a task needs to prove a code path
  ran, assert it in `--diagnose` instead.
- **Pixel-probe scripts.** Read the screenshot.
- **Any `powershell -Command` containing more than one statement or a `$variable`** — the outer
  PowerShell host expands it before the child process sees it. A reviewer reproduced this
  verbatim: `Index operation failed; the array index evaluated to null`. Put multi-step logic in a
  `.ps1` file, or use a single statement.

Where a task body still contains one of these, **replace it with the nearest permitted gate above**
and note the substitution in the commit message.

### G2a — The index check is REQUIRED, not forbidden

Every commit step carries `(git ls-files | Measure-Object -Line).Lines` before and after
`git add`. **Keep it.** On this machine the git index has been observed to vanish mid-session, and
a commit made against a truncated index silently drops files from the tree — this repository has
paid for that.

What is brittle is a **hardcoded absolute count** ("63 before, 65 after"), because it drifts the
moment any task adds a file, and then every later task's number is wrong. So:

- **Do** record the count before, record it after, and confirm the **delta** equals the number of
  files that task adds — zero for a modify-only task.
- **Do not** assert a specific absolute total. If a task body states one, treat it as advisory and
  check the delta instead.
- Also confirm `git ls-files` and `git ls-tree -r HEAD --name-only` agree after committing.

`verify-render.ps1` has **no** argument passthrough today: its param block is
`[CmdletBinding()] param($Configuration, $Platform, $SettleMs)` and line 156 is
`Start-Process -FilePath $exe -PassThru`. Task **F1** adds `-AppArgs`. It is never `-Args` —
that shadows PowerShell's automatic `$args`.

### G3 — Brand and interaction rules

- **`kProGold` (#FFC400) appears nowhere.** It is reserved for the Pro entitlement across the
  whole product.
- **`UrAccentBrush` (#EFF7BB) is never a bubble fill.** It is the send button and the selection
  outline only.
- **No new motion duration or curve.** Use `UrMotion.h`'s tokens, and gate every animation on
  `motion::ShouldAnimate()`.
- **Colour is never the sole carrier of state.** Selection carries on three channels (fill, a 2px
  accent bar, the automation name), per `SetPaneListRowSelected`'s precedent.
- **Never an empty glyph literal.** Every `Glyph(...)` is `L"\uXXXX"` with the icon name in a
  trailing comment.
- **A Button whose Content is a Panel gets no automatic name** — set
  `AutomationProperties::SetName`. This project has paid for that twice.
- **Anything that cannot be clicked must look inert.** A live-looking control that does nothing is
  a demo bug (design §9.1).
- **No new localization keys.** Demo copy is English string literals in the view modules.

### G4 — Honesty constraint

No copy in the demo may state that a message *was* encrypted as a fact about a real operation.
The demo shows what the UI *will* say; it does not demonstrate encryption. This is the single way
a UI demo can mislead an investor, and it is cheap to honour.

---

## File Structure

```
app/tools/
  verify-render.ps1              MODIFY  F1 adds -AppArgs
app/src/App/
  MainWindow.xaml(.cpp/.h)       MODIFY  thin composer: breakpoint, nav, hosts, click graph
  App.xaml                       MODIFY  one new key (UrBubbleButtonStyle, T2)
  Startup.cpp                    MODIFY  --diagnose assertion lines
  UrComponents.h                 MODIFY  kMessageThirdPaneDip (F7)
  Identicon.h/.cpp               CREATE  F5   deterministic identicon, own CornerRadius(8)
  Demo/
    DemoWorld.h/.cpp             CREATE  F2   the entire fabricated world, pure C++
    DemoSwitches.h/.cpp          CREATE  F4   --demo parsing
    AdvancedMode.h/.cpp          CREATE  F6   the advanced_mode pref + change notification
    DemoShellState.h             CREATE  W1   the composer's pure decisions
    ThreadLayout.h               CREATE  T1   run/glyph/width rules, pure C++
    DemoAutoplay.h/.cpp          CREATE  W8   ambient activity
    DeveloperSwitches.h/.cpp     CREATE  A2   session-only dev switches
  Views/
    ConversationRowModel.h/.cpp  CREATE  L1
    ConversationListView.h/.cpp  CREATE  L2
    ThreadView.h/.cpp            CREATE  T3
    InspectRailFields.h/.cpp     CREATE  R1
    InspectRailView.h/.cpp       CREATE  R2
    NetworkPageView.h/.cpp       CREATE  N2
    StatusStripRules.h/.cpp      CREATE  S1
    StatusStripView.h/.cpp       CREATE  S2
    SettingsView.h/.cpp          CREATE  A3
    DeveloperView.h/.cpp         CREATE  A5
```

Every `Demo/*` and `Views/*Rules|Model|Fields|Layout` header is **pure C++ with no `winrt/`
include**, because `GetWorld()` and the rule functions are called from `CollectDiagnostics()`,
which runs before `winrt::init_apartment()`.

---

## Tasks

46 tasks, 479 steps. Build them in this order — the ordering is a dependency ordering, not a
preference.

| Group | Tasks | Steps | File |
|---|---|---|---|
| **Foundation** | F1–F7 | 62 | [`tasks/foundation.md`](tasks/foundation.md) |
| **The demo shell** | W1, W3, W4 | 33 | [`tasks/shell.md`](tasks/shell.md) |
| **Conversation list** | L1–L6 | 59 | [`tasks/list.md`](tasks/list.md) |
| **Thread** | T1–T6 | 52 | [`tasks/thread.md`](tasks/thread.md) |
| **Inspector rail** | R1–R4 | 52 | [`tasks/rail.md`](tasks/rail.md) |
| **Network page** | N1–N6 | 69 | [`tasks/network.md`](tasks/network.md) |
| **Status strip** | S1–S4 | 50 | [`tasks/strip.md`](tasks/strip.md) |
| **Settings / Advanced / Developer** | A2–A6 | 52 | [`tasks/advanced.md`](tasks/advanced.md) |
| **Click graph and ambient activity** | W5–W9 | 50 | [`tasks/wiring.md`](tasks/wiring.md) |

**F1 first, and alone.** Every screenshot gate in every later task runs
`verify-render.ps1 -AppArgs`, which does not exist until F1 lands. **F2 next** — nothing has data
until the world exists.

### Two tasks were dropped during assembly

`W2` and `A1` are **deliberately absent**. All three of F6, W2 and A1 created
`Demo/AdvancedMode.h/.cpp`; only whole-plan assembly could see the collision. F6 builds it, and
`W7` and `A4` consume it rather than building their own.

---

## Known deviations and open decisions

Each is a judgement an implementer should not have to make alone.

1. **The delivery glyphs are circled checks. RULED 2026-09-06 — this is the design.**
   Delivered is `U+E930` (check in an outline circle), Read is `U+EC61` (check in a solid circle).
   A checkmark is a stroke and has no fill of its own, so design §6.2's "outline" vs "filled" only
   means anything as an enclosing shape — which is exactly what these two are. The WhatsApp idiom
   (grey double-check vs blue double-check) was rejected because it separates Delivered from Read
   **by colour alone**, which G3 forbids. **Contingency:** if either codepoint renders as a tofu
   box, do not fall back to colour — draw the checks as a vector `Path`. WinUI takes arbitrary
   geometry and a double-check is a four-point polyline, which removes the font dependency.
2. **One new App.xaml key: `UrBubbleButtonStyle`.** A bubble must be clickable, and all four
   existing Button styles paint a background on hover, which would destroy the incoming/outgoing
   fill distinction. Verified absent by grepping every `x:Key` in `App.xaml`.
3. **Glyph codepoints are the one class of claim that could not be verified statically** — the
   font is not in the repo. The repo's existing glyphs were byte-decoded to establish the range.
   T3's screenshot step makes "no tofu box" an explicit read, which is the cheapest honest check.
4. **`Demo/ThreadLayout.h` sits in `Demo/` but declares `urmsg::views`**, unlike the other pure
   rule modules which are in `Views/`. Moving it is a one-line include change in three files.
5. **`kInspectTargetRowId` and `MakeIdenticonPattern`** are additions beyond the fixed contract,
   both forced by the pre-apartment purity constraint. Neither renames or changes a fixed
   signature.
6. **The app icon stays the URnetwork globe. RULED 2026-09-06** — it is the company brand.
   `Assets/README.md` calls it a placeholder to be replaced; that note is superseded for this
   work. No task touches `app.ico`.
