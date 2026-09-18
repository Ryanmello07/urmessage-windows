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

}  // namespace urmsg::live
