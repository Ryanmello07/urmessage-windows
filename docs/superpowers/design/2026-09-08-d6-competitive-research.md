# D6 — Competitive research: Signal, Telegram, Element/Matrix, Session, SimpleX, URnetwork

Written 2026-09-08. Assignment: distil the 2024-2026 design language of the privacy-messenger
reference class into a pattern library that translates into **our** token set, honestly, inside
every constraint in `HANDOFF-2026-09-08-ui-design-swarm.md` §4. Research and design only — no
file under `app/src` was modified.

Method: baseline captures read (`.verify-baseline/inspect.png`, `chats.png`, `thread.png`,
`network-stub.png`); code read (`app/src/App/UrColors.h`, `UrMotion.h`, `App.xaml`,
`MainWindow.xaml`, `Views/ThreadView.cpp`, `Views/ConversationListView.cpp`,
`Views/InspectRailView.cpp`, `UrComponents.h`); plan/spec read (design doc D1–D8, §6.4/§6.5,
`plans/tasks/network.md`, `plans/tasks/strip.md`, the remaining-task audit); web sources as cited
inline per section.

---

## 1. The reference class at a glance

| App | Dark surface ladder | Outgoing bubble | Delivery indicator | Depth device | Motion voice |
|---|---|---|---|---|---|
| Signal Desktop | 5-step neutral ramp `#121212/#1b1b1b/#262626/#2e2e2e/#343434` ([_variables.scss](https://raw.githubusercontent.com/signalapp/Signal-Desktop/main/stylesheets/_variables.scss)) | Brand ultramarine `#2c6bed` fill | 1 ringed check = sent, 2 = delivered, 2 filled = read ([Android Police](https://www.androidpolice.com/google-messages-new-delivery-icons/)) | Tonal steps + hairlines; selection is a **fill** (`#4a4a4a` dark) | `cubic-bezier(0.19,1,0.22,1)` hard ease-out — our kStandard's cousin |
| Telegram Desktop | Dark navy ramp + wallpaper patterns | Accent/green fill with **tail**, soft shadow | 1 check sent, 2 = read (no delivered state); grey→accent colour flip | Shadows, pattern, glassy stickers | Bouncy springs, big staggers |
| Element X / Compound | Token system, light/dark/HC modes, semantic naming ([compound-design-tokens](https://github.com/element-hq/compound-design-tokens)) | Neutral, bubbles de-emphasised | Shield badges per message (trust state) | Flat, hairlines, sheets | "Invisible Encryption" — animation hides mechanism ([Element blog](https://element.io/blog/deep-dive-into-element-x/)) |
| Session | Near-black + single saturated green accent ([getsession.org](https://getsession.org/)) | Green fill | Delivered/read ticks | Flat | Minimal |
| SimpleX | Dense utilitarian dark | Neutral | Technical: per-connection server/queue rows | Flat | Minimal; terminal-adjacent |
| URnetwork (own product) | `#101010` system, pale-yellow accent, chart-series greens/pinks ([ur.io](https://ur.io/)) | n/a (VPN) | Connect state dot (green/yellow/blue ramp — see `UrColors.h:60-66`) | Tonal + the ConnectCanvas blob | Spring physics, idle pulse (`UrMotion.h:37-41`) |

The owner's target — *"slightly better than Signal, not as insane as SimpleX, kinda like Matrix
but better"* — maps to: Signal's restraint and check grammar, Element's inspector-rail
information architecture, a discipline Signal/Telegram both use and SimpleX/Session mostly lack
(tonal depth + one display voice), and URnetwork's own mesh identity on the two unbuilt surfaces.

---

## 2. Per-app findings

### 2.1 Signal Desktop — the bar to beat

- **Palette structure is identical to ours in kind.** Signal's dark theme is a pure neutral ramp —
  gray-95 `#121212` ground, gray-90 `#1b1b1b`, gray-85 `#262626`, gray-80 `#2e2e2e` — with white
  alphas (`rgba(255,255,255,0.06…0.9)`) for hairlines and state layers
  ([source](https://raw.githubusercontent.com/signalapp/Signal-Desktop/main/stylesheets/_variables.scss)).
  That is exactly our `page #101010 → sheet #151515 → card #1C1C1C → cardHover #242424 →
  cardPressed #2A2A2A` + `border #1FFFFFFF` (`UrColors.h:14-30`). We are already on the right
  ladder; we are just not **climbing** it — the handoff's gap #1.
- **Delivery grammar** is the industry standard ours already matches: count (1 vs 2 checks),
  fill (ring vs disc), plus colour as a *restatement*. Google Messages RCS copied this verbatim
  ([Android Police](https://www.androidpolice.com/google-messages-new-delivery-icons/)). Our
  three-channel rule (count, shape, word — `ThreadView.cpp:155-171`) is strictly stronger than
  Signal's, which at 13px is also glyph-size-critical (their bare checks blur together; that is
  why the handoff pins `E930`/`EC61`).
- **Motion curve** `$ease-out-expo: cubic-bezier(0.19, 1, 0.22, 1)` is a hard settle, functionally
  our `kStandardP1(0.10,0.90) → kStandardP2(0.20,1.00)` (`UrMotion.h:53-54`). Signal feels "fast"
  because entrances are short and exits are shorter — our exits-one-step-faster rule is the same
  instinct, already codified.
- **Selection is a fill change** (`$color-selected-message-background-dark: #4a4a4a`). We cannot
  copy this: our bubble fill carries direction (`ThreadView.cpp:329-333`), so our 2px accent
  outline is forced and correct. Signal's approach would erase the incoming/outgoing distinction.
- Review consensus ([createbytes](https://createbytes.com/insights/signal-ui-ux-review-is-it-just-a-trend)):
  Signal reads as trustworthy because it is *quiet* — no gradients, no decoration, iconography
  "universally understood". Quiet is the privacy aesthetic (§3).

### 2.2 Telegram Desktop — the ceiling of polish, mostly off-limits

- Telegram's outgoing bubble is a **tail + accent-family fill + soft drop shadow**, and the read
  state is a **colour flip** of the check pair (grey → accent blue; per the convention table at
  [meanzspot](https://meanzspot.com/what-does-two-check-marks-mean-in-texts-me/)). Both devices are
  banned here: accent `#EFF7BB` is reserved (send button + selection outline only), and
  colour-alone state violates the three-channel rule.
- What *is* worth copying: **run-shaping**. Telegram/iMessage tighten consecutive same-sender
  bubbles (small corner radius on adjoining corners, 2px intra-run gaps). It costs no colour and
  no claim; it is pure geometry. Ours are uniform-radius slabs (handoff gap #5).
- Telegram's wallpaper/pattern layer is identity for them and noise for us. Skip.
- Motion: Telegram overshoots (springs with visible bounce). Our `kHoverScale 1.03` /
  `kPressScale 0.97` tokens (`UrMotion.h:76-77`) are the most bounce this brand should ever show.

### 2.3 Element / Matrix — information architecture and restraint

- The **inspector rail pattern itself is Element's** — the design doc (D3) already credits it:
  select a message, the right column swaps to its detail. Element X pairs this with "Invisible
  Encryption" — the mechanism is hidden, trust states surface only when action is needed
  ([Element X deep dive](https://element.io/blog/deep-dive-into-element-x/)).
- **Compound** ([github.com/element-hq/compound-design-tokens](https://github.com/element-hq/compound-design-tokens))
  is the proof that a semantic-token ladder (canvas/subtle/primary surfaces, per-mode re-pointing)
  scales across platforms. We already have the ladder as raw values; the lesson is *assign each
  rung a job* and never let two surfaces share a rung without a reason (§4, P1).
- Element's per-message shield badges are a colour-carried trust claim — for us they would be a
  G4 violation waiting to happen. Our framed `Demo model:` strings do the same communicative work
  honestly.
- What not to copy: classic Element Web's density and settings sprawl — the very thing the owner
  means by "Matrix but better".

### 2.4 Session — accent discipline and the "people-powered network" voice

- Session is near-black with **one** saturated green accent doing all the signalling
  ([getsession.org](https://getsession.org/)). That is the discipline our brand demands of
  `#EFF7BB` — except our chromatic budget is even tighter (green is reserved for
  presence/padlock, `UrColors.h:68-80`).
- Session's marketing voice — *"People Powered… thousands of nodes run by a global community"* —
  is nearly word-for-word URnetwork's *"A Network Built by the People… a mesh of community nodes.
  No single node sees the full path"* ([ur.io](https://ur.io/)). **The mesh is the shared visual
  identity**: nodes + wires is what our Network page (design §6.4) and status strip drawer exist
  to draw. Session shows an onion-path indicator; our relay path diagram is the same idea with
  URnetwork's three fixed nodes (`this device → URnetwork → message server`).

### 2.5 SimpleX — the "insane" end of technical disclosure

- SimpleX surfaces per-connection servers, queues and routing state in primary UI, and defaulted
  private message routing from v6.0 ([simplex-chat repo](https://github.com/simplex-chat/simplex-chat)).
  It is honest and legible to engineers and unreadable to everyone else — precisely the
  "as insane as SimpleX" pole the owner placed above our target.
- The usable kernel: **progressive disclosure of the technical layer**. Our Advanced Mode
  (design §6.6: group-id chips in the list, epoch/session/records-per-second in the strip,
  per-hop timings on the relay wires, raw ids in the rail) is exactly SimpleX's transparency with
  Element's default calm. Keep that split sacred; never let an Advanced field leak into Normal.

### 2.6 URnetwork's own product — brand cues to echo

From [ur.io](https://ur.io/) and the ConnectCanvas lineage recorded in `UrMotion.h:34-41` and
`UrColors.h:59-80`:

- **Live counters** ("68,452 providers / 101 countries / 1,005,046 networks") — big condensed
  numerals over muted captions. The app already owns this voice: `UrStatValueStyle` (ABC Gravity
  Extra Condensed 26px) + `UrStatLabelStyle` + `UrStatTileStyle` exist at `App.xaml:547-569`,
  documented as "the two densest surfaces… the owner has said work". This is the cheapest,
  most on-brand way to make the Network page feel like URnetwork rather than like a settings app.
- **Connect-state dot semantics**: connected green / connecting Yellow400 `#E6EA23` / idle
  Blue500 `#2A60FF` (`UrColors.h:60-66`) — two of these three tokens are *defined but unspent*.
  The status strip is their designated home.
- **"Privacy by architecture, not promise"** — the brand line is itself a G4-shaped sentence:
  describe the architecture (nodes, wires, timings), never assert the outcome. Our prefix-first
  framing strings are the same move at string scale. The design should treat `Demo model:` not as
  a disclaimer to hide but as *house style* — the terminal-annotation voice of an app that shows
  its working.

---

## 3. What "looking private" consists of (synthesis)

Across all five apps, the ones that *read* as secure share four properties — none of which is a
padlock sticker:

1. **Near-black, never pure black.** Material 3's dark-theme guidance sets `#121212` as the floor
   because `#000000` produces halation and kills elevation headroom
   ([dark-mode best-practice summary](https://tijocreative.com/articles/dark-mode-ui-design-best-practices-pitfalls-real-examples));
   Signal sits at `#121212`. We sit at `#101010` — slightly darker, deliberately (the identicons
   need the contrast). Valid, but it makes the tonal ladder our *only* depth device (P1).
2. **Elevation by lightness, not shadow.** M3 tonal elevation: in dark mode shadows vanish, so
   raised surfaces are *lighter* ([same source](https://tijocreative.com/articles/dark-mode-ui-design-best-practices-pitfalls-real-examples)).
   Our four surface tokens are already ordered this way. Drop shadows and ThemeShadows would also
   fight the deliberately backdrop-free window (handoff §2 — PrintWindow verification depends on
   no system backdrop). Tonal only.
3. **One chromatic signal at a time.** Session's single green; Signal's single ultramarine; our
   single `#EFF7BB`. The apps that look advanced are the ones where colour is rare enough to mean
   something. Every pattern below spends chroma reluctantly.
4. **Visible mechanism, framed claims.** Element hides mechanism; SimpleX drowns you in it; the
   middle — a diagram of the architecture with every operational claim framed (`Demo model:`) —
   is unoccupied territory, and it is exactly where URnetwork's brand line ("no single node sees
   the full path") puts us. The relay diagram is the privacy aesthetic, drawn.

---

## 4. The pattern library

Each entry: source, the pattern, translation into our tokens with exact numbers, the constraint
it touches, and the code that would have to change. All durations are `UrMotion.h` tokens; all
colours are `UrColors.h` tokens; "gap #n" references the handoff §5 numbering.

### P1 — The tonal ladder, assigned (Signal, M3). *Handoff gap #1: depth.*

**Pattern.** Every surface rung has one job; adjacency, not outlines, separates panes.

**Translation.** Assign the five rungs:

| Rung | Token | Job (the only places it may appear) |
|---|---|---|
| 0 | `page #101010` | window ground, thread field, rail field |
| 1 | `sheet #151515` | pane headers (40px strip), composer bar, status strip, drawer |
| 2 | `card #1C1C1C` | incoming bubbles, day-separator pill, stat tiles, relay node chips |
| 3 | `cardHover #242424` | outgoing bubbles, row hover, node hover |
| 4 | `cardPressed #2A2A2A` | pressed states only |

Concretely: the composer moves from its current `card` fill (`ThreadView.cpp:723`) to **`sheet`**
— the same rung as the status strip, so "chrome" (header strips, composer, strip) is one tonal
family and "content" (bubbles, cards) is another. Keep its top hairline `border #1FFFFFFF`. The
thread field stays `page`; bubbles keep `card`/`cardHover` (direction fills are load-bearing,
Spec C §5.2 / `ThreadView.cpp:329-333`). The rail's 40px `Details` header already uses
`UrPaneHeaderStyle`; verify it resolves to `sheet` like the list's.

- **Constraints touched:** G3 (no new colours — a reassignment of existing tokens; zero new
  `Ur*` keys). Honesty untouched.
- **Code changes:** `Views/ThreadView.cpp` `MakeComposer` (`bar.Background` → `SheetBrush()`);
  audit `App.xaml` `UrPaneHeaderStyle`/`UrPaneStyle` backgrounds so pane chrome is consistently
  one rung above its field. One-line changes plus a screenshot pass.

### P2 — Hover as an edge, not a fill (forced by our own bubble rule). *Gap #2.*

**Pattern.** Signal/Telegram hover = fill change. `UrBubbleButtonStyle` exists *precisely* so
bubbles do not repaint on hover (handoff §5.2), so our hover must be a **shape** event.

**Translation.** On bubble hover, fade in a 1px `kBorderStrong #38FFFFFF` edge (the token the
composer focus rule already spends, `ThreadView.cpp:679`) at `kFastMs 150` on the standard
bezier; fade out at `kMicroMs 90` on the exit bezier (exits one step faster — `UrMotion.h:15`).
Incoming bubbles have no resting edge, so hover is a genuine state change; outgoing bubbles
swap their resting `UrBorderBrush` edge for the stronger alpha. Pressed on clickable rows:
`kPressScale 0.97` at `kMicroMs` — the token exists and is unspent (`UrMotion.h:77`). Nav and
list rows keep the kit's platform-drawn hover fills (`UrPaneRowButtonStyle`), which already
spend rungs 3-4 correctly.

- **Constraints touched:** three-channel rule safe (hover is affordance, not a state channel);
  G3 safe (existing tokens); `ShouldAnimate()` gate mandatory on both fades.
- **Code changes:** `Views/ThreadView.cpp` — a `PointerEntered/Exited` pair on the bubble
  `Button` in `MakeBubbleRow` driving a small storyboard on a dedicated `Border` edge overlay
  (must NOT touch `BorderBrush` itself: `SetBubbleEdge` is the ONE writer,
  `ThreadView.cpp:125-153` — the hover edge has to be a sibling overlay, not a second writer).
  That single-writer rule is the main implementation risk; flag it in the ledger.

### P3 — Delivery cluster: keep the grammar, animate the morph (Signal grammar + D5). *Gap #6.*

**Pattern.** Signal's count/fill check grammar is the reference; design D5 already names a
"delivery-state morph" as the pedagogically valuable animation.

**Translation.** Keep all three channels exactly as gated (`ThreadView.cpp:155-218`: `E930` ring
vs `EC61` disc, count 1 vs 2, the printed word, 13px glyph + 11px `UrCaptionTextStyle`). Add one
thing: when ambient activity advances a state, crossfade the glyph set at `kFastMs 150` (old
glyphs fade out on the exit bezier at `kMicroMs 90`) rather than hard-swapping. The morph teaches
the model; the word guarantees it survives a greyscale screenshot. **Do not** add a colour step
for Read (WhatsApp's blue-check move): colour-only channels are banned, and `textMuted → text`
already restates the shape change legitimately.

- **Constraints touched:** the three-channel gate is gate-asserted — the morph must be
  *additive* (a storyboard around the existing cluster), never a restructure. New timelines need
  the `--diagnose` entrance-timeline treatment (see the `EntranceTimelines` precedent,
  `Views/ThreadLayout.h`) so motion-off renders instantly and gate-asserted.
- **Code changes:** `Views/ThreadView.cpp` `SetRowCluster`/`MakeDeliveryCluster` — build the
  replacement cluster at Opacity 0, fade the pair, remove the old. `Demo/DemoWorld.cpp` is
  immutable: the states already exist in the fixture (`Sending/Sent/Delivered/Read/Failed` all
  render under `--demo=thread --demo-autoplay`).

### P4 — Bubble run-shaping (Telegram/iMessage geometry, zero chroma). *Gap #5.*

**Pattern.** Consecutive same-sender bubbles share a run: 2px intra-run spacing (vs the 6px
`stack.Spacing` today, `ThreadView.cpp:886`), and the two adjoining corners of mid-run bubbles
drop to a small radius. First/last of a run keep full radius on the outer corners. No tails
(Telegram's identity, not ours), no fill change (G3), no asymmetry of fill — only corner
geometry.

**Translation.** The `UrBubbleButtonStyle` template root is where CornerRadius must bind
(`App.xaml:753` per `ThreadView.cpp:136-138`); add a `CornerRadius` TemplateBinding there and set
per-run values in `MakeBubbleRow`, which already knows run starts (`showSenderHeader`) and
direction (`row.outgoing`). Suggested radii: singletons unchanged; run-start outgoing
`(14,14,14,4)`, mid-run outgoing `(14,4,14,4)`, incoming mirrored — confirm the current uniform
radius in the style first and match it.

- **Constraints touched:** none of the hard ones — pure geometry inside the existing fills and
  the existing 68%/640dip cap (`BubbleMaxWidthDip`). The selection outline (2px accent) follows
  the shaped corners automatically because it is the same template root — verify on a selected
  mid-run bubble.
- **Code changes:** `App.xaml` `UrBubbleButtonStyle` (one TemplateBinding — record in the ledger
  why the existing style couldn't serve as-is); `Views/ThreadView.cpp` `MakeBubbleRow`;
  `Views/ThreadLayout.h` may need a run-position field on the row plan if `showSenderHeader`
  alone cannot express mid/end.

### P5 — The status strip: chrome, not a readout (ProtonVPN/Portmaster class, D4). *Unbuilt.*

**Pattern.** ProtonVPN's persistent bottom line — "a quiet, always-true statement of what the app
is doing right now" — is already the comment on `UrStatusStripStyle` (`App.xaml:571-586`). The
strip exists as a style and a host (`MainWindow.xaml:278`, `StatusStripHost`; 26 DIP per design
§6.5; hidden below 560 DIP of window height); only the content is unbuilt (S1–S4).

**Translation.** Left to right, one 26 DIP row on `sheet #151515` with the top hairline:

    [8px dot]  Connected   urmsg-01.ur.io · Iceland        [lock] Demo model: verified
    kUrGreen   12px muted  UrStatusFieldValueStyle         kUrGreen 14px + 11px faint

- Dot: 8px, `kUrGreen` connected / `kStatusConnecting #E6EA23` connecting / `kUrAmber #F5C242`
  idle-at-0-peers — the two unspent tokens at `UrColors.h:60-66` are this element's designated
  palette. Pulse the dot only while *connecting*, `kPulseMs 1500`, gated.
- State word: `UrStatusFieldValueStyle` (12px muted) — a **word, never the dot alone**
  (colour-never-alone; `MakeMemberRow`'s "the WORD carries the state" at
  `InspectRailView.cpp:621-624` is the house precedent).
- Server host + jurisdiction suffix: the fixture carries both (`DemoWorld.cpp:390` per the audit:
  `urmsg-01.ur.io`, `Iceland`, 59 ms, keyVerified=true).
- Lock + framed word: the green padlock is, per the handoff's own reviewer note, *"the only
  unframed positive claim on that row."* On the strip it never sits alone: pair it with
  `Demo model: verified` in 11px `textFaint` — the exact `AttestationLabel` string
  (`InspectRailFields.cpp:113`). One owner per the audit's N2/N4/A3 override: call
  `urmsg::views::FormatKeyState` after it is corrected to the framed strings; never re-type them.
  If 26 DIP gets tight at narrow widths, drop the host first, never the framing.
- Activation raises the preview drawer (design §6.5): a `card`-filled panel opening *above* the
  strip (`MainWindow.xaml:275-277` already places the drawer inside `StatusStripHost`), `kBaseMs
  250` entrance with a `kDist24` 24dip rise on the standard bezier, `kFastMs` exit. Dismiss on
  outside-tap and on the strip's own second click.

- **Constraints touched:** G4 (the framed string is mandatory, not decorative); motion tokens
  only; `ShouldAnimate()`; the collapse rule (`ShouldShowStatusStrip`, strip.md S1) is pure and
  already gate-planned. Advanced Mode adds epoch/session-mode/records-s as additional
  `UrStatusField*` pairs — the styles were designed for exactly that (`App.xaml:588-594`).
- **Code changes:** new `Views/StatusStripView.*` (planned), consuming `UrStatusStripStyle` +
  `MakeStatusField(withDot: true)` (`UrComponents.h:257-276`); `MainWindow.xaml.cpp` mounts it —
  settle the S2/W6 ownership collision (audit §6) before dispatch.

### P6 — The Network page: the mesh, drawn once, honestly (URnetwork + Session). *Unbuilt.*

**Pattern.** ur.io's hero is counters + the mesh claim; Session shows an onion path. Design
§6.4 fixes the content: relay path (3 nodes, 2 wires), message server (host, jurisdiction,
latency, key state), linked devices.

**Translation.** Top to bottom in `NetworkBody` (the host exists at `MainWindow.xaml:265`):

1. **Pane header**: `NETWORK` in `UrPaneTitleStyle`, plus right-aligned counters in the ur.io
   voice — `UrStatValueStyle` (ABC Gravity Extra Condensed 26px) over `UrStatLabelStyle` — e.g.
   relay count and device count. *Display face for display-scale numerals only*: 26px stats are
   the style's documented use (`App.xaml:547-562`).
2. **RELAY PATH** group header (`MakePaneGroupHeader`), then the diagram: three node chips —
   56×56 `card` squares, `CornerRadius 12`, 1px `border` hairline, centred Segoe glyph 24px
   (E977 Devices / E774 Globe / E968 Server per network.md) — joined by two 2px `kBorderStrong`
   wires; each node captioned label + `textFaint` sublabel. Static in Normal mode. **Advanced
   only**: per-hop `FormatLatency` captions under each wire (10px `textFaint`, e.g. "12 ms") and
   the wire animation (D6's quarantine — a dash-offset or opacity chase at `kSlowMs 400`,
   staggered `kStaggerMs 40` per wire, gated by `ShouldAnimate()` AND Advanced Mode together).
   Each node gets a 6px `kUrGreen` health dot *with the word* "healthy" in faint 10px beside the
   sublabel — `RelayNode::healthy` is a bool, and dot-with-word is the house rule.
3. **MESSAGE SERVER** group: key/value rows — Host `urmsg-01.ur.io`, Jurisdiction `Iceland`,
   Round trip `FormatLatency(59)` → "59 ms", Server key → **`Demo model: verified`** (the audit's
   N2/N4 override is binding; never bare green "Verified"). A lock glyph, if repeated here,
   carries the same framed caption as the strip.
4. **YOUR DEVICES** group: `MakeDeviceRow` already exists in the rail
   (`InspectRailView.cpp:610-635`) — dot + name + owner + last-seen word. Reuse the shape. The
   remove affordance design §6.4 mentions must be a disabled button per design §9.1 (no mutation
   path exists), the same treatment as the composer's inert controls.

- **Constraints touched:** G4 (framed strings; no cipher; no bare "Verified"); D6 (wires animate
  only under Advanced, on this page only); G3 (green = presence/health/lock restatement with
  words; accent untouched; **no `kProGold` anywhere**, including "Pro" flourishes on the stats);
  display-face rule (stats and titles only, never body).
- **Code changes:** `Views/NetworkPageView.*` (planned N2/N3) for formatters + builders;
  `MainWindow.xaml.cpp` mounts into `NetworkHost` per the audit's N1 override (the host exists;
  do not create `NetworkPage`).

### P7 — Advanced disclosure as a density, not a mode-swap (SimpleX tamed by Element). *A-group.*

**Pattern.** SimpleX shows everything always; Element hides nearly everything; we have the toggle
(design §6.6's table). The pattern to copy is **SimpleX's honesty at Element's default calm**,
and the rendering trick is that Advanced is a *density* change, not a rebuild:
`SetConversationListAdvanced` already proves the shape (chips pre-built collapsed, shown in
place — `ConversationListView.cpp:375-393`).

**Translation.** Every Advanced field follows the chip rule: pre-built, `Visibility::Collapsed`,
one setter flips them all, no re-render flash (`PopulateConversation`'s "POPULATE ONLY" note,
`InspectRailView.cpp:491-493`, is the rail-side precedent). Advanced fields render in 10-11px
`textFaint` — visibly subordinate, so Normal users never mistake chrome for content. This is also
how the rail's raw ids / hex group id / leaf index / wire size should land (design §6.6).

- **Constraints touched:** G4 — Advanced is where the temptation to dump `cipher` lives (audit
  A6); the standing ruling holds: `MessageInspect::cipher` renders nowhere, dump included.
- **Code changes:** none beyond the planned A-group tasks; this entry is a review rule for them.

### P8 — Empty and transitional states (Element/Signal). *Gap #10.*

**Pattern.** Signal's empty thread field is a centred mark + one quiet line; Element X's empty
states are a glyph, a title, one sentence. Our stub page has the honest line but a giant
mid-pane emptiness (`network-stub.png`); empty search shows nothing at all (the filter returns a
count, `ConversationListView.cpp:356-373`, and 0 rows is a void).

**Translation.**
- **Empty search**: when `ApplyConversationListFilter` returns 0, swap the list for one
  `MakePaneEmptyLine` ("No conversations match") — the kit builder exists
  (`UrComponents.h:479`); the pane-header count already says 0, which is the second channel.
  Restore on a non-empty query. `kFastMs` crossfade if `ShouldAnimate()`.
- **Stub destinations** (today's live hazard): keep the one honest line but centre it properly
  and add the destination's glyph (E839/E713/E943) at 24px `textFaint` above it — "nothing here"
  vs "failed to load" is already the XAML comment's rule (`MainWindow.xaml:246-249`); a glyph +
  line reads as deliberate, not broken. The routing fix itself is handoff §9 item 1.
- **Empty thread**: unreachable today (fixture immutable, every conversation has rows). Do not
  build speculatively; note the glyph-plus-line pattern for when the world grows one.

- **Constraints touched:** honesty (empty states *describe*, never promise); no new tokens.
- **Code changes:** `Views/ConversationListView.cpp` filter (add the empty-line swap);
  `MainWindow.xaml` StubPage (glyph + centring) — trivial, bundle with the routing fix.

### P9 — Chrome voice: section strips carry structure (Element's hierarchy, our type). *Gap #3.*

**Pattern.** The letterspaced 11px `UrGroupHeaderTextStyle` voice (`CONVERSATIONS`, `MEMBERS`) is
our Compound-style structural layer. Signal and Element both get hierarchy from weight+spacing,
not extra colours.

**Translation.** Extend the existing voice rather than adding keys (87 `Ur*` keys exist; the demo
has added one — handoff §4): day separators already spend it (`ThreadView.cpp:489-506`); the
Network page's group headers spend it (P6); the drawer's node list should spend it (P5). The
display face (ABC Gravity Extended via `UrTitleTextStyle`, already the NavigationView header
template, `MainWindow.xaml:100-108`) stays where it is: destination titles + stat numerals (P6).
**Never body copy** — the rule is the face's unreadability at 12-14px; PP Neue Montreal is the
body face everywhere.

- **Constraints touched:** G3 typography rule; resource-key frugality.
- **Code changes:** none — a "keep doing" entry with two new spend sites (P5 drawer, P6 page).

### P10 — Identicon energy, spread thinly (our own asset). *Gap #4.*

**Pattern.** The pixel identicons are the most distinctive on-screen element (handoff §5.4) and
the only saturated colour allowed off-token — already proven ≥30° hue from every reserved state
colour, so they cannot be mistaken for a state.

**Translation.** Give the rail's member rows a 20px mini-identicon beside the presence dot
(`MakeMemberRow`, `InspectRailView.cpp:579-608` — currently a bare dot + name). 20px,
`CornerRadius 4` (scaled from the 40px/8px rule `MakeIdenticon` applies). The dot stays —
colour+word rule — the identicon is identity, not state. Do **not** add identicons to thread
bubbles: the gutter identicon at run-starts is enough (`ThreadView.cpp:396-404`), and identity
belongs to people, not messages.

- **Constraints touched:** G3 — identicon saturation is pre-cleared against reserved hues;
  otherwise token-neutral.
- **Code changes:** `Views/InspectRailView.cpp` `MakeMemberRow` (one `MakeIdenticon` call into
  the kit row's existing layout); device rows likewise if the rail gains them.

---

## 5. What NOT to copy — consolidated

| Temptation | Source app | Why not here |
|---|---|---|
| Brand-colour bubble fills | Signal ultramarine, Telegram green, Session green | G3: `#EFF7BB` reserved; direction fills `card`/`cardHover` are Spec C §5.2 |
| Colour-only read receipts (blue checks) | WhatsApp, Telegram | Three-channel rule — colour may only restate |
| Bubble tails + wallpapers + gradients | Telegram | Their identity, our noise; flat tonal brand |
| Drop shadows / ThemeShadow / Mica / Acrylic | Fluent defaults | Tonal-only depth (M3 dark rule); the app is deliberately backdrop-free and PrintWindow verification depends on it |
| Per-message shield badges | Element | A colour-carried trust claim — G4 hazard with no crypto behind it |
| Full technical dump in primary UI | SimpleX | Advanced Mode is the disclosure valve; Normal stays calm |
| Bouncy spring overshoot | Telegram | Max `kHoverScale 1.03`; `kRevealSpringDamping 0.86` exists precisely to prevent overshoot |
| Green theming everywhere | Session | `kUrGreen` is presence/padlock/health only, and each use carries a word |
| `kProGold #FFC400` anywhere | (URnetwork Pro) | Reserved for entitlement; must appear NOWHERE in the demo |
| The cipher name in any form | plan's A6 draft | `"XChaCha20-Poly1305"` renders nowhere, dump included (audit, binding) |
| Bare "Verified" in green for the server key | plan's N2/N4/A3 drafts | `Demo model: verified` via the corrected `FormatKeyState` (audit, binding) |

---

## 6. Payoff vs risk — suggested sequencing for the coordinator

Ordered by (visible payoff ÷ implementation risk) from the baseline captures:

1. **P8 stub polish + routing** (with handoff §9 item 1) — removes the blank-pane hazard visible
   in `network-stub.png`. Lines of code: tens. Risk: nil.
2. **P1 tonal ladder assignment** — the single biggest "flat → depth" change (handoff gap #1) for
   a handful of brush reassignments. Risk: nil; verify by pixel-diff screenshot.
3. **P2 hover/press pass** — makes every still read as alive; spends two unspent motion tokens.
   Risk: low-medium — the `SetBubbleEdge` single-writer rule (`ThreadView.cpp:125-153`) means the
   hover edge must be an overlay, not a brush write. Needs a hover screenshot.
4. **P5 status strip** — first visible URnetwork identity; mostly assembled (style, host, kit
   field builder, dot tokens all exist). Risk: medium — G4 wording must use the framed
   `FormatKeyState`; S2/W6 ownership must be settled first (audit).
5. **P6 Network page** — the identity surface: stat voice + relay diagram + framed server rows.
   Risk: medium-high — D6 animation quarantine, N1/N2 audit overrides, new view module.
6. **P9/P10 chrome voice + mini-identicons** — small, compounding distinctiveness. Risk: low.
7. **P4 bubble run-shaping** — high class factor, but touches selection-outline geometry and the
   bubble template. Risk: medium — do after P2 settles the edge story.
8. **P3 delivery morph** — the most pedagogically valuable animation (D5) on the most gate-dense
   element. Risk: highest of the set — three-channel gate, plus the motion-off path has never
   executed on this machine (handoff §4). Do last, with a deliberate-fail gate demo.

Items 1-3 are one worktree-safe pass; 4-5 belong to the S/N owners after the audit's ownership
cluster is resolved; 7-8 are the polish pass once surfaces are stable.

---

## Appendix — sources

- Signal-Desktop `_variables.scss` (palette, selection, easing): https://raw.githubusercontent.com/signalapp/Signal-Desktop/main/stylesheets/_variables.scss
- Check grammar across apps: https://www.androidpolice.com/google-messages-new-delivery-icons/ and https://meanzspot.com/what-does-two-check-marks-mean-in-texts-me/
- Element X / Compound: https://element.io/blog/deep-dive-into-element-x/ , https://github.com/element-hq/compound-design-tokens
- Session: https://getsession.org/
- SimpleX: https://github.com/simplex-chat/simplex-chat
- URnetwork: https://ur.io/
- M3 tonal elevation (dark-mode depth by lightness): https://tijocreative.com/articles/dark-mode-ui-design-best-practices-pitfalls-real-examples
- Signal UI/UX review: https://createbytes.com/insights/signal-ui-ux-review-is-it-just-a-trend
- Code citations: `app/src/App/UrColors.h`, `UrMotion.h`, `App.xaml` (:520, :547-611, :753),
  `MainWindow.xaml` (:90-148, :246-278), `Views/ThreadView.cpp`, `Views/ConversationListView.cpp`,
  `Views/InspectRailView.cpp`, `UrComponents.h`, `docs/superpowers/specs/2026-09-06-urmessage-demo-ui-design.md`,
  `docs/superpowers/plans/2026-09-08-remaining-task-audit.md`, `plans/tasks/network.md`, `plans/tasks/strip.md`
