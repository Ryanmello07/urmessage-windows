// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader> so that "pure C++, no winrt"
// is enforced by the compiler rather than by a comment at the top of a file.
#include "Views/ThreadLayout.h"

#include <algorithm>  // (std::min), parenthesised against the windows.h max/min macros

// UrMotion.h textually drags in winrt/Microsoft.UI.Composition.h and
// winrt/Microsoft.UI.Xaml.Media.Animation.h on its own account; this file
// reads exactly three plain integers off it (kStaggerMs, kMaxStaggerSteps,
// kBaseMs — for OpenStaggerBeginMs/OpenStaggerTailMs) and constructs nothing
// from either header. The same arrangement ConversationRowModel.cpp already
// ships, and the same review obligation: no WinRT type may ever be named on
// this path, because CollectDiagnostics reaches this unit before
// winrt::init_apartment().
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

// The whole open-vs-refresh rule, stated once: an entrance is how a thread
// announces a DIFFERENT conversation. The id pair is the view's only signal —
// a delivery-advance refresh re-sets the conversation that is already open.
bool ShouldRunEntrance(std::wstring const& openConvId, std::wstring const& nextConvId) {
  return openConvId != nextConvId;
}

// 5 * 40 + 250 = 450 ms at today's tokens: the worst-case stagger begin (the
// foot's newest bubble, kMaxStaggerSteps - 1 intervals out) plus one kBaseMs
// entrance. Read off UrMotion.h textually — the OpenStaggerBeginMs arrangement
// above, with the same no-winrt obligation.
int64_t OpenStaggerTailMs() {
  return static_cast<int64_t>(urnw::motion::kMaxStaggerSteps - 1) * urnw::motion::kStaggerMs +
         urnw::motion::kBaseMs;
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

// ---- T8: the sliding window ------------------------------------------------

std::size_t WindowRowCount(ThreadWindow w) { return w.end - w.start; }
bool WindowActive(std::size_t totalRows) { return kThreadWindowMaxRows < totalRows; }
bool WindowCovers(ThreadWindow w, std::size_t rowIndex) {
  return w.start <= rowIndex && rowIndex < w.end;
}

ThreadWindow InitialWindow(std::size_t totalRows) {
  ThreadWindow w;
  w.end = totalRows;
  w.start = WindowActive(totalRows) ? totalRows - kThreadWindowMaxRows : 0;
  return w;
}

ThreadWindow SlideWindowUp(ThreadWindow w) {
  // The clamp is min(chunk, start): the last slide to the top can be a PARTIAL
  // chunk (start 24 -> 0), and partial is still a slide, not a stall.
  const std::size_t added = (std::min)(kThreadWindowChunkRows, w.start);
  w.start -= added;
  // The cap trims the FOOT, never the head just loaded: the reader asked for
  // older rows, and dropping them in the same turn would defeat the slide.
  if (WindowRowCount(w) > kThreadWindowMaxRows) w.end = w.start + kThreadWindowMaxRows;
  return w;
}

ThreadWindow SlideWindowDown(ThreadWindow w, std::size_t totalRows) {
  const std::size_t room = totalRows - w.end;  // 0 once the window is home
  const std::size_t added = (std::min)(kThreadWindowChunkRows, room);
  w.end += added;
  // Mirror rule of the up-slide: the cap trims the HEAD, never the rows the
  // reader just scrolled down to.
  if (WindowRowCount(w) > kThreadWindowMaxRows) w.start = w.end - kThreadWindowMaxRows;
  return w;
}

bool NearTopOfLoaded(double offsetDip, double viewportDip) {
  // <=, not <: the exact boundary counts as near — a trigger that excludes its
  // own boundary is a fencepost the gate cannot tell from a working one.
  return offsetDip <= kWindowEdgeViewports * viewportDip;
}
bool NearFootOfLoaded(double offsetDip, double scrollableDip, double viewportDip) {
  return scrollableDip - offsetDip <= kWindowEdgeViewports * viewportDip;
}

double OffsetAfterSlide(double oldOffsetDip, double anchorBeforeDip, double anchorAfterDip) {
  return oldOffsetDip + (anchorAfterDip - anchorBeforeDip);
}
double ClampScrollOffset(double offsetDip, double scrollableDip) {
  if (offsetDip < 0.0) return 0.0;
  return (std::min)(offsetDip, scrollableDip);
}

AmbientAppendPlan PlanAmbientAppend(ThreadWindow w, std::size_t newRowIndex) {
  AmbientAppendPlan p;
  // The arrival renders exactly when the window covers the world's foot: the
  // new row's index is then the first index BEYOND the window. A window deep
  // in history (end < newRowIndex) takes no tree change at all.
  p.render = (w.end == newRowIndex);
  p.after = w;
  if (!p.render) return p;
  p.after.end = newRowIndex + 1;
  if (WindowRowCount(p.after) > kThreadWindowMaxRows)
    p.after.start = p.after.end - kThreadWindowMaxRows;  // one row leaves at the head
  return p;
}

RefreshWindowPlan PlanRefreshWindow(ThreadWindow w, std::size_t totalAfter,
                                    bool readerAtFoot) {
  RefreshWindowPlan p;
  if (readerAtFoot || !WindowActive(totalAfter)) {
    // At the foot (or a conversation under the cap): re-base at the newest
    // rows and pin — exactly what SetThreadConversation did before this wave.
    p.window = InitialWindow(totalAfter);
    p.pinToFoot = true;
    return p;
  }
  // Deep in history: keep the reader where they are. start names the same row
  // it named before the refresh (the world grows at the foot only), so the
  // window is re-clamped around it rather than re-based at the foot.
  p.window.start = (std::min)(w.start, totalAfter - kThreadWindowMaxRows);
  p.window.end = (std::min)(p.window.start + kThreadWindowMaxRows, totalAfter);
  p.pinToFoot = false;
  return p;
}

// ---- T9: progressive window hydration ----------------------------------------

// The per-shape estimates the initial set is sized with. These are the
// typical single-line heights of the stress generator's rows (the case that
// needs hydration at all): a bubble with its §1 rhythm margin, a separator
// pill with (0,16,0,8), a centred system line with (0,8,0,8), and the
// key-change record, which is the tallest thing the thread draws. Wrapping
// bodies lie higher — fine: the floor covers underestimates and the window
// cap covers overestimates, and --diagnose walks both ends.
double EstimatedRowDip(ThreadRowShape shape) {
  switch (shape) {
    case ThreadRowShape::IncomingBubble:
    case ThreadRowShape::OutgoingBubble:
      return 64.0;
    case ThreadRowShape::DaySeparator:
      return 48.0;
    case ThreadRowShape::SystemLine:
      return 36.0;
    case ThreadRowShape::SystemPermanentRecord:
      return 80.0;
  }
  return 64.0;
}

std::size_t InitialViewportRows(double viewportDip, std::vector<ThreadRowPlan> const& plan,
                                ThreadWindow window) {
  const std::size_t windowRows = WindowRowCount(window);
  if (windowRows == 0) return 0;
  const double vp = viewportDip > 0.0 ? viewportDip : kHydrateFallbackViewportDip;
  const double cover = kHydrateViewportCover * vp;
  // Walk back from the window's FOOT — the open is pinned there, so the
  // newest rows are the visible ones and the headroom accumulates upward.
  double covered = 0.0;
  std::size_t rows = 0;
  while (rows < windowRows && covered < cover) {
    covered += EstimatedRowDip(plan[window.end - 1 - rows].shape);
    ++rows;
  }
  // The floor and the cap, in one clamp: never fewer than 32 rows (unless the
  // window itself is smaller), never more than the window holds. The cap's
  // half is the under-500 pixel-identity rule: the initial set is then the
  // whole window and NOTHING is deferred.
  return (std::min)((std::max)(rows, (std::min)(kHydrateFloorRows, windowRows)), windowRows);
}

HydrateBeatPlan PlanHydrateBeat(std::size_t cursor, std::size_t target,
                                std::uint64_t beatGeneration,
                                std::uint64_t currentGeneration) {
  HydrateBeatPlan p;
  // A beat queued by a conversation the user has since switched AWAY from
  // lands here: dropped, and NOT rescheduled, so the dead conversation's
  // chain ends this turn instead of hydrating rows nobody is looking at.
  // (The live conversation's own chain carries its own generation.)
  if (beatGeneration != currentGeneration) return p;
  // Done: the cursor reached the target, OR a scroll-triggered slide jumped
  // PAST it mid-fill — cursor < target means the slide already materialized
  // the remainder, so the fill must not invent a negative-size beat.
  if (cursor <= target) return p;
  p.run = true;
  // Clamped at the TARGET, never below it: the beat must not overshoot into
  // history the window has not asked for — SlideWindowUp's own clamp would
  // overshoot and then trim the FOOT to hold the cap, yanking the newest
  // rows out of a thread the reader is watching. That is why the fill does
  // not simply reuse the slide.
  const std::size_t added = (std::min)(kHydrateFillBeatRows, cursor - target);
  p.newStart = cursor - added;
  p.reschedule = p.newStart > target;
  return p;
}

bool HydrateFillActive(std::size_t cursor, std::size_t target) { return target < cursor; }

}  // namespace urmsg::views
