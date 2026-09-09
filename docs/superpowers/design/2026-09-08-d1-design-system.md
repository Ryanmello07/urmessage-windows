# D1 — Design system foundation: depth, interaction, type, accent, motion

**Date:** 2026-09-08 · **Status:** proposal for coordinator review
**Scope:** whole-app foundation language for the demo UI swarm. No behaviour changes, no new
user-visible copy, no new colour tokens, no new duration tokens.
**Binds to:** G3 brand (`app/src/App/UrColors.h`), G4 honesty, the UrMotion token set
(`app/src/App/UrMotion.h`), the pane-shell vocabulary (the block at the foot of
`app/src/App/App.xaml`, lines 872–899).

**Grounding.** Everything below was read in code or seen in the baseline captures
(`.verify-baseline/{inspect,chats,thread,network-stub}.png`): `UrColors.h`, `UrMotion.h/.cpp`,
`App.xaml` (all 87 `Ur*` keys), `UrComponents.h/.cpp` (the kit builders), `MainWindow.xaml(.cpp)`,
`Views/ThreadView.cpp`, `Views/ConversationListView.cpp`, `Views/InspectRailView.cpp`,
`Views/ThreadLayout.h`, `Demo/DemoWorld.h`, `Demo/DemoShellState.h`, `Identicon.h/.cpp`, and the
governing design doc `docs/superpowers/specs/2026-09-06-urmessage-demo-ui-design.md`.

---

## 1. (a) Tonal layering model — "the stage and its chrome"

### The problem, measured in the captures

In `inspect.png` the list, thread and rail are three expanses of `#101010` separated by 1px
hairlines and nothing else. The four tonal steps the palette provides (`UrColors.h:16-28`) are
spent on *objects* (bubbles, the composer bar) but never on *regions*. Depth in this app must come
from tone and hairline — the pane vocabulary already forbids the alternatives ("Nothing has a
radius, a margin or a shadow", `App.xaml:895`-area comment).

### The model: four layers, each with a job

| Layer | Token | Value | Role | Who holds it today |
|---|---|---|---|---|
| **L0 ground** | `kBackground` / `UrBackgroundBrush` | `#101010` | **Content.** The surface things rest on. | list body, thread backlog, rail body, stub bodies (`UrPaneStyle`, `App.xaml:918-920`) |
| **L1 chrome** | `kSheet` / `UrSheetBrush` | `#151515` | **Framing strips and flanking panes.** One step above the ground, never a content area. | nav pane (`App.xaml:138-140`), every 40px pane header + 28px group header (`App.xaml:923-961`), status strip (`App.xaml:581-586`) |
| **L2 object** | `kCard` / `UrCardBrush` | `#1C1C1C` | **Discrete things on the ground, and the one working bar.** | incoming bubbles, day-separator pill, composer bar (`ThreadView.cpp:723`), selected list row, row hover fill |
| **L3 raised / active** | `kCardHover` `#242424` / `kCardPressed` `#2A2A2A` | — | **Outgoing direction, pointer feedback, nav selection.** Never a resting region fill. | outgoing bubbles, `UrCardButtonStyle` states, nav selected bg (`App.xaml:150-152`) |

The rule that makes this a *system* rather than four observations:

> **Chrome flanks content; content holds objects; a pointer may borrow L3 but nothing rests there.**

### The seam rule: hairline vs tonal step

Adopt, and state once: **a tonal step separates a seam by itself; a 1px `kBorder` hairline is
spend only where both sides share a tone — and no seam ever gets more than one.** Consequences:

- `list | thread` — page on page, so `PaneRule` (`MainWindow.xaml:216-218`) is load-bearing. Keep.
- `thread | rail` — becomes a tonal step under proposal 1.2 below. Keep `RailRule`
  (`MainWindow.xaml:239-241`) anyway: it collapses with the rail below 1500dip and costs 1px.
- `header | body` — sheet over page *plus* the header's bottom hairline (`App.xaml:927`). This is
  the one sanctioned seam with both, and it reads crisp in the captures. Keep; don't generalize it.
- `thread | composer` — card over page, plus the existing top hairline (`ThreadView.cpp:725`).
  See 1.3.

### Shadows: a ruling, so nobody re-litigates it per surface

**No `ThemeShadow` anywhere in the pane shell.** Three reasons, each sufficient: the pane
vocabulary already rules it out; on `#101010` a black shadow is at or below the just-noticeable
difference (the same panel physics `App.xaml:259-262` cites for hover fills); and the shell
deliberately composites no system backdrop (`MainWindow.xaml:22-25`), so there is no ambient
surface for elevation to read against. Depth here is tone + hairline + motion. The one legitimate
candidate — the status strip's preview drawer, which opens *above* chrome
(`MainWindow.xaml:275-278`) — should get `sheet` + a `UrBorderStrongBrush` edge + an 8dip rise
(`kDist8`, `kBaseMs`) instead of a shadow. If a future overlay genuinely floats over *content*,
that one element may revisit this ruling.

### 1.1 The thread reads as the surface — already true; say it and protect it

The thread backlog is L0 page and its bubbles are L2/L3 objects: the model's "stage". No change.
What breaks the reading today is the rail being the *same* tone:

### 1.2 Lift the rail to L1 chrome — the one-line depth fix

`Views/InspectRailView.cpp:546` — `root.Background(urnw::colors::BackgroundBrush())` →
`SheetBrush()`. Effect in the frame: **nav (L1) | list (L0) | thread (L0) | rail (L1)** — chrome
on both flanks, content in the middle. The rail is a key/value inspector, i.e. chrome about
content, so this is also semantically right. One line, one existing token, no new resource key.
The rail's row hairlines (`kBorder` on `MakePaneKeyValueRow`, height 34) read identically on
`#151515`.

### 1.3 The composer rests ON the thread — keep it full-bleed, wake its seam

Do **not** float the composer as a rounded island with margins: that is the card vocabulary the
pane shell deleted, and `App.xaml`'s pane block says why. The composer stays a full-width L2 bar
with its top hairline (`ThreadView.cpp:721-726`). Two refinements:

1. **Seam wakes on focus.** The bar's *top* hairline lifts from `kBorder` to the `kBorderStrong`
   literal already declared at `ThreadView.cpp:679`, faded with the existing `FadeFocusRule`
   helper (`ThreadView.cpp:684-700`): `kFastMs` in on the standard curve, `kMicroMs` out on the
   exit curve, instant when `ShouldAnimate()` is false. The composer already has an interior focus
   rule under the input; this gives the *surface boundary* the same live reading. No new token,
   no new colour.
2. **Keep the bar at L2 (`kCard`).** It is the one working object pinned to the stage; L2 is
   already correct. Do not lift it to sheet — chrome is framing, and the composer is a tool.

### 1.4 Coherence gap to hand to the thread/wiring owner (not this proposal's change)

Every pane in the shell opens with the 40px L1 header strip (`App.xaml:922-929` says "every"),
but the demo's `ThreadHost` (`MainWindow.xaml:236`) mounts ThreadView with no header, so the
thread's top edge is a bare cut (visible in `thread.png`). The fix is a header strip carrying the
conversation name — a thread-surface decision owned by that group, flagged here because the
layering model depends on every pane having its chrome strip.

---

## 2. (b) Hover / pressed / focus vocabulary — "fills are identity; feedback is edge, brightness, motion"

The law, derived from what the code already enforces: **a fill that *means* something is never
repainted by a pointer state.** Bubble fills say direction (`ThreadView.cpp:329-333`), the unread
pill's fill says unread, a presence dot's fill says online. `UrBubbleButtonStyle` exists precisely
because of this (`App.xaml:732-752` comment). Rows, nav items and cards *may* fill-hover because
their resting fill is transparent or meaningless. Per element class:

### 2.1 Conversation rows — already complete; freeze it

Rest transparent → hover `kCard` → pressed `kCardPressed` (`UrPaneRowButtonStyle`,
`App.xaml:1023-1031`, 150ms VSM transition); selected = fill + 2px accent bar + automation name
(three channels, `ConversationListView.cpp:299-325`). The code comment at :306-309 already notes
hover and selected share the fill channel deliberately, which is why the bar exists. **No
change.** The vocabulary statement is the deliverable here.

### 2.2 Nav items — already complete; freeze it

Muted foreground → hover `#1C1C1C` bg + off-white fg → selected `#242424` bg + accent indicator
pill (`App.xaml:149-160`). No change.

### 2.3 Bubbles — two extensions on the existing edge-layer pattern

The style's `EdgeLayer` (`App.xaml:779-782`) is hover feedback without touching fill or resting
edge; `SetBubbleEdge` (`ThreadView.cpp:139-153`) is the one writer of resting/selected edge. Build
on exactly those:

**(i) Press physics: scale 0.97, template-only.** Declare a `CompositeTransform` on the template
`Root` and add `Pressed`-state setters for `ScaleX/ScaleY = 0.97` — the existing
`kPressScale` token (`UrMotion.h:77`), interpolated by the state group's existing
`GeneratedDuration="0:0:0.15"` (=`kFastMs`). VSM transitions are exempt from the `ShouldAnimate()`
gate by the motion system's own rule ("stock XAML VisualState transitions… are the platform's own
business", `UrMotion.h:83-84`). **One interaction to verify:** `RunBubbleEntrance`
(`ThreadView.cpp:98-105`) writes `RenderTransform` as a local value, replacing the template's
transform instance; VSM setter paths re-evaluate at state-change time, so press still works, but
a just-arrived ambient bubble (`--demo=thread --demo-autoplay`) must be eyeballed. Fallback if it
misbehaves: keep the existing `Pressed` opacity-0.92 dip and drop the scale.

**(ii) Selection draw-on: 90ms.** Today selection swaps the edge 1px→2px accent instantly
(`ThreadView.cpp:141-152`). The bubble click → rail-inspect swap is the demo's money interaction
(design §9.1 table) and deserves a tactile confirmation. Add a `SelectEdge` layer to the template
(2px `UrAccentBrush`, `Opacity=0`, same corner radius, sitting over `EdgeLayer`), keep
`SetBubbleEdge` the single writer, and have it run one `MakeSplineDouble(0→1, kMicroMs, standard)`
on the layer's opacity when selecting; deselect removes instantly (exits one step faster —
`kMicroMs` is the floor, so removal is immediate). Gated on `ShouldAnimate()`; with motion off the
edge appears instantly, exactly as today. **Constraint check:** the accent 2px outline is the
*already-sanctioned* selection use; this animates an existing mark, it does not add one.

**(iii) Considered and rejected: hover timestamp brighten** (in-bubble time `#5A5A5A`→`#989898`
on PointerOver). The timestamp lives in the button's `Content`, outside the VSM's reach, so this
needs `PointerEntered/Exited` handlers in `MakeBubbleRow` — the hand-wired-pointer pattern this
codebase measured and deleted (`App.xaml:251-255`, the `WireCardAffordances` note). Not worth
re-opening that door for an 11px nicety. Bubble hover stays the edge.

### 2.4 Buttons

- `UrCardButtonStyle` / `UrPaneRowButtonStyle` / `UrPaneActionPrimary/SecondaryStyle`: fills and
  state layers already correct; no change.
- The composer's inert icon buttons stay disabled at platform 0.38 opacity
  (`ThreadView.cpp:705-719`) — honesty §9.1, untouched.

### 2.5 Focus

`UseSystemFocusVisuals=True` is already set on every custom style. See §4 for the proposed accent
focus stroke and its reservation check.

---

## 3. (c) Typographic scale — one body voice, one chrome voice, three display homes

### What exists (all `App.xaml`)

| Voice | Face | Sizes (px / line-height) | Keys |
|---|---|---|---|
| Body | PP Neue Montreal | 11 (notes/meta), 12/16 caption, 13 (row content), 14/20 body, 18/24 large, 20/28 subtitle | `UrCaption/Body/BodyStrong/BodyLarge/Subtitle`, `UrRowTitle/Note`, `UrKey/ValueText` (206-243, 1063-1111) |
| Chrome | PP Neue Montreal, uppercase, SemiBold, letterspaced 60–90 | 12 (pane titles), 11 (group/column headers) | `UrPaneTitle`, `UrGroupHeaderText`, `UrPaneColumnText`, `UrPaneMeta`, `UrStatusField*` (933-970, 1134-1142, 595-611) |
| Display | ABC Gravity Extended / Extra Condensed; PP NeueBit | 28/36 + 40/52 titles; 26 condensed stat numerals; 24 NeueBit wordmark; 22 NeueBit card label | `UrTitle/TitleLarge`, `UrStatValueStyle`, `UrWordmarkFontFamily`, `UrCardLabelStyle` (184-187, 234-243, 557-562, 613-618) |

The emphasis rule already in the file (`App.xaml:202-205`): **SemiBold is the only weight step**
(Montreal ships one face; SemiBold is synthesized, Bold is never used in body).

### The display faces' three legal homes today

1. **Wordmark** — NeueBit Bold 24 (`MainWindow.xaml:61-63`).
2. **Destination titles** — ABC Gravity Extended 28 via `UrTitleTextStyle` in the nav
   `HeaderTemplate` (`MainWindow.xaml:103-108`; "Network" in `network-stub.png` is this).
3. **Stat numerals** — Extra Condensed 26 (`UrStatValueStyle`, `App.xaml:557-562`). This is the
   load-bearing precedent: the brand already speaks condensed for *figures of consequence*.

### Rules for everything new (Network page, status strip, Settings)

- Section strips (`CONVERSATIONS`, `MEMBERS`, `RETENTION`) keep the chrome voice exactly as built —
  uppercase Montreal 11–12, spacing 60–90, muted. The captures show this working; it is the right
  amount of structure. Do not promote them to a display face: that is the "two clashing heading
  styles" defect the ramp comment records fixing (`App.xaml:189-205`).
- Row heights come from the existing set — 34 (key/value), 36 (list), 40 (standard/header/search),
  44 (two-line), 56 (subject), 64 (conversation) — the kit's "one row height per list" rule
  (`UrComponents.h:300-304`). New surfaces pick from these; they do not invent a 52.
- Metadata is 11px (`textFaint`/`textMuted`): bubble timestamps, delivery words, row notes.
  Interactive content never drops below 12.

### One optional addition (P3, needs a new key — justified)

The pane-header count ("8" beside `CONVERSATIONS`, `MainWindow.xaml:184-186`) in **Extra
Condensed 14**, off-white — a figure, not body copy, extending the `UrStatValueStyle` precedent to
chrome scale. Because `UrPaneMetaStyle` is shared by group-header metas, this needs one new key
(`UrPaneCountTextStyle`, BasedOn it, overriding family/size) — the handoff requires saying why an
existing key can't serve: the count is a *figure* role, the meta style is a *caption* role, and
condensing every meta would put a display face on words. **Default: skip it.** It is the
lowest-payoff item here and the only one that grows the resource dictionary.

---

## 4. (d) Accent `#EFF7BB` at micro scale — the scarcity principle

### What already spends it (verified in code)

Send pill (`ThreadView.cpp:809-819`, disabled → `#33EFF7BB`) · bubble selection outline
(`ThreadView.cpp:147-149`) · list-row selection bar (`ConversationListView.cpp:158-165`) · unread
pill (ibid. :65-87, sanctioned by design §6.1 / Spec C §4.1) · nav selection indicator
(`App.xaml:149`).

The principle these five share, stated so it can be enforced: **accent marks the single thing on
screen that is singled out — the live action, the current selection, the unseen count.** Every
existing use is a ≤2px stroke or a ≤20px chip; none is a fill over an area; none is a bubble fill.

### Proposed additions (both flagged for owner/coordinator sign-off)

The handoff's literal reservation is "send button and selection outline only" (with the unread
pill and nav indicator pre-existing per spec). The two below are the same *class* — point-sized
marks singling out the live element — but they widen the literal list, so they are flagged rather
than asserted:

1. **Composer caret.** `box.CaretBrush(AccentBrush())` in `MakeComposer` (`ThreadView.cpp:784-794`).
   A 1.5px blinking line in the demo's one live input: "this is where you can act." It can never
   co-render with the send pill as a second mass, and it is off whenever the box is unfocused.
   *Fallback:* default caret.
2. **Keyboard focus stroke.** `FocusVisualPrimaryBrush = UrAccentBrush` on the bubble and pane-row
   styles. Focus visuals draw for keyboard focus only, so this never competes with pointer
   selection; it is the selection outline's exact visual language (2px stroke) reused for the
   other pointing device. *Fallback:* the system focus visual, which is what draws today.

### Explicitly rejected (so nobody re-proposes)

- **Read-state discs in accent** — delivery state rides glyph count/shape/word (`ThreadLayout.h:84-91`,
  `ThreadView.cpp:172-218`); recolouring adds a colour channel to the one surface forbidden from
  leaning on colour, and makes accent a recurring thread element. No.
- Accent hairlines under headers, accent in the Network page's relay wires (status/hop colours are
  `kUrGreen`/`kStatus*` territory — `UrColors.h:59-66`), accent on the DEMO chip, accent hover
  underlines. All either large-area or attention-diluting. No.

---

## 5. (e) Motion within the token set

Existing and kept: list stagger (`kBaseMs` + 40ms stagger, ≤6 steps,
`ConversationListView.cpp:327-354`), bubble entrance (fade + 10dip rise + 0.96→1.0, `kBaseMs`,
`ThreadView.cpp:75-123`), rail mode crossfade (`CrossfadePageSwap`, `kBaseMs`/`kFastMs`),
typing wave (`kPulseMs`, 140ms phase), composer focus rule (`kFastMs`/`kMicroMs`), window reveal.

New, all on existing tokens, all gated:

| # | What | Tokens | Where | Gate |
|---|---|---|---|---|
| M1 | Selection outline draw-on | `kMicroMs` 90, standard curve; deselect instant | `UrBubbleButtonStyle` template + `SetBubbleEdge` (§2.3ii) | `ShouldAnimate()` |
| M2 | Bubble press scale 0.97 | `kPressScale`, `kFastMs` via VSM `GeneratedDuration` | `UrBubbleButtonStyle` template (§2.3i) | platform VSM (exempt per `UrMotion.h:83-84`); verify against entrance transform |
| M3 | Typing-row entrance: fade + 4dip rise | `kFastMs` + `kDist4`, standard; exit = instant collapse (one step faster) | `SetThreadTyping` (`ThreadView.cpp:1176`) — one-shot storyboard on the *row*, targeting different properties than the dots' forever-storyboard, so they cannot fight | `ShouldAnimate()`; row must appear instantly with motion off |
| M4 | Delivery-morph cross-dissolve on ambient state advance | `kFastMs`, opacity swap between old/new glyph panels | `MakeDeliveryCluster` host — **only if** `DemoAutoplay`'s delivery advance (design §8) is actually built; I did not verify that. Owner: the T6/autoplay group, listed here so the cluster is restyled nowhere else | `ShouldAnimate()` |

Not proposed: hover scale on text rows (1.03 on text reads as jitter), anything over `kSlowMs`
inside the shell, new springs (`kRevealSpringDamping` stays the reveal's), animated list reflow on
search filtering (expensive, and correct-instant is better under reduce-motion).

---

## 6. Constraint ledger

| Proposal | Constraint touched | How it stays inside |
|---|---|---|
| All of it | G4 honesty | **Zero new user-visible strings.** The three prefix-first strings, the gate-asserted framing, the unrendered cipher — untouched. No glyph, colour or layout here implies encryption, verification or attestation. |
| Rail → sheet; composer seam | G3 brand | Both spend existing tokens (`kSheet`, `kBorderStrong` literal at `ThreadView.cpp:679`). No new colour, no new key. |
| No-shadow ruling | pane vocabulary (`App.xaml:872-899`) | Restates it and gives the one overlay (drawer) a tonal alternative. |
| Bubble press scale, selection draw-on | accent reservation; one-writer rule | The 2px accent outline is the *sanctioned* selection use; `SetBubbleEdge` remains the sole edge writer; EdgeLayer pattern untouched. |
| Caret / focus stroke | accent reservation | Flagged as a micro-extension of the same class (point marks singling out the live element); fallbacks specified; owner sign-off requested. |
| Type rules | display-face reservation | Display faces stay in their three existing homes; the one proposed extension (pane count numeral) is a figure role argued from the `UrStatValueStyle` precedent, needs one justified new key, and is marked optional. |
| M1–M4 | motion tokens; `ShouldAnimate()` | No new durations, no new curves; exits one step faster throughout; reduce-motion path is instant and correct (note: that path has never executed on this machine — `SPI_GETCLIENTAREAANIMATION = 1` — so every new gated branch needs the deliberate-break test the handoff's §7 process requires). |
| Member identicons (below) | fixture immutability | `MemberRef.identityKey` / `DeviceRef.ownerKey` already exist (`DemoWorld.h:36,46`). `DemoWorld.cpp` untouched. |

## 7. File-level change map (nothing here is written yet)

- `app/src/App/App.xaml` — `UrBubbleButtonStyle` template (:753-825): add `SelectEdge` layer,
  template `CompositeTransform`, `Pressed` scale setters. *Optional:* one new key
  `UrPaneCountTextStyle` (justification in §3).
- `app/src/App/Views/ThreadView.cpp` — `SetBubbleEdge` (:139): selection draw-on storyboard (M1).
  `MakeComposer` (:721): focus-woken top seam (§1.3), optional `CaretBrush` (§4.1).
  `SetThreadTyping` (:1176): row entrance (M3).
- `app/src/App/Views/InspectRailView.cpp` — :546 background → `SheetBrush()` (§1.2). Member/device
  mini-identicons: add a 20px `MakeIdenticon(member.identityKey, 20)` to `MakeMemberRow` (:579)
  and `MakeDeviceRow` (:610) — this is what turns MEMBERS into the "Session-style connected
  clients" the owner named, using the one vivid element the app already has. **Caveat:**
  `MakeIdenticon` applies its own `CornerRadius(8)` at any size (`Identicon.h:35-45`) — radius 8
  on 20px reads as a squircle chip; acceptable, or change `Identicon.cpp` to a proportional radius
  after checking what the F-group gates assert about it. Coordinate with the rail owner.
- `app/src/App/Views/ThreadLayout.{h,cpp}` — *only if* the thread group adopts run-shaped bubble
  corners (last-of-run tight corner): the run-end decision belongs in the pure planner beside
  `endsOutgoingRun` so `--diagnose` can walk it. Shape, not colour; G4-neutral. Thread agent owns
  it; listed here because it is a vocabulary decision.
- **No changes** to `UrColors.h`, `UrMotion.h`, `Demo/DemoWorld.cpp`, or any string resource.

## 8. Payoff vs risk — suggested landing order

1. **Rail → sheet** (1 line; the single biggest depth win in the frame; zero risk). Do first.
2. **M2 + M1: press scale + selection draw-on** (template + one storyboard; makes the demo's core
   click physical). Risk: the entrance-transform interaction — verify with `--demo=thread
   --demo-autoplay`; fallback is the existing opacity dip.
3. **Composer focus seam** (reuses `FadeFocusRule`; low risk).
4. **Member/device 20px identicons** (high identity payoff; check identicon radius gates;
   rail-owner coordination).
5. **M3 typing-row entrance** (low risk, small moment).
6. **Caret / focus accent** (1–2 lines each; *after* owner sign-off on the reservation extension).
7. **M4 delivery morph** (only with the autoplay owner, once its state advance is confirmed).
8. **Run-shaped corners** (best "not-basic" bubble payoff; needs pure-planner work + thread-agent
   coordination; medium risk — corner radii are ungated visuals, so screenshot-verify per handoff §7).
9. **Pane-count condensed numeral** (optional; only new resource key in the set; skip by default).
