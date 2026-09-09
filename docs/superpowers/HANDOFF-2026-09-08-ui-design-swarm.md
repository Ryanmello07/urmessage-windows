# URmessage demo UI — handoff to the design swarm

Written 2026-09-08 by the agent that ran the first 25 of 46 tasks. You are inheriting a working,
reviewed, honest demo that the owner describes as *"okay but still basic"*. Your brief is to make
it look like proper work: depth, hierarchy, motion, and the URnetwork identity — while keeping
every constraint below intact. You have freedom on the visuals. You do not have freedom on the
honesty rules, the brand reservations, the gates, or the fixture.

Every number and path in this document was checked against the tree on the day it was written.
Where a fact could drift, the command that re-derives it is given instead of the answer.

---

## 1. Where everything lives

| What | Where |
|---|---|
| Worktree (all work happens here) | `C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/.claude/worktrees/demo-ui` |
| Main checkout (do NOT edit) | `C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows` — a different branch |
| Branch | `demo-ui`, 57 commits, all pushed |
| `origin` | `https://github.com/Ryanmello07/urmessage-windows.git` — the owner's fork, **push here** |
| `upstream` | `urnetwork/message-windows` — push is **disabled** in the remote config, PR only |
| Design doc (governs this demo) | `docs/superpowers/specs/2026-09-06-urmessage-demo-ui-design.md` |
| Spec C (the shipping-client spec it defers to) | `msgrepo/docs/specs/2026-08-12-spec-c-windows-client-ui.md` |
| Plan + per-surface task files | `docs/superpowers/plans/2026-09-06-urmessage-demo-ui.md`, `docs/superpowers/plans/tasks/*.md` |
| **Audit of the 21 remaining tasks** | `docs/superpowers/plans/2026-09-08-remaining-task-audit.md` — read this before touching any of them |
| Ledger (gitignored, append-only) | `.superpowers/sdd/2026-09-06-urmessage-demo-ui/progress.md` |
| Pre-flight tools | `.superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py`, `brief.py` |
| Preserved captures | `.verify-*/` directories (gitignored); `.verify/` is single-slot and gets overwritten |

Commit attribution: the owner asked that commits be attributed to their account. Commits so far use
`-c user.name="Ryanmello07"`. Keep doing that. Never push to `main`/`master`, never force-push.

## 2. Build, run, verify

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1     # 3–60 s
app\build\x64\Release\URmessage.exe --demo=inspect                      # the three-pane hero view
app\build\x64\Release\URmessage.exe --diagnose                          # the only test harness
```

Switches the binary actually parses (verified from the `static_assert`s in `Demo/DemoSwitches.cpp`):

| Switch | Effect |
|---|---|
| `--demo` | demo world, no forced screen |
| `--demo=chats` / `thread` / `inspect` | the three **built** surfaces |
| `--demo=network` / `settings` / `developer` | parse, but route to **empty hosts** — blank pane today |
| `--demo-advanced` | Advanced Mode on at launch |
| `--demo-autoplay` | seeds one ambient message into the thread (only under `--demo=thread`) |
| `--demo-watermark=off` | removes the DEMO chip — **this is why no honesty may depend on the chip** |

Also accepts `-demo`, `/demo`, bare `demo`.

**Counting `--diagnose` results.** Use a case-sensitive, word-bounded match. `PASS` and `FAIL` appear
as standalone tokens; the word "failed" appears inside *passing* lines, so a case-insensitive `FAIL`
grep reports false failures. In PowerShell: `$_ -cmatch '\bFAIL\b'`. Today: **42 PASS, 0 FAIL**.

**Screenshots.** `app/tools/verify-render.ps1 -AppArgs "--demo=inspect"` — the argument is a
single **string**, not an array. It usually reports `foreground: NO` (Windows refuses
`SetForegroundWindow`), so the *screen* capture contains other windows. **Read
`.verify/urmessage-window.png`, the PrintWindow capture.** Its usual disqualifier — it can't
composite a system backdrop — doesn't apply because this app deliberately has none.

**Index check before every commit.** `git ls-files | wc -l` must equal
`git ls-tree -r HEAD --name-only | wc -l` (both **96** today). Report the delta, not the total.
The index has silently truncated commits on this machine before.

## 3. What exists today, honestly

Twenty-five of 46 tasks are complete and reviewed: foundation (F1–F7), demo shell (W1/W3/W4),
conversation list (L1–L6), the whole thread (T1–T6), and the rail's field model, column and
message mode (R1–R3).

**Built and on screen at `--demo=inspect`, 1560×900:**

- **Conversation list** — 8 conversations, deterministic pixel identicons, unread pills, a muted
  row, a disappearing-timer row that shows the glyph *instead of* preview text, three-channel
  selection, staggered entrance, working search that respects the privacy property (a string in a
  hidden preview is not findable).
- **Thread** — bubbles with direction fills (incoming `card` left, outgoing `cardHover` + 1px border
  right), width capped at 68 % of content / 640 dip, sender header + identicon on run-starts only,
  day separators, centred system rows, a **permanent key-change record** (red rule, real key glyph,
  no dismiss control), delivery clusters carrying state on **three independent channels** — glyph
  count (1 vs 2), glyph shape (ring vs disc), and the word — never colour alone; a Failed row shows
  a red glyph, "Not sent", a reason line and a "Try again" affordance; selection is a 2 px accent
  **outline** with the fill unchanged; composer with a `24h` timer chip and a disabled faint send
  pill; typing indicator; bubble entrance motion; ambient append that correctly removes the
  previous run-end's cluster.
- **Inspector rail** (≥1500 dip only) — conversation mode: `Details`, subject row with identicon,
  `MEMBERS` with presence dots and device counts (`1/2 online`), `RETENTION`. Message mode: a lock
  header and eight key/value rows.

**Not built — and this is a live demo hazard:** clicking **Network**, **Settings** or **Developer**
in the nav shows a **completely blank pane**. `ShowDestination` routes them to `NetworkHost`,
`SettingsHost`, `DeveloperHost`, which are empty Grids until the N and A groups land. A `StubPage`
with a "not built" line already exists; routing the three unbuilt tags to it is a few lines and is
the cheapest improvement you can make. Do it first.

**The status strip** (S1–S4, the "connect indicator" the owner asked to be non-intrusive) and the
**Network page** (N1–N6, the relay-path / connected-nodes view "like the URnetwork VPN shows") do
not exist yet. These are the two surfaces where URnetwork's identity should be most visible, and
they are entirely yours to design.

## 4. Hard constraints — these are not yours to relax

**G4 — honesty. This is the most important rule in the project.** There is **no crypto and no
protocol** in this binary. Every value comes from the fabricated `Demo/DemoWorld`. **No user-visible
copy may state or imply that a message WAS encrypted, verified, or attested.** Describe the *field*,
never the *claim*. The established pattern is prefix-first framing, and three strings ship with it:

| Where | String |
|---|---|
| Attestation row | `Demo model: verified` / `Demo model: not verified` |
| Lock header title | `Demo model: end-to-end encrypted` |
| Lock header note | `Demo model: fabricated data, no crypto in this build` |

Prefix-first is deliberate: `"Verified (demo)"` leads with the claim and loses its framing to exactly
the crop a screenshot performs. The framing is **asserted** in `--diagnose` — a regression to the
bare word fails the build gate. The plan's own text for the rail header said `"End-to-end
encrypted"` plus the cipher name, defended with "the DEMO chip is the mitigation" — that defence is
wrong because `--demo-watermark=off` removes the chip and the owner patches the watermark in
themselves when presenting. **`MessageInspect::cipher` (`"XChaCha20-Poly1305"`) is rendered nowhere
and must stay that way** — a specific algorithm name is a claim about what encrypted the message,
whatever label sits above it. The audit found the plan's A6 task about to dump it to a
Copy-to-clipboard button, and N2/N4/A3 about to render `"Verified"` in green for the server key.
Those overrides are in the audit doc.

A reviewer's note the owner should hear: the **green padlock is now the only unframed positive claim
on that row**, and it is the half a cropped screenshot keeps. Not changed — it is the visual the
owner asked for — but if you redesign the lock header, keep that in mind.

**G3 — brand.** From `app/src/App/UrColors.h`:

| Token | Value | Rule |
|---|---|---|
| page | `#101010` | window ground |
| sheet | `#151515` | one step above the page — pane headers, strips |
| card | `#1C1C1C` | incoming bubbles, cards |
| cardHover | `#242424` | outgoing bubbles, hover |
| cardPressed | `#2A2A2A` | pressed |
| border | `#1FFFFFFF` | hairlines |
| textMuted / textFaint | `#989898` / `#5A5A5A` | secondary / tertiary |
| danger | `#F8523B` | Failed, key-change record |
| urGreen | `#87FB67` | presence, the padlock |
| **accent** | **`#EFF7BB`** | **the send button and the selection outline. NEVER a bubble fill, never a large area.** |
| **kProGold** | **`#FFC400`** | **reserved for Pro entitlement. Must appear NOWHERE in this demo.** |

Body text is **PP Neue Montreal**. **ABC Gravity Extended** and **Extra Condensed** are display
faces — titles and the wordmark only, never body copy (unreadable at body size and length). 87
`Ur*` resource keys exist in `App.xaml`; the whole demo so far added exactly one. Prefer extending
what's there over adding keys, and never add a key silently — say why an existing one couldn't
serve.

**Delivery state is carried on three channels** — glyph count, glyph shape, word — and never on
colour alone. `E930` (ring) and `EC61` (filled disc) are the font's only true outline/filled check
pair; the bare checks are indistinguishable at 13 px. If you restyle the cluster, preserve all three
channels.

**Motion** lives in `app/src/App/UrMotion.h`: `kMicroMs 90`, `kFastMs 150`, `kBaseMs 250`,
`kSlowMs 400`, `kStaggerMs 40`, `kMaxStaggerSteps 6`, standard bezier `(0.10, 0.90) → (0.20, 1.00)`.
Exits run one step faster than entrances. Use the tokens; don't invent durations. Respect the
`ShouldAnimate()` gate — the reduce-motion path has **never executed on this machine**
(`SPI_GETCLIENTAREAANIMATION = 1`), so if you touch motion, that branch is unverified.

**The fixture is immutable.** `Demo/DemoWorld.cpp` is byte-fingerprinted; `--diagnose` line `I10`
asserts `0x97B1C149D13010C3`. Do not edit it. If an assertion needs an adversarial case the world
doesn't contain, **build it locally inside the assertion**. The world has exactly one Failed row,
no Expired row, and every `groupIdHex` is exactly 16 chars — rare states appear once or never, so
gates over the fixture alone silently lose the ability to distinguish the rule from the row.

**`--diagnose` is pure C++.** `CollectDiagnostics()` runs in `wWinMain` **before**
`winrt::init_apartment()`. Anything it reaches must have no winrt, no XAML. Pure modules carry
`PrecompiledHeader=NotUsing`. This is why the rail's *mode* has no probe — the decision is an inline
`if` in a winrt TU — and why a pure `InitialRailMode(screen, conv)` extraction is assigned to R4.

**Do not add `<ObjectFileName>` to `App.vcxproj`.** The plan says to; R1 measured it failing with
two `MSB3191 "the given path's format is not supported"`. The measurement is recorded in the
vcxproj comment.

**Security constraints inherited from the owner, verbatim:** never run agents elevated; never
synthesize mouse or keyboard input (property writes and deep links are legitimate; `--demo=inspect`
selects `c0-r12` and is how selection is verified); kill processes only by exact executable path
from this worktree's build dir; never commit or log the beta-test seedphrase or JWTs.

## 5. Where the design headroom is — the actual creative brief

The owner's target: *"Slightly better than Signal, not as insane as SimpleX. Kinda like Matrix but
better."* And: *"beautiful, URnetwork-themed, simple yet advanced and robust, animations and smooth
modern look and feel."* The current build is correct and honest; it is not yet beautiful. Looking at
the captures, these are the gaps I'd point a designer at, roughly in order of payoff:

1. **Depth.** Everything is flat. List, thread and rail are separated by 1 px hairlines and nothing
   else. The composer sits on the page with a top hairline. There's no sense of the thread being a
   surface and the composer resting on it. `sheet`/`card`/`cardHover`/`cardPressed` give you four
   tonal steps and they're barely used for layering.
2. **Hover, pressed, focus.** `kCardHover` and `kCardPressed` exist and the stills read as static.
   Row hover, bubble hover, nav-item hover, button pressed states — the `UrBubbleButtonStyle` was
   added *precisely* so bubbles don't repaint on hover and erase the direction fill, so hover has
   to be something other than a fill change. That's a design problem worth solving well.
3. **Typographic hierarchy.** It's size-only, one face, one weight most of the time. The display
   face appears in the wordmark and nowhere else. Section strips (`CONVERSATIONS`, `MEMBERS`) use a
   letterspaced chrome voice — that voice could carry more of the structure. There is a lot of room
   between "one body face at three sizes" and the display face.
4. **The identicons are the most distinctive thing on screen** — pixel-art, saturated, proven ≥30°
   of hue from every reserved state colour. Nothing else on the surface matches their energy. The
   design language could take a cue from them rather than leaving them as the one vivid element.
5. **Bubbles** are uniform-radius rounded rectangles with a time stamp inside. No tail, no
   asymmetry, no treatment distinguishing the first and last of a run beyond the header rule.
6. **Delivery clusters are small** (13 px glyphs + 11 px word). They carry the most information
   per pixel on the screen and are the easiest thing to miss.
7. **The rail is a key/value table.** Correct, and dull. The member rows with presence dots and
   device counts are the "Session-style connected clients" the owner asked for; they could look
   like it.
8. **Nothing says URnetwork yet** beyond the wordmark and the colour tokens. The relay path (three
   nodes: this device → URnetwork → message server, with hop timings under Advanced Mode) is N3's
   job and doesn't exist. The status strip (S1–S4) is the connect indicator. Those two surfaces
   are where the mesh identity lives, and both are unbuilt — design them, don't just build them.
9. **Below 1500 dip the rail vanishes** and the thread takes the full width. The 1200×800 captures
   look emptier than the 1560×900 ones. Worth a look.
10. **Empty and transitional states.** The stub page has one honest line. Mode crossfades exist
    (`CrossfadePageSwap`, `kBaseMs`). Loading, empty search, empty conversation — all thin.

The plan is normative on **behaviour** and **honesty**. It is not normative on being basic. Where a
task file specifies a plain key/value row or a 34 dip line, and you can do something better that
keeps every rule in §4, do the better thing and say in the ledger what you changed and why.

## 6. The 21 remaining tasks

R4, N1–N6, S1–S4, A2–A6, W5–W9. **All 21 have verified defects in their briefs** — 62 findings, 30
confirmed, 27 partial, 2 refuted, none clean. They are in
`docs/superpowers/plans/2026-09-08-remaining-task-audit.md` with, for each, the evidence command,
what it actually printed, and a dispatch override ready to paste. The four worst:

- **A6** emits `cipher="XChaCha20-Poly1305"` plus unframed `key=verified` into a developer dump with
  a Copy-to-clipboard button. Every G4 decision on the rail undone by a text dump.
- **N2 / N4 / A3** render `"Verified"` / `"Not verified"` in green for the server key, gate-asserted.
- **W5** mounts the conversation list into `ListHost`, which is `Visibility="Collapsed"` and never
  made visible by any line of code. Zero pixels.
- **An ownership cluster** between the wiring group and the surface groups: S2 and W6 both mount the
  status strip; S3 and W6 both declare `ToggleStatusDrawer`; A4 produces `ApplyAdvancedMode(bool)`
  while W7 produces `ApplyAdvanced(bool)` and both call theirs "THE one subscription". These will
  collide in whichever order they land. Decide ownership before dispatching either side.

Recurring root cause: the per-surface task files were drafted **in parallel, before the shell
existed**, and each invented its own host names, constants and breakpoint functions. The shell
(W3) then created the canonical ones. Before dispatching any task, run
`python .superpowers/sdd/2026-09-06-urmessage-demo-ui/mount_check.py <brief>` — it reads the
demo-collapsed set from `MainWindow.xaml.cpp` itself and reports whether each `X().Children()`
target is live, and whether a created container collides with an existing one at the same
`Grid.Column`. It has caught this class four times. Its exit code is meaningful on a task file; on
`MainWindow.xaml.cpp` itself it is permanently 1 from two pre-existing `ThreadBody` writes, so read
the per-container lines there.

Two rulings already settled, so nobody re-litigates them:
- **`wiring.md` changes, not R3.** Its deep link picks the last `RowKind::Message` (`c0-r23`,
  Pending); R3 uses `PickInspectMessage` (`c0-r12`, Read). `c0-r12` has three consumers plus a
  probe asserting they agree; `c0-r23` would open the showcase on a blank `Received` and an empty
  device list. `wiring.md:236-241` should call `PickInspectMessage` / `kInspectTargetRowId`, and
  W5 must **delete** R3's `BuildInspectRail()` deep-link branch rather than keep both writers.
- **Design §9.2's do-not-yank scroll rule** goes to **W9 with its Files list widened** to include
  `Views/ThreadView.cpp`. It was deferred five times because every candidate task listed only
  MainWindow files. The unlock: extract `ShouldPinToBottom(armed, offset, scrollableHeight)` as a
  pure function — *the decision can be pure even when the event cannot be synthesized.*

## 7. Process lessons — what worked, what cost money

**The gate that passes but cannot fail is this project's signature defect.** Thirteen shipped.
Six lessons, each paid for once:
1. *A census is not a comparison.* Permuting two categories leaves every total identical; only a
   per-row check catches it.
2. *Sweep the siblings.* Fixing one instance of a pattern and leaving its neighbours happened four
   times.
3. *A gate can be dead rather than blind* — one certified a table nothing rendered from, and
   asserted a property the real render deliberately violates. Ask "does anything real depend on
   what this asserts?"
4. *A tautology looks like coverage.* `EntranceTimelineCount(true) == 4` compared a literal to a
   literal in the same one-liner.
5. *State the limit, accurately.* Where a gate genuinely can't reach something, say so in its own
   comment. A comment claiming a fix removed "the last deletable branch" was false and got quoted
   onward.
6. *Mutate the clause you're least sure of, not the one you're proudest of.* Every time an
   implementer ran three mutations against strong gates, the review found the defects in the
   clauses it hadn't tried.

**Demonstrate every new gate failing** against a deliberate break, then revert, and paste both
outputs. A gate that has only ever been seen passing is a hypothesis.

**Screenshot everything, and look.** Four features here compiled, launched, logged success and
passed every assertion while painting zero pixels, because `--demo` swaps whole containers and a
write into the collapsed twin is silent. The owner's standing instruction is to launch it and look
after every change; CI-green is not UI verification. Compare captures by **pixel difference**
(`ImageChops.difference(...).getbbox()`), never by byte length or hash — two runs of one binary
gave identical byte length, different SHA-256, and a 5-pixel drift.

**Comments carry the rule and the reason.** Not a count, not a census, not the current output of a
command, not same-file line geometry. One file took three fix rounds because each round's
corrected comment introduced a new false fact; the published grep even matched its own publication,
so documenting it changed its own answer. Cross-file `file.cpp:NN` citations are fine — house style,
one click to check, they rot visibly. What to fix in-round: a comment naming a **symbol, probe,
caller or surface that does not exist** — one `git grep` settles each. Defer counts, prose weight
and geometry to the branch review.

**A ledger assignment has no path to an implementer.** `brief.py` reads only the plan's task files;
it never opens `progress.md`. "Assigned to R4" in the ledger reaches R4 only if you paste it into
R4's dispatch by hand. This bit twice.

**A reviewer's claim is not evidence either.** One attribution error travelled reviewer → controller
→ implementer with none of the three running the grep. Nine relayed figures were wrong this session,
and the worst was the running task count — never re-derived, only incremented, wrong for the whole
session underneath every error that was caught. If a grep or a subtraction would settle it, run it
before you write it.

**Fix rounds introduce the defect they fix.** Scope every re-review to the fix diff, and tell the
re-reviewer to audit the *new* claims, not just the fixed ones.

**Non-ASCII.** Glyph literals in code are `L"\uXXXX"` escapes with the icon name in a trailing
comment, never the pasted character — PUA characters render blank in terminals, so a dropped one is
invisible. **Seven agents were bitten, in both directions:** escapes silently decoded into raw
characters, and raw characters emitted where the source had empty literals. Build escapes from
`chr(92)` + hex; verify at the byte level, never by reading. Comments, by contrast, use `§` and `—`
freely (33 of 54 files do); an agent that wrote `S9.1` instead of `§9.1` broke `grep '§9.2'` and
had to sweep it back.

## 8. Coordinating a swarm on one worktree

Parallel implementers on one working copy will collide — every task touches `MainWindow.xaml.cpp`
or `Startup.cpp`. Two workable shapes:

- **One worktree per agent**, branched from `demo-ui` (`git worktree add`), one surface each, merged
  by a single coordinator who runs the build, `--diagnose`, and a screenshot on the merge result
  before pushing. This is what the ownership cluster in §6 needs anyway.
- **Serialize implementation, parallelize review and audit.** That is what this session did; it is
  slower but every conflict surfaces at review.

Whichever you choose: the ledger is append-only and its first line names the plan file; never
rewrite it. Push to `origin demo-ui` after each reviewed task. Never open the upstream PR until the
branch is complete — the owner authorized the PR flow, but a half-built demo in front of the org
repo is the wrong moment for it.

## 9. Things I'd do first, in order

1. Route `network` / `settings` / `developer` to `StubPage` until their groups land. Removes the
   black-screen hazard from the owner's next demo.
2. Decide the S/W and A/W ownership cluster before dispatching either side.
3. Read the audit doc's Critical section end to end; every override is pre-written.
4. Then design. The status strip and the Network page are blank canvases with an honesty rule and a
   colour palette. That's where "proper work" shows.
