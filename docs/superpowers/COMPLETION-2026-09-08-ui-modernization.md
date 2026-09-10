# Demo UI modernization — completion record

Written 2026-09-08 by the design-swarm coordinator that took over from the HANDOFF-2026-09-08
agent. Companion to `HANDOFF-2026-09-08-ui-design-swarm.md` (read that first for the rules).

## What shipped

25 of 46 plan tasks were done at takeover. The remaining 21 (R4, N1–N6, S1–S4, A2–A6, W5–W9)
all landed, plus a full visual modernization, across 11 reviewed waves. Every wave: build green,
`--diagnose` green (42 PASS/0 FAIL at takeover → **57 PASS/0 FAIL** final, all new gates
demonstrated failing first), screenshot-verified, committed with `-c user.name="Ryanmello07"`,
pushed to `origin demo-ui`.

| Wave | Commit | Content |
|---|---|---|
| stub fix | 0ca2665 | unbuilt Network/Settings/Developer routed to StubPage (was: blank pane hazard) |
| design docs | 6314928, 90ecf72 | seven design proposals under `docs/superpowers/design/` (d1–d7) |
| 1 | 04dc390 | depth model (rail→sheet), bubble press scale, selection draw-on, composer focus seam, typing entrance + alignment, mini-identicons, proportional identicon radius (P5 gate) |
| 2 | 7ebd6a9 | run-shaped bubbles (T7 gate), run rhythm 2/10dip, sender names out of bubbles, composer well, day separators, cluster add-fade, PERMANENT RECORD header, open stagger (T6 gate) |
| 3 | 00989e9 | nav rule + search slab, search Esc/count/empty-state lattice, entrance rise, hover bar/plate/press, timer-row label, InfoBadge, list width step 320→360@1200 (gate) |
| 4 | a63571c | rail carded redesign + R4: device lists (DELIVERED TO/READ BY), presence rows with expandable devices, ADVANCED density switch, InitialRailMode probe |
| 5 | 3915127 | Network page (N1–N6): relay hero, framed server group, YOUR DEVICES, Advanced hop labels + travelling packet, SetMotionOverride hook (6 `net *` gates) |
| 6 | 44554e9 | status strip + drawer (S1–S4): 26dip strip, state words, halo/pulse, framed padlock, drawer with mini relay path (`strip fields` gate) |
| 7 | 212057e | A2/A3/A4: session switches, motion-off render executed for real, Settings page, no-persist gate for --demo-advanced |
| 8 | 9d0d218 | A5/A6: Developer page, framed world dump (cipher omitted forever), mono key |
| 9 | b576f4a…b596740 | W5 click graph (BuildDemoViews sole builder; mount_check exit 1→0), W7 the one ApplyAdvanced subscriber (+Developer nav insert/remove fix), W8 ambient module (gates), W9 scroll pin (ShouldPinToBottom pure, pinExtent deviation documented) |
| polish A | 249658f | thread header (d1 §1.4), thread empty state, typing shell, send-pill empty state, composer seam dedupe, switch ON re-theme (blue→off-white) |
| polish B1 | ae95495 | Network hero rescale + carded groups + header connect word + column cap; Settings/Developer width caps, scroll clearance, dump 12px |
| polish B2 | b181cfe | rail: subject hairlines, caption-meta alignment, device identicons seeded from member identity (26/26, gate), middots, offline ring, DETAILS chrome voice, scroller peek; list/nav: badge stadium, timer-glyph dedupe, lattice alpha, RECENT tone, nav seam |

## Settled decisions a future agent should not re-litigate

- **Strikethrough scare:** the failure reason line does NOT render struck-through; the 4x-crop
  "line" was a bicubic-upscale artifact. Pixel-map evidence in `.verify-polishA/_crops/`.
- **Delivery cluster alignment** was already flush with the bubble edge (reviewer mis-measured
  the row's trailing margin). Unchanged by design.
- **Strip padlock stays muted** (d5 §4.1, framed name beside it); **rail padlock stays green**
  (framing strings beside it); **server key stays green** with `Demo model: verified` (audit N4
  override — words carry the framing). Do not "harmonize" these.
- **RETENTION vs thread pill** ("Kept until deleted" beside a disappearing conversation) is
  fixture reality read honestly by both surfaces. The fixture is immutable; do not reconcile
  by editing either surface's read.
- **Avatar top-aligned** at run-starts (pairs with the sender name) — deliberate.
- **Accent caret/focus micro-extensions** (caret brush via `TextControlCaretBrush` override —
  WASDK 2.2 has no `CaretBrush` projection) were approved under the scarcity principle: point
  marks only, never fills, never large areas.
- The **toggle ON state** is off-white #E8E8E8 + page-dark knob. The old #638BFC "brand blue"
  was off-palette and is gone; do not bring blue back.

## Verification blind spots (stated, not hidden)

- `SPI_GETCLIENTAREAANIMATION = 1` on this machine: reduce-motion branches of pre-A2 surfaces
  executed only via temporary `SetMotionOverride(false)` probes (reverted before commit). The
  hook ships in UrMotion.
- No input synthesis, ever: hover/press/click states are code-inspection + property-write
  verification only. The drawer toggle, search focus, and pill TextChanged path are proven
  through deep links and temporary property writes, not pointer input.
- `--demo=chats` launches with NO selection by construction (DemoDeepLinkCheck); the thread
  shows the lattice empty state. The 1200×800 list-step probe is content-dip based (fires at
  window ≈1214px, not 1200).

## Where things live

Design library: `docs/superpowers/design/2026-09-08-d{1..7}-*.md`. Audit with per-task
overrides: `docs/superpowers/plans/2026-09-08-remaining-task-audit.md`. Ledger (gitignored):
`.superpowers/sdd/2026-09-06-urmessage-demo-ui/progress.md`. Capture sets: `.verify-baseline/`,
`.verify-wave{1..9}/`, `.verify-polish{A,B1,B2}/`, `.verify-ship/` (all gitignored).
