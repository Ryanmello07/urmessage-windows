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
#include "RunMode.h"             // urmsg::RunMode, for SetThreadRunMode
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
// `onSend` IS THE COMPOSER'S ONE VERB, and the host's answer is what decides whether the text
// survives the click: TRUE means the host has taken the octets and will report the outcome by
// redrawing the thread, and the box is cleared; FALSE means nothing was queued and the box KEEPS
// what was typed, so a send refused between the enablement check and the click costs nobody their
// message. The other two callbacks answer void because neither can fail.
//
// Its second argument names a FAILED ROW THIS SEND REPLACES — the [ Try again ] under a "Not sent"
// bubble passes the row's id, and the composer passes an empty string. A retry that did not name
// the row it came from would leave the failure standing beside its own second attempt, which reads
// as two messages where the person wrote one.
//
// Its THIRD argument names the ROW THIS SEND IS A REPLY TO, or is empty for a plain text. The
// composer fills it from its "replying to" state (a bubble's Reply button puts it there, Escape or
// the strip's cancel takes it away), and the host turns the row id into the parent the protocol
// names. A retry passes what the failed row carried, so a reply retried is still a reply.
//
// `onReact` IS THE OTHER VERB A BUBBLE HAS: put `emoji` on the row `rowId` names, or take it off
// when `remove` is true. Same contract as onSend's answer - true means the host has queued it and
// will report the outcome by redrawing the thread, false means nothing was queued - and the same
// no-blocking rule.
//
// The host MUST NOT BLOCK in either: this runs on the UI thread and the send path's ABI call does
// a round trip to a server.
ThreadView MakeThread(std::function<void(std::wstring)> onSelectMessage,
                      std::function<void()> onDeselect,
                      std::function<bool(std::wstring, std::wstring, std::wstring)> onSend,
                      std::function<bool(std::wstring, std::wstring, bool)> onReact);
void SetThreadConversation(ThreadView& v, urmsg::demo::Conversation const& c);
void SetThreadSelectedMessage(ThreadView& v, std::wstring const& id);
// Can the host send RIGHT NOW? The composer's Send button is live only when this is true AND the
// box holds something AND MakeThread was given an onSend; a failed bubble's [ Try again ] is live
// on the same condition. Everything else about the composer — focus, typing, the caret — is
// untouched, because those are things this app can honestly do in either mode.
//
// Called by MainWindow on the beat it latches a live world, for the same reason SetThreadRunMode
// is: the composer bar is built ONCE and is not among the surfaces that beat rebuilds.
//
// `mayRoleSend` IS THE SECOND, SEPARATE FACT (item 242 R4): does this device's ROLE in the open
// conversation permit writing to it? False only for an OBSERVER — Conversation::myRole, which both
// worlds already carry, so R4 needed no new ABI. The two are kept apart because a composer that
// could only say "dark" would leave a reader unable to tell a missing session from a role that
// cannot write, and because the session answer is what the retry buttons and the bubble actions
// keep asking. The composer's three states are picked from the pair by ComposerStateFor
// (Views/ThreadLayout.h), session first.
void SetThreadSendEnabled(ThreadView& v, bool enabled, bool mayRoleSend);
// Re-point the composer caption at the wording for `mode`. The composer bar is
// built ONCE by MakeThread and is never rebuilt, so unlike every other surface
// that carries mode-dependent copy it cannot pick the new wording up from a
// rebuild; MainWindow calls this on the beat it latches a live world.
void SetThreadRunMode(ThreadView& v, urmsg::RunMode mode);
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

// ---- the send verb, where a surface that is not the composer can reach it ----
//
// THE COMPOSER IS NOT THE ONLY PLACE A MESSAGE CAN BE SENT FROM. A failed message's [ Try again ]
// is drawn deep inside MakeBubbleRow — a free function this header fixes, which takes a row and no
// host — and the inspect rail draws a SECOND [ Try again ] for the same failure, in another file
// entirely. Both are retries of one message through one verb, and neither can be handed a callback
// down the path it is built on without a signature change rippling through every caller.
//
// So the verb has one home. MakeThread fills it, SetThreadSendEnabled arms it, and a surface that
// needs it asks HERE rather than growing a parameter. `send` is null and `enabled` false in a host
// that wired none, which is what makes every retry in a fabricated launch inert without any of
// those surfaces having to know what mode the app is in.
struct ThreadSendVerb {
  std::function<bool(std::wstring text, std::wstring replacesRowId, std::wstring replyToRowId)> send;
  // The reaction verb, reached from the bubble's picker the same way.
  std::function<bool(std::wstring rowId, std::wstring emoji, bool remove)> react;
  // Put the composer into its "replying to" state for `row`. Filled by MakeThread, because only
  // the composer's own parts know where the strip is; a bubble's Reply button - built by the free
  // function MakeBubbleRow, which has no parts - reaches it here. Null before MakeThread ran.
  std::function<void(urmsg::demo::MessageRow const& row)> beginReply;
  bool enabled = false;
};
ThreadSendVerb const& SendVerb();
// True when a retry drawn right now would actually do something. The one predicate both [ Try
// again ] buttons ask, so the two can never disagree about whether this session can resend.
bool CanRetrySend();

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
