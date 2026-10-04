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

// The five verbs a roster control can ask for. The first three are
// urnet_message_group_set_role with the role named; the fourth is
// urnet_message_group_transfer_ownership; the fifth is
// urnet_message_group_remove_member.
//
// REMOVE IS NOT A ROLE CHANGE AND THE ENUM IS THE ONLY PLACE THEY SIT TOGETHER.
// It shares this type because it shares everything a roster control needs - a
// gate keyed on the viewer's role and the member's, a label, an automation name
// in three states, and a commit whose five answers RoleActionNote already draws
// - and it differs in the one way RoleVerbTarget below makes explicit: there is
// no role it asks for. A member it takes out of the group holds no role
// afterwards, because it is not in the group.
enum class RoleVerb { MakeAdmin, MakeMember, MakeObserver, TransferOwnership, Remove };

// The role a verb asks the ABI for, in the protocol's spelling (kRoleAdmin,
// kRoleMember, kRoleObserver; kRoleOwner for a transfer).
//
// EMPTY FOR RoleVerb::Remove, which is the one verb that asks for no role.
// Returning a role there would be a lie with a plausible shape - every caller
// would spend it on a set_role - so it answers empty and InspectRailView
// branches on the verb before it ever reads this. --diagnose asserts the four
// are non-empty and distinct AND that the fifth is empty, because a default
// arm that quietly answered kRoleMember is exactly the defect this comment
// exists to prevent.
std::wstring_view RoleVerbTarget(RoleVerb verb);

// THE CONTROL SET. What a viewer holding `myRole` may do to a member holding
// `theirRole`, per MASTER section 11's table and rulings 4 and 15:
//   * the OWNER may make anyone an admin, a member or an observer, and may
//     transfer ownership to any current member (becoming an admin);
//   * an ADMIN may set MEMBER or OBSERVER, and may not touch an admin or the
//     owner (the admin set is the owner's alone);
//   * a MEMBER or an OBSERVER gets nothing.
//   * and REMOVE is offered wherever a role control is (item 242 R2): an admin
//     or the owner may remove any member they can already act on. It needs no
//     clause of its own, and that is a property of the order rather than an
//     omission - the admin-on-admin row has already returned empty above, so by
//     the time Remove is pushed an admin's target can only be a member or an
//     observer, which is R2's rule exactly. R3 (only the owner removes an admin)
//     is therefore held by the SAME early return that holds it for MakeAdmin.
// Empty on the viewer's own row (`theirRowIsMine`) and on the owner's row -
// ownership moves only through a transfer, which is offered on the OTHER rows.
// A removal of the owner is refused by the library by name and a removal of
// one's own last leaf answers mls.ErrRemoveCommitter, so neither is offered.
// A verb naming the role the member already holds is left out: the library
// answers OK and commits nothing, so it would be a control that does nothing.
// The order is the order the buttons are drawn in.
std::vector<RoleVerb> RoleControlsFor(std::wstring_view myRole, std::wstring_view theirRole,
                                      bool theirRowIsMine);

// The button's text: "Make admin", "Make member", "Make observer",
// "Transfer ownership".
std::wstring RoleControlLabel(RoleVerb verb);

// Why a role control is dark, when it is. A button goes dark for two unrelated
// reasons, and to someone who cannot see it they are not the same sentence:
// there is no session to change roles in, or this member's previous change is
// still inside the library. Saying the first while the second is true denies a
// session AT THE ONE MOMENT IT IS PROVABLY LIVE - the app is inside
// urnet_message_group_set_role while the words are being read out.
enum class RoleControlState {
  Live,       // the control acts
  Pending,    // this member's previous change has not come back yet
  NoSession,  // no live session with an open group
};

// The button's automation name in each state, keyed off the CAPABILITY (the
// rail's CanChangeRoles - a live session with an open group) and not the run
// mode, exactly as the bubble actions and the [ Try again ] pair are: a --live
// launch whose mesh has not answered yet draws the fabricated world with no
// session, and "no live session" is the true sentence there. Never empty; the
// three arms differ; NoSession carries "no live session" and Pending does not.
//
// EXACTLY ONE ARM OF THE TWELVE CLAIMS WHAT A ROLE ENFORCES, and item 242's R4
// is why it is one rather than none. The clause was taken out before R3 shipped
// because no send path then read the role, so "who can read but not send" named
// a gate the build did not have. R4 built it - the sdk refuses all four sendable
// kinds for a group it holds OBSERVER in, and this app's composer is disabled
// with Spec C section 5.6's own sentence - so the words are back on the
// Make-observer control's LIVE arm, and on no other cell: the pending and dark
// arms are about the CONTROL's state, and a sentence about the group read out
// there is R3's mistiming defect in the other mode's clothes. --diagnose's
// `roster names` line was INVERTED to match (required in that one cell,
// forbidden in the other eleven); the .cpp says the same thing where the strings
// are, which is where a copy edit will be standing.
std::wstring RoleControlName(RoleVerb verb, RoleControlState state);

// The transfer confirmation's copy. Ownership is the one change here with no
// undo by construction (MASTER section 11: the outgoing owner becomes an admin
// and only the new owner can give it back), so it is the one control behind a
// confirmation. `title`, `body`, the primary button and the close button.
std::wstring TransferConfirmTitle();
std::wstring TransferConfirmBody();
std::wstring TransferConfirmPrimary();
std::wstring TransferConfirmClose();

// The removal confirmation's copy, and removal is behind one for the same reason
// a transfer is: there is no undo in this product. Ruling 48 made leaving a
// group product surface rather than an MLS proposal, and the mirror of that is
// that re-admitting somebody is a fresh AddMember with a fresh key package -
// which this deployment's one-credential peer cannot even produce. So the body
// says what the person cannot take back, and says what the removed member keeps,
// because the honest sentence is not "their messages are gone": the transcript
// up to the epoch they were removed at is theirs and stays readable to them.
std::wstring RemoveConfirmTitle();
std::wstring RemoveConfirmBody();
std::wstring RemoveConfirmPrimary();
std::wstring RemoveConfirmClose();

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

// ---- labels this person gives (Live/LocalNames.h) ----------------------------------------
// A label is the viewer's own name for a member or a conversation, kept on this computer and
// never sent. Every word below says WHOSE name it is, because the protocol's own display name
// stays the placeholder beside it and the two must never read as one claim.
//
// The button that opens the dialog, by what it names and whether a label exists yet.
std::wstring LocalNameButtonLabel(bool conversation, bool named);
// Its accessible name: the button's label alone does not say the name stays here.
std::wstring LocalNameButtonName(bool conversation, bool named);
std::wstring LocalNameDialogTitle(bool conversation);
// The sentence under the box: only you see it, it stays on this computer, it is never sent.
std::wstring LocalNameDialogNote();
// The member detail's key for the label, and its value while there is none. "not set" and not
// the placeholder: that a label is absent is a fact this app knows, not one it cannot learn.
std::wstring LocalNameFieldKey();
std::wstring LocalNameUnset();
// A label shown where the protocol's sender name would be (the rail's Sender field), marked.
std::wstring LocalNameMarked(std::wstring const& label);

}  // namespace urmsg::views
