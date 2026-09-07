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

  // TWO different notions of "run" live here, on purpose, and the difference is
  // the whole reason this loop reads the way it does.
  //
  // endsOutgoingRun is a stretch of consecutive Message rows in the same
  // DIRECTION. A day separator or a system row BREAKS it, deliberately: a
  // position hint belongs under the last bubble the reader actually saw, not
  // under one that is three rows further down past a "Ana's safety number
  // changed" record.
  //
  // showSenderHeader is a stretch from the same SENDER, and it is NOT computed
  // here - it is delegated to ShowsSenderHeader() in Demo/ThreadLayout.h, which
  // compares senderKey. Spec C §5.2 names the sender on the first bubble of a
  // run so the reader knows WHO is speaking; a direction-only run merges
  // Mira-then-Tobias into one incoming run and Tobias loses his name. Measured
  // on the shipped world before this delegation existed: the rule said 20
  // headers, a direction-only computation said 15, and the 5 that disagreed
  // were DemoWorld.cpp:258, 302, 311, 314 and 316. One definition, one caller,
  // and "T4 sender headers" in CollectDiagnostics() gates the two staying equal.
  for (std::size_t i = 0; i < plan.size(); ++i) {
    const std::size_t row = plan[i].rowIndex;
    demo::MessageRow const& r = c.rows[row];
    if (!IsMessage(r)) continue;

    const bool endsRun = (i + 1 == plan.size()) || !IsMessage(c.rows[plan[i + 1].rowIndex]) ||
                         c.rows[plan[i + 1].rowIndex].outgoing != r.outgoing;

    demo::MessageRow const* prev = (row > 0) ? &c.rows[row - 1] : nullptr;
    plan[i].showSenderHeader =
        ShowsSenderHeader(prev, r, c.kind == demo::ConversationKind::Group);
    plan[i].endsOutgoingRun = endsRun && r.outgoing;
  }
  return plan;
}

}  // namespace urmsg::views
