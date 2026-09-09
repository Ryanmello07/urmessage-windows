# D4 — Inspector rail redesign proposal

2026-09-08 · design swarm, rail agent · RESEARCH AND DESIGN ONLY — no file under
`app/src` was modified and the app was not run for this document. Everything below is
grounded in the code at the paths cited and in the four baseline captures under
`.verify-baseline/` (`inspect.png` = message mode, `thread.png`/`chats.png` =
conversation mode).

---

## 0. What the rail is today (verified, not assumed)

One 360 DIP column, visible only at >=1500 DIP content width (`Demo/DemoShellState.h:37,45`;
the shell owns `RailColumn`/`RailRule`/`RailHost` — `MainWindow.xaml.cpp:353-423`). The
view is `app/src/App/Views/InspectRailView.{h,cpp}`; the content model is
`app/src/App/Views/InspectRailFields.{h,cpp}` (pure C++, probe-gated in `--diagnose`).

**Conversation mode** (`PopulateConversation`, InspectRailView.cpp:493-511): `Details`
header (40 DIP sheet strip, `UrPaneTitleStyle`), a 56 DIP subject row (40 px identicon,
name in `UrRowTitleStyle` 13, note `UrRowNoteStyle` 11), a `MEMBERS` group header with a
bare count meta, member rows at 36 DIP (7 px dot, name ` · Admin`, meta `1/2 online`),
a `RETENTION` header, two key/value rows at 34 DIP (`Retention`, `Media`).

**Message mode** (`PopulateMessage`, InspectRailView.cpp:514-535): 56 DIP lock header
(16 px `E72E` padlock in `kUrGreen`, title `Demo model: end-to-end encrypted`, note
`Demo model: fabricated data, no crypto in this build` — both gate-asserted, byte-exact,
untouched by this proposal), an optional failure block (danger reason + disabled
"Try again"), a `MESSAGE` header, eight key/value rows. Advanced density appends four
more (`BuildMessageFields`, InspectRailFields.cpp:148-179). Order is probe-pinned
(`kExpectedKeys`, InspectRailFields.cpp:265-268); normal must be a positional prefix of
advanced.

**Not yet built (R4 scope, unstarted):** the delivered-by / read-by device lists
(`MakeDeviceRow` exists at InspectRailView.cpp:610 and has **no caller** — verified by
grep), `SetInspectRailAdvanced` (declared only, InspectRailView.h:71). The R4 brief has
eight audit findings; the ones that matter here are at
`docs/superpowers/plans/2026-09-08-remaining-task-audit.md:1069-1214,1964-2097` — stale
anchors (`InspectRailHost` → `RailHost`; `demo_.advanced` → `options_.advanced`; the
`BuildMessageFields` loop → `AppendFieldRows`), a regex gate that miscounts
`CrossfadePageSwap(`, and empty-list branches no fixture row can reach.

**What the baselines show:** the rail is a flat key/value table on the page colour. Every
row is a 34–36 DIP ruled line; the only tonal step is the sheet header; the only vivid
element is the green padlock. Handoff section 5 gaps 1 (depth), 3 (typographic
hierarchy), and 7 ("the rail is a key/value table … the member rows could look like the
Session-style connected clients the owner asked for") all land on this surface.

---

## 1. Design intent

Move the rail from *table* to *composed inspector*: three devices, used in this order —

1. **Section cards.** Content groups sit on `kCard` (#1C1C1C) modules with an 8 px radius
   over the #101010 page; the group headers stop being ruled sheet strips and become
   floating letterspaced captions above their card. This spends the tonal ramp the
   handoff says is unused, and it is the iOS/Signal grouped-list idiom the owner already
   reads as "modern messenger". (G3: uses `card`, `border`, existing brushes only.)
2. **People, not rows.** Members get avatar + presence-badge + expandable device
   sub-rows — the "connected clients" treatment — and the message-mode device lists
   (R4) land as designed device rows instead of more table. (The owner's explicit ask.)
3. **One framed hero per mode.** Conversation mode: the subject block grows into a real
   identity header. Message mode: the lock header becomes a card that *visibly contains*
   its own framing — the answer to the reviewer's note that the green padlock is the only
   unframed positive claim on that row (handoff section 4). We ADD framing; we remove
   nothing; both strings stay byte-exact.

Nothing here reorders, renames, recolours, adds, or drops a *field*. The field model
(`InspectRailFields`) is probe-gated and stays as-is except for one additive pure helper
(section 9). `--diagnose` stays 42 PASS by construction, plus any new clauses we add on
purpose (section 15).

---

## 2. Conversation mode — proposed composition

```
+ Details ------------------------------------------------ [ADVANCED] -+  40, sheet strip (unchanged)
|                                                                      |
|  [identicon 48]  Design team                         <- 64 DIP       |  subject block, on page
|                  Group, 5 members                                    |
|                                                                      |
|  MEMBERS                                   5 members · 5/7 online    |  floating caption, 32
|  +--------------------------------------------------------------+    |  card: kCard, r8, 1px kBorder
|  | [avatar 28 +badge] Mira Okonkwo · Admin       1/2 online   v |    |  presence row, 44, Button
|  |      - Phone                                     online      |    |  device sub-row, 36 (expanded)
|  |      - Laptop                                12 min ago      |    |
|  | [avatar 28 +badge] Tobias Lind · Admin       1/2 online   v  |    |
|  | [avatar 28 +badge] Saoirse Kelly             1/1 online   v  |    |
|  | [avatar 28 +badge] Ravi Menon                1/1 online   v  |    |
|  | [avatar 28 +badge] Elena Vasquez             1/1 online   v  |    |
|  +--------------------------------------------------------------+    |
|                                                                      |
|  RETENTION                                                           |  caption (+ timer glyph
|  +--------------------------------------------------------------+    |   in trailing slot when
|  | Retention                                  Kept until deleted |    |   conv.disappearing)
|  | Media                                    Media kept 30 days   |    |  kv rows, 34
|  +--------------------------------------------------------------+    |
|                                                                      |
|  ADVANCED              <- only at advanced density, no animation     |
|  +--------------------------------------------------------------+    |
|  | Conversation id                            conv-design-team  |    |
|  | Group id                                          16 hex chars |   |  groups only (existing rule)
|  +--------------------------------------------------------------+    |
+----------------------------------------------------------------------+
```

**Subject block** (replaces `MakeSubjectRow`, InspectRailView.cpp:310-349):
`MakePaneRow(64)`, 48 px identicon (existing `MakeIdenticon(conv.identityKey, 48)` — it
applies its own radius, Identicon.h:35), name in `UrBodyStrongTextStyle` (14, SemiBold —
the subject is this pane's title), note unchanged and byte-identical:
`Group, N members` / `Direct message`, still computed from `members.size()` so it cannot
disagree with the list (the invariant the comment at InspectRailView.cpp:337-340
protects).

**MEMBERS caption meta** becomes words instead of a bare count:
`std::format(L"{} members · {}/{} online", members.size(), onlineDevices, totalDevices)`,
both sums from the existing public `OnlineDeviceCount` (InspectRailFields.h:161).
At 11 sp (`UrPaneMetaStyle`) this is ~190 DIP — fits beside the ~75 DIP letterspaced
caption within 360 − 24. (R2's brief checked the bare count; the string is view copy,
not probe-gated — re-baseline the capture.)

---

## 3. Message mode — proposed composition

```
+ Details ------------------------------------------------ [ADVANCED] -+
|                                                                      |
|  +--------------------------------------------------------------+    |  LOCK CARD (hero)
|  | +------+  Demo model: end-to-end encrypted                   |    |  kCard, r8, 1px kBorder,
|  | | lock |  Demo model: fabricated data, no crypto in this     |    |  margin 12, pad 12
|  | +------+  build  (wraps to two lines rather than trimming)   |    |  chip 36x36, kCardHover
|  +--------------------------------------------------------------+    |  lock 18 px kUrGreen
|                                                                      |
|  | The message server did not acknowledge this message.              |  failure block (unchanged
|  [ Try again ]  (disabled)                                           |  strings; +2px danger rule)
|                                                                      |
|  [identicon 20] You                                        11:08     |  message subject, 44 (s.10)
|                 Attaching the rail measurements now.                 |  body excerpt, trimmed
|                                                                      |
|  DELIVERY                                                            |
|  +--------------------------------------------------------------+    |
|  | Sender                                                      You |  |  fields 0-2 (positional
|  | Sent                                              Today 11:08   |  |  group, probe-asserted)
|  | Received                                          Today 11:08   |  |
|  +--------------------------------------------------------------+    |
|  PROTOCOL                                                            |
|  +--------------------------------------------------------------+    |
|  | Epoch                                                      4171 |  |  fields 3-4
|  | Sender leaf index                                               0 | |
|  +--------------------------------------------------------------+    |
|  MESSAGE                                                             |
|  +--------------------------------------------------------------+    |
|  | Retention class                                      Permanent |  |  fields 5-7
|  | Size                                                    <= 1 KiB |  |
|  | Attestation                                Demo model: verified |  |  value NEVER recoloured
|  +--------------------------------------------------------------+    |
|  ADVANCED                   <- advanced density only (fields 8-11)   |
|  +--------------------------------------------------------------+    |
|  | Group id · Wire size · Message id · Conversation id            |  |
|  +--------------------------------------------------------------+    |
|  DELIVERED TO                                                7       |  R4 lists, carded
|  +--------------------------------------------------------------+    |
|  | - Phone · Mira Okonkwo                              online     |  |  device rows, 36
|  | - Laptop · Mira Okonkwo                          12 min ago     |  |  (existing MakeDeviceRow)
|  | ... 5 more                                                      |  |
|  +--------------------------------------------------------------+    |
|  READ BY                                                     7       |
|  +--------------------------------------------------------------+    |
|  | ...                                                            |  |
|  +--------------------------------------------------------------+    |
+----------------------------------------------------------------------+
```

The field order on screen is exactly `kExpectedKeys` — grouping inserts captions and card
frames *between* rows at fixed positions; it never permutes them. See section 9 for the
probe-asserted mapping.

---

## 4. Section cards and floating captions (P1)

**What.** Each content group = one floating caption + one card. Card: `Border` with
`Background` kCard (#1C1C1C), `CornerRadius` 8, `BorderBrush` kBorder (#1FFFFFFF) 1 px,
`Margin` (12, 0, 12, 10). Radius 8 follows the boxed-tile/identicon radius
(`UrStatTileStyle`, App.xaml:566-568; `MakeIdenticon`) rather than the 12 of page-level
hero cards (`UrCardStyle`, App.xaml:464-470) — rail modules are subordinate objects.
Caption: `kit::MakePaneGroupHeader(title, meta)` (UrComponents.cpp:358) with three
per-instance overrides applied by the rail: `Background` → Transparent,
`BorderThickness` → 0, `Height` → 32. The letterspaced chrome voice
(`UrGroupHeaderTextStyle`, 11 sp, CharacterSpacing 90, textMuted) stays — it is exactly
the "chrome voice carries more of the structure" move handoff section 5.3 names. First
caption in a body gets top margin 4, later ones 12.

Rows inside a card keep their `MakePaneRow` 12 px padding and bottom hairlines
(UrComponents.cpp:149-160); the card assembler sets the **last** row's
`BorderThickness` to 0 so no hairline sits flush against the card's bottom edge. Row
roots are `Border`s handed back by every kit builder, so this is view-side, no kit
change.

**New kit builder** (because the Network page's groups will want the same module —
UrComponents.h:166-180 declares the kit the shared vocabulary; if the coordinator wants
minimal blast radius it can start file-local in InspectRailView.cpp):

```cpp
// UrComponents.h — Wave-2 block
struct PaneCard {
  winrt::Microsoft::UI::Xaml::Controls::Border root{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel body{nullptr};
};
// A section module for the pane layout: kCard, radius 8, 1px border, 12px side
// margins. Rows are appended to `body`; FinalizePaneCard clears the LAST row's
// bottom hairline so nothing sits flush against the card's edge.
PaneCard MakePaneCard();
void FinalizePaneCard(PaneCard const&);
```

**Constraints.** G3: spends `card`/`border`, adds no resource key, no accent, no gold,
no large accent area. No field/string changes. The handoff explicitly authorizes
superseding task-file geometry where every section-4 rule holds ("The plan is normative
on behaviour and honesty. It is not normative on being basic."). Behaviour — which rows
exist, in which order — is untouched.

---

## 5. Label/value typographic hierarchy (P2)

Today: key 13 sp textMuted / value 13 sp off-white (`UrKeyTextStyle`/`UrValueTextStyle`,
App.xaml:1063-1081) — one step of grey, same size, right across from each other; the
baselines read as a spreadsheet.

**Proposal.** Inside the rail only: keys drop to 12 sp (apply `UrCaptionTextStyle`,
App.xaml:206-211 — an existing key, so nothing is added to App.xaml), values stay 13 sp
`UrValueTextStyle`. Implementation: `AppendFieldRows` (the one funnel,
InspectRailView.cpp:480-487) already receives the built row; it sets
`row.key.Style(kit::StyleByKey(L"UrCaptionTextStyle"))` on the returned parts. The kit
styles themselves are **not** edited — they are shared with future pane surfaces, and
the rail's quieter keys are a rail-local decision. Keys stay left/Star, values stay
right/Auto; the 28-character value budget (`kInspectValueMaxChars`,
InspectRailFields.h:51-66) is unaffected because the value column's geometry is
unchanged — it actually gains a few DIP of key-column slack at 12 sp.

**Also in the funnel — blank values go faint.** When `field.value.empty()`, set
`row.value.Foreground(urnw::colors::FaintBrush())`. The drawn string still comes from
`RailValueOr` (the em dash) and the spoken string from `RailSpokenValueOr` ("not set") —
the poisoned macro, the static asserts and the spoken/drawn disagreement gate all stay
exactly as they are; only the *ink* of the placeholder quiets to #5A5A5A, which reads
as "nothing here" instead of lending a dash the same weight as a real value. (G4: the
dash is not a claim; quieting it adds no reading.)

**Constraint notes.** No display face anywhere in the rail — the rail has no title
surface, and `Details` stays in the Montreal chrome voice (G3 typography rule respected
by omission, stated so no later pass "upgrades" it). Keys in `UrCaptionTextStyle` are
still PP Neue Montreal.

---

## 6. The lock header as a designed, self-framing element (P3)

**The sensitivity, restated.** The two strings are gate-asserted and byte-exact
(`Demo model: end-to-end encrypted`, `Demo model: fabricated data, no crypto in this
build`; InspectRailView.cpp:410,423). `MessageInspect::cipher` is rendered nowhere and
stays that way (InspectRailFields.h:25-33). The reviewer's note (handoff section 4): the
green padlock is the only unframed positive claim on the row, and the half a cropped
screenshot keeps. So the design goal is: **the framing must be at least as visible as
the claim**, and the glyph must not be amplified.

**Proposal.** The header becomes the top card of message mode (P1 card geometry, margin
(12,12,12,10), padding 12):

- A 36x36 rounded-square **chip** — `kCardHover` fill (#242424), 1 px `kBorder` edge,
  radius 8 — holds the padlock. This is a *neutral tonal container*: it frames the glyph
  the way a mount frames a photo, adds no colour claim, and spends no reserved token.
  The padlock itself stays the same `E72E` glyph (already render-verified in this file,
  InspectRailView.cpp:402), same `kUrGreen`, bumped 16 → 18 px only so it centres
  optically in the chip. `kUrGreen`'s reservation is "presence, padlock" — this is still
  the padlock, and deliberately the *only* green pixel-group in the card. No green rule,
  no green tint, no larger glyph field.
- Title: `UrBodyStrongTextStyle` (14 SemiBold), text **byte-exact, unchanged**.
- Note: `UrRowNoteStyle` (11 muted), text **byte-exact, unchanged** — and here is the
  one behavioural change with teeth: the note **wraps to two lines instead of
  trimming**. Today the row is a fixed 56 DIP `MakePaneRow` with a `NoWrap` note; in the
  carded layout the available width is 360 − 24 (margins) − 24 (padding) − 36 (chip)
  − 10 (gap) = 266 DIP, and the 47-char note needs ~285 — it would trim away "in this
  build", i.e. crop the framing, which is the exact failure prefix-first framing exists
  to prevent. A hero card is not a fixed-height list row, so `TextWrapping=Wrap` costs
  nothing structurally. **Acceptance check: the full note must be measurable on screen
  in every capture.**
- Accessibility: keep today's shape (two static texts announced as today; glyph stays
  `AccessibilityView Raw` via `UrRowIconStyle`). No new words are added anywhere —
  framing is achieved with layout and typography, which cannot be misquoted.

**What is explicitly rejected:** colouring the title or any part green; a green left
rule on the card (amplifies the claim); a shield glyph (a *new* positive symbol);
moving the note into the value voice (it must read as caveat, not content). The card
treatment makes the header read as one *designed object* — the thing a screenshot crops
now contains both lines, because they are vertically adjacent inside a bounded box.

**Code.** `MakeLockHeader` (InspectRailView.cpp:383-430) rebuilt as above; the 56 DIP
rhythm comment at :416-417 is updated to say the lock header deliberately leaves the row
rhythm (it is the mode's hero, not a list row). No kit change required — the card is
`MakePaneCard()` and the chip is a `Border` + `FontIcon`.

---

## 7. Member rows as Session-style connected clients (P4)

The owner asked for this by name. Today a member is one 36 DIP line: 7 px dot, name,
`1/2 online` (InspectRailView.cpp:579-608). Session's connected-clients idiom = avatar
with a presence badge, and the client's *devices* visible under the person.

### 7.1 The presence row (new kit builder)

```cpp
// UrComponents.h — beside MakePaneListRow
struct PanePresenceRow {
  winrt::Microsoft::UI::Xaml::Controls::Button root{nullptr};      // UrPaneRowButtonStyle, 44
  winrt::Microsoft::UI::Xaml::FrameworkElement avatarSlot{nullptr}; // caller drops MakeIdenticon(seed, 28)
  winrt::Microsoft::UI::Xaml::Shapes::Ellipse badge{nullptr};      // 10px, bottom-right over avatar
  winrt::Microsoft::UI::Xaml::Controls::TextBlock title{nullptr};  // UrRowTitleStyle
  winrt::Microsoft::UI::Xaml::Controls::TextBlock meta{nullptr};   // UrPaneMetaStyle
  winrt::Microsoft::UI::Xaml::Controls::FontIcon chevron{nullptr}; // UrChevronIconStyle
};
PanePresenceRow MakePanePresenceRow();
void SetPanePresenceOnline(PanePresenceRow const&, bool online);  // badge: disc vs ring
```

- Height 44 (`UrPaneRowTallHeight`, App.xaml:1100 — the explained-row height; the avatar
  wants the air). Button root on the existing `UrPaneRowButtonStyle`, so hover =
  `kCard` fill, pressed = `kCardPressed`, 150 ms state transitions already in the
  template (App.xaml:1016-1039, `GeneratedDuration 0.15` = kFastMs) — the hover gap of
  handoff section 5.2 is closed here by making the interactive row an actual Button, not
  by faking hover on static rows.
- Avatar: 28x28 `MakeIdenticon(member.identityKey, 28)` — member identity keys exist
  (`MemberRef::identityKey`), and InspectRailView.h:78-83 already records that if member
  identicons are wanted, the right move is a kit variant, not a bespoke row. This is
  that variant. Identicon hues are static-asserted >=30 degrees from every reserved
  state colour (Identicon.h:36-43), so the badge can never be mistaken for part of the
  picture.
- Badge: 10 px, sitting on the avatar's bottom-right corner, implemented as a 10 px
  page-coloured (#101010) disc with an 8 px state element inside, so the badge reads
  punched out of the avatar. Online: inner disc `kUrGreen`. Offline: inner **ring** —
  transparent fill, 1 px `FaintBrush` stroke. Ring-vs-disc is a *shape* channel, the
  same ring/disc language the delivery glyphs use (`E930`/`EC61`, ThreadLayout.cpp:95-99).
  State is still carried primarily by the meta **words** (`1/2 online` — unchanged
  strings, so "colour is never the only carrier" stays true and the badge/dots stay
  `AccessibilityView Raw` restatements, per the rule at UrComponents.cpp:246-248).
- Chevron: `UrChevronIconStyle` (14, faint), Segoe Fluent `E70D` (ChevronDown) collapsed
  / `E70E` (ChevronUp) expanded. No chevron literal exists in the codebase today
  (verified by grep — only the style), so per house rule (handoff section 7, non-ASCII)
  the implementer must render-verify both codepoints and write them as `L"\uE70D"` escapes
  with trailing comments, never pasted glyphs.
- Automation: the kit rule — a Button with panel content gets no automatic name
  (UrComponents.h:384-386) — so the view sets the name to
  `name + (admin ? ", Admin" : "") + ", " + presence + ", expandable device list, collapsed"`,
  rewriting the trailing word on toggle. Title/meta/chevron marked Raw.

### 7.2 Expandable device sub-rows

Clicking a member row inserts that member's devices beneath it — the connected clients.

- Rows: the existing `MakeDeviceRow` (InspectRailView.cpp:610-635 — 36 DIP, dot, title,
  meta `online` / last-seen word) with one signature addition:
  `MakeDeviceRow(device, /*showOwner=*/true)`. Under a member the owner suffix is
  redundant (`Phone · Mira Okonkwo` under *Mira Okonkwo*), so the sub-rows pass `false`.
  Default `true` keeps the Network page's "Your devices" usage (design section 6.4)
  unchanged.
- Indent: sub-row root `Margin(26,0,0,0)`. Arithmetic: member title column starts at
  12 (padding) + 28 (avatar) + 10 (gap) = 50; the device row's dot column starts at
  12 + 2 (marker) + 10 (spacing) + 26 (margin) = 50 — device dots form one vertical line
  directly under the member names, which is what reads as "belonging" without a tree
  glyph. (Implementer verifies by screenshot; the number is the contract, the arithmetic
  is the derivation.)
- Motion: on expand, sub-rows fade 0→1 at `kFastMs` with the Soft ease — `kSoftP1/P2`
  is documented "a gentle disclosure" (UrMotion.h:57-58) — staggered `kStaggerMs` 40,
  at most 2 steps so the 6-step cap is never near. On collapse, instant removal (exit
  faster than entrance, UrMotion.h:14-15). All gated on `motion::ShouldAnimate()`; with
  motion off the rows simply appear. The chevron swaps glyph instantly in both cases.
- Expansion state: `PopulateConversation` clears and rebuilds the body
  (InspectRailView.cpp:498), so a density toggle would silently collapse everything.
  Park the expanded member ids on the view struct — `InspectRailView` gains
  `std::vector<std::wstring> expandedMemberIds{};` (plain C++ struct; the "three fields"
  comment at InspectRailView.cpp:267-269 is updated in the same diff). Cleared by
  `SetInspectRailConversation` (new subject, new state), preserved across
  `SetInspectRailAdvanced` re-population. Member ids are world-unique strings
  (`m-mira`, DemoWorld.cpp:247-251) — no pointers, consistent with the rail's
  ids-not-pointers rule (InspectRailView.h:18-23).

**Constraint notes.** G3: the badge spends `kUrGreen` only in its reserved role
(presence); the page-coloured punch-out is the page token; the ring is `textFaint`. No
accent, no gold. The words-carry-state rule is preserved verbatim. Fixture untouched —
device data is already on `MemberRef::devices` (DemoWorld.cpp:78-86: everyone a phone,
admins a second offline laptop — which is precisely the state this design exists to
show).

---

## 8. Delivered-by / read-by device lists (P5 — designing R4's landing, not building it twice)

R4 will add these lists; this proposal defines their final form so R4 builds them
carded instead of building a flat version and re-doing it.

- Captions `DELIVERED TO` / `READ BY` with the count meta (as the R4 brief specifies,
  rail.md:1456-1459), each followed by a P1 **card** of `MakeDeviceRow` rows at 36 DIP —
  owner suffix **kept** here (`showOwner=true`): in message mode the devices belong to
  *different* people, and `Pixel 9 · Bo Nakamura` is the load-bearing "statement by a
  device" content (Spec C section 5.3 via design section 6.3). No avatars on device
  rows — the person/device species contrast (avatar+badge vs dot) is the visual
  hierarchy, and `MakeDeviceRow` stays one reusable species for the Network page.
- The `This computer` substitution and the `online`/last-seen words stay exactly as
  `MakeDeviceRow` already writes them.
- Empty state: R4's honest line (`No device has acknowledged this message` /
  `No device has read this message`) renders **in place of** the card (caption + one
  centred faint line, no empty bordered box). Per the audit (lines ~2041-2067) the empty
  branch is unreachable from the fixture — cover it with a locally built
  `MessageInspect` in a probe clause, never by editing `DemoWorld` (I10 fingerprint).
- Placement: below the field cards, after ADVANCED. R4's step-8 invariant — "Advanced
  Mode adds rows; it does not reorder the surface" (rail.md:1549) — holds: the lists are
  always last.

---

## 9. Message-mode field grouping, with the probe on our side (P6)

The eight normal fields are positionally pinned (`kExpectedKeys`,
InspectRailFields.cpp:265-268) and normal must be a prefix of advanced. Grouping must
therefore be **contiguous and order-preserving**. The pinned order splits cleanly:

| Caption | Fields (indices) | Cluster meaning |
|---|---|---|
| `DELIVERY` | Sender, Sent, Received (0-2) | who and when |
| `PROTOCOL` | Epoch, Sender leaf index (3-4) | protocol metadata of the model |
| `MESSAGE` | Retention class, Size, Attestation (5-7) | this message's policy and shape |
| `ADVANCED` | Group id, Wire size, Message id, Conversation id (8-11) | density-only additions |

Caption names are honest labels of the *fields the rail shows* — the same register as
the existing `MESSAGE`/`MEMBERS`/`RETENTION` headers. `PROTOCOL` was weighed against
G4: it names the category of two fields that already ship; it asserts nothing about
what this binary did. `RATCHET` was rejected (an MLS mechanism claim), `METADATA`
rejected (vague to the point of filler). **No caption, row, or tooltip may ever name a
cipher** — `MessageInspect::cipher` stays unrendered (InspectRailFields.h:25-33), and a
future "Cipher" row is a G4 violation whatever label sits above it.

So the grouping itself is checkable rather than conventional, add one pure helper to
`InspectRailFields.h/.cpp` (the pure TU the probes live in):

```cpp
enum class InspectGroup { Delivery, Protocol, Message, Advanced };
InspectGroup MessageFieldGroup(size_t fieldIndex);      // 0-2, 3-4, 5-7, 8+
InspectGroup ConversationFieldGroup(size_t fieldIndex); // 0-1 Retention, 2+ Advanced
// Caption strings live here too, so the rail's copy passes through the same
// review funnel as its field copy:
inline constexpr wchar_t kGroupDelivery[] = L"DELIVERY";   // ... etc
```

…and one probe clause in `InspectRailFieldsProbe` asserting the partition against the
already-pinned key table (12 keys: 0-2 Delivery, 3-4 Protocol, 5-7 Message, 8-11
Advanced; conversation: 0-1 Retention, 2+ Advanced). Mutation demonstration required
per house rule: shift a boundary, watch the clause fail, revert, paste both outputs.

The view change is confined to `AppendFieldRows`: as it walks the field vector it asks
`MessageFieldGroup(i)`; on a group change it finalizes the current card and opens a new
caption+card. One funnel, one walker — the poisoned-macro rule (InspectRailView.cpp:69)
is untouched, and the blank/spoken substitutions still happen exactly once.

**Attestation stays ordinary.** The Attestation row rides in the MESSAGE card in the
standard value voice. Never green, never a checkmark glyph, never a tint — the audit
found N2/N4/A3 about to render "Verified" in green for the server key and gate-asserted
against it; the same discipline applies here. The value string `Demo model: verified`
is the framing, and it is enough.

---

## 10. Message subject row (P7 — orientation for message mode)

Today message mode opens *cold*: lock header, then a table. Arriving via
`--demo=inspect` (or a future bubble click), the rail nowhere says *which* message it
is inspecting. Add a 44 DIP row (`UrPaneRowTallHeight`) directly under the lock card
(and under the failure block when present):

- 20 px identicon from `row.senderKey` (exists on every MessageRow, DemoWorld.h:70 —
  outgoing rows get the deterministic "me" seed, DemoWorld.cpp:149, so "You" has a
  stable face);
- line 1: sender word from the existing pure `SenderLabel(row)` ("You" / display name)
  in `UrRowTitleStyle`, with `row.timeLabel` right-aligned in `UrPaneMetaStyle`;
- line 2: `row.body`, `UrRowNoteStyle`, `NoWrap` + CharacterEllipsis (fixture bodies
  are one line; the style already trims).

Honest by construction: it repeats content already on screen in the thread; it adds no
claim. It also gives message mode its own subject row, mirroring conversation mode's
identity block — the two modes become the same *shape* of page (subject → sections),
which is what "mode crossfade" then visually means. View-local build beside
`MakeSubjectRow`; no kit change, no field-model change.

---

## 11. Failure block: add the danger rule, keep everything else (P8)

The block (InspectRailView.cpp:442-460) keeps its exact strings, its disabled
`UrPaneActionSecondaryStyle` "Try again", and its automation name. One addition: a 2 px
`kDanger` leading bar beside the reason line (full row height), mirroring the thread's
key-change record idiom (red rule + content — handoff section 3). G3: `danger` is the
reserved token for exactly this. The bar is a *shape* reinforcement of the red text,
so the danger signal is never colour-alone either.

---

## 12. Mode crossfade polish (P9)

What exists is already correct: `PresentMode` crossfades only on a real mode change via
`CrossfadePageSwap` (kBaseMs in / kFastMs out, exit ease, `ShouldAnimate`-gated,
instant path when motion is off — InspectRailView.cpp:289-295, UrMotion.cpp:120-139),
and a slide is correctly rejected (fixed width means no navigation). Three refinements,
all inside existing tokens:

1. **Settle.** The incoming body rises `kDist4` (4 DIP) → 0 over `kBaseMs`, standard
   bezier, alongside the opacity fade. Implemented as one small helper
   `urnw::motion::SettleIn(FrameworkElement)` in UrMotion.cpp (built from
   `MakeSplineDouble`, targeting
   `(UIElement.RenderTransform).(CompositeTransform.TranslateY)`; a `CompositeTransform`
   is attached once in `MakeBodyScroller`). It no-ops when `ShouldAnimate()` is false.
   A *vertical* 4 DIP settle reads as material settling, not navigation — the reason a
   lateral slide was rejected does not apply to it.
2. **Section cascade.** On a mode swap only, the incoming body's caption/card children
   fade 0→1 at `kBaseMs` with `BeginTime = i * kStaggerMs`, i capped at
   `kMaxStaggerSteps` (6). Worst case 250 + 5x40 = 450 ms, about `kSlowMs` — a legal
   large-surface change. This composes with (does not replace) the scroller-level fade,
   which `CrossfadePageSwap` must keep because it owns the visibility/collapse
   bookkeeping. The conversation list already staggers on entrance, so this is a second
   use of an established rhythm, not a new one.
3. **Density stays silent.** `SetInspectRailAdvanced` keeps R4's populate-only
   contract: no crossfade, no stagger, no storyboard — re-rendering an on-screen
   surface reads as a phantom mode swap (rail.md:1426, InspectRailView.h:67-71). An
   optional fade-in of only the newly appended ADVANCED card (kFastMs, Soft ease) was
   considered and is **deferred**: R4 step 10 gates "no storyboard from this function",
   and that gate is right for this stage. If a later pass wants the disclosure, it must
   amend that gate deliberately, not sneak a raw Storyboard past the `CrossfadePageSwap(`
   regex.

**Verification caveat (from handoff section 4):** the reduce-motion path has never
executed on this machine (`SPI_GETCLIENTAREAANIMATION = 1`). Anything in P9 ships only
after a manual run with Windows "Show animations" switched off, confirming an instant,
correct swap. Screenshot verification of the cascade uses pixel-difference against a
settled frame, per house section 7 (never byte length/hash).

---

## 13. What Advanced Mode adds visually (P10)

Advanced density (8→12 message rows, 2→3/4 conversation rows) lands via R4's
`SetInspectRailAdvanced`. On top of the grouping, Advanced Mode gains exactly two
visible signals, both calm:

1. **An `ADVANCED` caption+card** containing the appended rows (section 9) — the
   additions are self-describing instead of silently lengthening the table.
2. **A header meta**: the `Details` pane header becomes `[title][meta]` with a
   right-aligned `ADVANCED` in `UrPaneMetaStyle` (11 muted), `Collapsed` at normal
   density. This is the same density vocabulary the status strip spec uses ("the VPN
   client's two-density strip", design section 6.5) and answers "why are there more
   rows than a minute ago" without a toast or a colour. Implementation: header built as
   a two-column Grid in `MakeInspectRail`; the meta handle is parked on
   `InspectRailView` (new field `headerMeta`; the struct's comment updated);
   `SetInspectRailAdvanced` toggles `Visibility` **instantly** — chrome appearing in
   step with a populate-only re-render, no storyboard.

What Advanced must *not* add: colour coding, a border glow, monospace, or any new field
(the probe pins 8/12 and 2/3-4). Advanced = more information in the same voice. (Relay
hop timings and session figures are the Network page and status strip's job — the N and
S tasks — not the rail's.)

---

## 14. The "never" list for this surface (restated so no later pass drifts)

- `MessageInspect::cipher` ("XChaCha20-Poly1305") renders **nowhere** — no row, no
  tooltip, no copy button, no dump (A6's defect, audit-confirmed). [G4]
- Lock-header strings and the Attestation values stay **byte-exact**; prefix-first
  framing is never weakened, reordered, or trimmed — the note *wraps*, full stop. [G4]
- The padlock keeps its green and gains only neutral framing; no green anywhere else in
  the lock card; the Attestation value is never coloured. [G4/G3]
- `kAccent` #EFF7BB does not appear in the rail at all (send button + selection outline
  only); `kProGold` appears nowhere; no accent-tinted card, badge, or rule. [G3]
- ABC Gravity faces do not enter the rail — it is chrome and body, no title surface. [G3]
- Presence and delivery are never colour-alone: words in meta, ring/disc shapes, dots
  and badges are restatements marked Raw. [delivery/presence channel rule]
- `Demo/DemoWorld.cpp` is not touched; rare states (empty device lists) are covered by
  locally built probe inputs, per the audit's R4 override. [fixture immutability]
- Below 1500 DIP behaviour is unchanged (design section 6.5a's deliberate limitation).
  This proposal does not re-litigate it.

---

## 15. File-level change list

| File | Change |
|---|---|
| `app/src/App/Views/InspectRailView.cpp` | Bulk of the work: card assembler usage, caption overrides, rebuilt `MakeLockHeader`, new message-subject row, presence-row wiring + expansion, `AppendFieldRows` group insertion + blank-faint styling, `MakeDeviceRow` `showOwner` param, failure-block danger rule, `SettleIn`/stagger hooks in `PresentMode`, header meta toggle. Poison macro and both substitution funnels untouched. |
| `app/src/App/Views/InspectRailView.h` | Struct gains `headerMeta` + `expandedMemberIds`; `MakeDeviceRow` signature; comments updated (the "three fields" and identicon-variant notes — the variant now exists in the kit). |
| `app/src/App/Views/InspectRailFields.h/.cpp` | Additive only: `InspectGroup`, `MessageFieldGroup`, `ConversationFieldGroup`, caption string constants, one probe clause asserting the partition. Field order, counts, labels, values unchanged. |
| `app/src/App/UrComponents.h/.cpp` | `MakePaneCard`/`FinalizePaneCard`, `MakePanePresenceRow`/`SetPanePresenceOnline` (or file-local in the view for one pass if the coordinator prefers; kit is the right home — the Network page reuses both). |
| `app/src/App/UrMotion.h/.cpp` | `SettleIn(FrameworkElement)` — kBaseMs/kDist4/standard bezier, `ShouldAnimate`-gated. |
| `app/src/App/App.xaml` | **No new keys.** Every style used exists (`UrCaptionTextStyle`, `UrPaneMetaStyle`, `UrChevronIconStyle`, `UrPaneRowButtonStyle`, ...). Said per handoff section 3: none of the 87 keys needed a sibling. |
| `MainWindow.xaml.cpp`, `Startup.cpp`, `Demo/DemoWorld.cpp` | Untouched. R4's `SetInspectRailAdvanced` lands per the audit's corrected anchors (`RailHost`, `options_.advanced`, after `AppendFieldRows`) and drives density through the existing seam. |

**Gate impact.** `--diagnose` field probes are green by construction (model untouched,
one additive clause). View geometry checks in the R2/R3/R4 briefs (34 DIP rows, bare
count meta, group-header strips) are superseded by this design — the coordinator
re-captures baselines and updates those briefs' pixel expectations. New gates follow
house section 7: demonstrated failing (mutate a group boundary / drop the faint-blank
branch), then reverted, outputs pasted.

---

## 16. Payoff vs risk — suggested landing order

1. **P1 + P2 + P6 — cards, captions, hierarchy, grouping.** The escape from "key/value
   table"; pure view/layout, zero gated strings touched, one additive probe clause.
   Highest payoff, lowest rule-risk. Do first; everything else composes onto it.
2. **P3 — lock header card.** Highest sensitivity per line of code (gate-asserted
   strings, the padlock note), but the change is layout + wrap, not copy. Small diff,
   big hero payoff. Land with a capture that proves the full note renders.
3. **P4 — member presence rows + expandable devices.** The owner's named ask
   ("Session-style connected clients"). New kit builder + expansion state — the most
   code here; medium risk, high payoff. Needs the render-verified chevron escapes.
4. **P5 — carded delivered-by/read-by lists.** Fold into R4's dispatch so the lists are
   built once, in final form. Depends on R4; audit overrides for R4 apply regardless.
5. **P8 — failure danger rule.** Five lines, ties the rail to the thread's danger
   idiom. Anytime after P1.
6. **P7 — message subject row.** Orientation + mode symmetry; low risk, medium payoff.
   Independent.
7. **P10 — ADVANCED header meta.** Tiny, but only meaningful once R4's density switch
   exists; keep with or after R4.
8. **P9 — settle + cascade.** Last: it is polish on top of a finished composition, and
   it carries the one unverified branch in the project (reduce-motion has never run on
   this machine) — ship it only with the manual no-motion check done.

*Non-goals, stated plainly:* no rail below 1500 DIP, no new fields, no copy edits to any
gated string, no per-device timestamps (the fixture has none to show), no colour-coding
of states anywhere in the rail.
