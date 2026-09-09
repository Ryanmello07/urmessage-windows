# D5 — Network page and status strip: design proposal

**Date:** 2026-09-08. **Author:** design swarm agent D5. **Status:** proposal, nothing here is built.
**Surfaces:** the Network destination (plan tasks N1–N6) and the status strip + preview drawer
(S1–S4). Both are blank canvases today: `NetworkHost` (MainWindow.xaml:265) is an empty Grid and
`--demo=network` routes to `StubPage` (MainWindow.xaml.cpp:739-744); `StatusStripHost`
(MainWindow.xaml:278) exists but nothing builds into it.

Everything below is grounded in four sources, read in full: the fixture (`app/src/App/Demo/
DemoWorld.{h,cpp}` — immutable, I10-fingerprinted), the shell (`MainWindow.xaml`, `MainWindow.
xaml.cpp`, `Demo/DemoShellState.h`), the component kit and tokens (`UrComponents.h`, `App.xaml`,
`UrColors.h`, `UrMotion.h`), the design doc (`docs/superpowers/specs/2026-09-06-urmessage-demo-ui-
design.md` §6.4–6.6, D4/D6), the ten task briefs, and the audit (`docs/superpowers/plans/
2026-09-08-remaining-task-audit.md`), whose overrides are treated as settled law here.

---

## 1. The data, and only the data

The fixture is the entire universe these two surfaces may render (DemoWorld.cpp:229-391):

| Field | Value | Where it may appear |
|---|---|---|
| `World::relayPath` | exactly 3 nodes, I7-asserted (`Startup.cpp:317-322`: "3 nodes, 0 empty glyphs") | Network page hero, strip drawer |
| node 0 | `"This device"` / `"This computer"` / glyph `E977` / `hopMs=0` / healthy | node card; **hopMs 0 renders no figure** (audit N2: there is no hop to yourself) |
| node 1 | `"URnetwork"` / `"3 hops"` / glyph `E774` (globe) / `hopMs=41` / healthy | node card; wire label source |
| node 2 | `"Message server"` / `"urmsg-01.ur.io"` / glyph `E968` / `hopMs=18` / healthy | node card; wire label source |
| `World::server` | host `urmsg-01.ur.io`, jurisdiction `Iceland`, `latencyMs=59`, `keyVerified=true` | MESSAGE SERVER group; strip host + lock |
| `World::myDevices` | 3 devices, `[0].isThisComputer == true`: "Windows desktop" (online, "now"), "Pixel 9" (online, "2 min ago"), "Linux laptop" (offline, "3 days ago") | YOUR DEVICES group |
| `World::connectState` | `Connected` (enum also has Connecting, Offline) | strip dot + word |
| `World::currentEpoch` / `sessionMode` / `recordsPerSecond` | 4182 / `"direct"` / 12 | strip, Advanced only |

Two properties of this data shape the design and are worth stating out loud:

- **The hop timings sum to the round trip.** 41 + 18 = 59 = `server.latencyMs`. Per-hop labels on
  the two wires and a "59 ms round trip" figure under the diagram are therefore *consistent*, not
  three unrelated numbers. This is the fixture's own arithmetic; we just display it.
- **There is no mesh beyond these three nodes.** No peer list, no provider list, no locations. The
  "connected nodes" section the owner wants ("like the URnetwork VPN shows") is, with this data,
  exactly YOUR DEVICES plus the relay path. Anything richer would fabricate data, which §2 of the
  design doc and G4 forbid. The mesh *feeling* must come from motion and composition, not from
  invented inventory.

## 2. Design language for these two surfaces

The house vocabulary is settled and both surfaces stay inside it: **panes, not cards**
(MainWindow.xaml:10-16) — floor-to-ceiling columns, 1 px hairlines, square corners, no shadows,
four tonal steps (#101010 page / #151515 sheet / #1C1C1C card / #242424 cardHover). Within that,
the URnetwork identity on these two surfaces comes from:

1. **The globe.** The brand icon is the URnetwork globe (owner ruling, design §11), and the
   fixture puts it in the *middle* of the relay path (node 1, `E774`). The path diagram is the one
   place the brand's core metaphor — your traffic crosses our mesh — can be *shown* rather than
   named. Treat the diagram as the hero, not as a legend row.
2. **The chart-series greens.** `kUrGreen #87FB67` is the brand's "bytes / on" series colour
   (UrColors.h:68-80). Presence dots and the padlock already spend it; a small travelling "bytes"
   marker on the wires is the same semantics in motion. `accent #EFF7BB` stays reserved (send
   button, selection outline — never here), and `kProGold` appears nowhere (G3).
3. **The chrome voice.** Letterspaced 11/12 px Montreal strips (`CONVERSATIONS`, `MEMBERS`) are
   the app's structural voice. On these surfaces that voice also carries the *framing*:
   `DEMO MODEL: RELAY PATH` as a group header is both the mesh label and the honesty frame, in one
   piece of chrome (pattern established by the rail header, InspectRailView.cpp:410/423, and the
   audit's S3 override).
4. **Motion with a subject.** Every animation below carries a state or a direction (a packet going
   device → server; a drawer rising off the strip; a halo that breathes only when connected).
   None is decoration. All use existing UrMotion tokens; all are gated on `ShouldAnimate()`;
   exits are one step faster than entrances (UrMotion.h:14-15).

## 3. Network page

### 3.1 Page skeleton

Per the audit's N3 class-6 override, `NetworkHost` is a bare Grid with `UrPaneStyle` — no header
strip, no scroller. `MakeNetworkPage` therefore returns a two-row Grid:

```
Grid root                                   (page, #101010 from UrPaneStyle)
├─ row 0: Border  UrPaneHeaderStyle         (40 DIP, #151515, bottom hairline, 12 DIP inset)
│    └─ TextBlock UrPaneTitleStyle  "NETWORK"      + right-aligned UrPaneMetaStyle: connect word
├─ row 1: ScrollViewer  (Vertical Auto, Horizontal Disabled)
     └─ StackPanel column
         ├─ group header  "DEMO MODEL: RELAY PATH"  meta "3 nodes"   (28 DIP, UrGroupHeaderStyle)
         ├─ Border pathPanel  (#151515, bottom hairline, padding 24,28)
         │    └─ relay path diagram (§3.2) + Advanced lines (§3.4)
         ├─ group header  "MESSAGE SERVER"  meta = world.server.host
         │    └─ 4 × MakePaneKeyValueRow (34 DIP)  (§3.5)
         ├─ group header  "YOUR DEVICES"  meta = count
         │    └─ 3 × MakePaneTwoLineRow (44 DIP) + empty-state sibling  (§3.6)
```

The pane title strip also carries the connect state word (`Connected`) right-aligned in
`UrPaneMetaStyle` — the page and the strip then agree about state without the page re-explaining
it. (Word from `StatusStateWord(world.connectState)`; one owner, S1's pure function. Constraint:
this is a state word, not a claim — the same ruling S1 ships.)

### 3.2 Hero: the relay path diagram

Built entirely from `World::relayPath` — no literal on the diagram is invented in the view (N3's
own rule, kept). One measurable 680 DIP row: `node – wire – node – wire – node`
(3 × 168 min-width + 2 × (72 + 16) wire spans), inside its own horizontal ScrollViewer so a
dragged-narrow window scrolls the diagram rather than crushing it (design §6.5a).

**Node card** — square-cornered Border (the pane vocabulary; N3's reasoning about mixed radius is
correct and stays): fill `kCard #1C1C1C`, 1 px `kBorder #1FFFFFFF`, padding 16,12, min-width 168.
Inside, centred: 20 px Segoe glyph (colour `textMuted #989898` when healthy, `kDanger #F8523B`
when not — colour is never alone: the automation name carries ", not healthy"), then
`UrRowTitleStyle` label, then `UrRowNoteStyle` sub-label. No in-card third line (see §3.4 — the
hop figure moves onto the wire).

**Wire** — 72 × 2 DIP rounded-end Rectangle, fill `UrBorderStrongBrush #38FFFFFF` (the app's
"hairline you are meant to see", App.xaml:262), 8 DIP clear space each side. Static in Normal
mode, identical left and right.

**Direction cue (new).** Each wire gets a small right-pointing chevron at its trailing end — 8 px,
`textFaint #5A5A5A`, static in both modes. Without it the row reads as three boxes and two rules;
with it the row reads as a *path with a direction*, which is the whole story the fixture tells
(this device → URnetwork → server). Glyph discipline follows the fixture's own rule
(DemoWorld.cpp:382-385 — codepoints are rendered and looked at before being written down):
candidate `E76C` (ChevronRight); if it renders hollow, fall back to a 6 × 8 `shapes::Polygon`
triangle in the same brush rather than shipping a tofu box. Constraint check: a chevron describes
the path's *shape*, which is fixture data (the vector order is the path order) — it asserts no
delivery and no crypto. G4 clean.

### 3.3 Advanced Mode: hop labels, round trip, travelling packet

D6 quarantines the moving wire behind Advanced Mode; design §6.6's row for this page is
"+ animated wires, per-hop timings". All three parts, redesigned from the N6 brief where the
audit found it defective:

1. **Per-hop labels move from inside the nodes onto the wires.** The N6 brief puts a third line
   inside each node card; the audit's N2 override already cuts that to two (node 0's `hopMs == 0`
   must render nothing). A latency figure belongs on the *link* it measures — that is how every
   network diagram reads, and it frees the node card from a line that is absent on exactly one of
   three cards. So: above each wire's midpoint, an 11 px `textFaint #5A5A5A` caption —
   wire A shows `FormatLatency(relayPath[1].hopMs)` → "41 ms", wire B shows
   `FormatLatency(relayPath[2].hopMs)` → "18 ms". The mapping is honest: `RelayNode::hopMs` is the
   timing of the hop *terminating* at that node (node 0's 0 ms is "no hop to yourself",
   audit N2). `Collapsed` at build; `SetNetworkPageAdvanced` flips Visibility — density only,
   never a rebuild, never a crossfade (the contract every `Set*Advanced` follows).
2. **Round trip** under the diagram, centred, 11 px `textMuted`: `FormatRoundTrip(world.server)`
   → "59 ms round trip". Worded "round trip" and centred under the *whole* diagram because
   `ServerInfo` carries one end-to-end number with no per-hop split (N6's own honesty rule, kept).
   Advanced only.
3. **The packet — replacing the N6 wire-opacity pulse.** The brief pulses both wires' opacity
   0.35↔1.0 on `kPulseMs`; a whole wire breathing reads as blinking chrome, and two wires out of
   phase read as a fault. The same motion budget spent as a *travelling marker* says the actual
   thing: bytes move device → server. Proposal:
   - One 4 × 4 DIP disc per wire, fill `kUrGreen` (the chart semantics' "bytes" colour,
     UrColors.h:69-70 — not accent, not gold, both forbidden here by G3), riding 1 DIP above the
     wire on a `TranslateTransform.X` across the 88 DIP wire span.
   - Travel duration `kEpicMs` (1000 ms) per wire, standard bezier, `RepeatBehavior Forever`;
     wire B's `beginMs = kSlowMs` (400) so the path reads as flowing rather than flashing.
   - Fade in over the first `kFastMs` (150), fade out over the last `kFastMs` (beginMs 850) — the
     packet materialises out of the node and dissolves into the next rather than popping at the
     ends. All four tracks are `MakeSplineDouble`; no new duration, no new curve.
   - Built **stopped** at page build; started only by `SetRelayPathAnimated(true)` and only when
     `advanced && ShouldAnimate()`; started from the grid's `Loaded` handler if the tree is not up
     yet (N6 step 2's Loaded pattern is right — keep it). Stop writes Opacity back explicitly.
   - Stretch, only if it lands cleanly: on each travel's completion, the destination node's glyph
     does a single opacity 0.6 → 1.0 `kFastMs` blip — an "arrived" tick. Cut without ceremony if
     it flickers; the travel alone carries the idea.
   - *Fallback for the coordinator:* if the packet proves flaky, N6's opacity pulse is the
     pre-audited plan-B (same quarantine, same gate, same tokens). Do not ship both.
4. **Honesty of the moving diagram.** D6's logic: on a diagram an audience reads a diagram. The
   group header already frames it — `DEMO MODEL: RELAY PATH` (the audit's S3 string, reused on
   the page so the drawer and the page speak one sentence). The packet is Advanced-only, and
   `--demo-advanced` is session-only (AdvancedMode.cpp:25-36), so the animated path never appears
   in a default capture.

### 3.4 The framing header (G4)

- Relay group header: **`DEMO MODEL: RELAY PATH`**, meta "3 nodes". Prefix-first framing in the
  chrome voice, always visible in both modes — the page's single always-on frame, the analogue of
  the rail's `Demo model: end-to-end encrypted` header (InspectRailView.cpp:410). It cannot be
  cropped away from the diagram it frames because it sits directly on top of it.
- **`Server key` row:** value is the audit-corrected `FormatKeyState` — `Demo model: verified` /
  `Demo model: not verified` (network.md's bare "Verified" in green was the audited defect; the
  correction matches `AttestationLabel`, InspectRailFields.cpp:113, character for character). Per
  the audit's N4 override the colour rule stays (`kUrGreen` / `kDanger`) since the words carry
  the meaning; colour is the redundant channel. Coordinator's option, from the handoff's reviewer
  note (the green padlock is the app's one unframed positive claim): painting this row's value
  `textMuted` instead of green costs nothing and the audit's wording still holds. Default: follow
  the audit.
- The bare strings `Verified` / `Not verified`, and `XChaCha20-Poly1305`
  (`MessageInspect::cipher`), render nowhere on this page under any label — the A6/N2/N4
  overrides are restated here so this page never becomes a second home for them.
- The `MESSAGE SERVER` group's other rows (Host, Jurisdiction, Latency) are plain facts and carry
  no frame, matching the rail's MESSAGE section.

### 3.5 MESSAGE SERVER group

Four `MakePaneKeyValueRow` rows (fixed 34 DIP, bottom hairline, 12 DIP inset) under one
`MakePaneGroupHeader("MESSAGE SERVER", world.server.host)`:

| Key | Value | Source |
|---|---|---|
| Host | `urmsg-01.ur.io` | `server.host` |
| Jurisdiction | `Iceland` | `server.jurisdiction` |
| Latency | `59 ms` | `FormatLatency(server.latencyMs)` — the same function `net fmt latency` asserts |
| Server key | `Demo model: verified` (green) | corrected `FormatKeyState(server.keyVerified)` |

Jurisdiction is a genuinely differentiating fact (the world builds conversations about it —
conv-yuki's rows, DemoWorld.cpp:362-367) and costs one row. No lock glyph here: the strip owns
the lock, and one claim per surface is enough.

### 3.6 YOUR DEVICES — the connected-nodes section

This *is* the connected-clients view the data supports (§1): the account's own devices, presence,
last-seen, and a remove that removes (design §9.1: nothing looks live and does nothing).

- One `MakePaneGroupHeader("YOUR DEVICES", count)`; rows are `MakePaneTwoLineRow` (44 DIP):
  title = `device.name`, note = `FormatDeviceMeta(device)` → per the audit's N5 correction,
  "This computer · Online" / "Online" / "Last seen 3 days ago".
- Trailing, in order: 8 DIP presence dot (`kUrGreen` online / `textFaint` offline — two green,
  one faint in this world; never colour alone, the note line says the same words), then the
  remove button (`E74D` Delete, 14 px, muted, 28 × 24 `UrPaneActionButtonStyle`, automation name
  "Remove <device name>").
- **Hover-reveal the remove (new).** The wastebasket rests at Opacity 0 and appears on row hover
  or keyboard focus over `kFastMs`, standard bezier; it stays in the tab order and in the UIA
  tree at all times (a hover-only control must never vanish from a screen reader — opacity, not
  Visibility, and the automation name never changes). The list stays calm, the affordance is one
  hover away. Remove semantics stay view-local per N5 (the world is immutable; relaunch restores
  all three — right for a re-opened demo), the group count ticks down, and the empty state is the
  sibling `MakePaneEmptyLine("No devices are linked to this account.")`.
- No per-device identicons: all three `ownerKey`s are the same person's seed, so three identical
  pixel tiles would be noise, and a device row is not a person row. The identicon stays the
  identity system's signature; devices keep the presence dot.

### 3.7 Entrance choreography

On the page's first show only (the page is built once and cached), the three top-level sections —
path panel, server group, devices group — rise `kDist8` (8 DIP) and fade in, staggered by
`kStaggerMs` (40 ms), 3 steps (≤ `kMaxStaggerSteps`), each `kBaseMs` (250) on the standard
bezier. This composes with the shell's `CrossfadePageSwap` rather than fighting it: the crossfade
owns the page root's opacity, the stagger owns the sections' rise — the two never animate the
same property on the same element. Gated on `ShouldAnimate()`; with motion off the page is simply
there, fully formed. Re-navigation does not re-run it (run-once flag on `PageParts`), or every
nav click becomes a light show.

### 3.8 Reduce-motion and width behaviour

- `ShouldAnimate() == false`: diagram static, no packet, no stagger; hop labels and round trip
  still appear under Advanced (they are density, not motion). **Measured warning:** the
  reduce-motion branch has never executed on this machine (`SPI_GETCLIENTAREAANIMATION = 1`,
  handoff §4), and `urnw::motion::SetMotionOverride` — which the audit's A2 override tells A2 to
  call — does not exist in `UrMotion.h` (verified by grep today). Whoever lands the first motion
  task on these surfaces must either add that test hook (a tri-state override read by
  `ShouldAnimate()`, UI-thread only) or hand-verify with Windows' Animation effects off. Do not
  stack more motion onto an unverified gate.
- Narrow windows: the diagram's own horizontal scroller takes the overflow; group headers and
  rows stretch to pane width because the page scroller is horizontally disabled (N3's shape,
  kept). Below 560 DIP content height the *strip* disappears but the page is unaffected.

---

## 4. Status strip

The owner's ruling stands and this design honours it: a **connect indicator, not a network
readout** (design D4). One 26 DIP row of window chrome; everything else lives in the drawer or
on the Network page.

### 4.1 Anatomy (Normal density)

26 DIP `UrStatusStripStyle` Border (#151515, 1 px hairline along its **top** edge only), full
window width, spanning under the nav pane as well as the destination — window chrome, not page
content. Height 26 and Padding 0 are local overrides of the style's `16,7` padding, per S2's
measured note (the style's padding needs ~30 DIP and would clip). The whole strip is one Button
on `UrPaneRowButtonStyle` (hover fill, focus visual, automation peer, Enter/Space invocation —
S2's shape, kept). Left to right, first mark 16 DIP from the window edge:

```
[20×20 dot host: 8 DIP dot]  Connected  │  server urmsg-01.ur.io  │  🔒(E72E, muted)
```

Four marks, two hairlines, nothing else. The right half stays **empty** — jurisdiction, latency,
hop timings and node detail are the page's and the drawer's (S2's division, kept; adding a
right-aligned readout is how a connect indicator creeps into a network readout).

- Dot host is 20 × 20 so the halo ring (8 → 16 DIP peak) keeps 2 DIP clear of the word
  (S2's arithmetic, kept). The state field takes no left margin; the host's slack supplies the
  kit's 6 DIP dot-to-value gap.
- Lock glyph: `E72E` closed when `keyVerified`, `E785` open when not — shape changes between
  states, so the fact never rides on colour (S1's pair). Foreground **muted** when verified,
  `kDanger` when not — deliberately not green: the handoff's reviewer note names the green
  padlock as the one unframed positive claim in the app, and the strip is the worst place to add
  a second one. Automation name is prefix-first per the audit's S2 override:
  "Demo model: server key verified" / "Demo model: server key not verified".
- The word "Connected" ships per S1's standing ruling (the strip is demo-only; the word is a
  state word, not a crypto claim). Colour is never alone: the word sits 6 DIP from the dot.

### 4.2 States and motion

`StatusStateWord` and `StatusStateColor` cover all three enum values even though the fixture
seeds only `Connected` — the words are S1's asserted contract:

| State | Dot | Word | Motion (all `ShouldAnimate()`-gated) |
|---|---|---|---|
| Connected | `kUrGreen #87FB67` | Connected | Halo ring: 8 DIP circle scaling 1.0 → 2.0 while fading 0.55 → 0, `kPulseMs` (1500), standard bezier, forever. Design §7's connect-dot row. |
| Connecting | `kStatusConnecting #E6EA23` (the ramp's yellow, **not** pale accent) | Connecting | The dot itself pulses opacity 0.35 ↔ 1.0, `kPulseMs`, AutoReverse — a "working" beat, visibly distinct from the connected halo. |
| Offline | `textFaint #5A5A5A` | Offline | **None.** A pulsing offline dot says the app is doing something, which is the one thing it is not (S2's note). |

Motion is gone, not reduced, when Windows says so: all three states then render as a plain
8 DIP dot and a word. The pulse starts once at build (the demo's connect state is seeded data
nothing mutates — S2's reasoning) and there is no state-change restart path to design.

### 4.3 Advanced density

Per design §6.6 row 1, appended **after** the padlock so the Normal marks never move: separator +
field × 3 — `epoch 4182`, `session direct`, `rec/s 12` — built once, tagged `ur.status.advanced`,
Collapsed at build, shown/hidden by `SetStatusStripAdvanced` as a pure Visibility walk (S4's
shape, kept). Captions 11 px faint, values 12 px muted — quieter than the state word; the strip
never competes with the page. Six fields + five rules ≈ 590 DIP at 1560 — no wrap, still one
26 DIP row. `--demo-advanced` is session-only; `app_prefs.json` gains no key (AdvancedMode.cpp's
contract).

### 4.4 The preview drawer

Raised by activating the strip (the strip is a toggle; drawer's own Visibility is the state —
S3's shape, kept). Hosted in `StatusDrawerHost` (Grid.Row 1, bottom-left aligned, declared after
the NavigationView so it draws over the destination), standing on the strip's top edge, left edge
at the strip's 16 DIP content inset (audit S3: aligned with the dot's 20 DIP host, not the dot
itself). A plain Border in the window tree — never a Popup or Flyout, which PrintWindow does not
composite (S3's measured reason, kept). No click-outside or Escape dismissal: the strip is the
toggle, the drawer is chrome not modal, and `ApplyBreakpoint` closes it whenever the strip
collapses — the one case where the toggle would otherwise be unreachable.

Width 320 (the app's established column width, S3's pick). Fill `kSheet #151515` over the
#101010 destination; 1 px hairline on left/top/right, none on the bottom (the strip's own top
hairline is the fourth edge). Contents, top to bottom:

1. Group header **`DEMO MODEL: RELAY PATH`**, meta "3" — the audit's S3 override string, the
   same framing the page's hero wears (§3.4). Drawer automation name: "Demo model: relay path
   preview" (same override).
2. **Mini-path (new, the drawer's one flourish):** a 40 DIP strip holding a centred horizontal
   miniature of the hero diagram — three 12 px glyphs (`E977` / `E774` / `E968`, from the same
   `RelayNode`s) joined by two 20 × 1 rules in `UrBorderStrongBrush`. Same data, no new strings,
   and the drawer stops being three plain rows: the mesh metaphor is visible from every screen
   the strip is on, not just the Network page. It is header furniture, not a row, so the
   "one row species per pane" rule (UrComponents.h:300-304) is untouched.
3. Three `MakePaneListRow` rows (36 DIP): health dot (`kUrGreen` healthy / `textFaint` not),
   title = `node.label`, meta = `node.subLabel` (+ "  offline" when unhealthy — the state is in
   words, since the kit marks its dot Raw on that premise). Row automation names carry the full
   fact. Last row's bottom hairline removed (S3's double-rule fix).

Height ≈ 1 + 28 + 40 + 3 × 36 = **177 DIP** (the brief's 137 + the 40 DIP mini-path).

**Drawer motion.** Open: `CrossfadePageSwap(nullptr, drawer)` (the documented no-outgoing case,
kBaseMs fade) plus the mini-path's two rules drawing in left → right at `kFastMs` each,
`kStaggerMs` apart — the path *assembles* as the drawer appears. Close: `kFastMs` fade on the
exit bezier (one step faster than entrance, UrMotion's rule), collapse on completion with the
opacity-restored guard S3 specifies. `ShouldAnimate() == false`: instant swap both ways, all
content present.

### 4.5 Collapse and gating (settled shell behaviour, restated so the design can't drift)

- Built **only under `--demo`** (`BuildStatusStrip` early-returns; a build with no protocol must
  not grow chrome reading "Connected | server urmsg-01.ur.io" on a plain launch — design §2/§8).
- Visible only when content height ≥ 560 DIP: `LayoutFor().strip` owns the rule
  (DemoShellState.h:41), `ApplyBreakpoint` is the single writer
  (MainWindow.xaml.cpp:713-717), and the log line `window: layout wide=.. rail=.. strip=..` is
  the gate (audit S2). `kStatusStripHeightDip = 26` lives in `StatusStripRules.h`; the 560
  threshold is **not** re-declared there (audit S1).
- `StatusStripHost` (MainWindow.xaml:278) and RevealRoot's third Auto row already exist — no
  XAML row surgery (audit S2 class 2). `StatusDrawerHost` is genuinely new markup (S3 owns it).

---

## 5. Ownership ruling for the S/W cluster (decision the coordinator must make)

The audit's own overrides conflict: its S2 class-8 override says W6 mounts the strip as `strip_`
from `BuildDemoViews()`; its W6 class-2 override says strip.md ships the member as `statusStrip_`
plus `ToggleStatusDrawer`, and W6 deletes its Steps 2 and 4. Both cannot land. Recommendation —
**surfaces own their surfaces, wiring owns orchestration only:**

1. **S2 owns** `Views/StatusStripView.{h,cpp}`, `MakeStatusStrip`, the MainWindow member
   **`statusStrip_`** (not `strip_` — the name the later, W6-side audit overrides use), and
   `MainWindow::BuildStatusStrip()` (demo-gated), called from `BuildDemoViews()`. W6 deletes its
   strip-mount steps entirely.
2. **S3 owns** `MakeStatusDrawer`, `v.drawer`, `SetStatusStripDrawerOpen`, the `StatusDrawerHost`
   markup and mount, and `MainWindow::ToggleStatusDrawer()` with drawer Visibility as the one
   state. W6 declares neither `ToggleStatusDrawer` nor `drawerOpen_`.
3. **W7 owns the single `OnAdvancedModeChanged` subscriber**, named `ApplyAdvanced(bool)`
   (wiring.md:563 — pick this name and strike A4's `ApplyAdvancedMode`), registered once in
   `EnterDemoMode()` after `BuildDemoViews()`. N6 and S4 each add exactly one line to it
   (`SetNetworkPageAdvanced(network_, on)` / `SetStatusStripAdvanced(statusStrip_, on)`) and
   register nothing themselves (audit N6 / S4 overrides).
4. W6 keeps only what no surface task owns: build-order assembly in `BuildDemoViews()` and the
   `if (view.root)` null-check + `LogWarn` discipline on each mount — including mounting the
   Network page into `NetworkHost()` per the audit's N3 override (`NetworkBody` does not exist).
5. When the Network page lands, `ShowDestination`'s `network` arm (MainWindow.xaml.cpp:739-744)
   flips `incoming` from `StubPage()` to `NetworkHost()` — one line, and the stub route the
   handoff §9.1 installed for the unbuilt destinations stays for settings/developer until their
   groups land.

## 6. Gates and assertions (pure, `--diagnose`)

All pure C++, no winrt, no `Localized()`, per the purity rules the existing lines follow:

- **Fix, per audit N2:** `FormatKeyState` returns the prefix-first pair and `net fmt keystate`
  asserts them; `net relay hops` asserts `size()==3` plus per-node `hopMs < 0` (not `<= 0`),
  glyph and label non-empty, printing the corrected query; `net fmt rtt` compares against a
  locally built `ServerInfo{latencyMs=7}` probe, never a restated literal (fixture untouched —
  I10).
- **Keep, per audit S1:** the single `strip fields` line (three state words, lock `e72e`/`e785`,
  epoch/rec formats). No `strip collapse` line — the shell's `stripEdge` term at Startup.cpp:359
  already owns the 560 boundary.
- **New, cheap:** in `CollectNetworkDiagnostics`, assert the page's relay group-header literal
  and the drawer's header literal both begin `L"Demo model:"` (same constant the views render),
  so the framing cannot be dropped silently — the same reason A6's dump gate asserts its first
  line. One line, printed with the compared values.
- **Motion is log-gated, not pixel-gated** (N6's honest admission, extended): on start/stop the
  packet logs `network: relay motion -> started|stopped (advanced, shouldAnimate)`; drawer
  open/close logs `window: status drawer -> open|closed`. A still can never prove motion.
- Every new gate must be demonstrated failing against a deliberate break, then reverted, with
  both outputs pasted (handoff §7's standing rule).

## 7. What code must change (file-level)

**New files (6):** `app/src/App/Views/NetworkPageView.{h,cpp}` (page + pure formatters), `Views/
StatusStripView.{h,cpp}` (strip + drawer), `Views/StatusStripRules.{h,cpp}` (pure rules; no
winrt include). `App.vcxproj` gains six entries — tracked-file delta **96 → 102**; check the
delta, never the absolute (handoff §2's index rule).

**Modified:**

| File | Change |
|---|---|
| `MainWindow.xaml` | add `<Grid x:Name="StatusDrawerHost" Grid.Row="1" VerticalAlignment="Bottom" HorizontalAlignment="Left" />` after `</muxc:NavigationView>`; fix the stale `StatusStripHost` comment per the audit's S3 override. Nothing else — hosts and rows exist. |
| `MainWindow.xaml.h` | `#include "Views/StatusStripView.h"` + `Views/NetworkPageView.h`; members `statusStrip_`, `network_`; declarations `BuildStatusStrip()`, `ToggleStatusDrawer()`, `BuildNetworkPage()`. No `demo_`, no `ApplyAdvancedMode` duplicate. |
| `MainWindow.xaml.cpp` | `BuildStatusStrip()` (demo-gated) + drawer mount + strip Click wiring inside `BuildDemoViews()`; `BuildNetworkPage()` mounting into `NetworkHost()`; `ShowDestination` network arm flips to `NetworkHost()`; one `Set*Advanced` line each inside W7's `ApplyAdvanced`. `ApplyBreakpoint` is **not** touched (strip visibility already correct at :713-717). |
| `main.cpp` | push `CollectNetworkDiagnostics()` lines inside the existing `if (diagnose)` block (N2's placement — never `CollectDiagnostics()`, which runs on every launch). |
| `Startup.cpp` | one `strip fields` line into `CollectDiagnostics()` (pure, per S1). |
| `UrMotion.{h,cpp}` | **optional but recommended:** `SetMotionOverride` test hook (see §3.8 — the audit references it and it does not exist). |
| `App.xaml` | **no new keys.** Every style/brush this design spends already exists (`UrPaneHeaderStyle`, `UrPaneTitleStyle`, `UrPaneMetaStyle`, `UrGroupHeaderStyle`, `UrStatusStripStyle`, `UrPaneRowButtonStyle`, `UrRowTitleStyle`, `UrRowNoteStyle`, `UrRowIconStyle`, `UrBorderStrongBrush`, `UrPaneActionButtonStyle`). The packet disc, wire labels, chevrons and mini-path are code-built from existing brushes — per G3's "extend what's there; never add a key silently." |
| `Demo/DemoWorld.cpp` | **none. Ever.** Immutable, I10-fingerprinted. |

**No component-kit changes.** `MakeStatusField`, `MakeStatusSeparator`, `MakePaneGroupHeader`,
`MakePaneKeyValueRow`, `MakePaneTwoLineRow`, `MakePaneListRow`, `MakePaneEmptyLine`,
`SetTextOrCollapse` all exist with the parts this design uses (`UrComponents.h:276-479`).

## 8. Payoff vs risk — recommended sequence

1. **S1 + S2 (strip Normal density + demo gate + collapse).** Visible on every demo screen; zero
   animation risk (halo is the only motion and it is design-§7-sanctioned); kills the "is this
   thing on?" question a dark window otherwise asks. Highest payoff per hour.
2. **N2 + honesty fixes (FormatKeyState prefix-first, gate corrections).** Gate-critical,
   zero visual risk; everything after this inherits the corrected strings.
3. **N3 static page + N4 server group + N5 devices (with hover-reveal remove), and the
   `ShowDestination` one-line flip.** Removes the live blank-pane hazard, lands the brand hero in
   its static form, and every pixel is screenshot-verifiable. Medium effort, no motion risk.
4. **S3 drawer + mini-path.** The strip's payoff moment, reachable from every screen; new host +
   UIA tool make it medium risk; verify-drawer.ps1 already specified by the brief.
5. **S4 + N6-static-half (Advanced fields; hop labels + round trip).** Pure Visibility flips on
   already-built elements; lowest risk in the list; do before any packet work.
6. **N6 packet travel (and §3.7 entrance stagger).** Highest wow, highest risk: motion can only
   be log-gated and hand-watched, and the reduce-motion path is unverified on this machine
   (§3.8). Land last, behind Advanced Mode, with the N6 opacity pulse held as plan-B.
7. **Reduce-motion verification hook (`SetMotionOverride`)** — fold into whichever of 5–6 lands
   first; it is the prerequisite for trusting anything animated the demo shows.

*Ambiguity noted for the coordinator:* the audit's conflicting member-name overrides
(`strip_` vs `statusStrip_`) are resolved by §5's ruling; whichever side the coordinator
dispatches first must be edited to match, since `brief.py` never sees this document.
