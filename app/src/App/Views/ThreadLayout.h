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

struct ThreadRowPlan {
  std::size_t rowIndex = 0;      // index into Conversation::rows
  ThreadRowShape shape = ThreadRowShape::SystemLine;

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
std::vector<TimelineSpec> EntranceTimelines(bool animate);

// design §7, "Typing indicator". One per dot, in dot order, each offset by
// TypingDotPhaseMs. Empty when `animate` is false.
std::vector<TimelineSpec> TypingTimelines(bool animate);

// == EntranceTimelines(animate).size() / TypingTimelines(animate).size().
int EntranceTimelineCount(bool animate);
int TypingTimelineCount(bool animate);

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

}  // namespace urmsg::views
