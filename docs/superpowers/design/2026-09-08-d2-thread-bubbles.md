# D2 — Thread, bubbles and composer: design proposal

Date: 2026-09-08. Surface: `--demo=thread` / the thread half of `--demo=inspect`.
Author's sources: `app/src/App/Views/ThreadView.cpp` (full), `Views/ThreadLayout.{h,cpp}`,
`Demo/ThreadLayout.h`, `App.xaml:632-825` (button styles), `App.xaml:180-272` (type ramp,
brushes), `UrColors.h`, `UrMotion.h`, `Startup.cpp:940-1270` (T4/T5/T6 gates),
`MainWindow.xaml.cpp:337-347`, baselines `thread.png`, `chats.png`, `inspect.png`.

## 0. What is on screen today (measured, not assumed)

- Bubbles: uniform `CornerRadius 12`, padding `12,8` (both from `UrBubbleButtonStyle`,
  App.xaml:758-759), incoming `UrCardBrush` #1C1C1C left / outgoing `UrCardHoverBrush` #242424
  + 1px `UrBorderBrush` right (`MakeBubbleRow`, ThreadView.cpp:332-336). Width cap 68%/640 dip
  (`BubbleMaxWidthDip`, Demo/ThreadLayout.h:37-41).
- Sender name renders **inside** the bubble, above the body (ThreadView.cpp:347-355); identicon
  (28 dip, radius 8) in a 36 dip gutter on run-starts only.
- Vertical rhythm is one uniform `stack.Spacing(6)` (ThreadView.cpp:886) — a run of three
  bubbles has the same 6 dip gaps as three separate runs. Runs are invisible except for the
  header rule. This is the single biggest "basic" tell in `thread.png`.
- Delivery cluster: 13px glyphs + 11px word, right-aligned, margin `(0,2,2,6)`
  (ThreadView.cpp:180-183). State appears/disappears **instantly**; design §7's
  "Delivery morph — `kFastMs` glyph cross-dissolve" (spec line ~243) was never implemented.
- Composer: `CardBrush` bar on a top hairline (ThreadView.cpp:722-726); focus draws a 1px
  `#38FFFFFF` rule **under the whole row** via `FadeFocusRule` (kFastMs in / kMicroMs out,
  ThreadView.cpp:684-700, 827-835). Send pill disabled, `AccentButtonStyle` wash #33EFF7BB.
- Typing indicator: three 6px dots + "Typing…", margin-left 20 (ThreadView.cpp:639) — which
  aligns with **nothing**: incoming bubble left edge in a group is 16 (scroller pad) + 36
  (gutter) = 52. In `thread.png` the dots float left of the bubble column.
- Day separator: bordered pill, radius 10, `UrGroupHeaderTextStyle` (ThreadView.cpp:489-506).
- Key-change record: 2px danger rule, `\uE192` key glyph, 12px off-white copy, disabled
  Review (ThreadView.cpp:546-623). Correct and appropriately heavy.
- The thread pane has **no header** — `ThreadHost` is a bare `UrPaneStyle` Grid
  (MainWindow.xaml.cpp:337-347). Header ownership belongs to the wiring group; this proposal
  only makes the composer/d separators consistent with `UrPaneHeaderStyle` (#151515 sheet)
  so a future header lands on a matching surface.

## 1. Run-shape geometry (the bubble language)

**Proposal: asymmetric corners + run rhythm; no tails.**

Classify every message row into a geometric run — same speaker, adjacent Message rows,
nothing between. Note carefully: this is a THIRD notion of "run", beside the sender-header
rule (`ShowsSenderHeader`, senderKey-based) and `endsOutgoingRun` (direction-only position
hint, consumed by no renderer — verified by grep; only gates read it). The geometry rule is
the one a reader's eye actually uses: a run boundary is exactly where the sender header and
identicon already appear. Concretely: `ContinuesBubbleRun(prev,cur)` = both Message, same
`outgoing`, and if incoming, same `senderKey`. Outgoing rows are one speaker ("You"), so
outgoing runs unify — which the current direction-only code already assumes everywhere else.

**Corner table** (base radius 12 — matches the card radius used app-wide; attached corners 4):

| Position | Incoming (TL,TR,BR,BL) | Outgoing (TL,TR,BR,BL) |
|---|---|---|
| Single | 12,12,12,12 | 12,12,12,12 |
| First  | 12,12,12,4  | 12,12,4,12  |
| Middle | 4,12,12,4   | 12,4,4,12   |
| Last   | 4,12,12,12  | 12,4,12,12  |

The tightened corner is always on the edge facing the adjacent same-speaker bubble — the
"spine" (left for incoming, right for outgoing). Solo bubbles stay fully round, so DMs (the
fixture's 4-row DMs are almost all singles) don't get a harsh look.

**Rhythm:** `stack.Spacing` 6 → 0; per-row top margins instead: run continuation 2 dip,
run start/single 10 dip. Day separator keeps `(0,16,0,8)`, system line `(0,8,0,8)`, record
`(0,12,0,12)`. Runs now read as blocks; the Failed row at DemoWorld.cpp:277 keeps its cluster
binding (§6 below).

**Sender name moves OUT of the bubble** to a line above the run-start bubble, aligned with
the bubble's text: margin-left = gutter 36 + bubble padding 12 = 48 (relative to rowRoot;
DMs never have headers — `ShowsSenderHeader` is group-only). 11px `UrCaptionTextStyle`,
`MutedBrush`, margin-bottom 2. This makes every bubble interior uniform (body + time), and
matches Signal/iMessage. Automation unchanged: `BubbleAutomationName` doesn't read the
visual tree (Demo/ThreadLayout.h:161-178); the name TextBlock is already `MarkRaw`.

**Why no tail/notch (asked to evaluate):** a tail must be a second element painting the
direction fill. That breaks three invariants at once: (1) the press state is
`Root.Opacity 0.92` (App.xaml:808-813) — a tail outside the Button would NOT dim, a visible
mismatch on every press; (2) the selection outline is the Button's BorderBrush/BorderThickness
written by the ONE writer `SetBubbleEdge` (ThreadView.cpp:139) — a 2px accent outline cannot
wrap a separate notch; (3) hover is `EdgeLayer`, a template Grid template-bound to
`CornerRadius` — asymmetric corners come **free** through that same binding (no template
change), a tail needs a template fork in App.xaml:753 AND a custom selection path. Asymmetric
corners deliver ~90% of the run-language signal at zero of those costs. Recommendation:
asymmetric only; tails documented here as the rejected option.

**Implementation:**
- `Views/ThreadLayout.h/.cpp` (pure): `enum class BubbleRunPos { Single, First, Middle, Last };`
  `BubbleRunPos RunPosFor(prev, cur, next)`; `void BubbleCornerDip(BubbleRunPos, bool outgoing, double out[4])`;
  `double GapAboveDip(BubbleRunPos)` (2 or 10). All pure → diagnosable.
- `Views/ThreadLayout.h`: `ThreadRowPlan` gains `BubbleRunPos runPos` — computed in the
  existing `PlanThreadRows` loop, which already computes both `prev`/`next` and the
  direction-based `endsRun`.
- `ThreadView.cpp MakeBubbleRow`: `bubble.CornerRadius(CornerRadiusHelper::FromCorners(...))`
  from the plan; rowRoot.Margin from `GapAboveDip`; name TextBlock hoisted out of `column`
  into rowRoot above gutterRow. `MakeThread`: `stack.Spacing(0)`.
- **Gate:** new pure assertion over the fixture (corner table per row vs a hand-built
  expectation on c0's known runs — e.g. the Mira/Mira/Tobias stretch at DemoWorld.cpp:256-258
  is First/Last-single boundaries; the 12:09-12:11 outgoing pair at :277-279 is First/Last).
  Must be demonstrated failing against a swapped radius, per handoff §7.

**Constraints touched:** G3 brand — no new colours or keys; radius values reuse the existing
12/8/4 shape vocabulary. Three-channel delivery — untouched (cluster is a sibling, its
glyph/count/word table `BadgeFor` is not modified). Honesty — no copy changes. Motion —
none. Fixture — read-only.

## 2. Hover / press / selection without fill repaint

The hard problem is already solved structurally: `UrBubbleButtonStyle` never lets a visual
state touch `Root.Background`/`Root.BorderBrush`; hover is the `EdgeLayer` overlay
(#38FFFFFF 1px), press adds `Root.Opacity 0.92` — a dim, not a repaint, so the direction fill
survives (App.xaml:776-813). Refinements, cheapest first:

1. **Press timing.** One blanket `VisualTransition GeneratedDuration="0:0:0.15"` today.
   Replace with explicit transitions: `To="Pressed"` at `0:0:0.09` (kMicroMs — press should
   feel instant), `From="Pressed"` and PointerOver↔Normal at `0:0:0.15` (kFastMs). Pure XAML;
   stock VSM transitions are explicitly the platform's business (UrMotion.h:81-82), so no
   ShouldAnimate gate needed.
2. **Selection reflow fix.** `SetBubbleEdge` writes thickness 0 (incoming rest), 1 (outgoing
   rest), 2 (selected) — so selecting shifts bubble content 1-2px. The style's own comment
   (App.xaml:750-752) says both directions should rest at 1 with a Transparent brush for
   exactly this reason; the local override to 0 defeats it. Fix: incoming rest = thickness 1,
   brush Transparent. Residual 1px shift on selection only, now uniform across directions.
   One line in `SetBubbleEdge`.
3. **Optional: selection crossfade.** Animate `(BorderBrush).(SolidColorBrush.Color)` to/from
   `kAccent` at kFastMs instead of a hard swap. Requires `SetBubbleEdge` to keep one
   SolidColorBrush instance per bubble and a ColorAnimation helper (UrMotion has only the
   double spline today — `MakeSplineColor` would be new code in UrMotion.h/.cpp, gated by
   ShouldAnimate). Low payoff; do last or skip.

**Constraints touched:** the no-fill-repaint rule (all feedback stays on overlay/opacity/
border); accent reservation (selection outline only — unchanged); motion tokens (only
kMicroMs/kFastMs spent).

## 3. Composer: a resting surface with depth

Today the bar is one tonal step (card on page) and the controls float in it. Restructure
into the palette's own layering — page → sheet → card, exactly as UrColors.h:18-23 prescribes:

```
Border composerBar                 // Background UrSheetBrush #151515 (was UrCardBrush)
                                   // BorderThickness 0,1,0,0, UrBorderBrush (unchanged)
                                   // Padding 12,8,12,10 (unchanged)
  StackPanel Spacing=6
    Grid                           // the WELL: one element, one child overlay
      Border inputWell             // Background UrCardBrush #1C1C1C, CornerRadius 12,
                                   // BorderBrush UrBorderBrush, BorderThickness 1,
                                   // Padding 6,2,6,2
        Grid (5 cols, unchanged)   // attach, emoji, 24h chip, TextBox, send
      Border focusEdge             // CornerRadius 12, BorderThickness 1,
                                   // BorderBrush #38FFFFFF, Opacity 0   <- overlay pattern
                                   // borrowed from UrBubbleButtonStyle's EdgeLayer
    TextBlock note                 // "Demo - nothing is sent..." - UNCHANGED (honesty line)
```

- **Focus behaviour:** retarget the existing `FadeFocusRule` from the bottom rule to
  `focusEdge` — same function, same durations (kFastMs standard in / kMicroMs exit out),
  same ShouldAnimate gate, zero new motion code. Delete the 1px `focusRule` element.
  The TextBox keeps its local `BorderThickness(0)` (ThreadView.cpp:789) so WinUI's
  blue `TextControlBorderBrushFocused` never draws — verify this stays true inside the well.
- **Send pill shape:** `send.CornerRadius(FromUniformRadius(16))` — a real pill against the
  12px well and chips. Keeps `AccentButtonStyle`, `IsEnabled(false)`, and the automation
  name. Accent stays inside its reservation (the send button).
- **24h chip:** keep disabled; optionally fill `UrCardHoverBrush` #242424 so it reads as a
  control seated on the well. Cosmetic, optional.
- **Enable motion (needs design §9.1 sign-off):** on `TextBox.TextChanged`, when text is
  empty render the pill glyph-only (no accent wash); when non-empty crossfade to today's
  disabled wash #33EFF7BB at kFastMs. It remains `IsEnabled(false)` in both states — never
  focusable, never clickable, always platform-drawn-disabled. Net effect vs today is *less*
  affordance when empty, identical when text present, so it cannot create "an
  enabled-looking button that eats a click"; it teaches the text→send relationship, which is
  the one honest thing the composer can express. If the coordinator rules §9.1 forbids it,
  drop this bullet only.
- The bar becoming #151515 also future-proofs the thread header: when wiring gives
  `ThreadHost` a header, `UrPaneHeaderStyle` is the same sheet fill — header and composer
  become matching bookends.

**Constraints touched:** G4 honesty — the note line and all disabled states/automation names
unchanged; accent reservation (send only); tonal ramp used as documented; motion tokens only.

## 4. Day separator and system rows

**Day separator — refine, don't reinvent.** The pill was a recorded decision ("a centred pill,
not a rule with text on it", ThreadView.cpp:485-489). Keep the pill; remove its 1px border (a
bordered pill competes with bubble edges; the fill alone lifts it off #101010), radius 10→8,
padding `(10,2,10,3)`→`(12,3,12,4)`, margin `(0,14,0,6)`→`(0,16,0,8)`. Text stays
`UrGroupHeaderTextStyle` (11px letterspaced muted) — the chrome voice the CONVERSATIONS/RECENT
strips already speak. *Alternative if the coordinator wants quieter:* bare centred label
flanked by two 40 dip `UrBorderBrush` hairlines at 8 dip gaps — but that reopens a settled
decision; only take it deliberately.

**System rows — restraint is the design.** Keep centred, 12px, `MutedBrush`, max-width 420
(ThreadView.cpp:512-531). Do NOT shrink to 11px/faint: "Disappearing messages set to 7 days"
is real state, and #5A5A5A at 11px on #101010 is ~3.3:1 contrast — fails legibility for
content text. Only change: margins `(0,10,0,10)`→`(0,8,0,8)` so the §1 row-margin rhythm
owns the gaps. No glyph: the fixture carries no icon field and matching on `systemText` words
is the banned pattern (Views/ThreadLayout.cpp:27-30). Honest low payoff — say so.

**Constraints touched:** none beyond rhythm — no colour, face, copy or honesty change.

## 5. Typing indicator

Wrap the dots in an incoming-shaped shell so the indicator reads as *a message
materializing*, and align it with the column it belongs to:

```
StackPanel row  Margin = (group ? 52 : 16), 0, 0, 6   // 52 = 16 scroller pad + 36 gutter;
                                                      // set in SetThreadConversation, which
                                                      // is where `group` is first known
  Border dotShell   // UrCardBrush, CornerRadius (12,12,12,4) — the incoming run-end shape
                    // from §1, Padding 10,7 (~28 dip tall)
    StackPanel dots // 3 x Ellipse 6px — UNCHANGED wave (kPulseMs/2, 140ms offsets,
                    // autoReverse, Forever; TypingTimelines untouched, T6 gate untouched)
  TextBlock "Typing…" 11px muted — UNCHANGED and kept OUTSIDE the shell: it is the
                    // reduce-motion channel and the screen-reader channel
```

The dots' storyboard logic does not change at all — only their container. The shell must not
get an identicon: the fixture has no typing-sender field and is immutable, so the indicator
stays anonymous by honesty, not by omission. Today's margin-left 20 (ThreadView.cpp:639)
aligns with nothing; in `thread.png` the dots float left of the incoming column.

**Constraints touched:** motion gate (inherited unchanged); honesty ("Typing" is the one
claim the demo can make about a fabricated participant — ThreadView.cpp:631-633); no new
colours; fixture read-only.

## 6. Delivery cluster: legibility without enlarging chrome

Keep 13px glyphs / 11px word — `BadgeFor` (Views/ThreadLayout.cpp:90-108) is gate-asserted
(`T5 delivery badges`, Startup.cpp:1119-1128) and E930/EC61 is documented as the font's only
distinguishable outline/filled pair at 13px. Legibility comes from rhythm, alignment and
motion instead:

1. **Bind the cluster to its bubble:** margin `(0,2,2,6)` → `(0,1,2,4)`; inter-run space moves
   to §1's row margins. In `thread.png` the cluster currently floats near-equidistant between
   its bubble and the next run — 1px above / larger below makes ownership unambiguous.
2. **Fade-in on appear (the design §7 "delivery morph", honest subset).** In `SetRowCluster`'s
   add branch, start the new cluster at Opacity 0 and run the existing spline helper to 1.0 at
   kFastMs/standard, gated by ShouldAnimate. Removal stays instant: the only removal event is
   a newer outgoing row arriving below, whose own bubble entrance is where the eye already is;
   an async fade-out + delayed `RemoveAt` would make a gate-treated-synchronous function
   stateful for no visible gain. A true Sent→Delivered morph never occurs in this build
   (ambient activity only appends; nothing mutates a rendered row's state) — say that in the
   ledger rather than building a cross-dissolve no fixture state can trigger.
3. **Failed cluster:** keep the reason line (11px danger, wrap, right, max 320) and the
   disabled "Try again" exactly as-is; only the margin change above applies. The mid-run
   Failed row (DemoWorld.cpp:277) is this surface's most important honesty artifact — no
   restyle risk taken there.

**Constraints touched:** three-channel delivery — all three channels preserved by not
touching the badge table; motion tokens; exits-faster rule (instant < kMicroMs).

## 7. Key-change record: keep the gravity, add structure

Non-negotiables preserved verbatim: 2px `UrDangerBrush` leading rule (a shape channel), the
`\uE192` key glyph in danger, the world's own copy at 12px off-white, disabled Review,
non-dismissible **by construction** (ThreadView.cpp:533-623 — no close control exists or may
be added). One addition: a header line above the copy — `PERMANENT RECORD` in
`UrGroupHeaderTextStyle` (11px, letterspacing 90), `DangerBrush`. It mirrors the automation
name ("Permanent record, cannot be dismissed. ", ThreadView.cpp:620-622), adds inscription
weight through the chrome voice, and makes no claim about crypto — it labels persistence,
which is the record's actual property. Optional flourish: seat the key glyph in a 24x24,
radius-6 chip of `WithAlpha(kDanger, 0x1A)` (helper exists at UrColors.h:82 — no new resource
key). Skip a full danger border: one red rule is a record; a red box is an alert banner.

## 8. Entrance-motion polish (existing tokens only)

1. **Direction-aware origin.** `RunBubbleEntrance` hard-codes `RenderTransformOrigin(0.5,1.0)`
   (ThreadView.cpp:104). Pass direction (AppendThreadRow knows `row.outgoing`; the stagger
   policy needs the signature anyway): origin `(0.0,1.0)` incoming / `(1.0,1.0)` outgoing —
   the 0.96→1.0 scale blooms from the speaker's side. Free: no new token, no gate change
   (the origin is not in the pure table — note that as a gate gap: the gate asserts the four
   timelines' endpoints but cannot see the origin; acceptable, comment it).
2. **Staggered entrance on conversation open.** Today `RunBubbleEntrance` runs only on
   append; a freshly built thread pops in whole, while the conversation list beside it
   staggers. Add pure `OpenStaggerBeginMs(bubbleRowCount)` → all rows 0 except the last
   `min(kMaxStaggerSteps 6, count)`, which get `kStaggerMs 40` x position (oldest→newest of
   the visible foot, so the newest settles last). Only the last 6 animate — rows above the
   fold animating invisibly would be waste, and 6 is the cap the list already uses.
   RenderTransform/Opacity don't affect layout, so the bottom-pin `stack.SizeChanged` handler
   is not re-triggered — no interaction with the W9 do-not-yank scroll work.
   **Gate impact, stated plainly:** `T6 bubble entrance` asserts `beginMs == 0` per timeline
   (Startup.cpp:1219). Change `EntranceTimelines(bool animate)` → `(bool animate, int64_t staggerMs = 0)`,
   keep the gate on the `staggerMs=0` shape, and add a second assertion on the staggered
   values. Demonstrate the new clause failing (set the step to 50) before shipping, per
   handoff §7.
3. **Reduce-motion caveat (inherits the project's known blind spot):** `ShouldAnimate()` is
   always true on this machine (`SPI_GETCLIENTAREAANIMATION = 1`), so the empty-plan branch of
   every new path here is unverified by execution — same status as the existing entrance,
   flagged at ThreadView.cpp:82-88. Coordinate with A2's motion override to eyeball it.

## 9. Explicitly NOT proposed

- **Bubble shadows / ThemeShadow:** the brand's depth system is the four-step tonal ramp, not
  elevation shadows; shadows on #101010 muddy it.
- **Display face anywhere in the thread** (day separators, sender headers): ABC Gravity is
  titles/wordmark only.
- **Delivery state inside bubbles** (Signal-style time+checks in the fill): violates the
  recorded sibling rule — the cluster is not part of the bubble's fill or click target
  (ThreadView.cpp:159-165).
- **Moving the timestamp inline after the last word:** WinUI flow layout can't right-align an
  inline; the stacked 11px faint time stays, tightened by §1's rhythm.
- **Tails / notches:** evaluated in §1 and rejected — a second painter of the direction fill
  breaks press-dim, the selection outline, and the one-writer edge invariant.
- **Any new App.xaml resource key, any new colour, any new motion token.** Every brush,
  duration, curve and helper above already exists (`WithAlpha` covers the one derived alpha).

## 10. Constraint ledger

| Proposal | G4 honesty | G3 brand | Three-channel delivery | Motion tokens | Fixture |
|---|---|---|---|---|---|
| §1 run geometry | no copy change | radii reuse 12/8/4 vocabulary; fills unchanged | untouched (cluster stays a sibling) | none | read-only |
| §2 hover/press/selection | none | accent stays selection-outline-only | none | kMicroMs/kFastMs only | n/a |
| §3 composer well | note line + all disabled states/automation names unchanged | accent stays send-only; sheet→card ramp as documented | none | kFastMs/kMicroMs via existing FadeFocusRule | n/a |
| §4 separator/system rows | none | no new keys; chrome voice kept | none | none | read-only |
| §5 typing shell | "Typing" claim unchanged; anonymous (no sender field exists) | UrCardBrush only | none | existing TypingTimelines untouched | immutability is WHY it stays anonymous |
| §6 cluster rhythm + fade | Failed row's reason/retry untouched | none | badge table `BadgeFor` untouched — all three channels preserved | kFastMs add-fade; instant remove | read-only |
| §7 record header | labels persistence, never crypto; no dismiss control added | DangerBrush + existing chrome style | rule/glyph/word channels kept | none | copy stays the world's |
| §8 entrance polish | none | none | none | tokens only; gate change stated | read-only |

## 11. File-level change inventory

| File | Change |
|---|---|
| `app/src/App/Views/ThreadLayout.h/.cpp` | `BubbleRunPos`, `RunPosFor`, `BubbleCornerDip`, `GapAboveDip`, `OpenStaggerBeginMs`; `ThreadRowPlan.runPos`; `EntranceTimelines` stagger param. Pure — diagnosable. |
| `app/src/App/Views/ThreadView.cpp` | per-row CornerRadius + margins; name-out-of-bubble; composer well restructure (MakeComposer, FadeFocusRule retarget); typing shell + group-aware margin; cluster margin + add-fade; record header line; entrance origin/stagger. |
| `app/src/App/App.xaml` | UrBubbleButtonStyle: explicit press/hover transition durations only. No new keys. |
| `app/src/App/Startup.cpp` | new pure gates: run-geometry table, stagger begins; keep T5 badge gate untouched; update T6 entrance gate for the new signature (demonstrate failing). |
| `app/src/App/UrMotion.h/.cpp` | ONLY if §2.3 selection crossfade is taken (`MakeSplineColor`). |
| `Demo/DemoWorld.cpp` | **untouched** (immutable). |

## 12. Payoff vs risk — suggested landing order

1. **§1 run geometry + §4 margins** — the biggest visual transformation (runs become visible
   objects); medium risk: planner + one new gate, template-safe. Screenshot `--demo=thread`.
2. **§3 composer well** — high payoff, self-contained, low risk; deletes nothing but the
   focusRule element. Screenshot the focus state via `--demo=inspect` (the box is clickable).
3. **§6 cluster rhythm + add-fade** — the thread's information-dense corner, made findable;
   low risk, gates untouched.
4. **§5 typing shell + alignment** — medium payoff, low risk; verify with
   `--demo=thread --demo-autoplay`.
5. **§2.1/2.2 press timing + selection metrics** — tiny diffs, immediate feel; XAML/one-liner.
6. **§7 record header** — small gravity win; review the wording once (must mirror the
   automation name).
7. **§8 entrance origin + open stagger** — delightful but touches a live gate and the
   unverifiable-here reduce-motion path; land after A2's motion override exists.
8. **§3 enable-motion bullet** — only with explicit design §9.1 sign-off; otherwise omit.
9. **§2.3 selection crossfade** — optional polish; skip unless everything above is quiet.

---

*Verified two facts the coordinator may want to propagate: `ThreadRowPlan::endsOutgoingRun`
is consumed by no renderer (only gates read it), and the thread pane currently has no header
host — `ThreadHost` is a bare UrPaneStyle Grid (MainWindow.xaml.cpp:337-347), so any
thread-pane chrome proposal must coordinate with the wiring group that owns the header.*
