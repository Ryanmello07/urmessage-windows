# D3 — Conversation list and nav pane: design proposal

Author: design-swarm agent D3. Date: 2026-09-08. Scope: the conversation list pane and the
NavigationView shell — row layout, state treatments, identicon language, search, entrance
motion, nav pane, and the below-1500dip reading. Research + proposal only: **no file under
`app/src` was touched**. Everything below was read out of the tree or measured off the
baseline captures (`.verify-baseline/chats.png`, `inspect.png`, `network-stub.png`; no
1200x800 capture exists in the baseline set — all three are 1560x900 logical — so the
narrow-width analysis reasons from `MainWindow::ApplyBreakpoint` and
`urmsg::demo::LayoutFor`, and says so where it does).

Constraint key: **G4** honesty, **G3** brand tokens/reservations, **3CH** three-channel
state rule, **MOT** UrMotion tokens + `ShouldAnimate()`, **FIX** immutable
`Demo/DemoWorld.cpp`, **DIAG** `--diagnose` is pure C++ pre-`init_apartment`.

---

## 1. What is actually there (grounding)

**Row construction** — `app/src/App/Views/ConversationListView.cpp:120-259`. One row =
one `kit::MakePaneTwoLineRowButton` call at `kConversationRowHeight = 64`
(`Views/ConversationListView.h:27`; the kit default 44 would put 40 of identicon inside 44
of row). The kit's chevron column is deleted; three things are appended into the kit grid:
the 2px selection bar (`kTagSelectionBar`, accent, `Opacity 0`, `Margin -10` -> lands 2 dip
in from the row's left edge), the 40dip `MakeIdenticon(c.identityKey, 40)` pinned left with
the text pushed 52 dip clear, and a trailing StackPanel: time label (kit `UrValueTextStyle`,
13px muted) over a cluster row (Advanced group-id chip 10px faint; mute glyph `E74F`; timer
glyph `E916`; unread pill). The unread pill (`:62-87`) is `UrAccentBrush #EFF7BB`, 18px
tall, radius 9, 11px SemiBold inverse text — the **only** chromatic mark a row may carry
(Spec C 4.1, stated in the code comment).

**Selection — three channels** (`ConversationListView.cpp:299-325`): (1) fill step
transparent -> `kCard #1C1C1C`, deliberately the same value `UrPaneRowButtonStyle` paints on
PointerOver (`App.xaml:1023-1027`); (2) the 2px accent bar opacity 0->1 — a shape change;
(3) the automation Name gains `. Selected` (`ConversationRowModel.cpp:69-86`). Any redesign
must keep all three.

**Hover/pressed today**: template setters, `GeneratedDuration 0.15` (= `kFastMs`):
hover `Root.Background -> UrCardBrush #1C1C1C`, pressed `-> UrCardPressedBrush #2A2A2A`
(`App.xaml:1016-1038`). So **hover fill == selected fill** — channel 1 is deliberately
indistinguishable between them, which is why channels 2 and 3 exist. `UseSystemFocusVisuals`
is already on.

**Entrance** (`ConversationListView.cpp:283-291, 327-354`): start pose `Opacity 0` written
in the constructor (WindowReveal rule), then `AnimateConversationListEntrance` — opacity
0->1, `kBaseMs 250`, standard bezier `(0.10,0.90)->(0.20,1.00)`, stagger `min(i,6)*40ms`
(`ConversationRowDelayMs`, gate-asserted at 0/5/6/50). **Opacity only — no translation.**
The RECENT group header does not participate.

**Search** (`MainWindow.xaml.cpp:227-262, 450-460`; `UrComponents.cpp:571-605`): a 40px
`MakePaneRow` on **page** `#101010` background, 13px muted Search glyph, borderless
transparent TextBox (`UrPaneSearchStyle`, `App.xaml:1118-1128`), placeholder from
`search_conversations`. `TextChanged` -> `ApplyConversationListFilter` — Visibility collapse
only, `ListPaneCount` updated to the visible count. Privacy: a disappearing conversation
matches **by name only** (`ConversationRowModel.cpp:94-103`), gate-asserted including a
locally built ghost row. **There is no zero-result state** — the pane below RECENT simply
goes blank and the count reads `0`.

**Identicon** (`Identicon.cpp:158-250`): 5x5 mirrored pattern (centre-on/corner-off
override, popcount static-asserted), six-colour palette Tint()ed toward `kCard`,
compile-time-proven >=30 deg of hue from every reserved state colour; plate = hue at alpha
`0x33`, cells solid hue, **CornerRadius 8 owned by `MakeIdenticon` — callers must not set
one**. Sizes in the app: 40 (list row, rail subject row `InspectRailView.cpp:321`), 28
(thread sender header, `ThreadView.h:47`).

**Nav shell** (`MainWindow.xaml:90-148`): `NavigationView`, `PaneDisplayMode="Auto"`
(>=1008epx expanded 220; 641-1007 compact 48 rail; <=640 overlay), item min height 44, 20px
Segoe Fluent glyphs, `IsSettingsVisible="False"` (load-bearing). Theme keys
(`App.xaml:138-160`): pane bg `#151515`, selection indicator `#EFF7BB`, item bg
selected `#242424` / hover `#1C1C1C` / pressed `#242424`, foreground `#989898` -> `#F8F8F8`
on selected/hover. Settings is pinned to the bottom via `FooterMenuItems`; `PaneFooter` is an
8px spacer. **The pane's right edge is a bg step only — no 1px rule**, unlike every other
pane boundary in the app (list|thread and thread|rail both get `UrPaneVRuleStyle` borders).
Known platform landmine: flipping a `NavigationViewItem` Collapsed->Visible corrupts WASDK
2.2.0's Auto pane-mode resolution; `DrainDeepLink` works around it with a
LeftCompact->Auto cycle (`MainWindow.xaml.cpp:530-569`). **Anything new in the nav must not
perform that transition.**

**DEMO chip** (`MainWindow.xaml:64-78`): sheet bg, 1px `UrBorderBrush`, radius 4, caption
text, set from code as an English literal (`kDemoWatermark`, `MainWindow.xaml.cpp:49`) —
this is the established pattern for any new string, since `Resources.resw` is generated and
may not gain keys. Neighbours: 20px `app.ico` + PP NeueBit Bold 24 wordmark to the left,
drag space and system caption buttons to the right. `--demo-watermark=off` removes it, so
nothing honesty-bearing may live on it.

**Strings available without touching resw** (verified in `Strings/en/Resources.resw`):
`app_name, nav_chats, nav_contacts, nav_settings, pane_conversations, pane_thread,
group_recent, search_conversations, thread_none_selected, destination_not_built`. Anything
else must be an English literal set from code (DemoChip precedent).

**Breakpoints** (`Demo/DemoShellState.h:37-59`, `MainWindow.xaml.cpp:669-721`): wide >=1000
(list 320 fixed + rule + thread star), rail >=1500 (360 fixed), strip >=560 content height.
Demo launches 1560x900 (`WindowShell.h:31-32`).

---

## 2. Proposals — conversation list

### 2.1 Row layout and density — keep the skeleton, tune the rhythm

Keep: 64dip uniform height (protected property — "one row height per list"), identicon 40,
the chevron-less two-column grid, the trailing time-over-cluster stack. The row's anatomy is
Spec C 4.1-correct; what is off is the **hierarchy inside it**:

- **Time label 13px -> 11px**, same muted `#989898`. Today the time (`UrValueTextStyle` 13)
  outweighs the preview line (`UrRowNoteStyle` 11) and ties with the name. At 11 it aligns
  with the cluster beneath it and the name at 13 becomes the clear primary. Change: one
  setter on `row.value` after the kit hands it over in
  `ConversationListView.cpp:195-201` (`row.value.FontSize(11)`). No new style key — a
  one-off override on a code-built row, not a new text species. **G3**: reuses
  `UrTextMutedBrush`; no new colour, no new key.
- **Unread rows: name weight Regular -> SemiBold.** Signal's unread-bold, done as a *weight*
  channel so the pill stays the only chromatic mark (Spec C 4.1). No colour change
  anywhere. `PaneTwoLineRowButton` returns `title`, so this is
  `if (!model.unread.empty()) row.title.FontWeight(SemiBold())` in `MakeConversationRow`.
  Zero pixels change on rows with nothing unread. **G3**: weight is not colour; the pill
  reservation is intact.
- **Identicon stays 40.** Tempting to grow it for presence, but 40 is also the rail subject
  size (`InspectRailView.cpp:321`) — list and rail showing the same face at the same size is
  the continuity the eye rides when moving across panes. Presence comes from interaction
  (2.3), not size.

### 2.2 Hover / pressed / selected — teach the bar, don't repaint the row

Selection's three channels are preserved untouched. The additions are a **hover rehearsal of
channel 2** and a pressed tick, both on elements that already exist:

- **Hover bar.** On `PointerEntered`, paint the existing selection bar with
  `UrBorderBrush #1FFFFFFF` at `Opacity 1` (it is transparent, accent-coloured, and in place
  on every row); on `PointerExited`, restore accent + `Opacity 0`. On the *selected* row,
  hover changes nothing — the accent bar already owns the slot. Net effect: hovering any row
  draws a hairline-strength leading tick exactly where selection would land; selection
  "commits" that tick to accent. Shape channel, not colour alone; achromatic border-white at
  12% cannot be mistaken for `#EFF7BB`. **3CH**: selection channels unchanged; hover reuses
  channel 2's *geometry* with a different colour, so the two states never share a full
  signal. **G3**: border token, not accent. Implementation: two handlers on `row.root` in
  `MakeConversationRow` (~15 lines, `ConversationListView.cpp`), swapping
  `bar.Background`/`bar.Opacity` through the existing `kTagSelectionBar` lookup. Because the
  bar sits at row scope, not in the Button template, this composes cleanly with the
  template's fill-step hover.
- **Keep the fill-step hover as-is** (`#1C1C1C` at `kFastMs` 150 via the existing
  VisualTransition). It matches the pane model ("a row's separation is its hairline and its
  hover fill") and its collision with selected-fill is deliberate and documented.
- **Pressed: identicon micro-scale.** On `PointerPressed`, scale the identicon to
  `kPressScale 0.97` over `kMicroMs 90`, restore on release. The row itself must **not**
  scale (a full-width pane row shrinking reads as a bug); the avatar taking the press is the
  playful version that stays inside the pane discipline. Handlers beside the hover pair in
  `ConversationListView.cpp`; the identicon gets a `ScaleTransform` at build time.
  **MOT**: both durations are existing tokens; these two handlers are the only hand-rolled
  part and are trivially gated on `ShouldAnimate()` (skip installing them when false).

### 2.3 Identicon presentation — the plate responds, and the language gets an inverse

The identicons are the most energetic element on screen (handoff 5.4) and currently the
only static one. One restrained interaction plus one new *usage* of the language:

- **Hover plate lift.** In the same `PointerEntered` handler, raise the identicon plate's
  background alpha one step (`0x33` -> `0x4D`, i.e. 20% -> 30% of the row's own hue) and
  back on exit. The avatar visibly "wakes" under the pointer without any new colour entering
  the app — the plate is already `WithAlpha(hue, ...)`, so this is a parameter change on an
  existing brush. **G3**: the hue is the compile-time-proven palette; only alpha moves.
  Implementation: `Identicon.cpp` grows a tiny `SetIdenticonPlateOpacity(Border, double)`
  (or `MakeIdenticon` gains an out-param for the plate brush) so the row can reach the
  plate; the palette, radius-8 rule and hue proofs are untouched.
- **The lattice as the app's "empty" motif (avatars-as-texture).** Add
  `MakeIdenticonLattice(double size)` to `Identicon.h/.cpp`: the same 5x5 star-grid
  construction as `MakeIdenticon`, but every cell drawn as a 1px rounded-square *outline* in
  `kTextFaint` at alpha `0x33` — an empty frame where a person-mark would go. Achromatic
  (faint grey), so it provably cannot collide with the hue-separated palette or any state
  colour (**G3**: no palette entry added, no reserved hue approached — grey has no hue).
  Consumers in this proposal: the search empty state (2.5) and optionally the DEMO chip
  (3.4). This is the identicon language echoing into empty/loading states *without*
  inventing a fake person: a lattice with no seed is the honest inverse of an identicon.
  Loading states need nothing more — the world is static and the window reveal is the app's
  only loading moment.
- Explicitly **not** proposed: accent rings on selected-row identicons (duplicates channel 2
  in the same place — noise), identicon textures behind list rows (violates pane flatness),
  animated cell patterns (would need new durations — **MOT** forbids inventing them, and a
  looping identicon undermines its identity function).

### 2.4 Unread pill, muted and timer glyphs

- **Pill: keep exactly as built** (18px stadium, accent bg, inverse 11 SemiBold). It is the
  one sanctioned chromatic mark and it reads well at capture scale. One dormant hardening:
  **muted + unread => pill renders `cardHover #242424` bg with `#989898` text** (Signal's
  muted-conversation convention). The fixture has no row that is both (baseline: URnetwork
  core = 2, Freya = 3, Sam = muted-only, Marta = timer-only), so this paints zero pixels
  today — a rule installed for the day the world changes, one `if` at the
  `MakeUnreadPill` call site. **FIX** respected: no fixture change; the rule is inert
  against today's world. Flag it in the ledger as deliberate-zero-pixel, or the next
  reviewer "fixes" the invisible code.
- **Timer row: give the second line an honest label.** Today a disappearing conversation
  draws the timer glyph in the trailing cluster and *nothing* on the preview line (the model
  refuses the preview — `ConversationRowModel.cpp:55`), so the 64px row reads half-empty
  (baseline: "Marta Kowalska" over a blank line). Proposal: second line = timer glyph
  `E916` at 12px + `Disappearing messages`, both `textFaint #5A5A5A`, 11px
  (`UrRowNoteStyle` metrics). **G4 analysis**: this describes the *field*, not a claim — the
  automation name already speaks exactly these words (`ConversationRowModel.cpp:74-77`), so
  a screen reader hears no change; the string is an English literal from code (DemoChip
  precedent — no resw key). **Spec C 4.2 intent check for the coordinator**: the rule is
  "glyph INSTEAD of preview" — the label keeps the preview hidden and names the state; if
  the coordinator reads 4.2 as "the line must stay empty", drop this item. It is the only
  one with a spec-interpretation dependency. Implementation: the model gains nothing
  (`showTimer` exists); the row builder appends a glyph+text note when `model.showTimer`,
  in place of the collapsed preview.
- **Muted glyph: keep.** 12px `E74F` muted in the cluster is right-sized already
  (`ConversationListView.cpp:46-60` records why 12, not 16).

### 2.5 Search — focus treatment, Esc, honest count, a real empty state

- **Chrome block: make the search row `sheet #151515`.** `MakePaneSearchRow` currently sets
  `BackgroundBrush()` = page (`UrComponents.cpp:574`). Switching it to `SheetBrush()` fuses
  the 40px pane header and the 40px search row into one 80px chrome slab above the rows —
  the cheapest depth win on the pane (handoff gap 1), using only the existing ramp step.
  One line in `UrComponents.cpp`; the builder has exactly one consumer surface (this pane —
  the non-demo placeholder list goes through the same code and inherits the same
  improvement). **G3**: existing token.
- **Focus treatment, no new colour.** On `GotFocus`: search glyph `#989898` -> `#F8F8F8`
  (off-white — the glyph *becomes active*, the same channel nav items use on hover); on
  `LostFocus`: revert. Deliberately **not** an accent underline: accent is reserved for the
  send button and selection outlines, and a focused field is neither (**G3**). Handlers in
  `MainWindow::BuildConversationList` where `search_` is built
  (`MainWindow.xaml.cpp:231-233`). Register them outside the `--demo` branch too — harmless
  on the placeholder list, and the two launches stay identical.
- **Esc clears.** `KeyDown` on `search_.box`: `Escape` -> `Text(L"")`; the existing
  `TextChanged` handler does the rest. Two lines, big demo-feel win. Same registration
  point as above.
- **Count reads `3 of 8` while filtering.** `ApplyConversationFilter`
  (`MainWindow.xaml.cpp:450-460`) sets `ListPaneCount` to `visible`; make it
  `visible == total ? to_hstring(total) : to_hstring(visible) + L" of " + to_hstring(total)`.
  Stays the ONE count readout (the RECENT header deliberately has none); `UrPaneMetaStyle`
  already right-aligns and trims. English literal fragment, no resw key.
- **Empty state.** When the filter returns 0, show a centred module in the list's scroll
  area: the identicon lattice (2.3, ~64dip), then `No conversations match` (12px muted),
  then `Search checks names and visible previews only.` (11px faint). That second line is
  the privacy property *taught at the moment it is operating* — and it is strictly true of
  `ConversationRowMatches` (name always; preview only when visible). **G4**: describes
  behaviour, claims no crypto. Both strings are code literals. Mount: a `Grid` sibling added
  at `ListScaffold` row 2 **after** the ScrollViewer (renders above it), `Visibility`
  toggled in `ApplyConversationFilter`; built only under `--demo` where the filter is wired.
  **Mount-discipline note for the implementer**: append it to `ListScaffold` itself, not a
  new container — `ListHost` (`MainWindow.xaml:201-214`) is the standing warning about
  writing into a collapsed twin. Entrance: the module fades in at `kBaseMs 250` standard
  bezier; exit is `kFastMs 150` (**MOT**: exits one step faster), both through the
  `ShouldAnimate()` gate, instant swap otherwise.

### 2.6 Entrance stagger — add the rise, keep the clock

Today is opacity-only. Proposal, matching the bubble entrance pattern already in the design
(doc 7: fade + rise):

- Start pose gains `TranslateY = kDist8 (8dip)` via a `CompositeTransform` on `row.root`
  (written beside the existing `Opacity 0` at `ConversationListView.cpp:289-291`); the
  entrance storyboard adds a second spline double per row, `TranslateY 8->0`, **same**
  `kBaseMs 250`, **same** standard bezier, **same** `ConversationRowDelayMs(i)` stagger —
  no new duration, no new curve, no new stagger constant. **MOT**: entirely inside the
  existing gate; when `ShouldAnimate()` is false neither the opacity nor the transform start
  pose is ever written (that two-pose-skip already exists for opacity — extend the same
  `if`).
- **Include the RECENT group header at delay 0** so the chrome doesn't pop a frame ahead of
  the rows it titles. One more timeline in the same storyboard.
- No scale on rows (text shimmer), no increase to stagger steps (the 6-step cap is
  gate-asserted — `demo.list.stagger` — do not touch it).

---

## 3. Proposals — nav pane and shell

### 3.1 The missing rule (highest payoff on this surface)

Give the nav pane's right edge the same 1px `UrBorderBrush` rule every other pane boundary
has (`PaneRule`, `RailRule`). Today the pane/content boundary is a `#151515`-on-`#101010`
step only — effectively invisible in the baseline captures. Implementation: a
`Border x:Name="NavPaneRule"` (`Width 1`, `UrPaneVRuleStyle`, left-aligned) as the first
child of the NavigationView's content Grid in `MainWindow.xaml`; `ApplyBreakpoint` shows it
whenever the pane is not overlay (content width > 640epx — the same threshold Auto mode
uses) and hides it below, one clause beside the existing thread/rail visibility writes
(`MainWindow.xaml.cpp:669-721`). **G3**: the rule token is the brand hairline. This is the
pane model completing itself: *panes are divided by 1px rules* — the nav pane is a pane.

### 3.2 Nav items — keep the theme states, add one separator and tooltips

- Hover/selected/pressed are already correct via theme keys (`App.xaml:149-160`): indicator
  accent, selected bg `#242424`, hover `#1C1C1C`, fg `#989898`->`#F8F8F8` — two-and-a-half
  channels already (bar shape + fill + fg). **Do not restyle the item template**; the
  WASDK 2.2.0 Auto-mode corruption (`MainWindow.xaml.cpp:530-569`) means every structural
  nav change must be re-verified for the icon-only-stuck defect, and the current states are
  brand-correct.
- Add one `NavigationViewItemSeparator` at the top of `FooterMenuItems`, above Settings —
  the pane model's hairline between the destination stack and the utility row.
  `NavigationViewItemSeparatorForeground` is already themed `#1FFFFFFF` (`App.xaml:146`).
  One XAML line.
- Compact-rail mode (641-1007epx, icons only): set `ToolTipService.ToolTip` to the same
  `Loc(nav_*)` string at the `Content()` write sites in `ApplyStrings`/`EnterDemoMode`, so
  an icon-only item still names itself on hover. Zero visual change in expanded mode.
- **Optional, P2 — unread InfoBadge on Chats.** `NavigationViewItem.InfoBadge` with the
  summed unread (5 in today's world, computed in `BuildConversationList`). Theme overrides
  needed so it does not render system-accent blue: `InfoBadgeBackground #242424`,
  `InfoBadgeForeground #F8F8F8` — **not** accent (reserved, **G3**), not green (presence),
  not red (danger): a neutral badge beside a neutral glyph. The world's unread counts never
  change at runtime, so the badge is static; if that reads as pointless in review, drop it —
  it is the most cuttable item in this document. Caution: set the `InfoBadge` property once
  at build; screenshot-verify the nav afterwards (the WASDK defect above is about item
  visibility, but treat any structural nav change with the same suspicion).

### 3.3 Footer Settings placement

Already right: `FooterMenuItems` pins it to the bottom of the pane, expanded and compact
alike; `PaneFooter`'s 8px spacer keeps it off the window edge. Keep. The separator (3.2)
is the only addition.

### 3.4 The DEMO chip's neighbours — leave the title bar quiet, one optional tie-in

The title bar (icon + NeueBit wordmark + chip, then drag space) is correct and calm; the
right side should stay empty — it is the drag region and the caption buttons' neighbour, and
anything right-aligned there reads as window chrome. **Do not** add an ADVANCED chip or a
status dot here (Advanced state is visible in the nav's Developer item; connection state
belongs to the S-group's status strip, not the title bar). The one tasteful tie-in:
**prefix the DEMO chip's text with a 10dip identicon lattice** (2.3) at alpha `0x33` —
decorative, links the chip to the identicon language. **G4**: the chip is removable
(`--demo-watermark=off`) and this adds no words, so nothing honesty-bearing depends on it;
the prefix-first framing strings are elsewhere and untouched. If the lattice item (2.5
empty state) is cut, cut this with it — it exists to make the lattice motif recur, not
standalone.

---

## 4. The below-1500dip layout (and why 1200x800 feels empty)

Arithmetic from `ApplyBreakpoint`/`LayoutFor` at a 1200x800 window (~1186x786 content):
nav 220 + list 320 + rule 1 + thread ~645; rail gone; status strip visible once the S-group
lands (786 >= 560). At 800 tall, the list pane itself is nearly full (40 header + 40 search
+ 28 RECENT + 8x64 rows = 620 of ~712 available after the 26px strip) — **the emptiness is
in the thread column**, which is the thread agent's surface (bubble cap 68%/640dip over a
645dip column). Within list/nav scope, three contributions:

1. **Step the list width: 320 -> 360 at content >= 1200dip** (still fixed widths — the "a
   proportional rail grows into dead space" rule from `DemoShellState.h:43-45` is about star
   weights, not about steps). More room for name+preview at exactly the widths where the
   rail is absent and the list is the only left-hand content. New constants
   `kListWideWidthDip = 360.0` and threshold `kListWideBreakpointDip = 1200.0` in
   `Demo/DemoShellState.h` (pure, beside `kRailBreakpointDip`), consumed in
   `ApplyBreakpoint` (`MainWindow.xaml.cpp:689-691`). **DIAG**: add boundary probes in
   `Startup.cpp` beside the existing `LayoutFor` assertions (the pattern at
   `Startup.cpp:352-362`): `1199 -> 320`, `1200 -> 360`, rail thresholds unchanged. Keep the
   step *out* of `LayoutFor`'s `Layout` struct if you want zero churn to its gate — a local
   width read in `ApplyBreakpoint` (the one place that decides what wide means) is enough;
   putting it in `Layout` is cleaner but touches the asserted struct. Coordinator's call.
2. **The nav rule (3.1) matters most here**: at 1560 the rail gives the right side a third
   vertical; at 1200 there are only two content columns, and the missing left rule is part
   of what makes the composition read as "one pane and a void".
3. **Nav stays expanded at 1200** (Auto: >=1008epx). Do not force it compact — the 220 pane
   is what gives the narrow window its structure; collapsing it would widen the void.

Explicitly not proposed: a slim rail below 1500 (design 6.5a makes inspect's absence
deliberate), a list-pane footer line (chrome noise; the composer's "Demo — nothing is
sent..." line already anchors the bottom edge on the thread side).

---

## 5. File-level change inventory

| File | Change | Proposal |
|---|---|---|
| `app/src/App/Views/ConversationListView.cpp` | time 11px; unread name weight; hover/pressed/exit handlers (bar, plate, scale); timer-row label line; entrance rise transform; group-header timeline | 2.1-2.6 |
| `app/src/App/Views/ConversationListView.h` | nothing required; optionally a `SetConversationRowHover` helper if the handlers grow | — |
| `app/src/App/Views/ConversationRowModel.cpp` | nothing (model already exposes `showTimer`, `unread`); dormant muted-pill rule lives in the view | 2.4 |
| `app/src/App/Identicon.h/.cpp` | `MakeIdenticonLattice(size)`; plate-opacity setter or out-param | 2.3, 2.5, 3.4 |
| `app/src/App/UrComponents.cpp` | `MakePaneSearchRow` background -> `SheetBrush()` | 2.5 |
| `app/src/App/MainWindow.xaml` | `NavPaneRule` border; `NavigationViewItemSeparator`; optional `InfoBadge` on `ChatsNavItem` | 3.1, 3.2 |
| `app/src/App/MainWindow.xaml.cpp` | search Esc/focus handlers; `N of 8` count; empty-state mount + toggle in `ApplyConversationFilter`; nav tooltips; `NavPaneRule` visibility and list-width step in `ApplyBreakpoint`; DEMO chip lattice | 2.5, 3.1-3.4, 4 |
| `app/src/App/Demo/DemoShellState.h` | `kListWideWidthDip`, `kListWideBreakpointDip` (pure constants) | 4 |
| `app/src/App/Startup.cpp` | boundary probes for the list-width step | 4 |
| `app/src/App/App.xaml` | only if the InfoBadge ships: `InfoBadgeBackground/Foreground` overrides | 3.2 |

Not touched: `Demo/DemoWorld.cpp` (**FIX**), `App.vcxproj` (no `ObjectFileName`), no resw
keys, no new `Ur*` colour keys, no new motion tokens. New gates: only the list-width
boundary probes — everything else here is pixel work verified by screenshot, and per the
handoff's process lessons each pixel item lands with a before/after capture compared by
pixel difference, not by a new `--diagnose` tautology.

---

## 6. Payoff vs risk — suggested landing order

1. **Nav pane rule + search-row sheet slab** (3.1, 2.5-chrome) — highest depth-per-line in
   this document; two tokens, zero behaviour. Risk: ~none. Verify by capture.
2. **Search focus + Esc + `N of 8`** (2.5) — makes the one interactive control on the pane
   feel engineered. Risk: low; handler wiring only.
3. **Entrance rise** (2.6) — the visible "smooth modern" the owner asked for, on tokens.
   Risk: low; the two-pose-skip pattern for `ShouldAnimate()` already exists — extend it,
   don't restructure it. The reduce-motion branch has never executed on this machine
   (handoff 4); say so in the ledger.
4. **Hover bar + plate lift + press scale** (2.2, 2.3-hover) — the "alive" pass.
   Risk: medium-low; per-row handlers must not fight `SetConversationSelected`'s bar writes
   (hover on the selected row must no-op). Screenshot the selected-row-hover case
   explicitly.
5. **Timer-row label** (2.4) — one row's worth of pixels, big clarity win.
   Risk: none technical; **needs coordinator sign-off against Spec C 4.2's intent** — the
   only spec-interpretation dependency here.
6. **Search empty state + lattice** (2.5-empty, 2.3-lattice) — the identicon language's
   second home; teaches the privacy property in place. Risk: medium; new builder plus a
   visibility-toggled sibling (respect the collapsed-twin lesson; mount into `ListScaffold`).
7. **Nav separator + compact-rail tooltips + DEMO chip lattice** (3.2, 3.4) — cosmetic
   polish, one-liners. Risk: ~none, but screenshot the nav after ANY nav edit (WASDK
   2.2.0 Auto-mode defect; re-verify labels at 1560x900 under `--demo=network
   --demo-advanced`).
8. **List width step 320->360 at >=1200** (4) — fixes the 1200x800 sag. Risk: medium;
   touches breakpoint logic with gate-tested neighbours — add the boundary probes and
   demonstrate one failing against a deliberate break before landing (house rule).
9. **InfoBadge on Chats** (3.2-optional) — nice Matrix-ish touch, most cuttable. Land last
   or drop. Risk: medium-low; theme-key overrides + static value.
10. **Muted-unread pill rule** (2.4-dormant) — zero pixels today; land whenever, labelled
    as deliberately inert.

Everything above stays inside G3 (no accent beyond send/selection, no gold, no new colour
keys), G4 (no crypto implication anywhere; the two new strings describe behaviour and fields
only), 3CH (selection's three channels untouched; delivery untouched — not this surface),
MOT (250/150/90ms, 40ms stagger, 8dip, standard bezier, `ShouldAnimate()`), and FIX (the
world is read, never edited).
