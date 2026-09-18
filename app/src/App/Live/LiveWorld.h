// THE REAL CONVERSATION, IN THE SHAPE THE EXISTING VIEWS ALREADY TAKE.
//
// The UI components built against Demo/DemoWorld.h are the valuable output of the demo exercise
// and they are KEPT. What is thrown away is the fabricated DATA behind them. So this file does one
// job: turn what the message protocol actually hands over into a urmsg::demo::World, and hold the
// most recent one for the UI thread to pick up.
//
// PURE C++, NO winrt, NO XAML. It is written on the live worker thread and read on the UI thread,
// and the handover is a shared_ptr to an IMMUTABLE snapshot rather than a lock around a mutable
// world: a reader that has the pointer holds the whole world it started drawing, and a writer
// publishing a newer one cannot pull it out from under them.
//
// ── THE PLACEHOLDER, WHICH IS THE ONE DESIGN DECISION IN THIS FILE ───────────────────────────
//
// The protocol does not carry delivery state past "the server took it", sender names, group names,
// member lists or unread counts. NOT ONE OF THOSE IS INVENTED HERE. Every one of them renders as
// kUnavailable — one string, used everywhere, VISIBLE rather than hidden. Hiding the field would
// let a reader assume the app simply has nothing to show; fabricating it would be the demo's lie
// with a real conversation behind it. Saying "unavailable" is the only one of the three that is
// true.
//
// WHAT IS *NOT* UNAVAILABLE, and the distinction is the whole honesty of this file:
//   * the message TEXT, its kind, its reply-to, its deleted bit and its reactions are REAL — they
//     came off a record this device opened.
//   * `outgoing` is real: the ABI's `mine` says this device sealed it.
//   * DeliveryState::Sent on an outgoing message is real and is the CEILING: the server
//     acknowledged the submit. Delivered and Read are never set, because nothing reports them.
//   * the sender HANDLE is real (16 opaque octets) and is not a name. It is shown as itself, in
//     the inspect rail, beside the name that is unavailable.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::live {

// ── what the ABI hands over, transcribed ──────────────────────────────────────
//
// One struct per ABI concept, filled by LiveMesh.cpp from urnet_message_list_info /
// _list_body / _list_reaction_info. It exists so that the JSON parsing lives with the ABI calls
// and the World mapping lives here, and neither file has to know the other's half.

struct LiveReaction {
  std::string emoji;  // utf-8, RAW: two spellings of one emoji are two reactions
  bool mine = false;
};

struct LiveMessage {
  uint64_t recordId = 0;
  std::string messageId;     // 64 lower-case hex, the name a reply/reaction quotes
  std::string senderHandle;  // 32 lower-case hex; 16 opaque octets and NOT a name
  bool mine = false;
  int64_t sentAtMs = 0;
  uint8_t kind = 0;
  // "" on a message that is a message. NON-EMPTY means something IS at this position that this
  // build cannot show — branch on this BEFORE kind, because a gap carries the code it arrived
  // under rather than what it is.
  std::string gap;
  std::string replyToId;  // the parent's message_id on a REPLY, "" everywhere else
  bool deleted = false;
  int32_t bodyLen = 0;
  std::string body;  // octets, byte for byte off the ABI. Not validated as text by the protocol.
  std::vector<LiveReaction> reactions;
};

// Everything one live session knows about itself. All of it is observed; none of it is invented.
struct LiveGroup {
  std::string groupIdHex;
  uint64_t epoch = 0;
  bool open = false;
  std::string clientId;        // this device's operator client_id
  std::string serverClientId;  // the message server this transport is bound to
  std::string platformUrl;     // what the library actually dialled
  std::string host;            // the operator host asked for
  std::string statsJson;    // urnet_message_group_stats, verbatim
  std::vector<LiveMessage> messages;
};

// ── the one placeholder ───────────────────────────────────────────────────────
// It is urmsg::demo::kUnavailable (Demo/DemoWorld.h) and it lives there rather than here so that
// the views which RENDER it — the inspect rail, the network page — can name it without any of them
// acquiring a dependency on this directory. One spelling, one presentation, no second copy.
using urmsg::demo::kUnavailable;

// ── the mapping ───────────────────────────────────────────────────────────────
// Build the World the existing views read. Total: one conversation, one row per message.
urmsg::demo::World BuildWorld(LiveGroup const& group);

// ── the handover ──────────────────────────────────────────────────────────────

using WorldPtr = std::shared_ptr<const urmsg::demo::World>;

// Called on the LIVE WORKER THREAD. Replaces the standing snapshot and then fires the callback
// SetOnPublish registered, still on the worker thread — the callback's job is to marshal, not to
// draw.
void Publish(urmsg::demo::World world);

// The standing snapshot, or nullptr when the live path has never published. Safe from any thread.
WorldPtr Snapshot();

// Register the "a new world is available" notification. Fires on the worker thread; the registrant
// must marshal to the UI thread itself. Pass {} to clear it. Safe from any thread.
void SetOnPublish(std::function<void()> onPublish);

// How many times Publish has been called. The UI uses it to drop a stale marshalled beat rather
// than rebuilding the list once per queued notification.
uint64_t Generation();

}  // namespace urmsg::live
