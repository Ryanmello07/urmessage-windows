# D7 — Audit distillation + landing order for the 21 remaining demo-UI tasks

2026-09-08 · design-swarm agent D7 · for the coordinator
Source: `docs/superpowers/plans/2026-09-08-remaining-task-audit.md` (62 findings: 30 CONFIRMED,
27 PARTIAL, 2 REFUTED, 3 UNVERIFIED) and the task files under `docs/superpowers/plans/tasks/`.
Every override below is quoted verbatim from the audit; `[…]` marks elisions. Nothing here relaxes
handoff §4 (G3 brand, G4 honesty, UrMotion tokens, three-channel delivery, the immutable fixture).

## 0. Read this first — the tree has moved since the audit

The audit's evidence baseline is HEAD `d57089a` (R3). Two commits have landed since:

- `b5e1ccf` — the handoff + the audit doc itself.
- **`0ca2665` — "shell: route unbuilt network/settings/developer destinations to the stub page".**
  Handoff §9 item 1 is DONE (the baseline `network-stub.png` shows it live). `ShowDestination`
  (MainWindow.xaml.cpp:723-759) now routes **network, settings and developer to `StubPage()`**,
  with an interim comment at :740-742: *"Route to the stub … until the real surface mounts into
  its host."* Only `contacts` was already stub-routed.

**Consequence for the audit's overrides.** Three overrides cite the old routing
("`MainWindow.xaml.cpp:740` crossfades to `NetworkHost()`" / ":745-747 already routes tag
settings" / ":742-744 already routes developer"). Their cores stand; each gains one line:

- **N3** mounts into `NetworkHost()` *and* re-points the network arm
  (`incoming = StubPage()` → `incoming = NetworkHost()`, MainWindow.xaml.cpp:743), deleting the
  interim comment for that arm.
- **A3** — same for the settings arm (:748-749 → `SettingsHost()`).
- **A5** — same for the developer arm (:745-747 → `DeveloperHost()`).

Do not delete the stub arms' shared shape; the pattern is already correct, only the target changes.

Also confirmed independently for this dispatch (grep over `app/src/App`, this worktree):
**no `BuildDemoViews`, `BuildStatusStrip`, `BuildNetworkPage`, `BuildSettings`, `BuildDeveloper`,
or `ApplyAdvanced*` symbol exists yet.** Every ownership ruling below is about code in *plan
text*, not code in the tree.

Process reminders that bind every dispatch (handoff §6/§7): run
`python .superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py <brief>` before dispatching
anything; `brief.py` never reads the ledger or this file, so **paste the override into the
dispatch text**; demonstrate every new gate failing, then revert, and paste both outputs.

## 1. The ownership cluster — resolved

The audit's per-task overrides **conflict with each other** on the strip. Two were written
assuming W6 owns the mount; one was written assuming the strip group owns it. Only one reading is
coherent with the mandated build order. Ruling first, evidence after.

### 1.1 Status-strip mount and `ToggleStatusDrawer`: **the strip group (S2/S3) owns; W6 is gutted**

**Ruling (resolution α).**
- **S2** keeps its mount: declares `urmsg::views::StatusStripView statusStrip_{};` (keep the
  brief's name — do **not** rename to `strip_`), `void BuildStatusStrip();`, and the constructor
  call, mounting into the **existing** `StatusStripHost` (MainWindow.xaml:278 — see §3, S2).
- **S3** keeps `MainWindow::ToggleStatusDrawer()`, the `strip.Click` wiring, the
  `StatusDrawerHost` markup and its mount, with the drawer's own `Visibility` as the only state.
- **W6** deletes the strip parts of Steps 1, 2 and 4 entirely: no `strip_` member, no
  `drawerOpen_`, no `ToggleStatusDrawer` declaration or definition, no Click wiring, no mount.
- **W7** calls `urmsg::views::SetStatusStripAdvanced(statusStrip_, on)` — `statusStrip_`, not
  `strip_`.

The audit's W6-class-2 override, verbatim:

> "Do NOT declare ToggleStatusDrawer, drawerOpen_ or a strip_ member: strip.md 3/4 already ships
> MainWindow::ToggleStatusDrawer, the statusStrip_ member and the strip Click wiring, and its
> drawer state is the drawer's own Visibility by design. Delete Steps 2 and 4 of W6 entirely and
> delete `strip_` and `drawerOpen_` from the Produces list; W7 must then call
> `urmsg::views::SetStatusStripAdvanced(statusStrip_, on)`, not `strip_`."

The two overrides I am **setting aside**, quoted so nobody re-derives them:

- S2-class-8 (β): "Name the member `strip_`, not `statusStrip_`, and do NOT add
  `MainWindow::BuildStatusStrip()` or a constructor call. wiring.md W6 Step 2 already mounts
  `strip_ = urmsg::views::MakeStatusStrip(urmsg::demo::GetWorld())` into StatusStripHost from
  `BuildDemoViews()` […] S2 delivers `Views/StatusStripView.{h,cpp}` and touches MainWindow only
  to add the two `#include`s and the `strip_` member declaration."
- S3-class-8 (β): "Do NOT declare `ToggleStatusDrawer()` and do NOT wire `strip.Click` —
  wiring.md W6 Steps 1, 2 and 4 own both. S3 delivers only `MakeStatusDrawer`, the `v.drawer`
  population in `MakeStatusStrip`, `SetStatusStripDrawerOpen`, the `StatusDrawerHost` markup, its
  mount, and verify-drawer.ps1. Add one note for W6: drop its `drawerOpen_` member and read
  `strip_.drawer.Visibility()` instead, so the drawer has one state and not two."

**Why α wins (the audit's own evidence):**

1. **Build order is mandated.** Plan table (2026-09-06-urmessage-demo-ui.md:206-220): "the
   ordering is a dependency ordering, not a preference", Status strip S1–S4 above Click graph
   W5–W9. S lands first; the second declaration is then W6's, and per the W6-class-2 verifier,
   "redeclaring a member function in a class body is ill-formed — the header alone stops the
   build." β makes the build red in plan order.
2. **β is infeasible in any order.** W6's own Consumes line (wiring.md:329) lists
   `SetStatusStripDrawerOpen` — S3's product — so W6 cannot be reordered ahead of S3 to make β
   work. And under β, S2/S3's screenshot gates (the 26 DIP strip on screen; verify-drawer.ps1
   opening the drawer) are unsatisfiable when those tasks run, because the mount hasn't landed —
   the exact compile-green-zero-pixels failure this project has shipped four times.
3. **The audit's W6-class-1 verifier says it outright:** "hosting the drawer is not W6's job.
   wiring.md legitimately contains zero `StatusDrawerHost` because S3 owns that element
   end-to-end […] when W6 runs, a drawer is already in the visual tree."
4. **S3's design rationale is the better state model.** strip.md:1278: "Visibility IS the state;
   there is no second bool to disagree with the tree." W6's `drawerOpen_` is that second bool;
   the S3-class-8 consequence: "if W6 lands alone, `drawerOpen_` and the drawer's real Visibility
   can disagree after the 150 ms dismiss animation, so one click is swallowed." (The 150 ms exit
   is `kFastMs` — exits one step faster than the `kBaseMs` entrance, per UrMotion.h.)
5. The surviving half of the β overrides — "one state, not two" — is preserved under α: W6 never
   adds `drawerOpen_` at all.

Conditional-only override, keep on file (W6-class-1): "**If** any part of Step 2 survives, it
must also host the drawer: after appending strip_.root, add
`StatusDrawerHost().Children().Clear(); if (strip_.drawer) StatusDrawerHost().Children().Append(strip_.drawer); else urnw::LogWarn("window: demo status strip has no drawer; activation will paint nothing");`."
Under α, Step 2 does not survive; S3 already hosts the drawer.

**What remains of W6.** The audit's second W6-class-2 override:

> "Delete Step 3 and the network_/settings_/developer_ declarations from Step 1. N3, A4 and A5
> already declare those three members and already build and mount those three views in
> BuildNetworkPage/BuildSettings/BuildDeveloper. W6's only remaining job is whatever null-check
> auditing those three builders lack; verify with
> `git grep -n 'NetworkHost()\|SettingsHost()\|DeveloperHost()' -- app/src/App/MainWindow.xaml.cpp`
> returning exactly one Append per host before you write any new code."

⚠ Attribution fix the coordinator should apply when quoting that: the cited lines
(advanced.md:349, 788, 841) sit inside **A3's** span (advanced.md:340-976) — the `settings_`
owner is **A3, not A4** (handoff §7: a reviewer's claim is not evidence; the line numbers
settle it).

**Recommendation: fold W6's residual into W7's dispatch as a pre-flight and mark W6 superseded
in the ledger.** W6's title promise — "every nav destination leading somewhere real" — is already
true (commit 0ca2665 routes all three to the stub) and stays true as N3/A3/A5 re-point the arms.
If the coordinator prefers keeping W6 as a task, it is a two-step hardening pass: the one-Append-
per-host grep plus the null-guard audit. Either way, W6 adds **no** members, methods, or mounts.

### 1.2 The Advanced Mode subscription: **W7 owns the one subscriber; N6/S4/A4 only seed**

**Ruling.** Exactly one function, `MainWindow::ApplyAdvanced(bool)`, **private**, registered once
via `urmsg::OnAdvancedModeChanged` in `EnterDemoMode()` after `BuildDemoViews()`, fanning out to
all five surfaces. The tree's own comment already assigns it: MainWindow.xaml.cpp:265-268 — "the
live subscription that re-calls this on every toggle is registered by the wiring surface, which
owns OnAdvancedModeChanged."

The audit's A4-class-8 override, verbatim:

> "A4 does not introduce a subscription. wiring.md W7 owns `MainWindow::ApplyAdvanced(bool)` and
> the single `urmsg::OnAdvancedModeChanged` registration, placed in `EnterDemoMode()` after
> `BuildDemoViews()`. Rename A4's handler to `ApplyAdvanced` and fold its body into W7's; A5's
> Developer gate becomes two lines added to W7's `ApplyAdvanced`, not a second function. Delete
> A4 step 2's registration block and A4 step 3's constructor call."

Supporting audit evidence (A4-class-8 verifier): four task files claim "THE one subscription" —
N6 (network.md:1548, **guarded**), S4 (strip.md:1614, **guarded**), A4 (advanced.md:1027,
**unguarded**), W7 (wiring.md:601, **unguarded**). The guards on N6/S4 are the fold-one-line
kind; A4 and W7 would collide head-on.

Per-surface rulings:

- **N6** lands first in plan order and verifies by launch-switch seeding (its Steps 12/13
  captures are `--demo-advanced` vs not — **not** a live runtime toggle). So N6 needs no
  registration at all. **Tightening of the audit's N6-class-8 override** (which permits N6 to add
  the function "only if it prints nothing"): under the W7-owns ruling, N6 adds **only** the
  seeding call `urmsg::views::SetNetworkPageAdvanced(network_, urmsg::AdvancedModeEnabled());` at
  the page's build site. The probe itself stays correct — quote it into N6's dispatch as the
  guard against a future second subscriber:

  > "Replace Step 6's guard with the probe strip.md:1602 already uses, which tests for the
  > SUBSCRIBER rather than a function name: `git grep -n "OnAdvancedModeChanged" --
  > app/src/App/MainWindow.xaml.cpp`. If it prints ANY hit, do NOT register a second subscriber
  > and do NOT add a second fan-out function […]. Only if it prints nothing do you add the
  > function, and declare it PRIVATE to match wiring.md:533 and advanced.md:986, not public."

  (Today that probe prints only the decl/def in AdvancedMode.h:31 / AdvancedMode.cpp:51 plus a
  comment at MainWindow.xaml.cpp:267 — zero call sites; my own grep confirms.)
- **S4**, verbatim: "Step 5: do NOT run the grep and do NOT add `InitAdvancedMode` or an
  `OnAdvancedModeChanged` registration. MainWindow.xaml.cpp:468 already calls
  `urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced)` in EnterDemoMode, and the
  comment at :265-267 assigns the single subscriber to the wiring surface — wiring.md W7 Step 3
  registers it […]. S4 delivers `SetStatusStripAdvanced` plus the one seeding call and log line
  at the strip's build site, and nothing else in MainWindow." (Under §1.1, the member W7's
  fan-out names is `statusStrip_`, not the `strip_` this override quotes.)
- **A4** keeps only its real deliverable: the "--demo-advanced writes no preference" gate (see
  §3, A4). No handler, no registration, no constructor call.
- **A5**'s Developer gate folds into W7 as "two lines", and those lines carry the nav-corruption
  workaround (A5-class-9, verbatim): "In `ApplyAdvancedMode`/`ApplyAdvanced`, every
  `DeveloperNavItem().Visibility(...)` write must be followed immediately by
  `HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact);
  HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);` — copy the pattern and the
  WORKAROUND comment from MainWindow.xaml.cpp:566-569 rather than restating it. Then add a
  verification step: launch `--demo=settings --demo-advanced`, and in the same run capture
  `urmessage-window-wide.png` and confirm Chats/Contacts/Network/Developer/Settings still show
  TEXT LABELS at 1200x800."
- **W7** gets the same probe guard pasted into its dispatch (it is unguarded today): expected
  zero call sites when it runs; if the landing order ever changes and a hit appears, fold its
  fan-out into the existing function instead of registering.
- **Stated limit** (handoff §7 lesson 5): no gate can exercise a *live* toggle — the only switch
  is A5's ToggleSwitch and input synthesis is forbidden. Gates cover launch-time seeding
  (`--demo-advanced`, deep-link `forceAdvanced`). W7's report must say so.

## 2. The four headline rulings

### 2.1 W5 mounts into a permanently collapsed host (CONFIRMED — fifth recurrence of class 1)

`ListHost` is `Visibility="Collapsed"` by XAML attribute at MainWindow.xaml:213; no line of code
ever flips it (zero `ListHost()` hits in MainWindow.xaml.cpp), and its own XAML comment (:200-212)
says "Left declared (Collapsed, never made Visible, nothing ever appended to it again)".
`mount_check.py` reports it **ok** because it reads only `.Visibility(Collapsed)` *writes* in the
.cpp — a documented false pass. W5-class-1 override, verbatim:

> "In Step 2 do NOT mount into ListHost. ListHost is Visibility="Collapsed" at
> MainWindow.xaml:213, its own XAML comment says "never made Visible, nothing ever appended to it
> again", and no code writes ListHost().Visibility. Replace `ListHost().Children().Clear(); ...
> ListHost().Children().Append(list_.root);` with `auto rows = ConversationList().Children();
> rows.Clear(); ... rows.Append(list_.root);` and also write `ListPaneCount().Text(...)`, exactly
> as the existing demo branch of BuildConversationList does (MainWindow.xaml.cpp:243-253).
> Re-run mount_check after the edit and note that its "ok" on ListHost was a false pass."

### 2.2 The `--demo=inspect` deep link: **c0-r12 (Read), not c0-r23 (Pending)** — wiring.md changes, not R3

Settled ruling (handoff §6), now backed by a CONFIRMED verifier result. W5's Step 6 reverse-scan
picks the last `RowKind::Message` = **c0-r23** ("Retrying the attachment.", Pending), which
DemoWorld.cpp:177-184 leaves with empty `receivedAtLabel`/`deliveredTo`/`readBy` — the showcase
capture would open on a blank Received row and empty device lists, contradicting W5's own Step 8
expectation. The shared pick is `PickInspectMessage` → **c0-r12** (Read, delivered-by 7,
read-by 7), pinned by three consumers plus a probe. W5-class-8 override, verbatim:

> "Replace the reverse-scan loop in Step 6 with the shared pick: `#include
> "Views/InspectRailFields.h"` and `if (pendingLink_.selectMessage) { auto const& conv =
> urmsg::demo::GetWorld().conversations.front(); if (auto const* picked =
> urmsg::views::PickInspectMessage(conv)) SelectMessage(picked->id); else
> urnw::LogWarn("window: --demo=inspect but conversation 0 has no message row"); }`. Do NOT
> designate the last RowKind::Message: that is c0-r23, Pending, with no received-at and no
> devices. Also fix the stale comment at Demo/DemoShellState.h:68, which still reads "and
> pre-select its newest Message row" -- it must read "and pre-select kInspectTargetRowId
> (c0-r12), the row PickInspectMessage returns"."

And the three-writers override (W5-class-8, UNVERIFIED in-line but independently **CONFIRMED** in
the audit's closing section), verbatim:

> "Before adding anything in Step 6, DELETE the `if (options_.screen ==
> urmsg::demo::DemoScreen::Inspect) { ... }` branch and its LogWarn from
> MainWindow::BuildInspectRail (MainWindow.xaml.cpp:413-419) -- the comment above it orders
> exactly this -- and DELETE the existing `if (pendingLink_.selectMessage && thread_.root)
> urmsg::views::SetThreadSelectedMessage(thread_, urmsg::demo::kInspectTargetRowId);` at
> MainWindow.xaml.cpp:584-585. After Step 6 there must be exactly ONE `pendingLink_.selectMessage`
> branch in the whole file; verify with `git grep -c 'pendingLink_.selectMessage' -- app/src`
> returning 1."

### 2.3 W9 owns design §9.2's do-not-yank rule — pure-function extraction (CONFIRMED)

Deferred five times because every candidate task listed only MainWindow files. The unlock, as
the handoff puts it: *the decision can be pure even when the event cannot be synthesized.*
W9-class-6 override, verbatim:

> "WIDEN W9's Files list to `Modify app/src/App/MainWindow.xaml.h; Modify
> app/src/App/MainWindow.xaml.cpp; Modify app/src/App/Views/ThreadView.h; Modify
> app/src/App/Views/ThreadView.cpp; Modify app/src/App/Startup.cpp` and add a step that closes
> design §9.2's do-not-yank rule. Extract the DECISION as a pure function in ThreadView.h --
> `bool ShouldPinToBottom(bool armed, double offset, double scrollableHeight);` returning true
> when !armed, and otherwise `scrollableHeight - offset <= 48.0` -- add a `bool pinArmed =
> false;` to ThreadParts, and replace the body of the stack.SizeChanged handler at
> ThreadView.cpp:949-951 with `if (!ShouldPinToBottom(parts->pinArmed,
> parts->scroller.VerticalOffset(), parts->scroller.ScrollableHeight())) return; parts->pinArmed
> = true; parts->scroller.ChangeView(...)`. You do NOT need to synthesise a scroll to prove this:
> gate ShouldPinToBottom directly in --diagnose over four cases -- (false, 0, 892.8) true (the
> first-pin case ThreadView.cpp:939-948 says a naive guard breaks), (true, 892.8, 892.8) true,
> (true, 860.0, 892.8) true, (true, 100.0, 892.8) false -- and delete the "NOT implemented, here
> or anywhere" paragraph at ThreadView.cpp:1305-1311 in the same commit. The decision can be pure
> even where the event cannot be synthesised."

### 2.4 Honesty-gate overrides (G4 — these are not optional)

- **A6 cipher dump (CONFIRMED).** `MessageInspect::cipher` ("XChaCha20-Poly1305",
  DemoWorld.cpp:172) would appear 66 times and ride the Copy button onto the clipboard.
  Override, verbatim: "Delete `cipher=\"{}\"` from the row-line format string and
  `r.inspect.cipher` from its argument list. `MessageInspect::cipher` stays unrendered in this
  binary — InspectRailFields.h:26-27 and InspectRailView.cpp:372 are the standing ruling and the
  dump is not an exception to it. Add a one-line comment at the format string saying the field is
  deliberately omitted, so the next reader does not 'complete' the dump."
- **A6 dump framing (PARTIAL — clipboard half confirmed).** Override, verbatim: "Give the dump
  its own framing, in the text itself and not in the group header. Make `DumpDemoWorld()` open
  with a first line `demo model: fabricated world, no crypto and no protocol in this build; every
  value below is generated by Demo/DemoWorld.cpp`, and change the two claim-carrying tokens to
  `key=demo-model-verified` / `key=demo-model-not-verified` and `att=demo-model-verified` /
  `att=demo-model-not-verified`. Then update step 5's expected `dump.world` line and step 7's
  expected first two lines to match, and assert in `WorldDumpDiagnostics()` that the dump's first
  line contains `demo model:` so the framing cannot be dropped silently."
- **N2 `FormatKeyState` (CONFIRMED) — the one owner of the string.** Override, verbatim: "Change
  `FormatKeyState` to the prefix-first form this codebase already ships: `return keyVerified ?
  std::wstring(L"Demo model: verified") : std::wstring(L"Demo model: not verified");` — matching
  `InspectRailFields.cpp:113` character for character in shape. Update the `net fmt keystate`
  assertion to `FormatKeyState(true) == L"Demo model: verified" && FormatKeyState(false) ==
  L"Demo model: not verified"`, and update the Step 8 expected output line to match. Do not ship
  the bare words in any label, any automation name, or any gate."
- **N4 Server key row (CONFIRMED).** Override, verbatim: "The `Server key` row value must be the
  prefix-first string from the corrected `FormatKeyState` — `Demo model: verified` / `Demo model:
  not verified` — not the bare words. Keep the colour rule as written (kUrGreen / DangerBrush)
  since the words carry the meaning either way, and rewrite Step 7's expectation 3 to read:
  `Server key` / `Demo model: verified` in green `#87FB67`. Do not render
  `MessageInspect::cipher` anywhere on this page under any label." (G3 note: `kUrGreen` #87FB67
  stays within its presence/padlock role; the framing lives in the words, not the colour.)
- **A3 Server key row (CONFIRMED) — sequencing edge: N2 must land first.** Override, verbatim:
  "In step 3's Server key row, render `world.server.keyVerified ? S(L"Demo model: verified") :
  S(L"Demo model: not verified")` — prefix-first, matching `urmsg::views::AttestationLabel`
  (InspectRailFields.cpp:113) — and update step 10's expectation to those two strings. Do NOT
  write your own copy of this formatter: network.md N2's `FormatKeyState(bool)` is the designated
  one owner of this string and its gate currently asserts the bare words; that gate and this row
  must be corrected together and this row must then call `urmsg::views::FormatKeyState`."
- **S2 padlock automation name (CONFIRMED).** The padlock is content — the strip's only statement
  of key verification — and it is named. Override, verbatim: "In Step 6 set `kKeyVerifiedName[] =
  L"Demo model: server key verified"` and `kKeyUnverifiedName[] = L"Demo model: server key not
  verified"` — the same prefix-first framing AttestationLabel ships at InspectRailFields.cpp:113
  for the same reason recorded there: the bare words 'state that a check RAN and returned a
  result', and the value must carry its own framing rather than lean on a chip
  --demo-watermark=off removes."
- **S3 drawer header (CONFIRMED).** The drawer states three live hops and per-node health as
  fact. Override, verbatim: "Step 3: set `kRelayHeader[] = L"DEMO MODEL: RELAY PATH"` and
  `kDrawerName[] = L"Demo model: relay path preview"`, and update Step 11's expected header text
  to match. The drawer states three live hops and per-node health as fact and carries no other
  framing; the rail's equivalent header already ships `Demo model: end-to-end encrypted`
  (InspectRailView.cpp:410) for this reason, and --demo-watermark=off removes the chip."
  Design note: the 28 DIP header row keeps the letterspaced chrome voice of the existing section
  strips (`CONVERSATIONS`, `MEMBERS`) — the framing is carried by the words, so no new colour,
  no new typeface, no new `Ur*` key.

## 3. Per-task dispatch blocks

Findings marked with verdict; overrides quoted verbatim. REFUTED findings are one-liners so
nobody re-derives them.

### R4 — rail device lists + density switch (rail.md:1411-1594) · 8 findings
Files: InspectRailView.cpp, MainWindow.xaml.cpp, and (per the missing-extraction override)
InspectRailFields.h/.cpp.
- **PARTIAL** — Step 4 anchor names `InspectRailHost`, which exists nowhere. Override: "In Step
  4, the anchor line is `RailHost().Children().Append(rail_.root);` at
  app/src/App/MainWindow.xaml.cpp:372 — NOT `InspectRailHost()`. Do NOT create, rename or declare
  any `InspectRailHost`: RailHost is already the Grid.Column=4 occupant of ChatsPage
  (MainWindow.xaml:242) and ApplyBreakpoint is its only visibility writer. Insert your one line
  immediately after :372 and before `auto const& world = urmsg::demo::GetWorld();`."
- **PARTIAL** — `demo_` does not exist; the obvious fix (`options_.advanced`) is the *wrong
  source*. Override: "In Step 4, write `urmsg::views::SetInspectRailAdvanced(rail_,
  urmsg::AdvancedModeEnabled());` — NOT `demo_.advanced` and NOT `options_.advanced`. […] Match
  the conversation list's established call at MainWindow.xaml.cpp:268 exactly; contract §5 allows
  one reader of that truth, not two."
- **CONFIRMED** — Step 10's CrossfadePageSwap gate expects 1; the regex already matches 2.
  Override: "In Step 10, the expected first number is 2, not 1: `[regex]::Matches($t,
  'CrossfadePageSwap\(')` matches the prose reference at InspectRailView.cpp:276 as well as the
  one real call at :294. Do NOT delete or reword that comment to make the number 1. Either change
  the expected output to `CrossfadePageSwap call sites in file : 2`, or change the regex to
  `urnw::motion::CrossfadePageSwap\(` (which matches only the call site) and keep the expected
  value at 1. Run the command BEFORE you edit the file and record the baseline number in your
  report; the second number (PresentMode after SetInspectRailAdvanced) must be 0 and is the
  clause that actually matters."
- **CONFIRMED** — the `InitialRailMode` extraction assigned to R4 (progress.md:391, live comment
  at MainWindow.xaml.cpp:139-145) appears nowhere in rail.md. Override: "Add a step before Step
  5: extract the mode decision from MainWindow::BuildInspectRail into `urmsg::views::RailMode
  InitialRailMode(demo::DemoScreen screen, demo::Conversation const& conv);` declared in
  Views/InspectRailFields.h and defined in InspectRailFields.cpp, have BuildInspectRail call it
  instead of deciding inline, and extend InspectRailDeviceProbe with three clauses:
  InitialRailMode(Inspect, world.conversations.front()) == Message; InitialRailMode(Chats,
  world.conversations.front()) == Conversation; and InitialRailMode(Inspect, <a locally built
  conversation whose rows contain no RowKind::Message>) == Conversation. Build that third
  conversation INSIDE the assertion — do not edit DemoWorld, which is byte-fingerprinted as I10.
  The LogWarn arm you are covering is at app/src/App/MainWindow.xaml.cpp:420, NOT
  InspectRailView.cpp:408 (that file contains no LogWarn); fix the citation wherever you repeat
  it. State in your report that this probe asserts what the mode SHOULD be and not which write
  lands last, so it becomes a real gate only alongside the wiring task's single-writer rule."
- **PARTIAL** — Step 2's `for (auto const& field : BuildMessageFields(...))` anchor is gone.
  Override: "In Step 2, the anchor is the line `AppendFieldRows(panel, BuildMessageFields(conv,
  row, advanced));` — the LAST statement of PopulateMessage at
  app/src/App/Views/InspectRailView.cpp:534. There is no `for (auto const& field :
  BuildMessageFields(...))` loop; do not go looking for one, and do NOT put your two
  AppendDeviceList calls in AppendFieldRows (:480-487), which conversation mode also calls.
  Append them after :534, using the `body` already bound at the top of PopulateMessage, and leave
  AppendFieldRows untouched."
- **PARTIAL** — Step 9 clause 2's "13 characters" Group id rendering cannot occur (16-char ids
  come back whole). Override: "In Step 9 clause 2, the expected reading is SIXTEEN hex characters
  rendered whole with no ellipsis: DemoWorld gives conversation 0 (`conv-design-team`,
  ConversationKind::Group, DemoWorld.cpp:245-246) a groupIdHex of exactly 16 characters, and
  ShortHex returns any input of size <= keep+8 unchanged (InspectRailFields.cpp:136-139) […].
  Delete the '13 characters' expectation and the 'either way' clause; check instead that the 16
  characters fit on one right-aligned line with nothing wrapped or clipped. Do NOT change
  ShortHex to make 13 appear."
- **PARTIAL** — Step 7 clause 5's empty-list branches are unreachable (pick is c0-r12, D=7 R=7).
  Override: "Keep the clause but state in your report that it did not fire, and cover the branch
  where the world cannot reach it: add two static_asserts or a --diagnose clause built from a
  LOCALLY constructed MessageInspect with an empty deliveredTo and an empty readBy, asserting
  AppendDeviceList's empty path produces exactly one MakePaneEmptyLine child carrying the
  expected string. Do NOT edit Demo/DemoWorld to create an empty-list row — the world is
  byte-fingerprinted as I10 = 0x97B1C149D13010C3."
- **PARTIAL** — R4 hands its live-toggle follow-up to "the Advanced Mode task". Override: "In
  Step 4's comment and in the closing 'What is NOT delivered here' paragraph, name the WIRING
  task, not the Advanced Mode task: wiring.md:578 is the file that adds
  `urmsg::views::SetInspectRailAdvanced(rail_, on);` […]. Do not register an
  OnAdvancedModeChanged subscriber in this task, and do not assert in your report that the window
  registers exactly one — four task files currently each claim to create it, and that conflict is
  the wiring task's to settle."

**Sequencing note (new, from this distillation):** R4's Step-4 seeding line lands inside
`BuildInspectRail`, which **W5's Step 0 later deletes**. Paste into W5's dispatch: "when deleting
BuildInspectRail, carry its `SetInspectRailAdvanced(rail_, urmsg::AdvancedModeEnabled());`
seeding line into BuildDemoViews beside the rail mount." This is why R4 lands before W5 (§4).

### N1 — Network destination shell (network.md:7-284) · 5 findings
N1 shrinks to a verification task: everything it was going to build already exists.
- **PARTIAL** — Steps 3-4 would add a second destination Grid. Override: "SKIP Step 3 and Step 4
  entirely. Do NOT add the `NetworkPage` Grid, `NetworkPaneTitle`, `NetworkBody`, the
  ScrollViewer, or any line to `ApplyStrings()`. The shell owns all of it: `NetworkNavItem` is at
  MainWindow.xaml:126, `NetworkHost` at :265, and `NetworkNavItem().Content(box_value(
  hstring{kDemoNavNetwork}))` is already set in `EnterDemoMode()` at MainWindow.xaml.cpp:486.
  Treat every step of N1 whose condition names `NetworkPage` or `NetworkBody` as satisfied by
  `NetworkHost` and add nothing."
- **REFUTED** (as claimed) — the "second Grid / second Content writer" consequences are refuted
  by Step 1's own guard, but the ruling stands: "Do NOT replace `ShowDestination`. […] Add
  nothing to it. Leave `CrossfadePageSwap`, `currentTag_` and `currentPage_` exactly as they
  are." (Drift note §0: the network arm now routes to StubPage; **N3** re-points it, not N1.)
- **CONFIRMED** — Steps 6-7's `ApplyDemoScreen` duplicates the deep-link path. Override: "SKIP
  Steps 6 and 7 entirely. Do NOT declare or define `ApplyDemoScreen`, do not add `#include
  "Demo/DemoSwitches.h"` for it, and add nothing to the constructor. The shell's
  `DrainDeepLink()` (MainWindow.xaml.cpp:520) already arms `--demo=network` through
  `DeepLinkFor(DemoScreen::Network)` -> `SelectNavTag(L"network")` post-layout, which is the only
  correct time to do it. N1 reduces to Steps 8-11 (build, launch, read, confirm) plus the
  commit."
- **PARTIAL** — hardcoded file counts (63/65) are stale (96 at audit time, more now). Override:
  "Ignore every literal file count in this file (63, 65). […] The invariant to check is the
  DELTA, not the absolute: N1, N3, N4, N5 and N6 add no files, so the count before and after
  `git add -A` must be IDENTICAL; N2 adds Views/NetworkPageView.h and .cpp, so it must go up by
  exactly 2. Run `git ls-files | Measure-Object -Line` before and after and compare the two
  numbers to each other, never to a number in this document." (Applies to all N tasks; G2a.)
- **PARTIAL** — glyph E701 vs the shell's shipped `` (Ethernet) at MainWindow.xaml:129.
  Override: "Do NOT change the Network nav item's glyph. The shell ships `` (Ethernet) at
  MainWindow.xaml:129 with its own comment naming the choice. Rewrite Step 10 expectation 3 to:
  the Network item's glyph is an Ethernet port, not a hollow rectangle — a hollow rectangle means
  the E839 codepoint missed. Ignore every mention of E701 in this task."

### N2 — pure formatters (network.md:285-586) · 3 findings
Creates Views/NetworkPageView.h/.cpp (+2 files); no MainWindow mount — the safest task in the
group and the owner of the honesty string.
- **CONFIRMED (G4)** — bare `Verified`/`Not verified`, gate-asserted. → §2.4 (N2 override).
- **CONFIRMED** — `net relay hops` fails on the shipped world (`relayPath[0].hopMs == 0` is
  correct and inside the I10 fingerprint). Override: "Do NOT edit Demo/DemoWorld.cpp —
  `relayPath[0].hopMs == 0` is correct (there is no hop to yourself) and its bytes are inside the
  I10 fingerprint at Startup.cpp:233. Change the `net relay hops` assertion to `hopsOk =
  (world.relayPath.size() == 3)` and, per node, `if (node.hopMs < 0 || node.glyph.empty() ||
  node.label.empty()) hopsOk = false;` — note `< 0`, not `<= 0`. Because I7 (Startup.cpp:319-322)
  already asserts size==3 and non-empty glyphs, the only new thing this line buys is the
  label/hop check, so say so in the printed QUERY. Separately, in N3's `MakeNode` and N6's
  Advanced per-hop line, render the hop figure ONLY when `node.hopMs > 0`, so node 1 does not
  display a fabricated-looking `0 ms`; update N6 Step 11 expectation 1 to two per-hop figures,
  and its deliverable to three visible lines, not four." (Paste the hop-figure sentence into N3's
  and N6's dispatches too — brief.py will not carry it.)
- **PARTIAL** — `net fmt rtt` restates the implementation. Override: "Replace the `net fmt rtt`
  assertion with a check against a locally built adversarial value, not against the
  implementation restated: build `urmsg::demo::ServerInfo probe{}; probe.latencyMs = 7;` inside
  the assertion block and assert `FormatRoundTrip(probe) == L"7 ms round trip" &&
  FormatRoundTrip(world.server) == FormatLatency(world.server.latencyMs) + L" round trip"`. The
  literal on the left is what makes it able to fail; build the probe LOCALLY and do not add a row
  to DemoWorld. Update Step 6's expected FAIL text and Step 8's expected PASS text to print the
  probe value."

### N3 — relay path, mounted (network.md:587-1012) · 2 findings
- **PARTIAL** — `NetworkBody()` does not exist. Override: "In Step 8, do NOT write
  `NetworkBody()`. The shell already landed the host: MainWindow.xaml:265 declares `<Grid
  x:Name="NetworkHost" Visibility="Collapsed" Style="{StaticResource UrPaneStyle}" />` […]. Write
  `BuildNetworkPage()` as `network_ = urmsg::views::MakeNetworkPage(urmsg::demo::GetWorld());
  NetworkHost().Children().Clear(); if (network_.root) { NetworkHost().Children().Append(
  network_.root); } else { urnw::LogWarn("window: MakeNetworkPage returned no root; the Network
  destination is empty"); }` — the body wiring.md W6 already specifies. There is no N1
  placeholder in ApplyStrings to delete; skip that half of Step 8. Before you commit, re-run
  mount_check.py on this brief and require exit 0." **Plus the §0 drift line:** re-point the
  network arm of ShowDestination (:743) from `StubPage()` to `NetworkHost()` and delete the
  interim comment for that arm.
- **REFUTED** (as claimed) — but the override sets the page's structure. Verbatim: "NetworkHost
  (MainWindow.xaml:265) is a bare `<Grid>` with UrPaneStyle — no pane header strip and no
  ScrollViewer, and shell.md:783 states that is deliberate. Drop N1 Step 10 items 5 and 6 and N3
  Step 11's phrase 'Under the NETWORK header strip'; judge from the top of the content area
  instead. Build the header and the scroller INSIDE `MakeNetworkPage`, not in MainWindow.xaml:
  make the returned `root` a two-row Grid whose row 0 is a `Border` styled `UrPaneHeaderStyle`
  containing a `UrPaneTitleStyle` TextBlock reading `NETWORK`, and whose row 1 is a
  `ScrollViewer` with `HorizontalScrollBarVisibility=Disabled`, `VerticalScrollBarVisibility=Auto`
  wrapping the `column` StackPanel." Structure sketch (tokens already in App.xaml —
  `UrPaneHeaderStyle` = 40 DIP, `UrSheetBrush` #151515, 1 px bottom hairline):
  ```xml
  <!-- root returned by MakeNetworkPage (Views/NetworkPageView.cpp) -->
  <Grid>                                   <!-- UrPaneStyle ground, page #101010 -->
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>       <!-- 40 DIP header -->
      <RowDefinition Height="*"/>
    </Grid.RowDefinitions>
    <Border Grid.Row="0" Style="{StaticResource UrPaneHeaderStyle}">
      <TextBlock Style="{StaticResource UrPaneTitleStyle}" Text="NETWORK"/>
    </Border>
    <ScrollViewer Grid.Row="1" HorizontalScrollBarVisibility="Disabled"
                  VerticalScrollBarVisibility="Auto">
      <StackPanel x:Name="column"/>        <!-- N3 relay path, N4 server group, N5 devices -->
    </ScrollViewer>
  </Grid>
  ```
  Per N2's override: in `MakeNode`, the hop figure renders only when `node.hopMs > 0` — the
  "This device" node shows no `0 ms`.

### N4 — message-server group (network.md:1013-1132) · 1 finding
- **CONFIRMED (G4)** — green bare `Verified`. → §2.4 (N4 override). Keep the colour rule
  (kUrGreen #87FB67 / danger #F8523B); the words carry the framing.

### N5 — your devices (network.md:1133-1343) · 1 finding
- **CONFIRMED** — Step 9's screenshot expectations contradict the fixture. Override: "Correct
  Step 9's expectations before you capture: the three rows read `This computer · Online` (Windows
  desktop), `Online` (Pixel 9 — it ships online=true, so its lastSeenLabel `2 min ago` is
  deliberately NOT shown), and `Last seen 3 days ago` (Linux laptop). Item 4 reads: TWO green
  `#87FB67` dots (Windows desktop and Pixel 9) and ONE faint `#5A5A5A` dot (Linux laptop). Do NOT
  edit Demo/DemoWorld.cpp to make the original text true — device `online` is inside the I10
  fingerprint (Startup.cpp:200)."

### N6 — Advanced Mode on the Network page (network.md:1344-end) · 2 findings
- **UNVERIFIED → ruled here** — the subscriber guard greps the wrong name. → §1.2 (probe
  override; N6 seeds only, adds no function and no registration).
- **CONFIRMED** — Step 12's expected log order is swapped against the mandated code. Override:
  "Fix Step 12's expected output to the order the mandated code actually emits: `network: relay
  pulse animate=true shouldAnimate=true loaded=false`, then `network: advanced mode -> true`,
  then `network: relay pulse begun on Loaded`. Do not reorder the LogInfo calls in
  `SetNetworkPageAdvanced` to match the brief — the pulse line belongs inside
  `SetRelayPathAnimated` where it reads the values it prints." Motion note (constraint: UrMotion
  gate): the wire pulse must read `ShouldAnimate()` (its log line already prints it), any pulse
  durations come from the UrMotion tokens (kMicroMs 90 / kFastMs 150 / kBaseMs 250 / kSlowMs 400,
  standard bezier (0.10,0.90)→(0.20,1.00)), and N6 Step 13's reduce-motion re-render is the only
  reduce-motion capture of this surface — do not skip it.

### S1 — pure strip rules (strip.md:7-362) · 1 finding
- **PARTIAL** — S1 re-declares the shell's 560 collapse rule under new names. Override: "Do NOT
  define `kStatusStripCollapseDip` or `ShouldShowStatusStrip` and do NOT add the `strip collapse`
  assertion. `urmsg::demo::kStripMinHeightDip` (Demo/DemoShellState.h:41) and `LayoutFor().strip`
  already own the 560 CONTENT-dip collapse rule, ApplyBreakpoint already consumes them, and
  Startup.cpp:359's `stripEdge` term already asserts that exact boundary. Ship only
  `kStatusStripHeightDip`, `StatusStateWord`, `StatusLockGlyph`, `StatusEpochValue`,
  `StatusRecordsValue` and the single `strip fields` assertion line."

### S2 — the 26 DIP strip (strip.md:363-1040) · 5 findings
Geometry already set by the brief and kept: a 26 DIP strip (`kStatusStripHeightDip`), full window
width, `UrSheetBrush` #151515 on page #101010 with a 1 px **top** hairline only; first mark is an
8 DIP `kUrGreen` dot centred in a 20 DIP host at a 16 DIP content inset.
- **PARTIAL** — Step 8 duplicates the host row + `StatusStripHost`. Override: "Skip Step 8
  entirely. MainWindow.xaml already has the third `<RowDefinition Height="Auto" />` (line 40) and
  `<Grid x:Name="StatusStripHost" Grid.Row="2" Visibility="Collapsed" />` (line 278). Add
  neither; mount into the existing host."
- **CONFIRMED** — Step 11's ApplyBreakpoint rewrite names members that don't exist and drops the
  rail term. Override: "Do NOT rewrite ApplyBreakpoint. It already computes `layout_ =
  urmsg::demo::LayoutFor(width, height)` on CONTENT dips (MainWindow.xaml.cpp:680), already
  carries `next.rail == layout_.rail` in its early-out, and already writes
  `StatusStripHost().Visibility(options_.enabled && layout_.strip ? ... )` at line 715. Change
  nothing in that function — there is no `demo_`, `wide_`, `stripVisible_` or
  `breakpointApplied_`; the members are `options_`, `layout_` and `layoutApplied_`."
- **CONFIRMED (G4)** — padlock named `Server key verified`. → §2.4 (S2 override).
- **CONFIRMED** — Steps 14/15 gate on window dips; ApplyBreakpoint logs content dips. Override:
  "Steps 14 and 15: do NOT add a `window: status strip -> ...` log line and do not expect one.
  ApplyBreakpoint already logs `window: layout wide=.. rail=.. strip=.. (content WxH dip)` in
  CONTENT dips, about 14 fewer than the window dips verify-render prints. Assert `strip=false` on
  that existing line for the no-switch launch, `strip=true` at 1560x900 (which logs `content
  1546x892`), and `strip=false` after the 1200x500 resize — never a window-dip figure."
- **PARTIAL** — the W6 mount collision. → §1.1 (resolution α: S2 keeps its mount; member stays
  `statusStrip_`; demo gate reads `options_.enabled`, not the nonexistent `demo_`).

### S3 — the preview drawer (strip.md:1041-1485) · 5 findings
Geometry kept: 320 DIP wide × 137 DIP tall (1 top hairline + 28 header + 3×36 rows), raised above
the strip from `StatusDrawerHost` (Grid.Row 1, bottom-left), drawer margin 16,0,0,0. Entrance at
kBaseMs 250, dismiss at kFastMs 150 (exits one step faster), both under `ShouldAnimate()`.
- **CONFIRMED** — `ToggleStatusDrawer` collision. → §1.1 (α: S3 owns it; W6 deletes its copy).
- **CONFIRMED (G4)** — unframed `RELAY PATH` header. → §2.4 (S3 override: `DEMO MODEL: RELAY
  PATH` in the same letterspaced chrome voice; no new colour or key).
- **PARTIAL** — Step 12's log gate reads across launches (append-mode log, 57 launches).
  Override: "Step 12: add `Remove-Item .localstate-verify\logs\urmessage-app.log
  -ErrorAction SilentlyContinue` as the first command of Step 10, before the verify-render
  launch. Log.cpp opens the file OPEN_ALWAYS and appends across launches (the current file holds
  57), so without the delete 'exactly one line for this run' can read green off a line an earlier
  attempt wrote." (This is plan G2's banned gate shape; the delete is the sanctioned repair.)
- **PARTIAL** — the shipped comment points at the wrong host. Override: "Step 2: in the same
  edit, replace MainWindow.xaml line 277's 'and its preview drawer opens ABOVE it inside this
  same host' with 'its preview drawer is hosted by StatusDrawerHost in Grid.Row 1, so raising it
  cannot grow this Auto row'. `git grep StatusDrawerHost` confirms the host itself is genuinely
  new, but the existing comment must not be left pointing at the wrong one."
- **PARTIAL** — "flush with the connect dot" is off by 6 DIP (dot ink sits at 22, slot at 16).
  Override: "Step 11: strike 'flush with the connect dot below it'. S2's MakeConnectDot centres
  an 8 DIP dot in a 20 DIP host at inset 16, so the dot's left edge is at 22 and the 16 DIP
  drawer margin puts the drawer 6 DIP to its LEFT. Expect instead: 'left edge 16 DIP from the
  window edge, aligned with the strip's content inset, i.e. with the left edge of the dot's 20
  DIP host'."

### S4 — Advanced fields on the strip (strip.md:1486-end) · 1 finding
- **PARTIAL** — the decision grep routes to a nonexistent lambda. → §1.2 (S4 override, quoted
  there). Six marks with five hairlines in the same 26 DIP row under `--demo-advanced`; four
  marks without; the appended fields come after the padlock.

### A2 — Developer session switches (advanced.md:7-339) · 2 findings
- **CONFIRMED** — the reduce-motion render has never executed on this machine; A2 is the only
  task that can look at it. Override: "Add a step 10b before the commit. Temporarily insert
  `urnw::motion::SetMotionOverride(false);` as the first line of the MainWindow constructor,
  rebuild, run `verify-render.ps1 -AppArgs "--demo=thread"`, and READ
  `.verify/urmessage-window-screen.png`: every conversation row and every message bubble must be
  at full opacity with no element stuck faded or offset, and the window must be fully revealed.
  Save it as `.verify/motion-off.png` and cite that filename in the deliverable. Then remove the
  temporary line and rebuild. This is the render ThreadView.cpp:83-88 says has never executed on
  this machine; A2 is the task that must look at it once."
- **CONFIRMED** — phantom `AdvancedModeDiagnostics()` / "A1". Override: "There is no
  `AdvancedModeDiagnostics()` in the tree — the Advanced Mode content in `CollectDiagnostics()`
  is an inline report line, not an assertion loop. Insert `for (auto& line :
  urmsg::demo::DeveloperSwitchDiagnostics()) lines.push_back(std::move(line));` immediately after
  the existing `for (auto& line : DemoWorldAssertions()) ...` loop in Startup.cpp, and A6's
  `WorldDumpDiagnostics()` loop immediately after that one. Strike every reference to 'A1' from
  A2's Interfaces block […]." Correction to the last sentence: Demo/AdvancedMode.h shipped from
  **F6**, not "wiring.md's W2" (plan:227-229 — W2 was dropped at assembly).

### A3 — Settings (advanced.md:340-976) · 4 findings
- **CONFIRMED** — `SettingsPage` paints zero pixels. Override: "Do NOT insert the Step-6 XAML.
  `SettingsHost` already exists at MainWindow.xaml:267 with Style=UrPaneStyle […]. In
  BuildSettings, mount into `SettingsHost()` and guard the append with `if (settings_.root)`.
  Delete step 9 entirely: ShowDestination has been rewritten by the shell task — there is no
  `const bool chats` local, no `StubPage()` visibility line and no `const auto label = (tag ==
  L"contacts") ? ...` tail to edit […]." **Plus the §0 drift line:** re-point the settings arm
  (:748-749) from `StubPage()` to `SettingsHost()`.
- **CONFIRMED (G4)** — green bare `Verified` for the server key. → §2.4 (A3 override; N2 lands
  first, A3 calls `urmsg::views::FormatKeyState`).
- **CONFIRMED** — duplicate options member, second parse, second init, second deep-link writer.
  Override: "Delete steps 7 and 8's `demoOptions_` member, the `demoOptions_ =
  ParseDemoOptions()` line, the `urmsg::InitAdvancedMode(...)` line and the whole
  `SelectDemoScreen` function and its declaration. Read `options_` (MainWindow.xaml.h) where you
  need the demo options; Advanced Mode is already seeded by `EnterDemoMode()` at
  MainWindow.xaml.cpp:468 via `DeepLinkFor(options_.screen).forceAdvanced` […]; and the deep link
  is already delivered by `DrainDeepLink()` -> `SelectNavTag(pendingLink_.navTag)` from the first
  SizeChanged, which is where it has to run."
- **CONFIRMED** — private `StyleByKey` copy. Override: "In BOTH Views/SettingsView.cpp and
  Views/DeveloperView.cpp, delete the private `StyleByKey` and its comment.
  `urnw::kit::StyleByKey` is exported at UrComponents.h:122 — the file already includes
  UrComponents.h, so call `kit::StyleByKey(L"...")` (as InspectRailView.cpp does) or add `using
  urnw::kit::StyleByKey;` (as ThreadView.cpp:41 does). A6's `FontFamilyByKey` is genuinely new
  and stays."

### A4 — the Advanced Mode gate (advanced.md:977-1173) · 2 findings
- **PARTIAL** — the positive control is unsatisfiable (there is no task A1). Override: "Replace
  step 6's positive control: there is no `AdvancedModeDiagnostics()` and no `advanced.notify`
  line in the tree — do not look for one. Prove the query and the path instead by writing the
  preference through the only writer there is: run `URMESSAGE_APP_ROOT=<scratch> URmessage.exe
  --diagnose` once to confirm NO app_prefs.json appears, then add a throwaway
  `urmsg::SetAdvancedModeEnabled(true)` call at the top of CollectDiagnostics, rebuild, re-run,
  and confirm `Select-String` finds `"advanced_mode":true` at `<scratch>\app_prefs.json`. Revert
  that line before step 7. Publish both outputs beside step 7's result; without them step 7 is
  not a gate."
- **CONFIRMED** — the second subscription. → §1.2 (A4 override, quoted there). A4 shrinks to the
  prefs gate; no MainWindow subscription code at all.

### A5 — Developer (advanced.md:1174-1723) · 4 findings
- **CONFIRMED** — `DeveloperPage` paints zero pixels. Override: "Do NOT insert the Step-6 `<Grid
  x:Name="DeveloperPage">`. `DeveloperHost` already exists at MainWindow.xaml:269 […]. In
  BuildDeveloper, mount into `DeveloperHost()` with `if (developer_.root)` guarding the append
  […] and drop step 8(e)'s ShowDestination edits […]." **Plus the §0 drift line:** re-point the
  developer arm (:745-747) from `StubPage()` to `DeveloperHost()`.
- **CONFIRMED** — duplicate `DeveloperNavItem`. Override: "Do NOT add a DeveloperNavItem. One
  already exists at MainWindow.xaml:134 (Tag="developer", Visibility="Collapsed", glyph E943 =
  Code) and its label is already written by `EnterDemoMode()` at MainWindow.xaml.cpp:487 as
  `kDemoNavDeveloper`. Delete step 6's FooterMenuItems replacement and step 8(b)'s
  `DeveloperNavItem().Content(...)` line entirely; keep the glyph the shell chose."
- **CONFIRMED** — the unpaired `Visibility` write (nav corruption). → §1.2 (A5-class-9 override,
  quoted there; the flip + PaneDisplayMode cycle lands inside **W7's** `ApplyAdvanced`, not in
  A5). A5's launch-time captures still work: DrainDeepLink:566 sets the launch visibility until
  W7 lands.
- **PARTIAL** — Step 10's 30-60 band fails against the seeded 66. Override: "In step 10, expect
  `Message rows` = **66**, not 'between 30 and 60'. `--diagnose` prints `demo world : 8
  conversations, 66 rows, 22 members, 3 devices`; that is the sum of `Conversation::rows.size()`
  and it is the same figure A6 step 5's `dump.world` line must show on both sides of its second
  pair. Do not touch DemoWorld to make a number match."

### A6 — the DemoWorld dump (advanced.md:1724-end) · 2 findings
- **CONFIRMED (G4)** — the cipher dump. → §2.4 (A6 cipher override).
- **PARTIAL (G4)** — unframed `key=verified` / `att=1` + clipboard. → §2.4 (A6 framing
  override). Also: A6's own step-5 expected `41 row lines` is wrong against the tree (66) — the
  completeness gate would fail its own documented output on first run; fix the expectation with
  the same 66 A5 uses.

### W5 — click graph, part 1 (wiring.md:7-320) · 5 findings
The highest-risk task in the set: it **deletes** live builders.
- **CONFIRMED** — the ListHost mount. → §2.1 (override quoted there).
- **PARTIAL** — BuildDemoViews is overwritten by three later builders. Override: "Add a Step 0 to
  W5: in app/src/App/MainWindow.xaml.cpp DELETE MainWindow::BuildThread(),
  MainWindow::BuildInspectRail() and MainWindow::OnConversationSelected() outright, delete their
  three declarations from MainWindow.xaml.h, delete the `BuildInspectRail(); BuildThread();`
  calls at MainWindow.xaml.cpp:150,152, and delete the whole `if
  (urmsg::demo::ParseDemoOptions().enabled) { ... }` demo branch of BuildConversationList
  (MainWindow.xaml.cpp:243-274) so that function keeps only its placeholder path. BuildDemoViews
  is the sole builder of list_, thread_ and rail_ from this task on. Re-run mount_check.py on
  MainWindow.xaml.cpp afterwards and confirm the two pre-existing ThreadBody writes are gone with
  it." **Append the R4 carry-over sentence from §3/R4** (the SetInspectRailAdvanced seeding line
  moves into BuildDemoViews). Built-in verification: mount_check's permanent exit-1 on
  MainWindow.xaml.cpp comes from those two ThreadBody writes — after Step 0 it should read clean.
- **CONFIRMED** (upgraded from UNVERIFIED by the audit's closing verifier) — three writers of the
  inspect selection. → §2.2 (override quoted there).
- **PARTIAL** — the c0-r23 pick. → §2.2 (override quoted there).
- **CONFIRMED** — Step 1 re-declares three members. Override: "In Step 1 add ONLY `#include
  <vector>`, `#include "Demo/DemoWorld.h"` and the six method declarations. Do NOT re-add list_,
  thread_ or rail_ -- MainWindow.xaml.h already declares them at lines 120, 67 and 80 -- and do
  NOT re-add Views/ConversationListView.h, Views/InspectRailView.h or Views/ThreadView.h, which
  are already included at lines 25-27. Add only the two genuinely new members, `std::wstring
  openConversationId_;` and `std::wstring selectedMessageId_;`."

### W6 — click graph, part 2 (wiring.md:321-523) · 3 findings → superseded
- **PARTIAL** — the drawer mount. Moot under §1.1 (S3 owns the drawer); the conditional override
  is on file in §1.1.
- **PARTIAL** — `ToggleStatusDrawer` / `strip_` / `drawerOpen_`. → §1.1 (W6 deletes them).
- **PARTIAL** — `network_` / `settings_` / `developer_`. → §1.1 (deleted; residual null-guard
  audit folds into W7's pre-flight; attribution fix: settings owner is **A3**).

### W7 — one subscriber, five surfaces (wiring.md:524-696) · 2 findings
- **PARTIAL** — Step 7's counted gate can't see a crossfade. Override: "Replace Step 7's second
  counted claim with one that a crossfade would actually move. Either (a) grep the source instead
  of the log: `git grep -c 'SetInspectRailMessage' -- app/src/App/MainWindow.xaml.cpp` must
  return 1, and that one call must sit inside MainWindow::SelectMessage -- ApplyAdvanced must
  contain zero SetInspectRailMessage and zero SetInspectRailConversation calls; or (b) have R2
  log one line inside RunCrossfade and count that. […]" Verifier correction to the override's
  closing rationale: `SetInspectRailMessage` **does** log (`rail: message mode ->`,
  InspectRailView.cpp:654) — so counting *that* string is a viable option (c); the defect is that
  the gate counts the wrong string, not that the mutation is invisible.
- **CONFIRMED** — Step 3's delete anchor names the wrong function. Override: "The line to delete
  is at MainWindow.xaml.cpp:566, inside DrainDeepLink, not EnterDemoMode. Delete it there and
  leave the NetworkNavItem().Visibility line and the LeftCompact/Auto PaneDisplayMode cycle below
  it untouched -- that cycle is the documented workaround for the WinAppSDK 2.2.0 nav corruption
  and it must still run after the last Visibility flip. After the edit, run `--demo=developer`
  and confirm the nav pane still shows TEXT LABELS beside Chats/Contacts/Network/Developer/
  Settings at 1560x900; if it renders icon-only, move ApplyAdvanced's DeveloperNavItem().
  Visibility write back behind the cycle instead of reverting the step."
- Plus §1.2: the probe guard pasted in; the fan-out names `statusStrip_`; the live-toggle limit
  stated in the report.

### W8 — the ambient module (wiring.md:697-1171) · 1 finding
- **CONFIRMED** — `ClockLabel()` reads the machine's wall clock (non-deterministic, wrong
  format). Override: "Do not call ::GetLocalTime. Delete ClockLabel and derive the ambient row's
  time from the conversation it is appended to: take the last RowKind::Message's timeLabel, parse
  its HH:MM, add (round + 1) * 3 minutes, and format that; then set `inspect.sentAtLabel` and
  `inspect.receivedAtLabel` with the same day prefix DemoWorld uses at DemoWorld.cpp:175
  (`L"Today " + row.timeLabel`), not the bare timeLabel. Add the determinism to the Step 5 gate:
  `IncomingForRound(conv, 0).timeLabel == IncomingForRound(conv, 0).timeLabel` is a tautology, so
  assert instead that round 0's label is strictly later than the conversation's last seeded
  message time and strictly earlier than round 1's."

### W9 — ambient wiring + the scroll pin (wiring.md:1172-end) · 3 findings
- **CONFIRMED** — §9.2 do-not-yank has no owner. → §2.3 (full override: widened Files list,
  `ShouldPinToBottom` pure extraction into ThreadView.h/.cpp, four-case --diagnose gate in
  Startup.cpp, delete the "NOT implemented" paragraph at ThreadView.cpp:1305-1311).
- **CONFIRMED** — Step 9's baseline is wrong by construction. Override: "Step 9's baseline is
  wrong: `--demo-autoplay` also triggers T6's two-row ambient SEED in DrainDeepLink
  (MainWindow.xaml.cpp:618-661), which appends c0-ambient-1 and c0-ambient-2 before any autoplay
  round. Do NOT compare against baseline-autoplay-off.png and do NOT delete the T6 seed. Instead
  assert what actually holds at ~1.2s: no typing indicator anywhere in the thread,
  `(Select-String -SimpleMatch 'demo: ambient message').Count` is 0, and `thread: ambient seed
  appended 2 rows` appears exactly once. Add the count of bubbles as baseline + 2, and state in
  the step that the +2 is T6's seed, not the loop."
- **CONFIRMED** (upgraded from UNVERIFIED) — the first RefreshOpenThread erases the T6 seed rows.
  Override: "Add to Step 4: when StartAmbientActivity is reached and `options_.autoplay` is set,
  the T6 seed rows (c0-ambient-1, c0-ambient-2) exist in the ThreadView only, so the loop's first
  RefreshOpenThread erases them. Fix it at the seed, not in W9's callbacks: change DrainDeepLink's
  T6 block to push `out` and `in` into
  `urmsg::demo::MutableWorld().conversations.front().rows` before calling AppendThreadRow, and
  update the comment at MainWindow.xaml.cpp:614-615 that says the world is not mutated. Then Step
  11's claim -- that the ambient rows survive a rebuild because they live in the world -- becomes
  true of every ambient row rather than only W8's." Constraint check: the fixture *file* stays
  byte-frozen (I10 fingerprints the seeded world at --diagnose time, before any window exists);
  this is a runtime append through the world's existing mutation seam — the same path W8's own
  rows take (wiring.md:1005, "Into the WORLD first").

## 4. Recommended landing order

Everything funnels through MainWindow.xaml.cpp (and Startup.cpp), so **serialize implementation**
(handoff §8). Within that, land in this order — the plan's dependency ordering, refined by the
audit edges:

1. **N2** — pure formatters, no MainWindow mount, +2 files. Unblocks the honesty chain: N4's row
   and A3's row both call its corrected `FormatKeyState`.
2. **S1** — pure strip rules; no MainWindow.
3. **A2** — session switches + the motion-off render (step 10b). Low collision
   (Demo/DeveloperSwitches + Startup.cpp). Optionally pull forward: it is the only task that ever
   renders the reduce-motion path.
4. **R4** — completes the rail; its seeding line must exist inside BuildInspectRail **before** W5
   deletes that function (carry-over note §3/R4). Lands the InitialRailMode probe.
5. **N1** (verify-only) → **N3** → **N4** → **N5** → **N6** — the Network page. N3 re-points the
   stub arm (§0). N6 seeds only (§1.2).
6. **S2** → **S3** → **S4** — the status strip and drawer (§1.1: S group owns mount and toggle).
7. **A3** → **A4** → **A5** → **A6** — Settings/Developer. A3 needs N2's formatter. A4 is now
   just the prefs gate. A5 needs A2's switches. A6's diagnostics loop goes after A2's.
8. **W5** — the big deletion; BuildDemoViews becomes sole builder; mount_check flips clean.
9. **W6** — superseded (§1.1); residual checks run as W7's pre-flight.
10. **W7** — the one subscriber (§1.2); every fan-out callee now exists (L6's list, S4's strip,
    N6's network, R4's rail, A5's Developer gate).
11. **W8** → **W9** — ambient module, then window wiring + the scroll pin (ThreadView editable
    for the first time since T6).

## 5. Payoff vs risk — for sequencing judgment

| Lands | Payoff (what the owner sees) | Risk |
|---|---|---|
| N2, S1, A2 | No pixels, but every later gate is real; A2 closes the never-rendered reduce-motion hole | Near zero — pure code, no mounts |
| R4 | The rail (on screen in every demo) finishes: device lists, density switch, the mode decision finally probed | Low-medium — one MainWindow line, but it must precede W5 |
| N3–N6 | The Network page — the URnetwork identity surface, first pixels on a blank canvas | Medium — mount discipline + the §0 stub re-point; mount_check enforced |
| S2–S4 | The connect indicator + drawer — the second identity surface, window-level | Medium — geometry/log gates were all re-derived above; honesty strings gate-asserted |
| A3–A6 | Settings/Developer leave the blank-pane class for good; A6 keeps the dump honest | Low (A4/A5 gutted of subscription work); A6 is honesty-critical, not risky |
| **W5** | The click graph actually works end-to-end | **Highest** — deletes three live builders; the R4 carry-over is the trap; verify by mount_check flipping clean and the `pendingLink_.selectMessage` count returning 1 |
| **W7** | One toggle drives five surfaces | **High** — consolidates four tasks' claims into one function next to the nav-corruption workaround; the live toggle is unverifiable headlessly (state the limit) |
| **W9** | Ambient activity that never yanks the reader and never deletes messages | Medium — ThreadView.cpp's append path changes under autoplay; four pure gate cases carry the proof |
| W6 | None (superseded) | None — that is the point |

**Honesty-critical, independent of order:** A6 (both overrides), N2/N4/A3 (one string, one
owner), S2 (padlock name), S3 (drawer header). A "correct" implementation of the raw briefs ships
a lie in each of these; the overrides and their gate updates land in the same commits, or the
tasks do not land at all.
