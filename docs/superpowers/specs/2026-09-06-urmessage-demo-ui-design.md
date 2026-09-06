# URmessage Windows — demo UI design

**Date:** 2026-09-06
**Status:** design, approved for planning
**Repo:** `urnetwork/message-windows`, branch `main`
**Supersedes for this work:** nothing. **Defers:** `msgrepo/docs/specs/2026-08-12-spec-c-windows-client-ui.md` (Spec C)

---

## 1. Why this document exists

Spec C is a 1,893-line normative specification of the *shipping* Windows client: 32 screens,
onboarding, key-change modals, installer, retention negotiation. It was written 2026-08-12 and is
the right document for the product.

It is the wrong document for **this** job, and using it as the working spec is how this work loses
focus. This document is scoped to one deliverable and one audience:

> **A UI demo of URmessage that the owner can open, click through in front of a team or an
> investor, and capture from — while no message protocol exists.**

Spec C remains the reference for anything this document does not decide — palette, row anatomy,
delivery-state vocabulary, the closed set of message states. Where this document differs from
Spec C it says so explicitly and says why. Nothing here amends Spec C.

**Success is not "it compiles."** Success is that the owner opens the app, clicks through it, and
it reads as a finished, modern, unmistakably-URnetwork encrypted messenger — with every surface
responding to a click and nothing on screen that looks live but isn't.

---

## 2. What this is not

Stated first, because every one of these is a thing a reader could reasonably assume is included:

- **No protocol, no store, no network, no cryptography.** Nothing is sent, received, stored,
  encrypted or decrypted. Every value on screen is fabricated in one module.
- **No onboarding stack.** No Welcome, no seedphrase display or confirmation, no restore, no
  device linking. Those are Spec C screens 1–8 and are the least differentiated part of the
  product.
- **No installer, updater, tray, notifications, or localization work.**
- **No SDK integration.** `URmessageSdk.dll` does not exist and is not referenced.
- **No claim of security.** The demo shows what the UI *will* say about encryption. It does not
  demonstrate encryption, and no copy in the demo may state that a message *was* encrypted as a
  fact about a real operation.

The last one is a hard constraint, not a preference. It is the single way a UI demo can mislead an
investor, and it is cheap to honour: the inspector shows fabricated values with the same fidelity
it will one day show real ones.

---

## 3. Decisions

| # | Decision | Rationale |
|---|---|---|
| **D1** | **Core loop, deep.** Conversation list, thread, inspector rail, Network page, status strip, Settings + Advanced Mode, Developer menu. Roughly seven surfaces, each finished. | An audience judges polish and differentiation, not screen count. Breadth at low fidelity reads as unfinished everywhere rather than excellent somewhere. |
| **D2** | **Plausible demo data, lightly watermarked.** Real-feeling names, groups and history. A small `DEMO` chip in the title bar, removable with `--demo-watermark=off`. | The owner presents from patched screenshots and does not distribute the binary, so the watermark is a safety net, not a design element. It exists so an unpatched screenshot cannot be mistaken for a shipping product. |
| **D3** | **Inspector rail.** Selecting a message swaps the right rail from conversation details to that message's cryptographic detail. | Keeps the thread and the evidence in one frame. It is the Matrix/Element pattern, it is the most demo-legible thing the app can do, and — since input may not be synthesised — a rail populated at launch is screenshottable where a modal is not. |
| **D4** | **Connect indicator + Network page.** A non-intrusive status strip (state, server, lock), a preview drawer on activation, and the full node map only on the Network page. | Matches the desktop reference class already fixed for this product line (ProtonVPN / Portmaster; persistent bottom strip). Explicitly **not** a permanent network readout — the owner ruled the strip must not be intrusive. |
| **D5** | **Expressive motion.** Spring entrances, 40 ms row stagger, breathing typing indicator, delivery-state morph, pulsing connect dot. | Every animation carries a state change rather than decorating one. The delivery morph in particular teaches Spec C §5.3's delivery model, which is the subtlest thing the product has to say. |
| **D6** | **Cinematic relay animation is quarantined to the Network page, behind Advanced Mode.** | A packet visibly flying the relay path on send animates a protocol that does not exist. On a diagram an audience reads it as a diagram; attached to a send it reads as a claim. This is the one place the demo could overstate what is built. |
| **D7** | **Demo mode opens at 1560×900 DIP; a normal launch keeps Spec C §1.2's 1100×760.** | The third pane exists only at ≥1500 DIP (§1.3). The rail must be live on launch for screenshots and video. The spec default is untouched for non-demo launches. |
| **D8** | **View modules, not a growing `MainWindow`.** | Spec C §0.2 W1 records that the VPN client's `MainWindow.xaml.cpp` reached 2,128 lines and became the collision point for parallel UI work. Ours is 219 lines and this work adds five surfaces. |

---

## 4. Architecture

```
app/src/App/
  MainWindow.xaml(.cpp/.h)     thin composer: breakpoint, nav, hosts the views
  Demo/
    DemoWorld.h/.cpp           the entire fabricated world, one seeded generator
    DemoAutoplay.h/.cpp        the scripted sequence and its timer
    DemoSwitches.h/.cpp        --demo parsing, in the shape of WantsDiagnose()
  Views/
    ConversationListView.*     the list pane
    ThreadView.*               bubbles, day separators, system rows, composer
    InspectRailView.*          conversation details <-> message inspect
    NetworkPageView.*          relay path, server, devices
    StatusStripView.*          connect indicator + preview drawer
    DeveloperView.*            the Advanced-Mode-only surface
  Identicon.h/.cpp             deterministic identicon from a byte string
```

**Boundaries.** Each view owns one surface, takes its data from `DemoWorld` through a small
struct, and exposes one `FrameworkElement Build()` plus whatever state setters it needs. No view
reads another view's internals. `MainWindow` wires them and owns the breakpoint decision, which
stays a single window-level function per Spec C §1.3 (`AdaptiveTrigger` is unusable in WinUI 3
desktop and `VisualStateGroup`s on a layout `Grid` are never processed — both are measured facts
recorded in `UrComponents.h`).

**Why `DemoWorld` is one module.** The day a protocol arrives, the diff that deletes the demo is
one directory and one switch. The current scaffold already establishes this instinct — its sample
rows sit in a single named block with a comment explaining why — and this preserves it at a larger
scale.

**Reuse, not reinvention.** `UrComponents.h` already provides pane rows, group headers, table
rows, search rows, empty states, dividers, section headers and a status-field builder. New
component work is limited to what genuinely does not exist: bubbles, identicons, the inspector
key/value rows, the relay-path element, and the status strip.

---

## 5. The demo world

One module, one **seeded** generator, no runtime randomness. Identical pixels on every launch is
what makes a screenshot diff meaningful and a re-recorded video cut together.

**Contents.**

- **8 conversations** — 2 groups (`Design team`, 5 members; `URnetwork core`, 11 members) and 6
  direct messages. One DM is `EPH`-class so its list row shows the disappearing-timer glyph
  *instead of* a preview, per Spec C §4.2. One is muted. Two carry unread pills.
- **~40 messages** across the two open conversations, with realistic cadence — bursts, gaps, a
  reply, one attachment card, one reaction.
- **System rows**, because these are what make it read as a secure messenger rather than a chat
  mock: a member-added line, a disappearing-timer change, and a permanent key-change record
  (Spec C §7.4 — non-dismissible).
- **Delivery states.** At least one message in each of `sent`, `delivered`, `read`, and one
  `failed` with its retry affordance. The closed set is Spec C §5.3's and is not extended.
- **3 linked devices** — this computer, a phone seen 2 minutes ago, a laptop seen 3 days ago.
- **1 message server** with host, jurisdiction and latency.

**Names.** Plausible and neutral. No real people, no employees, no recognisable handles.

**Identicons (Spec C §W9).** Deterministic, derived from a fabricated 32-byte identity key per
contact and from a group id per group; 40×40, corner radius 8. This is both the spec's rule and
the demo's avatars for free, and it sets up the property that a contact's avatar visibly changes
when their key changes.

---

## 6. Surfaces

### 6.1 Conversation list

Spec C §4.1 row anatomy, which the current build does not yet implement: identicon, display name,
one-line ellipsized preview, right-aligned relative timestamp, unread pill in `UrAccentBrush` with
`UrInverseTextBrush` text, muted and disappearing glyphs. Search row above.

**A row never carries a delivery-state colour** (§4.1). Delivery is a glyph in the thread.

### 6.2 Thread

- **Bubbles** per §5.2: incoming `UrCardBrush` `#1C1C1C`, outgoing `UrCardHoverBrush` `#242424`
  with a 1 px `UrBorderBrush`, max width 68% of the column capped at 640 DIP, body face only.
  `UrAccentBrush` is **never** a bubble fill — it is the send button and the selection outline.
- **Day separators** and centred, muted, non-bubble system rows.
- **Delivery glyphs** right-aligned under the last outgoing bubble of a run: clock, one check, two
  outline checks, two filled checks, and `UrDangerBrush` exclamation for failed.
- **Composer** with attachment, emoji and disappearing-timer chip. Non-functional; it renders and
  animates focus, and does not accept a send in the demo.

### 6.3 Inspector rail (D3)

Two modes in one 360 DIP rail.

**Conversation mode** (default): members with presence dots, retention, media policy.

**Message mode** (a bubble is selected): a lock header, then the fields Spec C §12 defines —
sender, sent time, received time, **epoch**, **sender leaf index**, **retention class**, size
bucket, **attestation state** — and then the **delivered-by** and **read-by** device lists.

That device list is the load-bearing idea. Spec C §5.3: *"Delivered is a statement by a device,
never by the server."* Showing which devices decrypted a message, by name, is a claim most
messengers cannot make, and it is the natural home for the "connected clients" idea.

The transition between modes is a crossfade at `kBaseMs`, not a slide — the rail's width is fixed
and a slide would imply navigation that has not happened.

### 6.4 Network page (D4)

A fourth nav destination:

- **Relay path** — this device → URnetwork → message server, as three nodes and two wires.
  Static in Normal mode; the wires animate only under Advanced Mode (D6).
- **Message server** — host, jurisdiction, latency, key-verification state.
- **Your devices** — the three linked devices with last-seen and a remove affordance.

### 6.5 Status strip (D4)

A 26 DIP strip along the bottom. **Normal:** state dot, state word, server host, a lock glyph.
That is all — it is a connect indicator, not a network readout. Activating it raises a preview
drawer with the node list. **Advanced Mode** adds epoch, session mode and records/s inline,
mirroring the VPN client's two-density strip.

**Collapse rule.** The strip is hidden below 560 DIP of window height so it can never eat a
readable thread at Spec C §1.2's 480 DIP minimum.

### 6.5a Below 1500 DIP, in the demo

Demo mode opens at 1560 and nothing forces it smaller, but a user can still drag the window
narrow. Spec C's answer at that width is a `ContentDialog` sheet (§1.3), which this work does not
build (§10). So the demo's behaviour is defined instead of left to chance:

- Below 1500 DIP the rail collapses, exactly as the breakpoint requires.
- Selecting a message then does nothing visible, and **message inspect is unavailable** until the
  window is widened again.
- No sheet, no fallback, no error. A narrowed demo window is a smaller demo, not a broken one.

This is a deliberate demo limitation, not a defect, and it is the reason D7 fixes the launch size.

### 6.6 Settings, Advanced Mode, Developer

Settings renders enough groups to look real, with Appearance, Privacy and Security populated and
the rest present but inert. **Advanced Mode** is a single persisted toggle using the
`advanced_mode` key already established in `Common/AppPrefs.h`. It is not a destination — it
changes what existing surfaces show:

| Surface | Normal | Advanced |
|---|---|---|
| Status strip | dot, state, server, lock | + epoch, session mode, records/s |
| Inspector rail | labelled fields | + raw ids, hex group id, leaf index, wire size |
| Conversation list | name, preview, time | + group id chip |
| Network page | static relay path | + animated wires, per-hop timings |
| Nav | Chats, Contacts, Network, Settings | + **Developer** |

**Developer** carries a demo-state inspector, an autoplay transport (play/pause/step), a motion
switch, and a `DemoWorld` dump. It is the honest place to put things that would otherwise tempt
their way into the product surfaces.

---

## 7. Motion (D5)

All motion goes through `UrMotion.h`'s existing tokens; this work adds no new duration or curve.

| Element | Token | Behaviour |
|---|---|---|
| Bubble entrance | `kBaseMs`, standard curve | fade + 10 DIP rise + 0.96→1.0 scale |
| List row reveal | `kStaggerMs` 40, max 6 steps | staggered entrance |
| Rail mode swap | `kBaseMs` | crossfade |
| Delivery morph | `kFastMs` | glyph cross-dissolve on state change |
| Typing indicator | `kPulseMs` | three dots, 140 ms phase offset |
| Connect dot | `kPulseMs` | expanding ring, `kUrGreen` at decreasing alpha |
| Page change | existing `CrossfadePageSwap` | unchanged |
| Window open | existing `WindowReveal` | unchanged |

**Every animation is gated on `motion::ShouldAnimate()`.** "Show animations in Windows = off"
means motion is gone, not reduced, and the demo must be correct and instant in that state.

---

## 8. Launch switches

Parsed in the shape of the existing `WantsDiagnose()` — `CommandLineToArgvW`, accepting `--x`,
`-x`, `/x` and bare `x`.

| Switch | Effect |
|---|---|
| `--demo` | Enable the demo world. Window opens 1560×900 (D7). Without it the app behaves exactly as it does today. |
| `--demo=<screen>` | Deep-link straight to a surface: `chats`, `thread`, `inspect`, `network`, `settings`, `developer`. Implies `--demo`. `inspect` is not a separate screen — it opens `thread` with a specific message pre-selected and the rail already in message mode, which is the state a screenshot needs. |
| `--demo-autoplay` | Enable ambient activity (§9.2) — occasional typing indicator and incoming message. Off by default; never moves the selection or the destination. Implies `--demo`. |
| `--demo-advanced` | Start with Advanced Mode on, without writing the preference. |
| `--demo-watermark=off` | Suppress the `DEMO` chip for clean capture. |

`--demo=<screen>` is what makes verification possible at all: it reaches every surface without
synthesising a single click.

**Autoplay sequence** (~28 s, loops): thread visible → typing indicator appears → an incoming
message springs in → the list reorders → an outgoing message sends and morphs
queued→sent→delivered→read → a bubble is selected and the rail swaps to message inspect → the
status strip drawer opens showing nodes → the Network page → back to the thread.

---

## 9. Interaction and verification

### 9.1 The deliverable is an app the owner drives

**The demo is operated by a person clicking it, not by a script.** The owner runs the build,
clicks through it, and captures whatever screenshots or video they want for a presentation
themselves. This document produces **no screenshot set, no video file, and no recording tool.**

That makes full interactivity a first-class requirement rather than a nice-to-have. Everything the
demo shows must be reachable by clicking:

| Action | Result |
|---|---|
| Click a conversation row | Opens that thread; the rail shows conversation details |
| Click a message bubble | Rail swaps to message inspect for that message; bubble takes the selection outline |
| Click empty thread space | Deselects; rail returns to conversation details |
| Click a nav destination | Chats / Contacts / Network / Settings, and Developer under Advanced |
| Click the status strip | Raises and dismisses the node preview drawer |
| Toggle Advanced Mode | Every affected surface re-renders live, without a restart |
| Click a failed message | Shows its reason and a `[ Try again ]` affordance |
| Resize the window | Crosses the 1500 and 1000 DIP breakpoints correctly |

Anything on screen that cannot be clicked must be visibly inert — no affordance that looks live
and does nothing. A dead-looking button is a demo bug.

### 9.2 Live activity, not a scripted tour

`--demo-autoplay` is reduced from a scripted run-through to **ambient activity**: while the demo
is open, a typing indicator occasionally appears and an incoming message arrives, so the app feels
alive while the owner is talking over it. It never moves the selection, never changes destination,
and never fights the person driving. Off by default; the Developer surface can pause it.

This exists so a presenter can show typing indicators and the delivery-state morph without needing
a second device — not to replace clicking.

### 9.3 How the agent verifies its own work

Separately, and for the agent's benefit rather than as a deliverable: after every substantive UI
change, build with `app/tools/build-local.ps1`, launch with the relevant `--demo=<screen>`,
capture with `app/tools/verify-render.ps1`, and *look at the image*. CI-green and "it launched"
are not UI verification — a process that paints garbage still exits 0.

`verify-render.ps1` gains an `-Args` passthrough so it can reach the deep-linked states. It
already handles the traps that matter: PerMonitorV2 DPI, `EnumWindows` + `GetClassNameW` rather
than `FindWindow`, process selection by executable path, and screen capture rather than
`PrintWindow`.

`--demo=<screen>` therefore serves two purposes — the agent's only way to see a given state, since
it may not synthesise input, and a convenience for jumping straight to a surface when presenting.

---

## 10. Relationship to Spec C

This document **defers** to Spec C and amends nothing in it. Where it narrows Spec C, it narrows
scope only:

| Spec C | Here |
|---|---|
| 32 screens | 7 surfaces (D1) |
| Default window 1100×760 | Unchanged for normal launch; 1560×900 under `--demo` only (D7) |
| Details are a `ContentDialog` sheet below 1500 DIP | Demo runs at 1560, so the rail is always available; the sheet fallback is not built |
| Message info is screen 12 | Rendered as the rail's message mode rather than a separate screen (D3) |
| Sample data must be obviously fake | Plausible + removable watermark (D2), because the audience changed |

Two things Spec C requires that this work **implements for the first time**: §4.1's identicon row
anatomy, and §W9's deterministic identicons.

---

## 11. Risks

- **Overstating what exists.** Mitigated by §2's hard constraint, D2's watermark and D6's
  quarantine of the relay animation.
- **Scope creep back toward Spec C.** Mitigated by this document existing at all, and by §2
  naming the omissions explicitly rather than leaving them to be discovered.
- **`MainWindow` regrowth.** Mitigated by D8's module boundaries. If any view file passes ~600
  lines it is a signal to split, not to continue.
- **Fonts are Latin-only.** Spec C flags this for real message content. Demo data is Latin, so it
  does not bite here, and it must not be presented as solved.
- **The app icon is still the VPN client's globe.** It is a brand error in the taskbar, in
  alt-tab, and beside the wordmark in every screenshot — `Assets/README.md` already flags it as
  "replace before anyone outside the team sees a screenshot", and a demo is exactly that.
  **No URmessage mark exists**, so this cannot be closed by implementation alone: it needs either
  a supplied asset or agreement to ship a simple generated placeholder that is at least not the
  VPN globe. Raised as an open question rather than assumed (§12).

---

## 12. Open questions

These are the things this design deliberately does not decide, because the answer is the owner's.

1. **The app icon.** No URmessage mark exists. Options: supply one, or accept a generated
   placeholder for the demo. Until then every screenshot carries the VPN client's globe, which
   `Assets/README.md` already calls a brand error. This is the one open item that is visible in
   the deliverable itself.
2. **Font licensing, unchanged and unresolved.** The four brand faces are commercially licensed
   and were assessed for one product. Building and screenshotting is not distributing, so this
   does not block the demo — it blocks a release. Recorded here only so it is not rediscovered.
3. **Non-Latin coverage.** All four faces are Latin-only. Demo data is Latin so it does not bite,
   and it must not be presented as solved.
