// The thread's layout DECISIONS, as pure data.
//
// PURE C++. No winrt/ include here, and none may be added: CollectDiagnostics()
// calls PlanThreadRows() from wWinMain BEFORE winrt::init_apartment(), which is
// the same reason DemoWorld.h is pure (CONTRACT-V2 §1). Keeping the decisions
// here and the pixels in ThreadView.cpp is what makes a --diagnose assertion
// about this surface possible at all; a builder that decided inline could only
// ever be checked by looking at it.
//
// NOT the same header as Demo/ThreadLayout.h. That one is namespace
// urmsg::demo and holds T1's row RULES (ShowsSenderHeader, DeliveryGlyph,
// AuditDaySeparators, ...); this one is namespace urmsg::views and holds the
// row PLANNER. Both are always included WITH their directory prefix -
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
  bool showSenderHeader = false; // first bubble of an INCOMING run, groups only
  bool endsOutgoingRun = false;  // T5 hangs the delivery cluster under THIS row
};

// One entry per row, in order, always. Never throws.
std::vector<ThreadRowPlan> PlanThreadRows(demo::Conversation const& c);

}  // namespace urmsg::views
