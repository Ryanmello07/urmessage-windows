// SPDX-License-Identifier: MPL-2.0
//
// WHAT THE APP SAYS FOR EACH REFUSAL OF A JOIN OR AN ADD, AND FOR A DEVICE THAT CANNOT MAKE A JOIN
// CODE (hardening plan Task 58; the lead's rulings R13, R17, R20, R25, R27 to R30, R32 to R35, R37,
// R38 and R40).
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
// beside it formats it) at the T1 branch's head after its sixth fix round, 9df6fca8414f. R33, R34
// and R36 reworded none of the sentences quoted here; they added clauses, each quoted where it is
// matched, and a comment that quotes an older head says so. EVERY HIT OF A STABLE PART IN THE
// LIBRARY OUTSIDE ITS TESTS IS LISTED WITH WHAT IT MEANS FOR THE MAPPING HERE, and the list is
// asserted both ways (R34 A4), so a hit nobody listed fails the check. Most have one. A clause the
// library formats in more than one sentence has more: the join gate's and the leave's read clauses
// (each hit carries a class's sentinel, which this file asks first) and Task 39's on-disk clause
// (each hit is ErrGroupAlreadyHeld's). The --diagnose cases are typed from those files, each line at
// the head it names, whole, and again with each tail reworded; they do not read the library, so only
// a stable part reworded there makes the app answer its fallback, and a reworded tail does not.
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
// text BEGINS with it: the window's deferred call puts it first. A join code the store holds and
// could not read is answered with it too when nothing proves the read will never pass: at e0d16ea
// only one held FOR A MOMENT (another process held it, R26 V3), and at fdc1b0aa3c31 a permission
// refused, the medium and a failure the library cannot classify as well (R33 X1).
//   whole: "urmessage: this invite was refused before anything was written, and the join code is
//          not spent"
inline constexpr std::string_view kLibJoinRefused =
    "urmessage: this invite was refused before anything was written";
// ErrStateStoreState (R34 A1): the durable state store's own refusal of a read or a write. AFTER THE
// WINDOW'S SENTENCE it is a join the store would not serve: a join code it holds and could not read
// for a reason it cannot prove permanent (R33 X1), or any other read or write of its own inside the
// window. Nothing was written and the join code is not spent; the remedy is the folder, and a
// re-sent invitation would be refused the same way.
//   whole: the same; it has no tail, and the store's own words for the path follow its colon.
inline constexpr std::string_view kLibStateStoreState =
    "urmessage: the durable state store could not be read or written";
// ErrNoJoinCodeForInvite: the one refusal inside the window that does NOT carry the sentence above
// (R13, R17): no join code this device can read was the invitation made for, so "not spent" is not
// true of anything. An invitation made for another device answers it, so does the same invitation
// once its join code has gone, and so does a join code that will never read: at fdc1b0aa3c31 its
// octets damaged or its path of the wrong type (R33 X1); at e0d16ea and 907109f any read that was
// not held for a moment as well (R26 V3).
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
// A FILE OF A GROUP THAT COULD NOT BE READ, FOR A REASON THIS BUILD CANNOT CLASSIFY (R29, R30, R33):
// the UNPROVEN class. A file gone missing (R33: a quarantine can move it away and back), a permission
// refused (a deny ACL), the medium, no errno, an errno in neither list, a failure that is not the
// store's. Nothing of the group was changed, and the library leaves it only once a Restore reads it
// or finds it damaged. It is carried on the same roads as the one above.
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
// fresh re-try of the group's restore SUCCEEDED and this device owns a group others are in. At
// fdc1b0aa3c31 the library does not keep the group held after that refusal (forget.go closes a group
// it rebuilt for the leave on any refused leave), and the next Restore hands it back.
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
// transient row, as it was. At fdc1b0aa3c31 two more refusals carry a class's sentence, each told
// apart by that sentence alone:
//   join   "urmessage: group <hex> is on this device's disk and the last Restore could not read it,
//          so the invite into it is refused: <the class's sentence>" (heldgroup.go, R33 X3), with
//          no held sentence, so it is the class's row and never Task 39's held row
//   leave  "urmessage: whether this device holds group <hex> on its disk could not be read, so
//          nothing was left: <ErrGroupReadUnproven's sentence>: its directory stands and its record
//          is not in it" (forget.go, R33 1): the clause above, so the unproven class is asked first
// And at 93cc0afb85b1 the gate refuses a record missing from a folder where the store's own files
// stand in the first clause's own form (heldgroup.go, R34 item 1): "urmessage: whether this device
// already holds group <hex> could not be read, so the invite into it is refused: <ErrGroupReadUnproven's
// sentence>: its directory stands and its record is not in it", which is the unproven row (R35 A7).
// Each is matched by its words before the group id, the id (64 lower-case hex digits), and its
// words after the id up to its comma; ", so ..." is what it cost.
inline constexpr std::string_view kLibJoinNotReadHead = "urmessage: whether this device already holds group ";
inline constexpr std::string_view kLibJoinNotReadAfterId = " could not be read";
inline constexpr std::string_view kLibLeaveNotReadHead = "urmessage: whether this device holds group ";
inline constexpr std::string_view kLibLeaveNotReadAfterId = " on its disk could not be read";
// group_open's refusal of a group that IS open on the server and whose record IS on this device's
// disk, when the store could not record that the group's creation finished: its creation mark could
// not be removed and its retraction could not be written (R34 item 3; group.go, Open, at
// 93cc0afb85b1). Its stable part is the whole clause before its cause: its ", but ..." is the
// condition itself, not a cost.
//   whole: "urmessage: this group is open on the server and its record is on this device's disk, but
//          this store could not record that the group's creation finished: <ErrStateStoreState's
//          sentence>: group <hex>: its record is on the disk and this store could not record that its
//          creation finished: the creation mark could not be removed: <cause>; and its retraction
//          could not be written: <cause>"
inline constexpr std::string_view kLibCreationUnrecorded =
    "urmessage: this group is open on the server and its record is on this device's disk, but this "
    "store could not record that the group's creation finished";
// group_open's refusal of a group that IS open on the server when its record could not be persisted
// (group.go, Open, at 93cc0afb85b1): a restart would refuse to send in it. Its stable part is the
// clause before ", so": what happened, and not what it costs.
//   whole: "urmessage: this group is open on the server and its record could not be persisted, so a
//          restart would refuse to send in it: <cause>"
inline constexpr std::string_view kLibOpenRecordNotPersisted =
    "urmessage: this group is open on the server and its record could not be persisted";
// THE LEAVE MARK, HELD AGAINST WHAT STANDS BESIDE IT (R36 item 2; T1 FIX 6, at 9df6fca8414f). A leave
// this device recorded is finished only while its mark still names the join of the group that
// stands. A mark that cannot be held against what stands decides nothing, and the call is refused
// with the class's own sentence after its clause, so the class's row answers it on every road:
//   leave    "urmessage: a leave of group <hex> this device recorded could not be held against what
//            stands beside it on the disk, so nothing was left: <the class's sentence>: the leave
//            mark in <dir> ..." (forget.go, ForgetGroup), which the bridge answers REFUSED
//   join     the same clause, "..., so the invite into it is refused: <the class's sentence>: ..."
//            (forget.go, finishLeaveBefore); CreateGroup's "so no group is founded under it" reaches
//            the founding road, which says it in that road's own words
//   restore  the store's own sentence, unwrapped: "<the class's sentence>: the leave mark in <dir>
//            ..." (restore.go)
// After the class's sentence the store says which read it was: "the leave mark in <dir> could not be
// read, so it decides nothing", "... names epoch <n>, and whether a record above it stands could not
// be read, so the mark decides nothing yet", "... names epoch <n>, and whether an epoch state above it
// stands could not be read, so the mark decides nothing yet", "... names epoch <n>, something of a
// later join stands above it, and the mark could not be removed", or "the leave mark in <dir> names
// no epoch it decided on, so it decides nothing" (the unproven class; no mark this build writes is
// one, and none stands after the T1 reset).
// A mark naming an EARLIER join than what stands is removed, and nothing is erased. At the erase that
// is ErrGroupNotRestored's answer, "<ErrGroupNotRestored's sentence>: <hex>: urmessage: the leave mark
// named an earlier join of this group than what stands on the disk, so it was removed and nothing was
// erased: group <hex>" (forget.go, leaveUnfinished), answered as ErrGroupNotRestored is. And a leave
// whose epoch cannot be read cannot write its mark: "urmessage: the leave could not be recorded on the
// disk, so nothing was left: <ErrStateStoreState's sentence>: <dir> could not be listed, so the epoch
// this leave decides on could not be read: <cause>" (forget.go; statestore_durable.go,
// MarkGroupForgetting), which keeps Task 39's fallback, the leave offered again.
// A MARK THAT NAMES ANOTHER GROUP decides nothing, and it is no class: it is a file this store did
// not write for this folder, and it stays until it is moved out (statestore_durable.go,
// leaveMarkLocked). Joins and leaves of the group are refused while it stands.
//   whole: "urmessage: this does not read as a record this store writes: the leave mark in <dir>
//          does not name its own group"
// Matched by its words before the folder's name, the name (64 lower-case hex digits), and its words
// after it, which together are in no other sentence the library makes.
inline constexpr std::string_view kLibLeaveMarkForeignHead = "the leave mark in ";
inline constexpr std::string_view kLibLeaveMarkForeignAfterName = " does not name its own group";

// ── what a person is shown: Task 58's table, verbatim ─────────────────────────────────────────

// The window's refusal for every cause but the store's own (R34 A1), an invitation damaged in
// transit among them.
inline constexpr std::string_view kSayJoinRefused =
    "This invitation was refused. Your join code is still good: ask for the invitation to be sent "
    "again another way.";
// R34 A1: the window's refusal of a join the store would not serve (ErrJoinRefused's sentence, and
// ErrStateStoreState's after it). The row above is false of it: a re-sent invitation reads the same
// join code file and is refused the same way. No "Make a new join code": the join code is still good.
inline constexpr std::string_view kSayJoinRefusedByStore =
    "URmessage could not use its own folder, so this invitation was not used. Your join code is "
    "still good: check that URmessage can read and write its folder, then paste the invitation "
    "again.";
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
// would erase it. R34 A2: it ends "then open URmessage again". The join gate answers from the last
// Restore's note, so the same invitation pasted again is refused until a Restore runs, and opening
// URmessage again runs one, on every road.
inline constexpr std::string_view kSayGroupRecordNotReadNow =
    "This conversation could not be read just now. Close anything else that may be using "
    "URmessage's folder, then open URmessage again.";
// R29: a file of the group could not be read for a reason this build cannot classify (R30: a deny
// ACL is one; R33: a file moved away is one). On every road the library reports it. NO LEAVE IS
// OFFERED FOR IT, for Q5's reason. R34 A3: it names a moved file, as the library's own remedy does
// ("fixing the folder's access, or putting back what was moved out of it").
inline constexpr std::string_view kSayGroupReadUnproven =
    "This conversation could not be read on this device, and nothing was changed. Check that "
    "URmessage can read its folder and that no program has moved files out of it, then open "
    "URmessage again.";
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
// THE FOUNDING ROAD'S OWN ROWS FOR A group_open THAT FAILED AFTER THE GROUP WAS PUBLISHED. The road
// adds the person, builds the invitation and then opens the group, and every refusal of the open
// returns before the invitation is shown, so each row says so. Its own "the group could not be
// published to the server" stays for every other refusal of the open, where it is true.
// R35 A8, as R37 amends it: the double failure (kLibCreationUnrecorded). The group IS open on the
// server and its record is on the disk; adding the same person again after the reopen meets R25 Q1's
// row for a person whose entry still stands, unchanged.
inline constexpr std::string_view kSayCreationUnfinished =
    "This conversation was created, but URmessage could not finish saving it, and its invitation was "
    "not shown. Close anything else that may be using URmessage's folder, then open URmessage again.";
// R37 A9: the record could not be persisted (kLibOpenRecordNotPersisted). The group IS open on the
// server, and this device would refuse to send in it after a restart.
inline constexpr std::string_view kSayOpenRecordNotSaved =
    "URmessage could not save this new conversation on this device, so it cannot be used, and its "
    "invitation was not shown. Check that URmessage can read and write its folder, then start the "
    "conversation again.";
// R40 A10: a leave mark in this conversation's folder that names another group (kLibLeaveMarkForeign,
// FIX 6 at 9df6fca8414f). Nothing was changed, and nothing this app can do moves it: the join and the
// leave of this group are both refused while it stands, so no leave is offered and the invitation is
// kept. The library's own words follow it on the screen, as on the fallback rows, so the folder is
// named (WithLibraryWords).
inline constexpr std::string_view kSayLeaveMarkForeign =
    "This conversation's folder on this device holds a note about leaving a different conversation, "
    "so URmessage cannot tell whether this one was left. Nothing was changed. It cannot be joined or "
    "left on this device until the file named 'forgetting' is moved out of that folder.";

// ── the join dialog's own copy ─────────────────────────────────────────────────────────────────

// THE DIALOG'S TITLE, BY ITS ROAD (the lead's ruling on item 4, 2026-10-18). A title must not
// contradict the sentence under it: a store that cannot make a join code was titled "Join a group"
// over "This installation cannot make a join code, so it cannot join a group". Each title names its
// own state, in the person's words, and only the join road is titled "Join a group". --diagnose's
// dialog title line holds each against its sentence.
inline constexpr wchar_t kNoCredentialTitle[] = L"URmessage needs its credential";
inline constexpr wchar_t kNoJoinCodeTitle[] = L"URmessage cannot make a join code";
inline constexpr wchar_t kJoinTitle[] = L"Join a group";
// The title for a state: no credential, a store that cannot make a join code, or the join road.
std::wstring_view OnboardTitle(bool noCredential, bool noJoinCode);

// The intro on the join road, as it was before this file.
inline constexpr std::wstring_view kJoinIntro =
    L"This device is connected but is not in a group yet. A group is opened by somebody who is "
    L"already in one: send them the join code below, and paste the invitation they send back.";
// THE INTRO WHILE THIS DEVICE HOLDS A GROUP: while the leave of a held group is on offer (R20), and
// on the founder's road. "Not in a group yet" is false there; the sentence under it says the rest.
inline constexpr std::wstring_view kHeldIntro = L"This device is connected.";
// The joiner's paste label (R40 A11). It said "whoever has it is in the group", which is false since
// invite version 3: an invitation is sealed to the join code it was made for, so only the device that
// holds that join code can open it. app\tools\verify-copy.ps1 fails the build on any app string that
// says it again.
inline constexpr std::wstring_view kJoinerPasteLabel =
    L"The invitation they send back. Keep it private: it was made for this device's join code, so "
    L"only this device can open it, and it works once.";
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
  JoinRefusedByStore,     // begins with kLibJoinRefused and carries kLibStateStoreState (R34 A1)
  NoJoinCodeForInvite,    // NEVER JoinRefused, whatever else the text carries (R13)
  JoinCodeSpent,
  InviteVersion,
  GroupPredatesJoinSeal,  // at Join (after kLibJoinRefused) and at Restore alike
  GroupRecordNotReadNow,  // at Restore (ErrGroupRecordNotReadNow) and at Join (the gate's unread)
  GroupReadUnproven,      // the same roads, for the unproven class (ErrGroupReadUnproven, R29)
  LeaveMarkForeign,       // a leave mark that names another group, at Join (FIX 6)
};

// Which row a refusal of parse_invite, device_join or device_restore is. Empty text is Other.
//
// THE ORDER IS THE RULE. ErrNoJoinCodeForInvite first, so that no spelling of it can ever reach the
// row that says the join code is still good (R13). Then what the join COST (spent), then the four
// rows more specific than the window's sentence (the unproven class before the transient one, the
// longer-lasting of the two). Then the window's refusal of a join the store would not serve, after
// every not-read class row, so a refusal that carries a class's sentinel keeps its class's row (R34
// A1), and the window's sentence last, by its prefix.
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
// Is this a leave mark that names another group (FIX 6): kLibLeaveMarkForeign's clause around a
// folder's name?
bool IsLeaveMarkForeign(std::string_view libraryError);

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
// the owner's question after that re-try brought the group back (R33 X2). MarkForeign is a leave mark
// in the group's folder that names another group (FIX 6), asked after the classes. Other is
// everything else: the caller's own handling (ErrGroupNotRestored, the stale mark at the erase among
// its causes, and Task 39's fallback for the rest, a write failure among them).
enum class LeaveRefusal {
  Other,
  NotReadNow,
  Unproven,
  NotConnected,
  OwnerMustTransfer,
  MarkForeign,
};
LeaveRefusal ClassifyLeaveRefusal(std::string_view libraryError);
// Does the app restore once more after this refusal, so the conversation shows (R33 X2)? The
// owner's question only: the group's files read, and the next Restore hands the group back.
bool RestoresOnceMore(LeaveRefusal refusal);

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
// The library's own refusal of the leave, by its class. MarkForeign answers its own row, with no
// leave. Other and OwnerMustTransfer answer an empty sentence: the caller restores once more for the
// second, and otherwise says what it said before this file.
HeldCopy LeaveRefusalCopy(LeaveRefusal refusal);
// The refusal's name, for a log line.
std::string_view LeaveRefusalName(LeaveRefusal refusal);

// R40 A10: THE ROWS WHOSE OWN WORDS ARE FOLLOWED ON THE SCREEN BY THE LIBRARY'S, so a folder they name
// is named: a leave mark that names another group, on the join and on the leave. Every other row says
// what it says alone.
bool ShowsLibraryWords(JoinCopy row);
bool ShowsLibraryWords(LeaveRefusal refusal);
// The row's sentence, then the library's own words after it, as the fallback rows show them.
std::string WithLibraryWords(std::string_view sentence, std::string_view libraryError);

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

// Is this group_open's double failure (kLibCreationUnrecorded, R34 item 3)? Then the founding road
// says kSayCreationUnfinished (R35 A8) and not its own "could not be published to the server".
bool IsCreationUnrecorded(std::string_view libraryError);
// Is this group_open's refusal of a group open on the server whose record could not be persisted
// (kLibOpenRecordNotPersisted)? Then the founding road says kSayOpenRecordNotSaved (R37 A9).
bool IsOpenRecordNotPersisted(std::string_view libraryError);
// The founding road's sentence for a refusal of group_open: A8's row, A9's row, or empty for every
// other refusal, where the road's own "could not be published to the server" is true.
std::string_view OpenRefusalCopy(std::string_view libraryError);

}  // namespace urmsg::live
