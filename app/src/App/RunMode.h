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

// The line under the composer. In BOTH modes this app cannot send: the ABI has the send verbs
// (urnet_message_group_send_reply/_react/_unreact/_delete) and the button is not wired to any of
// them. So the live form says THAT, plainly, instead of "nothing is sent, and no message leaves
// this window" — which reads as a claim about isolation beside messages that visibly arrived from
// another machine.
std::wstring ComposerNote(RunMode mode);

// The Network page's relay-path caption (chrome voice, letterspaced uppercase) and the status
// strip drawer's automation name for the same three nodes. In live mode those nodes are
// observations — the platform url the library dialled and the message server's own client_id — so
// "preview" is wrong there.
std::wstring RelayPathGroupTitle(RunMode mode);
std::wstring RelayDrawerName(RunMode mode);

// The status strip padlock's accessible name. A padlock is a claim and a Button whose Content is a
// Panel gets no automatic name, so the words are the only thing carrying the meaning.
// In live mode the two arms are ONE string: this build pins no server key and the live world never
// sets the bit, so there is no result to report either way.
std::wstring ServerKeyStateName(bool keyVerified, RunMode mode);

// The first line of the Developer page's DemoWorld dump, which rides the Copy button onto the
// clipboard where no page header can follow it. BOTH forms keep the "Demo model:" framing, because
// DumpDemoWorld renders demo::GetWorld() in both modes — the live form adds that the dump is the
// fixture and NOT the session on screen, which is the thing a reader of a live window would
// otherwise get wrong.
std::wstring WorldDumpFramingLine(RunMode mode);

// Settings' disclosure block: the caption, the row that opens it, and the paragraph inside.
// The fabricated paragraph's "No protocol, no store, no network and no cryptography are running"
// is the single most false sentence in the app when a live world is on screen.
std::wstring DisclosureCaption(RunMode mode);
std::wstring DisclosureTitle(RunMode mode);
std::wstring DisclosureBody(RunMode mode);

// ---- the gate -------------------------------------------------------------------------------
// One line per pair, each printing PASS/FAIL, the QUERY it asked, and both wordings, so a reader
// can tell what was checked rather than trusting the word PASS. Called from CollectDiagnostics on
// every launch; the mode of the RUN is irrelevant to it, because it evaluates BOTH arms by passing
// the enum explicitly. That is the only way a fabricated launch can gate the live copy at all.
std::vector<std::wstring> RunModeCopyDiagnostics();

}  // namespace urmsg
