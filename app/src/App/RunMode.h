// WHICH WORLD IS ON SCREEN, AND EVERY STRING WHOSE TRUTH DEPENDS ON THE ANSWER.
//
// THE RULE THIS FILE EXISTS TO KEEP, AND IT CUTS BOTH WAYS. A string here must describe THIS RUN.
// It may never claim a message was encrypted, verified or attested when it was not — and it may
// never DENY that when it was. The second half is not a softening of the first: the app shipped
// "Demo model: end-to-end encrypted / Demo model: fabricated data, no crypto in this build"
// directly above a record that a real device really did open under MLS, which is a false statement
// about this product's security in the direction that costs the product the benefit of the doubt.
//
// SO EVERY STRING BELOW IS A PAIR, NOT A COMPROMISE. One wording that happens to be true in both
// modes is almost always the vaguer of the three candidates; two precise wordings cost one `switch`
// and say the true thing twice. urmsg::RunModeCopyDiagnostics() asserts the pairs are DISJOINT —
// the live wording cannot appear in fabricated mode and the fabricated wording cannot appear in
// live mode — because a gate that only checks "the string is non-empty" would pass the exact defect
// this file was written to remove.
//
// THE MODE IS NOT THE COMMAND-LINE SWITCH, and that distinction is load bearing. urmsg::live::
// IsEnabled() answers "was --live asked for", which is TRUE while the mesh is still dialling and
// stays true if it never connects at all — and the window draws the fabricated world in exactly
// that case. Keying the copy off the switch would therefore put "Live session: end-to-end
// encrypted" over fabricated data on a failed connect, i.e. the original defect with the polarity
// flipped. ActiveRunMode() is instead LATCHED BY THE WINDOW, on the UI thread, in the same
// statement that makes the live world the one it draws (MainWindow::ApplyLiveWorld). Mode and
// pixels flip together or not at all.
//
// PURE C++, NO winrt, NO XAML, and App.vcxproj compiles RunMode.cpp with
// PrecompiledHeader=NotUsing so that is a property of the build. Startup.cpp's CollectDiagnostics()
// runs RunModeCopyDiagnostics() from wWinMain BEFORE winrt::init_apartment(), and it runs on EVERY
// launch with CI reading the output for the token FAIL.
//
// WHAT IS *NOT* HERE. Two strings keep their existing owners because each already has a dedicated
// gate built around its polarity, and moving them would have moved the gate too:
// InspectRailFields.cpp's AttestationLabel and NetworkPageView.cpp's FormatKeyState. Both take a
// RunMode the same way everything here does, and both are asserted in both modes where they live.
//
// NOR ARE THE CAPABILITY-KEYED NAMES, and the distinction is the one this file's own send clause
// draws: a string chosen by whether a SESSION CAN ACT is a different kind of string from one chosen
// by which world is on screen. The composer's Send names, the two [ Try again ] names and the
// per-bubble Reply / React names (Views/ThreadLayout.h's BubbleActionName) all take the capability
// (Views/ThreadView.h's CanRetrySend) rather than the mode, which is what keeps them true during a
// --live launch whose mesh has not answered yet - the fabricated world is drawn, no session exists,
// and "there is no live session" is the true sentence while ActiveRunMode() still says fabricated.
// Their gate is Startup.cpp's `bubble actions` line, which evaluates both arms by passing the
// capability explicitly, exactly as RunModeCopyDiagnostics passes the mode.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

namespace urmsg {

// Fabricated: Demo/DemoWorld.cpp's seeded fixture. Nothing was encrypted, nothing crossed a
// network, and the SDK is /DELAYLOAD'ed and not even mapped into the process.
// Live: App/Live/*.cpp published a world built from records this device fetched from the message
// server and opened under MLS.
enum class RunMode { Fabricated, Live };

// The mode the window is DRAWING. Fabricated until the window latches Live; never goes back.
RunMode ActiveRunMode();

// Called by MainWindow::ApplyLiveWorld, on the UI thread, beside the assignment that makes the
// live snapshot the world every view reads. Idempotent.
void SetActiveRunMode(RunMode mode);

// ---- WHICH ARM A STRING TAKES: THE LAUNCH AS WELL AS THE LATCH ------------------------------
//
// ActiveRunMode() answers "which world is on screen" and nothing else, and on its own it chose the
// WRONG ARM for a whole kind of launch. A PLAIN LAUNCH of the standard build is live: it draws the
// EMPTY world while its mesh dials, and for the whole session when the mesh never answers (no
// credential, no join code, offline) - and ActiveRunMode() says Fabricated all that time, because
// no live world has landed. Every string chosen by the latch alone therefore put the demo's words
// over a run with no demo in it. Measured on main f4280508 (ci/plain-control-1, run 39098035170):
// "Demo model: server key not verified" named the status strip's padlock on every page, Settings
// said "THIS DEMO" and "Demo model: not verified", the relay drawer said "DEMO MODEL: RELAY PATH",
// and the Developer page drew the whole fabricated fixture.
//
// SO THE COPY IS CHOSEN BY THE LAUNCH AS WELL. A FABRICATED LAUNCH (--demo, or any --demo-*
// switch: MainWindow's DemoOptions::enabled, which ParseDemoOptions answers) takes the fabricated
// arm until a live world lands, exactly as before. Every other launch takes the LIVE arm, and
// before its live world lands it says that too: the strings whose live arm speaks of a session or
// of "these messages" have a NO-SESSION arm, because those live arms are false over a screen with
// nothing on it (ComposerNote's third arm is the precedent, and its gate the model for theirs).
//
// THE CAPABILITY GATES STAY ON THE LATCH. A control that may only act on a live world (the rail's
// label, delete and leave rows) asks ActiveRunMode() == Live, which is "a live world is on
// screen": the right question for a capability, and the wrong one for a sentence.

// Recorded once, by MainWindow's constructor, before any view is built.
void SetFabricatedLaunch(bool fabricated);
bool FabricatedLaunch();

// Pure, so --diagnose reads every (launch, latch) pair:
//   a fabricated launch, no live world yet -> Fabricated
//   any launch, a live world on screen      -> Live
//   any other launch, no live world yet     -> Live, and no session
constexpr RunMode DrawnRunModeFor(bool fabricatedLaunch, RunMode latched) {
  return fabricatedLaunch && latched == RunMode::Fabricated ? RunMode::Fabricated : RunMode::Live;
}
constexpr bool NoSessionFor(bool fabricatedLaunch, RunMode latched) {
  return !fabricatedLaunch && latched == RunMode::Fabricated;
}

// The mode, and the no-session bit, every view's COPY is chosen by: the two above over
// FabricatedLaunch() and ActiveRunMode(). Read at build time, as ActiveRunMode() was; the window
// rebuilds the pages that use them on the beat a live world lands (MainWindow::ApplyLiveWorld).
RunMode DrawnRunMode();
bool DrawnNoSession();

// THE CHIP'S RULE, beside the copy because it is the same question: the title-bar chip is shown
// before a live world lands only in a fabricated launch (and only while --demo-watermark=off has
// not removed it). Over a plain launch's empty world neither of its words is true - there is no
// demo and no live session yet - so it is not shown at all; MainWindow::ApplyLiveWorld shows it,
// saying LIVE, on the beat a live world lands.
constexpr bool ModeChipShownBeforeLiveWorld(bool fabricatedLaunch, bool watermark) {
  return fabricatedLaunch && watermark;
}

// ---- the copy -------------------------------------------------------------------------------
// Each of these is a pair. The fabricated form names the demo and never the session; the live form
// names the session and never the demo. RunModeCopyDiagnostics asserts exactly that, in both
// directions, so a copy edit that reaches for one wording in both modes fails the launch.

// The title-bar chip. "DEMO" over a real mesh is the loudest of the false denials, because it is
// the one string a reader sees without opening anything.
std::wstring ModeChipText(RunMode mode);

// The inspect rail's lock-header card: the title beside the padlock, and the note under it that
// says WHY. Prefix-first in both modes — a trailing qualifier is the half a screenshot crop, a
// narrow column or a trimmed TextBlock throws away first, and the framing has to survive being
// read alone.
std::wstring LockHeaderTitle(RunMode mode);
std::wstring LockHeaderNote(RunMode mode);

// The line under the composer, and the ONE STRING IN THIS FILE WHOSE TRUTH DEPENDS ON A CAPABILITY
// RATHER THAN ON WHICH DATA IS ON SCREEN. In live mode this app SENDS: the composer's button calls
// urnet_message_group_send through the live worker, and a failed send draws as a "Not sent" bubble
// with a retry that works. In fabricated mode it sends nothing and never could. So the live form
// affirms the send path and names the ceiling the protocol leaves it at (Sent - nothing reports
// delivery), and the fabricated form keeps "nothing is sent, and no message leaves this window",
// which is a claim about isolation and is true of exactly one of the two modes.
//
// IT HAS NOW BEEN WRONG IN BOTH DIRECTIONS, one release apart, which is why it has a gate of its
// own. First the fabricated wording was printed under real messages (a denial of the crypto that
// had run); then the live wording said "sending is not wired up yet" under a button that was about
// to be wired (a denial of a capability).
//
// `maySend` IS THE ROLE (item 242 R4) and it is a THIRD state rather than a second mode: a live
// session this device may only READ. Its wording is Spec C §5.6's own sentence, verbatim and with
// NO caveat (ruling 22 — the caveat is about other people's clients and belongs where the group is
// configured), and the fabricated arm deliberately does not branch on it, because what stops a
// send in the demo is not the role.
//
// RunModeCopyDiagnostics' SEND CLAUSE is asserted PER STATE since R4, not as one boolean over two
// strings: the fabricated arms must deny the send path, live+may-send must not, and live+observer
// must deny it WITHOUT denying the session. The old single-boolean form broke by construction the
// day a live composer could honestly deny sending; the phrases the clause looks for are printed
// beside the verdict, per state.
//
// AND IT DRIVES THE ROLE. `maySend` is not passed as a literal by the gate: it is DERIVED for each
// of the four role spellings (and one this build has no source for) by demo::RoleMaySend, the
// predicate MainWindow::OpenConversationMaySend answers with — so the word "observer" on that line
// is a statement about the mapping and not a label over a bool the gate handed itself, and the
// expectation is keyed on the role's NAME so the mapping cannot mark its own paper.
//
// AND A THIRD ARM, FOR A STATE THAT DID NOT EXIST WHEN THE PAIR WAS WRITTEN. `noSession` is true on
// a --live launch that is drawing the EMPTY world: the mesh is dialling, or has answered and no
// group is on the screen. Both wordings above are false there and in different ways. "Demo
// model - nothing is sent, and no message leaves this window" is false because there is no demo:
// no fabricated row is on screen, the list is empty, and the sentence invites a reader to think
// the thing they are looking at is a mock-up. "Live session - these messages are real" is false
// because there are no messages and no session.
//
// THE FILE'S OWN REASONING COVERED THE OLD THIRD CASE AND NOT THIS ONE. It says a --live launch
// whose mesh has not answered draws the FABRICATED world, so the fabricated wording is true there
// - and that held for exactly as long as --live implied --demo. It does not any more.
//
// The true sentence names the absence ON THE SCREEN and claims nothing about this device. The
// empty world is drawn for a device in no group, and also for one whose group is on its disk and
// did not open (R25 Q4), one that cannot make a join code, and a launch whose mesh has not
// answered, which knows none of this yet. So it neither says "no group" of the device nor tells
// it to join one - the onboarding dialog says the right thing for each (Live/RefusalCopy.h) -
// and RunModeCopyDiagnostics' `run mode held` holds every third arm to that. It claims nothing
// about encryption in either direction, because nothing has been encrypted or decrypted yet.
std::wstring ComposerNote(RunMode mode, bool maySend, bool noSession = false);

// The Network page's relay-path caption (chrome voice, letterspaced uppercase) and the status
// strip drawer's automation name for the same three nodes. In live mode those nodes are
// observations — the platform url the library dialled and the message server's own client_id — so
// "preview" is wrong there.
//
// `noSession` (a launch that is not fabricated, before its live world lands; DrawnNoSession) is
// the THIRD ARM, and it wins over the mode as ComposerNote's does: there is no path to observe yet,
// so neither "preview" nor "Live session" is true.
//
// No default for `noSession`, here or on the four below: every caller says which world it draws,
// because a default false is the live arm over the empty world for any new call site.
std::wstring RelayPathGroupTitle(RunMode mode, bool noSession);
std::wstring RelayDrawerName(RunMode mode, bool noSession);

// The status strip padlock's accessible name. A padlock is a claim and a Button whose Content is a
// Panel gets no automatic name, so the words are the only thing carrying the meaning.
// In live mode the two arms are ONE string: this build pins no server key and the live world never
// sets the bit, so there is no result to report either way. With `noSession` there is no session
// whose key could be named at all, so the third arm says that and claims no result.
std::wstring ServerKeyStateName(bool keyVerified, RunMode mode, bool noSession);

// The first line of the Developer page's DemoWorld dump, which rides the Copy button onto the
// clipboard where no page header can follow it. BOTH forms keep the "Demo model:" framing, because
// DumpDemoWorld renders demo::GetWorld() in both modes — the live form adds that the dump is the
// fixture and NOT the session on screen, which is the thing a reader of a live window would
// otherwise get wrong.
std::wstring WorldDumpFramingLine(RunMode mode);

// The note under the inspect rail's MEMBERS card (item 242 R3). The roster is the one surface
// whose two arms differ in what is REAL rather than in what is claimed: the fabricated arm's
// members, names and roles are all fixture; the live arm's roster and roles are the group's own
// (urnet_message_group_members) while the names are not carried by anything. Prefix-first, like
// the lock header, so a crop keeps the framing.
std::wstring RosterNote(RunMode mode);

// Settings' disclosure block: the caption, the row that opens it, and the paragraph inside.
// The fabricated paragraph's "No protocol, no store, no network and no cryptography are running"
// is the single most false sentence in the app when a live world is on screen.
//
// Over the EMPTY WORLD the live paragraph is false too - "the messages on these screens are
// real: this device fetched the records" over a screen with no message on it - so the third arm
// says that no group is on these screens yet, says what the live paragraph says about a group IN
// THE CONDITIONAL, and keeps the live absence list word for word (RunModeCopyDiagnostics holds the
// two lists equal). Its bit is DrawnNoSession() OR a live world with no conversation in it (a
// device that left its group): SettingsView passes both, since the latch never goes back.
std::wstring DisclosureCaption(RunMode mode, bool noSession);
std::wstring DisclosureTitle(RunMode mode, bool noSession);
std::wstring DisclosureBody(RunMode mode, bool noSession);

// ---- the identity screens (identity phase 1, Task 6: slice 0) ---------------------------------
//
// A SECOND KIND OF PAIR, gated by its own clause rather than by the session pairs' rule. Every pair
// above is about the SESSION, so its live arm names the live session. These are about the ACCOUNT:
// the words, the ID, a removal, a notice. Their live arm says the account's own thing, with no
// session prefix, because a phrase and an ID exist whether or not a mesh answered. So the clause
// asks that the fabricated arm name the demo ("demo", and not "live"), that the live arm NOT name
// the demo, and that the two differ. With Demo/IdentityDemo.h's rule that every fabricated fact
// names the demo, that is the task's Property 1: no fabricated screen shows a string the live
// screen would show as a fact.
//
// The live arms are the copy SCOPE gives the live screens of Tasks 16, 28, 38 and 45, which are not
// built yet: in this build no identity screen is drawn in live mode at all. They are here now so
// that the clause holds the two arms apart from the first commit that has either.

// The label over the 25 words (SCOPE H2). In fabricated mode it is what makes 25 words on a screen
// not a recovery phrase, which is why its clause asks for the denial itself and not just a
// difference (PLAN Task 6, MA1).
std::wstring PhraseWordsLabel(RunMode mode);
// The sentence under the words (M5:416, SCOPE H2).
std::wstring PhraseWordsCaveat(RunMode mode);
// The note beside "Your ID" in Settings -> Account (SCOPE H4, B1).
std::wstring AccountIdNote(RunMode mode);
// The removed screen's title (SCOPE G5).
std::wstring RemovedTitle(RunMode mode);
// The Remove dialog's line on an only device (SCOPE G1).
std::wstring OnlyDeviceLine(RunMode mode);
// The notice a phrase device's self-removal leaves (SCOPE G6).
std::wstring SelfRemovalNotice(RunMode mode, std::wstring const& device);
// The done line of a phrase change (PLAN Task 45's toast; SCOPE K3, K6).
std::wstring PhraseChangedLine(RunMode mode, std::wstring const& device);

// ---- the gate -------------------------------------------------------------------------------
// One line per pair, each printing PASS/FAIL, the QUERY it asked, and both wordings, so a reader
// can tell what was checked rather than trusting the word PASS. Called from CollectDiagnostics on
// every launch; the mode of the RUN is irrelevant to it, because it evaluates BOTH arms by passing
// the enum explicitly. That is the only way a fabricated launch can gate the live copy at all.
//
// THE CLAUSES BEYOND THE PAIRS, each one a property the pair test cannot see:
//   * the DUMP line, whose two arms deliberately share a prefix;
//   * the SEND clause, asserted PER (MODE, ROLE) since item 242 R4 - the fabricated arm must deny
//     the send path whatever the role and must be ONE string across the roles; the live arm must
//     deny for the OBSERVER and for no other role; and no live arm may deny the SESSION, an
//     observer's session being provably live. The old single-boolean form ("the live arm must deny
//     none of it") broke by construction the day a live composer could honestly deny sending, which
//     is the day R4 landed; and the role is DERIVED by demo::RoleMaySend rather than passed in as
//     an answer, so the line's word "observer" is the mapping and not a label over it;
//   * the ABSENCE clause - the live disclosure's "What is NOT here:" list, printed in full, must
//     not name the send path among the things the app cannot do, nor the member list or the
//     roles, which the app reads off the group since item 242 R3;
//   * the IDENTITY clause (Task 6) - the account pairs above, each fabricated arm naming the demo
//     and no live arm naming it - and the WORDS line, which asks the words label for the denial
//     itself: the fabricated arm must say the words are "not a recovery phrase", and the live arm
//     must not;
//   * the NO-SESSION clause, over EVERY third arm (the composer's and the six above): each differs
//     from both of its function's arms, names neither mode, and the disclosure's keeps the live
//     absence list word for word; and the LAUNCH table, DrawnRunModeFor and NoSessionFor over all
//     four (launch, latch) pairs;
//   * the HELD clause - no third arm says this device is in no group, or tells it to join one: the
//     empty world is also a device whose group did not open (R25 Q4) and a store that cannot make a
//     join code (Task 58). Its control is in the same query, both ways.
std::vector<std::wstring> RunModeCopyDiagnostics();

}  // namespace urmsg
