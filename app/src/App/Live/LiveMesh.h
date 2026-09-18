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
bool QueueSend(std::string utf8Body, std::string replacesLocalId = {});

}  // namespace urmsg::live
