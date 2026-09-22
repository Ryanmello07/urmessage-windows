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
// member NAMES, device presence or unread counts. NOT ONE OF THOSE IS INVENTED HERE. Every one of them renders as
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
//   * THE ROSTER AND ITS ROLES ARE REAL (item 242 R3): every leaf of the group at its current
//     epoch, each with the role the transcript-covered policy gives it, off
//     urnet_message_group_members; this device's own role off urnet_message_group_my_role. What
//     is NOT real about a roster row is who the leaf belongs to - no identity layer exists - so
//     every row's name is the placeholder, its identity public key is shown as itself, and its
//     presence (a device list this protocol does not carry) is the placeholder too.
//   * a role change THIS DEVICE asked for is drawn on the member it names, as the library's own
//     answer: pending, refused by role, lost the epoch race, invalid, or failed in transport. A
//     change that succeeds is not drawn at all - the roster itself moves, in the same publish.
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

// ── a send that is not (yet) a record ─────────────────────────────────────────
//
// WHY THIS IS NOT A FABRICATION, which is the only question worth asking about a row the server
// has never heard of. A refused send produces NO record: urnet_message_group_send answers NULL and
// an out_error, the group's `submitted` counter does not move, and urnet_message_group_messages
// therefore hands back exactly what it handed back before the attempt. So a UI built only from the
// ABI's log cannot say a send failed — the message a person typed and watched disappear leaves no
// trace anywhere but the log file. What each of these rows states is a thing THIS APP did: it took
// those octets and tried to seal them, and the outcome was the error it carries.
//
// THE ENTRY IS SHORT LIVED BY CONSTRUCTION. A send that succeeds drops its entry in the same beat
// the real record appears in the log, so one message is never two rows; only a failure survives a
// publish, and only until it is retried or the session ends.
struct LiveOutboxEntry {
  std::string localId;      // stable for this session; the thread's row id is "outbox-" + this
  std::string body;         // utf-8, byte for byte what the composer held
  // The parent's message_id (64 hex) when this is a REPLY, "" for a plain text. It is what the
  // worker hands to urnet_message_group_send_reply, and it is what draws the parent line above the
  // pending row - the same line a far-side reply gets, because it is the same fact.
  std::string replyToId;
  int64_t attemptedAtMs = 0;  // wall clock at the attempt — the row's time label, not a guess
  bool failed = false;      // false while the ABI call is still in flight
  std::string error;        // the ABI's own out_error, verbatim; non-empty only when failed
};

// A REACTION THIS DEVICE IS TRYING TO PUT ON A MESSAGE, OR TAKE OFF ONE, that the server has not
// yet taken or has refused. The same argument as the entry above: a refused react leaves no record
// and moves no counter, so without this a person who tapped an emoji and saw nothing appear would
// have no way to learn the library refused it. It renders ON THE TARGET, as one entry of that
// row's reactions with state Pending or Failed - never as a standing reaction, which is what a
// record this device holds looks like and what a refused call is not.
//
// An entry is dropped in the same beat its call succeeds: the standing reaction is then in the
// group's own log (the library applies this device's own reaction on submit), so one tap is never
// two chips.
struct LiveReactionOutboxEntry {
  std::string localId;
  std::string targetId;     // the message_id (64 hex) the reaction is about
  std::string emoji;        // utf-8, RAW, exactly the spelling the picker sealed
  bool remove = false;      // true for an un-reaction
  int64_t attemptedAtMs = 0;
  bool failed = false;
  std::string error;
};

// ── the roster, transcribed ───────────────────────────────────────────────────
//
// One row per leaf, filled from urnet_message_member_list_info's json. The identity key is the
// value the two role verbs take, so it is carried as the hex the ABI hands over and passed back
// as is.
struct LiveMember {
  uint32_t leafIndex = 0;
  std::string senderHandle;  // 32 lower-case hex; joins a message to its roster row
  std::string identityPub;   // lower-case hex; what set_role / transfer_ownership name
  std::string role;          // "owner" | "admin" | "member" | "observer"
  bool mine = false;         // this device's own leaf; exactly one row per leaf it holds
};

// A ROLE CHANGE THIS DEVICE ASKED FOR that the library has not yet answered, or answered with
// anything but OK. Same argument as the outbox entries above: a refused or lost change moves no
// roster and leaves no record, so without this a person who pressed "Make admin" and saw nothing
// move would have no way to learn why. An entry is dropped in the beat its call answers OK: the
// roster read on that same publish already shows the change.
struct LiveRoleOutboxEntry {
  std::string localId;
  std::string identityPub;    // the member named, as the roster spells it
  std::string role;           // the role asked for; "owner" for a transfer
  int64_t attemptedAtMs = 0;
  bool done = false;          // false while the call is inside the library
  int32_t kind = 0;           // URNET_MESSAGE_COMMIT_* once done; meaningful when done
  std::string error;          // the ABI's own out_error, verbatim; "" for OK
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
  // What this device tried to send and the server has not (or will not) take. Ordered by attempt.
  std::vector<LiveOutboxEntry> outbox;
  // Likewise for reactions and un-reactions; each one lands on its target row rather than at the
  // foot, because a reaction is a change to a line and not a line.
  std::vector<LiveReactionOutboxEntry> reactionOutbox;
  // The roster at this epoch, in leaf order, and this device's own role. Both re-read on every
  // publish, because a commit from another member may have changed either.
  std::vector<LiveMember> members;
  std::string myRole;
  // And the role changes this device asked for that have not landed; each lands on the member
  // row it names.
  std::vector<LiveRoleOutboxEntry> roleOutbox;
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
