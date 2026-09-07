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

}  // namespace urmsg::views
