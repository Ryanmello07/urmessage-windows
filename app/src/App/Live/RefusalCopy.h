// SPDX-License-Identifier: MPL-2.0
//
// WHAT THE APP SAYS FOR EACH REFUSAL OF A JOIN OR AN ADD, AND FOR A DEVICE THAT CANNOT MAKE A JOIN
// CODE (hardening plan Task 58; the lead's rulings R13, R17, R20, R25, R27 to R30, R32 and R33).
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
// words, as Live/HeldGroupRules.h tells the held-group refusals apart.
//
// EACH IS MATCHED BY ITS STABLE PART, NEVER BY ITS WHOLE SENTENCE (R27). A sentinel's sentence is
// the clause that names what happened, then, after a comma or a semicolon, what it cost or what to
// do about it, and the library writes the cause after a colon. The app matches the first of these
// and nothing after it. R26's round reworded ErrGroupRecordNotReadNow, and an app that matched that
// sentence in full would have answered a known refusal with its fallback, silently. Each constant
// below is that stable part, and the comment above it quotes the whole sentence as
// urnetwork/message's sdk/urmessage/errors.go declares it (or, for a clause, as the file named
// beside it formats it) at the T1 branch's head after its third fix round (907109f1385e). Each
// stable part (a clause's with its group id between its two halves) is in exactly one sentence the
// library makes there, measured with git grep outside the tests. The --diagnose cases are typed from those files at that head, whole, and again with
// each tail reworded; they do not read the library, so only a stable part reworded there makes the
// app answer its fallback, and a reworded tail does not.
//
// TEXT THIS FILE DOES NOT KNOW IS NOT GUESSED AT. It answers Other, and the caller says what it
// said before this file.
#pragma once

#include <string>
#include <string_view>

namespace urmsg::live {

// ── the library's sentences, by their stable parts ─────────────────────────────────────────────

// ErrJoinRefused: the ONE sentence every refusal inside a join's window begins with (Task 36),
// because nothing was written and the join code went back. A refusal is this row only when its
// text BEGINS with it: the window's deferred call puts it first. At e0d16ea a join code the store
// holds and could not read FOR A MOMENT (another process held it) is answered with it too (R26 V3).
//   whole: "urmessage: this invite was refused before anything was written, and the join code is
//          not spent"
inline constexpr std::string_view kLibJoinRefused =
    "urmessage: this invite was refused before anything was written";
// ErrNoJoinCodeForInvite: the one refusal inside the window that does NOT carry the sentence above
// (R13, R17): no join code this device can read was the invitation made for, so "not spent" is not
// true of anything. An invitation made for another device answers it, so does the same invitation
// once its join code has gone, and at e0d16ea so does a join code that will never read (its
// octets damaged, or a read that failed for good; R26 V3).
//   whole: "urmessage: this device holds no join code this invitation was made for; make a new
//          join code"
inline constexpr std::string_view kLibNoJoinCodeForInvite =
    "urmessage: this device holds no join code this invitation was made for";
// ErrJoinCodeSpent: the join cost its join code. The put-back failed, the joined state was written
// and the group's record then could not be, or (R26 V3a) the take removed the join code and then
// failed.
//   whole: "urmessage: this join did not complete and its join code is spent; make a new join code
//          and ask to be added again"
inline constexpr std::string_view kLibJoinCodeSpent =
    "urmessage: this join did not complete and its join code is spent";
// ErrInviteVersion: parse_invite's refusal of an invitation at another version (an older adder's).
//   whole: the same; it has no tail, and the version after its colon is the cause.
inline constexpr std::string_view kLibInviteVersion =
    "urmessage: this invite is at a version this build does not read";
// ErrGroupPredatesJoinSeal: a group founded before invitations were sealed, refused at Restore and
// at Join (at Join it is inside the window, so it follows ErrJoinRefused's sentence there).
//   whole: "urmessage: this group was founded before invitations were sealed, so this build does not
//          open it; leave it and start the group again"
inline constexpr std::string_view kLibGroupPredatesJoinSeal =
    "urmessage: this group was founded before invitations were sealed";
// The adder's three refusals of a join code, each before a commit is staged (Task 45).
//   whole: the same; it has no tail.
inline constexpr std::string_view kLibKeyPackageInvalid =
    "urmessage: this join code is not a valid key package";
//   whole: "urmessage: this join code was made by a build that cannot open a sealed invite; ask for
//          a join code from an updated URmessage"
inline constexpr std::string_view kLibJoinerCannotOpenSealedInvite =
    "urmessage: this join code was made by a build that cannot open a sealed invite";
//   whole: "urmessage: this identity already has an entry in this group, perhaps from an invitation
//          that was never opened; remove that entry, then add the new join code"
inline constexpr std::string_view kLibJoinerAlreadyHasALeaf =
    "urmessage: this identity already has an entry in this group";
// ErrNoDeviceWrapKey: KeyPackage() of a device whose store was written before the X-Wing seed was
// kept (Task 42). Such a device publishes no join code.
//   whole: "urmessage: this device holds no x-wing seed for the leaf it publishes, so it cannot open
//          an encapsulation addressed to that leaf"
inline constexpr std::string_view kLibNoDeviceWrapKey =
    "urmessage: this device holds no x-wing seed for the leaf it publishes";
// A FILE OF A GROUP THAT COULD NOT BE READ JUST NOW (Task 57 F1; R26): another process held it,
// with a sharing or lock violation (the TRANSIENT class of transientread.go's classifyRead). Nothing
// of the group was changed. ErrRestore's sentence carries it, for the record (the list's report,
// statestore_durable.go's GroupRecords) and for an epoch state (restore.go, "group <id>: ..." after
// it) alike; so do the held-group gate's and the leave's refusals, after their own clause (R29).
//   whole: "urmessage: a file of a group on this device's disk could not be read just now, so that
//          group was not restored this time; nothing of it was changed, and the next Restore tries
//          it again"
// ITS SUBJECT IS NOT IN ITS STABLE PART. R26's round reworded the subject from "a group record" to
// "a file of a group", which is the rewording R27 was ruled over. The predicate names the
// condition, and it is in no other sentence the library makes.
inline constexpr std::string_view kLibGroupRecordNotReadNow =
    "on this device's disk could not be read just now";
// A FILE OF A GROUP THAT COULD NOT BE READ, FOR A REASON THIS BUILD CANNOT CLASSIFY (R29, R30): the
// UNPROVEN class. A permission refused (a deny ACL), the medium, no errno, an errno in neither list,
// a failure that is not the store's. Nothing of the group was changed, and the library leaves it
// only once a Restore reads it or finds it damaged. It is carried on the same roads as the one
// above.
//   whole: "urmessage: a file of a group on this device's disk could not be read, and this build
//          cannot tell whether that will pass, so that group was not restored; nothing of it was
//          changed, and it is not left until a Restore reads it or finds it damaged"
// Its subject is left out of its stable part, as the sentinel above's is.
inline constexpr std::string_view kLibGroupReadUnproven =
    "on this device's disk could not be read, and this build cannot tell whether that will pass";
// ErrNotConnected (R32): no Hello has been said on this device's transport, so nothing that needs
// the connection's nonce can run; a Restore answers it bare, before it tries any group.
//   whole: "urmessage: this device has not said Hello, and every authenticator is a mac over the
//          connection's server_nonce"
inline constexpr std::string_view kLibNotConnected = "urmessage: this device has not said Hello";
// The leave's own re-try of the group's restore, refused because of it (forget.go, restoreForLeave):
//   "urmessage: group <hex>'s restore could not be tried again for the leave, so nothing was left:
//   <ErrNotConnected's sentence>"
inline constexpr std::string_view kLibLeaveNotTriedHead = "urmessage: group ";
inline constexpr std::string_view kLibLeaveNotTriedAfterId = "'s restore could not be tried again for the leave";
// ErrOwnerMustTransfer (R33 X2): the leave reached the owner's question, because the library's own
// fresh re-try of the group's restore SUCCEEDED and this device owns a group others are in. At the
// R33 head the library does not keep the group held after that refusal, and the next Restore hands
// it back.
//   whole: "urmessage: you own this group and somebody else is in it, so hand it over first with
//          TransferOwnership; leaving now would leave them with an owner nobody can remove"
inline constexpr std::string_view kLibOwnerMustTransfer = "urmessage: you own this group and somebody else is in it";
// The held-group gate's and ForgetGroup's reads of ONE group's record, when the store could not
// answer. At 907109f each says which class it was, by the sentinel after its clause (R29):
//   join   "urmessage: whether this device already holds group <hex> could not be read, so the
//          invite into it is refused: <the class's sentence>: <cause>" (heldgroup.go, heldUnread)
//   leave  "urmessage: whether this device holds group <hex> on its disk could not be read, so
//          nothing was left: <the class's sentence>: <cause>" (forget.go, recordOnDisk), which the
//          bridge answers URNET_MESSAGE_FORGET_REFUSED
// The app answers each by the class it carries; a clause that carries none (e0d16ea's) is the
// transient row, as it was.
// Each is matched by its words before the group id, the id (64 lower-case hex digits), and its
// words after the id up to its comma; ", so ..." is what it cost.
inline constexpr std::string_view kLibJoinNotReadHead = "urmessage: whether this device already holds group ";
inline constexpr std::string_view kLibJoinNotReadAfterId = " could not be read";
inline constexpr std::string_view kLibLeaveNotReadHead = "urmessage: whether this device holds group ";
inline constexpr std::string_view kLibLeaveNotReadAfterId = " on its disk could not be read";

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
// R29: a file of the group could not be read for a reason this build cannot classify (R30: a deny
// ACL is one). On every road the library reports it. NO LEAVE IS OFFERED FOR IT, for Q5's reason.
inline constexpr std::string_view kSayGroupReadUnproven =
    "This conversation could not be read on this device, and nothing was changed. Check that "
    "URmessage can read its folder, then open URmessage again.";
// R32: the leave's restore could not be tried, because this device is not connected. THE LEAVE STAYS
// OFFERED: pressing it again once connected is safe, because the restore runs first.
inline constexpr std::string_view kSayNotConnected =
    "This device is not connected right now, and nothing was changed. Try again when it is "
    "connected.";
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
  GroupReadUnproven,      // the same roads, for the unproven class (ErrGroupReadUnproven, R29)
};

// Which row a refusal of parse_invite, device_join or device_restore is. Empty text is Other.
//
// THE ORDER IS THE RULE. ErrNoJoinCodeForInvite first, so that no spelling of it can ever reach the
// row that says the join code is still good (R13). Then what the join COST (spent), then the four
// rows more specific than the window's sentence (the unproven class before the transient one, the
// longer-lasting of the two), and the window's sentence last, by its prefix.
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

// Does a restore's report say that a file of a group could not be read just now
// (ErrGroupRecordNotReadNow's stable part, anywhere in it)?
bool IsGroupRecordNotReadNow(std::string_view restoreError);
// ... or that one could not be read for a reason this build cannot classify (ErrGroupReadUnproven's)?
bool IsGroupReadUnproven(std::string_view restoreError);
// Is this a refusal because this device is not connected (R32): ErrNotConnected's stable part, or
// the leave's "could not be tried again for the leave" clause around a group id?
bool IsNotConnected(std::string_view libraryError);

// THE RESTORE-ROAD FLAG (R28, R29): which "not read" class the last restore reported, if any. A
// report that carries both names the unproven one, the longer-lasting.
enum class ReadClass {
  None,
  NotReadNow,
  Unproven,
};
ReadClass RestoreReadClass(std::string_view restoreError);
// The flag after a restore. A restore that could not be tried because this device is not connected
// read nothing, so it leaves the flag as the restore before it left it.
ReadClass NextRestoreClass(ReadClass previous, std::string_view restoreError);

// ── the held-group leave after a restore that decided nothing (R28, R29, R32) ──────────────────

// WHAT THE LEAVE DOES AFTER ITS RESTORE-FIRST (R21 F1). The group came back, and the device is in
// it. Or the restore could not read a file of a group just now (ErrGroupRecordNotReadNow), could not
// read one for a reason this build cannot classify (ErrGroupReadUnproven), or could not be tried
// because this device is not connected (ErrNotConnected), and the group did not come back: the leave
// stops there, says that row, and does NOT call forget_group, because nothing is known to be wrong
// with the group and a leave erases it. Or it did not come back for any other reason, and
// forget_group is asked.
//
// A RECORD-ONLY FAILURE NAMES NO GROUP ID (the store cannot name one, R12), so a report that
// carries a sentinel stops the leave of ANY group: R28's conservative reading.
enum class HeldLeaveStep {
  CameBack,
  NotReadNow,
  Unproven,
  NotConnected,
  Forget,
};
HeldLeaveStep StepAfterRestoreFirst(bool cameBack, std::string_view restoreError);

// THE LIBRARY'S OWN REFUSAL OF THE LEAVE (forget_group, kind REFUSED), by the class inside it (R29,
// R32): its fresh re-try's ("<sentinel>; so nothing was left: group <hex>: ..."), its reads of the
// record ("whether this device holds group <hex> on its disk could not be read, ...: <sentinel>"
// and "group <hex>'s record could not be read again for the leave, ...: <sentinel>"), and its
// "could not be tried again for the leave" (not connected). OwnerMustTransfer is the leave reaching
// the owner's question after that re-try brought the group back (R33 X2). Other is everything else:
// the caller's own handling (ErrGroupNotRestored, and Task 39's fallback for the rest, a write
// failure among them).
enum class LeaveRefusal {
  Other,
  NotReadNow,
  Unproven,
  NotConnected,
  OwnerMustTransfer,
};
LeaveRefusal ClassifyLeaveRefusal(std::string_view libraryError);
// Does the app restore once more after this refusal, so the conversation shows (R33 X2)? The
// owner's question only: the group's files read, and the next Restore hands the group back.
bool RestoresOnceMore(LeaveRefusal refusal);

// A JOIN REFUSED OVER A GROUP THIS DEVICE HOLDS AND THE LAST RESTORE COULD NOT READ (R33 X3): the
// join gate answers with the class's own sentinel while the group is in the library's not-read set,
// whatever other sentence it carries. The class's row (GroupReadUnproven or GroupRecordNotReadNow),
// or Other when the text carries neither sentinel. The unproven class wins when both are there.
JoinCopy NotReadRowOf(std::string_view libraryError);

// WHAT A PERSON IS TOLD OF A GROUP ON THE DISK THAT IS NOT LIVE (Task 39's held refusal, and the
// leave above when it stops), and whether "Leave the old copy" is offered.
struct HeldCopy {
  std::string_view sentence;
  bool offerLeave = false;
};
// The join road's held refusal, by the restore-road flag: R25 Q5's row or R29's unproven row with
// no leave while the last restore could not read a file of a group (R28, R29); otherwise Task 39's
// sentence and the leave.
HeldCopy HeldNotLiveCopy(ReadClass lastRestore);
// The leave stopped before forget_group (NotReadNow, Unproven, NotConnected). Not connected keeps
// the leave offered (R32). Any other step is not a stop, and is answered Task 39's sentence.
HeldCopy LeaveStopCopy(HeldLeaveStep step);
// The library's own refusal of the leave, by its class. Other and OwnerMustTransfer answer an empty
// sentence: the caller restores once more for the second, and otherwise says what it said before
// this file.
HeldCopy LeaveRefusalCopy(LeaveRefusal refusal);

// WHAT THE JOIN DIALOG SAYS FIRST AFTER A RESTORE, from device_restore's report and how many groups
// it brought back: a group that predates the seal, or one with a file that could not be read (just
// now, or for a reason this build cannot classify), when no group came back. Empty otherwise (the app is in a group, or the report is nothing a
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
