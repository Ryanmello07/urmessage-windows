// SPDX-License-Identifier: MPL-2.0
//
// AN INVITATION REFUSED OVER A GROUP THIS DEVICE ALREADY HOLDS, AND THE LEAVE THAT ANSWERS IT
// (hardening plan Task 39; the lead's ruling R13).
//
// The library refuses an invitation into a group this device holds with ErrGroupAlreadyHeld, and
// that refusal names its remedy: leave the group first. A group that did not restore, or whose
// record on the disk does not read, is held all the same -- its record is on the disk -- and it is
// not live in this app, so the leave the conversation offers cannot reach it. This file is the
// app's half of that refusal: which refusal it is, the two sentences a person is shown, and where
// the group id the leave needs comes from.
//
// PURE, ON PURPOSE. No SDK call, no XAML, no winrt: --diagnose runs before the apartment and asks
// every function here (Startup.cpp, HeldGroupDiagnostics), and the live worker asks the same ones.
//
// THE BRIDGE CARRIES SENTENCES AND NOT TYPES, so the refusals are told apart by the library's own
// words. Each one this file matches is quoted below from urnetwork/message, sdk/urmessage, where
// the sentinel is declared (errors.go) and where the clause is formatted (heldgroup.go). EACH IS
// MATCHED BY ITS STABLE PART (R27, as Live/RefusalCopy.h says): the clause that names the condition,
// and never the cost or the remedy after it. Re-read at the T1 branch's head e0d16eac75e4.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace urmsg::live {

// ErrGroupAlreadyHeld's stable part. Every refusal of an invitation over a held group begins with
// its sentence, whatever follows the colon.
//   whole: "urmessage: this device already holds this group, so an invite into it is refused; leave
//          it first with ForgetGroup to join it again"
inline constexpr std::string_view kHeldRefusalSentence = "urmessage: this device already holds this group";

// The clause the gate adds when the group is held ON THE DISK and not in memory: it did not
// restore ("group <hex> has a record on this device's disk"), or its record does not read (the
// same words, then "that does not read: " and ErrGroupRecordDamaged's sentence).
inline constexpr std::string_view kHeldOnDiskClause = "has a record on this device's disk";

// ErrGroupNotRestored's stable part: what the leave of a group on the disk answers when no restore
// has noted that group as one that will not come back. The remedy it names is the restore, and
// then the leave again; the app restores before every leave, so this answer erases nothing and
// offers the leave again.
//   whole: "urmessage: this group is on this device's disk and has not been restored; call Restore,
//          and if it does not come back, ForgetGroup leaves it"
inline constexpr std::string_view kNotRestoredSentence =
    "urmessage: this group is on this device's disk and has not been restored";

// ErrForgetUnfinished's sentence, up to its remedy: a leave whose erase did not finish. The join
// of the same group after it is refused with it ("group <hex> cannot be joined or made until the
// erase of the leave before it has finished", forget.go finishLeaveBefore) until that erase ends.
// Its stable part is both of its first clauses, because together they are the condition (left, and
// not erased in full); the remedy after the semicolon is not matched.
//   whole: "urmessage: this device has left the group, but its copy on the disk could not be erased
//          in full; ForgetGroup again, or the next Restore, finishes the erase"
inline constexpr std::string_view kForgetUnfinishedSentence =
    "urmessage: this device has left the group, but its copy on the disk could not be erased in full";

// THE TWO SENTENCES A PERSON IS SHOWN, verbatim from Task 39's table.
inline constexpr std::string_view kHeldLiveSentence = "You are already in this group on this device.";
inline constexpr std::string_view kHeldNotLiveSentence =
    "This device still holds an old copy of this group that it cannot open. Leave it, and then use "
    "this invitation again.";

// The leave the second sentence names, as the button says it and as a screen reader names it.
inline constexpr std::wstring_view kLeaveHeldButton = L"Leave the old copy";
inline constexpr std::wstring_view kLeaveHeldButtonName = L"Leave the old copy of this group";

enum class JoinRefusal {
  Other,        // anything but a held group: the app says what it said before this file
  HeldLive,     // held, and live on this device
  HeldNotLive,  // held on the disk only: it did not restore, or its record does not read
};

// Which refusal a device_join error is. Empty text is Other.
JoinRefusal ClassifyJoinRefusal(std::string_view libraryError);

// Is this the refusal of a leave whose group no restore has tried (ErrGroupNotRestored)?
bool IsNotRestoredRefusal(std::string_view libraryError);

// Is this a join refused because the leave of THIS group (by its hex) has not finished its erase
// (ErrForgetUnfinished, naming the group)? Then the leave is offered again and not the sentence for
// an invitation meant for another device.
bool IsUnfinishedEraseOf(std::string_view libraryError, std::string_view groupIdHex);

// THE GROUP ID AN ENCODED INVITATION NAMES. It is the invitation's FIRST field: two version
// octets, a four-octet big-endian length that must say 32, and the 32 octets of the id. The same
// at invite version 2 and version 3. The bridge has no accessor for it (urnet_message_group_id
// reads a group, and a group that is not live has no handle), and adding one is an exported symbol
// and a change to the C header, which Task 39 does not make. Answers false, and leaves `out`
// alone, for anything shorter than that or whose length is not 32.
inline constexpr size_t kGroupIdOctets = 32;
bool InviteGroupId(const uint8_t* invite, size_t len, std::array<uint8_t, kGroupIdOctets>& out);

// Lower-case hex, the spelling the library's %x gives a group id in its sentences.
std::string GroupIdHex(std::array<uint8_t, kGroupIdOctets> const& groupId);

// Does the refusal name this group? The gate's sentence carries the id it refused ("group <hex>
// ..."), so the leave is offered only when the id read off the invitation is the id the library
// refused: a sentence about another group offers nothing.
bool RefusalNamesGroup(std::string_view libraryError, std::string_view groupIdHex);

}  // namespace urmsg::live
