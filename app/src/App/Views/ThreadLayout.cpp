// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader> so that "pure C++, no winrt"
// is enforced by the compiler rather than by a comment at the top of a file.
#include "Views/ThreadLayout.h"

#include <algorithm>  // (std::min), parenthesised against the windows.h max/min macros

// UrMotion.h textually drags in winrt/Microsoft.UI.Composition.h and
// winrt/Microsoft.UI.Xaml.Media.Animation.h on its own account; this file
// reads exactly two plain integers off it (kStaggerMs, kMaxStaggerSteps, for
// OpenStaggerBeginMs) and constructs nothing from either header. The same
// arrangement ConversationRowModel.cpp already ships, and the same review
// obligation: no WinRT type may ever be named on this path, because
// CollectDiagnostics reaches this unit before winrt::init_apartment().
#include "UrMotion.h"

namespace urmsg::views {
namespace {

bool IsMessage(demo::MessageRow const& r) { return r.kind == demo::RowKind::Message; }

}  // namespace

// The corner table of design d2 §1. The tightened corner is always on the
// edge facing the adjacent same-speaker bubble — the spine, left for
// incoming, right for outgoing — and Single keeps all four at the base 12 so
// a solo bubble never gets a harsh look.
void BubbleCornerDip(BubbleRunPos runPos, bool outgoing, double out[4]) {
  constexpr double kBase = 12.0;      // the card radius used app-wide
  constexpr double kAttached = 4.0;   // the corner facing a same-speaker neighbour
  out[0] = out[1] = out[2] = out[3] = kBase;
  switch (runPos) {
    case BubbleRunPos::Single:
      break;
    case BubbleRunPos::First:  // attached BELOW, on the spine side
      if (outgoing)
        out[2] = kAttached;  // BR
      else
        out[3] = kAttached;  // BL
      break;
    case BubbleRunPos::Middle:  // attached above AND below, on the spine side
      if (outgoing) {
        out[1] = kAttached;  // TR
        out[2] = kAttached;  // BR
      } else {
        out[0] = kAttached;  // TL
        out[3] = kAttached;  // BL
      }
      break;
    case BubbleRunPos::Last:  // attached ABOVE, on the spine side
      if (outgoing)
        out[1] = kAttached;  // TR
      else
        out[0] = kAttached;  // TL
      break;
  }
}

double GapAboveDip(BubbleRunPos runPos) {
  return (runPos == BubbleRunPos::Middle || runPos == BubbleRunPos::Last) ? 2.0 : 10.0;
}

BubbleRunPos RunPosFor(demo::MessageRow const* prev, demo::MessageRow const& cur,
                       demo::MessageRow const* next) {
  const bool continuesUp = prev != nullptr && ContinuesBubbleRun(*prev, cur);
  const bool continuesDown = next != nullptr && ContinuesBubbleRun(cur, *next);
  if (!continuesUp) return continuesDown ? BubbleRunPos::First : BubbleRunPos::Single;
  return continuesDown ? BubbleRunPos::Middle : BubbleRunPos::Last;
}

int64_t OpenStaggerBeginMs(std::size_t bubbleIndex, std::size_t bubbleCount) {
  if (bubbleCount == 0 || bubbleCount <= bubbleIndex) return -1;
  const std::size_t steps =
      (std::min)(bubbleCount, static_cast<std::size_t>(urnw::motion::kMaxStaggerSteps));
  const std::size_t footStart = bubbleCount - steps;
  if (bubbleIndex < footStart) return -1;  // above the fold: does not animate
  return static_cast<int64_t>(bubbleIndex - footStart) * urnw::motion::kStaggerMs;
}

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
    demo::MessageRow const* next = (row + 1 < c.rows.size()) ? &c.rows[row + 1] : nullptr;
    plan[i].showSenderHeader =
        ShowsSenderHeader(prev, r, c.kind == demo::ConversationKind::Group);
    plan[i].endsOutgoingRun = endsRun && r.outgoing;
    // The geometric run position (d2 §1), from the same prev/next. It AGREES
    // with the header rule about where runs start by construction — both break
    // on senderKey — so the corner language can never contradict the name and
    // identicon above a run. `T7 run geometry` walks this per row.
    plan[i].runPos = RunPosFor(prev, r, next);
  }
  return plan;
}

// The rendered delivery cluster, as a table.
//
// Codepoints verified against C:\Windows\Fonts\SegoeIcons.ttf and rendered at
// 13px on #101010 before being written here. E930/EC61 are the font's only
// outline/filled check pair; bare checks (E10B, E001, E0E7, E73E, E8FB) are
// indistinguishable from each other at this size, so "outline vs filled" cannot
// be carried by choosing between two of THOSE. Substituting a codepoint here
// breaks the SHAPE channel, and the "T5 delivery badges" line in
// CollectDiagnostics() is what says so out loud.
//
// Three channels, never colour:
//   COUNT  Sent vs Delivered  — same glyph (E930), repeat 1 vs 2
//   SHAPE  Delivered vs Read  — same repeat (2), ring (E930) vs disc (EC61)
//   WORD   all six distinct, and it is what survives a greyscale screenshot
DeliveryBadge BadgeFor(demo::DeliveryState s) {
  switch (s) {
    case demo::DeliveryState::Pending:
      return {DeliveryCue::Clock, L"\uE121", 1, L"Sending", false, false};  // "Clock"
    case demo::DeliveryState::Sent:
      return {DeliveryCue::OneCheck, L"\uE930", 1, L"Sent", false, false};  // "Completed" — check in a ring
    case demo::DeliveryState::Delivered:
      return {DeliveryCue::TwoOutlineChecks, L"\uE930", 2, L"Delivered", false, false};  // "Completed"
    case demo::DeliveryState::Read:
      return {DeliveryCue::TwoFilledChecks, L"\uEC61", 2, L"Read", false, true};  // "CompletedSolid" — check in a disc
    case demo::DeliveryState::Failed:
      return {DeliveryCue::Alert, L"\uE783", 1, L"Not sent", true, true};  // "Error" — exclamation in a ring
    case demo::DeliveryState::Expired:
      // Demo/ThreadLayout.h names this same codepoint "Stopwatch"; it is the
      // one drawing, under two of the font's own aliases.
      return {DeliveryCue::Timer, L"\uE916", 1, L"Expired", false, false};  // "Timer"
  }
  return {DeliveryCue::Clock, L"\uE121", 1, L"Sending", false, false};  // "Clock"
}

int SelectedBubbleIndex(std::vector<std::wstring> const& ids, std::wstring const& id) {
  if (id.empty()) return -1;
  for (std::size_t i = 0; i < ids.size(); ++i)
    if (ids[i] == id) return static_cast<int>(i);
  return -1;
}

// ---- T6 -------------------------------------------------------------------

int64_t TypingDotPhaseMs(int dot) {
  if (dot < 0 || kTypingDots <= dot) return -1;
  return kTypingPhaseMs * dot;
}

// The four channels of design §7's bubble entrance, in the order they are
// started. RunBubbleEntrance walks exactly this vector, so deleting an entry
// here deletes the timeline there and the --diagnose count follows.
// `staggerMs` shifts every begin together — the entrance is one gesture, so
// the four never slide relative to one another.
std::vector<TimelineSpec> EntranceTimelines(bool animate, int64_t staggerMs) {
  if (!animate) return {};  // motion GONE, not shortened
  return {
      {L"Opacity", 0.0, 1.0, staggerMs, false, false},
      {L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)", kBubbleRiseDip, 0.0,
       staggerMs, false, false},
      {L"(UIElement.RenderTransform).(CompositeTransform.ScaleX)", kBubbleFromScale, 1.0,
       staggerMs, false, false},
      {L"(UIElement.RenderTransform).(CompositeTransform.ScaleY)", kBubbleFromScale, 1.0,
       staggerMs, false, false},
  };
}

// One per dot, in dot order. Half a cycle out and AutoReverse back, repeated
// forever, each offset by design §7's 140 ms so the three read as a wave.
std::vector<TimelineSpec> TypingTimelines(bool animate) {
  if (!animate) return {};
  std::vector<TimelineSpec> out;
  out.reserve(static_cast<std::size_t>(kTypingDots));
  for (int i = 0; i < kTypingDots; ++i)
    out.push_back({L"Opacity", 0.30, 1.0, TypingDotPhaseMs(i), true, true});
  return out;
}

int EntranceTimelineCount(bool animate) {
  return static_cast<int>(EntranceTimelines(animate).size());
}
int TypingTimelineCount(bool animate) {
  return static_cast<int>(TypingTimelines(animate).size());
}

// The whole decision an append makes about the delivery cluster, on BOTH rows.
// One call to CarriesDeliveryGlyph per question, and no second rule: the row
// being appended is the newest, so its `next` is nullptr by definition, and the
// row above it is re-asked the same question with the new row as its `next`.
AppendClusterPlan PlanAppendCluster(demo::MessageRow const* prev,
                                    demo::MessageRow const& appended) {
  AppendClusterPlan p;
  p.appendedCarries = CarriesDeliveryGlyph(appended, nullptr);
  if (prev == nullptr) return p;
  p.prevCarried = CarriesDeliveryGlyph(*prev, nullptr);
  p.prevCarriesNow = CarriesDeliveryGlyph(*prev, &appended);
  p.prevMustLose = p.prevCarried && !p.prevCarriesNow;
  p.prevMustGain = !p.prevCarried && p.prevCarriesNow;
  return p;
}

}  // namespace urmsg::views
