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

namespace urmsg {
namespace {

// Fabricated until the window says otherwise. Relaxed is the right order: the only writer is the
// UI thread in MainWindow::ApplyLiveWorld and every reader that matters is that same thread
// building views a few statements later. The atomic is here so that a stray read from the live
// worker (a log line, a future probe) is defined behaviour rather than a data race.
std::atomic<int> g_mode{static_cast<int>(RunMode::Fabricated)};

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
//   2. A pair whose live arm is the ONE placeholder (AttestationLabel, FormatKeyState) cannot
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

}  // namespace

RunMode ActiveRunMode() { return static_cast<RunMode>(g_mode.load(std::memory_order_relaxed)); }

void SetActiveRunMode(RunMode mode) {
  g_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

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

std::wstring ComposerNote(RunMode mode) {
  // U+2014 EM DASH, written as an escape and never as a pasted character (the house non-ASCII
  // rule: an editing pass that silently re-encodes a pasted glyph leaves no build error behind,
  // only a wrong byte in a string).
  //
  // The live form does NOT say "nothing is sent, and no message leaves this window". That sentence
  // is about isolation, and printing it under a thread whose newest line arrived from another
  // machine reads as a claim that the app is inert — which is the false denial in miniature. What
  // is true of the send path in both modes is that it does not exist yet, so the live form says
  // that and affirms what the reader can already see.
  return mode == RunMode::Live
             ? L"Live session \u2014 these messages are real; sending is not wired up yet."
             : L"Demo model \u2014 nothing is sent, and no message leaves this window.";
}

std::wstring RelayPathGroupTitle(RunMode mode) {
  return mode == RunMode::Live ? L"LIVE SESSION: RELAY PATH" : L"DEMO MODEL: RELAY PATH";
}

std::wstring RelayDrawerName(RunMode mode) {
  // "preview" drops in live mode and nothing replaces it: the three nodes there are the platform
  // url the library actually dialled and the message server's own client_id (Live/LiveWorld.cpp),
  // so the drawer is showing a path rather than previewing what one would look like.
  return mode == RunMode::Live ? L"Live session: relay path" : L"Demo model: relay path preview";
}

std::wstring ServerKeyStateName(bool keyVerified, RunMode mode) {
  // ONE string for both arms in live mode, and that is the point rather than a shortcut. The live
  // world hard-codes keyVerified = false (Live/LiveWorld.cpp:352) because this build pins no
  // server key — so "not verified" would report the RESULT of a check that never ran. "pinning
  // unavailable" reports that there is no result, which is the true thing and the same thing the
  // rail's Attestation row says with the one placeholder.
  if (mode == RunMode::Live) return L"Live session: server key pinning unavailable";
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

std::wstring DisclosureCaption(RunMode mode) {
  return mode == RunMode::Live ? L"LIVE SESSION" : L"THIS DEMO";
}

std::wstring DisclosureTitle(RunMode mode) {
  return mode == RunMode::Live ? L"What this live session does not do"
                               : L"What this demo does not do";
}

std::wstring DisclosureBody(RunMode mode) {
  // The fabricated paragraph is the one that shipped, with the mode prefix added so it is framed
  // the way every other string here is framed. Not one word of its content changed.
  //
  // The live paragraph is the same SHAPE — what is real, then the complete list of what is absent,
  // then how the absences are drawn — because that shape is what makes the disclosure checkable
  // against the app instead of reassuring. The absence list is the ABI's own
  // (urnetwork_message.h: "WHAT IS STILL NOT HERE: receipts, edit, media, group names, contact
  // discovery, a third member") plus the two this CLIENT is missing: a wired send button and any
  // attestation check.
  if (mode == RunMode::Live) {
    return L"Live session \u2014 the messages on these screens are real: this device fetched the "
           L"records from the message server and opened them under MLS. What is NOT here: "
           L"sending, delivery and read receipts, contact discovery, member lists, group names, "
           L"and any per-message attestation check. Every field this build has no source for "
           L"reads \"unavailable\" rather than a guess.";
  }
  return L"Demo model \u2014 no protocol, no store, no network and no cryptography are running. "
         L"Every value on these screens is fabricated in one module. Nothing has been sent, "
         L"received, stored, encrypted or decrypted. The inspector shows what URmessage will one "
         L"day say about a real message; it is not a statement about one.";
}

// ---- the gate ---------------------------------------------------------------------------------

std::vector<std::wstring> RunModeCopyDiagnostics() {
  std::vector<std::wstring> lines;

  const Pair pairs[] = {
      {L"chip", ModeChipText(RunMode::Fabricated), ModeChipText(RunMode::Live)},
      {L"lock title", LockHeaderTitle(RunMode::Fabricated), LockHeaderTitle(RunMode::Live)},
      {L"lock note", LockHeaderNote(RunMode::Fabricated), LockHeaderNote(RunMode::Live)},
      {L"composer", ComposerNote(RunMode::Fabricated), ComposerNote(RunMode::Live)},
      {L"relay title", RelayPathGroupTitle(RunMode::Fabricated),
       RelayPathGroupTitle(RunMode::Live)},
      {L"drawer name", RelayDrawerName(RunMode::Fabricated), RelayDrawerName(RunMode::Live)},
      {L"key name +", ServerKeyStateName(true, RunMode::Fabricated),
       ServerKeyStateName(true, RunMode::Live)},
      {L"key name -", ServerKeyStateName(false, RunMode::Fabricated),
       ServerKeyStateName(false, RunMode::Live)},
      {L"disclosure caption", DisclosureCaption(RunMode::Fabricated),
       DisclosureCaption(RunMode::Live)},
      {L"disclosure title", DisclosureTitle(RunMode::Fabricated), DisclosureTitle(RunMode::Live)},
      {L"disclosure body", DisclosureBody(RunMode::Fabricated), DisclosureBody(RunMode::Live)},
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
      L"  run mode composer: fabricated \"{}\" | live \"{}\"",
      AsciiOnly(ComposerNote(RunMode::Fabricated)), AsciiOnly(ComposerNote(RunMode::Live))));
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

  // And the latch itself. A launch with no live world must report Fabricated: if this ever prints
  // "live" on a default launch, every string above is being chosen by the wrong arm and no other
  // gate here would notice, because every other gate passes the enum explicitly.
  const bool latchOk = ActiveRunMode() == RunMode::Fabricated;
  lines.push_back(std::format(
      L"  run mode latch   : {}  ActiveRunMode() at diagnose time is {} (diagnostics run before "
      L"any world is published, so fabricated is the only correct answer)",
      latchOk ? L"PASS" : L"FAIL",
      ActiveRunMode() == RunMode::Live ? L"live" : L"fabricated"));

  return lines;
}

}  // namespace urmsg
