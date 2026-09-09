// SPDX-License-Identifier: MPL-2.0
//
// The timer half of ambient activity (Demo/DemoAutoplayLoop.h says why the
// loop gets four narrow callbacks and no window pointer). The ladder, cadence
// and row fabrication it drives are the pure Demo/DemoAutoplay.cpp beside it.
// This TU uses the pch: the whole file is DispatcherQueueTimer work.

#include "pch.h"

#include "Demo/DemoAutoplayLoop.h"

#include <chrono>

#include "Log.h"
#include "Strings.h"

namespace urmsg::demo {
namespace {

void Schedule(Autoplay& loop, int64_t ms);

// One of the two writes contract v2 licenses ambient activity to make. Returns
// whether anything actually moved.
bool AdvanceNewestOutgoing(int index) {
  auto& rows = MutableWorld().conversations[static_cast<size_t>(index)].rows;
  for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
    if (it->kind != RowKind::Message || !it->outgoing) continue;
    const DeliveryState next = AdvanceDelivery(it->state);
    // Already Read, or Failed, or Expired: the ladder stops and the loop leaves
    // it alone from here on.
    if (next == it->state) return false;
    it->state = next;
    urnw::LogInfo("demo: ambient delivery {} -> state {}", urnw::Narrow(it->id),
                  static_cast<int>(next));
    return true;
  }
  return false;
}

void Tick(Autoplay& loop) {
  const int index =
      loop.callbacks.openConversationIndex ? loop.callbacks.openConversationIndex() : -1;

  if (loop.phase == AutoplayPhase::Idle) {
    // No open thread means nowhere for a typing indicator to appear, so the round
    // is skipped WHOLE rather than half-played into nothing.
    if (index < 0) {
      Schedule(loop, IdleMsForRound(loop.round));
      return;
    }
    // The delivery step happens at the TOP of the round, before the typing
    // indicator, because the composer's only way to redraw a changed delivery
    // glyph is to re-set the whole thread (contract v2's ThreadView has no
    // per-row delivery setter). Doing it here means that re-set lands while
    // nothing else on screen is moving, rather than on top of the incoming
    // message's entrance.
    if (AdvanceNewestOutgoing(index) && loop.callbacks.onDeliveryAdvanced)
      loop.callbacks.onDeliveryAdvanced();
    if (loop.callbacks.setTyping) loop.callbacks.setTyping(true);
    loop.phase = AutoplayPhase::Typing;
    Schedule(loop, TypingMsForRound(loop.round));
    return;
  }

  // Typing -> the message lands.
  if (loop.callbacks.setTyping) loop.callbacks.setTyping(false);
  if (0 <= index) {
    Conversation& conversation =
        MutableWorld().conversations[static_cast<size_t>(index)];
    const MessageRow row = IncomingForRound(conversation, loop.round);
    // Into the WORLD first. The window then appends it to the visible thread; it
    // never has to remember it, and nothing that rebuilds a view from the world
    // can lose it.
    conversation.rows.push_back(row);
    urnw::LogInfo("demo: ambient message {} in {}", urnw::Narrow(row.id),
                  urnw::Narrow(conversation.id));
    if (loop.callbacks.onIncoming) loop.callbacks.onIncoming(row);
  }

  loop.phase = AutoplayPhase::Idle;
  ++loop.round;
  Schedule(loop, IdleMsForRound(loop.round));
}

void Schedule(Autoplay& loop, int64_t ms) {
  if (!loop.timer) return;
  try {
    loop.timer.Stop();
    loop.timer.Interval(std::chrono::milliseconds(ms));
    loop.timer.Start();
  } catch (winrt::hresult_error const&) {
    // DispatcherQueueTimer.Start() throws ERROR_INVALID_OPERATION (0x800710dd)
    // once the queue has begun shutting down - the same teardown race
    // Snackbar::Show documents at UrComponents.cpp:885-890. An ambient message
    // nobody can see any more is safe to drop.
  }
}

}  // namespace

Autoplay::~Autoplay() {
  // A throw out of a destructor is std::terminate, and nothing here needs to
  // succeed - the timer dies with the queue either way.
  try {
    if (timer) timer.Stop();
  } catch (winrt::hresult_error const&) {
  }
  if (self) *self = nullptr;
}

std::unique_ptr<Autoplay> MakeAutoplay(
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue,
    AutoplayCallbacks callbacks) {
  auto loop = std::make_unique<Autoplay>();
  loop->callbacks = std::move(callbacks);
  loop->self = std::make_shared<Autoplay*>(loop.get());
  if (!queue) {
    urnw::LogError(
        "demo: autoplay built without a dispatcher queue - ambient activity is off");
    return loop;
  }
  loop->timer = queue.CreateTimer();
  loop->timer.IsRepeating(false);
  loop->timer.Tick([self = loop->self](auto const&, auto const&) {
    if (auto* live = *self) Tick(*live);
  });
  return loop;
}

void StartAutoplay(Autoplay& loop) {
  loop.phase = AutoplayPhase::Idle;
  loop.round = 0;
  urnw::LogInfo("demo: ambient activity on - first round in {} ms",
                IdleMsForRound(loop.round));
  Schedule(loop, IdleMsForRound(loop.round));
}

}  // namespace urmsg::demo
