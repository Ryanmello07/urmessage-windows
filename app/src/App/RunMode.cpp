// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader>, the same as Demo/DemoWorld.cpp,
// Views/InspectRailFields.cpp and Views/DeveloperDump.cpp. Startup.cpp's CollectDiagnostics()
// calls RunModeCopyDiagnostics() from wWinMain BEFORE winrt::init_apartment(), so "pure C++, no
// winrt" has to be a property the compiler enforces rather than a comment at the top of a file.
//
// Read RunMode.h first: it carries the rule these strings implement and why each one is a PAIR.
#include "RunMode.h"

#include <atomic>
#include <format>
#include <iterator>
#include <string_view>

// The role spellings and demo::RoleMaySend, for the send clause below: it DRIVES the role ->
// mayRoleSend mapping the composer's note is chosen by rather than handing itself the answer.
// DemoWorld.h is pure C++ and compiled NotUsing the pch for the same reason this file is, so the
// "no winrt before init_apartment" property survives the include.
#include "Demo/DemoWorld.h"

namespace urmsg {
namespace {

// Fabricated until the window says otherwise. Relaxed is the right order: the only writer is the
// UI thread in MainWindow::ApplyLiveWorld and every reader that matters is that same thread
// building views a few statements later. The atomic is here so that a stray read from the live
// worker (a log line, a future probe) is defined behaviour rather than a data race.
std::atomic<int> g_mode{static_cast<int>(RunMode::Fabricated)};

// Whether this is a fabricated launch (RunMode.h, "which arm a string takes"). MainWindow's
// constructor records it in the statement after it parses the options, before any view is built,
// so no reader runs before it (neither default would be right for one that did: false would put
// the third arms over the fixture, true the demo's words over the empty world). The same relaxed
// order as the latch, for the same reason.
std::atomic<bool> g_fabricatedLaunch{false};

// Every non-ASCII character printed as <U+XXXX>. --diagnose writes UTF-8 bytes through a
// redirected handle (Startup.cpp:156-171) and Windows PowerShell 5.1's Get-Content decodes ANSI by
// default, so an em dash comes back as mojibake in the one place these lines are read. The same
// helper InspectRailFields.cpp keeps, for the same reason.
std::wstring AsciiOnly(std::wstring const& s) {
  std::wstring out;
  for (wchar_t c : s) {
    if (c < 0x20 || 0x7E < c)
      out += std::format(L"<U+{:04X}>", static_cast<unsigned int>(c));
    else
      out.push_back(c);
  }
  return out;
}

// Case-insensitive ASCII substring test. The chrome voice is uppercase (DEMO MODEL: RELAY PATH)
// and the automation-name voice is mixed case, and the gated property is which WORD is present,
// not which casing carries it.
bool ContainsWord(std::wstring const& haystack, std::wstring const& needleLower) {
  if (needleLower.size() > haystack.size()) return false;
  for (size_t i = 0; i + needleLower.size() <= haystack.size(); ++i) {
    bool hit = true;
    for (size_t j = 0; j < needleLower.size(); ++j) {
      wchar_t c = haystack[i + j];
      if (L'A' <= c && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
      if (c != needleLower[j]) {
        hit = false;
        break;
      }
    }
    if (hit) return true;
  }
  return false;
}

// THE PROPERTY, and it is deliberately a property and not a list of literals. A blacklist of the
// twenty strings below would pass on any edit that changed them, which is the one thing a copy
// gate has to survive. What is asserted instead is that the two arms of a pair cannot be confused:
// the fabricated arm says DEMO and never LIVE, the live arm says LIVE and never DEMO, and they
// differ. An editor who reaches for one wording in both modes — the exact defect this file was
// written to remove — fails here whatever words they reached for.
//
// The three limits, stated rather than papered over:
//   1. Naming the mode is NECESSARY, NOT SUFFICIENT. "Live session: attestation PASSED" names the
//      mode, differs from its pair and asserts a check nothing ran. Only reading the copy catches
//      that.
//   2. A pair whose live arm is the ONE placeholder (AttestationLabel, and FormatKeyState's
//      unpinned arm) cannot
//      satisfy this — "unavailable" names no mode — so those two live with their own gates, in
//      their own files, where the polarity checks they already had could be extended instead of
//      duplicated.
//   3. The dump's framing line is deliberately "Demo model:" in BOTH modes, because the dump
//      renders the fixture in both. It is gated separately, below, on the property that actually
//      distinguishes the two.
struct Pair {
  std::wstring_view what;
  std::wstring fabricated;
  std::wstring live;
};

bool PairOk(Pair const& p) {
  return !p.fabricated.empty() && !p.live.empty() && p.fabricated != p.live &&
         ContainsWord(p.fabricated, L"demo") && !ContainsWord(p.fabricated, L"live") &&
         ContainsWord(p.live, L"live") && !ContainsWord(p.live, L"demo");
}

// ---- the send clause, which is limit 1 above in the one place it has actually bitten ----------
//
// EVERY DENIAL OF A SEND PATH THIS APP HAS EVER PRINTED. PairOk asks only that the two arms name
// their modes and differ, and "Live session - these messages are real; sending is not wired up yet"
// satisfied all three of those clauses for an entire release while the ABI's send verbs sat
// unspent. The day the Send button was wired to urnet_message_group_send that sentence became a
// denial of something the app does, PairOk went on passing it, and nothing else would have looked.
//
// So the property asserted below is the one that actually tracks the capability: THE FABRICATED ARM
// MUST DENY THE SEND PATH AND THE LIVE ARM MUST NOT. Both directions matter. A live arm that keeps
// a denial is the defect this commit removed; a fabricated arm that loses one is "Demo model" over
// a composer a reader would reasonably believe sends, which is the original defect wearing the
// other mode's clothes.
//
// A LIST OF PHRASES IS A BLACKLIST AND THAT IS ADMITTED RATHER THAN HIDDEN: a live arm that denied
// sending in words nobody has used yet would pass. What makes it worth having anyway is WHEN it
// fires - on every launch, against the string that is actually compiled in - and that the
// complement is printed, so a reader of --diagnose sees which phrases each arm carries rather than
// a bare PASS. Add to it when a new wording is introduced; do not replace it with a count.
constexpr std::wstring_view kSendDenials[] = {
    L"not wired",  L"nothing is sent", L"no message leaves",
    L"cannot send", L"not available",  L"does not send",
    // Item 242 R4's wording. Spec C §5.6's observer sentence is a denial of the
    // send path too — the first one this app has printed that is about the ROLE
    // rather than about the build or the session — and a list that did not carry
    // it would have let the live+observer arm pass with no denial in it at all,
    // which is the same hole "not wired" sat in for a release.
    L"not send to it",
};

// Which of them `text` carries, as a printable list. Empty means none.
std::wstring DenialsIn(std::wstring const& text) {
  std::wstring found;
  for (auto const& phrase : kSendDenials) {
    if (!ContainsWord(text, std::wstring(phrase))) continue;
    if (!found.empty()) found += L", ";
    found += phrase;
  }
  return found;
}

// The live disclosure's ABSENCE LIST, on its own: everything between "What is NOT here:" and the
// sentence's full stop. Pulled out rather than searched for inside the whole paragraph because the
// paragraph legitimately talks about sending in the affirmative, and a check that could not tell
// the two apart would either miss the defect or forbid the true sentence.
std::wstring AbsenceList(std::wstring const& disclosure) {
  constexpr std::wstring_view kLead = L"What is NOT here:";
  const size_t at = disclosure.find(kLead);
  if (at == std::wstring::npos) return {};
  const size_t from = at + kLead.size();
  const size_t stop = disclosure.find(L'.', from);
  return disclosure.substr(from, (stop == std::wstring::npos ? disclosure.size() : stop) - from);
}

}  // namespace

RunMode ActiveRunMode() { return static_cast<RunMode>(g_mode.load(std::memory_order_relaxed)); }

void SetActiveRunMode(RunMode mode) {
  g_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

void SetFabricatedLaunch(bool fabricated) {
  g_fabricatedLaunch.store(fabricated, std::memory_order_relaxed);
}

bool FabricatedLaunch() { return g_fabricatedLaunch.load(std::memory_order_relaxed); }

RunMode DrawnRunMode() { return DrawnRunModeFor(FabricatedLaunch(), ActiveRunMode()); }

bool DrawnNoSession() { return NoSessionFor(FabricatedLaunch(), ActiveRunMode()); }

// ---- the copy ---------------------------------------------------------------------------------

std::wstring ModeChipText(RunMode mode) {
  // "LIVE", not "ALPHA" and not a removed chip. The chip's job is to name what the window is
  // showing before a reader opens anything, and --demo-watermark=off can take it away — so it must
  // never be the ONLY thing carrying the framing (every string below carries its own), and it must
  // never say the wrong one of the two words while it is there.
  return mode == RunMode::Live ? L"LIVE" : L"DEMO";
}

std::wstring LockHeaderTitle(RunMode mode) {
  // The claim itself is the SAME claim in both modes and that is correct: the fabricated arm is a
  // statement about the model the demo is showing, the live arm a statement about the records on
  // screen. It is the PREFIX that carries which one, which is why the prefix leads.
  return mode == RunMode::Live ? L"Live session: end-to-end encrypted"
                               : L"Demo model: end-to-end encrypted";
}

std::wstring LockHeaderNote(RunMode mode) {
  // The note is the only one of the header's two lines that says WHY, so it is where the two modes
  // actually diverge. The live wording says what happened to THESE records and stops there: it
  // names MLS because that is what the library's group state is (urnetwork_message.h:142, "where
  // MLS keeps group state and private keys") and the open path is what produced the body above it,
  // and it names no cipher — MessageInspect::cipher is under a standing no-render ruling
  // (InspectRailFields.h) that live mode does not except.
  return mode == RunMode::Live
             ? L"Live session: real records, sealed and opened under MLS on this device"
             : L"Demo model: fabricated data, no crypto in this build";
}

std::wstring ComposerNote(RunMode mode, bool maySend, bool noSession) {
  // U+2014 EM DASH, written as an escape and never as a pasted character (the house non-ASCII
  // rule: an editing pass that silently re-encodes a pasted glyph leaves no build error behind,
  // only a wrong byte in a string).
  //
  // ---- THE THIRD ARM (item 242 R4) ------------------------------------------------------------
  // A LIVE SESSION THIS DEVICE MAY ONLY READ. It says Spec C §5.6's sentence VERBATIM and it says
  // nothing else about the rule, because ruling 22 puts the caveat — "someone who modifies their
  // app can still send … it can only hide the result" — where the group is CONFIGURED (the rail's
  // observer row) and not above the box a person types in. The composer sentence is about THIS
  // app's own behaviour, which after R4 is true unqualified: this client refuses all four sendable
  // kinds for a group it holds OBSERVER in. The caveat is about other people's clients.
  //
  // IT MUST NOT DENY THE SESSION and the send clause below holds it to that: the role could only
  // have been read off an open group, so "there is no live session" is the one false thing this
  // arm could say. That is R3's pending-arm defect stated for a different control.
  //
  // THE FABRICATED ARM DOES NOT BRANCH ON THE ROLE, and that is a decision. What stops a send in
  // the demo is that nothing is wired to a mesh, not the role — so a fabricated observer arm would
  // name the wrong cause. The role still reaches the demo's composer through the BUTTON and the
  // BOX (Views/ThreadLayout.h's three states), which is where the state belongs.
  //
  // The live form does NOT say "nothing is sent, and no message leaves this window". That sentence
  // is about isolation, and printing it under a thread whose newest line arrived from another
  // machine reads as a claim that the app is inert — which is the false denial in miniature. What
  // is true of the send path in both modes WAS that it did not exist yet, and that is no longer
  // true of either.
  //
  // IT NO LONGER SAYS "sending is not wired up yet", AND THAT IS THIS COMMIT. That wording held for
  // exactly as long as the composer's Send button called nothing: the ABI shipped the send verbs
  // and this client spent none of them. The button now calls urnet_message_group_send
  // (Views/ThreadView.cpp's SubmitComposer -> Live/LiveMesh.cpp's QueueSend), so the old sentence
  // became a denial of something the app does -- the same defect as the one above, one release
  // later, and the SEND CLAUSE in RunModeCopyDiagnostics is what will catch the next one.
  //
  // WHAT THE LIVE FORM CLAIMS AND WHERE IT STOPS. It affirms the seal and the submit, because those
  // are what the call does and what its answer reports, and it says plainly that nothing reports
  // delivery -- so the ceiling stays where Live/LiveWorld.cpp puts it (DeliveryState::Sent) and no
  // reader is invited to expect a tick this protocol cannot produce.
  // THE THIRD ARM FIRST, because it is about whether there is anything to send TO, and the two
  // below are about what happens when there is. See the header for why both of them are false
  // while the empty world is on screen.
  if (noSession)
    return L"No group yet \u2014 there is nothing to send to. Join a group to start a conversation.";
  if (mode != RunMode::Live) return L"Demo model \u2014 nothing is sent, and no message leaves this window.";
  if (!maySend)
    return L"Live session \u2014 these messages are real. You can read this group but not send "
           L"to it.";
  return L"Live session \u2014 these messages are real, and so is the Send button: what you "
         L"type is sealed on this device and submitted to the group. Nothing reports "
         L"delivery, so a message you send stops at Sent.";
}

std::wstring RelayPathGroupTitle(RunMode mode, bool noSession) {
  // THE THIRD ARM FIRST, as ComposerNote's. It says what the WINDOW has, which is no path to
  // show, and nothing about the session: a first run with a credential is connected and waiting
  // to be let into a group (LiveMesh's Waiting step), so "no session" would be false there.
  if (noSession) return L"RELAY PATH: NOTHING TO SHOW YET";
  return mode == RunMode::Live ? L"LIVE SESSION: RELAY PATH" : L"DEMO MODEL: RELAY PATH";
}

std::wstring RelayDrawerName(RunMode mode, bool noSession) {
  // "preview" drops in live mode and nothing replaces it: the three nodes there are the platform
  // url the library actually dialled and the message server's own client_id (Live/LiveWorld.cpp),
  // so the drawer is showing a path rather than previewing what one would look like.
  if (noSession) return L"Relay path: nothing to show yet";
  return mode == RunMode::Live ? L"Live session: relay path" : L"Demo model: relay path preview";
}

std::wstring ServerKeyStateName(bool keyVerified, RunMode mode, bool noSession) {
  // THE EMPTY WORLD: the window has no result to name, so the padlock says the house placeholder
  // and nothing about a session (a waiting first run is connected).
  if (noSession) return L"Server key: unavailable";
  // LIVE: the true bit is the PIN (ledger 268) - the URnetwork and direct routes refuse any
  // endpoint key but the compiled-in one - and the false bit is the platform route, which pins
  // nothing, so "not verified" would report the RESULT of a check that never ran. "pinning
  // unavailable" reports that there is no result, the same thing the rail's Attestation row
  // says with the one placeholder.
  if (mode == RunMode::Live)
    return keyVerified ? L"Live session: server key pinned"
                       : L"Live session: server key pinning unavailable";
  return keyVerified ? L"Demo model: server key verified" : L"Demo model: server key not verified";
}

std::wstring WorldDumpFramingLine(RunMode mode) {
  // BOTH arms keep "Demo model:", and WorldDumpDiagnostics still asserts that prefix on the first
  // line of the dump in both modes: DumpDemoWorld renders demo::GetWorld() whichever mode the app
  // is in, so the fixture framing is true either way. What the live arm adds is the disambiguation
  // that only matters in live mode — a reader looking at real messages, who pastes this dump
  // somewhere, must not read it as a record of what they were looking at.
  return mode == RunMode::Live
             ? L"Demo model: fabricated fixture data, and NOT this live session's messages"
             : L"Demo model: fabricated data, no crypto in this build";
}

std::wstring StatusStateText(RunMode mode, std::wstring_view liveWord) {
  if (mode == RunMode::Live || liveWord.empty()) return std::wstring(liveWord);
  std::wstring word(liveWord);
  if (L'A' <= word[0] && word[0] <= L'Z') word[0] = static_cast<wchar_t>(word[0] - L'A' + L'a');
  return L"Demo: " + word;
}

std::wstring RosterNote(RunMode mode) {
  // The live arm says what is real and what is not IN ONE SENTENCE, because the roster is the
  // one card where both are on screen together: real roles beside placeholder names.
  return mode == RunMode::Live
             ? L"Live session: the members and their roles are the group's own. Names are "
               L"unavailable, there being no identity layer yet; a name shown here is one you "
               L"gave, and only you see it"
             : L"Demo model: fabricated members, names and roles";
}

std::wstring DisclosureCaption(RunMode mode, bool noSession) {
  if (noSession) return L"THIS APP";
  return mode == RunMode::Live ? L"LIVE SESSION" : L"THIS DEMO";
}

std::wstring DisclosureTitle(RunMode mode, bool noSession) {
  // The absence list below is about the APP, so over the empty world the title names the app.
  if (noSession) return L"What this app does not do";
  return mode == RunMode::Live ? L"What this live session does not do"
                               : L"What this demo does not do";
}

std::wstring DisclosureBody(RunMode mode, bool noSession) {
  // THE THIRD ARM: the EMPTY WORLD - a launch that is not fabricated before its live world lands,
  // or a live world with no group in it (SettingsView passes both). The live paragraph's opening
  // is false there - no message is on these screens - so this one says so, then says what the live
  // one says about a group IN THE CONDITIONAL, then carries the live absence list WORD FOR WORD
  // (RunModeCopyDiagnostics holds the two lists equal). It says "on these screens", and never "no
  // group": a device whose group is on its disk but did not open still holds one (R25 Q4). And it
  // does not repeat the live paragraph's "every field ... reads unavailable", which the same page
  // contradicts over the empty world (Linked devices 0, a blank server). It names neither mode.
  if (noSession) {
    return L"No group is on these screens yet, so no message is either. In a group, the messages "
           L"here are records this device fetches from the message server and opens under MLS, a "
           L"message you write is sealed on this device and submitted to the group, and the "
           L"members list and each member's role are the group's own. What is NOT here: delivery "
           L"and read receipts, contact discovery and the names behind members, group names, "
           L"attachments, and any per-message attestation check.";
  }
  // The fabricated paragraph is the one that shipped, with the mode prefix added so it is framed
  // the way every other string here is framed. Not one word of its content changed.
  //
  // The live paragraph is the same SHAPE — what is real, then the complete list of what is absent,
  // then how the absences are drawn — because that shape is what makes the disclosure checkable
  // against the app instead of reassuring. The absence list is the ABI's own
  // (urnetwork_message.h: "WHAT IS STILL NOT HERE: receipts, edit, media, group names, contact
  // discovery, a third member") plus the one this CLIENT is still missing: any attestation check.
  //
  // "sending" CAME OUT OF THAT ABSENCE LIST IN THE SAME COMMIT THAT WIRED THE BUTTON, and it is
  // called out here because a stale absence list is the quieter half of this defect: nobody
  // re-reads the long paragraph on the Settings page, so it would have gone on listing a capability
  // the app has one screen away from the button that has it. The absence-list clause in
  // RunModeCopyDiagnostics reads THE LIST ITSELF rather than the paragraph, so the next one fails a
  // launch rather than waiting for somebody to re-read a wall of text.
  //
  // "member lists" CAME OUT THE SAME WAY with item 242 R3: the rail now draws the group's roster
  // and each member's role off the library, and the owner's and admins' controls change them. What
  // stays absent is the NAME behind a member (contact discovery, an identity layer) - and the
  // list says that instead, so the absence clause can hold the list to it.
  if (mode == RunMode::Live) {
    return L"Live session \u2014 the messages on these screens are real: this device fetched the "
           L"records from the message server and opened them under MLS, and a message you write "
           L"here is sealed on this device and submitted to the group. The members list and each "
           L"member's role are the group's own, and an owner or admin changes them from here. What "
           L"is NOT here: delivery and read receipts, contact discovery and the names behind "
           L"members, group names, attachments, and any per-message attestation check. Every field "
           L"this build has no source for reads \"unavailable\" rather than a guess.";
  }
  return L"Demo model \u2014 no protocol, no store, no network and no cryptography are running. "
         L"Every value on these screens is fabricated in one module. Nothing has been sent, "
         L"received, stored, encrypted or decrypted. The inspector shows what URmessage will one "
         L"day say about a real message; it is not a statement about one.";
}

// ---- the identity screens (Task 6) ------------------------------------------------------------
// Prefix-first in the fabricated arm, as everywhere in this file: a crop keeps the framing. The
// live arms are SCOPE's own copy for Tasks 16, 28, 38 and 45.

std::wstring PhraseWordsLabel(RunMode mode) {
  return mode == RunMode::Live ? L"Your recovery phrase" : L"Demo words: not a recovery phrase";
}

std::wstring PhraseWordsCaveat(RunMode mode) {
  return mode == RunMode::Live ? L"These words cannot bring your account back yet."
                               : L"Demo model: these words bring nothing back. A real phrase "
                                 L"cannot bring your account back yet, either.";
}

std::wstring AccountIdNote(RunMode mode) {
  return mode == RunMode::Live ? L"One ID for your whole account, the same on every device."
                               : L"Demo model: a made-up ID, not an account.";
}

std::wstring RemovedTitle(RunMode mode) {
  return mode == RunMode::Live ? L"This device is no longer on your account"
                               : L"Demo screen: this device is no longer on your account";
}

std::wstring OnlyDeviceLine(RunMode mode) {
  return mode == RunMode::Live ? L"This ends this identity."
                               : L"Demo model: on an only device, this would end this identity.";
}

std::wstring SelfRemovalNotice(RunMode mode, std::wstring const& device) {
  const std::wstring body = device +
                            L" held your recovery phrase's key and removed itself. If this was "
                            L"not you, lock it out.";
  return mode == RunMode::Live ? body : L"Demo notice, made up: " + body;
}

std::wstring PhraseChangedLine(RunMode mode, std::wstring const& device) {
  if (mode == RunMode::Live)
    return L"Recovery phrase changed. Your ID and your devices are the same. " + device +
           L" no longer keeps your phrase's key: type the new phrase there to make it a phrase "
           L"device again.";
  return L"Demo model: no phrase was changed. After a real change, your ID and your devices are "
         L"the same, and " +
         device +
         L" no longer keeps your phrase's key: you would type the new phrase there to make it a "
         L"phrase device again.";
}

// ---- the gate ---------------------------------------------------------------------------------

std::vector<std::wstring> RunModeCopyDiagnostics() {
  std::vector<std::wstring> lines;

  const Pair pairs[] = {
      {L"chip", ModeChipText(RunMode::Fabricated), ModeChipText(RunMode::Live)},
      {L"lock title", LockHeaderTitle(RunMode::Fabricated), LockHeaderTitle(RunMode::Live)},
      {L"lock note", LockHeaderNote(RunMode::Fabricated), LockHeaderNote(RunMode::Live)},
      {L"composer", ComposerNote(RunMode::Fabricated, true), ComposerNote(RunMode::Live, true)},
      // The observer arm is a PAIR TOO, and it is gated as one: it is still a live string and a
      // fabricated string and they must still name their own modes.
      {L"composer (observer)", ComposerNote(RunMode::Fabricated, false),
       ComposerNote(RunMode::Live, false)},
      {L"relay title", RelayPathGroupTitle(RunMode::Fabricated, false),
       RelayPathGroupTitle(RunMode::Live, false)},
      {L"drawer name", RelayDrawerName(RunMode::Fabricated, false),
       RelayDrawerName(RunMode::Live, false)},
      {L"key name +", ServerKeyStateName(true, RunMode::Fabricated, false),
       ServerKeyStateName(true, RunMode::Live, false)},
      {L"key name -", ServerKeyStateName(false, RunMode::Fabricated, false),
       ServerKeyStateName(false, RunMode::Live, false)},
      {L"disclosure caption", DisclosureCaption(RunMode::Fabricated, false),
       DisclosureCaption(RunMode::Live, false)},
      {L"disclosure title", DisclosureTitle(RunMode::Fabricated, false),
       DisclosureTitle(RunMode::Live, false)},
      {L"disclosure body", DisclosureBody(RunMode::Fabricated, false),
       DisclosureBody(RunMode::Live, false)},
      {L"roster note", RosterNote(RunMode::Fabricated), RosterNote(RunMode::Live)},
  };

  size_t ok = 0;
  std::wstring firstBad = L"(none)";
  for (auto const& p : pairs) {
    if (PairOk(p)) {
      ++ok;
    } else if (firstBad == L"(none)") {
      firstBad = std::wstring(p.what);
    }
  }
  const size_t total = std::size(pairs);

  // THE COMPLEMENT IS PRINTED, not just the score: the two arms of one pair, in full, so a reader
  // of --diagnose can see what the gate was looking at. "composer" is the pair chosen because it
  // is the one whose live arm was written from scratch and the one a reviewer is most likely to
  // want to re-read.
  lines.push_back(std::format(
      L"  run mode copy    : {}  {}/{} pairs disjoint and mode-named (first bad: {})   [query: "
      L"for each pair, fabricated != live AND fabricated contains \"demo\" and not \"live\" AND "
      L"live contains \"live\" and not \"demo\", case-insensitive]",
      ok == total ? L"PASS" : L"FAIL", ok, total, firstBad));
  lines.push_back(std::format(
      L"  run mode composer: fabricated \"{}\" | live \"{}\" | live+observer \"{}\"",
      AsciiOnly(ComposerNote(RunMode::Fabricated, true)),
      AsciiOnly(ComposerNote(RunMode::Live, true)),
      AsciiOnly(ComposerNote(RunMode::Live, false))));
  lines.push_back(std::format(
      L"  run mode lock    : fabricated \"{}\" / \"{}\" | live \"{}\" / \"{}\"",
      AsciiOnly(LockHeaderTitle(RunMode::Fabricated)),
      AsciiOnly(LockHeaderNote(RunMode::Fabricated)),
      AsciiOnly(LockHeaderTitle(RunMode::Live)), AsciiOnly(LockHeaderNote(RunMode::Live))));

  // The dump's framing line is the one pair the property above deliberately does not cover, so it
  // gets its own clause rather than an exemption: both arms carry the fixture framing, they
  // differ, and only the live arm names the session it is NOT.
  const std::wstring dumpFab = WorldDumpFramingLine(RunMode::Fabricated);
  const std::wstring dumpLive = WorldDumpFramingLine(RunMode::Live);
  const bool dumpOk = dumpFab != dumpLive && dumpFab.starts_with(L"Demo model:") &&
                      dumpLive.starts_with(L"Demo model:") &&
                      !ContainsWord(dumpFab, L"live session") &&
                      ContainsWord(dumpLive, L"live session");
  lines.push_back(std::format(
      L"  run mode dump    : {}  both arms open \"Demo model:\" (the dump is the fixture in both "
      L"modes), they differ, and only the live arm names the live session -> \"{}\" | \"{}\"",
      dumpOk ? L"PASS" : L"FAIL", AsciiOnly(dumpFab), AsciiOnly(dumpLive)));

  // ---- the send clause, on the two strings that claim something about sending ----------------
  // BOTH ARMS ARE EVALUATED BY PASSING THE ENUM, exactly as every clause above does, so a
  // fabricated launch gates the live copy. That property is what makes this gate worth running at
  // all: the run that would notice a stale live string by looking at it is the run that is least
  // likely to happen.
  // THE CLAUSE IS PER STATE SINCE ITEM 242 R4, and the rewrite is the point rather than an
  // accident of adding a parameter. The old form was one boolean over two strings -
  // `!deniedByFabricated.empty() && deniedByLive.empty()` - and it BREAKS BY CONSTRUCTION the
  // moment a live composer can legitimately deny sending, because an observer's live note MUST
  // deny it. A gate that simply dropped the live half there would have stopped tracking the thing
  // it was built for. So each of the three states is asserted on its own property:
  //
  //   fabricated (either role) - MUST deny: the demo sends nothing and never could.
  //   live + may send          - MUST NOT deny: the button calls urnet_message_group_send.
  //   live + observer          - MUST deny (this client will not seal an application record for a
  //                              group it holds OBSERVER in) AND MUST NOT DENY THE SESSION. That
  //                              third clause is R3's pending-arm rule reused verbatim and it is
  //                              the one that catches the defect nobody looks for: an observer's
  //                              session is PROVABLY LIVE, since the role could only have been read
  //                              off an open group, so "there is no live session" is the one false
  //                              sentence this arm could carry.
  //
  // AND SINCE 2026-09-22 THE ROLE IS DRIVEN RATHER THAN LABELLED. Until this rewrite the clause
  // handed ComposerNote a literal `false` and printed the answer under the words
  // "live+observer" - a sentence about the (live, observer) mapping over a bool the gate had
  // chosen itself. The one production site that turns a ROLE into that bool is
  // MainWindow::OpenConversationMaySend; inverting it would have left every field on this line
  // unchanged while the app showed an observer the SENDING note, so the line printed a claim it
  // did not hold, which this project ranks below printing no claim at all. The bool now comes
  // from demo::RoleMaySend - the predicate that production site answers with - and the four role
  // spellings are walked with an unsourced one beside them, so "observer" here is the ROLE and
  // the complement (the roles that MAY send, and the placeholder that counts as one) is printed
  // next to it rather than assumed.
  //
  // THE EXPECTATION IS KEYED ON THE ROLE'S NAME AND NEVER ON THE PREDICATE'S OWN ANSWER. Keyed on
  // the answer, this loop would pass for ANY RoleMaySend, including one that let an observer
  // send: it would be asking the mapping to mark its own paper. Spec C section 5.6 names the
  // observer as the role that may not send, and that sentence is what `mustDeny` transcribes.
  //
  // WHAT IS STILL NOT DRIVEN, so the line claims no more than it holds: the
  // no-conversation-open default (OpenConversationMaySend answers "may send", because nothing has
  // read a role there) needs a window and no pure gate can reach it. This clause is the ROLE half
  // of that function.
  //
  // THE FABRICATED ARM IS HELD EQUAL ACROSS THE ROLES, which is ComposerNote's own decision made
  // into a property: what stops a send in the demo is that nothing is wired to a mesh, not the
  // role, so a fabricated arm that branched on the role would name the wrong cause.
  struct SendRoleCase {
    wchar_t const* role;
    wchar_t const* label;
  };
  const SendRoleCase sendRoles[] = {{demo::kRoleOwner, L"owner"},
                                    {demo::kRoleAdmin, L"admin"},
                                    {demo::kRoleMember, L"member"},
                                    {demo::kRoleObserver, L"observer"},
                                    {demo::kUnavailable, L"unsourced"}};
  size_t sendRolesOk = 0;
  std::wstring sendTable;
  std::wstring fabArm;
  bool fabSameForEveryRole = true;
  bool observerDeniesSession = false;
  for (auto const& r : sendRoles) {
    // THE MAPPING, RUN: role -> mayRoleSend, by the production predicate and not by this file.
    const bool maySend = demo::RoleMaySend(r.role);
    const std::wstring fab = ComposerNote(RunMode::Fabricated, maySend);
    const std::wstring live = ComposerNote(RunMode::Live, maySend);
    const std::wstring fabDenials = DenialsIn(fab);
    const std::wstring liveDenials = DenialsIn(live);
    const bool mustDeny = (r.role == std::wstring_view(demo::kRoleObserver));
    const bool deniesSession = ContainsWord(live, L"no live session");
    if (mustDeny) observerDeniesSession = deniesSession;
    if (!fabDenials.empty() && mustDeny == !liveDenials.empty() && !deniesSession) ++sendRolesOk;
    if (fabArm.empty())
      fabArm = fab;
    else if (fab != fabArm)
      fabSameForEveryRole = false;
    if (!sendTable.empty()) sendTable += L"; ";
    sendTable += std::format(L"{} {} [fab: {} | live: {}]", r.label,
                             maySend ? L"may send" : L"MAY NOT send",
                             fabDenials.empty() ? std::wstring(L"(none)") : fabDenials,
                             liveDenials.empty() ? std::wstring(L"(none)") : liveDenials);
  }
  const bool sendOk = sendRolesOk == std::size(sendRoles) && fabSameForEveryRole;
  lines.push_back(std::format(
      L"  run mode send    : {}  {}/{} roles, per (mode, ROLE) through demo::RoleMaySend -> {}; "
      L"the fabricated arm is one string for every role: {}; live+observer denies the SESSION: "
      L"{}   [query: case-insensitive substring over \"not wired\", \"nothing is sent\", \"no "
      L"message leaves\", \"cannot send\", \"not available\", \"does not send\", \"not send to "
      L"it\". For each of the four role spellings and an unsourced one, mayRoleSend is DERIVED by "
      L"demo::RoleMaySend - the predicate MainWindow::OpenConversationMaySend answers with - and "
      L"the note is asked for that answer: the fabricated arm must deny whatever the role (the "
      L"demo sends nothing and never could) and must be the SAME string for every role (what "
      L"stops a send there is not the role); the live arm must deny for the OBSERVER and for no "
      L"other role, the expectation being keyed on the role's NAME per Spec C section 5.6 and "
      L"never on the predicate's own answer; and no live arm may contain \"no live session\", "
      L"because an observer's session is provably live. NOT driven here: "
      L"OpenConversationMaySend's no-conversation-open default, which needs a window]",
      sendOk ? L"PASS" : L"FAIL", sendRolesOk, std::size(sendRoles), sendTable,
      fabSameForEveryRole ? L"yes" : L"NO", observerDeniesSession ? L"YES (wrong)" : L"no"));

  // The disclosure's absence list is the SECOND string this commit made false, and it is checked as
  // a list rather than as a paragraph. THE LIST IS PRINTED IN FULL, which is the whole point: a
  // reader sees what the app claims it cannot do and can check it against the app, rather than
  // being told a count of things it agreed with itself about.
  // AND, SINCE ITEM 242 R3, THE ROSTER: the list must not say the app lacks the member list or
  // the roles it now reads off the group. "names behind members" is allowed - it is the thing
  // that IS still absent - so the words gated are "member list" and "role", not "member".
  const std::wstring absences = AbsenceList(DisclosureBody(RunMode::Live, false));
  const bool absenceOk = !absences.empty() && !ContainsWord(absences, L"send") &&
                         !ContainsWord(absences, L"member list") &&
                         !ContainsWord(absences, L"role");
  lines.push_back(std::format(
      L"  run mode absences: {}  the live disclosure's absence list must name neither the send "
      L"path nor the roster -> \"{}\"   [query: the text between \"What is NOT here:\" and the "
      L"next full stop, which must be non-empty and must contain none of \"send\", \"member "
      L"list\", \"role\"]",
      absenceOk ? L"PASS" : L"FAIL", AsciiOnly(absences)));

  // And the latch itself. A launch with no live world must report Fabricated: if this ever prints
  // "live" on a default launch, every string above is being chosen by the wrong arm and no other
  // gate here would notice, because every other gate passes the enum explicitly.
  const bool latchOk = ActiveRunMode() == RunMode::Fabricated;
  lines.push_back(std::format(
      L"  run mode latch   : {}  ActiveRunMode() at diagnose time is {} (diagnostics run before "
      L"any world is published, so fabricated is the only correct answer)",
      latchOk ? L"PASS" : L"FAIL",
      ActiveRunMode() == RunMode::Live ? L"live" : L"fabricated"));

  // THE THIRD ARM, WHICH THE PAIR GATE ABOVE CANNOT SEE. Every check in this function compares a
  // FABRICATED wording against a LIVE one, because for its whole life this file had exactly two
  // worlds. A --live launch drawing the EMPTY world is a third, and the sentence it needs is wrong
  // in both of the other two ways at once: it must not say "Demo model" (nothing is fabricated -
  // the list is empty) and it must not claim a live session (there is no group).
  //
  // ASSERTED IN FOUR DIRECTIONS, because fewer would pass a sentence that is simply one of the
  // other two copied: it differs from BOTH arms, it is the same whatever the role (the absence of
  // a group outranks what a role would permit), it names neither mode by name, and it is not
  // empty. AND THE CONTROL FIRES FOR ITS OWN REASON: the same function with noSession false still
  // answers the two paired wordings the gate above checks, so a third arm that had swallowed them
  // is caught here rather than silently passing both gates.
  {
    const std::wstring none = ComposerNote(RunMode::Fabricated, true, true);
    const std::wstring noneObserver = ComposerNote(RunMode::Live, false, true);
    const std::wstring fab = ComposerNote(RunMode::Fabricated, true);
    const std::wstring live = ComposerNote(RunMode::Live, true);
    const bool differs = !none.empty() && none != fab && none != live;
    const bool roleBlind = none == noneObserver;
    const bool claimsNothing = none.find(L"Demo model") == std::wstring::npos &&
                               none.find(L"Live session") == std::wstring::npos;
    const bool controlHolds = fab != live && fab.find(L"Demo model") != std::wstring::npos &&
                              live.find(L"Live session") != std::wstring::npos;
    const bool ok = differs && roleBlind && claimsNothing && controlHolds;
    lines.push_back(std::format(
        L"  run mode no-sess : {}  \"{}\" | differs from both arms {}, role-blind {}, names "
        L"neither mode {}, control (noSession=false still gives the paired wordings) {}   [query: "
        L"a --live launch drawing the EMPTY world is a third state; its sentence must differ from "
        L"the fabricated AND the live wording, must be the same for an observer, and must contain "
        L"neither \"Demo model\" nor \"Live session\"]",
        ok ? L"PASS" : L"FAIL", none, differs ? L"yes" : L"NO", roleBlind ? L"yes" : L"NO",
        claimsNothing ? L"yes" : L"NO", controlHolds ? L"holds" : L"FAILED"));
  }

  // ---- THE IDENTITY PAIRS (identity phase 1, Task 6) ------------------------------------------
  // Their own clause, because their live arms name the ACCOUNT's thing and not the session (see
  // RunMode.h): the fabricated arm must name the demo and not "live", the live arm must not name
  // the demo, and they must differ. The device names passed in are the fixture's kind of name, so
  // a name cannot be what supplies or removes the word.
  {
    const Pair identityPairs[] = {
        {L"words label", PhraseWordsLabel(RunMode::Fabricated), PhraseWordsLabel(RunMode::Live)},
        {L"words caveat", PhraseWordsCaveat(RunMode::Fabricated),
         PhraseWordsCaveat(RunMode::Live)},
        {L"your id note", AccountIdNote(RunMode::Fabricated), AccountIdNote(RunMode::Live)},
        {L"removed title", RemovedTitle(RunMode::Fabricated), RemovedTitle(RunMode::Live)},
        {L"only device", OnlyDeviceLine(RunMode::Fabricated), OnlyDeviceLine(RunMode::Live)},
        {L"self-removal notice", SelfRemovalNotice(RunMode::Fabricated, L"Old laptop"),
         SelfRemovalNotice(RunMode::Live, L"Old laptop")},
        {L"phrase changed", PhraseChangedLine(RunMode::Fabricated, L"Linux laptop"),
         PhraseChangedLine(RunMode::Live, L"Linux laptop")},
    };
    size_t idOk = 0;
    std::wstring idFirstBad = L"(none)";
    for (auto const& p : identityPairs) {
      const bool ok = !p.fabricated.empty() && !p.live.empty() && p.fabricated != p.live &&
                      ContainsWord(p.fabricated, L"demo") && !ContainsWord(p.fabricated, L"live") &&
                      !ContainsWord(p.live, L"demo");
      if (ok) {
        ++idOk;
      } else if (idFirstBad == L"(none)") {
        idFirstBad = std::wstring(p.what);
      }
    }
    lines.push_back(std::format(
        L"  run mode identity: {}  {}/{} identity pairs: the fabricated arm names the demo, the "
        L"live arm does not, and they differ (first bad: {})   [query: for each pair, fabricated "
        L"!= live AND fabricated contains \"demo\" and not \"live\" AND live does not contain "
        L"\"demo\", case-insensitive. The live arms are SCOPE's copy for the live screens of Tasks "
        L"16, 28, 38 and 45, none of which this build draws]",
        idOk == std::size(identityPairs) ? L"PASS" : L"FAIL", idOk, std::size(identityPairs),
        idFirstBad));

    // THE WORDS LABEL, ASKED FOR THE DENIAL ITSELF. A label that named the demo and still called
    // the words a phrase ("Demo: your recovery phrase") would pass the pair test above and be false
    // in exactly the way Task 6 exists to prevent, so this line asks for the words "not a recovery
    // phrase" in the fabricated arm, and for their absence in the live one.
    const std::wstring fab = PhraseWordsLabel(RunMode::Fabricated);
    const std::wstring live = PhraseWordsLabel(RunMode::Live);
    const bool wordsOk = ContainsWord(fab, L"demo words") &&
                         ContainsWord(fab, L"not a recovery phrase") &&
                         !ContainsWord(live, L"not a recovery phrase") &&
                         !ContainsWord(live, L"demo");
    lines.push_back(std::format(
        L"  run mode words   : {}  the words label: fabricated \"{}\" | live \"{}\"   [query: the "
        L"fabricated arm contains \"demo words\" and \"not a recovery phrase\"; the live arm "
        L"contains neither \"not a recovery phrase\" nor \"demo\"]",
        wordsOk ? L"PASS" : L"FAIL", AsciiOnly(fab), AsciiOnly(live)));
  }

  // THE OTHER THIRD ARMS (fix/plain-launch-run-mode), held to the composer's property: a plain
  // launch draws the EMPTY world with ActiveRunMode() still Fabricated, and every string it draws
  // there is chosen by DrawnRunMode() and DrawnNoSession() - the live mode and the no-session bit.
  // Each third arm must differ from BOTH of its function's arms, name neither mode ("demo", "live
  // session"), win over the mode (the same string whichever mode is passed beside it), and not be
  // empty; the paired arms must still name their modes, so a third arm that swallowed them is
  // caught; and the disclosure's must keep the live absence list WORD FOR WORD. Every third arm is
  // printed, in full: the complement of what the window draws in the empty world.
  {
    struct Third {
      std::wstring_view what;
      std::wstring fabricated, live, thirdLive, thirdFab;
    };
    const Third thirds[] = {
        {L"relay title", RelayPathGroupTitle(RunMode::Fabricated, false),
         RelayPathGroupTitle(RunMode::Live, false), RelayPathGroupTitle(RunMode::Live, true),
         RelayPathGroupTitle(RunMode::Fabricated, true)},
        {L"drawer name", RelayDrawerName(RunMode::Fabricated, false),
         RelayDrawerName(RunMode::Live, false), RelayDrawerName(RunMode::Live, true),
         RelayDrawerName(RunMode::Fabricated, true)},
        {L"key name +", ServerKeyStateName(true, RunMode::Fabricated, false),
         ServerKeyStateName(true, RunMode::Live, false), ServerKeyStateName(true, RunMode::Live, true),
         ServerKeyStateName(true, RunMode::Fabricated, true)},
        {L"key name -", ServerKeyStateName(false, RunMode::Fabricated, false),
         ServerKeyStateName(false, RunMode::Live, false),
         ServerKeyStateName(false, RunMode::Live, true),
         ServerKeyStateName(false, RunMode::Fabricated, true)},
        {L"disclosure caption", DisclosureCaption(RunMode::Fabricated, false),
         DisclosureCaption(RunMode::Live, false), DisclosureCaption(RunMode::Live, true),
         DisclosureCaption(RunMode::Fabricated, true)},
        {L"disclosure title", DisclosureTitle(RunMode::Fabricated, false),
         DisclosureTitle(RunMode::Live, false), DisclosureTitle(RunMode::Live, true),
         DisclosureTitle(RunMode::Fabricated, true)},
        {L"disclosure body", DisclosureBody(RunMode::Fabricated, false),
         DisclosureBody(RunMode::Live, false), DisclosureBody(RunMode::Live, true),
         DisclosureBody(RunMode::Fabricated, true)},
    };
    size_t good = 0;
    std::wstring firstBad = L"(none)", printed;
    for (auto const& t : thirds) {
      const bool ok = !t.thirdLive.empty() && t.thirdLive == t.thirdFab &&
                      t.thirdLive != t.fabricated && t.thirdLive != t.live &&
                      !ContainsWord(t.thirdLive, L"demo") &&
                      !ContainsWord(t.thirdLive, L"live session") &&
                      ContainsWord(t.fabricated, L"demo") &&
                      ContainsWord(t.live, L"live session");
      if (ok)
        ++good;
      else if (firstBad == L"(none)")
        firstBad = std::wstring(t.what);
      if (!printed.empty()) printed += L" | ";
      printed += std::wstring(t.what) + L" \"" + AsciiOnly(t.thirdLive) + L"\"";
    }
    const std::wstring liveAbsences = AbsenceList(DisclosureBody(RunMode::Live, false));
    const std::wstring thirdAbsences = AbsenceList(DisclosureBody(RunMode::Live, true));
    const bool absencesKept = !liveAbsences.empty() && thirdAbsences == liveAbsences;
    const bool ok = good == std::size(thirds) && absencesKept;
    lines.push_back(std::format(
        L"  run mode thirds  : {}  {}/{} third arms differ from both arms, name neither mode, win "
        L"over the mode, and leave the paired arms naming theirs (first bad: {}); the disclosure "
        L"keeps the live absence list word for word: {}; the empty world draws: {}   [query: for "
        L"each function with a noSession arm, f(Live, true) == f(Fabricated, true), it differs "
        L"from f(Fabricated) and f(Live), contains neither \"demo\" nor \"live session\" "
        L"(case-insensitive), f(Fabricated) contains \"demo\" and f(Live) \"live session\"; "
        L"AbsenceList(body(Live, true)) == AbsenceList(body(Live))]",
        ok ? L"PASS" : L"FAIL", good, std::size(thirds), firstBad, absencesKept ? L"yes" : L"NO",
        printed));
  }

  // THE LAUNCH TABLE: which arm each (launch, latch) pair takes. Written out from RunMode.h's
  // three rules and not from the functions, so the functions cannot mark their own paper; the
  // fourth row is the one a plain launch sits in for as long as its mesh does not answer.
  {
    struct Row {
      bool launch;
      RunMode latch;
      RunMode wantMode;
      bool wantNoSession;
      wchar_t const* label;
    };
    const Row rows[] = {
        {true, RunMode::Fabricated, RunMode::Fabricated, false, L"fabricated launch, no live world"},
        {true, RunMode::Live, RunMode::Live, false, L"fabricated launch, live world"},
        {false, RunMode::Live, RunMode::Live, false, L"plain launch, live world"},
        {false, RunMode::Fabricated, RunMode::Live, true, L"plain launch, no live world"},
    };
    size_t right = 0;
    std::wstring table;
    for (auto const& r : rows) {
      const RunMode m = DrawnRunModeFor(r.launch, r.latch);
      const bool n = NoSessionFor(r.launch, r.latch);
      right += (m == r.wantMode && n == r.wantNoSession) ? 1 : 0;
      if (!table.empty()) table += L"; ";
      table += std::format(L"{} -> {}{}", r.label, m == RunMode::Live ? L"live" : L"fabricated",
                           n ? L", no session" : L"");
    }
    lines.push_back(std::format(
        L"  run mode launch  : {}  {}/{} (launch, latch) pairs take the arm RunMode.h names: {}   "
        L"[query: DrawnRunModeFor and NoSessionFor against a table written from the three rules: "
        L"fabricated only for a fabricated launch with no live world; no session only for any "
        L"other launch with no live world]",
        right == std::size(rows) ? L"PASS" : L"FAIL", right, std::size(rows), table));
  }

  return lines;
}

}  // namespace urmsg
