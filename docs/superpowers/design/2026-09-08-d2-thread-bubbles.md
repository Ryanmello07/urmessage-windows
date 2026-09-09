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
