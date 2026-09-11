// The thread's layout DECISIONS, as pure data.
//
// PURE C++. No winrt/ include here, and none may be added: CollectDiagnostics()
// calls PlanThreadRows() from wWinMain BEFORE winrt::init_apartment(), which is
// the same reason DemoWorld.h is pure (CONTRACT-V2 §1). Keeping the decisions
// here and the pixels in ThreadView.cpp is what makes a --diagnose assertion
// about this surface possible at all; a builder that decided inline could only
// ever be checked by looking at it.
//
// NOT the same header as Demo/ThreadLayout.h. That one holds T1's row RULES
// (ShowsSenderHeader, DeliveryWord, AuditDaySeparators, ...); this one holds
// the row PLANNER. BOTH are namespace urmsg::views - only the PATH differs.
// Do not write demo::ShowsSenderHeader: it does not compile, and that mistake
// has already been made once. Both are always included WITH their directory prefix -
// "Demo/ThreadLayout.h" and "Views/ThreadLayout.h" - and $(MSBuildProjectDirectory)
// is on AdditionalIncludeDirectories (App.vcxproj:125), so the two resolve
// unambiguously. Do not merge them: one is what the demo world means, the
// other is what this surface draws.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Demo/DemoWorld.h"
#include "Demo/ThreadLayout.h"  // the ROW RULES (also urmsg::views, also pure)

namespace urmsg::views {

enum class ThreadRowShape {
  IncomingBubble,
  OutgoingBubble,
  DaySeparator,
  SystemLine,             // centred, muted, NOT a bubble (design §6.2)
  SystemPermanentRecord,  // Spec C §7.4: 2px UrDangerBrush left rule, non-dismissible
};

// ---- run-shape geometry (design d2 §1) -------------------------------------
// A THIRD notion of "run", beside showSenderHeader (same SENDER, delegated to
// ShowsSenderHeader in Demo/ThreadLayout.h) and endsOutgoingRun (same
// DIRECTION, a position hint no renderer consumes). This one is the GEOMETRIC
// run a reader's eye actually uses: same speaker, adjacent Message rows,
// nothing between. Its boundary is exactly where the sender header and
// identicon already appear, which is why the corner language and the header
// rule can never disagree about where a run starts.
enum class BubbleRunPos { Single, First, Middle, Last };

// Both Message rows, same direction, and — incoming only — the same senderKey.
// Outgoing rows are one speaker ("You"), so outgoing runs unify: the same
// assumption the direction-only code already makes everywhere else.
// Symmetric, so the "is the row above/below in my run" questions share one
// predicate.
inline bool ContinuesBubbleRun(demo::MessageRow const& a, demo::MessageRow const& b) {
  if (a.kind != demo::RowKind::Message || b.kind != demo::RowKind::Message) return false;
  if (a.outgoing != b.outgoing) return false;
  if (a.outgoing) return true;  // one speaker: "You"
  return a.senderKey == b.senderKey;
}

// Where `cur` sits in its geometric run. `prev`/`next` are the rows
// immediately above/below in Conversation::rows, or nullptr at an end. A day
// separator or a system row breaks a run (ContinuesBubbleRun is false for
// one), which is exactly what DemoWorld does when it clears previousSender.
BubbleRunPos RunPosFor(demo::MessageRow const* prev, demo::MessageRow const& cur,
                       demo::MessageRow const* next);

// The corner table, as TL,TR,BR,BL in `out`. Base radius 12 — the card radius
// used app-wide — with the corners that face an adjacent same-speaker bubble
// tightened to 4. The tightened corner is always on the spine (left for
// incoming, right for outgoing); Single stays fully round so DMs, which are
// almost all singles, do not get a harsh look. Asymmetric corners instead of
// tails because they flow through UrBubbleButtonStyle's CornerRadius
// template-binding to EdgeLayer and SelectEdge with no template fork — a tail
// would be a second painter of the direction fill and would break press-dim,
// the selection outline and the one-writer edge invariant (d2 §1).
void BubbleCornerDip(BubbleRunPos runPos, bool outgoing, double out[4]);

// The vertical rhythm that replaced stack.Spacing(6): 10 above a run
// start/single, 2 above a continuation, so runs read as blocks. Non-bubble
// rows keep their own margins and never consult this.
double GapAboveDip(BubbleRunPos runPos);

struct ThreadRowPlan {
  std::size_t rowIndex = 0;      // index into Conversation::rows
  ThreadRowShape shape = ThreadRowShape::SystemLine;

  // The GEOMETRIC run position above, computed for Message rows in the
  // PlanThreadRows loop from the same prev/next the loop already knows.
  // Meaningless (and left Single) on every other shape — day separators,
  // system lines and the record draw no bubble corners.
  BubbleRunPos runPos = BubbleRunPos::Single;

  // First bubble of a run, INCOMING, in a GROUP. Delegated verbatim to
  // ShowsSenderHeader() in Demo/ThreadLayout.h, so there is exactly ONE
  // definition of "starts a run" for the sender name in this app. That rule
  // breaks a run on the SENDER (it compares senderKey); computing it here from
  // direction alone would merge Mira-then-Tobias into one incoming run and drop
  // Tobias's name - 5 bubbles in the shipped world, gated by "T4 sender
  // headers" in CollectDiagnostics().
  bool showSenderHeader = false;

  // Last MESSAGE row of a same-DIRECTION outgoing stretch. A position hint, and
  // deliberately a different rule from the one above.
  //
  // T5: DO NOT hang the delivery cluster on this field. Use
  // CarriesDeliveryGlyph() from Demo/ThreadLayout.h, which additionally fires on
  // ANY Failed row wherever it sits. The shipped world contains exactly that
  // case - DemoWorld.cpp:277 is an outgoing Failed row followed at :279 by an
  // outgoing Pending row - so endsOutgoingRun is FALSE there while the row must
  // still show its reading. It is the red circle and the words "Not sent" on
  // 12:09 row in the capture; hanging the cluster on this field alone deletes
  // it, and a silently swallowed failure is the one delivery state this surface
  // must never lose. endsOutgoingRun is not a substitute for that rule.
  bool endsOutgoingRun = false;
};

// One entry per row, in order, always. Never throws.
std::vector<ThreadRowPlan> PlanThreadRows(demo::Conversation const& c);

// ---- the delivery badge (T5) --------------------------------------------
// What the cluster under the last outgoing bubble of a run DRAWS, as data.
// Pure, so --diagnose can walk the whole closed set of Spec C §5.3 rather
// than a screenshot having to be believed.
//
// THE table of what a state draws. Demo/ThreadLayout.h keeps only the WORDS
// now (DeliveryWord, for the automation name); its six-distinct-GLYPH table was
// deleted in T5 fix round 1 rather than left orphaned, because this one gives
// Sent and Delivered the same glyph on purpose and tells them apart by how many
// of it there are — so the two tables could not both be true.
enum class DeliveryCue { Clock, OneCheck, TwoOutlineChecks, TwoFilledChecks, Alert, Timer };

struct DeliveryBadge {
  DeliveryCue cue;
  wchar_t const* glyph;  // ONE Segoe Fluent codepoint, never empty
  int repeat;            // 1 or 2 — "two checks" (Spec C §5.3) is a COUNT
  wchar_t const* word;   // the non-colour channel: the state says itself
  bool danger;           // UrDangerBrush
  bool solid;            // UrTextBrush rather than UrTextMutedBrush
};

// Total over the closed set of Spec C §5.3. Pure, so --diagnose can walk it.
DeliveryBadge BadgeFor(demo::DeliveryState s);

// -1 when `id` is empty or matches nothing. Deselection is a real state and
// must not fall through to bubble 0 — which is what an index of 0 for "no
// match" would do, and it would be invisible in every screenshot where bubble 0
// happened to be the selected one.
int SelectedBubbleIndex(std::vector<std::wstring> const& ids, std::wstring const& id);

// ---- motion, as numbers rather than as code (T6) -------------------------
// design §7, "Bubble entrance": fade + 10 DIP rise + 0.96 -> 1.0 scale at
// motion::kBaseMs on the standard curve. The numbers live here rather than in
// the builder so --diagnose can read them without an apartment.
inline constexpr double kBubbleRiseDip = 10.0;
inline constexpr double kBubbleFromScale = 0.96;

// design §7, "Typing indicator": three dots at kPulseMs, 140 ms phase offset.
// 140 is an OFFSET between three copies of one timeline, not a new duration.
inline constexpr int kTypingDots = 3;
inline constexpr int64_t kTypingPhaseMs = 140;

// 0, 140, 280. -1 for a dot outside [0, kTypingDots).
int64_t TypingDotPhaseMs(int dot);

// ---- the timelines themselves, as data (T6 fix round 1) ------------------
// ONE timeline of one effect. The builders in ThreadView.cpp own no list of
// their own: RunBubbleEntrance and SetThreadTyping ITERATE these vectors and
// hand each entry to motion::MakeSplineDouble. That is the whole point of
// keeping them here.
//
// The first version of this counted with `return animate ? 4 : 0;` beside a
// builder that wrote out four add() calls by hand, and the --diagnose line
// compared that literal against the literal 4. Deleting the ScaleY timeline
// from the builder left the gate printing PASS. A count is only worth
// asserting if it is the count the render actually spends.
//
// `ms` is NOT here: a duration is a motion token and UrMotion.h pulls in winrt,
// which this header may never do (CollectDiagnostics runs before
// winrt::init_apartment). Both effects run every one of their timelines at one
// duration, so the caller passes it once.
struct TimelineSpec {
  wchar_t const* path;  // Storyboard::SetTargetProperty path, never empty
  double from;
  double to;
  int64_t beginMs;   // stagger; 0 for none
  bool autoReverse;  // out and back within one repeat
  bool forever;      // RepeatBehavior::Forever
};

// design §7, "Bubble entrance". FOUR timelines — opacity, TranslateY, ScaleX,
// ScaleY — and EMPTY when `animate` is false, because "off" means the motion is
// gone rather than shortened. The empty half is the one worth asserting: a
// table that is the same length either way is not a gate.
//
// `staggerMs` delays the WHOLE entrance (every timeline begins together at
// staggerMs); it is how design d2 §8.2's conversation-open stagger reuses one
// table. The default keeps the append path's shape, and the "T6 bubble
// entrance" gate asserts the staggerMs=0 shape exactly as before.
std::vector<TimelineSpec> EntranceTimelines(bool animate, int64_t staggerMs = 0);

// design §7, "Typing indicator". One per dot, in dot order, each offset by
// TypingDotPhaseMs. Empty when `animate` is false.
std::vector<TimelineSpec> TypingTimelines(bool animate);

// == EntranceTimelines(animate).size() / TypingTimelines(animate).size().
int EntranceTimelineCount(bool animate);
int TypingTimelineCount(bool animate);

// ---- conversation-open stagger (design d2 §8.2) -----------------------------
// A freshly built thread used to pop in whole while the conversation list
// beside it staggers. Now only the visible FOOT animates: the last
// min(kMaxStaggerSteps, bubbleCount) bubble rows, kStaggerMs apart,
// oldest-to-newest so the newest settles last. Only the foot animates because
// rows above the fold animating invisibly would be waste, and 6 is the cap the
// list already uses (ConversationRowDelayMs, gated by demo.list.stagger).
//
// Returns -1 for "does not animate" (every row above the foot) and a begin
// delay >= 0 for a foot row — the SelectedBubbleIndex precedent: 0 is a valid
// begin (the foot's oldest), so "skip me" cannot also be 0. The delay is
// 0-based within the foot, matching the list's convention (its index 0 also
// starts at 0 ms). d2's sketch signed this as a function of the count alone;
// a count cannot answer a per-row question, so the row's index among bubble
// rows is the first parameter.
//
// kStaggerMs/kMaxStaggerSteps live in UrMotion.h, which pulls in winrt — the
// .cpp reads them off it textually, the ConversationRowModel.cpp pattern; this
// header stays winrt-free.
int64_t OpenStaggerBeginMs(std::size_t bubbleIndex, std::size_t bubbleCount);

// ---- what an APPEND does to the row above it (T6) -------------------------
// Appending a row does not only ADD a cluster. It can also TAKE one away, and
// that half is the one an append-only implementation silently drops.
//
// CarriesDeliveryGlyph() (Demo/ThreadLayout.h) is a function of a row AND the
// row after it. The row that was newest was evaluated with next == nullptr, so
// an outgoing one carried a cluster because it was last of its run. Put another
// outgoing row under it and it is no longer last of its run: the SAME rule now
// says it must not carry one, and unless something removes it the thread shows
// two readings of one run. Design §6.2 gives a run exactly ONE reading.
//
// The Failed exception is why this is a re-EVALUATION and not "clear the
// previous cluster": CarriesDeliveryGlyph fires on any Failed row wherever it
// sits, so a Failed row keeps its cluster when a newer row lands under it, and
// blanket removal would delete the one delivery state this surface must never
// swallow.
//
// Pure, and returning DATA rather than doing the removal, for the reason every
// other decision on this surface is: --diagnose runs before
// winrt::init_apartment() and can only assert what it can call.
struct AppendClusterPlan {
  bool appendedCarries = false;  // CarriesDeliveryGlyph(appended, nullptr)
  bool prevCarried = false;      // what prev drew BEFORE, i.e. with next == nullptr
  bool prevCarriesNow = false;   // CarriesDeliveryGlyph(prev, &appended)
  bool prevMustLose = false;     // prevCarried && !prevCarriesNow  -> REMOVE

  // UNSATISFIABLE under today's rule, and kept deliberately. The proof is one
  // line: CarriesDeliveryGlyph(x, nullptr) reduces to "x is an outgoing message
  // row", so prevCarried is TRUE whenever prevCarriesNow can be, and
  // !prevCarried && prevCarriesNow cannot hold. It is here so that
  // SetRowCluster stays a total "make this row match the rule" rather than a
  // one-way "clear it" — the difference matters the day CarriesDeliveryGlyph
  // grows a clause that can turn a reading ON. `T6 append cluster` asserts the
  // zero rather than printing it, so this comment cannot quietly go stale.
  bool prevMustGain = false;     // !prevCarried && prevCarriesNow  -> ADD
};

// `prev` is the last row already in the column, or nullptr for an append into
// an empty thread. Never throws.
AppendClusterPlan PlanAppendCluster(demo::MessageRow const* prev,
                                    demo::MessageRow const& appended);

// ---- the sliding window over the backlog (T8) ------------------------------
// A conversation renders AT MOST the 500 most-relevant rows. Older history
// materializes in 100-row chunks as the scroller nears the top of the loaded
// range, and the window slides back down in 100-row chunks at the foot end as
// the reader scrolls home. Under 500 rows the window covers the whole
// conversation and the render is exactly what it was before this wave — the
// window is a VIEW concern over the full world plan, so every decision about
// it is pure arithmetic here and --diagnose can walk it.
//
// All ranges are half-open [start, end) indices into Conversation::rows. After
// the window exists the world only ever grows at the FOOT (ambient appends),
// so a window's start index names the same row for the window's whole life.
inline constexpr std::size_t kThreadWindowMaxRows = 500;
inline constexpr std::size_t kThreadWindowChunkRows = 100;

// The near-edge trigger, in VIEWPORTS of remaining distance. 1.5 rather than
// 1.0 so the chunk lands before the reader can outrun it (a fast flick covers
// a viewport in one gesture), and rather than 2.0 so a reader who only dips
// toward the edge does not pay for a 100-row build they may never look at.
inline constexpr double kWindowEdgeViewports = 1.5;

struct ThreadWindow {
  std::size_t start = 0;  // first world row index rendered
  std::size_t end = 0;    // one past the last
};

std::size_t WindowRowCount(ThreadWindow w);
// True when the window trims anything at all: at or under the cap every row
// renders and this wave's code paths must be indistinguishable from before it.
bool WindowActive(std::size_t totalRows);
bool WindowCovers(ThreadWindow w, std::size_t rowIndex);

// The window at conversation open: the NEWEST min(500, total) rows. A thread
// opens at its foot, so relevance starts at the newest row and walks back.
ThreadWindow InitialWindow(std::size_t totalRows);

// One chunk OLDER: start moves up by min(chunk, start); if that leaves more
// than 500 rendered, the FOOT is trimmed back to 500 (the newest rows leave
// the tree; sliding back down re-covers them). Idempotent once start == 0.
ThreadWindow SlideWindowUp(ThreadWindow w);

// One chunk NEWER: end moves down by min(chunk, total - end); if that leaves
// more than 500 rendered, the HEAD is trimmed back to 500. Idempotent once
// end == total.
ThreadWindow SlideWindowDown(ThreadWindow w, std::size_t totalRows);

// The edge proximity tests the scroller feeds. `scrollableDip` is the loaded
// extent minus the viewport (ScrollableHeight), so "near the foot of the
// loaded range" is a distance of scrollable - offset, symmetric with the top.
bool NearTopOfLoaded(double offsetDip, double viewportDip);
bool NearFootOfLoaded(double offsetDip, double scrollableDip, double viewportDip);

// The offset that keeps ONE surviving row at the same viewport position across
// a slide: the row's Y in the extent moves by (anchorAfter - anchorBefore), so
// the offset must move by exactly that. The view feeds it the per-mutation
// EXTENT delta as the anchor travel (a prepend moves every old row down by the
// inserted extent; a head trim pulls them up by the trimmed extent) — measured
// across the mutation's own layout pass, because UseLayoutRounding snaps each
// arranged row to the physical pixel grid and a per-row height sum loses the
// accumulated rounding (39.2 dip over a 100-row chunk at 125% DPI, caught by
// the A/B capture as a one-row drift).
double OffsetAfterSlide(double oldOffsetDip, double anchorBeforeDip, double anchorAfterDip);
// ScrollViewer clamps offsets into [0, ScrollableHeight] on its own; the pure
// math states the clamp so the gate can walk it too.
double ClampScrollOffset(double offsetDip, double scrollableDip);

// What an ambient arrival means for the window. `newRowIndex` is where the
// appended row landed in Conversation::rows (rows.size() - 1 at the call
// site). The row is rendered exactly when the window covers the world's foot
// (end == newRowIndex); a reader deep in history gets NO tree change — the
// row is a world row and the window re-covers it as it slides home. When the
// row renders, `after` grows the window by one at the foot and trims the head
// back to <= 500, so the cap holds through ambient traffic too.
struct AmbientAppendPlan {
  bool render = false;
  ThreadWindow after;
};
AmbientAppendPlan PlanAmbientAppend(ThreadWindow w, std::size_t newRowIndex);

// What re-setting the SAME conversation means for the window (autoplay's
// delivery-advance refresh). A reader at the foot gets today's behaviour — the
// window re-bases at the newest rows and the pin lands. A reader deep in
// history keeps their place: start still names the same row (the world grows
// at the foot only), so the refreshed window is re-clamped AROUND it rather
// than re-based at the foot — re-basing would be the yank design 9.2 forbids.
struct RefreshWindowPlan {
  ThreadWindow window;
  bool pinToFoot = false;
};
RefreshWindowPlan PlanRefreshWindow(ThreadWindow w, std::size_t totalAfter, bool readerAtFoot);

// ---- progressive window hydration (T9) --------------------------------------
// Opening or switching to a conversation over the 500-row window cap used to
// parent ALL 500 window rows in one synchronous turn — measured at 190-240 ms
// to the first presented frame on the 2024-row stress world on this machine,
// 600-1000 ms on the owner's. Hydration splits the open in two: a
// synchronous INITIAL SET covering what the viewport shows plus headroom, and
// BACKGROUND BEATS that materialize the rest of the window above it, one
// chunk per dispatcher turn, silent (no fade, no marker — off-viewport work).
//
// The residency truth is the window itself and there is exactly ONE: both the
// beats and the scroll-triggered slides move window.start, and each computes
// its insert range from the LIVE window.start in its own dispatcher turn, so
// a scroll-prepend mid-fill cannot double-materialize a beat's rows or vice
// versa — the scripted scenarios are walked in --diagnose ("T9 hydrate
// coalesce").

// The initial set covers the viewport plus ~one viewport of headroom. The
// thread opens pinned at its FOOT, so all the headroom is upward: the cover
// target is 2.0 viewport heights of estimated content walking back from the
// window's newest row.
inline constexpr double kHydrateViewportCover = 2.0;
// …with a floor: an estimate is a guess, and a guess must never leave the
// first frame sparse. 32 rows of mostly-bubbles is ~1.5-2 viewports of real
// content at typical heights, so the floor binds only on tiny viewports.
inline constexpr std::size_t kHydrateFloorRows = 32;
// The viewport is 0 on an unrealized tree (the constructor path); size the
// initial set against a typical window instead of the floor alone.
inline constexpr double kHydrateFallbackViewportDip = 800.0;
// The beat size IS the window's chunk size: one number sizes every "next
// slice of the backlog" this surface materializes, scroll-triggered or
// background, and the two already provably tile the same plan (T8).
inline constexpr std::size_t kHydrateFillBeatRows = kThreadWindowChunkRows;

// Estimated rendered height per row SHAPE, in dip — for initial-set SIZING
// ONLY. An underestimate is corrected by the fill within a beat or two, an
// overestimate by the cap; neither is worth simulating wrapped text for.
// What must NEVER happen is an initial set that leaves the viewport
// under-filled, and that is the floor's job, not the estimate's.
double EstimatedRowDip(ThreadRowShape shape);

// How many of the window's NEWEST rows render synchronously at open: walk
// back from the window's foot accumulating EstimatedRowDip until
// kHydrateViewportCover * viewportDip is covered, then clamp into
// [min(kHydrateFloorRows, windowRows), windowRows]. The clamp at the TOP end
// is the under-cap guarantee: at or under 500 rows the initial set IS the
// whole window and the open is pixel-identical to the pre-hydration path.
std::size_t InitialViewportRows(double viewportDip, std::vector<ThreadRowPlan> const& plan,
                                ThreadWindow window);

// The fill's one decision, per beat. `cursor` is the oldest RESIDENT row
// (the live window.start), `target` the window.start the fill walks down to
// (the initial window's start — a row index, so it names the same row for
// the fill's whole life: the world grows at the foot only).
struct HydrateBeatPlan {
  bool run = false;         // insert [newStart, cursor) above, this turn
  bool reschedule = false;  // queue the next beat after this one
  std::size_t newStart = 0; // valid when run
};
HydrateBeatPlan PlanHydrateBeat(std::size_t cursor, std::size_t target,
                                std::uint64_t beatGeneration,
                                std::uint64_t currentGeneration);

// Whether a fill from `cursor` down to `target` has work left. Also the
// coalescing rule for a scroll slide that jumps PAST the target mid-fill:
// cursor < target means the slide already covered the remainder (and more),
// so the fill is DONE rather than owed a negative-size beat.
bool HydrateFillActive(std::size_t cursor, std::size_t target);

}  // namespace urmsg::views
