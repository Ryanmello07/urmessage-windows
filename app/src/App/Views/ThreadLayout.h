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
// (ShowsSenderHeader, DeliveryGlyph, AuditDaySeparators, ...); this one holds
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
  // still show its reading. It is the red circle and the word "Failed" on the
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
// Not the same table as DeliveryGlyph()/DeliveryWord() in Demo/ThreadLayout.h,
// and deliberately so: that one is the six states as six DISTINCT glyphs, which
// is what a bubble's automation name and the T2 gate are built on. This one is
// the RENDERED cluster, where Sent and Delivered share a glyph on purpose and
// are told apart by how many of it there are.
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

}  // namespace urmsg::views
