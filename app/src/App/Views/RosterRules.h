// The roster's decisions that are text and arithmetic, with no element tree
// behind them: what a role is called, which controls a viewer of one role gets
// on a member of another, what the controls are called in both arms, and what
// the note under a member says while this device is changing its role and after
// the library has answered.
//
// PURE C++. No winrt/ include and no XAML type in this header or its .cpp, for
// the same reason StatusStripRules.h has none: Startup.cpp's CollectDiagnostics()
// asserts these, and wWinMain calls that function BEFORE winrt::init_apartment().
// The XAML half - the rows, the buttons, the confirmation - is
// Views/InspectRailView.cpp, which spends these and adds no wording of its own.
//
// THE RULES ARE MASTER SECTION 11'S TABLE AND THE R2 RULINGS, transcribed. The
// library refuses on BOTH arms (the committing client before it builds, every
// receiver on validation), so a control offered here that the library would
// refuse is not a security hole; it is a button that does nothing and then says
// so. The table below offers exactly what the library permits, so that the
// REFUSED answer is reachable only through a race with a role change that landed
// first - which is why the note for it still exists.
//
// No Localized() call and no resw key: demo copy is English string literals
// (design section 9.4), the same rule every pure-rules file here follows.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// The word a row draws for a role: "Owner", "Admin", "Member", "Observer" - the
// protocol's four spellings, capitalised, and nothing else. A role that is none
// of the four draws demo::kUnavailable, never an invented fifth word: it is the
// one placeholder, and a role this build does not know is a fact it cannot
// state.
std::wstring RoleWord(std::wstring_view role);

// The member row's title: "<name> (U+00B7 MIDDLE DOT) <RoleWord>", and "You" for the row that is
// this device's own leaf, whatever name the source gave it. The role is IN THE
// TITLE, in words - never colour alone, and never only in the expanded detail -
// so a collapsed roster already answers who may do what.
std::wstring MemberRowTitle(demo::MemberRef const& member);

// The four verbs a roster control can ask for. The first three are
// urnet_message_group_set_role with the role named; the fourth is
// urnet_message_group_transfer_ownership.
enum class RoleVerb { MakeAdmin, MakeMember, MakeObserver, TransferOwnership };

// The role a verb asks the ABI for, in the protocol's spelling (kRoleAdmin,
// kRoleMember, kRoleObserver; kRoleOwner for a transfer).
std::wstring_view RoleVerbTarget(RoleVerb verb);

// THE CONTROL SET. What a viewer holding `myRole` may do to a member holding
// `theirRole`, per MASTER section 11's table and rulings 4 and 15:
//   * the OWNER may make anyone an admin, a member or an observer, and may
//     transfer ownership to any current member (becoming an admin);
//   * an ADMIN may set MEMBER or OBSERVER, and may not touch an admin or the
//     owner (the admin set is the owner's alone);
//   * a MEMBER or an OBSERVER gets nothing.
// Empty on the viewer's own row (`theirRowIsMine`) and on the owner's row -
// ownership moves only through a transfer, which is offered on the OTHER rows.
// A verb naming the role the member already holds is left out: the library
// answers OK and commits nothing, so it would be a control that does nothing.
// The order is the order the buttons are drawn in.
std::vector<RoleVerb> RoleControlsFor(std::wstring_view myRole, std::wstring_view theirRole,
                                      bool theirRowIsMine);

// The button's text: "Make admin", "Make member", "Make observer",
// "Transfer ownership".
std::wstring RoleControlLabel(RoleVerb verb);

// The button's automation name in both arms, keyed off the CAPABILITY (the rail's
// CanChangeRoles - a live session with an open group) and not the run mode,
// exactly as the bubble actions and the [ Try again ] pair are: a --live launch
// whose mesh has not answered yet draws the fabricated world with no session,
// and "no live session" is the true sentence there. Never empty; the two arms
// differ; the dark arm carries "no live session".
std::wstring RoleControlName(RoleVerb verb, bool canAct);

// The transfer confirmation's copy. Ownership is the one change here with no
// undo by construction (MASTER section 11: the outgoing owner becomes an admin
// and only the new owner can give it back), so it is the one control behind a
// confirmation. `title`, `body`, the primary button and the close button.
std::wstring TransferConfirmTitle();
std::wstring TransferConfirmBody();
std::wstring TransferConfirmPrimary();
std::wstring TransferConfirmClose();

// The note drawn under a member while this device is changing its role and after
// the library has answered anything but OK. Empty for RoleActionState::None.
// THE THREE OUTCOMES A PERSON CAN ACT ON ARE THREE DIFFERENT SENTENCES: a
// refusal by role (retrying answers the same; the rule's own sentence is shown),
// a lost epoch race ("someone else changed the group first" - fetch and try
// again), and a transport failure (the library's reason, verbatim). INVALID is a
// fourth, and it is this app's bug rather than the person's: it is named as such.
std::wstring RoleActionNote(demo::MemberRef const& member);

// Which of the two words the MEMBERS caption spends for presence when a member
// list carries device presence and when it does not. A roster read off the
// protocol has no devices (presence is not carried), and "0/0 online" would be
// a claim about people who may be online right now.
std::wstring MembersCaptionMeta(demo::Conversation const& conv);

// The member row's meta (right-hand words): the device presence in words when
// the member has devices, demo::kUnavailable when it has none.
std::wstring MemberRowMeta(demo::MemberRef const& member);

}  // namespace urmsg::views
