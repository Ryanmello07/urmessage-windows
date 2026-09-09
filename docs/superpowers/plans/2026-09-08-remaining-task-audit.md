# Pre-flight audit of the 21 remaining demo-UI tasks

Generated 2026-09-08 from a 67-agent audit-then-verify workflow (run wf_463fb7e4-853).
Each finding was produced by an auditor reading the task brief against the real tree, then
handed to a SEPARATE verifier that re-ran the evidence command and tried to refute it.

**Read the verdict and the actual output before the claim.** CONFIRMED means the evidence
command printed what the auditor said it would. PARTIAL means real but narrower - the note
says how. REFUTED items are kept so nobody re-derives them. UNVERIFIED means the pairing
between auditor and verifier could not be reconstructed from the journal; treat as a claim.

Totals: 62 findings - 30 CONFIRMED, 27 PARTIAL, 2 REFUTED, 3 UNVERIFIED. **No task came back clean.**

Per task: A2 2, A3 4, A4 2, A5 4, A6 2, N1 5, N2 3, N3 2, N4 1, N5 1, N6 2, R4 8, S1 1, S2 5, S3 5, S4 1, W5 5, W6 3, W7 2, W8 1, W9 3

---

## Critical

### A3 - class 1 (stale mount / zero pixels) - **CONFIRMED**

**Claim.** A3 step 6 creates `<Grid x:Name="SettingsPage">` and steps 8-9 mount the Settings pane into it and toggle its Visibility, but ShowDestination routes tag "settings" to `SettingsHost()` — a Grid that already exists at MainWindow.xaml:267 — so SettingsPage is never made visible by any code path and the whole pane paints zero pixels.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'SettingsHost\|SettingsPage\|DeveloperHost\|DeveloperPage' app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'SettingsHost\|SettingsPage\|DeveloperHost\|DeveloperPage' app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.cpp

app/src/App/MainWindow.xaml:267:                <Grid x:Name="SettingsHost" Visibility="Collapsed"
app/src/App/MainWindow.xaml:269:                <Grid x:Name="DeveloperHost" Visibility="Collapsed"
app/src/App/MainWindow.xaml.cpp:743:    incoming = DeveloperHost();
app/src/App/MainWindow.xaml.cpp:746:    incoming = SettingsHost();

(SettingsPage / DeveloperPage: zero hits in either file.)

$ python .superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py .superpowers/sdd/2026-09-06-urmessage-demo-ui/task-A3-brief.md
== task-A3-brief.md ==
   SettingsPage         created by this task, position not explicit -- VERIFY ITS PARENT BY HAND

demo-collapsed elements, read from MainWindow.xaml.cpp: ThreadPane
EXIT=0

$ grep -n 'const bool chats\|StubPaneTitle().Text(label)\|tag == L"contacts") ? Loc' app/src/App/MainWindow.xaml.cpp
(exit 1 — no matches; step 9's four anchors do not exist)
```

**Verifier note.** CONFIRMED, and the mechanism is worse than the auditor stated.

WHAT REPRODUCES
- MainWindow.xaml:265-270 already declares NetworkHost/SettingsHost/DeveloperHost (added by W3, commit 9e1bf88, `git log -S SettingsHost` confirms). SettingsPage/DeveloperPage do not exist in the tree.
- ShowDestination (MainWindow.xaml.cpp:723-756) routes `tag == L"settings" && options_.enabled` to `incoming = SettingsHost()` at :746, then `CrossfadePageSwap(currentPage_, incoming)`. Nothing anywhere appends a child to SettingsHost today, so it is an empty UrPaneStyle Grid.
- task-A3-brief.md:438 step 6 adds `<Grid x:Name="SettingsPage" Visibility="Collapsed" />`; :520-521 step 8 does `SettingsPage().Children().Clear()/.Append(settings_.root)`; :541 step 9(a) writes `SettingsPage().Visibility(...)`. It compiles (SettingsNavItem exists at MainWindow.xaml:140, so step 8's SelectDemoScreen works), logs "window: settings pane built", selects the nav item, and shows the empty SettingsHost. Consequence confirmed.
- mount_check.py on the A3 brief prints exactly "VERIFY ITS PARENT BY HAND" and exits 0, as claimed.
- The defect is in the plan source too, not just the extracted brief: docs/superpowers/plans/tasks/advanced.md:349, 762, 772, 844-845, 865.

CORRECTION TO THE MECHANISM (makes it worse, not narrower)
The auditor's reasoning — "steps 8-9 toggle its Visibility, but ShowDestination routes elsewhere" — implies step 9 lands and is merely outranked. It does not land at all. Step 9's four anchors are gone: `const bool chats = (tag == L"chats");`, the `if (chats) {...return;}` block, and the `const auto label = (tag == L"contacts") ? Loc(...)` tail all return zero grep hits. W3 rewrote ShowDestination into the `FrameworkElement incoming` + CrossfadePageSwap form whose own comment says "nothing he

**Consequence if not overridden.** --demo=settings compiles, launches, logs 'window: settings pane built', selects the Settings nav item and shows a blank SettingsHost. Every step-10 expectation is read against a screenshot of nothing. mount_check exits 0 on this file because the grid carries no explicit Grid.Column — it printed VERIFY ITS PARENT BY HAND, and by hand it is wrong.

**Proposed dispatch override.** Do NOT insert the Step-6 XAML. `SettingsHost` already exists at MainWindow.xaml:267 with Style=UrPaneStyle, and MainWindow.xaml.cpp:745-747 already routes tag "settings" to it under `options_.enabled`. In BuildSettings, mount into `SettingsHost()` and guard the append with `if (settings_.root)`. Delete step 9 entirely: ShowDestination has been rewritten by the shell task — there is no `const bool chats` local, no `StubPage()` visibility line and no `const auto label = (tag == L"contacts") ? ...` tail to edit, and settings is already handled.

### A3 - class 3 (G4 honesty violation) - **CONFIRMED**

**Claim.** A3 step 3 renders the message-server key state as the bare words `Verified` / `Not verified` and colours the positive case `kUrGreen` (#87FB67), stating that a key verification RAN and PASSED, in a binary whose `ServerInfo::keyVerified` is a fabricated boolean in DemoWorld and where every other verification-flavoured string in the app carries prefix-first framing.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -rn 'Demo model:\|S(L"Verified")' app/src/App/Views/InspectRailFields.cpp app/src/App/Views/InspectRailView.cpp docs/superpowers/plans/tasks/advanced.md`

**Actual output.**
```
app/src/App/Views/InspectRailFields.cpp:85://  3. A polarity swap hidden from the token ("Demo model: unverified" against
app/src/App/Views/InspectRailFields.cpp:86://     "Demo model: not verified") passes with both forms reading negative.
app/src/App/Views/InspectRailFields.cpp:113:  return verified ? L"Demo model: verified" : L"Demo model: not verified";
app/src/App/Views/InspectRailView.cpp:410:  title.Text(L"Demo model: end-to-end encrypted");
app/src/App/Views/InspectRailView.cpp:418:  // "Demo model:" a THIRD time, and deliberately. This note is the only one of
app/src/App/Views/InspectRailView.cpp:423:  note.Text(L"Demo model: fabricated data, no crypto in this build");
docs/superpowers/plans/tasks/advanced.md:617:                            world.server.keyVerified ? S(L"Verified") : S(L"Not verified"));
```

**Verifier note.** The command reproduces the claim exactly: the plan emits bare `S(L"Verified")` while the two landed G4 strings both carry the "Demo model:" prefix. advanced.md:615-624 is inside A3 (task spans 340-976), in the unconditional PRIVACY AND SECURITY group, and applies `urnw::colors::MakeBrush(urnw::colors::kUrGreen)` on the positive branch.

I tried four ways to make it wrong; all four failed.

(1) Latent branch. Best rescue would have been that the green branch never renders. Dead: DemoWorld.cpp:390 is `w.server = {L"urmsg-01.ur.io", L"Iceland", 59, true};` — keyVerified is hard-coded true, so green "Verified" is the branch that ships on every launch.

(2) Guard elsewhere in A3. None. Grepping A3's body (lines 340-976) for `Demo model|AttestationLabel|framing|prefix|G4|fabricat|watermark` returns only step 4's "This demo" panel, whose honesty text sits behind a `Show` click — the same chip-dependent mitigation InspectRailView.cpp:355-372 explicitly ruled insufficient ("chip-dependent honesty disappears at exactly the moment the screen is being shown to people"). The auditor's cited precedent says what they claim it says, verbatim.

(3) A later step correcting it. The opposite: A3's own verification step (advanced.md:929-930) makes the defect the acceptance criterion — "row 4's right-hand value reads `Verified` in green `#87FB67`". The gate locks the wording in rather than catching it. This is not the network.md pattern where an early step pre-handles a later one.

(4) grep -r vs git grep artifact. Not applicable — `git ls-files` confirms all three paths are tracked; no build/ sweep is involved.

Two calibration notes, neither of which narrows the finding:
- It is BROADER than A3, not narrower. network.md:534 `FormatKeyState` returns the identical `L"Verified"`/`L"Not verifi

**Consequence if not overridden.** Settings ships a green 'Verified' beside 'Server key'. With --demo-watermark=off (or the watermark patched out for a presentation) nothing on screen says the value is fabricated, and the row reads as URmessage asserting a pinning check it never performed — the exact bargain InspectRailView.cpp:355-372 already refused for 'End-to-end encrypted'.

**Proposed dispatch override.** In step 3's Server key row, render `world.server.keyVerified ? S(L"Demo model: verified") : S(L"Demo model: not verified")` — prefix-first, matching `urmsg::views::AttestationLabel` (InspectRailFields.cpp:113) — and update step 10's expectation to those two strings. Do NOT write your own copy of this formatter: network.md N2's `FormatKeyState(bool)` is the designated one owner of this string and its gate currently asserts the bare words; that gate and this row must be corrected together and this row must then call `urmsg::views::FormatKeyState`.

### A5 - class 1 (stale mount / zero pixels) - **CONFIRMED**

**Claim.** A5 step 6 creates `<Grid x:Name="DeveloperPage">` and step 8 mounts the Developer pane into it, but ShowDestination routes tag "developer" to `DeveloperHost()` — a Grid that already exists at MainWindow.xaml:269 — so DeveloperPage is never shown and the Developer surface paints zero pixels.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'DeveloperHost\|DeveloperPage' app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.cpp docs/superpowers/plans/tasks/wiring.md`

**Actual output.**
```
app/src/App/MainWindow.xaml:269:                <Grid x:Name="DeveloperHost" Visibility="Collapsed"
app/src/App/MainWindow.xaml.cpp:743:    incoming = DeveloperHost();
docs/superpowers/plans/tasks/wiring.md:420:  DeveloperHost().Children().Clear();
docs/superpowers/plans/tasks/wiring.md:422:    DeveloperHost().Children().Append(developer_.root);
```

**Verifier note.** CONFIRMED — but note the evidence command does NOT settle it. It prints zero `DeveloperPage` hits because none of the three files it greps is the A5 file. It proves only the DeveloperHost half. The A5 half needs: grep -n 'DeveloperPage' .superpowers/sdd/2026-09-06-urmessage-demo-ui/task-A5-brief.md docs/superpowers/plans/tasks/advanced.md

Both halves verified independently:
- task-A5-brief.md:372 (inside Step 6, lines 333-374): `<Grid x:Name="DeveloperPage" Visibility="Collapsed" />`, introduced as "immediately after the `SettingsPage` grid A3 added".
- task-A5-brief.md:428-429 (Step 8(d)): `DeveloperPage().Children().Clear(); DeveloperPage().Children().Append(developer_.root);` inside BuildDeveloper().
- Identical text in the plan source at advanced.md:1530 and :1586-1587.
- MainWindow.xaml.cpp:742-743 `} else if (tag == L"developer" && options_.enabled) { incoming = DeveloperHost();` — the routed container is DeveloperHost.
- `grep -rn 'DeveloperPage' --include=*.xaml --include=*.cpp --include=*.h --include=*.idl --include=*.vcxproj .` exits 1: DeveloperPage exists in NO source file, only in plan docs. Mechanism and zero-pixel consequence hold exactly as claimed.

I tried four ways to make it wrong; none worked, and two made it worse:

(1) An in-brief guard like network.md's Step 1 grep? ABSENT. `grep -i 'grep\|Select-String\|mount_check\|whatever host\|if the shell'` over the A5 brief returns only two unrelated hits (window sizing at :496, watermark at :511). A5 has no host-name fallback.

(2) A later step that corrects it? No. Step 10 is a screenshot step but names only expected CONTENT, never the container, so it catches a blank pane only if the implementer actually looks. Its stated failure mode ("If the window opens on Chats instead...") points at constructor or

**Consequence if not overridden.** --demo=developer selects the Developer nav item, logs 'window: developer pane built', and shows an empty DeveloperHost. Step 10's ten-row DEMO STATE readout, both switches and A6's whole dump are verified against a blank pane. wiring.md W6 already mounts `developer_.root` into `DeveloperHost()`, so the two tasks would also mount the same view into two different containers.

**Proposed dispatch override.** Do NOT insert the Step-6 `<Grid x:Name="DeveloperPage">`. `DeveloperHost` already exists at MainWindow.xaml:269 and MainWindow.xaml.cpp:742-744 already routes tag "developer" to it. In BuildDeveloper, mount into `DeveloperHost()` with `if (developer_.root)` guarding the append — the same shape wiring.md W6 step 3 uses — and drop step 8(e)'s ShowDestination edits: the developer branch is already there.

### A6 - class 3 (G4 honesty violation) - **CONFIRMED**

**Claim.** A6 step 3's row-line format string emits `cipher="{}"` from `r.inspect.cipher`, which holds "XChaCha20-Poly1305"; InspectRailFields.h:26-27 records that this field is "deliberately emitted by NOTHING" and InspectRailView.cpp:372-378 records that a specific algorithm name is a claim about what encrypted the message whatever label sits above it.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'cipher' app/src/App/Views/InspectRailFields.h app/src/App/Views/InspectRailView.cpp app/src/App/Demo/DemoWorld.cpp docs/superpowers/plans/tasks/advanced.md`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'cipher' app/src/App/Views/InspectRailFields.h app/src/App/Views/InspectRailView.cpp app/src/App/Demo/DemoWorld.cpp docs/superpowers/plans/tasks/advanced.md

app/src/App/Views/InspectRailFields.h:26:// MessageInspect::cipher carries "XChaCha20-Poly1305" (DemoWorld.cpp:172) and is
app/src/App/Views/InspectRailFields.h:27:// deliberately emitted by NOTHING here. A named cipher is a claim about what
app/src/App/Views/InspectRailFields.h:118:// Extending this list is a G4 decision, not a layout one - MessageInspect::cipher
app/src/App/Views/InspectRailView.cpp:358:// "End-to-end encrypted" plus MessageInspect::cipher underneath, on the grounds
app/src/App/Views/InspectRailView.cpp:372:// AND THE CIPHER NAME IS NOT RENDERED AT ALL. MessageInspect::cipher holds
app/src/App/Views/InspectRailView.cpp:415:  // cipher; it answers "what am I looking at" instead, and it is what keeps the
app/src/App/Demo/DemoWorld.cpp:172:    n.cipher = L"XChaCha20-Poly1305";
docs/superpowers/plans/tasks/advanced.md:1907:          L"       epoch={} leaf={} ret={} size=\"{}\" wire={} att={} cipher=\"{}\" "

```

**Verifier note.** Every element of the claim reproduces verbatim, and the one figure I expected to break -- "66" -- is exactly right.

WHAT REPRODUCED. advanced.md:1907 emits `cipher="{}"` in the row-line continuation format; :1914 feeds it `r.inspect.cipher`. DemoWorld.cpp:172 sets that field to L"XChaCha20-Poly1305" unconditionally, outside any RowKind guard, so day separators and system rows carry it too -- a day separator that names the algorithm that encrypted it. The two prohibitions read exactly as claimed: InspectRailFields.h:26-31 "deliberately emitted by NOTHING here ... if a later task adds a 'Cipher' row it is a G4 violation on its own, whatever the label says", and InspectRailView.cpp:372-378 "A specific algorithm is a claim about what encrypted this message whatever label sits above it, this binary has no crypto to have used one".

THE COUNT IS 66, NOT AN ESTIMATE. c0=24 + c1=18 + 6 DMs x 4 = 66 RowSpec initializers, one `cipher="` per row line. Independently: A6's own step 5 prints an "expected" `41 row lines == 41 rows`, which is wrong against the tree -- so A6's completeness gate would FAIL its own documented output on first run. That is a second, separate defect in the same task.

THE ONE WAY I TRIED TO MAKE IT WRONG -- SCOPE -- AND WHY IT FAILS. The rail's rule is worded rail-locally ("emitted by NOTHING *here*", "adds a *'Cipher' row*"), and design §6.6 calls Developer "the honest place to put things that would otherwise tempt their way into the product surfaces", which reads like a carve-out for engineer-facing raw state. That defence dies on three facts: (1) G4 is a plan-wide constraint with its own heading in 2026-09-06-urmessage-demo-ui.md:158, not a rail rule; (2) InspectRailView.cpp:358-368 already litigated and REJECTED the context-dependent version of this mit

**Consequence if not overridden.** The one field the rail task fought to keep off screen appears 66 times on the Developer page and, via step 6's Copy button, on the clipboard — where no group header, no watermark and no surrounding page travels with it. Pasted into a chat or a slide, `cipher="XChaCha20-Poly1305"` is URmessage naming the algorithm that encrypted a message it never encrypted.

**Proposed dispatch override.** Delete `cipher=\"{}\"` from the row-line format string and `r.inspect.cipher` from its argument list. `MessageInspect::cipher` stays unrendered in this binary — InspectRailFields.h:26-27 and InspectRailView.cpp:372 are the standing ruling and the dump is not an exception to it. Add a one-line comment at the format string saying the field is deliberately omitted, so the next reader does not 'complete' the dump.

### A6 - class 3 (G4 honesty violation) - **PARTIAL**

**Claim.** A6 step 3's dump emits `key=verified`/`key=unverified` for `ServerInfo::keyVerified` and `att=1`/`att=0` for `MessageInspect::attestationVerified` with no framing at all, and step 6 puts a Copy-to-clipboard button on it, so those bare verification claims leave the app with nothing attached that says they are fabricated.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'keyVerified ? L"verified"\|int(r.inspect.attestationVerified)\|Copy the DemoWorld dump\|return verified ? L"Demo model' docs/superpowers/plans/tasks/advanced.md app/src/App/Views/InspectRailFields.cpp`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'keyVerified ? L"verified"\|int(r.inspect.attestationVerified)\|Copy the DemoWorld dump\|return verified ? L"Demo model' docs/superpowers/plans/tasks/advanced.md app/src/App/Views/InspectRailFields.cpp

docs/superpowers/plans/tasks/advanced.md:1870:                     world.server.keyVerified ? L"verified" : L"unverified");
docs/superpowers/plans/tasks/advanced.md:1914:          int(r.inspect.attestationVerified), r.inspect.cipher,
docs/superpowers/plans/tasks/advanced.md:2019:    automation::AutomationProperties::SetName(copy, S(L"Copy the DemoWorld dump"));
app/src/App/Views/InspectRailFields.cpp:113:  return verified ? L"Demo model: verified" : L"Demo model: not verified";
```

**Verifier note.** CORE CONFIRMED (clipboard path). advanced.md:1867-1870 emits `server {}  {}  {} ms  key={}` with "verified"/"unverified", and :1914 emits `att={int}`. Step 6 (:2019-2024) wires the copy button to `package.SetText(winrt::hstring{DumpDemoWorld()})` — the bare return value, no prefix, no banner. I read every format string in DumpDemoWorld (advanced.md:1858-1922): the emitted line tags are `world `, `server `, `device `, `relay `, `conv `, `  memb `, `    dev `, `  msg/day/sys `. The tokens "demo"/"DemoWorld" appear only in the FUNCTION NAME, never in the output text. So a pasted dump genuinely carries `key=verified` with nothing attached. The precedent the auditor cites is verbatim at InspectRailView.cpp:359-365 ("the chip is removed by --demo-watermark=off, and the watermark is patched out for presentations - so chip-dependent honesty disappears at exactly the moment the screen is being shown to people"), and InspectRailFields.cpp:104-113 refused the same bargain for this exact field. Step 4's WorldDumpDiagnostics (advanced.md:1928-1968) asserts only conv/row counts, byte-stability and non-emptiness — no framing property, so nothing in A6 catches this.

NARROWER THAN CLAIMED, three ways:

1. THE ON-SCREEN HALF OF THE CONSEQUENCE IS FALSE. "--demo-watermark=off removes it from the screen too" is wrong. On screen the dump sits under `kit::MakePaneGroupHeader(S(L"DEMOWORLD DUMP"))` (advanced.md:2006), inside a Developer pane reachable only under --demo-advanced. `grep -rn 'watermark' app/src/App/Views/` returns only two COMMENT lines (InspectRailFields.cpp:111, InspectRailView.cpp:361) — no view code reads the switch. DemoSwitches.cpp:60 sets only `o.watermark=false`, and :120-124 static_assert its non-contamination. So --demo-watermark=off strips the DEMO chip and nothing e

**Consequence if not overridden.** A pasted dump reads `server urmsg-01.ur.io  Iceland  38 ms  key=verified` and 66 rows of `att=1`. The DEMO chip is not in the clipboard, and --demo-watermark=off removes it from the screen too — this is the same chip-dependent-honesty bargain InspectRailView.cpp:359-365 rejected, minus even the chip.

**Proposed dispatch override.** Give the dump its own framing, in the text itself and not in the group header. Make `DumpDemoWorld()` open with a first line `demo model: fabricated world, no crypto and no protocol in this build; every value below is generated by Demo/DemoWorld.cpp`, and change the two claim-carrying tokens to `key=demo-model-verified` / `key=demo-model-not-verified` and `att=demo-model-verified` / `att=demo-model-not-verified`. Then update step 5's expected `dump.world` line and step 7's expected first two lines to match, and assert in `WorldDumpDiagnostics()` that the dump's first line contains `demo model:` so the framing cannot be dropped silently.

### N1 - class 1 (stale mount / zero pixels) - **PARTIAL**

**Claim.** N1's Step 1 guard fires (exit 0, seven hits) but Step 3 and Step 4 are conditioned on `NetworkPage`/`NetworkBody`, which the shell never created, so a literal reading of those steps adds a SECOND destination Grid that nothing ever makes visible, plus a second writer of `NetworkNavItem().Content` (already set at MainWindow.xaml.cpp:486).

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && git grep -n "NetworkNavItem\|NetworkPage\|NetworkHost\|NetworkBody\|ApplyDemoScreen" -- app/src/App`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && git grep -n "NetworkNavItem\|NetworkPage\|NetworkHost\|NetworkBody\|ApplyDemoScreen" -- app/src/App

app/src/App/MainWindow.xaml:126:                <muxc:NavigationViewItem x:Name="NetworkNavItem" Tag="network"
app/src/App/MainWindow.xaml:265:                <Grid x:Name="NetworkHost" Visibility="Collapsed"
app/src/App/MainWindow.xaml.cpp:486:  NetworkNavItem().Content(box_value(hstring{kDemoNavNetwork}));
app/src/App/MainWindow.xaml.cpp:538:  // re-adding only NetworkNavItem's reproduced icon-only for every item
app/src/App/MainWindow.xaml.cpp:560:  // NetworkNavItem/DeveloperNavItem in code and Append/InsertAt-ing them into
app/src/App/MainWindow.xaml.cpp:565:  NetworkNavItem().Visibility(Visibility::Visible);
app/src/App/MainWindow.xaml.cpp:740:    incoming = NetworkHost();

(7 lines, exit 0. Brief's own Step-1 pattern, without NetworkBody, prints the same 7 — `git grep -c NetworkBody` and `git grep -n NetworkPaneTitle` both exit 1, zero hits. `grep -r` on the same pattern returns 33 because it sweeps app/build artifacts; the auditor used git grep, so that discrepancy is not in pla
```

**Verifier note.** The guard fires exactly as claimed (7 hits, exit 0, vs Step 1's stated "no output at all (exit 1)"). But both named consequences are wrong, and the real residue sits somewhere else.

REFUTED - the "second destination Grid". Step 1's guard does not stop at "read them"; it continues: "add only what is missing in each step below, and wherever this task writes `NetworkBody()` use whatever host element that shell named instead (its plan calls it `NetworkHost`)" (task-N1-brief.md:52-53). It names `NetworkHost` verbatim - the exact element the shell created at MainWindow.xaml:265. This is the network.md pattern the brief uses deliberately, and it pre-empts precisely the duplicate-Grid outcome. Step 5 carries its own matching guard ("If step 1 showed the wiring surface has already rewritten ShowDestination, add only the two `network` lines"), and the shell already has that arm at :740, gated on `options_.enabled`.

REFUTED - "everything N3-N6 mounts renders nowhere". N3's `BuildNetworkPage` appends into `NetworkBody()` (task-N3-brief.md:371-372). Under the guard N1 never creates `NetworkBody`, so N3 is a compile error, not silent invisibility. The claimed consequence only follows on the branch where the guard is ignored - which contradicts the claim's own premise that the guard fires.

REFUTED - the `NetworkNavItem().Content` consequence. Real as literal text, but inert, and the cited evidence is misquoted:
- `kDemoNavNetwork` is `L"Network"` (MainWindow.xaml.cpp:47) - byte-identical to Step 4's literal. Same string.
- `ApplyStrings()` runs at MainWindow.xaml.cpp:95, `EnterDemoMode()` at :109. The Step-4 write lands FIRST and is overwritten at :486 with the same value. On a non-demo launch EnterDemoMode never runs, but NetworkNavItem is `Visibility="Collapsed"` in markup (:126-

**Consequence if not overridden.** A `NetworkPage`/`NetworkBody` Grid is added beside `NetworkHost`. ShowDestination never shows it, so everything N3-N6 mounts renders nowhere, and `NetworkNavItem().Content` gets a second writer against the shell's fix-round comment at MainWindow.xaml.cpp:484-487 that says Content is set there and only there.

**Proposed dispatch override.** SKIP Step 3 and Step 4 entirely. Do NOT add the `NetworkPage` Grid, `NetworkPaneTitle`, `NetworkBody`, the ScrollViewer, or any line to `ApplyStrings()`. The shell owns all of it: `NetworkNavItem` is at MainWindow.xaml:126, `NetworkHost` at :265, and `NetworkNavItem().Content(box_value(hstring{kDemoNavNetwork}))` is already set in `EnterDemoMode()` at MainWindow.xaml.cpp:486. Treat every step of N1 whose condition names `NetworkPage` or `NetworkBody` as satisfied by `NetworkHost` and add nothing.

### N1 - class 2 (duplicates shell-owned state) - **REFUTED**

**Claim.** N1 Step 5 instructs a full-body replacement of `MainWindow::ShowDestination` with a Visibility-writing version; the shell already rewrote that function (now at MainWindow.xaml.cpp:723) to route through `urnw::motion::CrossfadePageSwap` and it already carries the `network` arm, plus `developer`, `settings`, `options_.enabled` gating and `currentTag_`/`currentPage_` tracking.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '723,755p' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '723,755p' app/src/App/MainWindow.xaml.cpp

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
 
```

**Verifier note.** The shell half of the claim is true and reproduces verbatim: ShowDestination at MainWindow.xaml.cpp:723 does route through urnw::motion::CrossfadePageSwap, does already carry the `network` arm (incoming = NetworkHost()), plus `developer` and `settings` arms, `options_.enabled` gating, currentTag_/currentPage_ tracking, and the comment "nothing here writes Visibility itself". The auditor read the shell correctly.

The brief half is wrong, and it is wrong in the same sentence the auditor quoted. Step 5 is NOT an unconditional full-body replacement. task-N1-brief.md:128-131 (verbatim above, identical at docs/superpowers/plans/tasks/network.md:119-122) reads: "Replace the whole body of `MainWindow::ShowDestination` ... **If step 1 showed the wiring surface has already rewritten `ShowDestination`, add only the two `network` lines to whatever is there and leave the rest alone.**" The auditor appears to have quoted the first clause and stopped before the guard.

The guard is not hypothetical on this tree — it fires. Step 1's discovery grep (task-N1-brief.md:73-79) is `git grep -n "NetworkNavItem\|NetworkPage\|NetworkHost\|ApplyDemoScreen" -- app/src/App`, and on this worktree it prints 7 hits at exit 0, against a stated clean-tree expectation of "no output at all (exit 1)". Two of those hits are in MainWindow.xaml (:126 NetworkNavItem, :265 NetworkHost), which is the exact trigger Step 1 names: "If it prints hits in `MainWindow.xaml`, the wiring surface's demo shell is already in: read them, add only what is missing in each step below, and wherever this task writes `NetworkBody()` use whatever host element that shell named instead (its plan calls it `NetworkHost`)." A third hit, MainWindow.xaml.cpp:740 `incoming = NetworkHost();`, sits inside the rewritten ShowDestination itse

**Consequence if not overridden.** The page-change transition (design doc 7's page-change row), the Developer and Settings destinations, and the demo-gating all disappear; the function's own comment 'nothing here writes Visibility itself' is violated and both pages get drawn on top of each other on the first swap.

**Proposed dispatch override.** Do NOT replace `ShowDestination`. Step 5's escape clause applies and Step 1's grep cannot show it, so this is the ruling: the shell rewrote `ShowDestination` at MainWindow.xaml.cpp:723 and it ALREADY has the network arm (`else if (tag == L"network" && options_.enabled) { incoming = NetworkHost(); header = hstring{kDemoNavNetwork}; }` at :740-742). Add nothing to it. Leave `CrossfadePageSwap`, `currentTag_` and `currentPage_` exactly as they are.

### N2 - class 3 (G4 honesty violation) - **CONFIRMED**

**Claim.** `FormatKeyState` is mandated to return the bare words "Verified" / "Not verified" for the message-server key, and the `net fmt keystate` gate asserts those bare words — but this binary contains no crypto and the established house pattern is prefix-first (`InspectRailFields.cpp:113` ships "Demo model: verified", `InspectRailView.cpp:410` ships "Demo model: end-to-end encrypted").

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && git grep -n "Demo model" -- app/src`

**Actual output.**
```
app/src/App/Views/InspectRailFields.cpp:85://  3. A polarity swap hidden from the token ("Demo model: unverified" against
app/src/App/Views/InspectRailFields.cpp:86://     "Demo model: not verified") passes with both forms reading negative.
app/src/App/Views/InspectRailFields.cpp:113:  return verified ? L"Demo model: verified" : L"Demo model: not verified";
app/src/App/Views/InspectRailView.cpp:410:  title.Text(L"Demo model: end-to-end encrypted");
app/src/App/Views/InspectRailView.cpp:418:  // "Demo model:" a THIRD time, and deliberately. This note is the only one of
app/src/App/Views/InspectRailView.cpp:423:  note.Text(L"Demo model: fabricated data, no crypto in this build");
```

**Verifier note.** Both halves reproduce, and all three falsification attempts failed.

HOUSE PATTERN (evidence command, above): three distinct shipped "Demo model:" prefixes in R1/R2 — an attestation value, a section title, and a standing note. Prefix-first is the established pattern, not a one-off.

THE MANDATE IS BARE, in four places in docs/superpowers/plans/tasks/network.md:
- :347-348 header contract — `// true -> "Verified", false -> "Not verified"`
- :533-534 reference impl — `return keyVerified ? std::wstring(L"Verified") : std::wstring(L"Not verified");`
- :413-414 the gate — `Check(FormatKeyState(true) == L"Verified" && FormatKeyState(false) == L"Not verified")`
- :562 the expected-PASS line prints the bare words AS the evidence of correctness: `net fmt keystate : PASS  FormatKeyState(true|false) -> "Verified" | "Not verified"`

FALSIFICATION 1 — a later corrective step (the network.md Step-1-grep pattern the prompt warned about). Failed. I grepped every occurrence of "verified" in network.md (13 hits). There is no Step-1 grep guard on N2 and no later step that re-words the string. The opposite: N4's deliverable at :1113 restates the bare copy as the expected render — "`Server key` / `Verified` in green `#87FB67`, or `Not verified` in `#F8523B`". N2 and N4 agree on the bare form.

FALSIFICATION 2 — framing supplied by the surrounding chrome. Failed. network.md:1041 builds the section as `MakePaneGroupHeader(L"MESSAGE SERVER", world.server.host)`; the row is `MakePaneKeyValueRow(L"Server key", ...)`. No "Demo model" anywhere in the group, so the value is unframed in its own pane.

FALSIFICATION 3 — the branch never renders / DEMO chip covers it. Failed on both counts. app/src/App/Demo/DemoWorld.cpp:390 is `w.server = {L"urmsg-01.ur.io", L"Iceland", 59, true};` — keyVerified is T

**Consequence if not overridden.** The Network page (and Settings, via advanced.md:615-617) tells a viewer the message server's key WAS verified. With `--demo-watermark=off`, or with the owner's own watermark patched in while presenting, the DEMO chip is gone and that claim stands alone — a G4 violation, and the gate certifies it.

**Proposed dispatch override.** Change `FormatKeyState` to the prefix-first form this codebase already ships: `return keyVerified ? std::wstring(L"Demo model: verified") : std::wstring(L"Demo model: not verified");` — matching `InspectRailFields.cpp:113` character for character in shape. Update the `net fmt keystate` assertion to `FormatKeyState(true) == L"Demo model: verified" && FormatKeyState(false) == L"Demo model: not verified"`, and update the Step 8 expected output line to match. Do not ship the bare words in any label, any automation name, or any gate.

### N3 - class 1 (stale mount / zero pixels) - **PARTIAL**

**Claim.** N3 step 8 mounts the entire Network page into `NetworkBody()`, an x:Name that does not exist in MainWindow.xaml; the shell landed the host as `NetworkHost` (MainWindow.xaml:265) and `ShowDestination` (MainWindow.xaml.cpp:740) crossfades to `NetworkHost()` only.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && python .superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py .superpowers/sdd/2026-09-06-urmessage-demo-ui/task-N3-brief.md; echo EXIT=$?`

**Actual output.**
```
== task-N3-brief.md ==
   NetworkBody          *** MISSING from MainWindow.xaml ***

demo-collapsed elements, read from MainWindow.xaml.cpp: ThreadPane
EXIT=1
```

**Verifier note.** THE DEFECT ITSELF IS CONFIRMED, CLAUSE BY CLAUSE. `git grep -n "network_\|BuildNetworkPage\|NetworkBody\|NetworkHost" -- app/src` returns exactly two lines: `app/src/App/MainWindow.xaml:265: <Grid x:Name="NetworkHost" Visibility="Collapsed" Style="{StaticResource UrPaneStyle}" />` (a bare self-closing Grid, no children) and `app/src/App/MainWindow.xaml.cpp:740: incoming = NetworkHost();`. Both auditor line numbers are exact. No `NetworkBody`, no `NetworkPage`, no `network_`, no `BuildNetworkPage` anywhere in app/src. N3's brief (:371-372) writes `NetworkBody().Children().Clear()/.Append(network_.root)`.

I TRIED THREE WAYS TO MAKE IT WRONG; ALL THREE FAILED.
(a) The N1-style Step-1 grep guard that progress.md:162 credits: `grep -n "NetworkHost\|git grep\|wiring surface" task-N3-brief.md` returns only three unrelated prose hits. The N3 brief never says the word NetworkHost. N1's guard text — "wherever THIS TASK writes NetworkBody() use whatever host element that shell named instead" — is scoped to N1's own writes and lives only in task-N1-brief.md:52-53. It does not reach N3. progress.md's systemic audit (:157-163) checked T3, R2, A2/A5, N1 and S; it never audited N3, so this is a real gap in that audit, not a re-report.
(b) N3's one conditional, step 7 line 361: "If the wiring surface has already added a `network_` member and a builder, add neither — skip to step 9". Skipping to step 9 would skip the bad step 8. But that guard is false today: W6 (task-W6-brief.md:96-98, which correctly mounts `NetworkHost().Children().Append(network_.root)`) has not landed — `network_` and `BuildNetworkPage` are absent from app/src, and git log's last commit is d57089a (R3), with the W5-W9 group still pending.
(c) A tracked-vs-untracked or tool-artifact discrepancy: none. `git status --

**Consequence if not overridden.** N3, N4, N5 and N6 all compile, log `window: network page built`, pass every `--diagnose` line and every log gate, and paint ZERO pixels — `--demo=network` shows the empty #101010 NetworkHost. Four features, exactly the T3/ThreadBody failure shape.

**Proposed dispatch override.** In Step 8, do NOT write `NetworkBody()`. The shell already landed the host: MainWindow.xaml:265 declares `<Grid x:Name="NetworkHost" Visibility="Collapsed" Style="{StaticResource UrPaneStyle}" />` and MainWindow.xaml.cpp:740 crossfades to `NetworkHost()`. Write `BuildNetworkPage()` as `network_ = urmsg::views::MakeNetworkPage(urmsg::demo::GetWorld()); NetworkHost().Children().Clear(); if (network_.root) { NetworkHost().Children().Append(network_.root); } else { urnw::LogWarn("window: MakeNetworkPage returned no root; the Network destination is empty"); }` — the body wiring.md W6 already specifies. There is no N1 placeholder in ApplyStrings to delete; skip that half of Step 8. Before you commit, re-run mount_check.py on this brief and require exit 0.

### N4 - class 3 (G4 honesty violation) - **CONFIRMED**

**Claim.** N4 Step 4 renders `FormatKeyState(world.server.keyVerified)` as the `Server key` row value and paints it `kUrGreen` #87FB67; the shipped world sets `keyVerified = true` (DemoWorld.cpp:390), so the page always shows the word "Verified" in green.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n "w.server = " app/src/App/Demo/DemoWorld.cpp && git grep -n "Demo model" -- app/src/App/Views`

**Actual output.**
```
$ grep -n "w.server = " app/src/App/Demo/DemoWorld.cpp
390:  w.server = {L"urmsg-01.ur.io", L"Iceland", 59, true};

$ git grep -n "Demo model" -- app/src/App/Views
app/src/App/Views/InspectRailFields.cpp:85://  3. A polarity swap hidden from the token ("Demo model: unverified" against
app/src/App/Views/InspectRailFields.cpp:86://     "Demo model: not verified") passes with both forms reading negative.
app/src/App/Views/InspectRailFields.cpp:113:  return verified ? L"Demo model: verified" : L"Demo model: not verified";
app/src/App/Views/InspectRailView.cpp:410:  title.Text(L"Demo model: end-to-end encrypted");
app/src/App/Views/InspectRailView.cpp:418:  // "Demo model:" a THIRD time, and deliberately. This note is the only one of
app/src/App/Views/InspectRailView.cpp:423:  note.Text(L"Demo model: fabricated data, no crypto in this build");

(both exit 0; the "--- exit ---" markers in my transcript were my own echoes, not command output)

Corroborating reads, all from the same worktree:
  app/src/App/Demo/DemoWorld.h:100-105  struct ServerInfo { host; jurisdiction; int latencyMs; bool keyVerified; }
    -> the 4th initialiser at DemoWorld.cpp:390 IS keyVerified, and it is `true`.
  a
```

**Verifier note.** Every link in the chain reproduces literally. ServerInfo's 4th field is keyVerified (DemoWorld.h:104), DemoWorld.cpp:390 sets it true, N2 Step 7 (network.md:534) makes FormatKeyState(true) return exactly L"Verified", N4 Step 4 (network.md:1077-1081) puts that string in the `Server key` row and paints it kUrGreen, and UrColors.h:70 is #87FB67. The page always reads `Server key  Verified` in green.

I looked for the four escapes and none holds:
1. No later step corrects it. `grep -rn "Server key" docs/ app/src/` returns only network.md:1078/1113, advanced.md:615 and strip.md:526-527 — N5 and N6 add devices and Advanced Mode, neither touches the wording.
2. No Step-1-grep guard. N1 Step 1's git grep (network.md:38) is a mount check for NetworkNavItem/NetworkPage/NetworkHost — nothing about copy.
3. The demo chip does not cover it. MainWindow.xaml.cpp:489-490 gates DemoChip on options_.watermark, and DemoSwitches.cpp:60 implements `--demo-watermark=off`, so the chip can be off while the row still says Verified. The repo's own comment (InspectRailFields.cpp:104-114) makes exactly this argument and refuses to lean on the chip.
4. The Settings honesty panel does not cover it either: advanced.md Step 4 builds it Collapsed-by-default behind a "Show" button on a different destination.

Three corrections to the auditor's framing, all sharpening rather than shrinking the finding:

(a) Step 7 is NOT the binding lock. Step 7 is a human screenshot read. The machine lock is one task earlier: network.md:412-415, N2's --diagnose line, asserts `Check(FormatKeyState(true) == L"Verified" && FormatKeyState(false) == L"Not verified")`, and N2 Step 8 requires `net fmt keystate : PASS ... -> "Verified" | "Not verified"` before N2 may commit. So rewording in N4 alone flips a committed build gate

**Consequence if not overridden.** A green 'Verified' beside 'Server key' is the strongest honesty signal on the page and it asserts a verification that never happened. Step 7's acceptance text ('`Server key` / `Verified` in green `#87FB67`') locks the violation into the gate, so nothing downstream can catch it.

**Proposed dispatch override.** The `Server key` row value must be the prefix-first string from the corrected `FormatKeyState` — `Demo model: verified` / `Demo model: not verified` — not the bare words. Keep the colour rule as written (kUrGreen / DangerBrush) since the words carry the meaning either way, and rewrite Step 7's expectation 3 to read: `Server key` / `Demo model: verified` in green `#87FB67`. Do not render `MessageInspect::cipher` anywhere on this page under any label.

### S2 - class 2 (duplicates shell-owned state) - **PARTIAL**

**Claim.** Step 8 tells the implementer to add a third `<RowDefinition Height="Auto" />` to RevealRoot and a `<Grid x:Name="StatusStripHost" Grid.Row="2" />`; MainWindow.xaml already carries both (line 40 and line 278).

**Evidence command.** `grep -n 'RowDefinition\|x:Name="StatusStripHost"' app/src/App/MainWindow.xaml | head -6`

**Actual output.**
```
$ grep -n 'RowDefinition\|x:Name="StatusStripHost"' app/src/App/MainWindow.xaml | head -6
38:        <Grid.RowDefinitions>
39:            <RowDefinition Height="Auto" />
40:            <RowDefinition Height="*" />
41:            <RowDefinition Height="Auto" />
42:        </Grid.RowDefinitions>
174:                        <Grid.RowDefinitions>

NOTE: the auditor's own `head -6` truncates before the StatusStripHost line, so the command as written does NOT show the second half of its own claim. Re-run without head -6:
38:        <Grid.RowDefinitions>
39:            <RowDefinition Height="Auto" />
40:            <RowDefinition Height="*" />
41:            <RowDefinition Height="Auto" />
42:        </Grid.RowDefinitions>
174..178, 223..226, 252..255: (unrelated nested grids)
278:        <Grid x:Name="StatusStripHost" Grid.Row="2" Visibility="Collapsed" />

$ grep -n '</muxc:NavigationView>\|x:Name="RevealRoot"' app/src/App/MainWindow.xaml
37:    <Grid x:Name="RevealRoot">
273:        </muxc:NavigationView>

$ python .superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py .../task-S2-brief.md
== task-S2-brief.md ==
   StatusStripHost      ok (ancestors: RevealRoot < WindowPlate < Wi
```

**Verifier note.** The defect is REAL and is the same stale-brief class progress.md already logged for T3/R2/A2, but it is narrower than claimed on both consequence prongs.

WHAT REPRODUCES. task-S2-brief.md:454-476 (Step 8, "Add the host row to MainWindow.xaml") instructs the implementer to add a 3-row RowDefinitions block to RevealRoot AND to add `<Grid x:Name="StatusStripHost" Grid.Row="2" Visibility="Collapsed" />` after `</muxc:NavigationView>`. Both are already in app/src/App/MainWindow.xaml: the 3-row block at lines 38-42, StatusStripHost at line 278. Provenance confirms this is staleness, not prior S2 work — `git log -S` shows StatusStripHost arrived in 9e1bf88, the W3 shell commit, and S2 has demonstrably not run (no Views/StatusStripView.*, no StatusStrip entries in App.vcxproj, no BuildStatusStrip/statusStrip_ in MainWindow.xaml.h/.cpp). Two further stale-anchor tells in the same step: the brief says `</muxc:NavigationView>` is on "line 201" when it is on 273, and the existing StatusStripHost at 278 carries W3's comment, not the brief's.

NO GUARD ANYWHERE. I tried four ways to make the claim wrong and all four fail:
1. Steps 1-2 are gates, but on verify-render.ps1 `-AppArgs` and Demo/DemoSwitches.h only — neither touches MainWindow.xaml.
2. Step 9 DOES carry an "already exists" guard ("First decide whether `demo_` already exists — another surface may have landed it", with a Select-String and a skip instruction). It covers MainWindow.xaml.h, not Step 8's XAML. This strengthens rather than rescues the finding: the author guarded the adjacent cross-surface case one step later and missed this one.
3. Steps 10-16 are wiring/build/launch/commit; nothing corrects Step 8.
4. mount_check.py — the REQUIRED pre-flight per progress.md's DECISION line — I RAN IT on the S2 brief: exit 0, "S

**Consequence if not overridden.** Duplicate x:Name is an XamlCompiler error, so the surface does not build at all; if the implementer 'fixes' it by keeping only the row definition, RevealRoot gets four rows and the strip row stops being the last one.

**Proposed dispatch override.** Skip Step 8 entirely. MainWindow.xaml already has the third `<RowDefinition Height="Auto" />` (line 40) and `<Grid x:Name="StatusStripHost" Grid.Row="2" Visibility="Collapsed" />` (line 278). Add neither; mount into the existing host.

### S2 - class 9 (plan text that does not build) - **CONFIRMED**

**Claim.** Step 11's ApplyBreakpoint rewrite names `breakpointApplied_`, `wide_`, `stripVisible_` and `demo_` — none of which exist (the members are `layoutApplied_`, `layout_`, `options_`) — and its replacement early-out drops the `next.rail == layout_.rail` term the live function carries.

**Evidence command.** `git grep -n "breakpointApplied_\|stripVisible_\|next.rail == layout_.rail" -- app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ git grep -n "breakpointApplied_\|stripVisible_\|next.rail == layout_.rail" -- app/src/App/MainWindow.xaml.cpp
app/src/App/MainWindow.xaml.cpp:681:  if (layoutApplied_ && next.wide == layout_.wide && next.rail == layout_.rail &&
(exit 0 — exactly one match; zero hits for breakpointApplied_ and zero for stripVisible_)

Cross-check with plain `grep -rn` on the same path: identical single line, so this is not a git-grep-vs-grep-r artifact.
```

**Verifier note.** Both halves reproduce, and both stated consequences hold exactly.

LIVE (MainWindow.xaml.cpp:669-685, MainWindow.xaml.h:110/127/134):
  const auto next = urmsg::demo::LayoutFor(width, height);
  if (layoutApplied_ && next.wide == layout_.wide && next.rail == layout_.rail &&
      next.strip == layout_.strip)
    return;
  layout_ = next;
  layoutApplied_ = true;
Members are `urmsg::demo::DemoOptions options_{}` (h:110), `urmsg::demo::Layout layout_{}` (h:127), `bool layoutApplied_ = false` (h:134). `git grep "\bwide_\b\|\bbreakpointApplied_\b" -- app/src` returns NOTHING — the shell task's rename (docs/superpowers/plans/tasks/shell.md:846: "members `layout_` and `layoutApplied_` (replacing `wide_` and `breakpointApplied_`)") has already landed.

BRIEF Step 11 (task-S2-brief.md:555-582, identical text at docs/superpowers/plans/tasks/strip.md:916-919):
  if (breakpointApplied_ && wide == wide_ && strip == stripVisible_) return;
  wide_ = wide; stripVisible_ = strip; breakpointApplied_ = true;

Compile break: CONFIRMED via `breakpointApplied_` and `wide_` (undeclared identifiers). Two further stale anchors compound it: Step 11 says "In `MainWindow::ApplyBreakpoint()` (line 164)" — it is at line 669 — and instructs adding `const double height = root.ActualHeight();` after `if (width <= 0) return;`, but the live function already declares `height` at 676 and already reads `if (width <= 0 || height <= 0) return;`, so a literal apply is also a redeclaration error.

Rail regression: CONFIRMED with the numbers. DemoShellState.h: kWideBreakpointDip=1000 (UrComponents.h:75), kRailBreakpointDip=1500, kStripMinHeightDip=560, kRailWidthDip=360. A 1560→1200 window-dip resize is ~1546→~1186 content dips (the file's own frame allowance, cpp:672-674). wide: true→true. strip: height unchan

**Consequence if not overridden.** Applied literally the file does not compile. Applied 'in spirit' it regresses the shipped inspector rail: a 1560→1200 dip resize would hit the early-out and leave the 360 DIP rail standing below kRailBreakpointDip, which design 6.5a forbids.

**Proposed dispatch override.** Do NOT rewrite ApplyBreakpoint. It already computes `layout_ = urmsg::demo::LayoutFor(width, height)` on CONTENT dips (MainWindow.xaml.cpp:680), already carries `next.rail == layout_.rail` in its early-out, and already writes `StatusStripHost().Visibility(options_.enabled && layout_.strip ? ... )` at line 715. Change nothing in that function — there is no `demo_`, `wide_`, `stripVisible_` or `breakpointApplied_`; the members are `options_`, `layout_` and `layoutApplied_`.

### S2 - class 3 (G4 honesty violation) - **CONFIRMED**

**Claim.** Step 6 names the padlock `Server key verified` / `Server key not verified`, and the padlock is the strip's only statement about key verification — it is a bare FontIcon with no caption and no value beside it.

**Evidence command.** `git grep -n "Server key verified\|Demo model: verified" -- docs/superpowers/plans/tasks/strip.md app/src/App/Views/InspectRailFields.cpp`

**Actual output.**
```
$ git grep -n "Server key verified\|Demo model: verified" -- docs/superpowers/plans/tasks/strip.md app/src/App/Views/InspectRailFields.cpp

app/src/App/Views/InspectRailFields.cpp:113:  return verified ? L"Demo model: verified" : L"Demo model: not verified";
docs/superpowers/plans/tasks/strip.md:526:constexpr wchar_t kKeyVerifiedName[] = L"Server key verified";

(The auditor's own pattern under-reports: "Server key not verified" does not contain the substring "Server key verified", so line 527 is missed. Confirmed separately —
docs/superpowers/plans/tasks/strip.md:527:constexpr wchar_t kKeyUnverifiedName[] = L"Server key not verified";)
```

**Verifier note.** Every clause of the claim holds, and four refutation attempts failed.

WHAT THE BRIEF ACTUALLY DOES. Constants defined in Step 4 (strip.md:526-527), applied in Step 6 (strip.md:768-771) as the FontIcon's AutomationProperties Name. The padlock at strip.md:756-772 is a raw `Controls::FontIcon` appended straight to `fields` — it never goes through `kit::MakeStatusField`, so it has no caption and no value, unlike the `server` field two lines above it. strip.md:764 states the defect in its own words: "it is the ONLY statement of key verification anywhere on the strip, so it is content, and it is named."

REFUTATION 1 — a later step corrects it. Failed. `grep -n "Server key\|lock\|Lock" strip.md` across all four sub-tasks: 3/4 (drawer) adds only kDrawerName/kRelayHeader and relay-node rows; 4/4 (Advanced) appends epoch / session / rec/s and explicitly says "They come AFTER the padlock" (strip.md:1540). No later step touches the lock's name. Verification steps at 951, 1408, 1655, 1665 only count marks.

REFUTATION 2 — a Step 1 gate already handles it (the network.md pattern). Failed. strip.md has four Step 1 gates (lines 39, 403, 1065, 1509); they gate on `ConnectState`, verify-render.ps1 arg forwarding, the relay path, and AdvancedMode.h. None greps copy.

REFUTATION 3 — G4 already binds it. Failed, and this makes it worse. `docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md:158` G4 reads: "No copy in the demo may state that a message *was* encrypted as a fact about a real operation." Read literally, G4 covers message encryption, not key verification — so nothing in the plan tree catches this string. `git grep "Demo model" -- docs/` returns EMPTY: the honesty convention exists only in landed code (InspectRailFields.cpp), never propagated back into any brief.

REFUTATION 4

**Consequence if not overridden.** The strip asserts, as the closed padlock's only framing, that a server-key verification ran and returned a result. No key check exists in this binary; `world.server.keyVerified` is a fixture boolean. This is the identical claim AttestationLabel already refuses to make, and --demo-watermark=off (DemoSwitches.cpp:60) removes the DEMO chip exactly when the owner is presenting.

**Proposed dispatch override.** In Step 6 set `kKeyVerifiedName[] = L"Demo model: server key verified"` and `kKeyUnverifiedName[] = L"Demo model: server key not verified"` — the same prefix-first framing AttestationLabel ships at InspectRailFields.cpp:113 for the same reason recorded there: the bare words 'state that a check RAN and returned a result', and the value must carry its own framing rather than lean on a chip --demo-watermark=off removes.

### W5 - class 1 (stale mount / zero pixels) - **CONFIRMED**

**Claim.** W5 Step 2 mounts the demo conversation list into ListHost, which is declared Visibility="Collapsed" in MainWindow.xaml:213 and is never made Visible by any line of code, so RebuildConversationList paints zero pixels; mount_check reports it "ok" because it only reads Collapsed writes out of the .cpp and cannot see a XAML-declared Collapsed that nothing ever flips.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'ListHost' app/src/App/MainWindow.xaml docs/superpowers/plans/tasks/wiring.md && git grep -c 'ListHost()' -- app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'ListHost' app/src/App/MainWindow.xaml docs/superpowers/plans/tasks/wiring.md && git grep -c 'ListHost()' -- app/src/App/MainWindow.xaml.cpp

app/src/App/MainWindow.xaml:213:                    <Grid x:Name="ListHost" Grid.Column="0" Visibility="Collapsed"
docs/superpowers/plans/tasks/wiring.md:95:  ListHost().Children().Clear();
docs/superpowers/plans/tasks/wiring.md:100:  ListHost().Children().Append(list_.root);

(grep rc=0; git grep rc=1 -- ZERO matches, no output, so the && chain ends there)

--- corroboration, all run in the same worktree, HEAD d57089a, working tree clean ---

$ grep -rn 'ListHost()' app/          # sweeps build artifacts too, per the shell-fact caveat
app/src/App/x64/Release/MainWindow.xaml.g.h:253:        ::winrt::Microsoft::UI::Xaml::Controls::Grid ListHost()
  -> the generated ACCESSOR exists (so it compiles); still zero callers anywhere.

$ sed -n '200,214p' app/src/App/MainWindow.xaml
   <!-- UNUSED. The demo's ConversationListView briefly landed
        here while EnterDemoMode collapsed ListScaffold above;
        ... Left declared (Collapsed, nev
```

**Verifier note.** CONFIRMED, and the tree documents its own history of the divergence.

WHAT THE OUTPUT SHOWS. MainWindow.xaml:213 declares ListHost Collapsed, in Grid.Column="0", the same column as the live ListScaffold (MainWindow.xaml:173). `git grep -c 'ListHost()' -- app/src/App/MainWindow.xaml.cpp` returns NOTHING (exit 1): the .cpp does not name ListHost even once, so there is no Visibility write and no Append. The only two references left in the repo are wiring.md:95 and :100 -- W5 Step 2's mount.

I TRIED THE OBVIOUS REBUTTAL AND IT FAILS. shell.md:629 (task W3, the task that creates the host) does contain `ListHost().Visibility(Visibility::Visible);` inside EnterDemoMode, and W3 runs before W5 -- that would have been the guard. But W3 AS LANDED deliberately deleted it. `.superpowers/.../review-6081c81..6615bd4.diff` shows commit 6615bd4 removing all three lines together: `-  ListHost().Visibility(Visibility::Visible);`, `-  ListHost().Children().Clear();`, `-  ListHost().Children().Append(list_.root);`. progress.md:117 records why: collapsing ListScaffold was taking the search row and the "CONVERSATIONS 8" header down with it, so the demo rows were rerouted into the shipped ConversationList and ListHost was left as a dead stub. wiring.md was never updated to match. So the guard exists only in a plan doc describing code that no longer exists.

WHY THE GATE SAYS ok. mount_check prints `demo-collapsed elements, read from MainWindow.xaml.cpp: ThreadPane` -- one element, derived entirely from `X().Visibility(Visibility::Collapsed)` writes in the .cpp. ListHost is invisible by XAML attribute, never by a .cpp write, so it is structurally outside the gate's input. Exactly as claimed. Worse, progress.md:166 cites `exit 0 on wiring.md (all seven mounts ... ListHost ... resolve to live co

**Consequence if not overridden.** The list W5 builds -- the one carrying SelectConversation -- is appended to a permanently collapsed Grid sitting in the same Grid.Column=0 as ListScaffold. The frame still looks correct (BuildConversationList fills ConversationList a moment later), every W5 step-7 expectation passes, and the click graph this task exists to build is invisible and unreachable. This is defect class 1 shipping for the fifth time, past a mount_check that exits 0.

**Proposed dispatch override.** In Step 2 do NOT mount into ListHost. ListHost is Visibility="Collapsed" at MainWindow.xaml:213, its own XAML comment says "never made Visible, nothing ever appended to it again", and no code writes ListHost().Visibility. Replace `ListHost().Children().Clear(); ... ListHost().Children().Append(list_.root);` with `auto rows = ConversationList().Children(); rows.Clear(); ... rows.Append(list_.root);` and also write `ListPaneCount().Text(...)`, exactly as the existing demo branch of BuildConversationList does (MainWindow.xaml.cpp:243-253). Re-run mount_check after the edit and note that its "ok" on ListHost was a false pass.

### W5 - class 8 (cross-task conflict) - **PARTIAL**

**Claim.** wiring.md contains zero occurrences of BuildConversationList, BuildThread, OnConversationSelected or BuildInspectRail, yet all three builders run in the MainWindow constructor AFTER EnterDemoMode (MainWindow.xaml.cpp:150-152) and each reassigns the very members BuildDemoViews just set -- list_ with a callback into OnConversationSelected, thread_ with callbacks that only call SetThreadSelectedMessage.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -c 'BuildConversationList\|BuildThread\|OnConversationSelected\|BuildInspectRail' docs/superpowers/plans/tasks/wiring.md && sed -n '148,153p' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ grep -c 'BuildConversationList\|BuildThread\|OnConversationSelected\|BuildInspectRail' docs/superpowers/plans/tasks/wiring.md
0
---exit:1---

$ sed -n '148,153p' app/src/App/MainWindow.xaml.cpp
  // BuildConversationList(): that reintroduces the unreachable-rail failure this
  // comment was originally written about.
  BuildInspectRail();
  BuildConversationList();
  BuildThread();

--- corroborating reads ---
MainWindow.xaml.cpp:109   EnterDemoMode();          (wiring.md Step 6 puts BuildDemoViews() inside it, before :494 pendingLink_ = link;)
MainWindow.xaml.cpp:227   void MainWindow::BuildConversationList()  -> demo branch gated ON ParseDemoOptions().enabled:
                            list_ = MakeConversationList(world, [..](int index){ self->OnConversationSelected(index); });
                            list.Append(list_.root);   // into ConversationList() -- the VISIBLE ring
                            OnConversationSelected(0);
MainWindow.xaml.cpp:297   void MainWindow::BuildThread()  -> if (!options_.enabled) return;
                            thread_ = MakeThread([..](std::wstring id){ SetThreadSelectedMessage(self->thread_, id);
                                       
```

**Verifier note.** CORE DEFECT IS REAL AND REPRODUCES EXACTLY. grep -c returns 0 (exit 1); :150-152 are BuildInspectRail(); BuildConversationList(); BuildThread(); EnterDemoMode is called at :109 and its pendingLink_ assignment is at :494, so wiring.md Step 6's BuildDemoViews() runs inside it, ~40 lines before the three builders. All three run under --demo (BuildConversationList's demo branch is gated ON enabled; the other two early-return only when NOT demo) and each reassigns the member BuildDemoViews just set. The click graph is dead exactly as claimed: the visible list's callback is OnConversationSelected, which writes the name into ThreadBody() -- and BuildThread's own comment states ApplyBreakpoint collapses ThreadPane under --demo, so that is zero pixels -- and the visible thread's bubble callback only calls SetThreadSelectedMessage + logs "thread: bubble selected", never touching the rail.

I TRIED FOUR WAYS TO REFUTE IT; ALL FAILED. (1) No network.md-style Step 1 grep guard: the only DELETE in wiring.md's 1440 lines is at :586, in the W7/advanced section, removing a DeveloperNavItem visibility line. (2) No later step corrects it. (3) The git grep vs grep -r artifact-sweep trap does not apply -- the target is a single tracked doc, and the names are absent under either tool. (4) The builders are not inert under --demo.

WHERE THE CLAIM IS NARROWER THAN STATED (why PARTIAL, and it matters):
(a) "SelectConversation, SelectMessage and ClearMessageSelection are never reached" is wrong as written. They are never reached FROM A CLICK, but all three still run: wiring.md Step 6 calls SelectConversation(0) and SelectMessage(it->id) directly from DrainDeepLink, and Step 5 (:204) calls ClearMessageSelection() from ApplyBreakpoint.
(b) Worse for the project, not better: because those Select* f

**Consequence if not overridden.** BuildDemoViews runs first and is entirely overwritten. Clicking a row calls OnConversationSelected (which writes the conversation name into ThreadBody inside the demo-collapsed ThreadPane) and clicking a bubble logs "thread: bubble selected" without touching the rail. SelectConversation, SelectMessage and ClearMessageSelection are never reached from any click, so W9 Step 10's `window: conversation ->` and `window: message inspect` counts are vacuously 0 and its §9.2 evidence proves nothing.

**Proposed dispatch override.** Add a Step 0 to W5: in app/src/App/MainWindow.xaml.cpp DELETE MainWindow::BuildThread(), MainWindow::BuildInspectRail() and MainWindow::OnConversationSelected() outright, delete their three declarations from MainWindow.xaml.h, delete the `BuildInspectRail(); BuildThread();` calls at MainWindow.xaml.cpp:150,152, and delete the whole `if (urmsg::demo::ParseDemoOptions().enabled) { ... }` demo branch of BuildConversationList (MainWindow.xaml.cpp:243-274) so that function keeps only its placeholder path. BuildDemoViews is the sole builder of list_, thread_ and rail_ from this task on. Re-run mount_check.py on MainWindow.xaml.cpp afterwards and confirm the two pre-existing ThreadBody writes are gone with it.

### W5 - class 8 (cross-task conflict) - **UNVERIFIED**

**Claim.** W5 gives --demo=inspect two more writers: BuildInspectRail's `if (options_.screen == DemoScreen::Inspect)` branch (MainWindow.xaml.cpp:413-419) whose own source comment says in capitals the wiring task must delete it, and DrainDeepLink's existing `if (pendingLink_.selectMessage && thread_.root) SetThreadSelectedMessage(thread_, kInspectTargetRowId);` (MainWindow.xaml.cpp:584-585) -- W5 Step 6 adds a third selectMessage handler in the same function without removing either.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'WIRING TASK MUST DELETE\|kInspectTargetRowId\|pendingLink_.selectMessage' app/src/App/MainWindow.xaml.cpp && grep -c 'BuildInspectRail' docs/superpowers/plans/tasks/wiring.md`

**Consequence if not overridden.** Three writers of one state inside one launch. The rail is put on PickInspectMessage's c0-r12 by BuildInspectRail, the outline on c0-r12 by DrainDeepLink's existing line, then W5's loop re-points both to c0-r23 -- and the source comment states plainly that nothing in --diagnose probes the rail's MODE, so the regression is silent and only a person reading a screenshot can see it.

**Proposed dispatch override.** Before adding anything in Step 6, DELETE the `if (options_.screen == urmsg::demo::DemoScreen::Inspect) { ... }` branch and its LogWarn from MainWindow::BuildInspectRail (MainWindow.xaml.cpp:413-419) -- the comment above it orders exactly this -- and DELETE the existing `if (pendingLink_.selectMessage && thread_.root) urmsg::views::SetThreadSelectedMessage(thread_, urmsg::demo::kInspectTargetRowId);` at MainWindow.xaml.cpp:584-585. After Step 6 there must be exactly ONE `pendingLink_.selectMessage` branch in the whole file; verify with `git grep -c 'pendingLink_.selectMessage' -- app/src` returning 1.

### W6 - class 1 (stale mount / zero pixels) - **PARTIAL**

**Claim.** W6 Step 2 appends only `strip_.root` to StatusStripHost and never appends `strip_.drawer` to StatusDrawerHost; wiring.md contains zero occurrences of StatusDrawerHost, while strip.md Step 7 hosts the drawer there because it must draw in the row ABOVE the strip.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -c 'StatusDrawerHost' docs/superpowers/plans/tasks/wiring.md && grep -n 'StatusDrawerHost' docs/superpowers/plans/tasks/strip.md`

**Actual output.**
```
$ cd .../worktrees/demo-ui && grep -c 'StatusDrawerHost' docs/superpowers/plans/tasks/wiring.md && grep -n 'StatusDrawerHost' docs/superpowers/plans/tasks/strip.md
0

That is the ENTIRE output. `grep -c` printing 0 exits with status 1, so `&&` short-circuited and the second grep never ran. Re-run with `;` separators:

=== wiring.md count ===
0
exit=1
=== strip.md matches ===
1052:XAML: `<Grid x:Name="StatusDrawerHost" Grid.Row="1" VerticalAlignment="Bottom" HorizontalAlignment="Left" />`
1086:        <Grid x:Name="StatusDrawerHost" Grid.Row="1"
1264:  StatusDrawerHost().Children().Clear();
1265:  StatusDrawerHost().Children().Append(statusStrip_.drawer);
exit=0

git grep (tracked) and grep -r agree; grep -r adds only the identical .superpowers/sdd/ task-S3-brief.md copy. No count discrepancy.

=== who produces ToggleStatusDrawer ===
strip.md:1051:On `MainWindow` (`MainWindow.xaml.h`, private): `void ToggleStatusDrawer();`
strip.md:1257:  void ToggleStatusDrawer();
strip.md:1273:void MainWindow::ToggleStatusDrawer() {
wiring.md:330:- Produces: `void MainWindow::ToggleStatusDrawer();` members `strip_`, ...
wiring.md:366:  void ToggleStatusDrawer();
wiring.md:430:void MainWindow::Togg
```

**Verifier note.** The two greps reproduce exactly, but the claimed CONSEQUENCE is refuted, and the real defect is a different and louder one.

WHAT THE AUDITOR MISSED (two things).

1. A shell fact: their own command never ran its second half. `grep -c` printing `0` exits 1, so `&&` short-circuited. They saw only `0` and never saw strip.md's four hits.

2. A scope fact: hosting the drawer is not W6's job. wiring.md legitimately contains zero `StatusDrawerHost` because S3 owns that element end-to-end. strip.md S3 Step 2 adds the `<Grid x:Name="StatusDrawerHost">` to MainWindow.xaml (line 1086), Step 7 mounts it (`docs/superpowers/plans/tasks/strip.md:1264-1265`: `StatusDrawerHost().Children().Clear(); StatusDrawerHost().Children().Append(statusStrip_.drawer);`), and the plan's dependency-ordering table (`docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md:206,217,219` -- "the ordering is a dependency ordering, not a preference") puts Status strip S1-S4 BEFORE Click graph W5-W9. So when W6 runs, a drawer is already in the visual tree. There is no "element in no visual tree, zero pixels" window as described.

THE REAL DEFECT (narrower than claimed in one way, worse in another).

S3 and W6 both PRODUCE `MainWindow::ToggleStatusDrawer()`. strip.md:1051 (Produces), :1257 (Step 6 declares it in MainWindow.xaml.h private), :1273 (Step 7 defines it, keyed on `statusStrip_`). wiring.md:330 (Produces), :366 (Step 1 declares it again), :430 (Step 4 defines it again, keyed on `strip_`/`drawerOpen_`). W6's body contains no guard -- grepping wiring.md lines 321-524 for statusStrip_, S3, BuildStatusStrip, "already", "3/4" or "skip" returns nothing, unlike network.md's Step-1-grep pattern. Both also wire the strip Button's Click to it (strip.md:1266, wiring.md:385).

Since S3 lands first, a W6 agent f

**Consequence if not overridden.** A W6-built window calls SetStatusStripDrawerOpen on a drawer element that is in no visual tree. The click is accepted, the log prints "window: status drawer open", every W6 assertion passes, and nothing is painted -- the exact compile-launch-log-success-zero-pixels shape this project has shipped four times.

**Proposed dispatch override.** If any part of Step 2 survives, it must also host the drawer: after appending strip_.root, add `StatusDrawerHost().Children().Clear(); if (strip_.drawer) StatusDrawerHost().Children().Append(strip_.drawer); else urnw::LogWarn("window: demo status strip has no drawer; activation will paint nothing");`. A drawer that is toggled but never parented renders zero pixels while the log says it opened.


---

## Important

### A2 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** A2 introduces the motion override that ThreadView.cpp:88 names as "the first task that can" execute the reduce-motion render, but A2's only render step (step 10) runs with NO override set — the path that already worked — and no step in A2, A3, A4, A5 or A6 ever renders the app with animation forced off, because the only way to set the override is A5's ToggleSwitch and A4 states the agent may not synthesise input.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n "A2's motion override is the first task that can" -B 8 app/src/App/Views/ThreadView.cpp && grep -rn 'motion' app/src/App/Demo/DemoSwitches.cpp`

**Actual output.**
```
$ grep -n "A2's motion override is the first task that can" -B 8 app/src/App/Views/ThreadView.cpp
80-  // and an empty table IS the reduce-motion path.
81-  //
82-  // What that does NOT buy, stated because an earlier version of this comment
83-  // overclaimed it: the `plan.empty()` branch below is still deletable, and
84-  // deleting it leaves a reduce-motion bubble at Opacity(0.0) with a residual
85-  // transform — permanently invisible — while --diagnose still prints PASS.
86-  // No pure gate can reach that; only looking at the reduce-motion render can,
87-  // and this machine reports SPI_GETCLIENTAREAANIMATION = 1, so that path has
88:  // never executed here. A2's motion override is the first task that can.
=== EXIT: 0 ===
--- second grep ---
$ grep -rn 'motion' app/src/App/Demo/DemoSwitches.cpp
(no output)
=== EXIT: 1 ===

NOTE: the second grep is aimed at the wrong file and proves nothing on its own — A2 never touches DemoSwitches.cpp; it creates Demo/DeveloperSwitches.cpp. (Re-run case-insensitively for 'motion|animat': still no output, exit 1.) The load-bearing evidence is this instead:

$ grep -rn "SetMotionOverride" --include=*.md .
task-A2-brief.md:25,59,81,216,218
```

**Verifier note.** CONFIRMED — and understated. Every leg reproduces, and the four refutation avenues I tried all closed.

WHAT I CHECKED AND WHY IT DIDN'T SAVE THE BRIEF:

1. "A later step in the same file corrects it" (the network.md Step-1-grep pattern). A2 has ELEVEN steps and `grep -n "grep" task-A2-brief.md` returns nothing — there is no gate step. Step 10 is the last render step and its own text is dispositive: "No override is set on a normal launch, so `WindowReveal` must run exactly as before this task." Step 11 is the commit. So A2 renders only the path that already worked.

2. "A headless way to set the override exists." No. `SetMotionOverride` appears at exactly two sites across every .md in the tree: A2's own probe and A5:302's `Toggled` handler. `grep -rni "demo-motion"` across all .md returns nothing, and `grep -rn "Modify app/src/App/Demo/DemoSwitches"` returns nothing — no task ever adds a motion switch to the `--demo` parser. verify-render.ps1 passes only `-AppArgs` to `Start-Process` (:39, :164) and has no click/SendInput/UIAutomation, so A5's toggle is unreachable, exactly as A4:34 says.

3. "A5's own render steps cover it." They don't. A5 step 10 (`--demo=developer`) explicitly expects `Motion` = `on (Windows)` and the Animations switch **on**; the toggle is seeded `toggle.IsOn(urnw::motion::ShouldAnimate())`, which is true on this machine. A5 step 11 renders `--demo=settings` twice. All three captures are animations-ON.

4. "Another task family covers the reduce-motion render." Only N6 step 13 does (turn OFF Windows Accessibility > Animation effects, re-run), and it renders the NETWORK page — it asserts the relay-pulse log line, not a thread bubble. T6-review:420 hoped the coverage would come "for free at W9 ... and at A2"; W9's brief contains zero matches for motion

**Consequence if not overridden.** The `dev.motion` line asserts ShouldAnimate() returns the override — true by construction from step 2 — and the screenshot asserts the unchanged path. Nothing looks at what the override actually does. ThreadView.cpp:83-86 records the concrete failure this leaves uncovered: a reduce-motion bubble left at Opacity(0.0) with a residual transform, permanently invisible, while --diagnose still prints PASS. The same shape is live in ConversationListView.cpp:289 vs :329.

**Proposed dispatch override.** Add a step 10b before the commit. Temporarily insert `urnw::motion::SetMotionOverride(false);` as the first line of the MainWindow constructor, rebuild, run `verify-render.ps1 -AppArgs "--demo=thread"`, and READ `.verify/urmessage-window-screen.png`: every conversation row and every message bubble must be at full opacity with no element stuck faded or offset, and the window must be fully revealed. Save it as `.verify/motion-off.png` and cite that filename in the deliverable. Then remove the temporary line and rebuild. This is the render ThreadView.cpp:83-88 says has never executed on this machine; A2 is the task that must look at it once.

### A3 - class 2 (duplicates shell-owned state) - **CONFIRMED**

**Claim.** A3 step 7-8 declare `demoOptions_`, call `ParseDemoOptions()` a second time in the constructor, call `InitAdvancedMode()` a second time, and add `SelectDemoScreen()` — all four already exist on MainWindow as `options_` (parsed once, line 94), `InitAdvancedMode(options_.advanced || link.forceAdvanced)` in EnterDemoMode (line 468), and the `SelectNavTag`/`DrainDeepLink` deep-link path, whose own comment states that running the nav selection from the constructor does not work because NavigationView's markup `IsSelected="True"` wins at load.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'options_ = urmsg::demo::ParseDemoOptions\|urmsg::InitAdvancedMode\|void MainWindow::SelectNavTag\|pendingLinkArmed_ = true\|running from the constructor instead put' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'options_ = urmsg::demo::ParseDemoOptions\|urmsg::InitAdvancedMode\|void MainWindow::SelectNavTag\|pendingLinkArmed_ = true\|running from the constructor instead put' app/src/App/MainWindow.xaml.cpp

94:  options_ = urmsg::demo::ParseDemoOptions();
468:  urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced);
495:  pendingLinkArmed_ = true;
502:void MainWindow::SelectNavTag(std::wstring_view tag) {
523:  // settled by the first SizeChanged; running from the constructor instead put

--- corroborating (not in the auditor's command) ---
$ git grep -n 'InitAdvancedMode' -- app/
app/src/App/Demo/AdvancedMode.cpp:25:void InitAdvancedMode(bool sessionOverride) {
app/src/App/Demo/AdvancedMode.h:27:void InitAdvancedMode(bool sessionOverride);
app/src/App/MainWindow.xaml.cpp:468:  urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced);
app/src/App/main.cpp:174:    urmsg::InitAdvancedMode(urmsg::demo::ParseDemoOptions().advanced);

$ grep -n 'options_\|SelectNavTag\|DrainDeepLink\|pendingLink' app/src/App/MainWindow.xaml.h
49:  urmsg::demo::DemoOptions const& DemoOptio
```

**Verifier note.** CONFIRMED, and understated. All four sub-claims hold, and the tree is worse than the auditor described.

WHAT THE BRIEF ADDS (task-A3-brief.md:455-530, verbatim in the plan source docs/superpowers/plans/tasks/advanced.md:779-853 — the extraction is faithful, so this is not a brief-only bug):
- step 7: `urmsg::demo::DemoOptions demoOptions_{};` and `void SelectDemoScreen(urmsg::demo::DemoScreen screen);`
- step 8: `demoOptions_ = urmsg::demo::ParseDemoOptions();` + `urmsg::InitAdvancedMode(demoOptions_.advanced || demoOptions_.screen == DemoScreen::Developer);` in the constructor, plus `SelectDemoScreen(demoOptions_.screen)` immediately before the `main window constructed` log, whose body is `if (screen == Settings) HomeNav().SelectedItem(SettingsNavItem());`

WHAT ALREADY EXISTS:
1. Member: MainWindow.xaml.h:110 `urmsg::demo::DemoOptions options_{};` — same type, same role, plus a public accessor at :49. `demoOptions_` is a straight duplicate.
2. Parse: MainWindow.xaml.cpp:94, AND main.cpp:174 does its own independent `ParseDemoOptions()`. So the tree already has 2 parse sites; A3 makes 3.
3. InitAdvancedMode: main.cpp:174 (EVERY launch) and MainWindow.xaml.cpp:468 (inside EnterDemoMode, reached at :109 under `if (options_.enabled)`). Already 2; A3 makes 3. AdvancedMode.cpp:25-36 confirms each call does a fresh `urnw::LoadAppPrefs()` and re-derives `g_enabled` — so the extra call is a third prefs read and a third derive.
4. Deep link: DemoShellState.h:75-76 `DeepLinkFor(DemoScreen::Settings)` already returns navTag `L"settings"`, and DrainDeepLink already runs `SelectNavTag(pendingLink_.navTag)` at :571, which walks MenuItems/FooterMenuItems and sets `HomeNav().SelectedItem(item)`. `--demo=settings` ALREADY selects SettingsNavItem today. SelectDemoScreen is a pure secon

**Consequence if not overridden.** Two DemoOptions members and two InitAdvancedMode calls (the second re-reading prefs and re-deriving g_enabled), plus a second deep-link writer that competes with DrainDeepLink for HomeNav().SelectedItem — the same two-writers-for-one-state shape MainWindow.xaml.cpp:126-131 already names as this project's standing failure. The constructor-time selection is also documented not to stick, so --demo=settings would open on Chats if DrainDeepLink is ever removed as 'redundant'.

**Proposed dispatch override.** Delete steps 7 and 8's `demoOptions_` member, the `demoOptions_ = ParseDemoOptions()` line, the `urmsg::InitAdvancedMode(...)` line and the whole `SelectDemoScreen` function and its declaration. Read `options_` (MainWindow.xaml.h) where you need the demo options; Advanced Mode is already seeded by `EnterDemoMode()` at MainWindow.xaml.cpp:468 via `DeepLinkFor(options_.screen).forceAdvanced`, which is exactly the `screen == Developer` OR you were about to add; and the deep link is already delivered by `DrainDeepLink()` -> `SelectNavTag(pendingLink_.navTag)` from the first SizeChanged, which is where it has to run.

### A3 - class 6 (names something that does not exist) - **CONFIRMED**

**Claim.** A3 step 2 and A5 step 2 both state "UrComponents.cpp's StyleByKey is file-local (anonymous namespace, line 78) and is NOT exported" and instruct the implementer to write a private copy; `urnw::kit::StyleByKey` is in fact declared in UrComponents.h:122, and UrComponents.h:117-121 records that the three previous private copies were deliberately deleted and that "a fourth unit wanting this should do one of those two, never a fourth copy".

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -rn 'kit::StyleByKey\|Style StyleByKey' app/src/App/UrComponents.h app/src/App/Views/ThreadView.cpp app/src/App/Views/InspectRailView.cpp | head`

**Actual output.**
```
app/src/App/UrComponents.h:117:// it once. Both copies are gone: InspectRailView.cpp calls kit::StyleByKey, and
app/src/App/UrComponents.h:119:// it for a `using urnw::kit::StyleByKey;`, so its twelve call sites read exactly
app/src/App/UrComponents.h:122:winrt::Microsoft::UI::Xaml::Style StyleByKey(wchar_t const* key);
app/src/App/Views/ThreadView.cpp:17:#include "UrComponents.h"  // urnw::kit::StyleByKey
app/src/App/Views/ThreadView.cpp:29:// StyleByKey is urnw::kit::StyleByKey (UrComponents.h) and is no longer copied
app/src/App/Views/ThreadView.cpp:41:using urnw::kit::StyleByKey;
app/src/App/Views/InspectRailView.cpp:330:  if (auto style = kit::StyleByKey(L"UrRowTitleStyle")) name.Style(style);
app/src/App/Views/InspectRailView.cpp:335:  if (auto style = kit::StyleByKey(L"UrRowNoteStyle")) note.Style(style);
app/src/App/Views/InspectRailView.cpp:401:  if (auto style = kit::StyleByKey(L"UrRowIconStyle")) lock.Style(style);
app/src/App/Views/InspectRailView.cpp:409:  if (auto style = kit::StyleByKey(L"UrBodyStrongTextStyle")) title.Style(style);
```

**Verifier note.** REPRODUCED, and every load-bearing part of the claim holds.

Header side. UrComponents.h opens `namespace urnw::kit {` at line 28 and closes it at 559, so the declaration at line 122 is genuinely `urnw::kit::StyleByKey` at namespace scope, exported, with no preprocessor guard around it. Lines 117-121 are the exact prohibition quoted: "Both copies are gone... A fourth unit wanting this should do one of those two, never a fourth copy."

The A3 parenthetical is wrong three separate ways. A3-brief.md:128 says "file-local (anonymous namespace, line 78) and is NOT exported". In UrComponents.cpp the anonymous namespace is lines 23-31 (`namespace {` at 23, `}  // namespace` at 31). StyleByKey is defined at line 76 — outside it, at `urnw::kit` scope — under a comment at 73-75 reading "Declared in UrComponents.h and no longer file-local: two other units had copied these six lines verbatim because they could not reach this one, and both copies are gone." So: not anonymous, not line 78, and exported. The brief's citation contradicts the comment sitting directly above the line it cites.

Both briefs do instruct the copy. A3-brief.md:133 and A5-brief.md:117 each contain a verbatim six-line `Style StyleByKey(wchar_t const* key) {...}` inside `namespace urmsg::views { namespace { ... } }`, byte-identical to UrComponents.cpp:76-82. A3 then calls it at 8 sites (160, 194, 207, 210, 215, 256, 321, 368); A5 at 5 sites (186, 194, 196, 271, 292). Both files already `#include "UrComponents.h"` (A3:117, A5:101) and already alias `namespace kit = urnw::kit;` (A3:123, A5:109) — the exported symbol is in scope in both files, so the copy is a pure duplicate.

MY ONE ATTEMPT TO MAKE IT WRONG — four angles, all failed:
1. Step-1 guard (the network.md pattern). A3/A5 Step 1 writes the .h only; neither

**Consequence if not overridden.** Two new private copies of a lookup the kit exports, in the two files this surface adds — re-creating by hand the drift the promotion to UrComponents.h was done to end, and leaving a false claim about an API in two file headers.

**Proposed dispatch override.** In BOTH Views/SettingsView.cpp and Views/DeveloperView.cpp, delete the private `StyleByKey` and its comment. `urnw::kit::StyleByKey` is exported at UrComponents.h:122 — the file already includes UrComponents.h, so call `kit::StyleByKey(L"...")` (as InspectRailView.cpp does) or add `using urnw::kit::StyleByKey;` (as ThreadView.cpp:41 does). A6's `FontFamilyByKey` is genuinely new and stays.

### A4 - class 7 (missing baseline) - **PARTIAL**

**Claim.** A4 step 6's "positive control" tells the implementer that `--diagnose` writes `advanced_mode` into app_prefs.json via "A1's advanced.notify block"; no such block, no `AdvancedModeDiagnostics()` and no `advanced.notify` line exists anywhere in the tree, `urnw::SaveAppPref` has exactly one caller (`SetAdvancedModeEnabled`, AdvancedMode.cpp:43) which `--diagnose` never reaches, and a real run into a scratch root produces no app_prefs.json at all.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && rm -rf /tmp/urmprobe && mkdir -p /tmp/urmprobe && URMESSAGE_APP_ROOT="$(cygpath -w /tmp/urmprobe)" ./app/build/x64/Release/URmessage.exe --diagnose >/dev/null 2>&1; ls /tmp/urmprobe; grep -rn 'SaveAppPref' app/src/App --include=*.cpp | grep -v x64`

**Actual output.**
```
$ cd .../worktrees/demo-ui && rm -rf /tmp/urmprobe && mkdir -p /tmp/urmprobe && URMESSAGE_APP_ROOT="$(cygpath -w /tmp/urmprobe)" ./app/build/x64/Release/URmessage.exe --diagnose >/dev/null 2>&1; ls /tmp/urmprobe; grep -rn 'SaveAppPref' app/src/App --include=*.cpp | grep -v x64

--- exit: 0 ---
--- ls /tmp/urmprobe ---
total 296
drwxr-xr-x 1 ryanm 197609 0 Sep  8 08:51 .
drwxr-xr-x 1 ryanm 197609 0 Sep  8 08:51 ..
drwxr-xr-x 1 ryanm 197609 0 Sep  8 08:51 logs
--- grep SaveAppPref ---
app/src/App/Demo/AdvancedMode.cpp:43:  urnw::SaveAppPref(kPrefKey, on);

(no app_prefs.json — only logs/. Exactly one SaveAppPref caller.)

I then ran A4 step 6's OWN PowerShell verbatim:
--- files in probe ---
...\.localstate-persist-probe\logs
...\.localstate-persist-probe\logs\urmessage-app.log
--- Select-String on app_prefs.json ---
Select-String : Cannot find path 'C:\Users\ryanm\Downloads\claude_sandbox_message\message-windows\.claude\worktrees\demo-ui\.localstate-persist-probe\app_prefs.json' because it does not exist.
    + CategoryInfo          : ObjectNotFound: (...app_prefs.json:String) [Select-String], ItemNotFoundException
    + FullyQualifiedErrorId : PathNotFound,Microsoft.PowerShell.Comm
```

**Verifier note.** DEFECT IS REAL — step 6 is unsatisfiable as written. But the auditor's CONSEQUENCE is wrong, and wrong in a way the brief itself refutes two paragraphs later.

WHAT I CONFIRMED (all reproduced above):
1. Step 6's positive control cannot pass. Its expected "one match line containing `"advanced_mode":false`" is instead a PowerShell PathNotFound error. No app_prefs.json is created by --diagnose in a scratch root; only logs/.
2. `advanced.notify` exists nowhere in code — its only 3 occurrences in the whole tree are inside A4's own Consumes/step text. Same for `AdvancedModeDiagnostics()` (2 hits, both in A2's brief). Both are dangling references.
3. Root cause the auditor missed, which makes the finding STRONGER: **there is no task A1.** The plan says so explicitly at docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md:227-229 — "`W2` and `A1` are deliberately absent. All three of F6, W2 and A1 created `Demo/AdvancedMode.h/.cpp`... F6 builds it, and `W7` and `A4` consume it rather than building their own." The task table lists "A2–A6", and there is no task-A1-brief.md. A4's Consumes line credits a task that was dropped during assembly; the real builder is F6. F6's step 5 added the only `advanced` --diagnose line and it is read-only (`LoadAppPrefs`), and F6's own report says "--demo-advanced turns it ON and writes nothing" with diagnose printing "pref advanced_mode: absent". So the writer step 6 invokes was never specified by anyone — it is not a stale rename of something F6 built.
4. No later step corrects it. `diff` of advanced.md:988-1173 against the A4 brief is one trailing blank line — byte-identical otherwise. Unlike network.md, no earlier step's grep catches this.

WHY IT IS NARROWER THAN CLAIMED (the ONE way the claim is wrong):
The auditor asserts step 7 "will pri

**Consequence if not overridden.** Step 6 finds nothing, so by the brief's own rule step 7 is meaningless — yet step 7's `Test-Path .\.localstate-verify\app_prefs.json` will print False regardless, because nothing in a headless verify-render run can write that file (the only writer needs a click on the Settings toggle, and A4 itself states the agent may not synthesise input). The persistence gate ships green while proving nothing.

**Proposed dispatch override.** Replace step 6's positive control: there is no `AdvancedModeDiagnostics()` and no `advanced.notify` line in the tree — do not look for one. Prove the query and the path instead by writing the preference through the only writer there is: run `URMESSAGE_APP_ROOT=<scratch> URmessage.exe --diagnose` once to confirm NO app_prefs.json appears, then add a throwaway `urmsg::SetAdvancedModeEnabled(true)` call at the top of CollectDiagnostics, rebuild, re-run, and confirm `Select-String` finds `"advanced_mode":true` at `<scratch>\app_prefs.json`. Revert that line before step 7. Publish both outputs beside step 7's result; without them step 7 is not a gate.

### A4 - class 8 (cross-task conflict) - **CONFIRMED**

**Claim.** A4 produces `MainWindow::ApplyAdvancedMode(bool)` and registers `urmsg::OnAdvancedModeChanged` in the constructor calling it "THE one subscription (contract §5)"; wiring.md task W7 produces `MainWindow::ApplyAdvanced(bool)` and registers `urmsg::OnAdvancedModeChanged` in EnterDemoMode calling it "The ONE subscriber of urmsg::OnAdvancedModeChanged (contract v2 section 5)" — two differently-named handlers for the same single subscription, with different bodies and different registration points.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'OnAdvancedModeChanged\|ApplyAdvanced' docs/superpowers/plans/tasks/wiring.md docs/superpowers/plans/tasks/advanced.md`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'OnAdvancedModeChanged\|ApplyAdvanced' docs/superpowers/plans/tasks/wiring.md docs/superpowers/plans/tasks/advanced.md

docs/superpowers/plans/tasks/wiring.md:532:- Consumes: `urmsg::OnAdvancedModeChanged`, `urmsg::AdvancedModeEnabled` (W2); `urmsg::views::SetConversationListAdvanced` (Views/ConversationListView.h); `urmsg::views::SetStatusStripAdvanced` (Views/StatusStripView.h); `urmsg::views::SetNetworkPageAdvanced` (Views/NetworkPageView.h); `urmsg::views::SetInspectRailAdvanced` (Views/InspectRailView.h); `MainWindow::SelectNavTag`, `currentTag_`, `list_`, `strip_`, `network_`, `rail_` (W3, W5, W6)
docs/superpowers/plans/tasks/wiring.md:533:- Produces: `void MainWindow::ApplyAdvanced(bool on);` — the one subscriber contract v2 §5 requires
docs/superpowers/plans/tasks/wiring.md:555:  // The ONE subscriber of urmsg::OnAdvancedModeChanged (contract v2 section 5).
docs/superpowers/plans/tasks/wiring.md:557:  void ApplyAdvanced(bool on);
docs/superpowers/plans/tasks/wiring.md:560:- [ ] **Step 2: Add `ApplyAdvanced()` to `app/src/App/MainWindow.xaml.cpp`.**
docs/superpowers/plan
```

**Verifier note.** CONFIRMED — and wider than claimed. My attempt to break it (look for a guard elsewhere, the network.md pattern the prompt warned about) instead produced the proof.

THE COLLISION IS REAL AND UNGUARDED. Both tasks are in scope (master plan table: "Settings / Advanced / Developer | A2-A6" and "Click graph and ambient activity | W5-W9"; tasks #19 and #20 both pending). Neither file references the other at all: `grep -nE '\bA[1-9]\b' wiring.md` and `grep -nE '\bW[1-9]\b' advanced.md` both return ZERO lines. Neither A4 nor W7 has any preflight/dedup step — A4's steps are 1 declare, 2 register, 3 call, 4-8 build/screenshot/gate/commit; W7's are 1 declare, 2 define, 3 register, 4-8 build/screenshot/gate. wiring.md's only "If it prints" is line 1328 (inside W8/W9, about a hash gate), outside W7's span of 524-696.

THE PLAN'S OWN CONVENTION CONVICTS IT. There are FOUR tasks that register `OnAdvancedModeChanged`, not two, and exactly the two the auditor named are the unguarded ones:
  N6 network.md:1548  GUARDED   — step 6 greps, then "If it prints a hit, that function already exists: skip this step and step 7's function body"; step 7's else-branch is "make exactly one edit - add this line to that function's body and register nothing"
  S4 strip.md:1614    GUARDED   — step 5 greps MainWindow.xaml.cpp for 'OnAdvancedModeChanged|InitAdvancedMode', then "do not register a second subscriber; add exactly this one line inside the existing lambda's body"
  A4 advanced.md:1027 UNGUARDED — unconditional
  W7 wiring.md:601    UNGUARDED — unconditional
Build order is network -> strip -> advanced -> wiring, so N6 lands the subscriber, S4 correctly declines to add a second, then A4 adds a second anyway, then W7 adds a third. Note also network.md:1353 claims to Produce `MainWindow::ApplyAdvanc

**Consequence if not overridden.** Whichever lands second either duplicates the subscription (both handlers fire on every toggle, one of them re-running BuildDeveloper and the nav-visibility flip a second time) or is silently dropped by an implementer who notices the clash and picks one — losing either the five Set*Advanced density calls (W7) or the Developer gate (A5).

**Proposed dispatch override.** A4 does not introduce a subscription. wiring.md W7 owns `MainWindow::ApplyAdvanced(bool)` and the single `urmsg::OnAdvancedModeChanged` registration, placed in `EnterDemoMode()` after `BuildDemoViews()`. Rename A4's handler to `ApplyAdvanced` and fold its body into W7's; A5's Developer gate becomes two lines added to W7's `ApplyAdvanced`, not a second function. Delete A4 step 2's registration block and A4 step 3's constructor call.

### A5 - class 2 (duplicates shell-owned state) - **CONFIRMED**

**Claim.** A5 step 6 instructs a replacement that adds a `<muxc:NavigationViewItem x:Name="DeveloperNavItem" Tag="developer" Visibility="Collapsed">` with glyph EC7A to FooterMenuItems; an element with that exact x:Name and Tag, Collapsed, with glyph E943, already exists at MainWindow.xaml:134 in the same FooterMenuItems, and step 8(b) adds a `DeveloperNavItem().Content(...)` write that EnterDemoMode already performs at MainWindow.xaml.cpp:487.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'DeveloperNavItem' app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
app/src/App/MainWindow.xaml:134:                <muxc:NavigationViewItem x:Name="DeveloperNavItem" Tag="developer"
app/src/App/MainWindow.xaml.cpp:487:  DeveloperNavItem().Content(box_value(hstring{kDemoNavDeveloper}));
app/src/App/MainWindow.xaml.cpp:560:  // NetworkNavItem/DeveloperNavItem in code and Append/InsertAt-ing them into
app/src/App/MainWindow.xaml.cpp:566:  DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
```

**Verifier note.** Every factual leg reproduces.

EXISTING ELEMENT. MainWindow.xaml:133-145 is the FooterMenuItems block, and it already contains DeveloperNavItem (134, Tag="developer", Visibility="Collapsed", Glyph=E943 at 137) ABOVE SettingsNavItem (140, Glyph=E713). Step 6 (brief lines 333-366) instructs replacing "lines 108-114, exactly" with a block whose quoted "before" text is a FooterMenuItems containing ONLY SettingsNavItem, and whose "after" text appends a second DeveloperNavItem with Glyph=EC7A.

THE ANCHOR IS STALE BY EXACTLY ONE COMMIT, and git says so. `git log -S'x:Name="DeveloperNavItem"' -- app/src/App/MainWindow.xaml` returns one commit: 9e1bf88 "demo: MainWindow hosts, DEMO chip, Network/Developer destinations...". At the prior commit 4c2e6da the quoted before-block IS verbatim at lines 108-114 (`git show 4c2e6da:app/src/App/MainWindow.xaml | sed -n '105,116p'` prints it). advanced.md was drafted against the pre-shell tree. Today lines 108-114 are the HeaderTemplate close, the MenuItems open, and ChatsNavItem — so a literal line-range replacement would also destroy the MenuItems block, and a literal string-match replacement finds nothing and stops.

MECHANISM, without needing a build: the codegen declares one accessor per x:Name — app/src/App/x64/Release/MainWindow.xaml.g.h:118-124 has exactly one `DeveloperNavItem()` accessor over one `_DeveloperNavItem` field (:421), populated once at .g.hpp:101. Two elements cannot both bind to it.

MY ONE ATTEMPT TO REFUTE IT, AND WHY IT FAILED. This plan does have the guard pattern the prompt describes, and it is even aimed at this exact file: progress.md:158-166 records a SYSTEMIC PLAN DEFECT audit, names "A2/A5 (adv.)" explicitly, and mandates mount_check.py as a REQUIRED pre-flight before every remaining dispatch. It does not co

**Consequence if not overridden.** Two elements sharing one x:Name in a single namescope — the XAML compile fails outright, or if the implementer 'fixes' it by renaming, the window grows a second developer-tagged nav item and two writers of its Content. Step 6's quoted anchor (lines 108-114) is also not the SettingsNavItem block any more; the current file has DeveloperNavItem ABOVE SettingsNavItem.

**Proposed dispatch override.** Do NOT add a DeveloperNavItem. One already exists at MainWindow.xaml:134 (Tag="developer", Visibility="Collapsed", glyph E943 = Code) and its label is already written by `EnterDemoMode()` at MainWindow.xaml.cpp:487 as `kDemoNavDeveloper`. Delete step 6's FooterMenuItems replacement and step 8(b)'s `DeveloperNavItem().Content(...)` line entirely; keep the glyph the shell chose.

### A5 - class 9 (plan text that does not build) - **CONFIRMED**

**Claim.** A5 step 9's `ApplyAdvancedMode` writes `DeveloperNavItem().Visibility(advanced ? Visible : Collapsed)` with no follow-up PaneDisplayMode cycle; MainWindow.xaml.cpp:530-569 records a Windows App SDK 2.2.0 defect — isolated with three throwaway builds — in which a NavigationViewItem's Collapsed→Visible transition permanently corrupts NavigationView's Auto pane-mode resolution to icon-only, recoverable only by immediately stepping PaneDisplayMode to LeftCompact and back to Auto, which DrainDeepLink does and this function does not.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n "corrupts NavigationView's own Auto pane-mode\|DeveloperNavItem().Visibility\|PaneDisplayMode(NavigationViewPaneDisplayMode" app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
533:  // corrupts NavigationView's own Auto pane-mode resolution: it renders
566:  DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
568:  HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact);
569:  HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);

(4 lines, all inside DrainDeepLink. The only PaneDisplayMode writes in the file.)
```

**Verifier note.** CONFIRMED. The evidence command shows the workaround existing ONLY in DrainDeepLink (:566 flip immediately followed by the :568/:569 cycle), and those two lines are the file's ONLY PaneDisplayMode writes. A5 step 9 (task-A5-brief.md:469-486, read verbatim) writes `DeveloperNavItem().Visibility(advanced ? Visible : Collapsed)` as its first statement, then BuildDeveloper(), then the selection fallback, then LogInfo. No cycle.

SECOND CONFIRMATION THE AUDITOR DID NOT CITE, and it is stronger than the one quoted. MainWindow.xaml.cpp:482-486, in EnterDemoMode, states the pairing as a RULE rather than a local fix: "Content only, here. The Visibility flip is deferred to DrainDeepLink (fix round 1) and paired there with a PaneDisplayMode nudge - see that function for why. Isolated by disabling each call above in turn: Content() alone never triggered the regression, only Visibility() did." So the tree already records that a bare Visibility() write on these items is the trigger and that it must be paired. A5 step 9 introduces exactly the unpaired write the tree warns against.

FOUR REFUTATION ATTEMPTS, ALL FAILED:
(1) Another brief supplies the cycle - no. `grep -rn "PaneDisplayMode" .superpowers/sdd/` hits only progress.md:61/71, task-L2-brief.md:48, task-W3-report.md:217, task-W4-report.md:157. NO task STEP anywhere adds it. Only A3, A4, A5 and N6 mention ApplyAdvancedMode; N6 step 7's body is `SetNetworkPageAdvanced(network_, advanced);` and A4 step 2's is the stub A5 replaces. Neither cycles. A2 and A6 return zero hits for ApplyAdvancedMode/DeveloperNavItem/PaneDisplayMode.
(2) A Step-1 grep guard like network.md's - N6 step 6 DOES carry that pattern (`git grep -n "ApplyAdvancedMode\|MainWindow::SetAdvanced" -- app/src/App` with explicit if-it-prints-nothing / if-it-prints-a-

**Consequence if not overridden.** Turning Advanced Mode on from the Settings toggle drops the nav rail to icon-only for the rest of the session, with no labels beside Chats/Contacts/Network/Developer/Settings and no recovery on resize. A5 step 11 compares two SEPARATE LAUNCHES — each of which flips visibility inside DrainDeepLink, where the workaround lives — so nothing in A5 exercises the live toggle that breaks it.

**Proposed dispatch override.** In `ApplyAdvancedMode`/`ApplyAdvanced`, every `DeveloperNavItem().Visibility(...)` write must be followed immediately by `HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact); HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);` — copy the pattern and the WORKAROUND comment from MainWindow.xaml.cpp:566-569 rather than restating it. Then add a verification step: launch `--demo=settings --demo-advanced`, and in the same run capture `urmessage-window-wide.png` and confirm Chats/Contacts/Network/Developer/Settings still show TEXT LABELS at 1200x800.

### N1 - class 2 (duplicates shell-owned state) - **CONFIRMED**

**Claim.** N1 Steps 6-7 add `MainWindow::ApplyDemoScreen()` as a constructor-time deep-link dispatcher that writes `HomeNav().SelectedItem(...)`; the shell already owns that path via `DrainDeepLink()` -> `SelectNavTag(pendingLink_.navTag)` (MainWindow.xaml.cpp:571), run from the content root's first SizeChanged, and records a measured failure for doing it from the constructor.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '520,530p;560,575p' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ cd .../worktrees/demo-ui && sed -n '520,530p;560,575p' app/src/App/MainWindow.xaml.cpp

void MainWindow::DrainDeepLink() {
  // One-shot, and only once the layout has actually been written. breakpoint
  // state, window size and NavigationView's own load-time selection are all
  // settled by the first SizeChanged; running from the constructor instead put
  // the nav selection before NavigationView loaded (where the markup's
  // IsSelected="True" on ChatsNavItem wins) and the message selection before the
  // rail existed.
  if (!pendingLinkArmed_ || !layoutApplied_) return;
  pendingLinkArmed_ = false;

  // The demo nav items become visible HERE, not in EnterDemoMode (fix round 1).
  // NetworkNavItem/DeveloperNavItem in code and Append/InsertAt-ing them into
  // HomeNav().MenuItems()/FooterMenuItems() only inside EnterDemoMode, instead
  // of declaring them Collapsed in XAML and toggling Visibility here, would
  // never perform the corrupting transition at all - a candidate, not a
  // verified fix.
  NetworkNavItem().Visibility(Visibility::Visible);
  DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
                                          : Visibility::Col
```

**Verifier note.** Every element of the claim reproduces, and the incumbent demonstrably already covers the exact input N1 targets.

THE DUPLICATE IS EXACT, not adjacent. `DemoShellState.h:74-75` — `case DemoScreen::Network: return DeepLink{L"network", false, false, false};` — and `MainWindow.xaml:126` declares `<muxc:NavigationViewItem x:Name="NetworkNavItem" Tag="network"`. `SelectNavTag` (MainWindow.xaml.cpp:502-518) matches on `item.Tag()` and calls `HomeNav().SelectedItem(item)`. So for `--demo=network` the shell ALREADY reaches `HomeNav().SelectedItem(NetworkNavItem())` via DrainDeepLink:571. N1 Step 7's new body writes the identical statement. Startup.cpp:385 pins the mapping under a --diagnose gate (`{DemoScreen::Network, L"network"}`).

CONSTRUCTOR TIMING CONFIRMED. Step 7 says add `ApplyDemoScreen();` as the LAST statement of the constructor. The SizeChanged handler that calls DrainDeepLink is only *registered* at :178-185 and fires after the constructor returns, so ApplyDemoScreen provably runs first — into the failure mode :521-526 documents by measurement.

"COLLAPSED AT THAT MOMENT" CONFIRMED. MainWindow.xaml:127 declares `Visibility="Collapsed"`, and EnterDemoMode:482-486 explicitly defers the flip: "Content only, here. The Visibility flip is deferred to DrainDeepLink (fix round 1)... Isolated by disabling each call above in turn: Content() alone never triggered the regression, only Visibility() did."

MY ATTEMPT TO MAKE IT WRONG — the network.md "Step 1 grep already handles it" pattern — FAILS. I ran Step 1 verbatim; unlike the brief's stated expectation ("no output at all (exit 1)") it prints 7 hits, exit 0, including MainWindow.xaml:126 and :265, so the escape hatch does fire. It still does not rescue Steps 6-7, for three reasons:
  (a) The probe searches for `ApplyDemoS

**Consequence if not overridden.** Two dispatchers writing SelectedItem — the race N1's own Step 7 warns about — and the new one runs from the constructor, which MainWindow.xaml.cpp:521-527 documents as losing to the markup's `IsSelected="True"` on ChatsNavItem, against a `NetworkNavItem` still Collapsed at that moment.

**Proposed dispatch override.** SKIP Steps 6 and 7 entirely. Do NOT declare or define `ApplyDemoScreen`, do not add `#include "Demo/DemoSwitches.h"` for it, and add nothing to the constructor. The shell's `DrainDeepLink()` (MainWindow.xaml.cpp:520) already arms `--demo=network` through `DeepLinkFor(DemoScreen::Network)` -> `SelectNavTag(L"network")` post-layout, which is the only correct time to do it. N1 reduces to Steps 8-11 (build, launch, read, confirm) plus the commit.

### N2 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** The `net relay hops` gate asserts `node.hopMs > 0` for every node, but `relayPath[0]` ("This device") ships `hopMs = 0` (DemoWorld.cpp:386), so the gate FAILS on the shipped world — and Step 6 tells the implementer the world is wrong and to fix it, while `hopMs` is mixed into the I10 fingerprint (Startup.cpp:233) that is currently green at 0x97B1C149D13010C3.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n "relayPath.push_back" app/src/App/Demo/DemoWorld.cpp && grep -n "n.hopMs\|kExpectedWorldFingerprint" app/src/App/Startup.cpp && ./app/build/x64/Release/URmessage.exe --diagnose 2>&1 | grep -E "I7|I10"`

**Actual output.**
```
386:  w.relayPath.push_back({L"This device", L"This computer", L"\uE977", 0, true});   // Devices
387:  w.relayPath.push_back({L"URnetwork", L"3 hops", L"\uE774", 41, true});           // Globe
388:  w.relayPath.push_back({L"Message server", L"urmsg-01.ur.io", L"\uE968", 18, true}); // Server
233:    MixU64(h, static_cast<uint64_t>(n.hopMs)); MixU64(h, n.healthy ? 1u : 0u);
246:constexpr uint64_t kExpectedWorldFingerprint = 0x97B1C149D13010C3ull;
341:      Verdict(fp == kExpectedWorldFingerprint), fp, kExpectedWorldFingerprint));
    I7  relay path          PASS  3 nodes, 0 empty glyphs
    I10 determinism         PASS  fingerprint 0x97B1C149D13010C3, expected 0x97B1C149D13010C3
```

**Verifier note.** All four legs reproduce, and the field-order objection dies: DemoWorld.h:108-114 declares `RelayNode{label, subLabel, glyph, hopMs, healthy}`, so the literal `0` at position 4 on line 386 is genuinely hopMs, not a mis-read initializer.

The gate the brief dictates (task-N2-brief.md:176-183, identical at network.md:445-452) is:
  bool hopsOk = (world.relayPath.size() == 3);
  for (auto const& node : world.relayPath) {
    hopSum += node.hopMs;
    if (node.hopMs <= 0 || node.glyph.empty() || node.label.empty()) hopsOk = false;
  }
No index skip, and `0 <= 0` is true, so node 0 sets hopsOk = false. Step 6 (:248) and Step 8 (:296) both print the expected line as `net relay hops : PASS`, and Step 6 adds verbatim: "if it says FAIL, the world is wrong, not this task; stop and fix that instead." The brief header also says "Use every value in it verbatim," so the implementer has no license to soften the predicate.

I attacked the claim from its strongest angle — that Task 1's contract or a later task raises hop 0 above zero, so the Step 6 FAIL never fires. It backfired. foundation.md:722 and task-F2-brief.md:605 seed the identical `{L"This device", L"This computer", L"\uE977", 0, true}`, i.e. hopMs = 0 for hop 0 IS the fixed contract N2's own Consumes line points at. So Step 6 sends the implementer to "fix" data that is exactly what the plan specifies. No later step corrects it: N3 only renders the hop figure (Collapsed until N6), and no brief re-seeds relayPath.

Two secondary checks, both against the claim's favor:
- Fingerprint escape hatch is real and blessed. task-F3-brief.md:25: "Later tasks that change the world MUST update that constant in the same commit." So editing DemoWorld.cpp under Step 6 obligates re-basing kExpectedWorldFingerprint in the same commit — precisely

**Consequence if not overridden.** The implementer follows Step 6, edits Demo/DemoWorld.cpp to make hop 0 nonzero, turns I10 red, then re-bases kExpectedWorldFingerprint — silently unlocking the byte-fingerprinted world. The alternative reading fabricates a per-hop latency for 'this device to itself'. The rest of the gate (size==3, non-empty glyph) already ships as I7 at Startup.cpp:319-322.

**Proposed dispatch override.** Do NOT edit Demo/DemoWorld.cpp — `relayPath[0].hopMs == 0` is correct (there is no hop to yourself) and its bytes are inside the I10 fingerprint at Startup.cpp:233. Change the `net relay hops` assertion to `hopsOk = (world.relayPath.size() == 3)` and, per node, `if (node.hopMs < 0 || node.glyph.empty() || node.label.empty()) hopsOk = false;` — note `< 0`, not `<= 0`. Because I7 (Startup.cpp:319-322) already asserts size==3 and non-empty glyphs, the only new thing this line buys is the label/hop check, so say so in the printed QUERY. Separately, in N3's `MakeNode` and N6's Advanced per-hop line, render the hop figure ONLY when `node.hopMs > 0`, so node 1 does not display a fabricated-looking `0 ms`; update N6 Step 11 expectation 1 to two per-hop figures, and its deliverable to three visible lines, not four.

### N2 - class 4 (gate that cannot fail) - **PARTIAL**

**Claim.** After Step 7 lands, the `net fmt rtt` assertion `FormatRoundTrip(world.server) == FormatLatency(world.server.latencyMs) + L" round trip"` is the mandated function body restated, so both sides are the same expression and the gate cannot fail for any mutation, including a broken `FormatLatency`.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '434,439p;544,547p' docs/superpowers/plans/tasks/network.md`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '434,439p;544,547p' docs/superpowers/plans/tasks/network.md

      L"  net fmt rtt      : {}  FormatRoundTrip == FormatLatency + \" round "
      L"trip\" -> \"{}\" vs \"{}\"",
      Check(FormatRoundTrip(world.server) ==
            FormatLatency(world.server.latencyMs) + L" round trip"),
      FormatRoundTrip(world.server),
      FormatLatency(world.server.latencyMs) + L" round trip"));

std::wstring FormatRoundTrip(urmsg::demo::ServerInfo const& server) {
  return FormatLatency(server.latencyMs) + L" round trip";
}
```

**Verifier note.** File: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui/docs/superpowers/plans/tasks/network.md (N2 has not landed; app/src/App/Views/ has no NetworkPageView.* yet, so this is plan-only).

CONFIRMED HALF. The textual overlap is exactly as the auditor prints it. Step 7's mandated body (network.md:545-547) is `return FormatLatency(server.latencyMs) + L" round trip";` and the Check at network.md:436-437 is `FormatRoundTrip(world.server) == FormatLatency(world.server.latencyMs) + L" round trip"`. Substituting the body, `FormatLatency(...)` appears identically on both sides and cancels. So the PASS/FAIL bit of `net fmt rtt` is genuinely blind to FormatLatency's return value, despite a label that reads as though it checks it.

REFUTED HALF 1 — "the gate cannot fail for any mutation" is false. The cancellation is with respect to FormatLatency only. The assertion lives in CollectNetworkDiagnostics, a different definition site from FormatRoundTrip, so it is a live change-detector on FormatRoundTrip's own body: a wrong suffix, wrong field, reversed operands, or a stub all break it. The plan demonstrates this itself — Step 6's RED expectation at network.md:516 is `net fmt rtt : FAIL  FormatRoundTrip == FormatLatency + " round trip" -> "" vs " round trip"`, with the brief's own gloss at :520-521 ("the left side is `""` and the right side is `"" + " round trip"` = `" round trip"`, which are not equal"). A gate the plan shows printing FAIL in its own red state is not a permanent PASS, and is not the "fourteenth gate that cannot fail."

REFUTED HALF 2 — the claimed consequence does not follow. The auditor's escape scenario (FormatLatency regresses to `""`, blank figure ships) is caught three assertions above, by the sibling gate at network.md:40

**Consequence if not overridden.** One of the five `net *` lines is a permanent PASS that certifies nothing. If FormatLatency regresses to returning "", `net fmt rtt` still reads PASS (" round trip" == " round trip") while the page renders a blank round-trip figure — the fourteenth gate here that cannot fail.

**Proposed dispatch override.** Replace the `net fmt rtt` assertion with a check against a locally built adversarial value, not against the implementation restated: build `urmsg::demo::ServerInfo probe{}; probe.latencyMs = 7;` inside the assertion block and assert `FormatRoundTrip(probe) == L"7 ms round trip" && FormatRoundTrip(world.server) == FormatLatency(world.server.latencyMs) + L" round trip"`. The literal on the left is what makes it able to fail; build the probe LOCALLY and do not add a row to DemoWorld. Update Step 6's expected FAIL text and Step 8's expected PASS text to print the probe value.

### N5 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** N5 Step 9's screenshot acceptance contradicts the shipped fixture in two places: it expects the other two device rows to read `Last seen 2 min ago` and `Last seen 3 days ago` and expects one green dot, but `dev-pixel9` ships `online = true` (DemoWorld.cpp:239-240), so `FormatDeviceMeta` returns `Online` for it and two of the three dots are green.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '237,242p' app/src/App/Demo/DemoWorld.cpp`

**Actual output.**
```
w.myDevices.push_back(MakeDevice(L"dev-desktop", L"Windows desktop", L"Rowan Ashby",
                                   L"now", true, true));
  w.myDevices.push_back(MakeDevice(L"dev-pixel9", L"Pixel 9", L"Rowan Ashby",
                                   L"2 min ago", true, false));
  w.myDevices.push_back(MakeDevice(L"dev-laptop", L"Linux laptop", L"Rowan Ashby",
                                   L"3 days ago", false, false));
```

**Verifier note.** REPRODUCED, both halves, and I could not find an out.

Parameter binding (the step the auditor skipped, DemoWorld.cpp:57-58): `MakeDevice(id, name, ownerName, lastSeen, bool online, bool thisComputer)`. So the 5th arg is `online`. dev-desktop = (online:true, thisComputer:true); dev-pixel9 = (online:true, thisComputer:false); dev-laptop = (online:false, false).

N2's shipped contract body (task-N2-brief.md:268-274) is `online ? "Online" : ("Last seen " + lastSeenLabel)`, prefixed `"This computer · "` when isThisComputer. Applied to the fixture the three rows render:
  dev-desktop -> "This computer · Online"
  dev-pixel9  -> "Online"
  dev-laptop  -> "Last seen 3 days ago"

Place 1 (N5-brief:186-187, Step 9 item 3): expects "the other two read `Last seen 2 min ago` and `Last seen 3 days ago`". "Last seen 2 min ago" is unreachable — dev-pixel9's `lastSeenLabel` "2 min ago" is dead data because `online` short-circuits it. Contradiction.

Place 2 (N5-brief:188-189, Step 9 item 4): expects "green #87FB67 on the online device, faint #5A5A5A on the other two" — one green. Step 4's own code (N5-brief:97) is `dot.Fill(device.online ? kUrGreen : FaintBrush())`, so desktop and pixel9 are both green: 2 green, 1 faint. Contradiction, and it is self-contradiction inside one brief — Step 4's code and Step 9's acceptance disagree with each other, independent of the fixture.

Outs I tried, all closed:
1. Corrective later step / Step-1-grep guard (the network.md precedent): none. `grep -n "fixture|DemoWorld|online" task-N5-brief.md` returns only lines 24, 97, 189 — no caveat, no reconciliation, and Step 9 sits between launch (Step 8) and the first removal click (Step 10), so all three rows are present.
2. Transcription slip in extraction: no. The source plan carries identical text at docs

**Consequence if not overridden.** The screenshot gate cannot be satisfied by correct code. The implementer resolves it by either changing `FormatDeviceMeta` (breaking N2's asserted contract) or editing DemoWorld.cpp's `online` flag — which is mixed into the I10 fingerprint via `mixDevice` (Startup.cpp:200) and turns I10 red.

**Proposed dispatch override.** Correct Step 9's expectations before you capture: the three rows read `This computer · Online` (Windows desktop), `Online` (Pixel 9 — it ships online=true, so its lastSeenLabel `2 min ago` is deliberately NOT shown), and `Last seen 3 days ago` (Linux laptop). Item 4 reads: TWO green `#87FB67` dots (Windows desktop and Pixel 9) and ONE faint `#5A5A5A` dot (Linux laptop). Do NOT edit Demo/DemoWorld.cpp to make the original text true — device `online` is inside the I10 fingerprint (Startup.cpp:200).

### N6 - class 8 (cross-task conflict) - **UNVERIFIED**

**Claim.** N6 Step 6's guard greps for `ApplyAdvancedMode|MainWindow::SetAdvanced`, which does not match wiring.md W7's `void MainWindow::ApplyAdvanced(bool on)` (wiring.md:563) — the same 'ONE subscriber of urmsg::OnAdvancedModeChanged' declared by a different name and registered in `EnterDemoMode()` rather than the constructor.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n "MainWindow::ApplyAdvanced\|OnAdvancedModeChanged" docs/superpowers/plans/tasks/wiring.md docs/superpowers/plans/tasks/advanced.md docs/superpowers/plans/tasks/strip.md`

**Consequence if not overridden.** If wiring W7 lands first, N6's guard returns nothing, N6 adds a second `OnAdvancedModeChanged` registration and a second fan-out function, and `SetNetworkPageAdvanced` runs twice per toggle from two functions with no owner — the exact contract-§5 single-subscriber rule all three plans claim to enforce. N6 also declares it PUBLIC where wiring.md:533 and advanced.md:986 declare it private.

**Proposed dispatch override.** Replace Step 6's guard with the probe strip.md:1602 already uses, which tests for the SUBSCRIBER rather than a function name: `git grep -n "OnAdvancedModeChanged" -- app/src/App/MainWindow.xaml.cpp`. If it prints ANY hit, do NOT register a second subscriber and do NOT add a second fan-out function — add exactly the one line `urmsg::views::SetNetworkPageAdvanced(network_, on);` inside the existing lambda's target function, whatever it is named (`ApplyAdvanced` per wiring.md:563, or `ApplyAdvancedMode` per advanced.md:1035). Only if it prints nothing do you add the function, and declare it PRIVATE to match wiring.md:533 and advanced.md:986, not public.

### R4 - class 2 (duplicates shell-owned state) - **PARTIAL**

**Claim.** R4 Step 4 anchors its insertion on the line `InspectRailHost().Children().Append(rail_.root);`, but no `InspectRailHost` exists anywhere in the tree — the shell owns `RailHost` (MainWindow.xaml:242, written at MainWindow.xaml.cpp:371-372), and mount_check flags the same name as a Grid.Column=4 duplicate.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'RailHost' app/src/App/MainWindow.xaml.cpp && python .superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py docs/superpowers/plans/tasks/rail.md`

**Actual output.**
```
$ grep -n 'RailHost' app/src/App/MainWindow.xaml.cpp
362:  // RailHost, and NOT a host of the rail task's own. RailHost is already the
368:  // RailColumn().Width(), RailRule().Visibility() or RailHost().Visibility():
371:  RailHost().Children().Clear();
372:  RailHost().Children().Append(rail_.root);
711:  RailHost().Visibility(railVisibility);

$ python .superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py docs/superpowers/plans/tasks/rail.md
== rail.md ==
   InspectRailHost      CREATED HERE at Grid.Column=4 -- THAT COLUMN ALREADY HOLDS: RailHost

demo-collapsed elements, read from MainWindow.xaml.cpp: ThreadPane
(exit 1)

--- follow-up checks ---

$ git grep -n 'InspectRailHost'      # tracked files only
docs/superpowers/plans/tasks/rail.md:530   (Produces: named XAML elements ... `InspectRailHost`)
docs/superpowers/plans/tasks/rail.md:983   <Grid x:Name="InspectRailHost" Grid.Column="4" Visibility="Collapsed"
docs/superpowers/plans/tasks/rail.md:1045  InspectRailHost().Children().Clear();
docs/superpowers/plans/tasks/rail.md:1046  InspectRailHost().Children().Append(rail_.root);
docs/superpowers/plans/tasks/rail.md:1097  InspectRailHost().Visibility(visibility);
docs/su
```

**Verifier note.** CORE DEFECT IS REAL. R4 Step 4 (task-R4-brief.md:100, rail.md:1495) anchors on `InspectRailHost().Children().Append(rail_.root);`. That symbol exists in NO source file — git grep and grep -r agree, zero hits under app/src/ or app/build/, only the rail plan and sdd scratch. The shell owns `RailHost`: MainWindow.xaml:242 `<Grid x:Name="RailHost" Grid.Column="4">`, written at MainWindow.xaml.cpp:371-372. The auditor's line numbers are exact. I could not refute the defect itself.

NARROWER THAN CLAIMED IN TWO WAYS.

(1) The mount_check clause is R2's finding, not R4's. The evidence command runs mount_check on docs/superpowers/plans/tasks/rail.md — the WHOLE rail plan, which contains R2's XAML insert at line 983. The "Grid.Column=4 duplicate" string is emitted by that R2 line; nothing in R4 creates XAML (Files: "Modify InspectRailView.cpp; Modify MainWindow.xaml.cpp", no .xaml). Run mount_check on R4's own brief and the diagnosis changes: `InspectRailHost  *** MISSING from MainWindow.xaml ***`. Dangling reference, not duplicate mount. And the duplicate it does report is already resolved — R2 landed by mounting into RailHost() with MainWindow.xaml untouched (task-R2-report.md:21).

(2) The expensive half of the consequence is already guarded — by the shell, not the brief. R2 left an 8-line warning at MainWindow.xaml.cpp:362-370 sitting DIRECTLY above the anchor, naming this exact failure: "a second Grid in that cell would mean the shell displayed its empty one while this filled the other — 'mounted into the collapsed twin', the failure this window has already shipped four times." Step 4 requires inserting between `Append(rail_.root);` and `auto const& world = ...`, both inside BuildInspectRail(), so the implementer must read those lines to place the edit and cannot miss the w

**Consequence if not overridden.** The implementer cannot find the anchor line and either re-adds an `InspectRailHost` Grid in the cell RailHost already occupies (the shell displays its empty twin while R4 fills the other — the zero-pixel failure this window has shipped four times), or spends a round rediscovering that R2 landed as RailHost.

**Proposed dispatch override.** In Step 4, the anchor line is `RailHost().Children().Append(rail_.root);` at app/src/App/MainWindow.xaml.cpp:372 — NOT `InspectRailHost()`. Do NOT create, rename or declare any `InspectRailHost`: RailHost is already the Grid.Column=4 occupant of ChatsPage (MainWindow.xaml:242) and ApplyBreakpoint is its only visibility writer. Insert your one line immediately after :372 and before `auto const& world = urmsg::demo::GetWorld();`.

### R4 - class 2 (duplicates shell-owned state) - **PARTIAL**

**Claim.** Step 4's mandated line `urmsg::views::SetInspectRailAdvanced(rail_, demo_.advanced);` names a member that does not exist (`demo_` has zero occurrences in app/src; the member is `options_`), and its comment's premise — "AdvancedMode.h does not exist yet" — is false: Demo/AdvancedMode.h is in the tree and `urmsg::AdvancedModeEnabled()` is already the density source the conversation list reads at MainWindow.xaml.cpp:268.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -rn 'demo_' app/src/App --include='*.cpp' --include='*.h' | wc -l && ls app/src/App/Demo/AdvancedMode.h && grep -n 'AdvancedModeEnabled()' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -rn 'demo_' app/src/App --include='*.cpp' --include='*.h' | wc -l && ls app/src/App/Demo/AdvancedMode.h && grep -n 'AdvancedModeEnabled()' app/src/App/MainWindow.xaml.cpp

0
app/src/App/Demo/AdvancedMode.h
268:    urmsg::views::SetConversationListAdvanced(list_, urmsg::AdvancedModeEnabled());
469:  advanced_ = urmsg::AdvancedModeEnabled();

Corroborating runs:
$ grep -rn 'demo_' app/src            -> (no output; untracked + all extensions)
$ git grep -n 'demo_' -- app/src      -> (no output; tracked)
$ grep -n 'options_' app/src/App/MainWindow.xaml.h
  49:  urmsg::demo::DemoOptions const& DemoOptions() const { return options_; }
  110:  urmsg::demo::DemoOptions options_{};
  111:  bool advanced_ = false;
$ git grep -n 'SetAdvancedModeEnabled' -- app/src
  app/src/App/Demo/AdvancedMode.cpp:38:void SetAdvancedModeEnabled(bool on) {
  app/src/App/Demo/AdvancedMode.h:22:void SetAdvancedModeEnabled(bool on);   <- definition + decl only, ZERO callers
```

**Verifier note.** BOTH PREMISES REPRODUCE EXACTLY; THE CONSEQUENCE IS MISATTRIBUTED.

What is confirmed verbatim:
1. `demo_` = 0 occurrences. Not in tracked files (`git grep`), not untracked, not in any extension (`grep -rn 'demo_' app/src` with no --include is also empty). The member is `urmsg::demo::DemoOptions options_{}` at C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui/app/src/App/MainWindow.xaml.h:110. Step 4's line is a C2065 undeclared-identifier error. Real.
2. The comment's premise is false. Demo/AdvancedMode.h AND AdvancedMode.cpp are both in the tree, and A2-A6 (the Advanced Mode task that supposedly "lands" them) is still pending. The brief's source, docs/superpowers/plans/tasks/rail.md:1495-1508, is byte-identical to the brief -- the extraction dropped nothing.

MY ONE REFUTATION ATTEMPT, and why it fails: the prompt's strip.md pattern is real and I found it -- strip.md:827 has "First decide whether `demo_` already exists" with a branching grep. rail.md has NO such guard for R4. But the project's own log does: progress.md:337 records for Task R2 "the brief's `demo_` member is `options_`". So the compile error is pre-handled -- an R4 agent reading progress.md knows the substitution, and Step 5's build would surface it in seconds regardless. The guard does not refute the finding; it makes the second half WORSE, because it steers the agent to exactly the mechanical `options_.advanced` that is the wrong source.

WHERE THE CLAIM IS NARROWER THAN STATED:

a) The stored-preference scenario is UNREACHABLE in this tree. `SetAdvancedModeEnabled` -- which AdvancedMode.h:21 calls "The ONLY write" -- has zero callers in app/src (only its own definition and declaration). Nothing in the app can set `advanced_mode`, so `g_stored` is always false t

**Consequence if not overridden.** The line does not compile; the obvious local fix (`options_.advanced`) compiles and gives the rail a SECOND source of truth for Advanced Mode — it reads only the launch switch and ignores the stored `advanced_mode` preference. A user with the preference on then gets an advanced-density conversation list beside a normal-density rail, and neither R4 gate can see it because both gates run under --demo-advanced, where the two sources agree.

**Proposed dispatch override.** In Step 4, write `urmsg::views::SetInspectRailAdvanced(rail_, urmsg::AdvancedModeEnabled());` — NOT `demo_.advanced` and NOT `options_.advanced`. Delete the Step 4 comment's claim that AdvancedMode.h does not exist yet: app/src/App/Demo/AdvancedMode.h is already in the tree, EnterDemoMode() already calls InitAdvancedMode(options_.advanced || link.forceAdvanced) at MainWindow.xaml.cpp:468 before BuildInspectRail() runs, and AdvancedModeEnabled() already folds --demo-advanced over the stored preference. Match the conversation list's established call at MainWindow.xaml.cpp:268 exactly; contract §5 allows one reader of that truth, not two.

### R4 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** Step 10's gate states "Expected exactly: CrossfadePageSwap call sites in file : 1", but its own regex `CrossfadePageSwap\(` already matches 2 in InspectRailView.cpp today (the comment at :276 and the real call at :294), and R4 adds none — so the gate reports a violation on arrival regardless of what the implementer writes.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && powershell -Command "([regex]::Matches((Get-Content app\src\App\Views\InspectRailView.cpp -Raw),'CrossfadePageSwap\(')).Count"`

**Actual output.**
```
2
```

**Verifier note.** Evidence command printed `2`; expected literal in the brief is `1`.

Corroboration (all read-only, worktree C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui):

1. Where the 2 comes from. `grep -n CrossfadePageSwap app/src/App/Views/InspectRailView.cpp` gives three lines, two of which carry the open paren the regex requires:
   - `276: // CrossfadePageSwap(outgoing, incoming) sets incoming.Opacity(0) and fades it`  <- matches
   - `287: // CrossfadePageSwap is already gated on motion::ShouldAnimate(), so "animations`  <- no paren, does not match
   - `294:   urnw::motion::CrossfadePageSwap(swapping ? outgoing : incoming, incoming);`  <- matches
   Tree is clean (`git status --porcelain` empty) at d57089a "R3: name the probe that actually carries the assertion", so this is the state R4 arrives to.

2. The gate text is verbatim as claimed. C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui/.superpowers/sdd/2026-09-06-urmessage-demo-ui/task-R4-brief.md Step 10 (and the identical source at docs/superpowers/plans/tasks/rail.md:1555-1562) says "Expected exactly:" followed by `CrossfadePageSwap call sites in file : 1` / `PresentMode calls inside/after SetInspectRailAdvanced : 0`, then "Either number rising means the rail now blanks and re-fades on every Advanced Mode toggle." It is an absolute expectation, not a delta.

3. R4 adds no match. Steps 1-4 are the only code steps; Step 3's inserted comment says "There is deliberately NO / // PresentMode call in this function ... and CrossfadePageSwap would blank the visible" — "CrossfadePageSwap would" has no open paren, so it adds 0 to the count. The count stays 2.

4. I looked for the escape hatches you named and none exist:
   - No earlier step in the 

**Consequence if not overridden.** The implementer resolves a gate that cannot pass by either deleting the explanatory comment at :276 (destroying the note that documents why PresentMode passes `incoming` as its own outgoing) or by declaring the gate wrong and skipping it — and skipping it also discards the second, load-bearing number that proves SetInspectRailAdvanced never reaches PresentMode. R3's deliverable makes the same wrong claim, so nobody has run this regex.

**Proposed dispatch override.** In Step 10, the expected first number is 2, not 1: `[regex]::Matches($t,'CrossfadePageSwap\(')` matches the prose reference at InspectRailView.cpp:276 as well as the one real call at :294. Do NOT delete or reword that comment to make the number 1. Either change the expected output to `CrossfadePageSwap call sites in file : 2`, or change the regex to `urnw::motion::CrossfadePageSwap\(` (which matches only the call site) and keep the expected value at 1. Run the command BEFORE you edit the file and record the baseline number in your report; the second number (PresentMode after SetInspectRailAdvanced) must be 0 and is the clause that actually matters.

### R4 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** The InitialRailMode extraction and three-clause probe that the round-1 re-review assigned to R4 (progress.md:391, recorded live in the tree at MainWindow.xaml.cpp:139-145) appear nowhere in rail.md — the string `InitialRailMode` occurs 0 times in the whole task file — and the assignment's own citation is stale: it names `InspectRailView.cpp:408`, a file that contains no LogWarn at all; the uncovered arm is `urnw::LogWarn("rail: --demo=inspect but conversation 0 has no message row")` at MainWindow.xaml.cpp:420.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -c 'InitialRailMode' docs/superpowers/plans/tasks/rail.md; grep -c 'LogWarn' app/src/App/Views/InspectRailView.cpp; grep -n 'LogWarn' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
0
0
142:  // clause being the LogWarn arm below, which R4 must cover. It asserts what the
379:    urnw::LogWarn("rail: not populated - the demo world has no conversations");
420:    urnw::LogWarn("rail: --demo=inspect but conversation 0 has no message row");
```

**Verifier note.** All three clauses reproduce exactly. (1) `grep -c 'InitialRailMode' docs/superpowers/plans/tasks/rail.md` = 0. (2) `grep -c 'LogWarn' app/src/App/Views/InspectRailView.cpp` = 0 — the file is 659 lines, so :408 exists but holds a "Demo model: end-to-end encrypted" banner block, not a LogWarn; the citation is wrong, not merely out of range. (3) The only LogWarn call sites are MainWindow.xaml.cpp:379 and :420, and :420 is verbatim the claimed uncovered arm. The comment is at MainWindow.xaml.cpp:139-145 as claimed ("R4 is assigned the extraction that closes half of this: InitialRailMode(" is line 139), and progress.md:391 carries the assignment with the stale `InspectRailView.cpp:408` citation.

I tried four ways to make it wrong; all four failed.

(a) A later step in the same task file correcting it (the network.md pattern): no. rail.md's R4 runs lines 1411-1594, twelve steps, entirely device lists + the density switch. Its Produces names only `SetInspectRailAdvanced`. Its closing "What is NOT delivered here, stated plainly" section enumerates omissions and does not mention mode. Nothing in R4 touches the mode decision.

(b) Assigned elsewhere in the brief set: no. `grep -rln 'InitialRailMode' docs/superpowers/plans/` returns NOTHING — zero task files, rail.md and wiring.md included. wiring.md has no mode probe either; its only mode references are prose about what the rail shows.

(c) The dispatch pipeline carrying the progress.md note into R4 anyway — the strongest candidate, since progress.md:415 says R4 "carries the InitialRailMode extraction plus probe." It cannot. brief.py reads only `docs/superpowers/plans/tasks/<fn>` and stamps each brief "Extracted from docs/superpowers/plans/tasks/%s. This is your requirements." It never opens progress.md. And task-R4-brief.md ret

**Consequence if not overridden.** R4 ships as the last rail task with the rail's MODE decision still an inline `if` in a winrt translation unit that no probe covers, and the LogWarn fallback arm still at zero coverage — exactly the hole MainWindow.xaml.cpp:135-138 says leaves --diagnose green and the build clean while only a human looking at a screenshot can notice. The tree then carries a comment naming an R4 deliverable that R4 never produced.

**Proposed dispatch override.** Add a step before Step 5: extract the mode decision from MainWindow::BuildInspectRail into `urmsg::views::RailMode InitialRailMode(demo::DemoScreen screen, demo::Conversation const& conv);` declared in Views/InspectRailFields.h and defined in InspectRailFields.cpp, have BuildInspectRail call it instead of deciding inline, and extend InspectRailDeviceProbe with three clauses: InitialRailMode(Inspect, world.conversations.front()) == Message; InitialRailMode(Chats, world.conversations.front()) == Conversation; and InitialRailMode(Inspect, <a locally built conversation whose rows contain no RowKind::Message>) == Conversation. Build that third conversation INSIDE the assertion — do not edit DemoWorld, which is byte-fingerprinted as I10. The LogWarn arm you are covering is at app/src/App/MainWindow.xaml.cpp:420, NOT InspectRailView.cpp:408 (that file contains no LogWarn); fix the citation wherever you repeat it. State in your report that this probe asserts what the mode SHOULD be and not which write lands last, so it becomes a real gate only alongside the wiring task's single-writer rule.

### S1 - class 2 (duplicates shell-owned state) - **PARTIAL**

**Claim.** S1 creates `kStatusStripCollapseDip = 560.0` and `ShouldShowStatusStrip(double windowHeightDip)`, both already owned by the shell as `urmsg::demo::kStripMinHeightDip = 560.0` and `LayoutFor(w,h).strip` (DemoShellState.h:41,53), which ApplyBreakpoint already consumes (MainWindow.xaml.cpp:680,715) and Startup.cpp:359 already asserts at the boundary as `stripEdge`.

**Evidence command.** `git grep -n "kStripMinHeightDip\|stripEdge\|LayoutFor(" -- app/src`

**Actual output.**
```
$ git grep -n "kStripMinHeightDip\|stripEdge\|LayoutFor(" -- app/src

app/src/App/Demo/DemoShellState.h:41:inline constexpr double kStripMinHeightDip = 560.0;
app/src/App/Demo/DemoShellState.h:53:inline Layout LayoutFor(double contentWidthDip, double contentHeightDip) {
app/src/App/Demo/DemoShellState.h:57:      /*strip=*/kStripMinHeightDip <= contentHeightDip,
app/src/App/MainWindow.xaml.cpp:680:  const auto next = urmsg::demo::LayoutFor(width, height);
app/src/App/MainWindow.xaml.cpp:713:  // The strip is hidden below kStripMinHeightDip of content HEIGHT so it can
app/src/App/MainWindow.xaml.h:91:  // urmsg::demo::LayoutFor(), which answers all three content-dip thresholds
app/src/App/MainWindow.xaml.h:93:  // kStripMinHeightDip) - there is exactly one place where this app decides
app/src/App/Startup.cpp:357:  const bool wideEdge = LayoutFor(1000.0, 800.0).wide && !LayoutFor(999.9, 800.0).wide;
app/src/App/Startup.cpp:358:  const bool railEdge = LayoutFor(1500.0, 800.0).rail && !LayoutFor(1499.9, 800.0).rail;
app/src/App/Startup.cpp:359:  const bool stripEdge = LayoutFor(1200.0, 560.0).strip && !LayoutFor(1200.0, 559.9).strip;
app/src/App/Startup.cpp:361:  const bool axes = Layou
```

**Verifier note.** The duplication is REAL; two of the four consequence clauses are wrong, one in the auditor's favor and one against.

WHAT REPRODUCES. The shell already owns the entire collapse rule end to end: the constant (DemoShellState.h:41), the predicate (:53-57), the pixel (MainWindow.xaml.cpp:715, `StatusStripHost().Visibility(options_.enabled && layout_.strip ...)`), the host element (MainWindow.xaml:278) and the boundary assertion (Startup.cpp:359 `stripEdge`, printed in the `demo layout` line as `strip@560h`). S1's Produces block (strip.md:19-26) re-declares `kStatusStripCollapseDip = 560.0`, `ShouldShowStatusStrip`, and a `strip collapse` --diagnose line whose assertion (strip.md:176-182: 559 hidden / 560 shown / 900 shown) tests the identical boundary Startup.cpp:359 already tests. W1 (shell.md) is the task that produced the shell version and it has already landed. `grep -rn "kStripMinHeightDip\|LayoutFor" docs/superpowers/plans/tasks/strip.md` returns nothing — strip.md is unaware the shell version exists.

I TRIED THREE WAYS TO MAKE IT WRONG.
1. A Step 1 grep gate like network.md's — strip.md:44-48 does have one, but it only Select-Strings `DemoWorld.h` for `ConnectState`/`ServerInfo`. It never looks at the shell. No guard.
2. A later correcting step — none anywhere in strip.md, task-S1-brief.md or task-S2-brief.md.
3. Scope/tracked-file difference — none; `git grep` and the plan text agree.

CLAUSE THE AUDITOR GOT WRONG, AGAINST THEM (it is worse than claimed). "A dead gate certifying a function the real render never calls" is FALSE. strip.md:915 (S2 step 11) wires it: `const bool strip = demo_.enabled && urmsg::views::ShouldShowStatusStrip(height);` followed by a second `StatusStripHost().Visibility(strip ? ...)` write. So it is not dead code — it is a live SECOND owner

**Consequence if not overridden.** A second 560 constant and a second collapse function that no pixel depends on, plus a `strip collapse` --diagnose line duplicating the existing `demo layout` line's stripEdge term — a dead gate certifying a function the real render never calls. Worse, the new parameter is named windowHeightDip while the shipped threshold is a CONTENT height (a 480-dip window measures ~466 content), so anyone who later wires it collapses the strip at the wrong window size.

**Proposed dispatch override.** Do NOT define `kStatusStripCollapseDip` or `ShouldShowStatusStrip` and do NOT add the `strip collapse` assertion. `urmsg::demo::kStripMinHeightDip` (Demo/DemoShellState.h:41) and `LayoutFor().strip` already own the 560 CONTENT-dip collapse rule, ApplyBreakpoint already consumes them, and Startup.cpp:359's `stripEdge` term already asserts that exact boundary. Ship only `kStatusStripHeightDip`, `StatusStateWord`, `StatusLockGlyph`, `StatusEpochValue`, `StatusRecordsValue` and the single `strip fields` assertion line.

### S2 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** Steps 14 and 15 gate on log lines `window: status strip not built (demo off)`, `-> hidden (760 dip high, demo off)` and `-> shown (900 dip high, demo on)`; the live line is `window: layout wide={} rail={} strip={} (content {:.0f}x{:.0f} dip)` and it prints CONTENT dips, so a 1560x900 window logs `content 1546x892`.

**Evidence command.** `grep -o "window: layout wide=[^\"]*" .localstate-verify/logs/urmessage-app.log | sort -u`

**Actual output.**
```
$ grep -o "window: layout wide=[^\"]*" .localstate-verify/logs/urmessage-app.log | sort -u
window: layout wide=true rail=false strip=true (content 1186x792 dip)
window: layout wide=true rail=true strip=true (content 1546x892 dip)

Corroborating (same worktree):
app/src/App/MainWindow.xaml.cpp:669-720  ApplyBreakpoint()
  const double width  = root.ActualWidth();
  const double height = root.ActualHeight();
  const auto next = urmsg::demo::LayoutFor(width, height);
  ...
  StatusStripHost().Visibility(options_.enabled && layout_.strip ? Visible : Collapsed);
  urnw::LogInfo("window: layout wide={} rail={} strip={} (content {:.0f}x{:.0f} dip)",
                layout_.wide, layout_.rail, layout_.strip, width, height);

app/src/App/Demo/DemoShellState.h:9-14
  // UNITS: LayoutFor takes CONTENT-ROOT dips, not window dips. ApplyBreakpoint
  // passes Content().ActualWidth()/ActualHeight(), and the two differ by the
  // window frame ...
app/src/App/Demo/DemoShellState.h:41  inline constexpr double kStripMinHeightDip = 560.0;

app/src/App/WindowShell.h:31-32  kDemoWidthDips = 1560; kDemoHeightDips = 900;

app/tools/verify-render.ps1:199-210
  [void][Win]::GetWindowRect($win.Hwnd, [ref]$r
```

**Verifier note.** REPRODUCED EXACTLY. The evidence command printed the literal string `content 1546x892` the auditor predicted. Demo launch size is 1560x900 (WindowShell.h:31-32); the frame costs 14 dip wide and 8 dip high (1560->1546, 900->892; 1200->1186, 800->792). verify-render.ps1:199 uses GetWindowRect, so its `dips : 1560x900` line is WINDOW dips, while ApplyBreakpoint logs Content().ActualHeight(). Step 15's rule "the first line's height must equal the height the harness printed in step 13" therefore compares 892 against 900 and can never hold.

The defect is worse than described, in a way that strengthens it. Step 11 of the brief (C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui/.superpowers/sdd/2026-09-06-urmessage-demo-ui/task-S2-brief.md:555-582) does not merely risk a duplicate line -- it instructs one. It tells the implementer to add `const double height = root.ActualHeight();` plus a second LogInfo "after the existing LogInfo", and to build `breakpointApplied_ / wide_ / stripVisible_ / demo_ / urmsg::views::ShouldShowStatusStrip(height)` from a new `Views/StatusStripRules.h`. None of those symbols exist; the shipped tree already has the entire collapse rule under different names -- `layoutApplied_`, `layout_.strip`, `options_`, `urmsg::demo::LayoutFor` in Demo/DemoShellState.h, `kStripMinHeightDip = 560.0`, the demo gate already ANDed at MainWindow.xaml.cpp:715, and the log line at :719 already carrying `strip={}`. Following Step 11 as written produces a parallel duplicate mechanism and a second log line reporting the same number the shipped one already reports. The brief's line anchors are also stale (it cites ApplyBreakpoint at line 164; it is at 669).

THREE RESCUE ATTEMPTS, ALL FAILED. (1) Step 13's guard -- "if it prints some t

**Consequence if not overridden.** Step 15's rule 'the first line's height must equal the height the harness printed in step 13' is arithmetically impossible (892 != 900) and step 14's '760 dip high' would be ~746, so the collapse branch is never actually proven — the implementer either invents a second log line that duplicates the shipped one or declares the gate passed on a line that never printed.

**Proposed dispatch override.** Steps 14 and 15: do NOT add a `window: status strip -> ...` log line and do not expect one. ApplyBreakpoint already logs `window: layout wide=.. rail=.. strip=.. (content WxH dip)` in CONTENT dips, about 14 fewer than the window dips verify-render prints. Assert `strip=false` on that existing line for the no-switch launch, `strip=true` at 1560x900 (which logs `content 1546x892`), and `strip=false` after the 1200x500 resize — never a window-dip figure.

### S2 - class 8 (cross-task conflict) - **PARTIAL**

**Claim.** S2 declares `MainWindow::BuildStatusStrip()`, member `statusStrip_` and a constructor call, while wiring.md Task W6 (also undispatched) mounts the same `MakeStatusStrip(GetWorld())` into the same StatusStripHost from `BuildDemoViews()` into a member named `strip_`, which W7's `ApplyAdvanced` then calls `SetStatusStripAdvanced(strip_, on)` on.

**Evidence command.** `grep -n "strip_\|ToggleStatusDrawer\|drawerOpen_" docs/superpowers/plans/tasks/wiring.md | head -12`

**Actual output.**
```
$ grep -n "strip_\|ToggleStatusDrawer\|drawerOpen_" docs/superpowers/plans/tasks/wiring.md | head -12

330:- Produces: `void MainWindow::ToggleStatusDrawer();` members `strip_`, `network_`, `settings_`, `developer_`, `drawerOpen_`
366:  void ToggleStatusDrawer();
368:  urmsg::views::StatusStripView strip_{};
372:  bool drawerOpen_ = false;
380:  strip_ = urmsg::views::MakeStatusStrip(urmsg::demo::GetWorld());
382:  if (strip_.root) StatusStripHost().Children().Append(strip_.root);
383:  if (strip_.strip) {
384:    strip_.strip.Click([weak = get_weak()](auto const&, auto const&) {
385:      if (auto self = weak.get()) self->ToggleStatusDrawer();
427:- [ ] **Step 4: Add `ToggleStatusDrawer()` to `MainWindow.xaml.cpp`.**
430:void MainWindow::ToggleStatusDrawer() {
431:  drawerOpen_ = !drawerOpen_;
```

**Verifier note.** The collision is real and unguarded. The claimed failure mode is not.

CONFIRMED — the duplication exists, with no guard on either side.
strip.md S2 (.../docs/superpowers/plans/tasks/strip.md:848,853,879,884-897) declares `void BuildStatusStrip();`, member `urmsg::views::StatusStripView statusStrip_{};`, a `BuildStatusStrip();` constructor call after `BuildConversationList();`, and a body doing `statusStrip_ = MakeStatusStrip(GetWorld()); StatusStripHost().Children().Clear(); ...Append(statusStrip_.root);`. wiring.md W6 (.../tasks/wiring.md:368,380-382) does the identical mount into the identical host under `strip_`. Neither is dispatched: tasks #18 and #20 are both pending, and the landed `.../app/src/App/MainWindow.xaml.h/.cpp` contains neither member (only W3's `options_` and its `StatusStripHost().Visibility(...)` line at MainWindow.xaml.cpp:715). I looked hard for the network.md-style Step-1 grep guard and it is not here: strip.md has "already landed" greps, but only for `demo_` (strip.md:827) and for the advanced subscriber (strip.md:1599) — neither covers the strip member or the mount. wiring.md has NO source-grep guard anywhere; every `Select-String` in it (wiring.md:287-303, 462-463, 650-651, 1305-1395) is a log assertion, not a landed-state check. The plan even shows this dedup pass exists and missed here: 2026-09-06-urmessage-demo-ui.md drops W2 and A1 because "all three of F6, W2 and A1 created Demo/AdvancedMode.h/.cpp" — the same sweep did not catch the strip.

REFUTED — the two clauses of the stated consequence.
1. "W7's fan-out pointing at a member S2 never created" is wrong. W7's Consumes line (wiring.md:529) lists `strip_` as coming from W6, and W6 creates it. Once both land, both `statusStrip_` and `strip_` exist; W7 compiles.
2. "the strip's density s

**Consequence if not overridden.** Whichever lands second collides: two members holding the same view, two mount sites appending into StatusStripHost, and W7's advanced-mode fan-out pointing at a member S2 never created — so the strip's density silently stops working.

**Proposed dispatch override.** Name the member `strip_`, not `statusStrip_`, and do NOT add `MainWindow::BuildStatusStrip()` or a constructor call. wiring.md W6 Step 2 already mounts `strip_ = urmsg::views::MakeStatusStrip(urmsg::demo::GetWorld())` into StatusStripHost from `BuildDemoViews()`, which runs inside `EnterDemoMode()` and is therefore already demo-gated — S2's own `if (!demo_.enabled) return;` re-implements a gate the shell owns. S2 delivers `Views/StatusStripView.{h,cpp}` and touches MainWindow only to add the two `#include`s and the `strip_` member declaration.

### S3 - class 8 (cross-task conflict) - **CONFIRMED**

**Claim.** S3 Steps 6 and 7 declare `MainWindow::ToggleStatusDrawer()` and wire `statusStrip_.strip.Click`; wiring.md Task W6 declares the identical method, the identical Click wiring and a `drawerOpen_` bool, while S3 deliberately keeps no bool ('Visibility IS the state').

**Evidence command.** `grep -n "ToggleStatusDrawer\|drawerOpen_\|strip_.strip.Click" docs/superpowers/plans/tasks/wiring.md`

**Actual output.**
```
$ grep -n "ToggleStatusDrawer\|drawerOpen_\|strip_.strip.Click" docs/superpowers/plans/tasks/wiring.md

330:- Produces: `void MainWindow::ToggleStatusDrawer();` members `strip_`, `network_`, `settings_`, `developer_`, `drawerOpen_`
366:  void ToggleStatusDrawer();
372:  bool drawerOpen_ = false;
384:    strip_.strip.Click([weak = get_weak()](auto const&, auto const&) {
385:      if (auto self = weak.get()) self->ToggleStatusDrawer();
427:- [ ] **Step 4: Add `ToggleStatusDrawer()` to `MainWindow.xaml.cpp`.**
430:void MainWindow::ToggleStatusDrawer() {
431:  drawerOpen_ = !drawerOpen_;
432:  urmsg::views::SetStatusStripDrawerOpen(strip_, drawerOpen_);
433:  urnw::LogInfo("window: status drawer {}", drawerOpen_ ? "open" : "closed");
1190:`ClearMessageSelection` or `ToggleStatusDrawer`. Step 10 turns that into log evidence scoped

$ grep -rn "ToggleStatusDrawer" docs/superpowers/plans/
strip.md:1051:On `MainWindow` (`MainWindow.xaml.h`, private): `void ToggleStatusDrawer();`
strip.md:1240:- [ ] **Step 6: Declare `ToggleStatusDrawer` in `MainWindow.xaml.h`.**
strip.md:1257:  void ToggleStatusDrawer();
strip.md:1267:    if (auto self = weak.get()) self->ToggleStatusDrawer();
strip.md:127
```

**Verifier note.** Reproduces verbatim, and the collision is BROADER than the grep shows -- but the auditor's stated consequence is misdiagnosed in both branches.

REFUTATION ATTEMPTS, ALL FAILED:
(1) Scope/superseded-plan. Both files are linked from the same master plan docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md, and both groups are live in its table: "Status strip | S1-S4 | 50" and "Click graph and ambient activity | W5-W9 | 50". The nine group step counts sum to exactly 479, matching the header "46 tasks, 479 steps", so neither group is a leftover. The plan even documents de-duplicating this exact class of collision at assembly ("W2 and A1 are deliberately absent... only whole-plan assembly could see the collision") -- ToggleStatusDrawer was missed by that same pass. (W6's Interfaces still cites the dropped W2, so wiring.md was only partly patched.)
(2) A network.md-style Step 1 grep guard. strip.md DOES carry that pattern -- line 827: "First decide whether `demo_` already exists... skip the `demo_` member below" -- but it covers `demo_` only. W6 has no gate whatsoever: Step 1 IS its first step and adds `void ToggleStatusDrawer();` and `bool drawerOpen_ = false;` unconditionally. Cross-references between the two files: zero, both directions (greps above).
(3) Shell/tracking fact. git grep (tracked) and grep -r agree at 11 hits; find shows one wiring.md. No app/build/ artifact inflation.
(4) A later correcting step. wiring.md:1190 is W9 prose merely naming ToggleStatusDrawer as something autoplay must not call. Not a correction.

BROADER THAN CLAIMED: it is not only the method and the bool. S2 Produces `urmsg::views::StatusStripView statusStrip_{}` plus `BuildStatusStrip()`; W6 Step 1 adds a SECOND member `strip_` of the same type, and W6 Step 2 makes a second `MakeStatusStr

**Consequence if not overridden.** Two tasks define the same method two different ways. If both land, the second redefinition is a link error; if W6 lands alone, `drawerOpen_` and the drawer's real Visibility can disagree after the 150 ms dismiss animation, so one click is swallowed.

**Proposed dispatch override.** Do NOT declare `ToggleStatusDrawer()` and do NOT wire `strip.Click` — wiring.md W6 Steps 1, 2 and 4 own both. S3 delivers only `MakeStatusDrawer`, the `v.drawer` population in `MakeStatusStrip`, `SetStatusStripDrawerOpen`, the `StatusDrawerHost` markup, its mount, and verify-drawer.ps1. Add one note for W6: drop its `drawerOpen_` member and read `strip_.drawer.Visibility()` instead, so the drawer has one state and not two.

### S3 - class 3 (G4 honesty violation) - **CONFIRMED**

**Claim.** The drawer presents three named live hops and per-node health as fact — header `RELAY PATH`, container name `Relay path preview`, rows `This device / This computer`, `URnetwork / 3 hops`, `Message server / urmsg-01.ur.io`, automation names ending `online`/`offline` — with no 'Demo model:' framing anywhere, while the rail's equivalent header ships `Demo model: end-to-end encrypted`.

**Evidence command.** `git grep -n "RELAY PATH\|Relay path preview\|Demo model: end-to-end" -- docs/superpowers/plans/tasks/strip.md app/src/App/Views/InspectRailView.cpp`

**Actual output.**
```
$ git grep -n "RELAY PATH\|Relay path preview\|Demo model: end-to-end" -- docs/superpowers/plans/tasks/strip.md app/src/App/Views/InspectRailView.cpp

app/src/App/Views/InspectRailView.cpp:410:  title.Text(L"Demo model: end-to-end encrypted");
docs/superpowers/plans/tasks/strip.md:1094:constexpr wchar_t kDrawerName[] = L"Relay path preview";
docs/superpowers/plans/tasks/strip.md:1095:constexpr wchar_t kRelayHeader[] = L"RELAY PATH";
docs/superpowers/plans/tasks/strip.md:1428:- Top row: a **28 DIP** header reading **`RELAY PATH`**, letterspaced, on the left, with **`3`** right-aligned.

--- corroborating runs ---

$ grep -n "Demo model" docs/superpowers/plans/tasks/strip.md
(no output)

$ git grep -n "Demo model" -- docs/superpowers/plans
(no output)

$ grep -n "^## Task" docs/superpowers/plans/tasks/strip.md
7:## Task S1: Status strip 1/4 — the pure rules and their `--diagnose` assertions
363:## Task S2: Status strip 2/4 — the 26 DIP strip, built only under `--demo`, collapsing at 560
1041:## Task S3: Status strip 3/4 — the preview drawer the strip raises
1486:## Task S4: Status strip 4/4 — Advanced Mode adds epoch, session and rec/s inline
  -> hits at 1094/1095/1428 are all insid
```

**Verifier note.** Every factual element reproduces, and the consequence's precondition is real code.

SCOPE HOLDS. Task headers put S3 at strip.md:1041-1485; all four hits (1094, 1095, 1428, 1432) are inside it. Not a neighboring task's copy.

THE CLAIM'S PARTS, EACH VERIFIED. Header `RELAY PATH` (1095, restated 1428). Container automation name `Relay path preview` (1094, applied via SetName(drawer,...)). Rows `This device / This computer`, `URnetwork / 3 hops`, `Message server / urmsg-01.ur.io` (1432). Per-node health as fact is stronger than the grep alone shows — S3 step 3 fills row.dot green on node.healthy, appends L"  offline" to the meta text, and sets each row's automation name to label + ", " + subLabel + ", " + (healthy ? "online" : "offline"). No 'Demo model:' string anywhere in strip.md, and I read all 13 of S3's steps: no later step corrects it. The only honesty-adjacent word is "preview", and S3's own prose fixes its meaning as preview OF THE NETWORK PAGE'S FULLER VIEW ("the full map, the per-hop timings and the device list are the Network page's"), not "not real".

THE CONSEQUENCE IS REAL, NOT HYPOTHETICAL. --demo-watermark=off exists and does exactly what the claim assumes: DemoSwitches.cpp:60 clears o.watermark, MainWindow.xaml.cpp:490 collapses DemoChip on it.

THE GUARD HUNT — where I expected to refute, and did not. There IS a binding global honesty constraint: G4 in the parent plan, under a section reading "Where a task step conflicts with anything here, this section wins." But G4's text is scoped to encryption of a message: "No copy in the demo may state that a message *was* encrypted as a fact about a real operation." It does not reach relay topology, hop counts, a named host, or node health. The guard exists, is binding, and does not cover this drawer. An implemen

**Consequence if not overridden.** A screenshot taken with --demo-watermark=off shows a panel asserting a live three-hop relay to a named message server with every hop healthy. Nothing in the drawer says that is a model, and the chip that would have said so is exactly what the owner switches off when presenting.

**Proposed dispatch override.** Step 3: set `kRelayHeader[] = L"DEMO MODEL: RELAY PATH"` and `kDrawerName[] = L"Demo model: relay path preview"`, and update Step 11's expected header text to match. The drawer states three live hops and per-node health as fact and carries no other framing; the rail's equivalent header already ships `Demo model: end-to-end encrypted` (InspectRailView.cpp:410) for this reason, and --demo-watermark=off removes the chip.

### S4 - class 8 (cross-task conflict) - **PARTIAL**

**Claim.** Step 5's decision grep matches MainWindow.xaml.cpp:267 (a comment) and :468 (`InitAdvancedMode(options_.advanced || link.forceAdvanced)`), routing the implementer to the 'another surface landed both already — add exactly this one line inside the existing lambda's body' branch, but no `urmsg::OnAdvancedModeChanged(` registration exists anywhere in the file.

**Evidence command.** `grep -n "OnAdvancedModeChanged\|InitAdvancedMode\|urmsg::OnAdvancedModeChanged(" app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ grep -n "OnAdvancedModeChanged\|InitAdvancedMode\|urmsg::OnAdvancedModeChanged(" app/src/App/MainWindow.xaml.cpp
267:    // OnAdvancedModeChanged.
468:  urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced);

The brief's OWN Step 5 decision command (PowerShell, not grep) prints the same two lines:
$ powershell -ExecutionPolicy Bypass -Command "Select-String -Path '.../app/src/App/MainWindow.xaml.cpp' -Pattern 'OnAdvancedModeChanged|InitAdvancedMode'"
app\src\App\MainWindow.xaml.cpp:267:    // OnAdvancedModeChanged.
app\src\App\MainWindow.xaml.cpp:468:  urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced);

Tracked-file sweep for any registration (git grep, so no app/build/ artifacts):
$ git grep -n "OnAdvancedModeChanged" -- app/src
app/src/App/Demo/AdvancedMode.cpp:51:void OnAdvancedModeChanged(std::function<void(bool)> fn) {
app/src/App/Demo/AdvancedMode.h:31:void OnAdvancedModeChanged(std::function<void(bool)> fn);
app/src/App/MainWindow.xaml.cpp:267:    // OnAdvancedModeChanged.
(the declaration, the definition, and a comment — zero call sites app-wide)

Line 267 in context (BuildConversationList) is prose, not code:
    // Contract 5: Advanced Mode has
```

**Verifier note.** The routing defect is CONFIRMED exactly as claimed; one clause of the claimed consequence is REFUTED, which narrows the severity.

CONFIRMED, precisely:
1. The decision grep fires on two lines, so the implementer takes the "**If it prints one or more lines**" branch, whose instruction is "another surface landed both already ... add exactly this one line inside the existing lambda's body". There is no lambda: zero `OnAdvancedModeChanged(` call sites exist in the entire app (only the decl in AdvancedMode.h:31 and the def in AdvancedMode.cpp:51). The branch is not executable as written. Line 267 is a comment; line 468 is `InitAdvancedMode` in `EnterDemoMode()`.
2. The Step 1 gate does NOT rescue this (the network.md pattern the prompt warned me about does not apply here). Step 1 only Select-Strings `Demo/AdvancedMode.h` for four declarations and `DemoWorld.h` for three fields — both pass; it never looks at MainWindow.xaml.cpp. I ran it: all four declarations present.
3. No later step corrects it. Steps 6-11 are: build; two `verify-render.ps1` launches (`--demo=chats --demo-advanced`, then `--demo=chats`); a prefs-file check; a `Select-String ... 'status strip advanced ->' | Select -Last 2` log check; commit. Nothing calls `SetAdvancedModeEnabled` at runtime, and the seeding block (`const bool advanced = urmsg::AdvancedModeEnabled(); SetStatusStripAdvanced(...); LogInfo(...)`) is added "**Either way**" — so it alone satisfies steps 7, 8 and 10 whether or not a subscriber was ever registered. Gate-blindness confirmed: S4 goes green with a strip that cannot react to a live toggle.
4. Not a tool artifact: grep, the brief's own PowerShell Select-String, and `git grep` over tracked files all print the same thing.

REFUTED clause (this is the narrowing): "the other adds a second 

**Consequence if not overridden.** Neither branch is executable: one sends the implementer to edit a lambda that does not exist, the other adds a second `InitAdvancedMode(demo_.advanced)` in the constructor that drops `link.forceAdvanced` and runs before EnterDemoMode. If it lands the wrong way, the strip's density never responds to a live Settings toggle — and every S4 gate (steps 7-10 are launch-switch screenshots and their seeding log lines) still reads green, because none of them toggles the mode at runtime.

**Proposed dispatch override.** Step 5: do NOT run the grep and do NOT add `InitAdvancedMode` or an `OnAdvancedModeChanged` registration. MainWindow.xaml.cpp:468 already calls `urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced)` in EnterDemoMode, and the comment at :265-267 assigns the single subscriber to the wiring surface — wiring.md W7 Step 3 registers it and its `ApplyAdvanced` already calls `SetStatusStripAdvanced(strip_, on)`. S4 delivers `SetStatusStripAdvanced` plus the one seeding call and log line at the strip's build site, and nothing else in MainWindow.

### W5 - class 8 (cross-task conflict) - **PARTIAL**

**Claim.** W5 Step 6's deep link walks conversation 0's rows in reverse for the last RowKind::Message, which is index 23 -- "Retrying the attachment." at 12:11, outgoing, DeliveryState::Pending -- not kInspectTargetRowId's c0-r12; because DemoWorld only fills receivedAtLabel/deliveredTo/readBy when an outgoing row reached Delivered or Read, c0-r23 has an empty Received field and empty device lists.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && python -c "import re;s=open('app/src/App/Demo/DemoWorld.cpp',encoding='utf-8').read();b=s[s.index('AppendRows(c0'):s.index('// c1 -')];r=re.findall(r'\{(kSep|kMsg|kSys), L\"([^\"]*)\",',b);i=max(k for k,v in enumerate(r) if v[0]=='kMsg');print('last RowKind::Message = c0-r%d'%i);print([l.strip() for l in b.splitlines() if l.strip().startswith('{kMsg')][-1])" && grep -n 'kInspectTargetRowId\[\]' app/src/App/Demo/DemoWorld.h`

**Actual output.**
```
last RowKind::Message = c0-r23
{kMsg, L"", L"Retrying the attachment.", L"12:11", true, kPend, L"", false},
137:inline constexpr wchar_t kInspectTargetRowId[] = L"c0-r12";
```

**Verifier note.** THE CORE DEFECT IS REAL AND REPRODUCES EXACTLY. THE SECOND CLAIMED CONSEQUENCE IS REFUTED.

CONFIRMED (four independent anchors, all read, not inferred):

1. The pick. task-W5-brief.md:236-247 (verbatim from the uncorrected docs/superpowers/plans/tasks/wiring.md:230-241) walks `rows.rbegin()..rend()` for the first `RowKind::Message`. Enumerating c0's 24 row specs by index confirms the id arithmetic: DemoWorld.cpp:117 builds `r.id = "c<conv>-r<index>"` with `index` incrementing over EVERY spec including kSep and kSys, so spec index 23 is literally id "c0-r23". c0-r23 = {kMsg, L"", L"Retrying the attachment.", L"12:11", true, kPend}. kInspectTargetRowId is L"c0-r12" (DemoWorld.h:137) = {kMsg, L"", L"Nice. I'll take the identicon pass this morning.", L"11:08", true, kRead}. Different rows.

2. The empty fields. DemoWorld.cpp:177-184: for an outgoing row, `const bool reached = (state == Delivered || state == Read); if (reached) { n.receivedAtLabel = n.sentAtLabel; n.deliveredTo = AllMemberDevices(c); if (Read) n.readBy = n.deliveredTo; }`. kPend is neither, so c0-r23 gets receivedAtLabel = L"", deliveredTo = {}, readBy = {}. c0-r12 is kRead, so it gets all three — the committed --diagnose output corroborates: `.verify/diagnose-base.txt:61` prints `pick "c0-r12" state Read delivered-by 7 read-by 7`.

3. Step 8 does contradict it. task-W5-brief.md Step 8 expects the rail to show "a lock header, then labelled fields, then the delivered-by and read-by device lists with device names." Against c0-r23 those two lists are empty. No later step in the 328-line brief corrects the pick — I read all eleven; Steps 9 and 10 assert on log lines only, and the brief's Interfaces/Consumes list names NEITHER PickInspectMessage NOR kInspectTargetRowId (grep count over the whole brief: 0), so th

**Consequence if not overridden.** --demo=inspect, the one showcase capture an agent can reach without synthesised input, opens the rail on a Pending send with a blank Received row and empty delivered-by / read-by lists -- which is precisely what W5 Step 8 tells the implementer to expect to see populated ("the delivered-by and read-by device lists with device names"), so the step reads as a regression in R3 and sends the fix into the wrong file. It also breaks InspectRailFields' probe assertion that the pick and the constant designate one row.

**Proposed dispatch override.** Replace the reverse-scan loop in Step 6 with the shared pick: `#include "Views/InspectRailFields.h"` and `if (pendingLink_.selectMessage) { auto const& conv = urmsg::demo::GetWorld().conversations.front(); if (auto const* picked = urmsg::views::PickInspectMessage(conv)) SelectMessage(picked->id); else urnw::LogWarn("window: --demo=inspect but conversation 0 has no message row"); }`. Do NOT designate the last RowKind::Message: that is c0-r23, Pending, with no received-at and no devices. Also fix the stale comment at Demo/DemoShellState.h:68, which still reads "and pre-select its newest Message row" -- it must read "and pre-select kInspectTargetRowId (c0-r12), the row PickInspectMessage returns".

### W5 - class 2 (duplicates shell-owned state) - **CONFIRMED**

**Claim.** W5 Step 1 tells the implementer to add `urmsg::views::ConversationListView list_{}`, `ThreadView thread_{}` and `InspectRailView rail_{}` to MainWindow's private section, but MainWindow.xaml.h already declares all three (lines 120, 67, 80) and already includes all three view headers (lines 25-27).

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'thread_{}\|rail_{}\|list_{}\|Views/ConversationListView.h\|Views/InspectRailView.h\|Views/ThreadView.h' app/src/App/MainWindow.xaml.h`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'thread_{}\|rail_{}\|list_{}\|Views/ConversationListView.h\|Views/InspectRailView.h\|Views/ThreadView.h' app/src/App/MainWindow.xaml.h

25:#include "Views/ConversationListView.h"
26:#include "Views/InspectRailView.h"
27:#include "Views/ThreadView.h"
67:  urmsg::views::ThreadView thread_{};
80:  urmsg::views::InspectRailView rail_{};
120:  urmsg::views::ConversationListView list_{};

Corroborating (all against the same clean worktree, HEAD=d57089a, `git status --porcelain` empty):

task-W5-brief.md lines 78-80, inside "Step 1 ... Add to `private:`, under `void DrainDeepLink();`":
  78	  urmsg::views::ConversationListView list_{};
  79	  urmsg::views::ThreadView thread_{};
  80	  urmsg::views::InspectRailView rail_{};

$ grep -n '#include <vector>' app/src/App/MainWindow.xaml.h ; echo "rc_vector=$?"
rc_vector=1
$ grep -n 'Demo/DemoWorld.h' app/src/App/MainWindow.xaml.h ; echo "rc_demoworld=$?"
rc_demoworld=1

$ for f in ConversationListView InspectRailView ThreadView; do printf "%s: " "$f"; grep -c '#pragma once' "app/src/App/Views/$f.h"; done
ConversationListView: 1
InspectRailV
```

**Verifier note.** Reproduced exactly, line numbers included. The worktree is clean at d57089a on branch demo-ui and W5 is unexecuted (no BuildDemoViews/SelectConversation/openConversationId_ anywhere), so this is the real pre-W5 baseline, not a half-applied state. The three members were landed by three earlier surface tasks — thread_ by 970d91e (T3), rail_ by 84a6af2 (R2), list_ by 7a1bc19 (L1) — and W5 Step 1 re-declares all three verbatim at brief lines 78-80.

I hunted for four outs and all four failed:
(1) A preceding wiring task that removes them: W1/W3/W4 all have reports and are at HEAD; none removes them, and W6 (wiring.md:532) *consumes* list_ and rail_, so nothing pending deletes them either.
(2) A guard elsewhere in the brief: I read all 328 lines. Steps 2-11 never revisit the header; there is no "if already declared, skip" anywhere in W5.
(3) A plan-level guard: the parent plan's Global Constraints (G1, G2, G2a, G2b, G3, G4) contain no rule about pre-existing declarations. This is the strongest evidence *for* the finding, not against it — the exact guard pattern the auditor's mental model expects DOES exist in this plan family (network.md:158 "If it **is** already declared, skip this step and step 7's function body"; network.md:1527; strip.md:827 "First decide whether `demo_` already exists"; rail.md:214's STOP rule). The author writes this guard when they know a member may already be landed. wiring.md has zero such hits. So the omission in W5 is a gap, not a convention I misread.
(4) A grep-scope artifact: not applicable — the command greps one tracked file, not a recursive sweep, so the git-grep-vs-grep-r/app/build divergence cannot explain it.

Two narrowings the parent should carry, neither of which rescues the brief:

(a) The include half of the claim is TRUE but BENIGN.

**Consequence if not overridden.** Three duplicate class-member declarations -- MainWindow.xaml.h does not compile (C2374 member redefinition) on the first build of Step 4, and the implementer's first instinct will be to rename W5's members, which would fork the click graph away from the members BuildThread/BuildInspectRail/BuildConversationList already use.

**Proposed dispatch override.** In Step 1 add ONLY `#include <vector>`, `#include "Demo/DemoWorld.h"` and the six method declarations. Do NOT re-add list_, thread_ or rail_ -- MainWindow.xaml.h already declares them at lines 120, 67 and 80 -- and do NOT re-add Views/ConversationListView.h, Views/InspectRailView.h or Views/ThreadView.h, which are already included at lines 25-27. Add only the two genuinely new members, `std::wstring openConversationId_;` and `std::wstring selectedMessageId_;`.

### W6 - class 2 (duplicates shell-owned state) - **PARTIAL**

**Claim.** W6 declares and defines `void MainWindow::ToggleStatusDrawer()` plus a `drawerOpen_` bool and a `strip_` member, but strip.md's status-strip 3/4 -- which lands before W6 in the plan's own dependency order -- already declares ToggleStatusDrawer (strip.md:1257), defines it (strip.md:1273), holds the view in `statusStrip_` (strip.md:853), and deliberately uses the drawer's Visibility as the state "so there is no second bool to disagree with the tree".

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'ToggleStatusDrawer\|statusStrip_{}' docs/superpowers/plans/tasks/strip.md docs/superpowers/plans/tasks/wiring.md && sed -n '206,220p' docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md`

**Actual output.**
```
docs/superpowers/plans/tasks/strip.md:386:urmsg::views::StatusStripView statusStrip_{};
docs/superpowers/plans/tasks/strip.md:853:  urmsg::views::StatusStripView statusStrip_{};
docs/superpowers/plans/tasks/strip.md:1051:On `MainWindow` (`MainWindow.xaml.h`, private): `void ToggleStatusDrawer();`
docs/superpowers/plans/tasks/strip.md:1240:- [ ] **Step 6: Declare `ToggleStatusDrawer` in `MainWindow.xaml.h`.**
docs/superpowers/plans/tasks/strip.md:1257:  void ToggleStatusDrawer();
docs/superpowers/plans/tasks/strip.md:1267:    if (auto self = weak.get()) self->ToggleStatusDrawer();
docs/superpowers/plans/tasks/strip.md:1273:void MainWindow::ToggleStatusDrawer() {
docs/superpowers/plans/tasks/wiring.md:330:- Produces: `void MainWindow::ToggleStatusDrawer();` members `strip_`, `network_`, `settings_`, `developer_`, `drawerOpen_`
docs/superpowers/plans/tasks/wiring.md:366:  void ToggleStatusDrawer();
docs/superpowers/plans/tasks/wiring.md:385:      if (auto self = weak.get()) self->ToggleStatusDrawer();
docs/superpowers/plans/tasks/wiring.md:427:- [ ] **Step 4: Add `ToggleStatusDrawer()` to `MainWindow.xaml.cpp`.**
docs/superpowers/plans/tasks/wiring.md:430:void MainWindow::ToggleStatus
```

**Verifier note.** The collision is CONFIRMED exactly as claimed; the claimed *fallback* consequence is REFUTED in mechanism. Hence PARTIAL.

CONFIRMED — the build-stopping half:
- Order holds. Plan table (2026-09-06-urmessage-demo-ui.md:206-220) is explicitly "a dependency ordering, not a preference", and Status strip (S1-S4) sits above Click graph (W5-W9). S3 lands before W6.
- Same class, same two files. strip.md:1041 S3 Files = .../app/src/App/MainWindow.xaml.h + .xaml.cpp; wiring.md:326 W6 Files = app/src/App/MainWindow.xaml.h + .xaml.cpp. Same MainWindow.
- S3 Step 6 (strip.md:1240-1257) declares `void ToggleStatusDrawer();` in private; W6 Step 1 (wiring.md:366) declares the identical signature in private. Redeclaring a member function in a class body is ill-formed — the header alone stops the build, before the two definitions (strip.md:1273, wiring.md:430) ever collide at the .cpp.
- The Visibility-as-state quote is verbatim at strip.md:1278: "Visibility IS the state; there is no second bool to disagree with the tree." W6 adds `bool drawerOpen_ = false;` (wiring.md:372) and toggles it — the exact second bool S3 refused.

I searched for the escape hatch and there is none:
- `grep -n 'statusStrip_|BuildStatusStrip|already|StatusStripHost|StatusDrawerHost' wiring.md` returns no guard. W6's Consumes (wiring.md:329) lists only the header symbols (`StatusStripView`, `MakeStatusStrip`, `SetStatusStripDrawerOpen`) — it never names `statusStrip_`, `BuildStatusStrip`, or S3. W6 believes it is building the strip from scratch. No "if not already present" language, no Step-1 gate (unlike network.md's pattern), no later step in wiring.md that reconciles.
- Not a tracked-vs-untracked artifact: nothing landed yet. `grep -n 'ToggleStatusDrawer|statusStrip_|strip_|BuildDemoViews|BuildStatusStrip|dra

**Consequence if not overridden.** MainWindow.xaml.h gets two declarations of ToggleStatusDrawer and MainWindow.xaml.cpp two definitions -- the build stops. If the implementer resolves it by renaming instead, the window carries two StatusStripView members and two Click handlers on the same Button, so one activation toggles the drawer twice and it never appears to open.

**Proposed dispatch override.** Do NOT declare ToggleStatusDrawer, drawerOpen_ or a strip_ member: strip.md 3/4 already ships MainWindow::ToggleStatusDrawer, the statusStrip_ member and the strip Click wiring, and its drawer state is the drawer's own Visibility by design. Delete Steps 2 and 4 of W6 entirely and delete `strip_` and `drawerOpen_` from the Produces list; W7 must then call `urmsg::views::SetStatusStripAdvanced(statusStrip_, on)`, not `strip_`.

### W6 - class 2 (duplicates shell-owned state) - **PARTIAL**

**Claim.** W6 Step 1/Step 3 create the members `network_`, `settings_`, `developer_` and their MakeNetworkPage / MakeSettings / MakeDeveloper mounts, all of which are already Produced by tasks that land before W6: N3 (network.md:596, 929, 941), A4 (advanced.md:349, 788, 841) and A5 (advanced.md:1183, 1547, 1585), along with BuildNetworkPage, BuildSettings and BuildDeveloper.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'network_{}\|settings_{}\|developer_{}\|BuildNetworkPage\|BuildSettings();\|BuildDeveloper();' docs/superpowers/plans/tasks/network.md docs/superpowers/plans/tasks/advanced.md | head -20`

**Actual output.**
```
$ grep -n 'network_{}\|settings_{}\|developer_{}\|BuildNetworkPage\|BuildSettings();\|BuildDeveloper();' docs/superpowers/plans/tasks/network.md docs/superpowers/plans/tasks/advanced.md | head -20

network.md:596:- Produces: `struct urmsg::views::NetworkPageView {...}` ... `void MainWindow::BuildNetworkPage()` (private) and the member `urmsg::views::NetworkPageView network_{};`.
network.md:923:  void BuildNetworkPage();
network.md:929:  urmsg::views::NetworkPageView network_{};
network.md:940:void MainWindow::BuildNetworkPage() {
network.md:948:      call `BuildNetworkPage();` in the constructor immediately after `BuildConversationList();`
network.md:1352:- Consumes: ... Task N3: ... `MakeNetworkPage`, `MainWindow::BuildNetworkPage`, the `network_` member. ...
network.md:1425:  // Started from Loaded, never from the constructor. BuildNetworkPage runs from
network.md:1534:      immediately after `MainWindow::BuildNetworkPage`:
network.md:1615:      `BuildNetworkPage` runs from the constructor, before `Activate()`, on a Collapsed subtree.
network.md:1639:      crash — `BuildNetworkPage` and the Advanced subscriber both run on this path too, on a
advanced.md:349:- Produces: struct urm
```

**Verifier note.** The duplication is REAL and the ordering premise holds. The stated CONSEQUENCE is refuted.

WHAT SURVIVES.
Ordering is mandated, not incidental: the plan (2026-09-06-urmessage-demo-ui.md:~205) says "Build them in this order — the ordering is a dependency ordering, not a preference," and the table lists Network N1-N6 (:216), Settings/Advanced/Developer A2-A6 (:218), Click graph W5-W9 (:219). So N3, A4 and A5 all land before W6. W6 Step 1 (wiring.md:369-371) declares all three members unconditionally, and Step 3 (wiring.md:400-424) builds and mounts all three unconditionally. I searched wiring.md's preamble (:1-5, which only forwards to Global Constraints) and the whole of W6 through its Step 10 commit: there is no "add only what is absent" instruction anywhere in the file, unlike network.md, whose preamble states every MainWindow edit is "written twice: what to look for, and the exact block to add only when it is absent."

THE ONE-DIRECTIONAL GUARD (the thing I tried to refute with, and it does not save W6).
network.md:932 does carry a reciprocal guard: "If the wiring surface has already added a `network_` member and a builder, add neither — skip to step 9 and check its builder calls `urmsg::views::MakeNetworkPage(...)`." But it is inert under the mandated order — N3 runs first, finds nothing, and declares. W6 has no matching guard pointing the other way. A4 and A5 have no such guard in EITHER direction; grepping advanced.md for "already/skip this step/wiring surface" returns only unrelated hits.

WHY THE CONSEQUENCE IS WRONG (two independent reasons).
1. There cannot be two `network_` members. Both declarations go into the private section of the same class in the same header (N3 Step 7: "add the member beside `search_` (:60)" in MainWindow.xaml.h; W6 Step 1: "Add to `pr

**Consequence if not overridden.** Three more duplicate member declarations and three second builders for the same three hosts. Whichever runs last wins the mount, and W7's ApplyAdvanced then calls SetNetworkPageAdvanced on whichever NetworkPageView it happens to hold -- possibly the orphaned one -- so the Advanced toggle silently changes nothing on the Network page.

**Proposed dispatch override.** Delete Step 3 and the network_/settings_/developer_ declarations from Step 1. N3, A4 and A5 already declare those three members and already build and mount those three views in BuildNetworkPage/BuildSettings/BuildDeveloper. W6's only remaining job is whatever null-check auditing those three builders lack; verify with `git grep -n 'NetworkHost()\|SettingsHost()\|DeveloperHost()' -- app/src/App/MainWindow.xaml.cpp` returning exactly one Append per host before you write any new code.

### W7 - class 4 (gate that cannot fail) - **PARTIAL**

**Claim.** W7 Step 7's gate -- `(Select-String 'window: message inspect ->').Count` expected 1, justified as "a second message inspect -> would mean ApplyAdvanced re-showed the rail through SetInspectRailMessage" -- cannot detect that mutation: the string is logged only inside MainWindow::SelectMessage (wiring.md:177), and SetInspectRailMessage is a view function in InspectRailView.cpp that writes no log line at all.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'window: message inspect ->' docs/superpowers/plans/tasks/wiring.md && git grep -n 'LogInfo\|LogWarn' -- app/src/App/Views/InspectRailView.cpp | grep -i inspect`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'window: message inspect ->' docs/superpowers/plans/tasks/wiring.md && git grep -n 'LogInfo\|LogWarn' -- app/src/App/Views/InspectRailView.cpp | grep -i inspect

177:    urnw::LogInfo("window: message inspect -> {}", urnw::Narrow(selectedMessageId_));
288:(Select-String -Path $log -SimpleMatch 'window: message inspect ->' | Select-Object -Last 1)
293:`window: message inspect -> <row id>` line after it; and the `unavailable` count is **0**
651:(Select-String -Path $log -SimpleMatch 'window: message inspect ->').Count
app/src/App/Views/InspectRailView.cpp:642:  urnw::LogInfo("rail: conversation mode -> {} ({} members)", winrt::to_string(c.id),
app/src/App/Views/InspectRailView.cpp:654:  urnw::LogInfo("rail: message mode -> {} in {} (state {}, failure \"{}\")",

--- follow-ups ---

$ git grep -n 'window: message inspect ->' -- app/ docs/
docs/superpowers/plans/tasks/wiring.md:177   (the only emitter; zero hits under app/)
docs/.../wiring.md:288, 293, 651             (gates/prose only)

$ sed -n '646,658p' app/src/App/Views/InspectRailView.cpp
void SetInspectRailMessage(InspectRail
```

**Verifier note.** The defect is real but its stated reason is false, and the gate is not inert.

WHAT REPRODUCES. `window: message inspect ->` has exactly one emitter: wiring.md:177 inside `MainWindow::SelectMessage`. `git grep` over app/ and docs/ finds no other source (the other three hits are the gates at 288/651 and prose at 293). No gate anywhere counts the rail's own lines — `git grep "SimpleMatch 'rail:" -- docs/` returns nothing. W7 (wiring.md:524-680) contains no source-grep gate either; the ban exists only as prose (line 543) and a code comment the implementer copies (line 575, "NOT SetInspectRailMessage"). So inserting a direct `SetInspectRailMessage(rail_, ...)` into `ApplyAdvanced` leaves Step 7 reading `1` and `1`, green. Step 6's hash gate cannot backstop it: a crossfade is a transient animation and both frames are settled stills.

WHAT REFUTES THE JUSTIFICATION. "SetInspectRailMessage ... writes no log line at all" is false, and the auditor's own second command printed the line that refutes it — InspectRailView.cpp:654, `urnw::LogInfo("rail: message mode -> {} in {} (state {}, failure \"{}\")", ...)`, inside `SetInspectRailMessage` itself (function opens at :646). The `| grep -i inspect` filter passed :642 and :654 only because the path segment `InspectRailView.cpp` contains "Inspect"; the auditor appears to have read the filter as matching message text and concluded the two hits were noise. The real problem is not a missing log — it is that Step 7 counts the wrong string. The mutation is fully observable in the log the gate already reads.

HOW IT IS NARROWER THAN CLAIMED. "A fourteenth gate that cannot fail" overstates it. `SelectMessage` logs at :177 unconditionally, with no already-selected early-out — unlike `SelectConversation`, which has an explicit `if (conversatio

**Consequence if not overridden.** Adding the forbidden `SetInspectRailMessage(rail_, conv, row)` to ApplyAdvanced leaves the count at exactly 1 and the gate green, so the contract-v2 §4 crossfade ban ships unenforced -- a fourteenth gate that cannot fail, and the deliverable claims this exact line "proves the rail changed density without a mode crossfade".

**Proposed dispatch override.** Replace Step 7's second counted claim with one that a crossfade would actually move. Either (a) grep the source instead of the log: `git grep -c 'SetInspectRailMessage' -- app/src/App/MainWindow.xaml.cpp` must return 1, and that one call must sit inside MainWindow::SelectMessage -- ApplyAdvanced must contain zero SetInspectRailMessage and zero SetInspectRailConversation calls; or (b) have R2 log one line inside RunCrossfade and count that. Do not present the `message inspect ->` count as crossfade evidence: SetInspectRailMessage logs nothing, so that number is identical whether the bug is present or not.

### W9 - class 6 (names something that does not exist) - **CONFIRMED**

**Claim.** W9 does not own design §9.2's do-not-yank scroll rule: wiring.md contains zero occurrences of "ThreadView.cpp", "yank" and "ShouldPinToBottom", its Files list is MainWindow.xaml.h/.cpp only, and ThreadView.cpp:1307 states the rule "is NOT implemented, here or anywhere" while ThreadView.cpp:948 defers it to "the task that owns scroll behaviour" -- of which there is none left after W9.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -c 'ThreadView.cpp' docs/superpowers/plans/tasks/wiring.md; grep -c 'yank' docs/superpowers/plans/tasks/wiring.md; sed -n '930,951p;1300,1312p' app/src/App/Views/ThreadView.cpp`

**Actual output.**
```
$ grep -c 'ThreadView.cpp' docs/superpowers/plans/tasks/wiring.md
0
$ grep -c 'yank' docs/superpowers/plans/tasks/wiring.md
0
$ sed -n '930,951p;1300,1312p' app/src/App/Views/ThreadView.cpp
  // resize - this handler fires on width-only changes too and yanks them back.
  //
  // T6 GAVE IT A SECOND JOB, and it is the more important one now. Appending a
  // row adds a child to this stack, so this handler fires and re-pins - which
  // is what keeps an arriving message visible and is why AppendThreadRow writes
  // no scroll offset of its own (two writers of one property, and the later one
  // wins by accident of ordering). It also means design §9.2's "do not yank a
  // reader who has scrolled away" is unimplemented on the APPEND path too, not
  // only across a resize, and for the same reason: the guard belongs here.
  //
  // The obvious two-line guard ("return if we are far from the bottom") is
  // WRONG here and was deliberately not added: on the construction path the
  // first size change that carries a real extent has offset 0 and a large
  // scrollable height, which is indistinguishable from "the reader scrolled to
  // the top" - so the guard would skip the very first p
```

**Verifier note.** All three legs reproduce verbatim. wiring.md: 0 for "ThreadView.cpp", 0 for "yank" (and 0 case-insensitively, and 0 for ChangeView/ScrollableHeight/ShouldPinToBottom, so this is not a grep-case artifact). W9's Files list at wiring.md:1176 is exactly "Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp". All 12 of W9's steps are MainWindow edits plus screenshot/log verification; none reaches ThreadView.cpp. Line numbers check out: the unconditional pin is stack.SizeChanged at 949-951 as claimed.

I tried three ways to break it and each failed:

(1) SCOPE -- another task owns it. thread.md T6 Step 7 does contain the guard verbatim ("Follow the bottom only if the reader was already there ... which design §9.2 forbids", slack < 40.0). So the plan text did once assign it. But T6 is spent: progress.md records "Task T6: COMPLETE (commits 870e49d, a00125a, d254e82)" and "THREAD GROUP T1-T6 COMPLETE AND REVIEWED. 37 of 46 tasks." The implementer deliberately deviated -- shipped AppendThreadRow with no scroll write at all, because the T3 pin would override the conditional follow -- and that deviation is what ThreadView.cpp:1300-1312 documents. A completed task's unexecuted step is not a live owner.

(2) A GUARD ELSEWHERE IN THE BRIEF. None. The plan's task table ends with "Click graph and ambient activity | W5-W9", so W9 is the last task in the plan, and W9 Step 11 is the only step touching the append path -- it checks the ambient row survives a rebuild, not scroll position.

(3) A LATER STEP THAT CORRECTS IT (the network.md pattern). No. Steps 1-12 of W9 never widen the file scope.

The one amendment, and it makes the finding sharper rather than narrower: the claim's "of which there is none left after W9" is wrong about intent. progress.md:278 is an expl

**Consequence if not overridden.** W9 is the last task in the plan that touches the append path, so the rule ships unimplemented for a sixth time. The mechanical cause is exactly the Files list: the unconditional re-pin lives in MakeThread's stack.SizeChanged at ThreadView.cpp:949-951, and every candidate task -- including this one -- listed only MainWindow files, so no implementer was ever permitted to edit the file that holds the bug. With --demo-autoplay on, a viewer who scrolls up to read the backlog is snapped to the foot every ~40s.

**Proposed dispatch override.** WIDEN W9's Files list to `Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp; Modify app/src/App/Views/ThreadView.h; Modify app/src/App/Views/ThreadView.cpp; Modify app/src/App/Startup.cpp` and add a step that closes design §9.2's do-not-yank rule. Extract the DECISION as a pure function in ThreadView.h -- `bool ShouldPinToBottom(bool armed, double offset, double scrollableHeight);` returning true when !armed, and otherwise `scrollableHeight - offset <= 48.0` -- add a `bool pinArmed = false;` to ThreadParts, and replace the body of the stack.SizeChanged handler at ThreadView.cpp:949-951 with `if (!ShouldPinToBottom(parts->pinArmed, parts->scroller.VerticalOffset(), parts->scroller.ScrollableHeight())) return; parts->pinArmed = true; parts->scroller.ChangeView(...)`. You do NOT need to synthesise a scroll to prove this: gate ShouldPinToBottom directly in --diagnose over four cases -- (false, 0, 892.8) true (the first-pin case ThreadView.cpp:939-948 says a naive guard breaks), (true, 892.8, 892.8) true, (true, 860.0, 892.8) true, (true, 100.0, 892.8) false -- and delete the "NOT implemented, here or anywhere" paragraph at ThreadView.cpp:1305-1311 in the same commit. The decision can be pure even where the event cannot be synthesised.

### W9 - class 7 (missing baseline) - **CONFIRMED**

**Claim.** W9 Step 9 compares the `--demo=thread --demo-autoplay` capture against `.verify\baseline-autoplay-off.png` taken with `--demo=thread` and expects True, but DrainDeepLink's T6 ambient seed is gated on `options_.autoplay` (MainWindow.xaml.cpp:618) and appends TWO extra bubbles at launch when that switch is passed.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '612,662p' app/src/App/MainWindow.xaml.cpp | grep -n 'options_.autoplay\|AppendThreadRow\|c0-ambient'`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '612,662p' app/src/App/MainWindow.xaml.cpp | grep -n 'options_.autoplay\|AppendThreadRow\|c0-ambient'

7:  if (options_.autoplay && options_.screen == urmsg::demo::DemoScreen::Thread &&
11:    out.id = L"c0-ambient-1";
20:    urmsg::views::AppendThreadRow(thread_, out);
36:      in.id = L"c0-ambient-2";
46:    urmsg::views::AppendThreadRow(thread_, in);

(offsets 7/11/20/36/46 within the 612-662 window = absolute lines 618/622/631/647/657)
```

**Verifier note.** CONFIRMED, and the defect is WIDER than claimed, not narrower.

REPRODUCTION. MainWindow.xaml.cpp:618 gates on `options_.autoplay && options_.screen == DemoScreen::Thread && thread_.root` and appends `c0-ambient-1` (outgoing, 12:14) then `c0-ambient-2` (incoming, 12:16, sender lifted from c0's last incoming row) via the real `AppendThreadRow`. DemoSwitches.cpp:55 confirms `--demo-autoplay` sets `autoplay=true`, so `--demo=thread --demo-autoplay` satisfies the gate and `--demo=thread` alone does not. W9 brief Step 7 (line 161) copies the `--demo=thread` capture to `baseline-autoplay-off.png`; Step 9 (line 215) hashes the `--demo=thread --demo-autoplay` capture from Step 8 against it and expects `True`. verify-render.ps1 captures at `$SettleMs = 1200` ms, long after DrainDeepLink's first SizeChanged, so the seeded rows are in the frame. T6's own report states it: "The `--demo-autoplay` seed now appends **two** rows", and its screenshot table lists `t6-autoplay-window.png` = "`--demo=thread --demo-autoplay`: typing row + the appended row".

WHY IT IS WIDER. The auditor found one of TWO autoplay-gated launch seeds. Views/ThreadView.cpp:990, inside `MakeThread` (window-constructor path, not DrainDeepLink), reads `ParseDemoOptions()` directly:
    if (opts.autoplay && opts.screen == demo::DemoScreen::Thread) SetThreadTyping(v, true);
So under `--demo-autoplay` the typing row is Visible with its wave running from launch. Step 9's fallback branch demands (a) "no typing indicator anywhere in the thread" AND (b) equal bubble count. BOTH fallback checks fail, not just (b) — and (a) fails for a reason that is not in the file the auditor cited. Third difference: T6's report records that the appended 12:14 row TAKES the delivery cluster away from 12:11 ("12:11 `Sending` is gone"), so

**Consequence if not overridden.** Both branches of Step 9 fail by construction and neither failure is about W9: the hashes differ, and the fallback bubble count differs by exactly 2. The implementer is sent hunting a nonexistent regression in a loop that has not ticked yet, and the plausible "fix" is to delete T6's seed -- which is the only thing that makes AppendThreadRow reachable in a running window and is what every capture of that surface depends on.

**Proposed dispatch override.** Step 9's baseline is wrong: `--demo-autoplay` also triggers T6's two-row ambient SEED in DrainDeepLink (MainWindow.xaml.cpp:618-661), which appends c0-ambient-1 and c0-ambient-2 before any autoplay round. Do NOT compare against baseline-autoplay-off.png and do NOT delete the T6 seed. Instead assert what actually holds at ~1.2s: no typing indicator anywhere in the thread, `(Select-String -SimpleMatch 'demo: ambient message').Count` is 0, and `thread: ambient seed appended 2 rows` appears exactly once. Add the count of bubbles as baseline + 2, and state in the step that the +2 is T6's seed, not the loop.


---

## Minor

### A2 - class 6 (names something that does not exist) - **CONFIRMED**

**Claim.** A2's Interfaces block and step 7 anchor on "A1's assertion loop in urnw::CollectDiagnostics()" and "A1's `AdvancedModeDiagnostics()` loop"; no `AdvancedModeDiagnostics` symbol exists anywhere in app/src, and CollectDiagnostics' Advanced Mode content is a single non-asserting report line built inline at Startup.cpp:480-489.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && (grep -rn 'AdvancedModeDiagnostics' app/src/App || echo 'NOT PRESENT IN TREE') && grep -n 'advanced mode    :\|DemoWorldAssertions' app/src/App/Startup.cpp`

**Actual output.**
```
NOT PRESENT IN TREE
250:std::vector<std::wstring> DemoWorldAssertions() {
484:        L"  advanced mode    : {}  (pref advanced_mode: {}; session override: {})",
489:  for (auto& line : DemoWorldAssertions()) lines.push_back(std::move(line));
```

**Verifier note.** REPRODUCED. `AdvancedModeDiagnostics` has zero hits in app/src. The Advanced Mode content in CollectDiagnostics is one braced block, Startup.cpp:480-488, containing a single non-asserting `lines.push_back` of the "advanced mode :" report line (minor precision fix: the auditor's range 480-489 over-includes :489, which is a different loop).

THE COUNTER I TRIED, AND WHY IT ONLY NARROWS A2. Startup.cpp:489 IS a real assertion loop (`for (auto& line : DemoWorldAssertions())`) sitting directly under the advanced-mode block, so an implementer could recover a workable insertion point without the phantom name, and A2 step 7's expected output lists only the two `dev.*` lines with no ordering constraint — either side of :489 compiles and prints identically. That narrows A2's consequence to a choice between two adjacent, functionally equivalent positions. It does not refute the finding, for two reasons: (1) that loop is NOT A1's — `git log -S DemoWorldAssertions` gives e13cb51 "demo: assert the world's ten invariants in --diagnose", which predates the advanced-mode commit b2e82ae, so the Interfaces block's "Consumes: A1's assertion loop" misattributes another task's code; (2) the named symbol is absent everywhere.

SCOPE COUNTERS BOTH FAIL IN THE AUDITOR'S FAVOUR. The auditor's `grep -r app/src` is MORE inclusive than `git grep` — it sweeps `app/src/App/x64/Release/*.obj`, which I confirmed exist and do match for other symbols (`InitAdvancedMode` hits AdvancedMode.obj, main.obj, vc145.pdb) — and it still returned nothing. `git grep 'AdvancedModeDiagnostics'` over the whole repo returns exactly one hit: docs/superpowers/plans/tasks/advanced.md:255, the brief itself. No Step-1 discovery grep exists in A2 to self-correct (unlike network.md); A2 step 1 is "declare the motion override"

**Consequence if not overridden.** The implementer has no anchor for step 7's insertion and must guess where the new loop goes; the same phantom is what A4 step 6's positive control is built on, so one non-existent task's output is load-bearing in two briefs.

**Proposed dispatch override.** There is no `AdvancedModeDiagnostics()` in the tree — the Advanced Mode content in `CollectDiagnostics()` is an inline report line, not an assertion loop. Insert `for (auto& line : urmsg::demo::DeveloperSwitchDiagnostics()) lines.push_back(std::move(line));` immediately after the existing `for (auto& line : DemoWorldAssertions()) ...` loop in Startup.cpp, and A6's `WorldDumpDiagnostics()` loop immediately after that one. Strike every reference to 'A1' from A2's Interfaces block: Demo/AdvancedMode.h shipped from wiring.md's W2 and carries no diagnostics.

### A5 - class 4 (gate that cannot fail) - **PARTIAL**

**Claim.** A5 step 10 accepts `Message rows` only if it is "a number between 30 and 60"; the seeded world has 66 rows, so a correct implementation fails this check.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && ./app/build/x64/Release/URmessage.exe --diagnose 2>&1 | grep 'demo world'`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && ./app/build/x64/Release/URmessage.exe --diagnose 2>&1 | grep 'demo world'

  demo world       : 8 conversations, 66 rows, 22 members, 3 devices
  identicons       : 30 seeds from the demo world, 28 unique identities

(two lines match: the identicons line also contains the phrase "demo world")

Supporting lines from the same --diagnose run:
    I1  conversation count   PASS  8 total, 2 group, 6 direct
    I8  this computer        PASS  1 of 3 devices, index 0 is this computer
    I10 determinism          PASS  fingerprint 0x97B1C149D13010C3, expected 0x97B1C149D13010C3
  thread T1 header : PASS - 60 message rows, 20 draw a sender header, 0 disagree with DemoWorld::senderName
  T4 row shapes        : PASS — 0 wrong of 66 planned; in 36 out 24 day 3 sys 2 perm 1
```

**Verifier note.** THE BOUND IS REAL. The seeded world has 66 rows and A5 step 10 demands 30-60.

Where it lives: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui/.superpowers/sdd/2026-09-06-urmessage-demo-ui/task-A5-brief.md:504-505 (source: docs/superpowers/plans/tasks/advanced.md:1662). A literal grep for "between 30 and 60" returns nothing — the markdown bolds it: "between **30 and 60**".

Same quantity, no scope escape. A5's own code block (task-A5-brief.md:216-217) computes `messageRows += conversation.rows.size()` over `world.conversations`; --diagnose computes `rows += c.rows.size()` over the same vector at app/src/App/Startup.cpp:255-262. Identical expression, identical source, so a correct A5 renders exactly the 66 that --diagnose prints and fails the stated band.

The brief's own rationale is also wrong, which is where 30-60 came from. It says "(design §5 says ~40 messages plus day separators and system rows)". Actual composition, from this run: 60 message rows (thread T1 header line), broken down as in 36 / out 24, plus day 3 and sys 3 = 66. The "~40 messages" premise is off by 20; the band was derived from it, so it is not merely a tight ceiling.

NARROWER THAN CLAIMED — the escalation sentence does not cover this row. Step 10's closing sentence reads in full: "`Conversations` = 8 and `Linked devices` = 3 are design §5's stated contents and contract §1's asserted invariant 1. If either differs, that is a finding against the DemoWorld task, not something to relax here." "Either" binds to exactly those two figures, and `Message rows` is not one of them. Both named figures are correct in the seeded world (8 conversations, 3 devices; I1 PASS, I8 PASS), so that sentence never fires. The chain the auditor asserts — false DemoWorld defect -> 

**Consequence if not overridden.** The implementer sees 66, reads step 10's closing sentence ('If either differs, that is a finding against the DemoWorld task'), and either files a false defect against DemoWorld or edits the fixture — which breaks the I10 determinism fingerprint 0x97B1C149D13010C3 that ten invariants are pinned to.

**Proposed dispatch override.** In step 10, expect `Message rows` = **66**, not 'between 30 and 60'. `--diagnose` prints `demo world : 8 conversations, 66 rows, 22 members, 3 devices`; that is the sum of `Conversation::rows.size()` and it is the same figure A6 step 5's `dump.world` line must show on both sides of its second pair. Do not touch DemoWorld to make a number match.

### N1 - class 7 (missing baseline) - **PARTIAL**

**Claim.** Every commit step in this file names a tracked-file baseline that is wrong: N1 says 63 before and after, N2 says 63 -> 65, N3-N6 say 65 -> 65, but `git ls-files` currently returns 96.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && git ls-files | wc -l`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && git ls-files | wc -l
96

# and the form the plan's own steps use:
$ powershell -NoProfile -Command "(git ls-files | Measure-Object -Line).Lines; (git ls-tree -r HEAD --name-only | Measure-Object -Line).Lines"
96
96

# the six absolutes, verbatim from docs/superpowers/plans/tasks/network.md:
269:      count the tracked files first. This task adds no files, so the count must be **63 before and
270:      63 after**:
572:- [ ] **Step 9: commit.** Two new files, so the tracked count must go **63 -> 65**:
998:- [ ] **Step 13: commit.** No new files, so **65 -> 65**:
1118:- [ ] **Step 8: commit.** No new files, so **65 -> 65**:
1329:- [ ] **Step 12: commit.** No new files, so **65 -> 65**:
1642:- [ ] **Step 15: commit.** No new files, so **65 -> 65**:

# provenance: 63 was the count at the commit BEFORE the plan landed
$ git ls-tree -r fb363d4^ --name-only | wc -l   -> 63
$ git ls-tree -r fb363d4  --name-only | wc -l   -> 73   (the plan itself added 10 files)
```

**Verifier note.** The numbers are real and stale exactly as claimed — all six, unchanged since fb363d4, and off by 33. 63 was the count one commit BEFORE the plan itself was committed, so the baseline was already wrong (73) the instant network.md landed; F/W/L/T/R have since carried it to 96, and R4 is still open, so it will not be 96 when N1 actually runs either. network.md is also the ONLY one of the nine task files carrying absolutes: I regexed all nine at fb363d4 and at HEAD — network 6, every other file 0. rail.md:1405 and :1590 say only "The count must be unchanged"; strip.md/advanced.md/foundation.md print before/after with no target.

What makes the CONSEQUENCE narrower than claimed — the guard the auditor missed. The parent plan's Global Constraints §G2a (docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md:82-94), written in the SAME commit, pre-empts this by name and quotes network.md's own figures back: "What is brittle is a hardcoded absolute count ('63 before, 65 after'), because it drifts the moment any task adds a file... Do record the count before, record it after, and confirm the DELTA equals the number of files that task adds — zero for a modify-only task. Do NOT assert a specific absolute total. If a task body states one, treat it as advisory and check the delta instead. Also confirm git ls-files and git ls-tree -r HEAD --name-only agree after committing." network.md's own line 3 says "Read that file's Global Constraints first — they apply to every task here."

And the deltas network.md encodes are all CORRECT, so nothing actionable is lost by following G2a. I checked every N-task's Files: line — N1 Modify-only (+0), N2 Create NetworkPageView.h/.cpp (+2), N3/N4/N5/N6 Modify-only (+0). That matches 63/63, 63->65, 65->65 x4 exactly as deltas. So this is not "a compari

**Consequence if not overridden.** The Windows git-index truncation guard is the reason these counts exist. A baseline that is 33 files off is a comparison that cannot fail — the implementer sees a number that matches nothing and stops reading the check, which is when a silently truncated tree gets committed.

**Proposed dispatch override.** Ignore every literal file count in this file (63, 65). The tracked baseline is 96 as of this dispatch. The invariant to check is the DELTA, not the absolute: N1, N3, N4, N5 and N6 add no files, so the count before and after `git add -A` must be IDENTICAL; N2 adds Views/NetworkPageView.h and .cpp, so it must go up by exactly 2. Run `git ls-files | Measure-Object -Line` before and after and compare the two numbers to each other, never to a number in this document.

### N1 - class 6 (names something that does not exist) - **PARTIAL**

**Claim.** N1 Step 2 specifies glyph `E701` (NetworkTower) and Step 10 expectation 3 makes 'a broadcast tower' a blocking acceptance criterion, but the shell's shipped `NetworkNavItem` uses `&#xE839;` (Ethernet) at MainWindow.xaml:129.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '122,132p' app/src/App/MainWindow.xaml`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '122,132p' app/src/App/MainWindow.xaml

                </muxc:NavigationViewItem>
                <!-- Demo destinations, hidden on a normal launch. Segoe Fluent
                     Icons through UrIconFontFamily like every other glyph in
                     this file: E839 is Ethernet, E943 is Code. -->
                <muxc:NavigationViewItem x:Name="NetworkNavItem" Tag="network"
                                         Visibility="Collapsed">
                    <muxc:NavigationViewItem.Icon>
                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xE839;" />
                    </muxc:NavigationViewItem.Icon>
                </muxc:NavigationViewItem>
            </muxc:NavigationView.MenuItems>

$ grep -n "xE839" app/src/App/MainWindow.xaml
129:                        <FontIcon FontFamily="{StaticResource UrIconFontFamily}" FontSize="20" Glyph="&#xE839;" />

$ git grep -n "NetworkNavItem\|NetworkPage\|NetworkHost\|ApplyDemoScreen" -- app/src/App   # N1 Step 1, verbatim
app/src/App/MainWindow.xaml:126:                <muxc:Na
```

**Verifier note.** Every fact in the claim reproduces verbatim. Shipped: MainWindow.xaml:129 is Glyph="&#xE839;", with the shell's own comment at :123-125 pinning the choice ("E839 is Ethernet, E943 is Code"), authored by shell.md:450-454 / task-W3-brief.md:169-173. Brief: task-N1-brief.md:62,67 specifies E701 "NetworkTower"; :254-255 makes "the glyph is a broadcast tower" a Step 10 blocking criterion. network.md:53,58,245,246 is identical, so the brief dropped no guard.

I tried the guard counter-argument and it lands on ONE of the two halves, so PARTIAL, not CONFIRMED. Narrower in two specific ways:

(1) The Step 2 half is already dead. Step 2 is conditional: "If step 1 found no NetworkNavItem, add this". I ran Step 1's grep verbatim and it exits 0 with a hit at MainWindow.xaml:126, so Step 2's E701 block is never written and Step 1's "read them, add only what is missing" applies. E701 never reaches the file. This is exactly the network.md pattern flagged in my instructions. The defect is Step-10-only, not "Step 2 + Step 10".

(2) The claimed consequence over-reaches. Step 1's guard is worded as an add/skip rule for the write steps and does NOT reach Step 10, so expectation 3 does remain an unguarded blocking acceptance criterion that the shipped tree cannot satisfy — that part is real. But expectation 3's remedy clause is gated on a symptom that will not occur: "A hollow rectangle means the E701 codepoint missed and must be fixed." E839 is a valid Segoe Fluent Icons glyph, so the capture shows an Ethernet plug — not a hollow rectangle. The tripwire never fires, and the brief therefore never instructs "set the glyph to E701". What fails is only the bare first clause ("the glyph is a broadcast tower"). Editing the shell's E839 to E701 is one plausible resolution an implementer might reac

**Consequence if not overridden.** An implementer holding Step 10 as a gate edits the shell's nav item to E701 to make it pass, changing an icon the shell chose and documented ('E839 is Ethernet, E943 is Code') for a reason unrelated to this task.

**Proposed dispatch override.** Do NOT change the Network nav item's glyph. The shell ships `&#xE839;` (Ethernet) at MainWindow.xaml:129 with its own comment naming the choice. Rewrite Step 10 expectation 3 to: the Network item's glyph is an Ethernet port, not a hollow rectangle — a hollow rectangle means the E839 codepoint missed. Ignore every mention of E701 in this task.

### N3 - class 6 (names something that does not exist) - **REFUTED**

**Claim.** N3 Step 11 expectation 1 places the relay panel 'Under the `NETWORK` header strip' and N1 Step 10 expectations 5-6 describe a 40px #151515 NETWORK strip and a ScrollViewer, but `NetworkHost` is a self-closing empty Grid with no pane header and no ScrollViewer.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '262,271p' app/src/App/MainWindow.xaml`

**Actual output.**
```
<!-- The demo destinations. Each is one empty pane that its view
                     fills; MainWindow never draws into them itself. -->
                <Grid x:Name="NetworkHost" Visibility="Collapsed"
                      Style="{StaticResource UrPaneStyle}" />
                <Grid x:Name="SettingsHost" Visibility="Collapsed"
                      Style="{StaticResource UrPaneStyle}" />
                <Grid x:Name="DeveloperHost" Visibility="Collapsed"
                      Style="{StaticResource UrPaneStyle}" />
```

**Verifier note.** The evidence command reproduces literally -- NetworkHost IS a self-closing empty Grid today -- but the claimed consequence is refuted by N1 Step 3, in the same task file, which is the step that BUILDS the header strip and the ScrollViewer.

WHAT REFUTES IT. task-N1-brief.md:62-93 (network.md:62-93), Step 3, is a verbatim XAML block adding to MainWindow.xaml:
  <Border Grid.Row="0" Style="{StaticResource UrPaneHeaderStyle}">
      <TextBlock x:Name="NetworkPaneTitle" Style="{StaticResource UrPaneTitleStyle}" />
  </Border>
  <ScrollViewer Grid.Row="1" HorizontalScrollBarVisibility="Disabled"
                VerticalScrollBarVisibility="Auto">
      <Grid x:Name="NetworkBody" />
  </ScrollViewer>
UrPaneHeaderStyle (App.xaml:923) sets Height={UrPaneHeaderHeight}=40 (App.xaml:902), Background={UrSheetBrush}=#151515 (App.xaml:249), BorderThickness="0,0,0,1". That is expectation 5 exactly -- 40px, #151515, 1px bottom hairline. Step 4 sets NetworkPaneTitle().Text(L"NETWORK"). So N1 Step 10 expectations 5-6 describe what N1 Steps 3-4 built seven steps earlier, and N3 Step 11's "Under the NETWORK header strip" references a strip N1 produced. This is the same pattern the orchestrator flagged: network.md's earlier steps already handle what looks like a defect in a later step.

THE OWNERSHIP PREMISE IS ALSO WRONG. The auditor says "a pane header MainWindow does not own". network.md:16 is N1's Produces list: "XAML x:Names on MainWindow: NetworkNavItem, NetworkPage (Grid), NetworkPaneTitle (TextBlock), NetworkBody (Grid -- every later Network task appends into this)". MainWindow.xaml is in N1's "Files: Modify" list. MainWindow explicitly owns the pane header and the scroller.

shell.md:783 DOES NOT SAY WHAT THE AUDITOR SAYS. Exact line 783: "content area right of the nav pane is enti

**Consequence if not overridden.** Screenshot acceptance in N1, N3, N4 and N5 references chrome that will not be on screen, so an implementer either re-adds a pane header MainWindow does not own (shell.md:783 states NetworkHost is deliberately empty) or waves the acceptance through, and the page loses its vertical scroller as later tasks lengthen it.

**Proposed dispatch override.** NetworkHost (MainWindow.xaml:265) is a bare `<Grid>` with UrPaneStyle — no pane header strip and no ScrollViewer, and shell.md:783 states that is deliberate. Drop N1 Step 10 items 5 and 6 and N3 Step 11's phrase 'Under the NETWORK header strip'; judge from the top of the content area instead. Build the header and the scroller INSIDE `MakeNetworkPage`, not in MainWindow.xaml: make the returned `root` a two-row Grid whose row 0 is a `Border` styled `UrPaneHeaderStyle` containing a `UrPaneTitleStyle` TextBlock reading `NETWORK`, and whose row 1 is a `ScrollViewer` with `HorizontalScrollBarVisibility=Disabled`, `VerticalScrollBarVisibility=Auto` wrapping the `column` StackPanel.

### N6 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** N6 Step 12's expected log order puts `network: advanced mode -> true` before `network: relay pulse animate=...`, but the brief's own `SetNetworkPageAdvanced` body calls `SetRelayPathAnimated(parts, advanced)` (which emits the pulse line) BEFORE its own `LogInfo("network: advanced mode -> {}")`.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '1503,1508p;1605,1612p' docs/superpowers/plans/tasks/network.md`

**Actual output.**
```
$ cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '1503,1508p;1605,1612p' docs/superpowers/plans/tasks/network.md

  if (parts->roundTrip)
    parts->roundTrip.Visibility(advanced ? Visibility::Visible : Visibility::Collapsed);
  SetRelayPathAnimated(parts, advanced);
  urnw::LogInfo("network: advanced mode -> {}", advanced);
}
```

      Expected, in this order:

```
network: advanced mode -> true
network: relay pulse animate=true shouldAnimate=true loaded=false
network: relay pulse begun on Loaded
```
```

**Verifier note.** Reproduces exactly. Code at 1506 emits pulse-then-advanced (SetRelayPathAnimated logs at 1462); Step 12's expected block at 1609-1611 lists advanced-then-pulse. Swapped.

Four refutation attempts, all failed:
(1) The brief's own branch-resolving grep -- Step 6's `git grep -n "ApplyAdvancedMode\|MainWindow::SetAdvanced" -- app/src/App`, the same escape-hatch pattern that neutralizes other findings in this file -- I ran verbatim: exit 1, no output. So the "If it prints nothing" branch applies and Step 7's body + constructor registration land as written. No pre-existing ApplyAdvancedMode with earlier logging.
(2) No double invocation to reorder things: EnterDemoMode() is called at MainWindow.xaml.cpp:109 and calls InitAdvancedMode(options_.advanced || link.forceAdvanced) at :468 before any view is built, so Step 7's ApplyAdvancedMode(AdvancedModeEnabled()) (added after ApplyBreakpoint(), :187) fires exactly once with advanced=true.
(3) Exactly one emitter of each string in the brief (1506 pulse-consumer, 1462 pulse) and zero in the tree -- `git grep -n "advanced mode ->" -- app/src` and `grep -rn "advanced mode ->" app/src` both empty, so the tracked-vs-untracked count trap does not apply.
(4) Select-String -Path emits matches in file order; no shell fact reorders them.

Consequence holds as claimed and is not narrower. Step 13 also reads this log but names one line (animate=false shouldAnimate=false loaded=false) plus an absence (no "begun on Loaded"), with no ordering against "advanced mode" -- it survives the swap. Step 12 is the only gate whose pass condition depends on this order and the only one proving the pulse was armed with animate=true.

Additional defect found while verifying, which widens rather than narrows: app/src/Common/Log.cpp:57 opens the log with FILE_A

**Consequence if not overridden.** The only gate that proves the pulse was armed reports a mismatch on a correct implementation, so it gets waved through — and once it is waved through it stops catching the case it exists for (animate=false when it should be true).

**Proposed dispatch override.** Fix Step 12's expected output to the order the mandated code actually emits: `network: relay pulse animate=true shouldAnimate=true loaded=false`, then `network: advanced mode -> true`, then `network: relay pulse begun on Loaded`. Do not reorder the LogInfo calls in `SetNetworkPageAdvanced` to match the brief — the pulse line belongs inside `SetRelayPathAnimated` where it reads the values it prints.

### R4 - class 6 (names something that does not exist) - **PARTIAL**

**Claim.** Step 2 tells the implementer to append the two device lists "immediately after the `for (auto const& field : BuildMessageFields(...))` loop" in PopulateMessage, but no such loop exists: PopulateMessage ends with `AppendFieldRows(panel, BuildMessageFields(conv, row, advanced));` at InspectRailView.cpp:534, and the file's only `for (auto const& field` is at :482 inside the shared AppendFieldRows helper.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'for (auto const& field\|AppendFieldRows(panel, BuildMessageFields' app/src/App/Views/InspectRailView.cpp`

**Actual output.**
```
482:  for (auto const& field : fields)
534:  AppendFieldRows(panel, BuildMessageFields(conv, row, advanced));
```

**Verifier note.** The stale anchor is REAL and stronger than the auditor showed: `grep -rn 'for (auto const& field : BuildMessageFields' --include='*.cpp' .` returns NOTHING anywhere in the worktree, so the string is not merely relocated, it is gone. Provenance is clean: the loop is in the plan (docs/superpowers/plans/tasks/rail.md:1286) and in task-R3-brief.md:139-140 as the LAST statement of PopulateMessage; R3's implementation replaced it with AppendFieldRows and poisoned the macro, and R4's brief was never regenerated. No later R4 step corrects it -- `AppendFieldRows` appears zero times in task-R4-brief.md and zero times in rail.md.

It is PARTIAL because the defect is one stale appositive in a sentence carrying two other anchors that are both correct, and because BOTH claimed consequences are wrong.

Narrower on the anchor: Step 2 reads "**Step 2: Append the two lists in `PopulateMessage`.** Add these two calls at the very end of the function, immediately after the [loop]". The step title names the function; "at the very end of the function" is exact and still correct -- :534 is the last statement, `body` is in scope (:518) and `row` is a parameter, so the pasted code compiles verbatim at the surviving anchor. The two anchors described the same point until R3 changed the code; one went stale, the other did not.

Wrong on consequence (a): "would try to add DELIVERED TO / READ BY to conversation mode as well" cannot happen. AppendFieldRows is `void AppendFieldRows(StackPanel const&, std::vector<InspectField> const&)` -- no `row` -- so `row.inspect.deliveredTo` is a hard compile error. It never runs, so it never reaches conversation mode. The auditor's own next clause contradicts this one; only the compile error is possible, and it is caught at build, not at Step 7's screenshot read.



**Consequence if not overridden.** The only line matching the anchor is inside AppendFieldRows, which BOTH PopulateConversation and PopulateMessage call — appending there would try to add DELIVERED TO / READ BY to conversation mode as well, and would not compile because `row.inspect` is not in scope in that helper. At best it costs a round; at worst the implementer restores a hand-rolled field loop in PopulateMessage and re-opens the RailValueOr bypass R3 poisoned the macro to prevent.

**Proposed dispatch override.** In Step 2, the anchor is the line `AppendFieldRows(panel, BuildMessageFields(conv, row, advanced));` — the LAST statement of PopulateMessage at app/src/App/Views/InspectRailView.cpp:534. There is no `for (auto const& field : BuildMessageFields(...))` loop; do not go looking for one, and do NOT put your two AppendDeviceList calls in AppendFieldRows (:480-487), which conversation mode also calls. Append them after :534, using the `body` already bound at the top of PopulateMessage, and leave AppendFieldRows untouched.

### R4 - class 4 (gate that cannot fail) - **PARTIAL**

**Claim.** Step 9 clause 2 states the advanced `Group id` row "renders as eight hex characters, a horizontal ellipsis and four more — 13 characters", then adds "(If the id was already short enough it renders whole; either way...)" — but conversation 0 is a GROUP whose groupIdHex is exactly 16 characters and ShortHex returns any input of length <= keep+8 (=16) WHOLE, so the row always renders 16 characters and never the 13-character form the clause leads with.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && sed -n '136,139p' app/src/App/Views/InspectRailFields.cpp && grep -n 'conv.groupIdHex.size() == 16\|ShortHex(conv.groupIdHex) == conv.groupIdHex' app/src/App/Views/InspectRailFields.cpp && sed -n '244,246p' app/src/App/Demo/DemoWorld.cpp`

**Actual output.**
```
$ sed -n '136,139p' app/src/App/Views/InspectRailFields.cpp
std::wstring ShortHex(std::wstring const& hex, size_t keep) {
  if (hex.size() <= keep + 8) return hex;
  return hex.substr(0, keep) + kEllipsis + hex.substr(hex.size() - 4);
}

$ grep -n 'conv.groupIdHex.size() == 16\|ShortHex(conv.groupIdHex) == conv.groupIdHex' app/src/App/Views/InspectRailFields.cpp
420:                         conv.groupIdHex.size() == 16,
421:                         ShortHex(conv.groupIdHex) == conv.groupIdHex};

$ sed -n '244,246p' app/src/App/Demo/DemoWorld.cpp
  // c0 - Design team, 5 members, 24 rows.
  Conversation c0 = MakeConversation(
      L"conv-design-team", ConversationKind::Group, L"Design team",

--- corroborating runs ---

$ grep -n 'groupIdHex' app/src/App/Demo/DemoWorld.cpp
173:    n.groupIdHex = c.groupIdHex;
207:  c.groupIdHex = (kind == ConversationKind::Group) ? HexOf(c.identityKey, 8) : L"";
(HexOf emits 2 hex chars per byte, so HexOf(key, 8) = 16 chars)

$ grep -n 'ShortHex' app/src/App/Views/InspectRailFields.h
110:std::wstring ShortHex(std::wstring const& hex, size_t keep = 8);   // keep+8 = 16

$ grep -c 'static_assert' app/src/App/Views/InspectRailFields.cpp
0

$ sed -n '4
```

**Verifier note.** The FACTUAL core reproduces exactly; the CONSEQUENCE is wrong on both of its mechanisms, which makes the defect real but much narrower.

WHAT HOLDS. The row can never render 13 characters. DemoWorld.cpp:207 sets `c.groupIdHex = HexOf(c.identityKey, 8)` for groups; HexOf pushes 2 hex digits per byte, so that is exactly 16 chars. ShortHex's default keep is 8 (InspectRailFields.h:110), and its guard is `if (hex.size() <= keep + 8) return hex;` — 16 <= 16, returns whole. Non-groups get L"" (shorter still). So no conversation in the world can produce the 13-char form; Step 9 clause 2 leads with a rendering that does not exist. The R1 author knew this and wrote it down at InspectRailFields.cpp:390-394: "groupIdHex DemoWorld builds is HexOf(seed, 8) = 16 characters, which is `keep + 8` exactly and comes back whole." The brief contradicts a comment already in the tree.

NARROWER 1 — the build-break does not exist. `grep -c static_assert app/src/App/Views/InspectRailFields.cpp` returns 0. Lines 415-421 are a runtime `const bool got[8]` array of named conjuncts feeding a --diagnose string, not static_asserts. Editing ShortHex to force truncation compiles fine. The auditor's "costing a round" cost is fabricated.

NARROWER 2 — the mutation is NOT uncaught, and an earlier step in the SAME task file catches it (the network.md pattern the instructions flagged). R4 Step 6 (rail.md:1521-1524) runs `--diagnose` and pipes it through `Select-String 'inspect '`. `shortHexOk` is a conjunct of the top-level `ok` at line 525, and `ok` prints as the PASS/FAIL token on the line `"  inspect rail     : {} - ..."` — which contains "inspect " and therefore matches that filter (verified: grep -c returned 1). A forced-truncation edit fails conjuncts 4 and 8 and surfaces BY NAME as `ok no (16 (keep+8) 

**Consequence if not overridden.** An implementer reading a 16-character id where the brief leads with 13 either edits ShortHex to force truncation (which the static_asserts at InspectRailFields.cpp:415-421 would break the build over, costing a round) or accepts the 'either way' escape clause — a screenshot check that accepts both outcomes and therefore catches no mutation at all.

**Proposed dispatch override.** In Step 9 clause 2, the expected reading is SIXTEEN hex characters rendered whole with no ellipsis: DemoWorld gives conversation 0 (`conv-design-team`, ConversationKind::Group, DemoWorld.cpp:245-246) a groupIdHex of exactly 16 characters, and ShortHex returns any input of size <= keep+8 unchanged (InspectRailFields.cpp:136-139), a property already asserted by `conv.groupIdHex.size() == 16` and `ShortHex(conv.groupIdHex) == conv.groupIdHex` in the probe. Delete the '13 characters' expectation and the 'either way' clause; check instead that the 16 characters fit on one right-aligned line with nothing wrapped or clipped. Do NOT change ShortHex to make 13 appear.

### R4 - class 4 (gate that cannot fail) - **PARTIAL**

**Claim.** Step 7 clause 5's empty-list assertions ("If R is 0 ... If D is 0 ...") can never run: --diagnose reports the picked message c0-r12 with delivered-by 7 and read-by 7, and DemoWorld.h:135 documents message mode as opening with both lists populated, so the `MakePaneEmptyLine` branch of Step 1's AppendDeviceList ships with zero coverage from any capture R4 takes.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && ./app/build/x64/Release/URmessage.exe --diagnose | grep -i 'inspect devices'`

**Actual output.**
```
$ cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && ./app/build/x64/Release/URmessage.exe --diagnose | grep -i 'inspect devices'

  inspect devices  : PASS - pick "c0-r12" state Read delivered-by 7 read-by 7; read-by is a subset of delivered-by in 47/47 rows that have readers, of 60 message rows; the check accepts the pick (yes) and rejects 2/2 local counter-examples (an unknown device id, and a second device of an existing name); pick == kInspectTargetRowId "c0-r12", falls back to "c0-r23" with no read row and to null with none, ok yes; lookups resolve 8/8 conversations and 24/24 rows of conversation 0 by id, misses null yes; devices 22 online of 26 across 22 members (4 admin), 4 short of full, 0 wrong
```

**Verifier note.** THE CORE CLAIM REPRODUCES EXACTLY. D=7 and R=7 on the picked message, so both "If R is 0" and "If D is 0" in Step 7 clause 5 are vacuous. I closed the escape hatches:

- No switch can retarget the rail. app/src/App/Demo/DemoSwitches.h:16-21 gives DemoOptions exactly five fields (enabled, screen, autoplay, advanced, watermark) - there is no --demo-message=<id>. The rail's message-mode subject is fixed to PickInspectMessage(conversations[0]) for every run.
- All four R4 captures miss it. Steps 5 and 8 are both --demo=inspect (same c0-r12, same 7/7); Steps 11 and R2's rail-chats.png are conversation mode, which has no DELIVERED TO / READ BY lists at all. Step 10 is a regex gate on CrossfadePageSwap/PresentMode counts and reads nothing about strings.
- No later step or sibling task covers it. wiring.md:936-939 builds ambient rows with deliveredTo = world.myDevices and readBy = the online subset (populated), and wiring.md:281 checks the lists "with device names". advanced.md:1917-1918 prints delivered=<n> read=<n> in the Developer page's text dump - counts, not the rail's rendering.
- The world does contain rows that would fire it, and R1's picker is built to avoid them: the same line says read-by is a subset "in 47/47 rows that have readers, of 60 message rows" (so 13 rows have R=0), the inspect rail line reports "4 'Received' on an outgoing row not yet delivered", and the fallback ladder is "falls back to 'c0-r23' with no read row and to null with none" - it structurally prefers a row WITH readers.

WHY PARTIAL - the consequence is narrower than claimed. The auditor names three failure modes that would "pass every gate": wrong string, wrong style, or an empty StackPanel. Two of the three are not R4's to introduce, because R4 does not author the empty line:

- MakePaneEmpty

**Consequence if not overridden.** The 'nothing here vs. this failed to load' distinction the Step 1 comment justifies the branch with is asserted by nobody; a broken empty branch (wrong string, wrong style, or an empty StackPanel) would pass every gate in this task and only surface once the thread's onSelectMessage seam lets a click reach a message with no readers.

**Proposed dispatch override.** Step 7 clause 5 is unreachable as written — the pick is c0-r12 with delivered-by 7 and read-by 7, so neither D nor R is ever 0 in any capture this task takes. Keep the clause but state in your report that it did not fire, and cover the branch where the world cannot reach it: add two static_asserts or a --diagnose clause built from a LOCALLY constructed MessageInspect with an empty deliveredTo and an empty readBy, asserting AppendDeviceList's empty path produces exactly one MakePaneEmptyLine child carrying the expected string. Do NOT edit Demo/DemoWorld to create an empty-list row — the world is byte-fingerprinted as I10 = 0x97B1C149D13010C3.

### R4 - class 8 (cross-task conflict) - **PARTIAL**

**Claim.** R4's Step 4 comment and its closing paragraph both say the Advanced Mode task will add `SetInspectRailAdvanced(rail_, on);` to the single OnAdvancedModeChanged subscriber, but advanced.md contains zero occurrences of SetInspectRailAdvanced — wiring.md:578 is the file that carries that line, and the tree's own comment at MainWindow.xaml.cpp:267-268 says the wiring surface owns OnAdvancedModeChanged.

**Evidence command.** `cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -c 'SetInspectRailAdvanced' docs/superpowers/plans/tasks/advanced.md docs/superpowers/plans/tasks/wiring.md && sed -n '265,268p' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
$ cd /c/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -c 'SetInspectRailAdvanced' docs/superpowers/plans/tasks/advanced.md docs/superpowers/plans/tasks/wiring.md && sed -n '265,268p' app/src/App/MainWindow.xaml.cpp

docs/superpowers/plans/tasks/advanced.md:0
docs/superpowers/plans/tasks/wiring.md:3
    // This is the initial application; the live subscription that re-calls this
    // on every toggle is registered by the wiring surface, which owns
    // OnAdvancedModeChanged.
    urmsg::views::SetConversationListAdvanced(list_, urmsg::AdvancedModeEnabled());
```

**Verifier note.** The command's output reproduces byte-for-byte. The inference on top of it does not, and the defect is real but a different shape.

WHAT HOLDS. R4 makes the claim in three places, not two (the auditor missed one): rail.md:1420 (Produces: "what the Advanced Mode task's single OnAdvancedModeChanged subscriber in MainWindow calls"), rail.md:1502-1504 (the Step 4 code comment, which is baked into MainWindow.xaml.cpp as a source comment), and rail.md:1594 (closing "What is NOT delivered here"). advanced.md really does contain zero `SetInspectRailAdvanced`.

WHY THE CLAIM AS STATED FAILS. R4 never names advanced.md, "A4", or the Settings/Advanced/Developer group — I grepped rail.md for `W7|A4|wiring|advanced.md` and got zero hits. Its only referent is "the Advanced Mode task (Demo/AdvancedMode.h, fixed contract §5)", and per the plan's own assembly note (2026-09-06-urmessage-demo-ui.md:225-231) Demo/AdvancedMode.h is built by F6 while "W7 and A4 consume it" — the phrase does not uniquely name advanced.md. And a live task in the plan contains R4's promised line verbatim: W7 (wiring.md:578) puts `urmsg::views::SetInspectRailAdvanced(rail_, on);` inside `MainWindow::ApplyAdvanced`, registered as the OnAdvancedModeChanged subscriber at wiring.md:601 — matching R4's text down to the parameter spelling `on` (A4 and network.md name that lambda parameter `advanced`). So "hands off to a task that does not contain it" is false of the plan as a whole. The auditor's own second exhibit cuts against the predicted consequence too: MainWindow.xaml.cpp:265-268 is the one place a reader actually resolved this handoff, and they resolved it to "the wiring surface" — correctly, to where the line is — not to advanced.md and a second subscriber.

WHAT IS ACTUALLY WRONG (narrower, and sharper). Insid

**Consequence if not overridden.** R4 hands off its live-toggle follow-up to a task that does not contain it. Whoever later checks 'is the rail's Advanced toggle wired?' against advanced.md finds nothing and either adds a second subscriber or concludes the line was dropped — and four task files (advanced.md:1027, network.md:1548, strip.md:1614, wiring.md:601) each already claim to register the one OnAdvancedModeChanged subscriber R4's text calls singular.

**Proposed dispatch override.** In Step 4's comment and in the closing 'What is NOT delivered here' paragraph, name the WIRING task, not the Advanced Mode task: wiring.md:578 is the file that adds `urmsg::views::SetInspectRailAdvanced(rail_, on);` to MainWindow::ApplyAdvancedMode, and MainWindow.xaml.cpp:267-268 already records that the wiring surface owns OnAdvancedModeChanged. Do not register an OnAdvancedModeChanged subscriber in this task, and do not assert in your report that the window registers exactly one — four task files currently each claim to create it, and that conflict is the wiring task's to settle.

### S3 - class 4 (gate that cannot fail) - **PARTIAL**

**Claim.** Step 12's gate reads `Select-String 'status drawer ->' | Select-Object -Last 3` and expects 'exactly one line for this run', but Log.cpp opens the log with OPEN_ALWAYS and appends across launches — the checked-in .localstate-verify log already spans 57 launches.

**Evidence command.** `grep -c "wWinMain" .localstate-verify/logs/urmessage-app.log`

**Actual output.**
```
57
```

**Verifier note.** MECHANISM: fully reproduced. `grep -c "wWinMain" .localstate-verify/logs/urmessage-app.log` = 57 in one 619,610-byte file, spanning two days (45 launches on 2026-09-07, 12 on 2026-09-08). app/src/Common/Log.cpp:57-59 is `CreateFileW(logFile, FILE_APPEND_DATA, ..., OPEN_ALWAYS, ...)`; the only size check (:67-68) writes a BOM when size==0 — no rotation, no truncation. Nothing clears it: app/tools/verify-render.ps1:158 sets URMESSAGE_APP_ROOT to .localstate-verify and only Stop-ByPath-kills the prior process; grep for Clear-Content/Remove-Item/truncate across app/tools/, the S3 brief and strip.md returns no log reset. I ran the gate's exact shape against an analogous once-per-launch pattern and it returned three lines from three different pids 13.5 hours apart (pid 6540 @01:59, pid 35004 @02:23, pid 48032 @15:32), drawn from 57 total matches. So `-Last 3` provably reads across launches. `status drawer ->` count is currently 0 (S3 not yet implemented), so the first attempt would read green and every later attempt would not.

THE PLAN ALREADY CONDEMNS THIS GATE — VERBATIM. docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md:67-70, section "G2 — The verification standard", under "Forbidden, because they were measured not to work": "Grepping the app log for expected lines. The log is append-mode, so a 'last N lines' check reads a previous run's output and passes on stale evidence. If a task needs to prove a code path ran, assert it in --diagnose instead." Step 12 is that exact banned shape.

I checked the one thing that could refute this — the "guard elsewhere" pattern. G2:74 does carry a guard: "Where a task body still contains one of these, replace it with the nearest permitted gate above and note the substitution in the commit message." But that guard never reaches the 

**Consequence if not overridden.** A leftover `-> open` line from an earlier attempt makes the gate read green even when step 11's UIA Invoke did nothing, which is the one failure this gate exists to catch.

**Proposed dispatch override.** Step 12: add `Remove-Item .localstate-verify\logs\urmessage-app.log -ErrorAction SilentlyContinue` as the first command of Step 10, before the verify-render launch. Log.cpp opens the file OPEN_ALWAYS and appends across launches (the current file holds 57), so without the delete 'exactly one line for this run' can read green off a line an earlier attempt wrote.

### S3 - class 6 (names something that does not exist) - **PARTIAL**

**Claim.** MainWindow.xaml:275-277 states the strip's 'preview drawer opens ABOVE it inside this same host' (StatusStripHost); S3 Step 2 creates a separate `StatusDrawerHost` in Grid.Row 1, which leaves the shipped comment naming the wrong host.

**Evidence command.** `grep -n -B3 'x:Name="StatusStripHost"' app/src/App/MainWindow.xaml`

**Actual output.**
```
$ grep -n -B3 'x:Name="StatusStripHost"' app/src/App/MainWindow.xaml
275-        <!-- The persistent connect indicator (design doc 6.5, D4). Window level,
276-             not per-destination: it is one strip along the bottom of the app,
277-             and its preview drawer opens ABOVE it inside this same host. -->
278:        <Grid x:Name="StatusStripHost" Grid.Row="2" Visibility="Collapsed" />
```

**Verifier note.** The stale comment is real and it does ship. The claimed consequence is factually wrong, and the finding is mis-attributed to S3.

WHAT HOLDS. The comment is verbatim at 275-277 (output above). S3 Step 2 (task-S3-brief.md:52-63, = strip.md:1079) puts the drawer in a NEW host: `<Grid x:Name="StatusDrawerHost" Grid.Row="1" VerticalAlignment="Bottom" HorizontalAlignment="Left" />`. I looked for a later step that repairs the sentence and there is none: `grep -rn 'ABOVE it' --include='*.md' --include='*.xaml' --include='*.cpp' --include='*.h' .` returns only task-W3-brief.md:157, docs/superpowers/plans/tasks/shell.md:438, app/src/App/MainWindow.xaml:277, and a stale build copy at app/src/App/x64/Release/MainWindow.xaml:277. `grep -rn 'same host' --include='*.md'` returns the same two briefs. So after S3 the tree carries a comment that is flatly false.

WHY IT IS NARROWER — 1, the mechanics. RevealRoot's rows are Auto/*/Auto (MainWindow.xaml:38-42) and StatusStripHost has NO RowDefinitions, so it is one cell whose children overlap in z-order. S2 Step 6 gives the strip root `root.Height(kStatusStripHeightDip)` = 26 and `root.VerticalAlignment(VerticalAlignment::Bottom)`; S3 Step 3 gives the drawer `drawer.VerticalAlignment(VerticalAlignment::Bottom)` at 137. Mounting the drawer there makes row 2 max(26,137) = 137 — it grows by 111, not by 137 — and the strip, being Height 26 + Bottom, stays glued to the window's bottom edge. It is not pushed off. What actually breaks is the opposite pair: the 137 drawer, also Bottom, lands on top of the 26 strip and covers it, and the `*` row loses 111 DIP so the destination is DISPLACED — the exact failure S3 Step 2's own comment says Grid.Row="1" exists to avoid.

2, adjacency. S3 Step 2 inserts its block "immediately after `</muxc:Navigation

**Consequence if not overridden.** A later reader following the comment mounts the drawer inside StatusStripHost, whose Grid.Row 2 is Auto — the row grows by the drawer's 137 DIP and pushes the strip off the window's bottom edge instead of drawing over the destination.

**Proposed dispatch override.** Step 2: in the same edit, replace MainWindow.xaml line 277's 'and its preview drawer opens ABOVE it inside this same host' with 'its preview drawer is hosted by StatusDrawerHost in Grid.Row 1, so raising it cannot grow this Auto row'. `git grep StatusDrawerHost` confirms the host itself is genuinely new, but the existing comment must not be left pointing at the wrong one.

### S3 - class 6 (names something that does not exist) - **PARTIAL**

**Claim.** Step 11's acceptance criterion 'its left edge 16 DIP from the window's left edge — flush with the connect dot below it' contradicts S2's own geometry, which centres an 8 DIP dot inside a 20 DIP host that starts at the strip's 16 DIP inset.

**Evidence command.** `grep -n "flush with the connect dot\|leaves exactly 6 DIP" docs/superpowers/plans/tasks/strip.md`

**Actual output.**
```
$ grep -n "flush with the connect dot\|leaves exactly 6 DIP" docs/superpowers/plans/tasks/strip.md

583:// it, which leaves exactly 6 DIP to its right, and 6 is the dot-to-value gap
1426:- A **320 DIP wide** panel standing directly **on top of the status strip**, its left edge **16 DIP** from the window's left edge — flush with the connect dot below it, and therefore over the NavigationView pane rather than aligned with the conversation-list column.

Follow-up grep (all window-left-edge measurements in the task tree):
$ grep -rn "16 DIP from the window\|from the window's left edge" docs/superpowers/plans/tasks/

strip.md:951:- Inside it, left to right, the first mark **16 DIP** from the window's left edge: an **8 DIP green dot** (`#87FB67`); **6 DIP** later the word **`Connected`**; ...
strip.md:1426:- A **320 DIP wide** panel standing directly **on top of the status strip**, its left edge **16 DIP** from the window's left edge — flush with the connect dot below it, ...
```

**Verifier note.** The arithmetic reproduces exactly. S2 Step 6 (strip.md:707) sets root Border Padding 0 and the Button Padding(16,0,16,0); the fields StackPanel's first child is the 20x20 dotHost (strip.md:590-615), and the 8 DIP dot is HorizontalAlignment::Center inside it. So the host slot spans x=16..36 and the dot's PAINTED circle spans x=22..30. S3 Step 3 (strip.md:1128) sets drawer.Margin(16,0,0,0) inside StatusDrawerHost, which is Grid.Row="1" HorizontalAlignment="Left" — and MainWindow.xaml:38-41,90 confirms row 1 is the full-width NavigationView row with no offset, so the drawer's left edge is genuinely window x=16. 16 vs 22: the 6 DIP gap is real.

But the claim is misattributed and BOTH stated consequences are blocked.

MISATTRIBUTED. S3 Step 11 does not contradict S2 — it inherits S2's own wording verbatim. S2's acceptance at strip.md:951 already says "the first mark **16 DIP** from the window's left edge: an **8 DIP green dot**". Same convention, same number, applied to the same dot, and S2 passes on it. The slot-vs-ink imprecision originates in S2's Step 13 acceptance, not in S3's Step 11. (Line 951 is in fact the one place that mixes conventions in a single sentence: it measures the dot's left from the SLOT — 16 — and the dot-to-word gap from the INK — "6 DIP later the word Connected", which is ink-accurate since the ink ends at 30 and the host at 36. S4's Step 7 at strip.md:1655 repeats the same "dot is still 8 DIP at 16 from the left edge" phrasing.) A reviewer who accepted the S2 screenshot has already settled that "16 from the left edge" means the field slot; applying the identical sentence to the drawer cannot then reject a correct drawer.

CONSEQUENCE 1 (implementer moves the drawer to 22) is unreachable. S3 Step 3 is not a description, it is a literal copy-in code 

**Consequence if not overridden.** The dot's visual left edge is at 22 DIP, so a drawer at margin 16 sits 6 DIP to its left. Either the reviewer rejects a correct drawer for failing a criterion that cannot hold, or the implementer moves the drawer to 22 and breaks the 'drawer edge = strip content inset' rule the same step states.

**Proposed dispatch override.** Step 11: strike 'flush with the connect dot below it'. S2's MakeConnectDot centres an 8 DIP dot in a 20 DIP host at inset 16, so the dot's left edge is at 22 and the 16 DIP drawer margin puts the drawer 6 DIP to its LEFT. Expect instead: 'left edge 16 DIP from the window edge, aligned with the strip's content inset, i.e. with the left edge of the dot's 20 DIP host'.

### W7 - class 6 (names something that does not exist) - **CONFIRMED**

**Claim.** W7 Step 3 instructs "In MainWindow::EnterDemoMode(), DELETE this line, which W3 wrote: DeveloperNavItem().Visibility(advanced_ ? ...)" -- that line is not in EnterDemoMode; it is at MainWindow.xaml.cpp:566 inside DrainDeepLink, immediately above the NavigationView PaneDisplayMode workaround cycle, and EnterDemoMode only calls DeveloperNavItem().Content().

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && awk '/^void MainWindow::/{f=$0} /DeveloperNavItem\(\)/{print NR": "f"  ->  "$0}' app/src/App/MainWindow.xaml.cpp`

**Actual output.**
```
487: void MainWindow::EnterDemoMode() {  ->    DeveloperNavItem().Content(box_value(hstring{kDemoNavDeveloper}));
566: void MainWindow::DrainDeepLink() {  ->    DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
```

**Verifier note.** Reproduced exactly, and the awk's function attribution is not an artifact. Real boundaries from `grep -n "^[A-Za-z_].*MainWindow::"`: EnterDemoMode 462, SelectNavTag 502, DrainDeepLink 520, ApplyBreakpoint 669. So 487 (Content) is genuinely inside EnterDemoMode and 566 (Visibility) is genuinely inside DrainDeepLink, 18 lines past the next function boundary. EnterDemoMode contains no Visibility call on DeveloperNavItem at all.

BRIEF TEXT VERBATIM (.superpowers/sdd/2026-09-06-urmessage-demo-ui/task-W7-brief.md, Step 3; identical at docs/superpowers/plans/tasks/wiring.md:~586): "In `MainWindow::EnterDemoMode()`, DELETE this line, which W3 wrote:" followed by the two-line Visibility statement.

PROVENANCE -- "which W3 wrote" was true of the PLAN, not the CODE. task-W3-brief.md:353 does place that line inside EnterDemoMode (step 9, function opens at :336). W3's implementer then moved it: task-W3-report.md:207 "Moved that same call from the constructor (`EnterDemoMode`) into `DrainDeepLink`", :215 "The working fix: keep the `Visibility` flip in `DrainDeepLink` ... and immediately follow it with HomeNav().PaneDisplayMode(LeftCompact) then back to ::Auto". The landed source says the same at :483-486 and :530-566. W7 was written against W3's brief and never re-anchored to what W3 landed.

FOUR REFUTATION ATTEMPTS, ALL FAILED:
1. Earlier task restores it? No. W5 and W6 are the only tasks between W3 and W7 that touch this file; neither writes DeveloperNavItem().Visibility (grep over task-W5/W6-brief.md returns no hit).
2. W7's other anchor is stale too, so maybe the whole step is misread? No -- the second anchor is fine and this is a clean split. W5-brief.md:218-228 inserts `BuildDemoViews();` into EnterDemoMode anchored on `pendingLink_ = link; pendingLinkArmed_ = true;`, which 

**Consequence if not overridden.** The implementer searches EnterDemoMode, finds nothing to delete, and either skips the step (leaving two writers of the Developer item's visibility) or deletes the DrainDeepLink line -- which moves the Collapsed->Visible transition into the constructor, ahead of the PaneDisplayMode cycle that DrainDeepLink's comment documents as the only known recovery from the Windows App SDK 2.2.0 defect that renders the whole nav pane icon-only. Step 6's own expectation, "the nav pane's footer lists Developer above Settings", is what breaks.

**Proposed dispatch override.** The line to delete is at MainWindow.xaml.cpp:566, inside DrainDeepLink, not EnterDemoMode. Delete it there and leave the NetworkNavItem().Visibility line and the LeftCompact/Auto PaneDisplayMode cycle below it untouched -- that cycle is the documented workaround for the WinAppSDK 2.2.0 nav corruption and it must still run after the last Visibility flip. After the edit, run `--demo=developer` and confirm the nav pane still shows TEXT LABELS beside Chats/Contacts/Network/Developer/Settings at 1560x900; if it renders icon-only, move ApplyAdvanced's DeveloperNavItem().Visibility write back behind the cycle instead of reverting the step.

### W8 - class 4 (gate that cannot fail) - **CONFIRMED**

**Claim.** W8's IncomingForRound sets `row.timeLabel = ClockLabel()` (the machine's real wall clock, wiring.md:862/901) and `inspect.sentAtLabel = row.timeLabel` (wiring.md:932), while every seeded row carries a fixed time and a sentAtLabel of the form `day + L" " + time` (DemoWorld.cpp:175) -- so the ambient row is neither deterministic nor in the same format as the world it is appended to.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'ClockLabel\|inspect.sentAtLabel' docs/superpowers/plans/tasks/wiring.md && grep -n 'n.sentAtLabel = day' app/src/App/Demo/DemoWorld.cpp`

**Actual output.**
```
862:std::wstring ClockLabel() {
901:  row.timeLabel = ClockLabel();
932:  inspect.sentAtLabel = row.timeLabel;
175:    n.sentAtLabel = day + L" " + std::wstring(s.time);
```

**Verifier note.** Every limb reproduces, and the surrounding code makes it worse than claimed.

MECHANISM. wiring.md:862-865 `std::wstring ClockLabel() { SYSTEMTIME now{}; ::GetLocalTime(&now); return std::format(L"{:02}:{:02}", now.wHour, now.wMinute); }` -- the machine's real local clock. :901 puts it on the bubble, :932 and :933 copy it into BOTH `inspect.sentAtLabel` and `inspect.receivedAtLabel`. Seeded rows use `n.sentAtLabel = day + L" " + s.time` (DemoWorld.cpp:175) where `day` starts at L"Today" (:117) and is reset by each DaySeparator (:134). c0's last seeded row is `{kMsg, L"", L"Retrying the attachment.", L"12:11", true, kPend...}` (DemoWorld.cpp:279) under a `{kSep, L"", L"Today"}` separator (:265) -- so the auditor's "12:11" and "Today 12:11" are literal, not illustrative.

NOT VIEW-LOCAL. wiring.md:1005 `conversation.rows.push_back(row)` writes the ambient row into `MutableWorld()`, with the brief's own comment "Into the WORLD first." The rail renders the field raw -- InspectRailFields.cpp:152-153 `out.push_back({L"Sent", row.inspect.sentAtLabel});` -- so there is no normalising layer between the bare "23:47" and the screen.

FOUR ATTEMPTS TO MAKE THE CLAIM WRONG, ALL FAILED:
(a) A later corrective step, the network.md pattern. `git grep -n ClockLabel` over the whole tracked tree returns exactly two hits, both in wiring.md (:862 def, :901 use). `grep -n 'timeLabel|sentAtLabel|receivedAtLabel|GetLocalTime|GetSystemTime' docs/superpowers/plans/tasks/wiring.md` returns exactly four lines: 864, 901, 932, 933. Nothing in W8 Steps 3-8 or in W9 touches them.
(b) The tracked-vs-untracked count trap. Re-ran with `git grep` (tracked only) instead of `grep` -- same two hits, so no app/build/ inflation and no deflation either.
(c) W8's own --diagnose gate (Step 5), which is the strong

**Consequence if not overridden.** The ambient bubble lands under a thread whose last row reads 12:11 carrying whatever the presenter's clock says -- 23:47 during an evening demo -- and the rail's Sent field shows a bare "23:47" where every other message shows "Today 12:11". It also contradicts W8's own stated rule that "the demo world is seeded and two runs of the same demo must behave the same way": two runs produce different bubbles.

**Proposed dispatch override.** Do not call ::GetLocalTime. Delete ClockLabel and derive the ambient row's time from the conversation it is appended to: take the last RowKind::Message's timeLabel, parse its HH:MM, add (round + 1) * 3 minutes, and format that; then set `inspect.sentAtLabel` and `inspect.receivedAtLabel` with the same day prefix DemoWorld uses at DemoWorld.cpp:175 (`L"Today " + row.timeLabel`), not the bare timeLabel. Add the determinism to the Step 5 gate: `IncomingForRound(conv, 0).timeLabel == IncomingForRound(conv, 0).timeLabel` is a tautology, so assert instead that round 0's label is strictly later than the conversation's last seeded message time and strictly earlier than round 1's.

### W9 - class 8 (cross-task conflict) - **UNVERIFIED**

**Claim.** W9's RefreshOpenThread calls SetThreadConversation, which rebuilds the bubbles from World::conversations, while T6's ambient seed rows exist only in the ThreadView -- its own comment says "The demo WORLD is not mutated" -- so the first autoplay round's onDeliveryAdvanced silently deletes the two seeded bubbles.

**Evidence command.** `cd C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui && grep -n 'The demo WORLD is not mutated' app/src/App/MainWindow.xaml.cpp && grep -n 'SetThreadConversation(thread_, conversation)' docs/superpowers/plans/tasks/wiring.md`

**Consequence if not overridden.** About 40 seconds into a `--demo=thread --demo-autoplay` session -- mid-presentation -- two messages the viewer has already read disappear from the thread with no cause, which is the one thing design §9.2 says ambient activity must never do. W9 Step 11 asserts the opposite property ("any later SetThreadConversation redraws it rather than dropping it") and is true only of W8's world-backed row, so nothing in the plan catches this.

**Proposed dispatch override.** Add to Step 4: when StartAmbientActivity is reached and `options_.autoplay` is set, the T6 seed rows (c0-ambient-1, c0-ambient-2) exist in the ThreadView only, so the loop's first RefreshOpenThread erases them. Fix it at the seed, not in W9's callbacks: change DrainDeepLink's T6 block to push `out` and `in` into `urmsg::demo::MutableWorld().conversations.front().rows` before calling AppendThreadRow, and update the comment at MainWindow.xaml.cpp:614-615 that says the world is not mutated. Then Step 11's claim -- that the ambient rows survive a rebuild because they live in the world -- becomes true of every ambient row rather than only W8's.


---

## Verifier results with no matching auditor finding

- **N6 — defect class 8 (Advanced Mode single-subscriber fan-out; network.md Step 6 guard)** [PARTIAL] N6 Step 6's guard greps for `ApplyAdvancedMode|MainWindow::SetAdvanced`, which does not match wiring.md W7's `void MainWindow::ApplyAdvanced(bool on)` (wiring.md:563) — the same "ONE subscriber of urmsg::OnAdvancedModeChanged" declared by a different name and registered in `EnterDemoMode()` rather t
- **W5 (defect class 8) — third writer of the --demo=inspect message-selection state** [CONFIRMED] W5 gives --demo=inspect two more writers: BuildInspectRail's `if (options_.screen == DemoScreen::Inspect)` branch (MainWindow.xaml.cpp:413-419), whose own source comment says in capitals the wiring task must delete it, and DrainDeepLink's existing `if (pendingLink_.selectMessage && thread_.root) Set
- **W9 (defect class 8) — demo-UI plan pre-flight verification, worktree C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui** [CONFIRMED] W9's RefreshOpenThread calls SetThreadConversation, which rebuilds the bubbles from World::conversations, while T6's ambient seed rows exist only in the ThreadView ("The demo WORLD is not mutated"), so the first autoplay round's onDeliveryAdvanced silently deletes the two seeded bubbles ~40s into a 
