// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader>, the same as Views/StatusStripRules.cpp,
// because Startup.cpp's CollectDiagnostics() calls into it before winrt::init_apartment().
//
// Read RosterRules.h first: it carries the table these functions transcribe.
#include "Views/RosterRules.h"

#include <format>

#include "Views/InspectRailFields.h"  // OnlineDeviceCount

namespace urmsg::views {

std::wstring RoleWord(std::wstring_view role) {
  if (role == demo::kRoleOwner) return L"Owner";
  if (role == demo::kRoleAdmin) return L"Admin";
  if (role == demo::kRoleMember) return L"Member";
  if (role == demo::kRoleObserver) return L"Observer";
  return std::wstring(demo::kUnavailable);
}

std::wstring MemberRowTitle(demo::MemberRef const& member) {
  // "You" over whatever the source called this leaf: the fixture never sets `mine`, and the live
  // world's own leaf carries the placeholder for a name exactly as every other leaf does, so
  // without this the viewer's own row would read "unavailable (middle dot) Owner".
  const std::wstring who = member.mine ? std::wstring(L"You") : member.displayName;
  return who + L" \u00B7 " + RoleWord(member.role);  // U+00B7 MIDDLE DOT
}

std::wstring_view RoleVerbTarget(RoleVerb verb) {
  switch (verb) {
    case RoleVerb::MakeAdmin: return demo::kRoleAdmin;
    case RoleVerb::MakeMember: return demo::kRoleMember;
    case RoleVerb::MakeObserver: return demo::kRoleObserver;
    case RoleVerb::TransferOwnership: return demo::kRoleOwner;
  }
  return demo::kRoleMember;
}

std::vector<RoleVerb> RoleControlsFor(std::wstring_view myRole, std::wstring_view theirRole,
                                      bool theirRowIsMine) {
  std::vector<RoleVerb> out;
  // Nothing on one's own row: SetRole naming oneself is either a no-op or an admin-set change
  // the owner alone may make, and a transfer to oneself is INVALID by name. And nothing on the
  // owner's row for anybody: the owner's role changes only through the owner's own transfer.
  if (theirRowIsMine || theirRole == demo::kRoleOwner) return out;
  const bool owner = myRole == demo::kRoleOwner;
  const bool admin = myRole == demo::kRoleAdmin;
  if (!owner && !admin) return out;
  // Only the owner touches the admin set (MASTER section 11: "Sole authority for ... admin-set
  // changes"): an admin gets no control at all on another admin, and never offers MakeAdmin.
  if (admin && theirRole == demo::kRoleAdmin) return out;
  if (owner && theirRole != demo::kRoleAdmin) out.push_back(RoleVerb::MakeAdmin);
  if (theirRole != demo::kRoleMember) out.push_back(RoleVerb::MakeMember);
  if (theirRole != demo::kRoleObserver) out.push_back(RoleVerb::MakeObserver);
  if (owner) out.push_back(RoleVerb::TransferOwnership);
  return out;
}

std::wstring RoleControlLabel(RoleVerb verb) {
  switch (verb) {
    case RoleVerb::MakeAdmin: return L"Make admin";
    case RoleVerb::MakeMember: return L"Make member";
    case RoleVerb::MakeObserver: return L"Make observer";
    case RoleVerb::TransferOwnership: return L"Transfer ownership";
  }
  return {};
}

std::wstring RoleControlName(RoleVerb verb, RoleControlState state) {
  // THE NoSession ARMS follow the [ Try again ] pair's wording ("there is no live session to ...")
  // so a screen reader hears the same fact from every dark control on the surface. They name the
  // SESSION and never the build: this build can change roles, and does, one session away.
  //
  // THE Pending ARMS exist because the same button goes dark for a second reason, and the two
  // were one string until 2026-09-22. While this member's previous change is inside the library
  // the control is dark so a second press cannot ask for the same change twice -- and the old
  // wording then announced "there is no live session to change roles in" AT THE ONE MOMENT the
  // session is provably live, because the app is inside urnet_message_group_set_role while the
  // words are read out. Someone who cannot see the row was told the opposite of what was
  // happening. The Pending arm names the wait and says the note carries the answer.
  //
  // THE Live ARMS name the VERB and nothing the role would then enforce. "Make this member an
  // observer, who can read but not send" was the wording once, and it claimed a gate this build
  // does not have: nothing in the sdk's send path or the app's composer reads the role (the
  // ledger's item 242 orders "R4 OBSERVER read-only" after R3, and Spec C section 5.6's own
  // observer sentence is "Observers are asked not to send ... this version of URmessage cannot
  // stop it at the server"). A screen-reader user hearing "cannot send" would have been told the
  // group enforces something it does not. The clause returns with R4, when the composer and the
  // sdk gate on the role; --diagnose's `roster names` forbids the enforcement words until then.
  const wchar_t* live = L"Change this member's role";
  const wchar_t* waiting = L"Change this member's role: waiting for the group to answer the last "
                           L"change to this member";
  const wchar_t* dark = L"There is no live session to change roles in";
  switch (verb) {
    case RoleVerb::MakeAdmin:
      live = L"Make this member an admin";
      waiting = L"Make admin: waiting for the group to answer the last change to this member";
      dark = L"Make admin: there is no live session to change roles in";
      break;
    case RoleVerb::MakeMember:
      live = L"Make this member a member";
      waiting = L"Make member: waiting for the group to answer the last change to this member";
      dark = L"Make member: there is no live session to change roles in";
      break;
    case RoleVerb::MakeObserver:
      live = L"Make this member an observer";
      waiting = L"Make observer: waiting for the group to answer the last change to this member";
      dark = L"Make observer: there is no live session to change roles in";
      break;
    case RoleVerb::TransferOwnership:
      live = L"Transfer ownership of this group to this member; you become an admin";
      waiting =
          L"Transfer ownership: waiting for the group to answer the last change to this member";
      dark = L"Transfer ownership: there is no live session to change roles in";
      break;
  }
  switch (state) {
    case RoleControlState::Live: return live;
    case RoleControlState::Pending: return waiting;
    case RoleControlState::NoSession: return dark;
  }
  return dark;
}

std::wstring TransferConfirmTitle() { return L"Transfer ownership?"; }

std::wstring TransferConfirmBody() {
  // Says the two things the person is committing to and cannot undo alone: who owns the group
  // afterwards, and what they themselves become. No name is available for the member, so the
  // sentence names the row rather than pretending to name a person.
  return L"This member becomes the group's owner and you become an admin. Only the new owner "
         L"can hand ownership back.";
}

std::wstring TransferConfirmPrimary() { return L"Transfer ownership"; }
std::wstring TransferConfirmClose() { return L"Keep ownership"; }

std::wstring RoleActionNote(demo::MemberRef const& member) {
  const bool transfer = member.roleActionVerb == demo::kRoleOwner;
  // The library's reason, verbatim, or the true sentence when it gave none. A rephrasing would
  // be this app's guess at what went wrong; the point of showing a failure is the actual one.
  const std::wstring reason = member.roleActionReason.empty()
                                  ? std::wstring(L"the library gave no reason")
                                  : member.roleActionReason;
  switch (member.roleAction) {
    case demo::RoleActionState::None:
      return {};
    case demo::RoleActionState::Pending:
      // U+2026 HORIZONTAL ELLIPSIS, as an escape (the house non-ASCII rule).
      return transfer ? std::wstring(L"Transferring ownership\u2026")
                      : std::format(L"Changing role to {}\u2026", RoleWord(member.roleActionVerb));
    case demo::RoleActionState::Refused:
      return std::format(L"Not changed: your role does not permit this. {}", reason);
    case demo::RoleActionState::Lost:
      // The one outcome that is not a failure: the change may still be right. The reader is
      // told what to do next in the words the ABI's own contract uses for it.
      return L"Not changed: someone else changed the group first \u2014 try again.";
    case demo::RoleActionState::Invalid:
      return std::format(L"Not changed: this app made a request the library refused by name. {}",
                         reason);
    case demo::RoleActionState::Failed:
      return std::format(L"Not changed: the change did not reach the server. {}", reason);
  }
  return {};
}

std::wstring MembersCaptionMeta(demo::Conversation const& conv) {
  if (conv.members.empty()) return std::wstring(demo::kUnavailable);
  size_t online = 0;
  size_t total = 0;
  for (auto const& member : conv.members) {
    online += OnlineDeviceCount(member);
    total += member.devices.size();
  }
  // "0/0 online" is a claim about people; "presence unavailable" is the fact. U+00B7 MIDDLE DOT.
  if (total == 0) return std::format(L"{} members \u00B7 presence unavailable", conv.members.size());
  return std::format(L"{} members \u00B7 {}/{} online", conv.members.size(), online, total);
}

std::wstring MemberRowMeta(demo::MemberRef const& member) {
  if (member.devices.empty()) return std::wstring(demo::kUnavailable);
  return std::format(L"{}/{} online", OnlineDeviceCount(member), member.devices.size());
}

}  // namespace urmsg::views
