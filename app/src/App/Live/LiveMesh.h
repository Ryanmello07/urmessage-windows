// THE APP'S FIRST REAL CONNECTION. Off by default; see IsEnabled().
//
// This is the half of the app that talks to the URnetwork message mesh through
// the SDK's C ABI (vendor/urnetwork-sdk/include/urnetwork_message.h) instead of
// through the fabricated world. It stands up a platform-attached client from a
// credential on disk, binds a transport to the alpha message server, says Hello
// across the operator's reconnect window, and fetches. What it produces is LOG
// LINES — it renders nothing and touches no XAML.
//
// WHY THE HEADER OF THIS FILE IS SO SHORT AND THE .cpp IS NOT: everything that
// is subtle here is a property of the ABI and of the deployed operator, so it is
// documented at the call it constrains rather than summarised up here where it
// would drift.
//
// TWO RULES THIS INTERFACE EXISTS TO KEEP:
//
//   1. NOTHING BLOCKS THE UI THREAD. The ABI's connect blocks for tens of
//      seconds by design, and this app's apartment is single-threaded; a
//      std::thread::join() on the UI thread does not pump messages (main.cpp
//      states this at its own redirect worker, :112). So Start() DETACHES and
//      the caller gets nothing to wait on. There is deliberately no Stop() and
//      no join: the worker owns its handles and closes them itself.
//
//   2. ONE CLIENT PER ACCOUNT, EVER. The same client_id connected twice — or
//      two state directories descended from one — is two devices at one MLS
//      leaf: one sender_handle, one stream counter, and therefore a reused
//      (epoch, sender_handle, stream_index), which is a reused nonce under a
//      reused record key. The spec calls that a total break of both AEADs. So
//      Start() must be called only from the instance that OWNS the
//      single-instance key (main.cpp calls it after that check), and the state
//      directory it uses is its own and is never a copy of another one.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <memory>
#include <string>

namespace urmsg::live {

// Is the live path switched on for this launch? True only when
// %URMESSAGE_LIVE% is "1" or the command line carries --live.
//
// THE DEFAULT IS OFF AND THAT IS LOAD BEARING FOR NOW: the diagnostic suite runs
// on every launch and CI reads its output, so an ordinary launch must behave
// exactly as it did before this file existed. Nothing here runs, and the SDK
// dll is not even loaded, unless this answers true (the import is delay-loaded;
// see App.vcxproj).
bool IsEnabled();

// Start the worker on a detached, guarded background thread, if IsEnabled().
// Returns true when a thread was started. Never throws and never blocks.
//
// CALL THIS ONLY FROM THE PRIMARY INSTANCE — see rule 2 above.
bool StartIfEnabled();

// ── sending ───────────────────────────────────────────────────────────────────

// CAN A MESSAGE BE SENT RIGHT NOW? Not "was --live asked for" (IsEnabled) and not "is a live world
// on screen" (urmsg::ActiveRunMode): this is the narrower question the composer's Send button has
// to ask, and all three of its clauses are load bearing —
//
//   * the worker is running at all (so: a --live launch, past the credential and the dll),
//   * the device said Hello and was routed to,
//   * and it holds a group the SERVER says is open.
//
// The third is the one that is easy to drop and is the reason this exists. A device that founded a
// group nobody joined has a group handle, an epoch and a message log, and every send into it is
// refused by the server — so a button gated on "there is a group" is an enabled button that cannot
// work, which is precisely what design §9.1 forbids. Answered from an atomic the worker re-reads
// off urnet_message_group_is_open each poll, so a group that closes under the app turns the button
// off within one fetch period rather than at the next click.
//
// Safe from any thread and never blocks.
bool CanSend();

// SEAL AND SUBMIT `utf8Body` TO THE OPEN GROUP, on the worker. Answers false — having queued
// NOTHING — when CanSend() is false or the body is empty; true means the worker has taken it and
// the outcome will arrive as a published world, never as a return value here.
//
// NEVER BLOCKS, AND THAT IS THE WHOLE REASON IT IS A QUEUE. urnet_message_group_send does a round
// trip to the message server inside one call; on the UI thread of a single-threaded apartment that
// is a frozen window for as long as the server takes. So the UI hands the octets over and goes
// back to pumping messages, and the worker — which already owns every handle, and is the only
// thread allowed to touch them — makes the call between two fetches.
//
// `replacesLocalId` NAMES A FAILED OUTBOX ENTRY THIS SEND IS A RETRY OF, or is empty for a fresh
// one. A retry that queued a new entry without naming the old one would leave the failed row
// standing beside its own retry, which reads as two messages where the person wrote one.
//
// `replyToMessageIdHex` NAMES THE PARENT WHEN THIS IS A REPLY - the 64 lower-case hex characters
// urnet_message_list_info hands over as message_id - and is empty for a plain text. It is decoded
// to the 32 octets the ABI takes HERE, at the queue, so that a name that does not decode is refused
// with the text still in the box rather than refused by the worker after the box has emptied. The
// worker then calls urnet_message_group_send_reply instead of _send; nothing else differs.
bool QueueSend(std::string utf8Body, std::string replacesLocalId = {},
               std::string replyToMessageIdHex = {});

// PUT A REACTION ON A MESSAGE, OR TAKE ONE OFF, on the worker. `targetMessageIdHex` is the
// message_id of the line it is about, `utf8Emoji` is the RAW spelling to seal (the ABI checks it
// as 1..64 octets of valid UTF-8 and folds nothing, so the spelling here is the spelling every
// member sees and the spelling a later unreact must repeat exactly), and `remove` chooses
// urnet_message_group_unreact over _react. Answers false, having queued nothing, when CanSend() is
// false, the emoji is empty, or the id does not decode. The outcome arrives as a published world:
// the reaction standing on the target row, or a Failed entry there carrying the library's reason.
bool QueueReaction(std::string targetMessageIdHex, std::string utf8Emoji, bool remove);

// ── the two role verbs (item 242 R3) ──────────────────────────────────────────

// CHANGE ONE MEMBER'S ROLE, on the worker: urnet_message_group_set_role with `identityPubHex` (the
// identity_pub a roster row carries, passed back as is) and `role` ("admin", "member" or
// "observer" - "owner" is refused here, because ownership moves only through the call below).
// Answers false, having queued nothing, when CanSend() is false, the identity is not hex, or the
// role is none of the three. The outcome arrives as a published world: the roster itself moved
// (OK), or a note on the member naming which of the ABI's four other answers came back.
//
// ONE ROUND TRIP INSIDE ONE ABI CALL, like a send, and so queued for the same reason: the worker
// owns the group handle and the UI thread must never wait on the server.
bool QueueRoleChange(std::string identityPubHex, std::string role);

// TRANSFER OWNERSHIP TO ONE MEMBER, on the worker: urnet_message_group_transfer_ownership. This
// device, the outgoing owner, becomes an admin in the same commit (MASTER section 11, ruling 4).
// The CONFIRMATION is the caller's - the rail asks before it calls this - because this is the one
// change here with no undo by construction. Same answers as QueueRoleChange.
bool QueueTransferOwnership(std::string identityPubHex);

// REMOVE ONE MEMBER FROM THE GROUP, on the worker: urnet_message_group_remove_member with the
// identity_pub a roster row carries, passed back as is. Answers false, having queued nothing, when
// CanSend() is false or the identity is not hex. Same five answers as the two verbs above, drawn
// the same way.
//
// ONE CALL TAKES EVERY LEAF THAT IDENTITY HOLDS, and that is the ABI's contract rather than this
// app's loop (ruling 49): a person with a phone and a laptop in the group is two leaves and one
// identity, and a removal that took one would leave the other reading everything. So there is no
// per-device verb here and this one is keyed on the identity, which is also why the confirmation
// says "every device this member holds".
//
// THE CONFIRMATION IS THE CALLER'S, exactly as it is for a transfer: the rail asks before it calls
// this, because nothing in this product can put the member back. Re-admitting needs a fresh key
// package from them (device.KeyPackage is single use and is consumed destructively at the join),
// and the alpha's one-credential peer cannot mint a second one at all.
bool QueueRemoveMember(std::string identityPubHex);

// ── getting into a group at all (the onboarding half) ─────────────────────────
//
// WHY THIS EXISTS. Until now the only way into a group was a FILE HANDSHAKE with sdk/livepeer
// running on the same machine: this device wrote `app.keypackage` beside its credential and waited
// for somebody to drop `app.invite` back. That is a developer's flow. A person on their own
// computer cannot run a Go binary under a second account, and two people on two machines have no
// shared directory at all.
//
// WHAT REPLACES IT IS TEXT, because text goes down every channel people already have. A JOIN CODE
// is this device's key package, base64. An INVITE CODE is the Welcome that answers it, base64. One
// is pasted out of this app and sent to whoever holds the group; the other is pasted back in.
//
// THE TWO ARE NOT THE SAME KIND OF SECRET AND THE UI MUST NOT TREAT THEM ALIKE:
//   * a JOIN CODE is a public offer to be added. Showing it to the wrong person costs nothing;
//     they can add this device to a group it will simply ignore.
//   * an INVITE CODE carries the group's secrets. WHOEVER READS IT IS IN THE GROUP. It is consumed
//     on use and must never be stored, logged or shown twice.
// Neither is ever written to the log by this module, and the code text itself is never formatted
// into any log line.

enum class OnboardStep {
  Idle,       // no group, nothing asked for yet
  Waiting,    // a join code exists and this device is waiting to be let in
  Joining,    // an invite is being parsed and applied
  Joined,     // there is a group; the fetch loop owns the session now
  Refused,    // the last invite or add was refused, and `message` says why
  // ── the founder's road, which is the other half of the same screen ──────────
  Founded,    // this device made a group. IT CANNOT SEND YET: a group of one sits at epoch 0 and
              // is not open on the server, and the commit that opens it is the one that adds the
              // first other person. So this state is "waiting for somebody's join code", not
              // "ready".
  Invited,    // an add succeeded and `inviteCode` holds the invitation to send to that person
};

struct OnboardState {
  OnboardStep step = OnboardStep::Idle;
  // The join code to show, base64. Empty until the device has minted one.
  std::string joinCode;
  // AN INVITATION THIS DEVICE JUST MINTED FOR SOMEBODY ELSE, base64, and empty except at Invited.
  // Set by an add that succeeded and cleared by the next publication, because an invitation is
  // used ONCE: leaving it on screen past its moment invites a second person to be sent the same
  // one, and the second of them is refused at the join with no way to tell why from here.
  std::string inviteCode;
  // One sentence for the person, in their words rather than the library's. Never empty except at
  // Idle, and never carries a code.
  std::string message;
};

using OnboardPtr = std::shared_ptr<const OnboardState>;

// The most recent onboarding state, or nullptr before the worker has published one. Safe from any
// thread; the pointer is swapped under a lock and the pointee is immutable, exactly as the world
// snapshot is.
OnboardPtr OnboardSnapshot();

// Bumped on every publication, so the UI can tell a new state from the one it already drew.
uint64_t OnboardGeneration();

// HAND A PASTED INVITE TO THE WORKER. Answers false, having queued nothing, when the text is empty
// or is not base64 this build can decode - refused HERE so that a typo is a message beside the box
// rather than a failure thirty seconds later in a log nobody is reading. True means the worker has
// taken it; the outcome arrives as a published OnboardState, never as a return value.
//
// IT IS SAFE TO CALL BEFORE THE MESH ANSWERS and it is safe to call twice: the worker consumes one
// invite at a time and the second replaces the first, because a person who pastes again has
// corrected themselves rather than asked for two groups.
bool QueueJoinFromInviteCode(std::string base64Invite);

// MAKE A GROUP, with this device as its owner. Answers false, having queued nothing, when this
// device already holds one - a device is in one group in this build, and a second create would
// quietly replace the group on screen.
//
// WHAT COMES BACK IS NOT A USABLE GROUP AND THE UI MUST NOT SAY IT IS. A group of one stands at
// epoch 0 and is not open on the server; every send into it is refused. The commit that opens it
// is the one that adds the FIRST other person, which is why OnboardStep::Founded's message is
// about somebody else's join code rather than about being ready.
bool QueueCreateGroup();

// BRING SOMEBODY IN, from the join code they sent. Answers false, having queued nothing, when
// there is no group or the text is not a decodable code.
//
// TWO DIFFERENT ABI CALLS SIT BEHIND THIS AND THE WORKER CHOOSES BY ASKING THE GROUP, not by
// counting: urnet_message_group_add_member builds the FOUNDING commit and is refused once the
// group is open, and urnet_message_group_add_member_and_publish is refused before it. The worker
// branches on urnet_message_group_is_open, so the first person in and the fifth take the same
// road through this call.
//
// THE OUTCOME IS AN INVITATION TO SEND BACK, published as OnboardState::inviteCode. A refusal -
// a role that may not add, a lost epoch race, a stale join code - arrives as Refused with the
// library's own sentence.
bool QueueAddMemberFromCode(std::string base64JoinCode);

// ── how this app reaches the message server (ledger 268) ──────────────────────

// The Settings switch "Route through URnetwork", persisted as route_through_urnetwork and ON
// by default. ON: the connection to the server's own endpoint leaves through a URnetwork exit,
// so the message server never sees this computer's address. OFF: connect directly, which
// shows it. %URMESSAGE_ROUTE% (urnetwork | direct | platform) overrides it for one launch. The
// live worker reads it when it starts, so a change applies the next time the app connects.
bool RouteThroughUrnetwork();
void SetRouteThroughUrnetwork(bool on);

}  // namespace urmsg::live
