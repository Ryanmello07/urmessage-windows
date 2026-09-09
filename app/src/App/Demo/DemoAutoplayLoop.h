// Ambient activity (design doc 9.2): the timer half — the loop the window
// starts. The pure half it drives is Demo/DemoAutoplay.h.
//
// It is NOT a scripted tour. It only ever ADDS to the conversation that is
// already open. It never changes destination, never moves the selection and
// never opens or closes the rail or the drawer - which is why the loop is given
// four narrow callbacks rather than the window: it has no way to do any of those
// things even by mistake.
//
// It is also the ONLY caller of urmsg::demo::MutableWorld (contract v2 section
// 1), and it makes exactly the two writes that comment names: one row appended,
// one DeliveryState advanced.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <memory>

#include <winrt/Microsoft.UI.Dispatching.h>

#include "Demo/DemoAutoplay.h"

namespace urmsg::demo {

enum class AutoplayPhase { Idle, Typing };

struct AutoplayCallbacks {
  // Index into World::conversations, or -1 when no thread is open: the round is
  // then skipped WHOLE rather than half-played into nothing.
  std::function<int()> openConversationIndex;
  std::function<void(bool)> setTyping;
  // The world's newest outgoing row moved one step; the thread has to be re-set
  // to draw the new glyph.
  std::function<void()> onDeliveryAdvanced;
  // The row is ALREADY in World::conversations[i].rows. The window appends it to
  // the visible thread; it does not have to store it.
  std::function<void(MessageRow const&)> onIncoming;
};

struct Autoplay {
  Autoplay() = default;
  ~Autoplay();
  Autoplay(Autoplay const&) = delete;
  Autoplay& operator=(Autoplay const&) = delete;

  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
  AutoplayCallbacks callbacks;
  AutoplayPhase phase = AutoplayPhase::Idle;
  int round = 0;
  // Snackbar's dangling-tick guard (UrComponents.cpp:848-878): the destructor
  // nulls this, so a tick that outlives the loop finds a null rather than a
  // dangling pointer. "The window owns both, so the timer cannot outlive it" is
  // an invariant nothing enforces.
  std::shared_ptr<Autoplay*> self;
};

// Built but NOT started. Off unless --demo-autoplay (design doc 8).
std::unique_ptr<Autoplay> MakeAutoplay(
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue,
    AutoplayCallbacks callbacks);
// Stopping is the destructor's job: the loop dies with the unique_ptr member on
// the window, and there is no in-app control that stops it (see the plan's
// remaining concerns). No StopAutoplay is declared, because nothing would call
// one.
void StartAutoplay(Autoplay& loop);

}  // namespace urmsg::demo
