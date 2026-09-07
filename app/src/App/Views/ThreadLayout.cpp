// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader> so that "pure C++, no winrt"
// is enforced by the compiler rather than by a comment at the top of a file.
#include "Views/ThreadLayout.h"

namespace urmsg::views {
namespace {

bool IsMessage(demo::MessageRow const& r) { return r.kind == demo::RowKind::Message; }

}  // namespace

std::vector<ThreadRowPlan> PlanThreadRows(demo::Conversation const& c) {
  std::vector<ThreadRowPlan> plan;
  plan.reserve(c.rows.size());

  for (std::size_t i = 0; i < c.rows.size(); ++i) {
    demo::MessageRow const& r = c.rows[i];
    ThreadRowPlan p;
    p.rowIndex = i;
    switch (r.kind) {
      case demo::RowKind::DaySeparator:
        p.shape = ThreadRowShape::DaySeparator;
        break;
      case demo::RowKind::System:
        // permanentRecord is the ONLY field in the fixed contract that
        // distinguishes the key-change record from the other system lines, so
        // it is the only thing this switches on. Classifying by matching words
        // inside systemText would break the first time the copy changed.
        p.shape = r.permanentRecord ? ThreadRowShape::SystemPermanentRecord
                                    : ThreadRowShape::SystemLine;
        break;
      case demo::RowKind::Message:
        p.shape = r.outgoing ? ThreadRowShape::OutgoingBubble
                             : ThreadRowShape::IncomingBubble;
        break;
    }
    plan.push_back(p);
  }

  // A RUN is a maximal stretch of consecutive Message rows in the same
  // direction. A day separator or a system row BREAKS it, deliberately: the
  // delivery cluster belongs under the last bubble the reader actually saw, not
  // under one that is three rows further down past a "Ana's safety number
  // changed" record.
  for (std::size_t i = 0; i < plan.size(); ++i) {
    demo::MessageRow const& r = c.rows[plan[i].rowIndex];
    if (!IsMessage(r)) continue;

    const bool startsRun = (i == 0) || !IsMessage(c.rows[plan[i - 1].rowIndex]) ||
                           c.rows[plan[i - 1].rowIndex].outgoing != r.outgoing;
    const bool endsRun = (i + 1 == plan.size()) || !IsMessage(c.rows[plan[i + 1].rowIndex]) ||
                         c.rows[plan[i + 1].rowIndex].outgoing != r.outgoing;

    // Spec C §5.2: sender name + identicon on the first bubble of a run, in
    // GROUPS only. Never in a DM (the other name is the window title) and never
    // on your own run (it is you).
    plan[i].showSenderHeader =
        startsRun && !r.outgoing && c.kind == demo::ConversationKind::Group;
    plan[i].endsOutgoingRun = endsRun && r.outgoing;
  }
  return plan;
}

}  // namespace urmsg::views
