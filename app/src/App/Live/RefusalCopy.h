// SPDX-License-Identifier: MPL-2.0
//
// WHAT THE APP SAYS FOR EACH REFUSAL OF A JOIN OR AN ADD, AND FOR A DEVICE THAT CANNOT MAKE A JOIN
// CODE (hardening plan Task 58; the lead's rulings R13, R17 and R20).
//
// The join seal (the plan's phase T1) gives the library a refusal for each thing that can go wrong
// with an invitation or a join code, and each says what it cost. The app's half is the sentence a
// person reads: in their words, never the library's, and true about the one thing they can do
// next. THIS FILE IS THE ONE PLACE THOSE SENTENCES ARE WRITTEN, beside the library sentence each
// one answers, and the rule that picks one.
//
// PURE, ON PURPOSE. No SDK call, no XAML, no winrt: --diagnose runs before the apartment and asks
// every function here (Startup.cpp, RefusalCopyDiagnostics), and the live worker and the window ask
// the same ones.
//
// THE BRIDGE CARRIES SENTENCES AND NOT TYPES, so the refusals are told apart by the library's own
// words, as Live/HeldGroupRules.h tells the held-group refusals apart. Each sentence below is quoted
// from urnetwork/message sdk/urmessage/errors.go, where its sentinel is declared (or, for a clause,
// from the file named beside it), at the T1 branch's head after the red team's fixes
// (09018b10dd23; every sentence quoted at 7c4119792c1d reads the same there). The --diagnose cases
// are typed from the same files and test the rules against those copies; they do not read the
// library, so a sentence reworded there leaves them green.
//
// TEXT THIS FILE DOES NOT KNOW IS NOT GUESSED AT. It answers Other, and the caller says what it
// said before this file.
#pragma once

#include <string>
#include <string_view>

namespace urmsg::live {

// ── the library's sentences ────────────────────────────────────────────────────────────────────

// ErrJoinRefused: the ONE sentence every refusal inside a join's window begins with (Task 36),
// because nothing was written and the join code went back. A refusal is this row only when its
// text BEGINS with it: the window's deferred call puts it first.
inline constexpr std::string_view kLibJoinRefused =
    "urmessage: this invite was refused before anything was written, and the join code is not spent";
// ErrNoJoinCodeForInvite: the one refusal inside the window that does NOT carry the sentence above
// (R13, R17): no join code this device can read was the invitation made for, so "not spent" is not
// true of anything. An invitation made for another device answers it, and so does the same
// invitation once its join code has gone.
inline constexpr std::string_view kLibNoJoinCodeForInvite =
    "urmessage: this device holds no join code this invitation was made for; make a new join code";
// ErrJoinCodeSpent: the join cost its join code (the put-back failed, or the joined state was
// written and the group's record then could not be).
inline constexpr std::string_view kLibJoinCodeSpent =
    "urmessage: this join did not complete and its join code is spent; make a new join code and ask "
    "to be added again";
// ErrInviteVersion: parse_invite's refusal of an invitation at another version (an older adder's).
inline constexpr std::string_view kLibInviteVersion =
    "urmessage: this invite is at a version this build does not read";
// ErrGroupPredatesJoinSeal: a group founded before invitations were sealed, refused at Restore and
// at Join (at Join it is inside the window, so it follows kLibJoinRefused there).
inline constexpr std::string_view kLibGroupPredatesJoinSeal =
    "urmessage: this group was founded before invitations were sealed, so this build does not open "
    "it; leave it and start the group again";
// The adder's three refusals of a join code, each before a commit is staged (Task 45).
inline constexpr std::string_view kLibKeyPackageInvalid =
    "urmessage: this join code is not a valid key package";
inline constexpr std::string_view kLibJoinerCannotOpenSealedInvite =
    "urmessage: this join code was made by a build that cannot open a sealed invite; ask for a join "
    "code from an updated URmessage";
inline constexpr std::string_view kLibJoinerAlreadyHasALeaf =
    "urmessage: this identity already has an entry in this group, perhaps from an invitation that "
    "was never opened; remove that entry, then add the new join code";
// ErrNoDeviceWrapKey: KeyPackage() of a device whose store was written before the X-Wing seed was
// kept (Task 42). Such a device publishes no join code.
inline constexpr std::string_view kLibNoDeviceWrapKey =
    "urmessage: this device holds no x-wing seed for the leaf it publishes, so it cannot open an "
    "encapsulation addressed to that leaf";
// A GROUP RECORD THAT COULD NOT BE READ JUST NOW (Task 57, F1; the T1 fix, at 09018b10dd23): a file
// another process holds, or an I/O error. It is NOT damage, nothing of the group was changed, and
// the library refuses whatever damage would permit. Three roads carry it:
//   restore  ErrGroupRecordNotReadNow, inside ErrRestore's sentence (restore.go; the list's report
//            is statestore_durable.go's GroupRecords);
//   join     the held-group gate's unanswered read, "whether this device already holds group <hex>
//            could not be read, so the invite into it is refused: ..." (heldgroup.go, heldUnread);
//   leave    ForgetGroup's one-id read, "whether this device holds group <hex> on its disk could
//            not be read, so nothing was left: ..." (forget.go, recordOnDisk), which the bridge
//            answers URNET_MESSAGE_FORGET_REFUSED.
// The two clauses carry the group's id between their halves, as 64 lower-case hex digits.
inline constexpr std::string_view kLibGroupRecordNotReadNow =
    "urmessage: a group record on this device's disk could not be read just now, so that group was "
    "not restored this time; nothing of it was changed, and the next Restore tries it again";
inline constexpr std::string_view kLibJoinNotReadHead = "urmessage: whether this device already holds group ";
inline constexpr std::string_view kLibJoinNotReadTail = " could not be read, so the invite into it is refused";
inline constexpr std::string_view kLibLeaveNotReadHead = "urmessage: whether this device holds group ";
inline constexpr std::string_view kLibLeaveNotReadTail = " on its disk could not be read, so nothing was left";

// ── what a person is shown: Task 58's table, verbatim ─────────────────────────────────────────

inline constexpr std::string_view kSayJoinRefused =
    "This invitation was refused. Your join code is still good: ask for the invitation to be sent "
    "again another way.";
inline constexpr std::string_view kSayNoJoinCodeForInvite =
    "This invitation cannot be used with any join code this device can read. Make a new join code "
    "and ask to be invited again.";
inline constexpr std::string_view kSayJoinCodeSpent =
    "This join did not complete. Make a new join code and ask to be added again.";
inline constexpr std::string_view kSayInviteVersion =
    "This invitation was made by an older URmessage. Ask them to update and invite you again.";
inline constexpr std::string_view kSayGroupPredatesJoinSeal =
    "This group was started before invitations were sealed. Leave it and start it again.";
inline constexpr std::string_view kSayJoinerCannotOpenSealedInvite =
    "This join code was made by an older URmessage. Ask them for a new one from an updated app.";
inline constexpr std::string_view kSayKeyPackageInvalid =
    "This is not a valid join code. Ask them to send it again.";
// AMENDED BY R24's F5, in R25 Q1's scoped form. The app has no "has not joined yet" mark (Step 2 is
// not in T1: the bridge cannot tell it), so the sentence names what the join code claims and the
// one thing that makes removing the entry safe, which the person can ask.
inline constexpr std::string_view kSayJoinerAlreadyHasALeaf =
    "This join code claims to be someone already in this group. Remove their entry only if they "
    "tell you they never opened their invitation, then add this join code.";
// R25 Q5: a group record that could not be read just now, on the restore, join and leave roads
// alike. NO LEAVE IS OFFERED FOR IT: nothing is known to be wrong with the group, and a leave
// would erase it.
inline constexpr std::string_view kSayGroupRecordNotReadNow =
    "This conversation could not be read just now. Close anything else that may be using "
    "URmessage's folder, then try again.";
// The table's last row asks for "an on-screen refusal: this installation cannot make a join code".
// Its words are this file's: why it cannot, in the person's terms. A store with no seed is one an
// older build wrote, because this build writes the seed with every identity it mints.
inline constexpr std::string_view kSayNoDeviceWrapKey =
    "This installation cannot make a join code, so it cannot join a group. Its saved data was made "
    "by an older URmessage.";

// ── the join dialog's own copy ─────────────────────────────────────────────────────────────────

// The intro on the join road, as it was before this file.
inline constexpr std::wstring_view kJoinIntro =
    L"This device is connected but is not in a group yet. A group is opened by somebody who is "
    L"already in one: send them the join code below, and paste the invitation they send back.";
// THE INTRO WHILE THIS DEVICE HOLDS A GROUP: while the leave of a held group is on offer (R20), and
// on the founder's road. "Not in a group yet" is false there; the sentence under it says the rest.
inline constexpr std::wstring_view kHeldIntro = L"This device is connected.";
// The joiner's paste label. IT KEEPS ITS "SECRET" FRAMING (Task 58, Step 3).
inline constexpr std::wstring_view kJoinerPasteLabel =
    L"The invitation they send back. THIS ONE IS A SECRET — whoever has it is in the group, and "
    L"it can only be used once.";
// THE ADDER'S COPY FOR AN INVITATION, verbatim from Task 58, Step 3. Since invite version 3 the
// join secrets are sealed to the joiner's own device key, so only their device opens it.
inline constexpr std::wstring_view kAdderInviteLabel =
    L"Only their device can open it. It works once. Send it privately.";

// THE WAITING STATE'S SENTENCE, in its two halves. The first is true only of a device that holds no
// group; a device whose group is on its disk and did not open (it predates the seal, or its record
// could not be read just now) is told why first, and is not told it is "not in a group yet" (R25
// Q4).
inline constexpr std::string_view kWaitingNotInAGroup = "This device is not in a group yet.";
inline constexpr std::string_view kWaitingSendJoinCode =
    "Send your join code to whoever is setting the group up, and paste the invitation they send back.";

// "MAKE A NEW JOIN CODE" (R25 Q2): the button on the two rows that ask for one (ErrNoJoinCodeForInvite
// and ErrJoinCodeSpent). A press mints a fresh key package through the existing export and puts it
// in the join-code box; there is no automatic re-mint.
inline constexpr std::wstring_view kNewJoinCodeButton = L"Make a new join code";
inline constexpr std::string_view kSayNewJoinCode =
    "This is a new join code. Send it to whoever is inviting you, and paste the invitation they send "
    "back.";

// ── the rules ──────────────────────────────────────────────────────────────────────────────────

enum class JoinCopy {
  Other,                  // text this file does not know: the caller's own fallback
  JoinRefused,            // begins with kLibJoinRefused, and is none of the rows below
  NoJoinCodeForInvite,    // NEVER JoinRefused, whatever else the text carries (R13)
  JoinCodeSpent,
  InviteVersion,
  GroupPredatesJoinSeal,  // at Join (after kLibJoinRefused) and at Restore alike
  GroupRecordNotReadNow,  // at Restore (ErrGroupRecordNotReadNow) and at Join (the gate's unread)
};

// Which row a refusal of parse_invite, device_join or device_restore is. Empty text is Other.
//
// THE ORDER IS THE RULE. ErrNoJoinCodeForInvite first, so that no spelling of it can ever reach the
// row that says the join code is still good (R13). Then what the join COST (spent), then the three
// rows more specific than the window's sentence, and the window's sentence last, by its prefix.
JoinCopy ClassifyJoinCopy(std::string_view libraryError);
// The sentence for a row; empty for Other.
std::string_view JoinCopySentence(JoinCopy row);
// The sentinel's name, for a log line.
std::string_view JoinCopyName(JoinCopy row);
// Does this row's sentence ask for a new join code, so the dialog offers "Make a new join code"
// (R25 Q2)? ErrNoJoinCodeForInvite and ErrJoinCodeSpent only.
bool OffersNewJoinCode(JoinCopy row);

// Is this ForgetGroup's refusal of a group whose record could not be read just now (the leave
// road of R25 Q5)? Then the app says kSayGroupRecordNotReadNow and offers no leave.
bool IsLeaveNotReadNow(std::string_view libraryError);

// WHAT THE JOIN DIALOG SAYS FIRST AFTER A RESTORE, from device_restore's report and how many groups
// it brought back: a group that predates the seal, or one whose record could not be read just now,
// when no group came back. Empty otherwise (the app is in a group, or the report is nothing a
// person is told about).
std::string_view RestoreNote(std::string_view restoreError, int restoredCount);

// The Waiting state's sentence: the note first, if any, and "not in a group yet" only for a device
// that holds no group on its disk (R25 Q4).
std::string WaitingText(std::string_view note, bool holdsGroup);

// The dialog's intro. "This device is connected." (kHeldIntro) while the device holds a group: the
// leave of a held group is on offer (R20), it is on the founder's road, or its group is on its disk
// and did not open (R25 Q4). The join road's intro otherwise.
std::wstring_view OnboardIntro(bool offerLeaveHeldGroup, bool founded, bool holdsGroup);

enum class AddCopy {
  Other,
  KeyPackageInvalid,
  JoinerCannotOpenSealedInvite,
  JoinerAlreadyHasALeaf,
};

// Which row a refusal of group_add_member or group_add_member_and_publish is. Empty text is Other.
AddCopy ClassifyAddCopy(std::string_view libraryError);
std::string_view AddCopySentence(AddCopy row);
std::string_view AddCopyName(AddCopy row);

// Is this device_key_package's refusal of a device that holds no X-Wing seed (ErrNoDeviceWrapKey)?
bool IsNoDeviceWrapKey(std::string_view libraryError);

}  // namespace urmsg::live
