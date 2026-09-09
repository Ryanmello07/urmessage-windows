// The thread surface: bubbles, day separators, system rows, the column, the
// typing indicator and the composer.
//
// Structure follows the UrComponents.h grain — a struct of named elements plus
// free Make*/Set* functions. No classes with virtuals, no MVVM, no IDL.
// The pure half (what decides, as opposed to what draws) is
// Demo/ThreadLayout.h, which has no winrt in it.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "Demo/DemoWorld.h"
#include "Views/ThreadLayout.h"  // BubbleRunPos, for MakeBubbleRow

namespace urmsg::views {

// ---- the fixed contract §4 block, verbatim -------------------------------
struct ThreadBubble {
  std::wstring id;
  winrt::Microsoft::UI::Xaml::Controls::Button root{nullptr};
};
struct ThreadView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer scroller{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel stack{nullptr};
  std::vector<ThreadBubble> bubbles;
};
ThreadView MakeThread(std::function<void(std::wstring)> onSelectMessage,
                      std::function<void()> onDeselect);
void SetThreadConversation(ThreadView& v, urmsg::demo::Conversation const& c);
void SetThreadSelectedMessage(ThreadView& v, std::wstring const& id);
void SetThreadTyping(ThreadView& v, bool typing);
void AppendThreadRow(ThreadView& v, urmsg::demo::MessageRow const& row);

// Design 9.2's do-not-yank rule, as a PURE decision: true means the thread may
// re-pin to its foot when the stack's size changes. Unarmed (no pin has landed
// yet) always pins - the first size change that carries a real extent has
// offset 0 and a large scrollable height, which is indistinguishable from "the
// reader scrolled to the top", so the naive guard would skip the very first
// pin and reopen the bug the handler exists to fix. Once armed, the pin holds
// only while the reader is within 48 dip of the foot: a reader who scrolled up
// to read history is NOT yanked down when an ambient row lands or the column
// resizes, and a reader at the bottom stays pinned. 48 is one short bubble
// row of slack - near enough that "at the foot" survives a sub-row rounding,
// far enough that an arriving row does not drag the backlog with it. The
// decision can be pure even where the event cannot be synthesised: Startup.cpp
// walks all four cases in --diagnose (W9, the d7 audit's class-6 override).
// What the handler feeds as `scrollableHeight` is the extent the LAST
// decision was made at (ThreadParts::pinExtent), not the live extent - a
// size change IS the extent moving, and measuring the reader against the
// moving target misreads "the extent grew under a pinned reader" as
// "scrolled away".
bool ShouldPinToBottom(bool armed, double offset, double scrollableHeight);

// ---- the thread's own internals (NOT in the contract) --------------------
// The identicon gutter. 28 + 8 of air: Spec C §W9's 40x40 is the LIST row's
// avatar, and 40 beside a 20 DIP line of body text in a thread is a portrait,
// not an avatar. Continuation bubbles draw nothing in the gutter but still
// reserve it, so a run keeps one left edge.
inline constexpr double kThreadGutterDip = 36.0;
inline constexpr double kThreadIdenticonDip = 28.0;

// One row of the thread stack for a MESSAGE row: the sender-name line on a
// run-start (OUTSIDE the bubble, d2 §1 — every bubble interior is then
// uniformly body + time), the identicon gutter, the bubble Button, and — when
// this row carries it — the delivery CLUSTER under the bubble, which is the
// ONE element on a row that draws a delivery indication (the bubble itself
// draws none).
// Two elements come back because they have different owners: `root`
// goes into ThreadView::stack, `bubble` goes into ThreadView::bubbles, and
// ThreadBubble::root stays the Button exactly as the contract requires (the
// identicon must sit OUTSIDE it, or the bubble's fill would paint the gutter).
struct BubbleRow {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  ThreadBubble bubble;
};
// `runPos` is the plan's GEOMETRIC run position (Views/ThreadLayout.h): it
// sets the bubble's asymmetric corners and the row's top margin.
BubbleRow MakeBubbleRow(urmsg::demo::MessageRow const& row, bool group,
                        bool showSenderHeader, bool carriesDeliveryGlyph,
                        BubbleRunPos runPos);

}  // namespace urmsg::views
