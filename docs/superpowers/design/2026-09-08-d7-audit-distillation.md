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
