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
  // AN ADD FROM A JOIN CODE: the group must be held and not leaving, and need not be open, because
  // the first add of a group of one is the commit that opens it (the conversation-list design 2.3)
  Add,
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

// ── the push router (the conversation-list design 2.1) ─────────────────────────────────────────
//
// ONE PUSH WAITER PER DEVICE, and a push names the group it is for. An id this device holds wakes
// that group's loop and no other. The empty id says the session was replaced (lib device.go:882-883),
// and every subscription went with the old connection, so it wakes every loop. An id this device does
// not hold wakes no loop, and adopts nothing.
enum class PushRoute {
  One,   // wake the loop of the group the push names, and only that one
  All,   // wake every loop
  None,  // wake none: this device does not hold that group
};

// The key a pushed id is matched by: the hex the library answered, in lower case, as this app
// writes every group id (LiveMesh.cpp ToHex).
std::string PushGroupKey(std::string_view pushed);

// Which loops a push for `pushed` wakes, among the groups held (`heldIds`, each in lower case).
PushRoute RoutePush(std::vector<std::string> const& heldIds, std::string_view pushed);

std::wstring_view PushRouteName(PushRoute route);

// ── a new conversation (the conversation-list design 2.3, A7) ───────────────────────────────────
//
// ONE GROUP OF ONE AT A TIME. A group this device founds waits for its first member before it is
// open, and the library keeps no group of one across a restart (lib restore.go:240-254): a second
// one would be a second invitation to send, with one panel to send it from, and lost the same way.
// Every other group held, open or not, leaves the create alone, because a device holds many
// conversations.
struct HeldForCreate {
  std::string groupIdHex;
  bool groupOfOne = false;  // founded here, and nobody else in it yet
};

enum class CreateVerdict {
  Take,             // make the group
  GroupOfOneWaits,  // refused: `waiting` names the group of one, whose add panel opens again
};

CreateVerdict CreateAction(std::vector<HeldForCreate> const& held, std::string& waiting);

std::wstring_view CreateVerdictName(CreateVerdict verdict);

// ── the list's notice after a restore (the conversation-list design 2.5, A12) ───────────────────
//
// A GROUP THAT DOES NOT RESTORE STANDS BESIDE THE ONES THAT DO. The list's notice is the restore's
// sentence for its class, in Task 58's words (RefusalCopy.h RestoreNote), whatever else came back. A
// device that held one group dropped the sentence whenever any group came back, because the join
// dialog was then not opened at all; with many conversations that hid the one that did not. The
// library reports only its first failure, so one notice stands at a time.
std::string_view ListNoticeAfterRestore(std::string_view restoreError, int restoredCount);

// The notice after one more restore: a restore that could not be tried (this device is not
// connected) read nothing and leaves `previous` as it was; any other replaces it, with nothing
// when it read every group.
std::string NextListNotice(std::string_view previous, std::string_view restoreError,
                           int restoredCount);

// This file's --diagnose lines, in the "  key              : PASS  detail" shape.
std::vector<std::wstring> CollectConversationRulesDiagnostics();

}  // namespace urmsg::live
