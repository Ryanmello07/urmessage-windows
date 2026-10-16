// THE CONVERSATION LIST'S PURE RULES, and their --diagnose lines (the conversation-list design,
// scratchpad/convlist/DESIGN.md, accepted as R59).
//
// A live device holds many groups. What the list shows, in what order, which conversation an action
// goes to, which loop a push wakes, when a new conversation may be made, and which one opens: every
// one of those is decided here, by a pure function, so --diagnose can hold each to its rule before
// the apartment exists and a mutant can put the one-group design back and be seen to fail.
//
// PURE C++, NO winrt, NO XAML, NO SDK: CollectDiagnostics calls this before winrt::init_apartment,
// and the live worker calls the same functions on its own thread.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace urmsg::live {

// ── the gate every action asks (the conversation-list design 2.2) ──────────────────────────────
//
// ONE GATE PER GROUP, and an action is done to the group it names and to no other. Each action is
// stamped with that group's id and its INCARNATION, the number this device gave the group when it
// took it up (restored, joined or founded). A group that is left and joined again is a new
// incarnation, so nothing queued before the leave is ever done to the new membership: the job the
// one global "pass" did while a device held one group.
struct GroupGate {
  std::string groupIdHex;
  uint64_t incarnation = 0;
  bool open = false;     // the server says the group is open, so a commit may go
  bool leaving = false;  // a leave of this group is in flight, so nothing else is taken for it
};

enum class ActionClass {
  Commit,         // a send, a reply, a reaction, a deletion, a role change, a hand-over, a removal
  Leave,          // a plain leave: the group must be held, and need not be open
  LeaveHandOver,  // a leave that hands the group over first: that is a commit, so it must be open
};

enum class GateVerdict {
  Take,     // queue it, stamped with the group's incarnation
  NotHeld,  // this device holds no group of that id
  Closed,   // the group is not open on the server, and this action needs it to be
  Leaving,  // a leave of this group is in flight
};

// The verdict for an action of class `cls` naming `groupIdHex`, among the groups held.
GateVerdict GateAction(std::vector<GroupGate> const& gates, std::string const& groupIdHex,
                       ActionClass cls);

// Is an action stamped (groupIdHex, incarnation) still for a group this device holds, in that
// incarnation? The worker asks this of every queued action before it does it.
bool ActionStillHeld(std::vector<GroupGate> const& gates, std::string const& groupIdHex,
                     uint64_t incarnation);

// The gate of `groupIdHex`, or nullptr.
GroupGate const* FindGate(std::vector<GroupGate> const& gates, std::string const& groupIdHex);

std::wstring_view GateVerdictName(GateVerdict verdict);

// This file's --diagnose lines, in the "  key              : PASS  detail" shape.
std::vector<std::wstring> CollectConversationRulesDiagnostics();

}  // namespace urmsg::live
