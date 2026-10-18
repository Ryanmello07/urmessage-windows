// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Startup.h"

#include <shellapi.h>  // CommandLineToArgvW
#include <dwrite.h>    // the icon font's glyphs, asked of this machine (IconGlyphsLine)
#pragma comment(lib, "dwrite.lib")

#include <algorithm>  // sort/unique, for the T5 badge-table assertion
#include <array>
#include <atomic>
#include <iterator>
#include <vector>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string_view>

#include "AppPrefs.h"
#include "Ids.h"
#include "Identicon.h"
#include "Localization.h"
#include "Log.h"
#include "Paths.h"
#include "Strings.h"
#include "UrComponents.h"  // kIconFontFamily and kIconGlyphs, for the icon glyphs line
#include "UrMotion.h"
#include "Views/ConversationRowModel.h"
#include "RunMode.h"  // RunModeCopyDiagnostics - the mode-dependent honesty copy's gate
#include "Views/DeveloperDump.h"
#include "Views/InspectRailFields.h"
#include "Views/RosterRules.h"  // the roster's pure table, for the roster lines below
#include "Views/StatusStripRules.h"
#include "Demo/DemoShellState.h"
#include "Demo/AdvancedMode.h"
#include "Demo/DemoAutoplay.h"
#include "Demo/DeveloperSwitches.h"
#include "Demo/DemoWorld.h"
#include "Demo/ThreadLayout.h"
#include "Views/ThreadLayout.h"
#include "Views/ThreadView.h"  // ShouldPinToBottom, for the scroll-pin line below
#include "Live/ConversationRules.h"  // the conversation list's pure rules (pure)
#include "Live/LiveWorld.h"    // BuildWorld, for the reply/reaction mapping gate below (pure)
#include "Live/LocalNames.h"   // the labels gate below (pure)
#include "Live/HeldGroupRules.h"  // the held-group lines below (pure; Task 39)
#include "Live/RefusalCopy.h"     // the refusal copy lines below (pure; Task 58)
#include "Demo/DemoSwitches.h"
#include "Demo/DemoStress.h"

// The Windows App SDK version this binary was BUILT against, injected from the
// single MSBuild property that also drives the PackageReference (App.vcxproj),
// so the two cannot drift. Printed next to the runtime actually loaded: a
// major.minor mismatch is then one line to read instead of an invisible
// incompatibility.
//
// It arrives as a BARE token (2.2.0) and is stringized here — an MSBuild
// PreprocessorDefinition cannot carry a `\"`-escaped literal through to cl:
// verified in the VPN repo against msbuild locally, where the value ends at the
// `L` and the TU dies with C2065 'L': undeclared identifier.
#define URM_STR2(x) #x
#define URM_STR(x) URM_STR2(x)
#if defined(URM_WINDOWSAPPSDK_VERSION_RAW)
#define URM_WINDOWSAPPSDK_VERSION URM_STR(URM_WINDOWSAPPSDK_VERSION_RAW)
#else
#define URM_WINDOWSAPPSDK_VERSION "(not injected by the build)"
#endif

namespace urnw {
namespace {

// The Windows App Runtime's own dll. An unpackaged app (WindowsPackageType=None)
// reaches it through the bootstrapper, which adds the framework package
// directory to the process dll search path; so "can this name be resolved" is
// the same question as "did the bootstrapper find a runtime", and the resolved
// path carries the version.
//
// NOTE for this app: WindowsAppSDKSelfContained=true, so the runtime binaries
// are copied NEXT TO THE EXE and there is no machine-wide framework package to
// find. The probe still answers the useful question — which copy is loaded.
constexpr wchar_t kAppRuntimeDll[] = L"Microsoft.WindowsAppRuntime.dll";

// Shipped next to the exe by the Windows App SDK targets; the auto-initializer
// imports it, so a missing one means the process never starts at all.
constexpr wchar_t kBootstrapDll[] = L"Microsoft.WindowsAppRuntime.Bootstrap.dll";

// Whether StartupLogInit got the log file open. Unset until it has run, so the
// diagnostics never claim anything about a log nobody tried to open yet.
std::optional<bool> g_logOpened;

// Written on the UI thread in OnLaunched, read on the same thread after the
// message loop ends; atomic anyway, because a flag that decides whether the
// owner gets told anything is not the place to be clever.
std::atomic<bool> g_launched{false};

// Set by FailVisible, which can fire from the UI thread or the XAML unhandled-
// exception handler.
std::atomic<bool> g_failed{false};

std::filesystem::path ExePath() {
  wchar_t path[MAX_PATH]{};
  const DWORD n = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path(std::wstring(path, n));
}

// "present (12345 bytes)" / "MISSING". Never throws: file_size takes the
// error_code overload, because a diagnostic that dies while diagnosing is worse
// than no diagnostic.
std::wstring Presence(const std::filesystem::path& file) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec) return L"MISSING";
  return std::format(L"present ({} bytes)", size);
}

// Which copy of a dll this process actually has loaded, when there may be more
// than one drop on disk.
//
// "not loaded" is NOT an error here and the wording must not imply that it is.
// This app is WindowsAppSDKSelfContained, so the bootstrap dll ships next to the
// exe but is never imported: the self-contained initializer resolves the
// app-local runtime directly instead of going through the bootstrapper. A
// diagnostic that cries wolf on the normal case teaches the reader to skip it,
// which is how a real signal gets missed.
std::wstring LoadedModule(const wchar_t* name, const std::filesystem::path& beside) {
  if (HMODULE mod = ::GetModuleHandleW(name)) {
    wchar_t path[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(mod, path, MAX_PATH);
    if (n) return L"loaded from " + std::wstring(path, n);
    return L"loaded (path unavailable)";
  }
  return std::format(
      L"not loaded (expected: this build is self-contained) — on disk: {}",
      Presence(beside));
}

std::wstring OsVersion() {
  // GetVersionExW lies to a manifest that does not opt in; RtlGetVersion does
  // not. It is ntdll's, so it is resolved dynamically.
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
  auto rtlGetVersion =
      ntdll ? reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"))
            : nullptr;
  if (!rtlGetVersion) return L"unknown";
  RTL_OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = static_cast<ULONG>(sizeof(info));
  if (rtlGetVersion(&info) != 0) return L"unknown";
  return std::format(L"{}.{}.{}", info.dwMajorVersion, info.dwMinorVersion,
                     info.dwBuildNumber);
}

std::wstring AppRuntimeProbe() {
  bool loadedHere = false;
  HMODULE runtime = ::GetModuleHandleW(kAppRuntimeDll);
  if (!runtime) {
    runtime = ::LoadLibraryExW(kAppRuntimeDll, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    loadedHere = (runtime != nullptr);
  }
  if (!runtime) {
    return std::format(
        L"NOT FOUND (LoadLibrary error {}) — the Windows App Runtime could not "
        L"be resolved; the app cannot start without it",
        ::GetLastError());
  }
  wchar_t path[MAX_PATH]{};
  const DWORD n = ::GetModuleFileNameW(runtime, path, MAX_PATH);
  std::wstring resolved(path, n);
  if (loadedHere) ::FreeLibrary(runtime);
  return resolved.empty() ? std::wstring(L"present") : L"present: " + resolved;
}

// stdout for a /SUBSYSTEM:WINDOWS process: a console handle takes WriteConsoleW
// (wide, so non-ascii paths survive), a redirected one (`--diagnose > out.txt`,
// or a pipe into PowerShell) takes utf-8 bytes. False when there is nothing to
// write to, which is the caller's cue to use a message box instead.
bool WriteStdout(HANDLE out, std::wstring_view text) {
  if (out == nullptr || out == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  DWORD mode = 0;
  if (::GetConsoleMode(out, &mode)) {
    return ::WriteConsoleW(out, text.data(), static_cast<DWORD>(text.size()), &written,
                           nullptr) != FALSE;
  }
  const std::string utf8 = Narrow(text);
  return ::WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                     nullptr) != FALSE;
}

// ---- the demo world's ten invariants (contract section 1.1) ----------------
//
// These run on EVERY launch, not only under --diagnose: CollectDiagnostics is
// called at main.cpp:168 and LogDiagnostics writes every line to the log, so a
// world that broke overnight leaves evidence in the log of the run that broke
// it rather than waiting for someone to think to ask.

void MixU64(uint64_t& h, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h ^= (v >> (i * 8)) & 0xFFull;
    h *= 1099511628211ull;
  }
}
void MixStr(uint64_t& h, std::wstring const& s) {
  MixU64(h, static_cast<uint64_t>(s.size()));
  for (wchar_t c : s) MixU64(h, static_cast<uint64_t>(static_cast<uint16_t>(c)));
}
void MixSeed(uint64_t& h, urmsg::demo::Seed const& s) {
  for (uint8_t b : s) MixU64(h, static_cast<uint64_t>(b));
}

uint64_t WorldFingerprint(urmsg::demo::World const& w) {
  uint64_t h = 1469598103934665603ull;
  auto mixDevice = [&h](urmsg::demo::DeviceRef const& d) {
    MixStr(h, d.id); MixStr(h, d.name); MixStr(h, d.ownerName);
    MixSeed(h, d.ownerKey); MixStr(h, d.lastSeenLabel);
    MixU64(h, d.online ? 1u : 0u); MixU64(h, d.isThisComputer ? 1u : 0u);
  };
  for (auto const& c : w.conversations) {
    MixStr(h, c.id); MixU64(h, static_cast<uint64_t>(c.kind)); MixStr(h, c.name);
    MixSeed(h, c.identityKey); MixStr(h, c.groupIdHex); MixStr(h, c.preview);
    MixStr(h, c.timeLabel); MixU64(h, static_cast<uint64_t>(c.unread));
    MixU64(h, c.muted ? 1u : 0u); MixU64(h, c.disappearing ? 1u : 0u);
    MixU64(h, static_cast<uint64_t>(c.memberCount));
    MixStr(h, c.retentionLabel); MixStr(h, c.mediaRetentionLabel);
    // The viewer's role and each member's role, mine bit and identity key (item 242 R3): the
    // fixture's roles are fabricated by name in DemoWorld.cpp, so a role that drifts there moves
    // this fingerprint exactly as a renamed member would. The old `admin` bit these replace was
    // a one-bit projection of the same fact.
    MixStr(h, c.myRole);
    for (auto const& m : c.members) {
      MixStr(h, m.id); MixStr(h, m.displayName); MixSeed(h, m.identityKey);
      MixStr(h, m.role); MixU64(h, m.mine ? 1u : 0u); MixStr(h, m.identityPubHex);
      for (auto const& d : m.devices) mixDevice(d);
    }
    for (auto const& r : c.rows) {
      MixU64(h, static_cast<uint64_t>(r.kind)); MixStr(h, r.id);
      MixStr(h, r.senderName); MixSeed(h, r.senderKey); MixStr(h, r.body);
      MixStr(h, r.timeLabel); MixU64(h, r.outgoing ? 1u : 0u);
      MixU64(h, static_cast<uint64_t>(r.state)); MixStr(h, r.failureReason);
      MixStr(h, r.systemText); MixU64(h, r.permanentRecord ? 1u : 0u);
      // The role the row's sender held at its sealing epoch (item 242 R4). It
      // decides whether the row draws as a bubble or collapses to Spec C
      // section 5.1's hidden-observer line, so a drift in it is a drift in what
      // the thread SHOWS and must move this fingerprint.
      MixStr(h, r.senderRoleAtSend);
      auto const& n = r.inspect;
      MixU64(h, n.epoch); MixU64(h, n.senderLeafIndex);
      MixU64(h, static_cast<uint64_t>(n.retention)); MixStr(h, n.sizeBucket);
      MixU64(h, n.wireSizeBytes); MixU64(h, n.attestationVerified ? 1u : 0u);
      MixStr(h, n.cipher); MixStr(h, n.groupIdHex); MixStr(h, n.senderDisplayName);
      MixStr(h, n.sentAtLabel); MixStr(h, n.receivedAtLabel);
      for (auto const& d : n.deliveredTo) mixDevice(d);
      for (auto const& d : n.readBy) mixDevice(d);
    }
  }
  for (auto const& d : w.myDevices) mixDevice(d);
  for (auto const& n : w.relayPath) {
    MixStr(h, n.label); MixStr(h, n.subLabel); MixStr(h, n.glyph);
    MixU64(h, static_cast<uint64_t>(n.hopMs)); MixU64(h, n.healthy ? 1u : 0u);
  }
  MixStr(h, w.server.host); MixStr(h, w.server.jurisdiction);
  MixU64(h, static_cast<uint64_t>(w.server.latencyMs));
  MixU64(h, w.server.keyVerified ? 1u : 0u);
  MixU64(h, w.currentEpoch); MixU64(h, static_cast<uint64_t>(w.connectState));
  MixStr(h, w.sessionMode); MixU64(h, static_cast<uint64_t>(w.recordsPerSecond));
  return h;
}

// Filled in by Step 4, from the value the first run prints. A task that
// deliberately changes the world updates this in the SAME commit; anything
// else that changes it is the bug this line exists to catch.
//
// 0x97B1C149D13010C3 -> 0x8C52DBB3652A9BD3 with item 242 R3 (the role model
// on the roster): MemberRef::admin became MemberRef::role, and the fingerprint
// now mixes each member's role word, mine bit and identity key and each
// conversation's myRole. The fixture's roles are fabricated by name in
// DemoWorld.cpp (Mira owns Design team, the viewer owns URnetwork core), so
// the world's BYTES changed and the constant moved with them, in one commit.
//
// -> 0x3323AD211AADAB9F with item 242 R4 (OBSERVER read-only). Five deliberate
// changes to the fixture, all in one conversation: MessageRow gained
// senderRoleAtSend and this hash mixes it; Elena Vasquez is c0's OBSERVER
// rather than a member; her 12:02 line carries an explicit "member" as the
// collapse rule's positive control; and a new 12:05 line carries "observer" and
// is the row that collapses; and a fifth, role-less line of hers at 12:07 sits
// directly under it as the RUN-BREAK CONTROL — without an incoming row by the
// same sender there, the substitution that makes a collapsed row break a run
// changed no pixel and a mutant that deleted it survived every gate. Inserted
// BEFORE c0's last two rows so
// kInspectTargetRowId still names an outgoing Read row (c0-r12) and c0's
// preview is still its last line.
constexpr uint64_t kExpectedWorldFingerprint = 0x3323AD211AADAB9Full;

std::wstring Verdict(bool ok) { return ok ? L"PASS" : L"FAIL"; }

std::vector<std::wstring> DemoWorldAssertions() {
  using namespace urmsg::demo;
  World const& w = GetWorld();
  std::vector<std::wstring> out;

  size_t rows = 0, members = 0, groups = 0, direct = 0;
  for (auto const& c : w.conversations) {
    rows += c.rows.size();
    members += c.members.size();
    (c.kind == ConversationKind::Group ? groups : direct) += 1;
  }
  out.push_back(std::format(
      L"  demo world       : {} conversations, {} rows, {} members, {} devices",
      w.conversations.size(), rows, members, w.myDevices.size()));

  // I1
  const bool i1 = (w.conversations.size() == 8 && groups == 2);
  out.push_back(std::format(L"    I1  conversation count   {}  {} total, {} group, {} direct",
                            Verdict(i1), w.conversations.size(), groups, direct));
  // I2
  size_t i2Agree = 0;
  for (auto const& c : w.conversations)
    if (c.memberCount == static_cast<int>(c.members.size())) ++i2Agree;
  out.push_back(std::format(L"    I2  memberCount agrees   {}  {}/{} conversations, {} members counted",
                            Verdict(i2Agree == w.conversations.size()), i2Agree,
                            w.conversations.size(), members));
  // I3 - the timer REPLACES a preview that exists, so the rule is falsifiable
  size_t i3Count = 0;
  bool i3Preview = false;
  for (auto const& c : w.conversations)
    if (c.disappearing) { ++i3Count; i3Preview = !c.preview.empty(); }
  out.push_back(std::format(L"    I3  one disappearing     {}  {} of {} disappearing, preview {}",
                            Verdict(i3Count == 1 && i3Preview), i3Count,
                            w.conversations.size(),
                            i3Preview ? L"non-empty" : L"EMPTY"));
  // I4
  size_t i4 = 0;
  for (auto const& c : w.conversations) if (!c.preview.empty()) ++i4;
  out.push_back(std::format(L"    I4  previews non-empty   {}  {}/{} non-empty",
                            Verdict(i4 == w.conversations.size()), i4, w.conversations.size()));
  // I5
  size_t pend = 0, sent = 0, deliv = 0, read = 0, failed = 0, expired = 0, reasons = 0;
  for (auto const& c : w.conversations)
    for (auto const& r : c.rows) {
      if (r.kind != RowKind::Message || !r.outgoing) continue;
      switch (r.state) {
        case DeliveryState::Pending:   ++pend; break;
        case DeliveryState::Sent:      ++sent; break;
        case DeliveryState::Delivered: ++deliv; break;
        case DeliveryState::Read:      ++read; break;
        case DeliveryState::Failed:    ++failed; if (!r.failureReason.empty()) ++reasons; break;
        case DeliveryState::Expired:   ++expired; break;
      }
    }
  const size_t outgoing = pend + sent + deliv + read + failed + expired;
  const bool i5 = (pend && sent && deliv && read && failed == 1 && reasons == 1);
  out.push_back(std::format(
      L"    I5  delivery spread     {}  {} outgoing: pending {} sent {} delivered {} "
      L"read {} failed {} expired {}; {} failure reason",
      Verdict(i5), outgoing, pend, sent, deliv, read, failed, expired, reasons));
  // I6
  size_t systemRows = 0, permanent = 0;
  for (auto const& c : w.conversations)
    for (auto const& r : c.rows)
      if (r.kind == RowKind::System) { ++systemRows; if (r.permanentRecord) ++permanent; }
  out.push_back(std::format(L"    I6  key-change record   {}  {} permanent of {} system rows",
                            Verdict(permanent >= 1), permanent, systemRows));
  // I7
  size_t emptyGlyphs = 0;
  for (auto const& n : w.relayPath) if (n.glyph.empty()) ++emptyGlyphs;
  out.push_back(std::format(L"    I7  relay path          {}  {} nodes, {} empty glyphs",
                            Verdict(w.relayPath.size() == 3 && emptyGlyphs == 0),
                            w.relayPath.size(), emptyGlyphs));
  // I8
  size_t thisComputer = 0;
  for (auto const& d : w.myDevices) if (d.isThisComputer) ++thisComputer;
  const bool i8 = (thisComputer == 1 && !w.myDevices.empty() && w.myDevices[0].isThisComputer);
  out.push_back(std::format(L"    I8  this computer       {}  {} of {} devices, index 0 is {}",
                            Verdict(i8), thisComputer, w.myDevices.size(),
                            (!w.myDevices.empty() && w.myDevices[0].isThisComputer)
                                ? L"this computer" : L"NOT this computer"));
  // I9
  size_t withGroupId = 0;
  for (auto const& c : w.conversations)
    if (c.kind == ConversationKind::Group && !c.groupIdHex.empty()) ++withGroupId;
  out.push_back(std::format(L"    I9  group ids           {}  {}/{} groups carry a group id",
                            Verdict(withGroupId == groups), withGroupId, groups));
  // I10
  const uint64_t fp = WorldFingerprint(w);
  out.push_back(std::format(
      L"    I10 determinism         {}  fingerprint 0x{:016X}, expected 0x{:016X}",
      Verdict(fp == kExpectedWorldFingerprint), fp, kExpectedWorldFingerprint));
  return out;
}

// ---- demo composer invariants ----------------------------------------------
//
// This repo has no test project, so these lines ARE the tests for the composer's
// pure decisions. Each prints PASS or FAIL and prints the QUERY beside it, so a
// reader can tell what was actually checked rather than trusting the word PASS.
//
// BOUNDARIES, not samples. LayoutFor is fed CONTENT-root dips at runtime and a
// sampled window size ("1560x900 -> rail") would be asserting a different
// quantity from the one the app computes. `f(t) && !f(t - epsilon)` is true of
// the function whatever the frame inset is.
std::wstring DemoLayoutCheck() {
  using urmsg::demo::LayoutFor;
  using urmsg::demo::ListWidthFor;
  const bool wideEdge = LayoutFor(1000.0, 800.0).wide && !LayoutFor(999.9, 800.0).wide;
  const bool railEdge = LayoutFor(1500.0, 800.0).rail && !LayoutFor(1499.9, 800.0).rail;
  const bool stripEdge = LayoutFor(1200.0, 560.0).strip && !LayoutFor(1200.0, 559.9).strip;
  // width and height decide different things and must not leak into each other.
  const bool axes = LayoutFor(1600.0, 400.0).rail && !LayoutFor(1600.0, 400.0).strip &&
                    LayoutFor(800.0, 900.0).strip && !LayoutFor(800.0, 900.0).wide;
  // The list-width step (d3 section 4): 320 below 1200 of content, 360 at or
  // above it, with NO upper bound - at rail widths the flanks read 360|360.
  // Compared against LITERALS, not the constants, so a wrong constant fails
  // here rather than agreeing with itself; the rail edge above is asserted
  // unchanged beside it, since the step and the rail threshold are neighbours
  // at 1200/1500 and a move in one must not drag the other.
  const bool listStep = ListWidthFor(999.9) == 320.0 && ListWidthFor(1199.9) == 320.0 &&
                        ListWidthFor(1200.0) == 360.0 && ListWidthFor(1500.0) == 360.0;
  const bool ok = wideEdge && railEdge && stripEdge && axes && listStep;
  return std::format(
      L"  demo layout      : {}  (CONTENT dips. wide@1000 {} | rail@1500 {} | "
      L"strip@560h {} | width/height independent {} | list step 320->360@1200 {})",
      ok ? L"PASS" : L"FAIL", wideEdge, railEdge, stripEdge, axes, listStep);
}

std::wstring DemoDeepLinkCheck() {
  using urmsg::demo::DeepLinkFor;
  using urmsg::demo::DemoScreen;
  // Fix round 1: `checked` used to live inside a lambda chained with &&, so
  // short-circuit evaluation stopped counting at the FIRST failing tag - a
  // broken 5th mapping would have printed "4 nav tags checked", quietly
  // shrinking the very count this line exists to make trustworthy. Every
  // mapping is now evaluated unconditionally, so `checked` is always 7 and
  // `matches` is the number that actually agreed.
  struct Expected {
    DemoScreen screen;
    std::wstring_view tag;
  };
  constexpr Expected kExpectedTags[] = {
      {DemoScreen::None, L"chats"},       {DemoScreen::Chats, L"chats"},
      {DemoScreen::Thread, L"chats"},     {DemoScreen::Inspect, L"chats"},
      {DemoScreen::Network, L"network"},  {DemoScreen::Settings, L"settings"},
      {DemoScreen::Developer, L"developer"},
  };
  int checked = 0, matches = 0;
  for (auto const& e : kExpectedTags) {
    ++checked;
    if (DeepLinkFor(e.screen).navTag == e.tag) ++matches;
  }
  const bool tags = (matches == checked);
  // inspect is thread PLUS a message; thread is not.
  const bool ladder = !DeepLinkFor(DemoScreen::Chats).selectConversation &&
                      DeepLinkFor(DemoScreen::Thread).selectConversation &&
                      !DeepLinkFor(DemoScreen::Thread).selectMessage &&
                      DeepLinkFor(DemoScreen::Inspect).selectConversation &&
                      DeepLinkFor(DemoScreen::Inspect).selectMessage;
  // Developer is the ONLY screen that turns Advanced Mode on by existing.
  const bool advanced = DeepLinkFor(DemoScreen::Developer).forceAdvanced &&
                        !DeepLinkFor(DemoScreen::Network).forceAdvanced &&
                        !DeepLinkFor(DemoScreen::Settings).forceAdvanced &&
                        !DeepLinkFor(DemoScreen::Inspect).forceAdvanced;
  const bool ok = tags && ladder && advanced;
  return std::format(
      L"  demo deep link   : {}  ({}/{} nav tags matched {} | thread=conv, "
      L"inspect=conv+msg {} | only developer forces advanced {})",
      ok ? L"PASS" : L"FAIL", matches, checked, tags, ladder, advanced);
}

std::wstring DemoAutoplayCheck() {
  using namespace urmsg::demo;
  const bool ladder = AdvanceDelivery(DeliveryState::Pending) == DeliveryState::Sent &&
                      AdvanceDelivery(DeliveryState::Sent) == DeliveryState::Delivered &&
                      AdvanceDelivery(DeliveryState::Delivered) == DeliveryState::Read &&
                      AdvanceDelivery(DeliveryState::Read) == DeliveryState::Read &&
                      AdvanceDelivery(DeliveryState::Failed) == DeliveryState::Failed &&
                      AdvanceDelivery(DeliveryState::Expired) == DeliveryState::Expired;
  int rounds = 0;
  bool cadence = true;
  for (int round = 0; round < 8; ++round) {
    ++rounds;
    const int64_t typing = TypingMsForRound(round);
    const int64_t idle = IdleMsForRound(round);
    if (typing < 3000 || 6000 < typing) cadence = false;
    if (idle < 38000 || 42000 < idle) cadence = false;
  }
  // Deterministic, not random: the demo world is seeded and a second run of the
  // same demo has to behave like the first.
  const bool stable = IdleMsForRound(0) == IdleMsForRound(4) &&
                      TypingMsForRound(1) == TypingMsForRound(5) &&
                      IdleMsForRound(0) != IdleMsForRound(1);
  // The ambient row's clock is DERIVED from the conversation, never the wall
  // clock (the d7 audit's W8-class-4 override): round 0 lands strictly after
  // the open conversation's last seeded message and strictly before round 1.
  // String order is time order for zero-padded HH:MM. The tautology this
  // replaces compared IncomingForRound's label to itself.
  std::wstring lastSeeded;
  auto const& conv = GetWorld().conversations.front();
  for (auto const& row : conv.rows)
    if (row.kind == RowKind::Message) lastSeeded = row.timeLabel;
  const std::wstring round0 = IncomingForRound(conv, 0).timeLabel;
  const std::wstring round1 = IncomingForRound(conv, 1).timeLabel;
  const bool derived = lastSeeded < round0 && round0 < round1;
  const bool ok = ladder && cadence && stable && derived;
  return std::format(
      L"  demo autoplay    : {}  (ladder Pending->Sent->Delivered->Read->Read, "
      L"Failed/Expired terminal {} | {} rounds each with typing in 3000-6000ms and "
      L"idle in 38000-42000ms {} | period 4 and not constant {} | ambient clock "
      L"derived: last seeded {} < round 0 {} < round 1 {} {})",
      ok ? L"PASS" : L"FAIL", ladder, rounds, cadence, stable, lastSeeded, round0,
      round1, derived);
}

std::wstring DemoScrollPinCheck() {
  // Design 9.2's do-not-yank rule, walked as a pure function (W9, the d7
  // audit's class-6 override): the DECISION can be pure even where the event
  // cannot be synthesised. The four cases, with 892.8 as the scrollable
  // extent a measured run of this thread reports:
  //   unarmed pins unconditionally - the first real extent has offset 0 and a
  //     large scrollable height, indistinguishable from "scrolled to the
  //     top", so an unarmed guard would skip the very pin the handler exists
  //     to make;
  //   armed at the foot (zero slack) stays pinned;
  //   armed within 48 dip of the foot still counts as at the foot;
  //   armed and scrolled away is NOT pinned - the reader keeps their place
  //   when an ambient row lands.
  using urmsg::views::ShouldPinToBottom;
  const bool firstPin = ShouldPinToBottom(false, 0.0, 892.8);
  const bool atFoot = ShouldPinToBottom(true, 892.8, 892.8);
  const bool nearFoot = ShouldPinToBottom(true, 860.0, 892.8);
  const bool scrolledAway = !ShouldPinToBottom(true, 100.0, 892.8);
  const bool ok = firstPin && atFoot && nearFoot && scrolledAway;
  return std::format(
      L"  demo scroll pin  : {}  (ShouldPinToBottom over extent 892.8: unarmed "
      L"first pin {} | armed at foot {} | armed 32.8dip off foot (within 48) {} "
      L"| armed scrolled away blocked {})",
      ok ? L"PASS" : L"FAIL", firstPin, atFoot, nearFoot, scrolledAway);
}

// ---- reply and react: the names, the picker and the mapping ----------------
//
// Four lines, all evaluable without an apartment and without a session:
//   * the two per-bubble action names, BOTH ARMS BY PASSING THE CAPABILITY, so a launch that can
//     reach neither still gates both - the same arrangement RunModeCopyDiagnostics uses for the
//     mode;
//   * the picker's closed set against the ABI's own rule (1..64 octets of UTF-8, and distinct);
//   * the chip-word table over its closed set, with the one property that matters printed: a
//     reaction that is not Sent never carries the standing word;
//   * the live mapping over a locally built adversarial group - a standing reaction, a refused
//     react, a pending unreact and a pending reply - asserting that nothing unsealed renders as
//     standing and that a pending reply names its parent the way a sealed one does.
//
// Every non-ASCII character is printed as <U+XXXX> for the reason RunMode.cpp gives: the output
// crosses a redirected handle into a shell that decodes ANSI by default.
std::wstring EscapeNonAscii(std::wstring const& s) {
  std::wstring out;
  for (wchar_t c : s) {
    if (c < 0x20 || 0x7E < c)
      out += std::format(L"<U+{:04X}>", static_cast<unsigned int>(c));
    else
      out.push_back(c);
  }
  return out;
}

std::vector<std::wstring> ReplyReactDiagnostics() {
  using namespace urmsg::views;
  std::vector<std::wstring> out;

  // 1. the four per-message send affordances: every arm of every name, and the whole canAct table
  //
  // THE PROPERTY, STATED BEFORE THE ARM WAS WRITTEN. Each affordance has THREE names, one per
  // ComposerState, and each names the fact that actually stopped it: the MaySend arm denies
  // nothing, the NoSession arm names the missing SESSION, and the ObserverOnly arm denies
  // something and it is NEVER the session - an observer's session is provably live, because the
  // role could only have been read off an open group. And the decision itself is a total over
  // (state, targetable) that is true in exactly ONE of its six cells.
  //
  // WHY THE CELL TABLE IS HERE AT ALL: item 242 R4's first pass had three correct composer strings
  // and a `canAct` that read the SESSION alone, so the surface shipped 39 live Reply and React
  // buttons and a live [ Try again ] beside a composer saying this device could not send. A gate
  // over the copy could not see it. This one walks the decision.
  {
    constexpr std::wstring_view kSessionDenial = L"no live session";
    // "not send to it" is in the list so the MaySend arm cannot borrow the OBSERVER sentence
    // either: a live button must deny nothing at all, whichever way it were to do it.
    constexpr std::wstring_view kDenials[] = {L"no live session", L"not available", L"cannot",
                                              L"not send to it"};
    auto denies = [&](std::wstring const& s) {
      for (auto const& d : kDenials)
        if (s.find(d) != std::wstring::npos) return true;
      return false;
    };
    struct ActionRow {
      BubbleAction action;
      wchar_t const* label;
    };
    constexpr ActionRow kActions[] = {{BubbleAction::Reply, L"reply"},
                                      {BubbleAction::React, L"react"},
                                      {BubbleAction::Retry, L"thread retry"},
                                      {BubbleAction::RailRetry, L"rail retry"},
                                      {BubbleAction::Delete, L"delete"}};
    size_t ok = 0;
    std::wstring listing;   // ALL TWELVE NAMES, printed whatever the verdict: the complement of a
    std::wstring offenders; // narrowing is the thing that shows it narrowed to something real.
    for (auto const& a : kActions) {
      const std::wstring live = BubbleActionName(a.action, ComposerState::MaySend);
      const std::wstring dark = BubbleActionName(a.action, ComposerState::NoSession);
      const std::wstring obs = BubbleActionName(a.action, ComposerState::ObserverOnly);
      const bool nonEmpty = !live.empty() && !dark.empty() && !obs.empty();
      const bool distinct = live != dark && dark != obs && live != obs;
      const bool liveDeniesNothing = !denies(live);
      const bool darkNamesSession = dark.find(kSessionDenial) != std::wstring::npos;
      const bool obsNamesRoleNotSession =
          denies(obs) && obs.find(kSessionDenial) == std::wstring::npos;
      if (nonEmpty && distinct && liveDeniesNothing && darkNamesSession && obsNamesRoleNotSession)
        ++ok;
      else
        offenders += std::format(L"{}{}", offenders.empty() ? L"" : L", ", a.label);
      listing += std::format(L"{}{} \"{}\" | \"{}\" | \"{}\"", listing.empty() ? L"" : L"; ",
                             a.label, EscapeNonAscii(live), EscapeNonAscii(dark),
                             EscapeNonAscii(obs));
    }
    // THE DECISION, over all six cells. AND, never OR, and never the session alone.
    const bool cells = BubbleActionCanAct(ComposerState::MaySend, true) &&
                       !BubbleActionCanAct(ComposerState::MaySend, false) &&
                       !BubbleActionCanAct(ComposerState::ObserverOnly, true) &&
                       !BubbleActionCanAct(ComposerState::ObserverOnly, false) &&
                       !BubbleActionCanAct(ComposerState::NoSession, true) &&
                       !BubbleActionCanAct(ComposerState::NoSession, false);
    out.push_back(std::format(
        L"  bubble actions   : {}  {}/5 affordances name all three states apart; acts in {} of 6 "
        L"(state x targetable) cells; not-ok: {} -> {}   [query: for each of reply, react, thread "
        L"retry, rail retry and delete the three ComposerState names are non-empty and pairwise distinct; "
        L"the MaySend arm contains none of \"no live session\", \"not available\", \"cannot\", "
        L"\"not send to it\"; the NoSession arm contains \"no live session\"; the ObserverOnly arm "
        L"contains one of the denials and NOT \"no live session\", because an observer's session is "
        L"provably live; BubbleActionCanAct is true in exactly (MaySend, targetable)]",
        Verdict(ok == 5 && cells), ok,
        (BubbleActionCanAct(ComposerState::MaySend, true) ? 1 : 0) +
            (BubbleActionCanAct(ComposerState::MaySend, false) ? 1 : 0) +
            (BubbleActionCanAct(ComposerState::ObserverOnly, true) ? 1 : 0) +
            (BubbleActionCanAct(ComposerState::ObserverOnly, false) ? 1 : 0) +
            (BubbleActionCanAct(ComposerState::NoSession, true) ? 1 : 0) +
            (BubbleActionCanAct(ComposerState::NoSession, false) ? 1 : 0),
        offenders.empty() ? L"(none)" : offenders, listing));
  }

  // DELETE ALL MY MESSAGES: which lines it names. A synthetic conversation holding one of each
  // kind of row, and the ids it must and must not name, printed beside the verdict.
  {
    urmsg::demo::Conversation c;
    auto row = [&](urmsg::demo::RowKind kind, wchar_t const* id, bool outgoing, urmsg::demo::DeliveryState state) {
      urmsg::demo::MessageRow r{};
      r.kind = kind;
      r.id = id;
      r.outgoing = outgoing;
      r.state = state;
      c.rows.push_back(r);
    };
    row(urmsg::demo::RowKind::Message, L"sent", true, urmsg::demo::DeliveryState::Sent);
    row(urmsg::demo::RowKind::Message, L"read", true, urmsg::demo::DeliveryState::Read);
    row(urmsg::demo::RowKind::Message, L"theirs", false, urmsg::demo::DeliveryState::Sent);
    row(urmsg::demo::RowKind::Message, L"pending", true, urmsg::demo::DeliveryState::Pending);
    row(urmsg::demo::RowKind::Message, L"failed", true, urmsg::demo::DeliveryState::Failed);
    row(urmsg::demo::RowKind::System, L"system", true, urmsg::demo::DeliveryState::Sent);
    row(urmsg::demo::RowKind::DaySeparator, L"day", true, urmsg::demo::DeliveryState::Sent);
    const auto ids = OwnDeletableMessageIds(c);
    const bool named = ids == std::vector<std::wstring>{L"sent", L"read"};
    const bool words = DeleteAllConfirmTitle(3).find(L"3") != std::wstring::npos &&
                       DeleteAllConfirmPrimary(3).find(L"3") != std::wstring::npos &&
                       DeleteAllConfirmBody().starts_with(DeleteConfirmBody()) &&
                       DeleteAllConfirmBody().find(L"Only the messages this computer sent") !=
                           std::wstring::npos;
    std::wstring listing;
    for (auto const& id : ids) listing += id + L" ";
    out.push_back(std::format(
        L"  delete all mine  : {}  names [{}] of sent, read, theirs, pending, failed, system, day; "
        L"words name the count and open with Spec C's explainer {}   [query: OwnDeletableMessageIds "
        L"names exactly this device's outgoing Message rows in Sent, Delivered or Read]",
        Verdict(named && words), listing, words ? L"yes" : L"NO"));
  }

  // DELETE FOR ME AND LEAVE (ledger 273): the hand-over confirmation names its person and ends with
  // the plain one word for word, and neither says the conversation is gone anywhere but from this
  // computer, nor that anybody is told.
  {
    const std::wstring body = LeaveConfirmBody();
    const std::wstring handOver = LeaveHandOverBody(L"Ada");
    // SPEC C 12's QUOTED COPY, transcribed here by hand, so the check has a second copy to disagree
    // with and not only the function it checks
    const std::wstring specC =
        L"This deletes the conversation and its keys from this computer. The others are not told. "
        L"They keep their copies, and you stay on their member list until they remove you. To take "
        L"your messages off their devices too, delete them for everyone first: that reaches devices "
        L"that are online and honest, not anyone who has already read them.";
    const bool words = body == specC &&
                       handOver.find(L"Ada") != std::wstring::npos && handOver.ends_with(body) &&
                       body.find(L"from this computer") != std::wstring::npos &&
                       body.find(L"not told") != std::wstring::npos &&
                       body.find(L"forever") == std::wstring::npos &&
                       LeaveHandOverTitle(L"Ada").find(L"Ada") != std::wstring::npos;
    out.push_back(std::format(
        L"  leave words      : {}  the body is Spec C 12's, the hand-over names its person and ends with "
        L"the plain body; "
        L"the body says \"from this computer\" and \"not told\" and never \"forever\"   "
        L"[query: LeaveConfirmBody, LeaveHandOverBody(L\"Ada\")]",
        Verdict(words)));
  }

  // THE DELETE CONFIRMATION, against Spec C's string table TRANSCRIBED HERE (the kSpecC511
  // pattern: an independent copy, so a paraphrase in the app fails the launch). The owner's ruling
  // of 2026-10-02 removed the 24-hour window and kept this explainer.
  //   docs/specs/2026-08-12-spec-c-windows-client-ui.md - msg_delete_for_everyone_explainer
  {
    constexpr wchar_t kSpecCDeleteExplainer[] =
        L"Removed from this conversation on every device that is online and honest. Anyone who "
        L"already read it may have kept a copy, and we cannot detect that.";
    const bool body = DeleteConfirmBody() == kSpecCDeleteExplainer;
    const bool noWindow = DeleteConfirmBody().find(L"24 hours") == std::wstring::npos &&
                          DeleteConfirmTitle().find(L"24") == std::wstring::npos;
    const bool distinct = DeleteConfirmPrimary() != DeleteConfirmClose() &&
                          !DeleteConfirmPrimary().empty() && !DeleteConfirmClose().empty();
    out.push_back(std::format(
        L"  delete confirm   : {}  body is Spec C's explainer {}; no time window named {}; two "
        L"distinct buttons {} -> \"{}\" [{}] [{}]   [query: DeleteConfirmBody() == the "
        L"msg_delete_for_everyone_explainer transcribed in Startup.cpp; neither title nor body "
        L"mentions 24 hours; primary and close differ and are non-empty]",
        Verdict(body && noWindow && distinct), body ? L"yes" : L"NO", noWindow ? L"yes" : L"NO",
        distinct ? L"yes" : L"NO", DeleteConfirmTitle(), DeleteConfirmPrimary(),
        DeleteConfirmClose()));
  }

  // 2. the picker
  {
    size_t inRange = 0;
    std::wstring listing;
    std::vector<std::wstring> seen;
    bool distinct = true;
    for (size_t i = 0; i < kReactionPickerCount; ++i) {
      const size_t octets = ReactionPickerOctets(i);
      if (1 <= octets && octets <= 64) ++inRange;
      const std::wstring e = kReactionPicker[i];
      for (auto const& s : seen)
        if (s == e) distinct = false;
      seen.push_back(e);
      if (!listing.empty()) listing += L", ";
      listing += std::format(L"{} ({} octets)", EscapeNonAscii(e), octets);
    }
    const bool sized = 6 <= kReactionPickerCount && kReactionPickerCount <= 8;
    out.push_back(std::format(
        L"  reaction picker  : {}  {} entries, {} within the ABI's 1..64 UTF-8 octets, distinct "
        L"{} -> {}   [query: 6 <= count <= 8; every entry narrows to 1..64 octets; no two equal]",
        Verdict(sized && inRange == kReactionPickerCount && distinct), kReactionPickerCount,
        inRange, distinct ? L"yes" : L"NO", listing));
  }

  // 3. the chip words over the closed set
  {
    using urmsg::demo::DeliveryState;
    using urmsg::demo::MessageReaction;
    auto make = [](bool mine, DeliveryState s, bool removing) {
      MessageReaction r;
      r.emoji = L"x";
      r.mine = mine;
      r.state = s;
      r.removing = removing;
      return r;
    };
    const std::wstring yours = ReactionChipWord(make(true, DeliveryState::Sent, false));
    const std::wstring theirs = ReactionChipWord(make(false, DeliveryState::Sent, false));
    const std::wstring sending = ReactionChipWord(make(true, DeliveryState::Pending, false));
    const std::wstring removing = ReactionChipWord(make(true, DeliveryState::Pending, true));
    const std::wstring notSent = ReactionChipWord(make(true, DeliveryState::Failed, false));
    const std::wstring notRemoved = ReactionChipWord(make(true, DeliveryState::Failed, true));
    // THE PROPERTY: the standing word appears on a Sent reaction of yours and on NOTHING that is
    // not Sent - a refused or in-flight attempt must never read as standing.
    const bool ok = yours == L"you" && theirs.empty() && sending != yours && removing != yours &&
                    notSent != yours && notRemoved != yours && !sending.empty() &&
                    !removing.empty() && !notSent.empty() && !notRemoved.empty() &&
                    sending != removing && notSent != notRemoved;
    out.push_back(std::format(
        L"  reaction words   : {}  yours \"{}\" | theirs \"{}\" | sending \"{}\" | removing "
        L"\"{}\" | not sent \"{}\" | not removed \"{}\"   [query: the standing word is on the "
        L"Sent+mine case only; every non-Sent case has its own non-empty word]",
        Verdict(ok), yours, theirs, sending, removing, notSent, notRemoved));
  }

  // 4. the live mapping, over an adversarial group built here
  {
    using namespace urmsg::live;
    LiveGroup g;
    g.groupIdHex = "00";
    g.epoch = 1;
    g.open = true;
    LiveMessage m;
    m.recordId = 5;
    m.messageId = std::string(64, 'a');
    m.senderHandle = std::string(32, 'b');
    m.mine = false;
    m.sentAtMs = 1789754263488;
    m.kind = 0x01;  // URNET_MESSAGE_KIND_TEXT, by value: this TU does not include the ABI header
    m.body = "a line from the peer";
    m.bodyLen = static_cast<int32_t>(m.body.size());
    m.reactions.push_back({"\xF0\x9F\x91\x8D", false});   // the peer's standing thumbs up
    m.reactions.push_back({"\xF0\x9F\x8E\x89", true});    // this device's standing party popper
    g.messages.push_back(m);
    LiveReactionOutboxEntry refused;
    refused.localId = "1";
    refused.targetId = m.messageId;
    refused.emoji = "\xE2\x9D\xA4\xEF\xB8\x8F";        // a heart the library refused
    refused.failed = true;
    refused.error = "refused for the test";
    g.reactionOutbox.push_back(refused);
    LiveReactionOutboxEntry removing;
    removing.localId = "2";
    removing.targetId = m.messageId;
    removing.emoji = "\xF0\x9F\x8E\x89";                 // taking the party popper back, in flight
    removing.remove = true;
    g.reactionOutbox.push_back(removing);
    LiveOutboxEntry reply;
    reply.localId = "3";
    reply.body = "a reply not yet sent";
    reply.replyToId = m.messageId;
    reply.attemptedAtMs = m.sentAtMs + 1000;
    g.outbox.push_back(reply);

    const urmsg::demo::World w = BuildWorld(g);
    const urmsg::demo::MessageRow* target = nullptr;
    const urmsg::demo::MessageRow* parentLine = nullptr;
    const urmsg::demo::MessageRow* pendingReply = nullptr;
    if (!w.conversations.empty()) {
      auto const& rows = w.conversations.front().rows;
      for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].id == urnw::Widen(m.messageId)) target = &rows[i];
        if (rows[i].id == L"outbox-3-reply") parentLine = &rows[i];
        if (rows[i].id == L"outbox-3") pendingReply = &rows[i];
      }
    }
    size_t standing = 0, notStanding = 0, mineStanding = 0;
    bool refusedCarriesReason = false, removingIsPending = false;
    if (target) {
      for (auto const& r : target->reactions) {
        if (r.state == urmsg::demo::DeliveryState::Sent) {
          ++standing;
          if (r.mine) ++mineStanding;
        } else {
          ++notStanding;
        }
        if (r.state == urmsg::demo::DeliveryState::Failed && r.failureReason == L"refused for the test")
          refusedCarriesReason = true;
        if (r.removing && r.state == urmsg::demo::DeliveryState::Pending) removingIsPending = true;
      }
    }
    const bool parentNamed = parentLine != nullptr &&
                             parentLine->kind == urmsg::demo::RowKind::System &&
                             parentLine->systemText.find(L"In reply to message aaaaaaaaaaaa") !=
                                 std::wstring::npos;
    const bool replyPending = pendingReply != nullptr &&
                              pendingReply->state == urmsg::demo::DeliveryState::Pending;
    const bool ok = target != nullptr && target->reactions.size() == 4 && standing == 2 &&
                    mineStanding == 1 && notStanding == 2 && refusedCarriesReason &&
                    removingIsPending && parentNamed && replyPending;
    out.push_back(std::format(
        L"  live reactions   : {}  target row carries {} reactions: {} standing ({} yours), {} not "
        L"standing; refused react carries the library's reason {}; in-flight unreact is Pending "
        L"and the record it takes back still stands {}; pending reply draws its parent line {} "
        L"and is Pending {}   [query: BuildWorld over 1 message with 2 standing reactions + 1 "
        L"refused react + 1 pending unreact + 1 pending reply; nothing unsealed may be Sent]",
        Verdict(ok), target ? target->reactions.size() : 0, standing, mineStanding, notStanding,
        refusedCarriesReason ? L"yes" : L"NO", removingIsPending ? L"yes" : L"NO",
        parentNamed ? L"yes" : L"NO", replyPending ? L"yes" : L"NO"));
  }
  return out;
}


// ---- OBSERVER read-only: the composer's three states and the hidden row (item 242 R4) ---------
//
// Same terms as everything above it: every function asked here is pure (Views/ThreadLayout.h,
// Views/RosterRules.h), no world is mutated, no XAML is touched, and every line prints PASS/FAIL,
// the QUERY it asked, and the complement it looked at.
//
// THE FOUR PROPERTIES, STATED BEFORE THE STRINGS WERE WRITTEN (this app has burned twice on
// gates written after the copy, which is how a check ends up agreeing with whatever is there):
//
//   1. COMPOSER. The three states are distinct, the SESSION denial appears in the no-session arm
//      ONLY, the observer arm carries Spec C section 5.6 line 511 CHARACTER FOR CHARACTER in all
//      three of its channels (button name, box name, placeholder) and denies no session, and the
//      may-send arm denies nothing at all. The box is dead in exactly the observer arm.
//   2. HIDDEN ROW. Over the fabricated world: a row collapses IF AND ONLY IF its senderRoleAtSend
//      is "observer"; the collapsed copy is Spec C line 377 character for character; the row is a
//      MESSAGE row whose BODY IS STILL THERE (hide, not drop); it is not a gap, not a system row
//      and not a reason; and the expansion's warning is non-empty and differs from the line. Both
//      sets are printed and both must be NON-EMPTY, or the case passes vacuously.
//   3. THE CAVEAT'S PLACEMENT (ruling 22). The caveat is Spec C line 513 verbatim, it is NOT in
//      the composer's sentence, and the composer's sentence is NOT in the caveat: the two say
//      different things about different people's software and neither may borrow the other.
//   4. THE SENTENCE ON THE AFFORDANCES BESIDE THE COMPOSER. The per-bubble Reply and React and
//      both [ Try again ] buttons end in Spec C line 511 character for character in their observer
//      arm, and carry it in NO other arm. R4's first pass gated the composer and left these four
//      on the session fact alone, so the screen showed 39 live Reply/React buttons and a live
//      retry beside a composer saying this device could not send; property 4 is the copy half of
//      closing that, and `bubble actions` above is the decision half.
std::vector<std::wstring> ObserverDiagnostics() {
  using namespace urmsg::views;
  namespace demo = urmsg::demo;
  std::vector<std::wstring> out;

  // SPEC C'S OWN SENTENCES, TRANSCRIBED HERE, IN THIS FILE, FROM THE SPEC — not read out of
  // Views/ThreadLayout.h. That distinction is the difference between a gate and a mirror: a check
  // that compares the app's constant against the app's constant passes on every rewording of it,
  // and "character for character" would then be a claim about nothing. These three literals are
  // the INDEPENDENT copy, so a paraphrase anywhere in the app fails the launch and has to be a
  // deliberate edit HERE, naming the spec line it moved.
  //   docs/specs/2026-08-12-spec-c-windows-client-ui.md:511 — the composer's reason
  //   docs/specs/2026-08-12-spec-c-windows-client-ui.md:377 — the collapsed system row
  //   docs/specs/2026-08-12-spec-c-windows-client-ui.md:513 — the roster row's caveat
  // U+2014 EM DASH as an escape, the house non-ASCII rule (the spec's line carries the glyph).
  constexpr wchar_t kSpecC511[] = L"You can read this group but not send to it.";
  constexpr wchar_t kSpecC377[] = L"A message from an observer was hidden.";
  constexpr wchar_t kSpecC513[] =
      L"Observers are asked not to send. Someone who modifies their app can still send, and this "
      L"version of URmessage cannot stop it at the server \u2014 it can only hide the result.";

  // 1. the composer's three states
  {
    constexpr std::wstring_view kSessionDenial = L"no live session";
    auto name = [](ComposerState s, bool hasText) {
      return ComposerSendName(s, hasText, /*replying=*/false);
    };
    const std::wstring darkSend = name(ComposerState::NoSession, true);
    const std::wstring obsSend = name(ComposerState::ObserverOnly, true);
    const std::wstring liveSend = name(ComposerState::MaySend, true);
    const std::wstring liveEmpty = name(ComposerState::MaySend, false);
    const std::wstring darkBox = ComposerBoxName(ComposerState::NoSession, false);
    const std::wstring obsBox = ComposerBoxName(ComposerState::ObserverOnly, false);
    const std::wstring liveBox = ComposerBoxName(ComposerState::MaySend, false);
    const std::wstring obsHint = ComposerBoxPlaceholder(ComposerState::ObserverOnly);
    const std::wstring liveHint = ComposerBoxPlaceholder(ComposerState::MaySend);
    // THE SPEC'S TEXT, not the app's constant — see the transcription note above.
    const std::wstring reason = kSpecC511;

    // The state machine itself, over the whole 2x2: session first, always.
    const bool machine = ComposerStateFor(false, true) == ComposerState::NoSession &&
                         ComposerStateFor(false, false) == ComposerState::NoSession &&
                         ComposerStateFor(true, false) == ComposerState::ObserverOnly &&
                         ComposerStateFor(true, true) == ComposerState::MaySend;
    // The SESSION denial is in the dark arm and NOWHERE else. An observer's session is provably
    // live - the role could only have been read off an open group - so this is the one sentence
    // that arm must never borrow, and it is the defect R3 shipped on a different control.
    const bool denialPlaced = darkSend.find(kSessionDenial) != std::wstring::npos &&
                              darkBox.find(kSessionDenial) != std::wstring::npos &&
                              obsSend.find(kSessionDenial) == std::wstring::npos &&
                              obsBox.find(kSessionDenial) == std::wstring::npos &&
                              obsHint.find(kSessionDenial) == std::wstring::npos &&
                              liveSend.find(kSessionDenial) == std::wstring::npos &&
                              liveBox.find(kSessionDenial) == std::wstring::npos;
    // Spec C's sentence, character for character, in all three channels of the observer arm.
    const bool reasonSpoken = obsSend == reason && obsBox.find(reason) != std::wstring::npos &&
                              obsHint == reason;
    // Three distinct arms, and the box dead in exactly one of them.
    const bool distinct = darkSend != obsSend && obsSend != liveSend && darkSend != liveSend &&
                          darkBox != obsBox && obsBox != liveBox && liveSend != liveEmpty;
    const bool boxState = ComposerBoxEnabled(ComposerState::NoSession) &&
                          !ComposerBoxEnabled(ComposerState::ObserverOnly) &&
                          ComposerBoxEnabled(ComposerState::MaySend);
    const bool ok = machine && denialPlaced && reasonSpoken && distinct && boxState;
    out.push_back(std::format(
        L"  composer states  : {}  no session \"{}\" / box \"{}\" | observer \"{}\" / box \"{}\" / "
        L"placeholder \"{}\" | may send \"{}\" (empty box \"{}\") / box \"{}\"; box dead in the "
        L"observer arm only {}   [query: ComposerStateFor is session-first over the whole 2x2; the "
        L"words \"no live session\" appear in the no-session arm and in NEITHER other arm, because "
        L"an observer's session is provably live; the observer arm's button name, box name and "
        L"placeholder all carry Spec C section 5.6 line 511 character for character; the three "
        L"arms are pairwise distinct; ComposerBoxEnabled is false in exactly the observer arm]",
        Verdict(ok), EscapeNonAscii(darkSend), EscapeNonAscii(darkBox), EscapeNonAscii(obsSend),
        EscapeNonAscii(obsBox), EscapeNonAscii(obsHint), EscapeNonAscii(liveSend),
        EscapeNonAscii(liveEmpty), EscapeNonAscii(liveBox), boxState ? L"yes" : L"NO"));
  }

  // 1b. THE REMOVED ARM (the conversation-list design 2.1): first, whatever the session and the role
  {
    constexpr std::wstring_view kSessionDenial = L"no live session";
    const bool machine = ComposerStateFor(false, true, true) == ComposerState::Removed &&
                         ComposerStateFor(true, true, true) == ComposerState::Removed &&
                         ComposerStateFor(true, false, true) == ComposerState::Removed &&
                         ComposerStateFor(true, true) == ComposerState::MaySend;
    const std::wstring send = ComposerSendName(ComposerState::Removed, true, false);
    const std::wstring box = ComposerBoxName(ComposerState::Removed, false);
    const std::wstring hint = ComposerBoxPlaceholder(ComposerState::Removed);
    const std::wstring note = ComposerRemovedNote();
    const std::wstring line = demo::kRemovedLine;
    const bool says = send.find(line) != std::wstring::npos && box.find(line) != std::wstring::npos &&
                      hint == line && note == line;
    const bool noDenial = send.find(kSessionDenial) == std::wstring::npos &&
                          box.find(kSessionDenial) == std::wstring::npos &&
                          hint.find(kSessionDenial) == std::wstring::npos;
    const bool dead = !ComposerBoxEnabled(ComposerState::Removed);
    const bool ok = machine && says && noDenial && dead;
    out.push_back(std::format(
        L"  composer removed : {}  removed first over the session and the role {} | send \"{}\" / box "
        L"\"{}\" / placeholder \"{}\" / caption say the removed line {} and never \"no live session\" {} "
        L"| box dead {}   [query: ComposerStateFor answers Removed whatever the session and the role, "
        L"and its four channels carry the removed line and not a session denial, which is false of a "
        L"device whose session is live and whose group removed it]",
        Verdict(ok), machine ? L"yes" : L"NO", EscapeNonAscii(send), EscapeNonAscii(box),
        EscapeNonAscii(hint), says ? L"yes" : L"NO", noDenial ? L"yes" : L"NO", dead ? L"yes" : L"NO"));
  }

  // 1c. THE REMOVED CONVERSATION'S OTHER CONTROLS (the conversation-list design 2.1; lead ruling
  //     R67): every bubble action, both [ Try again ]s and every role control on the roster name the
  //     removal, act on nothing, and never deny a session, which is live. The sentence is typed HERE,
  //     not read off demo::kRemovedLine, so a rewording anywhere in the app fails this line.
  {
    constexpr std::wstring_view kSessionDenial = L"no live session";
    constexpr std::wstring_view kRemoved = L"You were removed from this conversation.";
    std::size_t asked = 0;
    std::size_t right = 0;
    std::wstring firstBad;
    std::wstring listing;
    std::vector<std::wstring> seen;
    auto ask = [&](std::wstring const& name, bool acts, wchar_t const* what) {
      ++asked;
      const bool endsWithIt = kRemoved.size() < name.size() &&
                              name.compare(name.size() - kRemoved.size(), kRemoved.size(), kRemoved) == 0;
      const bool named = name.find(L"disabled") != std::wstring::npos;
      const bool noDenial = name.find(kSessionDenial) == std::wstring::npos;
      const bool apart = std::find(seen.begin(), seen.end(), name) == seen.end();
      seen.push_back(name);
      if (endsWithIt && named && noDenial && !acts && apart) {
        ++right;
      } else if (firstBad.empty()) {
        firstBad = std::format(L"{} \"{}\"", what, EscapeNonAscii(name));
      }
      listing += std::format(L"{}{} \"{}\"", listing.empty() ? L"" : L"; ", what, EscapeNonAscii(name));
    };
    struct BubbleRow {
      BubbleAction action;
      wchar_t const* label;
    };
    constexpr BubbleRow kBubbles[] = {{BubbleAction::Reply, L"reply"},
                                      {BubbleAction::React, L"react"},
                                      {BubbleAction::Delete, L"delete"},
                                      {BubbleAction::Retry, L"thread retry"}};
    for (auto const& b : kBubbles)
      ask(BubbleActionName(b.action, ComposerState::Removed),
          BubbleActionCanAct(ComposerState::Removed, true), b.label);
    // THE RAIL'S [ Try again ] keeps its own commas and so says what the thread's says: it is asked on
    // its own, for the removal and no denial, and not for being apart from the thread's
    {
      const std::wstring rail = BubbleActionName(BubbleAction::RailRetry, ComposerState::Removed);
      const bool railOk = rail.ends_with(kRemoved) && rail.find(kSessionDenial) == std::wstring::npos &&
                          !BubbleActionCanAct(ComposerState::Removed, true);
      ++asked;
      if (railOk) {
        ++right;
      } else if (firstBad.empty()) {
        firstBad = std::format(L"rail retry \"{}\"", EscapeNonAscii(rail));
      }
      listing += std::format(L"; rail retry \"{}\"", EscapeNonAscii(rail));
    }
    struct RoleRow {
      RoleVerb verb;
      wchar_t const* label;
    };
    constexpr RoleRow kRoles[] = {{RoleVerb::MakeAdmin, L"make admin"},
                                  {RoleVerb::MakeMember, L"make member"},
                                  {RoleVerb::MakeObserver, L"make observer"},
                                  {RoleVerb::TransferOwnership, L"transfer"},
                                  {RoleVerb::Remove, L"remove"}};
    for (auto const& r : kRoles) ask(RoleControlName(r.verb, RoleControlState::Removed), false, r.label);
    // THE OTHER ARMS KEEP THEIR OWN: a session that is really missing is still named so
    const bool othersKept =
        BubbleActionName(BubbleAction::Reply, ComposerState::NoSession).find(kSessionDenial) != std::wstring::npos &&
        RoleControlName(RoleVerb::Remove, RoleControlState::NoSession).find(kSessionDenial) != std::wstring::npos &&
        BubbleActionName(BubbleAction::Reply, ComposerState::MaySend) == L"Reply to this message";
    // AND THE STATE EVERY AFFORDANCE ASKS FOR (SendAffordanceState): the removal first, with the
    // session dark, which is how a removed conversation's group stands
    const bool machine = ComposerStateFor(false, true, true) == ComposerState::Removed &&
                         ComposerStateFor(false, false, true) == ComposerState::Removed;
    const bool ok = right == asked && othersKept && machine;
    out.push_back(std::format(
        L"  removed controls : {}  {} of {} controls name the removal, are dark and deny no session{}{} "
        L"| the other arms kept {} | removed asked first {} | {}   [query: BubbleActionName for reply, "
        L"react, delete and both retries and RoleControlName for the five role verbs, in a removed "
        L"conversation, end with the removed sentence typed here, say \"disabled\", never say \"no live "
        L"session\" and act on nothing; the conversation-list design 2.1, lead ruling R67]",
        Verdict(ok), right, asked, firstBad.empty() ? L"" : L", first wrong: ", firstBad,
        othersKept ? L"yes" : L"NO", machine ? L"yes" : L"NO", listing));
  }

  // 2. the hidden row, over the fabricated world, with BOTH sets printed
  {
    demo::World const& w = demo::GetWorld();
    std::wstring collapsed;   // rows whose plan says HiddenObserverRecord
    std::wstring carriesRole; // rows carrying a non-observer role: the positive control
    size_t collapsedCount = 0, controlCount = 0, mismatched = 0, bodyLost = 0, notAMessage = 0;
    for (auto const& c : w.conversations) {
      const auto plan = PlanThreadRows(c);
      for (auto const& p : plan) {
        demo::MessageRow const& r = c.rows[p.rowIndex];
        const bool hidden = p.shape == ThreadRowShape::HiddenObserverRecord;
        const bool isObserver = r.senderRoleAtSend == demo::kRoleObserver;
        // THE IF AND ONLY IF, per row. A shape that fired on anything else, or failed to fire on
        // an observer's row, lands here whichever direction it went.
        if (hidden != (isObserver && r.kind == demo::RowKind::Message)) ++mismatched;
        if (hidden) {
          ++collapsedCount;
          // HIDE, NOT DROP: the record is still a Message row and its body is still here.
          if (r.kind != demo::RowKind::Message) ++notAMessage;
          if (r.body.empty()) ++bodyLost;
          if (!collapsed.empty()) collapsed += L", ";
          collapsed += std::format(L"{} \"{}\"", r.id, r.body);
        } else if (!r.senderRoleAtSend.empty()) {
          ++controlCount;
          if (!carriesRole.empty()) carriesRole += L", ";
          carriesRole += std::format(L"{} {}", r.id, r.senderRoleAtSend);
        }
      }
    }
    const std::wstring line = kHiddenObserverLine;
    const std::wstring warn = kHiddenObserverWarning;
    const std::wstring showName = HiddenObserverToggleName(false);
    const std::wstring hideName = HiddenObserverToggleName(true);
    // Spec C section 5.1's row, against this file's own transcription of it.
    const bool copyVerbatim = (line == kSpecC377);
    // EXPANDABILITY, as far as a pure gate can reach it: the toggle has two distinct non-empty
    // names and the warning is real copy that is not the collapsed line repeated. The PIXELS are
    // the screenshot's job; what is checkable here is that the row has a second state to go to and
    // something to say when it gets there.
    const bool expandable = !showName.empty() && !hideName.empty() && showName != hideName &&
                            !warn.empty() && warn != line;
    // NEITHER SET MAY BE EMPTY. An empty collapsed set means the fixture lost its observer row and
    // this gate would pass over nothing; an empty control set means nothing proves the rule is
    // about the ROLE rather than about carrying one at all.
    const bool ok = mismatched == 0 && bodyLost == 0 && notAMessage == 0 && collapsedCount >= 1 &&
                    controlCount >= 1 && expandable && copyVerbatim;
    out.push_back(std::format(
        L"  hidden observer  : {}  collapsed [{}] || NOT collapsed but role-carrying [{}]; "
        L"mismatches {}, bodies lost {}, non-message rows {}; copy \"{}\"; toggle \"{}\" / \"{}\"; "
        L"warning \"{}\"   [query: over every conversation of the fabricated world, "
        L"PlanThreadRows says HiddenObserverRecord IF AND ONLY IF the row is a Message row whose "
        L"senderRoleAtSend is \"observer\"; every collapsed row is still a Message row with a "
        L"non-empty body (hide, not drop); both sets non-empty; the collapsed copy equals this "
        L"file's own transcription of spec C section 5.1 line 377; the toggle has two distinct "
        L"non-empty names and the warning is non-empty and differs from the collapsed line]",
        Verdict(ok), collapsed.empty() ? std::wstring(L"none") : collapsed,
        carriesRole.empty() ? std::wstring(L"none") : carriesRole, mismatched, bodyLost,
        notAMessage, EscapeNonAscii(line), EscapeNonAscii(showName), EscapeNonAscii(hideName),
        EscapeNonAscii(warn)));
  }

  // 3. the two observer sentences, and the rule that keeps them apart (ruling 22)
  {
    const std::wstring reason = kObserverComposerReason;
    const std::wstring caveat = kObserverSettingsCaveat;
    // Both against this file's own transcription of spec C, for the reason at the top.
    const bool verbatim = (reason == kSpecC511) && (caveat == kSpecC513);
    // The composer sentence says what THIS app does and stops. The caveat says what it cannot do
    // about OTHER people's clients. Neither may contain the other: a composer that carried the
    // caveat would undercut its own refusal above the box, and a caveat that carried the composer
    // sentence would be this app claiming a server-side gate on somebody else's row.
    const bool apart = !reason.empty() && !caveat.empty() &&
                       caveat.find(reason) == std::wstring::npos &&
                       reason.find(caveat) == std::wstring::npos &&
                       reason.find(L"modif") == std::wstring::npos &&
                       reason.find(L"server") == std::wstring::npos &&
                       caveat.find(L"cannot stop it at the server") != std::wstring::npos;
    // And the caveat reaches a surface: the rail puts it on an observer's row and on no other, so
    // the fixture must actually have one for the screenshot to be of anything.
    size_t observers = 0, others = 0;
    for (auto const& c : demo::GetWorld().conversations)
      for (auto const& m : c.members) (m.role == demo::kRoleObserver ? observers : others) += 1;
    const bool ok = apart && verbatim && observers >= 1 && others >= 1;
    out.push_back(std::format(
        L"  observer caveat  : {}  composer \"{}\" | roster row \"{}\"; the fabricated world has "
        L"{} observer member row(s) and {} other member row(s)   [query: both sentences equal this "
        L"file's own transcription of spec C section 5.6 lines 511 and 513; NEITHER contains the "
        L"other; the composer sentence names no modified client and no server (ruling 22 puts the "
        L"caveat where the group is configured); the caveat names the server it cannot stop; and "
        L"at least one member row of each kind exists, so the rail's observer-only placement is "
        L"not vacuous]",
        Verdict(ok), EscapeNonAscii(reason), EscapeNonAscii(caveat), observers, others));
  }

  // 4. the observer sentence on the FOUR AFFORDANCES BESIDE the composer, and NOWHERE ELSE
  //
  // Property 4 of the list at the top. The composer's own three channels are case 1's; this is the
  // same sentence on the per-bubble Reply and React and on both [ Try again ] buttons, which is
  // where item 242 R4's first pass left the role unread. TWO HALVES, and the second is the one a
  // copy gate usually forgets: the sentence is in the ObserverOnly arm of all four, AND it is in
  // NO arm of any other state - so a tidy-up cannot make every button say it and pass.
  {
    const std::wstring reason = kSpecC511;  // this file's transcription, never the app's constant
    size_t carry = 0, leaked = 0;
    std::wstring leakage;
    for (BubbleAction a : {BubbleAction::Reply, BubbleAction::React, BubbleAction::Retry,
                           BubbleAction::RailRetry}) {
      const std::wstring obs = BubbleActionName(a, ComposerState::ObserverOnly);
      // ENDS WITH the sentence, character for character: the arm names its control first (a reader
      // has to know WHICH control is dead), then quotes spec C and stops - so a paraphrase, a
      // truncation or a trailing apology all fail here.
      if (obs.size() >= reason.size() && obs.compare(obs.size() - reason.size(), reason.size(),
                                                     reason) == 0)
        ++carry;
      for (ComposerState s : {ComposerState::NoSession, ComposerState::MaySend}) {
        const std::wstring other = BubbleActionName(a, s);
        if (other.find(reason) != std::wstring::npos) {
          ++leaked;
          leakage += std::format(L"{}{}", leakage.empty() ? L"" : L", ", EscapeNonAscii(other));
        }
      }
      // And the CAVEAT is never on a button: ruling 22 puts it where the group is configured.
      if (obs.find(kObserverSettingsCaveat) != std::wstring::npos) ++leaked;
    }
    const bool ok = carry == 4 && leaked == 0;
    out.push_back(std::format(
        L"  observer buttons : {}  {}/4 affordances end in spec C line 511 verbatim; {} leak(s) of "
        L"it into another state -> {}; the four observer arms are [{}]   [query: for reply, react, "
        L"thread retry and rail retry, BubbleActionName(ObserverOnly) ENDS WITH this file's own "
        L"transcription of spec C section 5.6 line 511; the same sentence appears in NEITHER the "
        L"NoSession nor the MaySend arm of any of them; and the roster's caveat (line 513) appears "
        L"on no button at all, because ruling 22 puts it where the group is configured]",
        Verdict(ok), carry, leaked, leakage.empty() ? L"(none)" : leakage,
        EscapeNonAscii(BubbleActionName(BubbleAction::Reply, ComposerState::ObserverOnly)) + L" / " +
            EscapeNonAscii(
                BubbleActionName(BubbleAction::React, ComposerState::ObserverOnly)) +
            L" / " +
            EscapeNonAscii(
                BubbleActionName(BubbleAction::Retry, ComposerState::ObserverOnly)) +
            L" / " +
            EscapeNonAscii(
                BubbleActionName(BubbleAction::RailRetry, ComposerState::ObserverOnly))));
  }
  return out;
}

// ---- the roster: role words, control sets, outcome notes, the live mapping (item 242 R3) ------
//
// Same terms as ReplyReactDiagnostics: every function asked here is pure (Views/RosterRules.h,
// Live/LiveWorld.h's BuildWorld), no world is mutated, no XAML is touched, and every line prints
// PASS/FAIL, the QUERY it asked and the complement it looked at. The framing pair the roster adds
// ("roster note") is gated with the other pairs in RunModeCopyDiagnostics, and the live
// disclosure's absence list is held there to no longer name the member list.
std::vector<std::wstring> RosterDiagnostics() {
  using namespace urmsg::views;
  namespace demo = urmsg::demo;
  std::vector<std::wstring> out;

  // 1. the role words: four distinct non-empty words for the four spellings, the placeholder for
  //    anything else, and "You" leading the title of the viewer's own row whatever its name.
  {
    const std::wstring words[] = {RoleWord(demo::kRoleOwner), RoleWord(demo::kRoleAdmin),
                                  RoleWord(demo::kRoleMember), RoleWord(demo::kRoleObserver)};
    bool distinct = true;
    bool nonEmpty = true;
    for (size_t i = 0; i < 4; ++i) {
      if (words[i].empty() || words[i] == demo::kUnavailable) nonEmpty = false;
      for (size_t j = 0; j < i; ++j)
        if (words[i] == words[j]) distinct = false;
    }
    const std::wstring unknown = RoleWord(L"founder");
    demo::MemberRef mine;
    mine.displayName = demo::kUnavailable;
    mine.role = demo::kRoleOwner;
    mine.mine = true;
    demo::MemberRef theirs = mine;
    theirs.displayName = L"Mira Okonkwo";
    theirs.role = demo::kRoleObserver;
    theirs.mine = false;
    const std::wstring mineTitle = MemberRowTitle(mine);
    const std::wstring theirsTitle = MemberRowTitle(theirs);
    const bool titlesOk = mineTitle.starts_with(L"You") &&
                          mineTitle.find(demo::kUnavailable) == std::wstring::npos &&
                          mineTitle.find(words[0]) != std::wstring::npos &&
                          theirsTitle.starts_with(L"Mira Okonkwo") &&
                          theirsTitle.find(words[3]) != std::wstring::npos;
    const bool ok = distinct && nonEmpty && unknown == demo::kUnavailable && titlesOk;
    out.push_back(std::format(
        L"  roster words     : {}  owner \"{}\" | admin \"{}\" | member \"{}\" | observer \"{}\" | "
        L"unknown \"{}\"; own row \"{}\" | other \"{}\"   [query: four distinct non-empty words, "
        L"none the placeholder; an unknown role is the placeholder; the viewer's own row leads "
        L"with You and carries its role word; another's row leads with its name]",
        Verdict(ok), words[0], words[1], words[2], words[3], unknown, mineTitle, theirsTitle));
  }

  // 2. the control set per own role, over every (viewer role, member role) pair and the own row.
  //    Printed as the table it is, so a reader can check it against MASTER section 11 rather
  //    than trusting the count.
  {
    auto label = [](std::vector<RoleVerb> const& verbs) {
      std::wstring s;
      for (auto v : verbs) {
        if (!s.empty()) s += L"/";
        s += RoleControlLabel(v);
      }
      return s.empty() ? std::wstring(L"none") : s;
    };
    auto same = [](std::vector<RoleVerb> const& a, std::vector<RoleVerb> const& b) {
      return a == b;
    };
    using V = RoleVerb;
    struct Case {
      wchar_t const* mine;
      wchar_t const* theirs;
      std::vector<V> want;
    };
    // REMOVE IS LAST ON EVERY ROW THAT HAS ANY CONTROL AND IS ABSENT FROM EVERY ROW THAT HAS
    // NONE, which is the whole of item 242 R2 and R3 as a table. The two rows that matter most
    // are the ones where it must NOT appear and a reader would expect it to: an admin looking at
    // another ADMIN gets nothing (R3 - the admin set is the owner's alone, and removal is part of
    // that set), and anybody looking at the OWNER gets nothing.
    const Case cases[] = {
        {demo::kRoleOwner, demo::kRoleMember,
         {V::MakeAdmin, V::MakeObserver, V::TransferOwnership, V::Remove}},
        {demo::kRoleOwner, demo::kRoleAdmin,
         {V::MakeMember, V::MakeObserver, V::TransferOwnership, V::Remove}},
        {demo::kRoleOwner, demo::kRoleObserver,
         {V::MakeAdmin, V::MakeMember, V::TransferOwnership, V::Remove}},
        {demo::kRoleOwner, demo::kRoleOwner, {}},
        {demo::kRoleAdmin, demo::kRoleMember, {V::MakeObserver, V::Remove}},
        {demo::kRoleAdmin, demo::kRoleObserver, {V::MakeMember, V::Remove}},
        {demo::kRoleAdmin, demo::kRoleAdmin, {}},
        {demo::kRoleAdmin, demo::kRoleOwner, {}},
        {demo::kRoleMember, demo::kRoleMember, {}},
        {demo::kRoleMember, demo::kRoleObserver, {}},
        {demo::kRoleObserver, demo::kRoleMember, {}},
        {demo::kUnavailable, demo::kRoleMember, {}},
    };
    size_t agree = 0;
    std::wstring table;
    for (auto const& c : cases) {
      const auto got = RoleControlsFor(c.mine, c.theirs, /*theirRowIsMine=*/false);
      if (same(got, c.want)) ++agree;
      if (!table.empty()) table += L"; ";
      table += std::format(L"{} over {} -> {}", c.mine, c.theirs, label(got));
    }
    // The own row: nothing, whatever the roles.
    const bool ownRowEmpty =
        RoleControlsFor(demo::kRoleOwner, demo::kRoleOwner, true).empty() &&
        RoleControlsFor(demo::kRoleAdmin, demo::kRoleAdmin, true).empty() &&
        RoleControlsFor(demo::kRoleOwner, demo::kRoleMember, true).empty();
    // Every verb has a non-empty label and the five labels differ.
    const std::wstring labels[] = {RoleControlLabel(V::MakeAdmin), RoleControlLabel(V::MakeMember),
                                   RoleControlLabel(V::MakeObserver),
                                   RoleControlLabel(V::TransferOwnership),
                                   RoleControlLabel(V::Remove)};
    bool labelsOk = true;
    for (size_t i = 0; i < std::size(labels); ++i) {
      if (labels[i].empty()) labelsOk = false;
      for (size_t j = 0; j < i; ++j)
        if (labels[i] == labels[j]) labelsOk = false;
    }
    // THE ROLE A VERB ASKS FOR, AND THE ONE THAT ASKS FOR NONE. RoleVerbTarget's default arm
    // answers kRoleMember, so a Remove that fell through to it would read as a demotion AND would
    // be spent on urnet_message_group_set_role by any caller that did not branch first. Asserted
    // BOTH WAYS: the four role verbs answer a non-empty, distinct spelling, and Remove answers
    // empty. A gate that only checked the four would pass the exact defect.
    const std::wstring targets[] = {
        std::wstring(RoleVerbTarget(V::MakeAdmin)), std::wstring(RoleVerbTarget(V::MakeMember)),
        std::wstring(RoleVerbTarget(V::MakeObserver)),
        std::wstring(RoleVerbTarget(V::TransferOwnership))};
    bool targetsOk = RoleVerbTarget(V::Remove).empty();
    for (size_t i = 0; i < std::size(targets); ++i) {
      if (targets[i].empty()) targetsOk = false;
      for (size_t j = 0; j < i; ++j)
        if (targets[i] == targets[j]) targetsOk = false;
    }
    const bool ok = agree == std::size(cases) && ownRowEmpty && labelsOk && targetsOk;
    out.push_back(std::format(
        L"  roster controls  : {}  {}/{} (viewer, member) pairs give MASTER section 11's set, own "
        L"row empty {}, labels distinct {}, role targets {} -> {}   [query: owner gets Make "
        L"admin/member/observer minus the member's current role plus Transfer ownership; admin gets "
        L"Make member/observer minus current, nothing on an admin or the owner; member/observer/"
        L"unknown get nothing; nothing on one's own row; Remove LAST on every row that has any "
        L"control and on no row that has none; the four role verbs name a distinct non-empty role "
        L"and Remove names none]",
        Verdict(ok), agree, std::size(cases), ownRowEmpty ? L"yes" : L"NO",
        labelsOk ? L"yes" : L"NO", targetsOk ? L"yes" : L"NO", table));
  }

  // 3. the control names in ALL THREE arms, on the capability, like the bubble actions - and the
  //    ENFORCEMENT CLAIM in EXACTLY ONE CELL of the twelve.
  //
  //    THE HISTORY THIS CLAUSE IS MADE OF. The first cut of the observer control said "who can
  //    read but not send", passed this check (it forbade session denials, not enforcement claims)
  //    and shipped a gate the build did not have: no send path read the role. The second cut had
  //    two arms, so the PENDING control - dark because the app is inside the library right then -
  //    announced "there is no live session", denying a session at the one moment it is provably
  //    live. Both escapes were wordings this check allowed.
  //
  //    ITEM 242 R4 INVERTS THE ENFORCEMENT LIST RATHER THAN RELAXING IT, and the difference is the
  //    whole point. R4 built the gate: the composer and the sdk both refuse an observer's send, so
  //    "who can read this group but not send to it" is now TRUE and the Make-observer control must
  //    SAY it - a control that changed someone's role without naming what the role does would be
  //    the same omission from the other side. Deleting the word list would have been the
  //    relaxation, and it would have left nothing watching the eleven cells where such a claim is
  //    still false. So the property is now a POSITION: the claim belongs to the (Make observer,
  //    LIVE) cell and to no other. The pending and dark arms are about the CONTROL's state - it is
  //    waiting, there is no session - and a sentence about what the group enforces, read out
  //    there, is the R3 mistiming defect wearing the other mode's clothes.
  //
  //    The other three live arms are held to NONE as well, which is stricter than "allowed
  //    anywhere live": a true claim on Make member ("who can read and send") would fail this and
  //    be a deliberate one-line edit naming that cell, which is exactly how this list is meant to
  //    grow. Like RunMode's kSendDenials it is an admitted blacklist, kept worth having by WHEN it
  //    runs (every launch, against the compiled string) and by PRINTING the whole 4x3 grid.
  {
    constexpr std::wstring_view kDenials[] = {L"no live session", L"not available", L"cannot"};
    constexpr std::wstring_view kEnforcementClaims[] = {L"not send", L"read but", L"read-only",
                                                        L"read only", L"can read"};
    auto denies = [&](std::wstring const& s) {
      for (auto const& d : kDenials)
        if (s.find(d) != std::wstring::npos) return true;
      return false;
    };
    auto claimsIn = [&](std::wstring const& s) {
      std::wstring found;
      for (auto const& c : kEnforcementClaims) {
        if (s.find(c) == std::wstring::npos) continue;
        if (!found.empty()) found += L", ";
        found += c;
      }
      return found;
    };
    size_t ok = 0;
    std::wstring listing;
    for (RoleVerb v : {RoleVerb::MakeAdmin, RoleVerb::MakeMember, RoleVerb::MakeObserver,
                       RoleVerb::TransferOwnership, RoleVerb::Remove}) {
      const std::wstring on = RoleControlName(v, RoleControlState::Live);
      const std::wstring waiting = RoleControlName(v, RoleControlState::Pending);
      const std::wstring off = RoleControlName(v, RoleControlState::NoSession);
      const std::wstring onClaims = claimsIn(on);
      const std::wstring waitClaims = claimsIn(waiting);
      const std::wstring offClaims = claimsIn(off);
      // THE INVERSION, per verb: Make observer's LIVE arm must carry a claim; every other cell of
      // the twelve must carry none.
      const bool wantsClaim = (v == RoleVerb::MakeObserver);
      const bool claimOk = (wantsClaim == !onClaims.empty()) && waitClaims.empty() &&
                           offClaims.empty();
      if (!on.empty() && !waiting.empty() && !off.empty() && on != off && on != waiting &&
          waiting != off && !denies(on) && !denies(waiting) &&
          off.find(L"no live session") != std::wstring::npos && claimOk)
        ++ok;
      if (!listing.empty()) listing += L"; ";
      listing += std::format(L"live \"{}\" [claims: {}] | pending \"{}\" [claims: {}] | dark "
                             L"\"{}\" [claims: {}]",
                             on, onClaims.empty() ? L"none" : onClaims, waiting,
                             waitClaims.empty() ? L"none" : waitClaims, off,
                             offClaims.empty() ? L"none" : offClaims);
    }
    out.push_back(std::format(
        L"  roster names     : {}  {}/5 controls have three distinct arms, a live and a pending "
        L"arm that deny no session, a dark arm that names the missing one, and an enforcement "
        L"claim in EXACTLY the (Make observer, live) cell -> {}   [query: three distinct non-empty "
        L"arms; NEITHER the live nor the pending arm contains \"no live session\", \"not "
        L"available\", \"cannot\" - the pending arm is read out while the app is inside the "
        L"library, so a session denial there is false; dark arm contains \"no live session\"; of "
        L"the words \"not send\", \"read but\", \"read-only\", \"read only\", \"can read\", the "
        L"Make-observer LIVE arm must carry at least one - item 242 R4 built the gate, so the "
        L"control must say what the role does - and the other FOURTEEN cells must carry none, "
        L"Remove's three included: what Remove does is not a gate on reading]",
        Verdict(ok == 5), ok, listing));
  }

  // 3b. THE TWO CONFIRMATIONS, which had no gate at all until a removal needed one. Both guard a
  //     change this product cannot undo, and the property that makes them safe is not the wording:
  //     it is that the CLOSE button is the harmless arm and is the dialog's default, so a stray
  //     Enter keeps things as they are. That half lives in InspectRailView.cpp's
  //     DefaultButton(ContentDialogButton::Close) and is not reachable from here. What IS reachable
  //     is that all four strings exist, that the two dialogs are told apart by every one of them,
  //     and that each CLOSE arm names KEEPING rather than cancelling - "Cancel" beside a title
  //     asking "Remove this member?" is the one wording where a hurried reader cannot tell which
  //     button does nothing.
  {
    struct Confirm {
      wchar_t const* what;
      std::wstring title, body, primary, close;
      wchar_t const* keeps;
    };
    const Confirm pair[] = {
        {L"transfer", TransferConfirmTitle(), TransferConfirmBody(), TransferConfirmPrimary(),
         TransferConfirmClose(), L"Keep"},
        {L"remove", RemoveConfirmTitle(), RemoveConfirmBody(), RemoveConfirmPrimary(),
         RemoveConfirmClose(), L"Keep"},
    };
    size_t good = 0;
    std::wstring listing;
    for (auto const& c : pair) {
      const bool filled = !c.title.empty() && !c.body.empty() && !c.primary.empty() &&
                          !c.close.empty();
      const bool distinct = c.primary != c.close && c.title != c.body;
      const bool keeps = c.close.find(c.keeps) != std::wstring::npos;
      // The body must say what cannot be taken back, in its own words rather than by being short.
      const bool irreversible = c.body.find(L"Only the new owner") != std::wstring::npos ||
                                c.body.find(L"cannot add them back") != std::wstring::npos;
      if (filled && distinct && keeps && irreversible) ++good;
      if (!listing.empty()) listing += L"; ";
      listing += std::format(L"{}: \"{}\" primary \"{}\" close \"{}\"", c.what, c.title,
                             c.primary, c.close);
    }
    // And the two dialogs share NO string, so neither can be mistaken for the other on screen.
    const bool disjoint = pair[0].title != pair[1].title && pair[0].body != pair[1].body &&
                          pair[0].primary != pair[1].primary && pair[0].close != pair[1].close;
    out.push_back(std::format(
        L"  roster confirms  : {}  {}/2 confirmations are filled, have a close arm that names "
        L"keeping, and say in the body what cannot be undone; the two share no string {} -> {}   "
        L"[query: title/body/primary/close all non-empty; primary != close; close contains "
        L"\"Keep\"; body names the irreversibility (\"Only the new owner\" / \"cannot add them "
        L"back\"); and all four strings differ between the two dialogs]",
        Verdict(good == 2 && disjoint), good, disjoint ? L"yes" : L"NO", listing));
  }

  // THE COMPOSER'S TEXT LIMIT (ledger 266, item 6): the UTF-8 count is exact at all four encoded
  // widths and for a lone surrogate, it agrees with the narrowing the send path really uses, the two
  // limits are the library's (also static_asserted in ThreadLayout.cpp), and the caption names both
  // numbers, grouped.
  {
    struct Width {
      std::wstring text;
      size_t want;
      wchar_t const* what;
    };
    const Width widths[] = {
        {L"hello", 5, L"ascii"},
        {L"\u00e9", 2, L"two"},
        {L"\u20ac", 3, L"three"},
        {L"\U0001F600", 4, L"pair"},
        {std::wstring(1, static_cast<wchar_t>(0xD800)), 3, L"lone"},
    };
    size_t exact = 0;
    std::wstring listing;
    for (auto const& w : widths) {
      const size_t got = urmsg::views::Utf8Octets(w.text);
      if (got == w.want) ++exact;
      listing += std::format(L"{}={} ", w.what, got);
    }
    const std::wstring mixed = L"a\u00e9\u20ac\U0001F600z";
    const bool agrees = urnw::Narrow(mixed).size() == urmsg::views::Utf8Octets(mixed);
    const bool limits = urmsg::views::ComposerTextLimit(false) == 65333 &&
                        urmsg::views::ComposerTextLimit(true) == 65301;
    const std::wstring note = urmsg::views::ComposerTooLongNote(70112, 65333);
    const bool named = note.find(L"70,112") != std::wstring::npos &&
                       note.find(L"65,333") != std::wstring::npos;
    // the dark button SAYS why for MaySend only; the other two states keep their own reasons
    using urmsg::views::ComposerSendName;
    using urmsg::views::ComposerSendNameAt;
    const bool spoken =
        ComposerSendNameAt(ComposerState::MaySend, true, false, true).find(L"too long") !=
            std::wstring::npos &&
        ComposerSendNameAt(ComposerState::MaySend, true, false, false) ==
            ComposerSendName(ComposerState::MaySend, true, false) &&
        ComposerSendNameAt(ComposerState::ObserverOnly, true, false, true) ==
            ComposerSendName(ComposerState::ObserverOnly, true, false) &&
        ComposerSendNameAt(ComposerState::NoSession, true, false, true) ==
            ComposerSendName(ComposerState::NoSession, true, false);
    out.push_back(std::format(
        L"  composer limit   : {}  {}/5 widths counted exactly ({}); agrees with Narrow on a mixed "
        L"string {}; limits 65,333/65,301 {}; caption \"{}\"; the dark button says why {}   "
        L"[query: Utf8Octets of ascii, U+00E9, U+20AC, U+1F600 and a lone high surrogate is 5, 2, 3, "
        L"4, 3; equal to Narrow().size() on a mixed string; ComposerTextLimit(false/true) is "
        L"65333/65301; the note names both numbers comma-grouped; ComposerSendNameAt says \"too "
        L"long\" for MaySend past the limit and is ComposerSendName otherwise and in the other "
        L"two states]",
        Verdict(exact == 5 && agrees && limits && named && spoken), exact, listing,
        agrees ? L"yes" : L"NO", limits ? L"yes" : L"NO", note, spoken ? L"yes" : L"NO"));
  }

  // THE LABELS (Live/LocalNames.h): the rule a label must pass, the file it is kept in, and the
  // overlay that lays it on a live world. A label is the viewer's own name for an identity or a
  // conversation, kept on this computer and never sent, so the overlay may write LABELS AND NOTHING
  // ELSE, and it attributes a line by the identity that SIGNED it, never by the leaf's handle. This
  // line prints verdicts and counts, never a label: W10 keeps names out of the log, test ones too.
  {
    using namespace urmsg::live;
    // (a) the rule: trimmed, bounded in UTF-8 octets at both encoded widths, controls refused
    struct Case {
      std::wstring typed;
      LocalNameVerdict want;
      std::size_t octets;  // checked on Ok and TooLong
      wchar_t const* what;
    };
    std::wstring sixteenPairs;
    for (int i = 0; i < 16; ++i) sixteenPairs += L"\U0001F600";
    const Case cases[] = {
        {L"  Ravi  ", LocalNameVerdict::Ok, 4, L"trimmed"},
        {L"", LocalNameVerdict::Empty, 0, L"empty"},
        {L" \t\u3000 ", LocalNameVerdict::Empty, 0, L"blank"},
        {std::wstring(64, L'a'), LocalNameVerdict::Ok, 64, L"64"},
        {std::wstring(65, L'a'), LocalNameVerdict::TooLong, 65, L"65"},
        {sixteenPairs, LocalNameVerdict::Ok, 64, L"16pairs"},
        {sixteenPairs + L"\U0001F600", LocalNameVerdict::TooLong, 68, L"17pairs"},
        {L"\u00c9lise", LocalNameVerdict::Ok, 6, L"accented"},
        {L"Ra\nvi", LocalNameVerdict::ControlCharacter, 0, L"newline"},
        {L"Ra\u0085vi", LocalNameVerdict::ControlCharacter, 0, L"C1"},
        {L"Ra\u202Evi", LocalNameVerdict::ControlCharacter, 0, L"override"},
        {L"Ra\u2067vi", LocalNameVerdict::ControlCharacter, 0, L"isolate"},
    };
    std::size_t ruled = 0;
    std::wstring misruled;
    for (auto const& c : cases) {
      const LocalNameCheck got = CheckLocalName(c.typed);
      const bool counted = c.want != LocalNameVerdict::Ok && c.want != LocalNameVerdict::TooLong;
      if (got.verdict == c.want && (counted || got.octets == c.octets)) {
        ++ruled;
      } else {
        misruled += std::format(L"{} ", c.what);
      }
    }
    const bool trimmedTo = CheckLocalName(L"  Ravi  ").utf8 == "Ravi";
    const bool boundNamed =
        LocalNameRefusal(CheckLocalName(std::wstring(65, L'a'))).find(L"65 of 64") !=
        std::wstring::npos;

    // (b) the file: a round trip, damage dropped entry by entry, garbage is no labels, and a
    //     second save REPLACES the first and leaves no temporary file behind
    const std::string idA(64, 'a'), idB(64, 'b'), idC(64, 'c'), idMe(64, 'e'), gid(32, 'd');
    LocalNames labels;
    labels.members[idA] = "Ravi";
    labels.members[idB] = "Bo";
    labels.members[idC] = "Former";
    labels.conversations[gid] = "Design";
    const LocalNames back = ParseLocalNames(SerializeLocalNames(labels));
    const bool roundTrip = back.members == labels.members && back.conversations == labels.conversations;
    const LocalNames damaged = ParseLocalNames(
        R"({"version":1,"members":{"AA":"upper","aa":" untrimmed","bb":7,"cc":"fine"},"conversations":"x"})");
    const bool damageDropped = damaged.members.size() == 1 && damaged.members.count("cc") == 1 &&
                               damaged.conversations.empty();
    const bool garbageIsNone = ParseLocalNames("not json").members.empty();
    std::error_code fsError;
    const auto dir = std::filesystem::temp_directory_path() / L"urmsg-labels-gate";
    std::filesystem::remove_all(dir, fsError);
    const auto file = dir / L"local_names.json";
    LocalNames first;
    first.members[idA] = "First";
    const bool saved = SaveLocalNamesTo(file, first) && SaveLocalNamesTo(file, labels);
    const LocalNames loaded = LoadLocalNamesFrom(file);
    const bool fileOk = saved && loaded.members == labels.members &&
                        loaded.conversations == labels.conversations &&
                        !std::filesystem::exists(dir / L"local_names.json.tmp");
    std::filesystem::remove_all(dir, fsError);

    // (c) the mapping and the overlay, over a group built here: this device, A and B on the
    //     roster, and three incoming lines. C was REMOVED and a newcomer took C's leaf, so C's
    //     line carries the HANDLE that B holds now: attributed by identity it is C's, by handle B's.
    auto member = [](std::string handle, std::string identity, bool mine) {
      LiveMember m;
      m.senderHandle = std::move(handle);
      m.identityPub = std::move(identity);
      m.role = mine ? "owner" : "member";
      m.mine = mine;
      return m;
    };
    auto line = [](char id, std::string handle, std::string identity) {
      LiveMessage m;
      m.messageId = std::string(64, id);
      m.senderHandle = std::move(handle);
      m.senderIdentity = std::move(identity);
      m.kind = 0x01;  // URNET_MESSAGE_KIND_TEXT, by value: this TU does not include the ABI header
      m.sentAtMs = 1'700'000'000'000;
      m.body = "x";
      m.bodyLen = 1;
      return m;
    };
    LiveGroup g;
    g.groupIdHex = gid;
    g.epoch = 2;
    g.open = true;
    g.myRole = "owner";
    g.members.push_back(member(std::string(32, '0'), idMe, true));
    g.members.push_back(member(std::string(32, '1'), idA, false));
    g.members.push_back(member(std::string(32, '2'), idB, false));
    g.messages.push_back(line('1', std::string(32, '1'), idA));
    g.messages.push_back(line('2', std::string(32, '2'), idB));
    g.messages.push_back(line('3', std::string(32, '2'), idC));
    const demo::World raw = BuildWorld(g);
    const demo::Conversation* rc = raw.conversations.empty() ? nullptr : &raw.conversations.front();
    const bool grouped = rc && rc->kind == demo::ConversationKind::Group;
    // ONE FACE: every line's identicon is its signer's roster identicon. Seeded off the handle,
    // C's line would wear B's face.
    std::vector<demo::MessageRow const*> lines3;
    if (rc)
      for (auto const& r : rc->rows)
        if (r.kind == demo::RowKind::Message) lines3.push_back(&r);
    auto rosterFace = [&](std::wstring const& identity) -> demo::Seed const* {
      if (!rc) return nullptr;
      for (auto const& m : rc->members)
        if (m.identityPubHex == identity) return &m.identityKey;
      return nullptr;
    };
    const bool faces = lines3.size() == 3 && rosterFace(urnw::Widen(idA)) &&
                       lines3[0]->senderKey == *rosterFace(urnw::Widen(idA)) &&
                       lines3[1]->senderKey == *rosterFace(urnw::Widen(idB)) &&
                       lines3[2]->senderKey != *rosterFace(urnw::Widen(idB));

    const demo::World named = WithLocalNames(raw, labels);
    const demo::Conversation* nc = named.conversations.empty() ? nullptr : &named.conversations.front();
    std::vector<demo::MessageRow const*> nlines;
    if (nc)
      for (auto const& r : nc->rows)
        if (r.kind == demo::RowKind::Message) nlines.push_back(&r);
    const bool attributed = nlines.size() == 3 && nlines[0]->senderName == L"Ravi" &&
                            nlines[1]->senderName == L"Bo" && nlines[2]->senderName == L"Former" &&
                            nlines[2]->senderLocalName == L"Former";
    bool protocolKept = nc != nullptr;
    if (nc) {
      for (auto const& m : nc->members)
        if (m.displayName != demo::kUnavailable) protocolKept = false;
      for (auto const* r : nlines)
        if (r->inspect.senderDisplayName != demo::kUnavailable) protocolKept = false;
    }
    std::size_t labelled = 0;
    if (nc)
      for (auto const& m : nc->members)
        if (!m.localName.empty()) ++labelled;
    const bool conversationOwn = nc && nc->name == L"Design" && nc->localName == L"Design";
    // A TWO-PARTY conversation is called what you call the other party, and its own label wins.
    LiveGroup pair = g;
    pair.members.pop_back();
    pair.messages.clear();
    LocalNames noConversation = labels;
    noConversation.conversations.clear();
    const demo::World pairNamed = WithLocalNames(BuildWorld(pair), noConversation);
    const demo::World pairOwn = WithLocalNames(BuildWorld(pair), labels);
    const bool direct =
        !pairNamed.conversations.empty() &&
        pairNamed.conversations.front().kind == demo::ConversationKind::Direct &&
        pairNamed.conversations.front().name == L"Ravi" && !pairOwn.conversations.empty() &&
        pairOwn.conversations.front().name == L"Design";
    // NOTHING ELSE MOVES: no labels is the world exactly as it was.
    const bool inert = WorldFingerprint(WithLocalNames(raw, LocalNames{})) == WorldFingerprint(raw);

    // (d) the words: whose name it is, and that it is never sent
    const std::wstring dialogNote = urmsg::views::LocalNameDialogNote();
    const bool words = dialogNote.find(L"Only you") != std::wstring::npos &&
                       dialogNote.find(L"never sent") != std::wstring::npos &&
                       urmsg::views::LocalNameMarked(L"X").find(L"your name") != std::wstring::npos;

    const bool ok = ruled == std::size(cases) && trimmedTo && boundNamed && roundTrip &&
                    damageDropped && garbageIsNone && fileOk && grouped && faces && attributed &&
                    protocolKept && labelled == 2 && conversationOwn && direct && inert && words;
    out.push_back(std::format(
        L"  local names      : {}  rule {}/{} {}; trimmed {}; bound named {}; file round trip {}, "
        L"damage dropped {}, garbage none {}, replace-and-no-temp {}; 3-member live group is a "
        L"Group {}; one face per signer {}; lines attributed by identity {}; protocol names kept "
        L"{}; {} members labelled; conversation's own {}; two-party {}; inert without labels {}; "
        L"words {}   [query: CheckLocalName over 12 cases (trim, empty, blank, 64/65 ascii, 16/17 "
        L"surrogate pairs, accented, newline, C1, override, isolate); Parse(Serialize) equal; a "
        L"damaged file keeps only its one good entry; SaveLocalNamesTo twice then Load; BuildWorld of "
        L"3 members is Group; each line's senderKey is its signer's roster identityKey and C's is "
        L"not B's; WithLocalNames names A, B and REMOVED C by sender_identity although C's handle is "
        L"B's; displayName and senderDisplayName stay unavailable; the conversation label wins; a "
        L"pair takes the other member's label; no labels leaves the fingerprint unchanged]",
        Verdict(ok), ruled, std::size(cases), misruled.empty() ? L"" : misruled,
        trimmedTo ? L"yes" : L"NO", boundNamed ? L"yes" : L"NO", roundTrip ? L"yes" : L"NO",
        damageDropped ? L"yes" : L"NO", garbageIsNone ? L"yes" : L"NO", fileOk ? L"yes" : L"NO",
        grouped ? L"yes" : L"NO", faces ? L"yes" : L"NO", attributed ? L"yes" : L"NO",
        protocolKept ? L"yes" : L"NO", labelled, conversationOwn ? L"yes" : L"NO",
        direct ? L"yes" : L"NO", inert ? L"yes" : L"NO", words ? L"yes" : L"NO"));
  }

  // 4. the outcome notes: nothing for None, and five distinct sentences for the five states -
  //    the three a person acts on differently (refused by role, lost the race, transport) being
  //    told apart by their own words, and the two that carry the library's reason carrying it.
  {
    auto make = [](demo::RoleActionState s, wchar_t const* verb, wchar_t const* reason) {
      demo::MemberRef m;
      m.roleAction = s;
      m.roleActionVerb = verb;
      m.roleActionReason = reason;
      return m;
    };
    const std::wstring none = RoleActionNote(make(demo::RoleActionState::None, demo::kRoleAdmin, L""));
    const std::wstring pending =
        RoleActionNote(make(demo::RoleActionState::Pending, demo::kRoleAdmin, L""));
    const std::wstring pendingTransfer =
        RoleActionNote(make(demo::RoleActionState::Pending, demo::kRoleOwner, L""));
    const std::wstring refused = RoleActionNote(
        make(demo::RoleActionState::Refused, demo::kRoleAdmin, L"the rule's own sentence"));
    const std::wstring lost =
        RoleActionNote(make(demo::RoleActionState::Lost, demo::kRoleAdmin, L"epoch stale"));
    const std::wstring invalid = RoleActionNote(
        make(demo::RoleActionState::Invalid, demo::kRoleAdmin, L"not a role"));
    const std::wstring failed = RoleActionNote(
        make(demo::RoleActionState::Failed, demo::kRoleAdmin, L"the transport failed"));
    const std::wstring notes[] = {pending, refused, lost, invalid, failed};
    bool distinct = true;
    bool nonEmpty = true;
    for (size_t i = 0; i < 5; ++i) {
      if (notes[i].empty()) nonEmpty = false;
      for (size_t j = 0; j < i; ++j)
        if (notes[i] == notes[j]) distinct = false;
    }
    const bool ok = none.empty() && nonEmpty && distinct && pending != pendingTransfer &&
                    refused.find(L"the rule's own sentence") != std::wstring::npos &&
                    refused.find(L"role") != std::wstring::npos &&
                    lost.find(L"someone else changed the group first") != std::wstring::npos &&
                    lost.find(L"try again") != std::wstring::npos &&
                    failed.find(L"the transport failed") != std::wstring::npos &&
                    invalid.find(L"not a role") != std::wstring::npos &&
                    pending.find(RoleWord(demo::kRoleAdmin)) != std::wstring::npos;
    out.push_back(std::format(
        L"  roster outcomes  : {}  none \"{}\" | pending \"{}\" | pending transfer \"{}\" | refused "
        L"\"{}\" | lost \"{}\" | invalid \"{}\" | failed \"{}\"   [query: None is empty; the five "
        L"states are five distinct non-empty sentences; a transfer's pending differs from a role's; "
        L"refused names the role and carries the reason; lost says someone else changed the group "
        L"first and try again; invalid and failed carry the reason]",
        Verdict(ok), none, EscapeNonAscii(pending), EscapeNonAscii(pendingTransfer),
        EscapeNonAscii(refused), EscapeNonAscii(lost), EscapeNonAscii(invalid),
        EscapeNonAscii(failed)));
  }

  // 5. the live mapping, over a roster built here: two leaves (this device the owner, the peer a
  //    member), a lost change on the peer, and a stale answered entry the newest one supersedes.
  {
    using namespace urmsg::live;
    LiveGroup g;
    g.groupIdHex = "00";
    g.epoch = 3;
    g.open = true;
    g.myRole = "owner";
    LiveMember me;
    me.leafIndex = 0;
    me.senderHandle = std::string(32, 'a');
    me.identityPub = std::string(64, '1');
    me.role = "owner";
    me.mine = true;
    LiveMember peer;
    peer.leafIndex = 1;
    peer.senderHandle = std::string(32, 'b');
    peer.identityPub = std::string(64, '2');
    peer.role = "member";
    peer.mine = false;
    g.members.push_back(me);
    g.members.push_back(peer);
    LiveRoleOutboxEntry stale;
    stale.localId = "1";
    stale.identityPub = peer.identityPub;
    stale.role = "admin";
    stale.done = true;
    stale.kind = 4;  // URNET_MESSAGE_COMMIT_FAILED, by value: this TU does not include the ABI header
    stale.error = "an earlier transport failure";
    LiveRoleOutboxEntry lost;
    lost.localId = "2";
    lost.identityPub = peer.identityPub;
    lost.role = "admin";
    lost.done = true;
    lost.kind = 2;  // URNET_MESSAGE_COMMIT_LOST
    lost.error = "epoch stale";
    g.roleOutbox.push_back(stale);
    g.roleOutbox.push_back(lost);

    const demo::World w = BuildWorld(g);
    const demo::Conversation* conv = w.conversations.empty() ? nullptr : &w.conversations.front();
    const demo::MemberRef* mineRow = nullptr;
    const demo::MemberRef* peerRow = nullptr;
    if (conv) {
      for (auto const& m : conv->members) {
        if (m.mine) mineRow = &m;
        else peerRow = &m;
      }
    }
    const bool twoRows = conv && conv->members.size() == 2 && conv->memberCount == 2 && mineRow &&
                         peerRow;
    const bool myRoleOk = conv && conv->myRole == demo::kRoleOwner;
    const bool mineOk = mineRow && mineRow->role == demo::kRoleOwner &&
                        mineRow->displayName == demo::kUnavailable &&
                        mineRow->identityPubHex == std::wstring(64, L'1') &&
                        mineRow->devices.empty() &&
                        mineRow->roleAction == demo::RoleActionState::None &&
                        MemberRowTitle(*mineRow).starts_with(L"You");
    const bool peerOk = peerRow && peerRow->role == demo::kRoleMember &&
                        peerRow->displayName == demo::kUnavailable &&
                        peerRow->identityPubHex == std::wstring(64, L'2') &&
                        peerRow->roleAction == demo::RoleActionState::Lost &&
                        peerRow->roleActionVerb == demo::kRoleAdmin &&
                        peerRow->roleActionReason == L"epoch stale" &&
                        peerRow->id == std::wstring(32, L'b');
    // THE SET AND NOT ITS SIZE. This read `.size() == 3` until a fifth verb was added, and a
    // count is a claim with its query thrown away: it goes red when a verb is added (which is how
    // this line was found) but it would go GREEN if one verb were swapped for another, which is
    // the change a count cannot see. The expected set is this device's own row for an owner
    // looking at a member: promote, make observer, hand over the group, take them out.
    const std::vector<RoleVerb> wantPeer = {RoleVerb::MakeAdmin, RoleVerb::MakeObserver,
                                            RoleVerb::TransferOwnership, RoleVerb::Remove};
    const bool controlsOk =
        conv && peerRow && mineRow &&
        RoleControlsFor(conv->myRole, peerRow->role, peerRow->mine) == wantPeer &&
        RoleControlsFor(conv->myRole, mineRow->role, mineRow->mine).empty();
    const std::wstring caption = conv ? MembersCaptionMeta(*conv) : std::wstring();
    const bool captionOk = caption.find(L"2 members") != std::wstring::npos &&
                           caption.find(L"online") == std::wstring::npos &&
                           caption.find(L"unavailable") != std::wstring::npos;
    const bool ok = twoRows && myRoleOk && mineOk && peerOk && controlsOk && captionOk;
    out.push_back(std::format(
        L"  roster live      : {}  BuildWorld over 2 leaves gives {} rows (memberCount {}), my role "
        L"\"{}\", own row {} (You, owner, no name, no devices, key kept), peer row {} (member, no "
        L"name, newest outcome Lost with the library's reason, id = sender handle), controls {} "
        L"(3 on the peer, 0 on own row), caption \"{}\" {}   [query: the roster is transcribed row "
        L"for row with the placeholder for every name and no device list; the newest role-outbox "
        L"entry naming an identity is the one drawn; presence is never claimed]",
        Verdict(ok), conv ? conv->members.size() : 0, conv ? conv->memberCount : -1,
        conv ? conv->myRole : std::wstring(L"-"), mineOk ? L"ok" : L"WRONG",
        peerOk ? L"ok" : L"WRONG", controlsOk ? L"ok" : L"WRONG", caption,
        captionOk ? L"ok" : L"WRONG"));
  }
  return out;
}

// ---- the status strip's pure rules (design §6.5) ----------------------------
//
// ONE line, per the d7 audit's S1 override: the 560 content-dip collapse rule
// has exactly one owner already (urmsg::demo::kStripMinHeightDip,
// DemoShellState.h:41) and one boundary assertion already (DemoLayoutCheck's
// stripEdge term above), so there is no `strip collapse` line here and
// StatusStripRules.h declares no second threshold. What remains is what only
// this surface owns: the three state words, the two lock glyphs and the two
// Advanced Mode number formats.
//
// PURE on the same terms as the rest of this function: it builds no demo
// world (the enum values are constructed, GetWorld() is never called),
// touches no XAML and calls no Localized(), so it is as safe on a plain
// launch as under --diagnose — which it has to be, because LogDiagnostics()
// writes every one of these lines into a normal launch's log file.
std::wstring StatusStripFieldsAssertion() {
  namespace demo = urmsg::demo;
  const std::wstring_view offline =
      urmsg::views::StatusStateWord(demo::ConnectState::Offline);
  const std::wstring_view connecting =
      urmsg::views::StatusStateWord(demo::ConnectState::Connecting);
  const std::wstring_view connected =
      urmsg::views::StatusStateWord(demo::ConnectState::Connected);
  const std::wstring lockOn = urmsg::views::StatusLockGlyph(true);
  const std::wstring lockOff = urmsg::views::StatusLockGlyph(false);
  const std::wstring epoch = urmsg::views::StatusEpochValue(41);
  const std::wstring records = urmsg::views::StatusRecordsValue(12);

  // Compared against LITERALS, not restated constants, so a wrong answer fails
  // here rather than agreeing with itself (the same rule DemoLayoutCheck
  // states for its list-step figures).
  const bool wordsOk = offline == L"Offline" && connecting == L"Connecting" &&
                       connected == L"Connected";
  // e72e = Lock (closed padlock), e785 = Unlock (open padlock), Segoe Fluent
  // Icons — escapes, never the pasted PUA character (a dropped one renders
  // blank in every terminal and is invisible).
  const bool glyphsOk = lockOn == L"\ue72e" && lockOff == L"\ue785";
  const bool valuesOk = epoch == L"41" && records == L"12";
  if (wordsOk && glyphsOk && valuesOk)
    return L"  strip fields     : PASS (3 words, lock e72e/e785, epoch 41, rec 12)";

  // The glyphs are reported as CODE UNITS, not as characters: a private-use
  // codepoint written to a console is an empty box, which is exactly the shape
  // of the defect being looked for.
  const auto code = [](std::wstring const& s) -> unsigned {
    return s.empty() ? 0u : static_cast<unsigned>(s[0]);
  };
  return std::format(
      L"  strip fields     : FAIL (words \"{}\"/\"{}\"/\"{}\", lock {:04x}/{:04x}, "
      L"epoch \"{}\", rec \"{}\")",
      offline, connecting, connected, code(lockOn), code(lockOff), epoch, records);
}

// ---- S3: the relay drawer's rows and the strip fit their own width -----------------------------
//
// PURE, on the same terms as the line above: StatusStripRules.h's decisions, asked with the long
// values of a real live session TYPED HERE (the message server's client_id, Live/LiveMesh.cpp's
// kServerClientId, and the platform url the screenshot rig dials) and with widths that stand in
// for the strip's measured ones. The view turns each answer into XAML; these lines hold the
// answers. Each prints PASS or FAIL, what it asked, and the QUERY.
std::wstring RelayRowFitAssertion() {
  using namespace urmsg::views;
  const RelayRowFit fit = RelayDrawerRowFit();
  const std::wstring id = L"01a0a199-5b06-5117-e86a-4bf5c01db15c";  // 36 characters
  const std::wstring url = L"wss://127.0.0.1:64809/urmessage/v1";     // 34 characters
  const bool labelOk = fit.label == TextCut::None;
  const bool valueOk = fit.value == TextCut::Ellipsis;
  const bool columnsOk = fit.labelSizesToContent;
  // The tooltip and the help text are RelayRowValue itself: whole, with the state in words.
  const std::wstring idValue = RelayRowValue(id, true);
  const std::wstring urlOffline = RelayRowValue(url, false);
  const bool wholeOk = idValue == id && idValue.size() == 36 && urlOffline == url + L"  offline";
  const std::wstring name = RelayRowName(L"Message server", id, true);
  const std::wstring nameOffline = RelayRowName(L"URnetwork", url, false);
  const bool nameOk = name == L"Message server, " + id + L", online" &&
                      nameOffline == L"URnetwork, " + url + L", offline";
  const bool ok = labelOk && valueOk && columnsOk && wholeOk && nameOk;
  return std::format(
      L"  relay row fit    : {}  label cut {} | value cut {} | label sizes to content {} | "
      L"tooltip and help text whole ({} of 36 chars) {} | automation name holds label, value and "
      L"state {}   [query: S3, a relay drawer row's label is never cut and its value is cut with "
      L"an ellipsis, the whole value in its tooltip, help text and automation name]",
      ok ? L"PASS" : L"FAIL", labelOk ? L"none" : L"NOT NONE", valueOk ? L"ellipsis" : L"NOT ELLIPSIS",
      columnsOk ? L"true" : L"FALSE", idValue.size(), wholeOk ? L"true" : L"FALSE",
      nameOk ? L"true" : L"FALSE");
}

std::wstring StatusStripFitAssertion() {
  using namespace urmsg::views;
  // Stand-in widths, in DIPs, of the strip's items: the dot's host, "Connected", a separator,
  // the "server" caption with its 6 of spacing, the value, a separator and the padlock; then the
  // three Advanced fields, each with its separator. The value's floor is the view's 48.
  const auto normal = [](double value) {
    return std::vector<StripItem>{{StripPart::Fixed, 20},     {StripPart::Fixed, 62},
                                  {StripPart::Fixed, 29},     {StripPart::Caption, 39},
                                  {StripPart::Value, value, 48}, {StripPart::Fixed, 29},
                                  {StripPart::Fixed, 14}};
  };
  const auto advanced = [&normal](double value) {
    auto items = normal(value);
    items.push_back({StripPart::Optional, 29 + 52});   // epoch
    items.push_back({StripPart::Optional, 29 + 108});  // session
    items.push_back({StripPart::Optional, 29 + 40});   // rec/s
    return items;
  };
  // The strip's room at the window's 400 DIP minimum less its 16 + 16 inset, up to the widest
  // window the app opens at.
  const double minRoom = 400.0 - 32.0;
  const double maxRoom = 1560.0 - 32.0;

  // THE PROPERTY, for one fit: it fits; a Fixed item is whole; the value is never dropped and
  // never cut below its floor; an Optional is whole or dropped, never cut; the dropped Optionals
  // are the rightmost ones; the caption goes only once every Optional has gone; and the value is
  // cut only once the caption has gone.
  const auto holds = [](std::vector<StripItem> const& items, double room) {
    const StripFit fit = FitStatusStrip(items, room);
    if (!fit.fits || fit.total > room + 0.01 || fit.fit.size() != items.size()) return false;
    bool optionalDropped = false, optionalShown = false, captionDropped = false, valueCut = false;
    for (size_t i = 0; i < items.size(); ++i) {
      const auto f = fit.fit[i];
      switch (items[i].part) {
        case StripPart::Fixed:
          if (f != StripItemFit::Whole || fit.width[i] != items[i].width) return false;
          break;
        case StripPart::Caption:
          if (f == StripItemFit::Cut) return false;
          if (f == StripItemFit::Dropped) captionDropped = true;
          break;
        case StripPart::Value:
          if (f == StripItemFit::Dropped) return false;
          if (fit.width[i] + 0.01 < std::min(items[i].floor, items[i].width)) return false;
          if (f == StripItemFit::Cut) valueCut = true;
          break;
        case StripPart::Optional:
          if (f == StripItemFit::Cut) return false;
          if (f == StripItemFit::Dropped) optionalDropped = true;
          else if (optionalDropped) return false;  // a shown Optional right of a dropped one
          else optionalShown = true;
          break;
      }
    }
    if (captionDropped && optionalShown) return false;
    if (valueCut && !captionDropped) {
      for (size_t i = 0; i < items.size(); ++i)
        if (items[i].part == StripPart::Caption) return false;
    }
    return true;
  };

  int asked = 0, held = 0;
  std::wstring firstBad;
  for (double value : {40.0, 170.0, 260.0, 600.0}) {
    for (double room = minRoom; room <= maxRoom; room += 8.0) {
      for (int mode = 0; mode < 2; ++mode) {
        ++asked;
        if (holds(mode == 0 ? normal(value) : advanced(value), room)) {
          ++held;
        } else if (firstBad.empty()) {
          firstBad = std::format(L"{} value {:.0f} in {:.0f}", mode == 0 ? L"normal" : L"advanced",
                                 value, room);
        }
      }
    }
  }

  // Four answers by name. "URnetwork exit · United States" (170) at the minimum fits whole in
  // Normal mode, and in Advanced Mode the three extras go. Two exit countries (260) cost the
  // caption, and the value is cut to the room that is left. At the widest window nothing goes.
  const StripFit a = FitStatusStrip(normal(170), minRoom);
  const bool aOk = a.fits && a.fit[3] == StripItemFit::Whole && a.fit[4] == StripItemFit::Whole;
  const StripFit b = FitStatusStrip(advanced(170), minRoom);
  const bool bOk = b.fits && b.fit[7] == StripItemFit::Dropped && b.fit[8] == StripItemFit::Dropped &&
                   b.fit[9] == StripItemFit::Dropped && b.fit[4] == StripItemFit::Whole;
  const StripFit c = FitStatusStrip(normal(260), minRoom);
  const bool cOk = c.fits && c.fit[3] == StripItemFit::Dropped && c.fit[4] == StripItemFit::Cut &&
                   c.width[4] > 213.9 && c.width[4] < 214.1;
  const StripFit d = FitStatusStrip(advanced(260), maxRoom);
  bool dOk = d.fits;
  for (auto f : d.fit) dOk = dOk && f == StripItemFit::Whole;

  // EVERY FACT STAYS REACHABLE: the tooltip and help text hold each item's spoken fact whole, one
  // per line, the empty ones (the dot and the rules) skipped.
  const std::wstring facts = StatusStripFacts(
      {L"Connection, Connected", L"", L"server, URnetwork exit · United States", L"",
       L"Live session: server key pinned", L"epoch, 3", L"session, unavailable", L"rec/s, 0"});
  const bool factsOk = facts ==
      L"Connection, Connected\nserver, URnetwork exit · United States\nLive session: server "
      L"key pinned\nepoch, 3\nsession, unavailable\nrec/s, 0";

  const bool ok = held == asked && aOk && bOk && cOk && dOk && factsOk;
  return std::format(
      L"  strip fit        : {}  {}/{} fits hold from {:.0f} to {:.0f} dip{}{} | exit at the "
      L"minimum whole {} | Advanced extras dropped there {} | two countries: caption dropped, "
      L"value cut to 214 {} | widest window whole {} | facts reachable whole {}   [query: S3, the "
      L"strip never draws past its own edge: the Advanced fields drop rightmost first, then the "
      L"caption, then the value is cut with an ellipsis, and every fact stays in its tooltip and "
      L"help text]",
      ok ? L"PASS" : L"FAIL", held, asked, minRoom, maxRoom, firstBad.empty() ? L"" : L", first not: ",
      firstBad, aOk ? L"true" : L"FALSE", bOk ? L"true" : L"FALSE", cOk ? L"true" : L"FALSE",
      dOk ? L"true" : L"FALSE", factsOk ? L"true" : L"FALSE");
}

// ---- an invitation refused over a held group, and the leave that answers it (Task 39) --------
//
// PURE on the same terms as the lines above: every function asked is Live/HeldGroupRules.h's, no
// SDK is loaded and no XAML is touched. The library's sentences below are TYPED HERE, copied from
// urnetwork/message sdk/urmessage (errors.go declares each sentinel, heldgroup.go and forget.go
// format the clause after it) at 215a0696, and read again at e0d16eac75e4 (R27; the sentences are
// the same there), and nothing reads the library: these lines test the
// app's rules against those copies, and a sentence reworded there leaves them green. (A check of
// the pinned text itself is a follow-up, R21.) Each line prints PASS or FAIL, the QUERY, and every
// case it asked, so the complement is on the page beside the verdict.
std::vector<std::wstring> HeldGroupDiagnostics() {
  using namespace urmsg::live;
  std::vector<std::wstring> out;
  const std::string hex(64, 'a');
  const std::string otherHex = std::string(63, 'a') + "b";
  // ErrGroupAlreadyHeld's sentence WHOLE, typed from errors.go at e0d16eac75e4 and not built from
  // kHeldRefusalSentence, which is only its stable part (R27)
  const std::string held =
      "urmessage: this device already holds this group, so an invite into it is refused; leave it "
      "first with ForgetGroup to join it again: ";

  // 1. which refusal: the gate's four spellings of ErrGroupAlreadyHeld, and the refusals beside it
  //    that are NOT a held group, among them the one that names a held group and is not the
  //    sentinel (a store that could not answer).
  {
    struct Case {
      const wchar_t* name;
      std::string text;
      JoinRefusal want;
    };
    const Case cases[] = {
        {L"live", held + "group " + hex + " is live on this device", JoinRefusal::HeldLive},
        {L"judged", held + "the group this device holds under " + hex +
                        " is not the one this join was judged against",
         JoinRefusal::HeldLive},
        {L"on-disk", held + "group " + hex + " has a record on this device's disk",
         JoinRefusal::HeldNotLive},
        // R27: the sentence's stable part with its tail reworded is still a held group
        {L"on-disk-reworded",
         "urmessage: this device already holds this group; it refuses an invite into it: group " + hex +
             " has a record on this device's disk",
         JoinRefusal::HeldNotLive},
        {L"damaged", held + "group " + hex +
                         " has a record on this device's disk that does not read: urmessage: a "
                         "group record on this device's disk does not read, so that group cannot "
                         "be restored; ForgetGroup leaves it, and it can then be joined again: the "
                         "record in 0f: short",
         JoinRefusal::HeldNotLive},
        {L"unanswered", "urmessage: whether this device already holds group " + hex +
                            " could not be read, so the invite into it is refused: io",
         JoinRefusal::Other},
        {L"join-refused", "urmessage: this invite was refused before anything was written, and "
                          "the join code is not spent: x",
         JoinRefusal::Other},
        {L"no-join-code", "urmessage: this device holds no join code this invitation was made "
                          "for; make a new join code: x",
         JoinRefusal::Other},
        {L"not-restored", std::string(kNotRestoredSentence) +
                              "; call Restore, and if it does not come back, ForgetGroup leaves "
                              "it: " + hex,
         JoinRefusal::Other},
        {L"empty", std::string(), JoinRefusal::Other},
    };
    auto word = [](JoinRefusal r) {
      return r == JoinRefusal::HeldLive ? L"live" : r == JoinRefusal::HeldNotLive ? L"not-live" : L"other";
    };
    std::wstring table;
    int right = 0;
    for (auto const& c : cases) {
      const JoinRefusal got = ClassifyJoinRefusal(c.text);
      if (got == c.want) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{}->{}{}", c.name, word(got), got == c.want ? L"" : L"(WRONG)");
    }
    const int total = static_cast<int>(std::size(cases));
    out.push_back(std::format(
        L"  held refusal     : {}  {}/{} classified: {}   [query: ErrGroupAlreadyHeld's sentence "
        L"with \"has a record on this device's disk\" after it is a group that is not live and "
        L"is offered the leave; with any other clause it is live; every sentence without it is "
        L"neither, including a store that could not say whether the group is held]",
        Verdict(right == total), right, total, table));
  }

  // 2. the group id: the invitation's first field at version 2 and at version 3, refused when its
  //    length is not 32 or the octets stop short; and the refusal must name that id.
  {
    auto invite = [](uint16_t version, uint32_t declared, size_t idOctets) {
      std::vector<uint8_t> v = {static_cast<uint8_t>(version >> 8), static_cast<uint8_t>(version)};
      for (int shift = 24; shift >= 0; shift -= 8) v.push_back(static_cast<uint8_t>(declared >> shift));
      for (size_t i = 0; i < idOctets; ++i) v.push_back(static_cast<uint8_t>(0xA0 + i));
      // the next field's length, so a reader that ran past the id would read something
      for (int i = 0; i < 4; ++i) v.push_back(0x00);
      return v;
    };
    std::array<uint8_t, kGroupIdOctets> want{};
    for (size_t i = 0; i < kGroupIdOctets; ++i) want[i] = static_cast<uint8_t>(0xA0 + i);
    const std::string wantHex = GroupIdHex(want);
    std::array<uint8_t, kGroupIdOctets> got2{}, got3{}, untouched{};
    untouched.fill(0x5A);
    auto shortOut = untouched;
    auto badLenOut = untouched;
    const auto v2 = invite(2, 32, 32);
    const auto v3 = invite(3, 32, 32);
    const auto badLen = invite(2, 31, 32);
    const auto cut = invite(2, 32, 31);
    const bool read2 = InviteGroupId(v2.data(), v2.size(), got2) && got2 == want;
    const bool read3 = InviteGroupId(v3.data(), v3.size(), got3) && got3 == want;
    const bool refusedLen = !InviteGroupId(badLen.data(), badLen.size(), badLenOut) && badLenOut == untouched;
    // 6 + 31 octets of id: shorter than the field, whatever follows it
    const bool refusedShort = !InviteGroupId(cut.data(), 6 + 31, shortOut) && shortOut == untouched;
    const bool hexOk = wantHex.size() == 64 && wantHex.rfind("a0a1a2", 0) == 0 &&
                       wantHex.find_first_of("ABCDEF") == std::string::npos;
    const std::string disk = held + "group " + wantHex + " has a record on this device's disk";
    const bool names = RefusalNamesGroup(disk, wantHex);
    const bool namesNotOther = !RefusalNamesGroup(disk, otherHex);
    const bool namesNotPrefix = !RefusalNamesGroup(held + "group " + wantHex + "ff is live", wantHex);
    const bool namesNotShort = !RefusalNamesGroup(disk, wantHex.substr(0, 62));
    const bool ok = read2 && read3 && refusedLen && refusedShort && hexOk && names &&
                    namesNotOther && namesNotPrefix && namesNotShort;
    out.push_back(std::format(
        L"  held group id    : {}  v2 {} | v3 {} | length 31 refused {} | 31 octets refused {} | "
        L"hex {} ({}) | names its own id {} | not another {} | not a longer hex {} | not a short "
        L"one {}   [query: the id is the 32 octets after two version octets and a big-endian "
        L"length that must read 32, at invite version 2 and 3 alike, and a refusal is answered by "
        L"a leave only when it names exactly that id]",
        Verdict(ok), read2, read3, refusedLen, refusedShort, hexOk, Widen(wantHex.substr(0, 12)), names,
        namesNotOther, namesNotPrefix, namesNotShort));
  }

  // 3. the leave's own refusal: ErrGroupNotRestored, and the leave's other answers beside it.
  {
    const bool notRestored = IsNotRestoredRefusal(
        std::string(kNotRestoredSentence) +
        "; call Restore, and if it does not come back, ForgetGroup leaves it: " + hex);
    const bool notHeld = !IsNotRestoredRefusal("urmessage: this device holds no such group: " + hex);
    const bool unfinished = !IsNotRestoredRefusal(
        "urmessage: this device has left the group, but its copy on the disk could not be erased "
        "in full; ForgetGroup again, or the next Restore, finishes the erase: io");
    const bool heldText = !IsNotRestoredRefusal(held + "group " + hex + " has a record on this device's disk");
    const bool empty = !IsNotRestoredRefusal("");
    const bool ok = notRestored && notHeld && unfinished && heldText && empty;
    out.push_back(std::format(
        L"  held not-restored: {}  ErrGroupNotRestored {} | ErrGroupNotHeld not it {} | "
        L"ErrForgetUnfinished not it {} | the held refusal not it {} | empty not it {}   [query: "
        L"the leave of a group no restore has tried answers ErrGroupNotRestored; the app has restored "
        L"first already, so that answer erases nothing and offers the leave again]",
        Verdict(ok), notRestored, notHeld, unfinished, heldText, empty));
  }

  // 4. the join after a leave whose erase did not finish: offered the leave again only when it is
  //    ErrForgetUnfinished naming the held group (review F3).
  {
    const std::string joinUnfinished =
        std::string(kForgetUnfinishedSentence) +
        "; ForgetGroup again, or the next Restore, finishes the erase: group " + hex +
        " cannot be joined or made until the erase of the leave before it has finished: io";
    const bool itsOwn = IsUnfinishedEraseOf(joinUnfinished, hex);
    const bool notOther = !IsUnfinishedEraseOf(joinUnfinished, otherHex);
    const bool forgetsOwn = !IsUnfinishedEraseOf(
        std::string(kForgetUnfinishedSentence) +
            "; ForgetGroup again, or the next Restore, finishes the erase: io",
        hex);
    const bool heldText = !IsUnfinishedEraseOf(held + "group " + hex + " has a record on this device's disk", hex);
    const bool otherJoin = !IsUnfinishedEraseOf(
        "urmessage: this device holds no join code this invitation was made for; make a new join "
        "code: group " + hex,
        hex);
    const bool empty = !IsUnfinishedEraseOf("", hex);
    const bool classifiedOther = ClassifyJoinRefusal(joinUnfinished) == JoinRefusal::Other;
    const bool ok = itsOwn && notOther && forgetsOwn && heldText && otherJoin && empty && classifiedOther;
    out.push_back(std::format(
        L"  held unfinished  : {}  the join's ErrForgetUnfinished for its group {} | for another "
        L"group not it {} | naming no group not it {} | the held refusal not it {} | another join "
        L"refusal not it {} | empty not it {} | classified other, not held {}   [query: a join "
        L"refused because the held group's leave has not finished its erase offers that leave again "
        L"instead of \"not meant for this device\"; nothing else does]",
        Verdict(ok), itsOwn, notOther, forgetsOwn, heldText, otherJoin, empty, classifiedOther));
  }

  // 5. the two sentences a person is shown, and the button the second one names.
  {
    const std::string live(kHeldLiveSentence);
    const std::string notLive(kHeldNotLiveSentence);
    const std::wstring button(kLeaveHeldButton);
    const std::wstring buttonName(kLeaveHeldButtonName);
    auto raw = [](std::string const& s) {
      return s.find("urmessage:") != std::string::npos || s.find("ForgetGroup") != std::string::npos ||
             s.find("Err") != std::string::npos;
    };
    const bool distinct = !live.empty() && !notLive.empty() && live != notLive;
    const bool noRaw = !raw(live) && !raw(notLive);
    const bool namesLeave = notLive.find("Leave it") != std::string::npos && button.starts_with(L"Leave") &&
                            buttonName.starts_with(button);
    const bool liveOffersNothing = live.find("Leave") == std::string::npos;
    const bool ok = distinct && noRaw && namesLeave && liveOffersNothing;
    out.push_back(std::format(
        L"  held sentences   : {}  live \"{}\" | not live \"{}\" | button \"{}\" (\"{}\")   "
        L"[query: two distinct sentences, neither the library's words; the not-live one says to "
        L"leave and the button it sits over is that leave; the live one offers no leave]",
        Verdict(ok), Widen(live), Widen(notLive), button, buttonName));
  }
  return out;
}

// ---- what the app says for each refusal of a join or an add (Task 58) ------------------------
//
// PURE on the same terms as the held-group lines above: every function asked is
// Live/RefusalCopy.h's, no SDK is loaded and no XAML is touched. The library's sentences are TYPED
// HERE WHOLE from urnetwork/message sdk/urmessage/errors.go at the T1 branch's head after its second
// fix round (e0d16eac75e4), and those of its third round at 907109f1385e (R29, R30, R32), never built
// from the header's constants, which are only their stable parts (R27). The clauses after them are typed from where that branch formats them (group.go's
// Join, invite.go's ParseInvite, joinseal.go and rolescommit.go for the adder, device.go's
// KeyPackage, messagegroup/engine.go's refused key packages, restore.go and statestore_durable.go's
// GroupRecords and readRecord for a file not read just now, heldgroup.go's heldUnread and forget.go's
// recordOnDisk). Nothing reads the library: a sentence reworded there leaves these green.
// THE TABLE'S OWN WORDS ARE TYPED HERE A SECOND TIME, from the plan's Task 58, so a sentence edited
// in the header is a FAIL here and not a silent change of what a person reads. Each line prints
// PASS or FAIL, the QUERY, and every case it asked.
std::vector<std::wstring> RefusalCopyDiagnostics() {
  using namespace urmsg::live;
  std::vector<std::wstring> out;
  const std::string hex(64, 'c');
  // THE LIBRARY'S SENTENCES, WHOLE (R27), as errors.go declares them at e0d16eac75e4.
  const std::string refused =
      "urmessage: this invite was refused before anything was written, and the join code is not spent";
  const std::string noCode =
      "urmessage: this device holds no join code this invitation was made for; make a new join code";
  const std::string spent =
      "urmessage: this join did not complete and its join code is spent; make a new join code and ask "
      "to be added again";
  const std::string predates =
      "urmessage: this group was founded before invitations were sealed, so this build does not open "
      "it; leave it and start the group again";
  const std::string inviteVersion = "urmessage: this invite is at a version this build does not read";
  const std::string invalidKp = "urmessage: this join code is not a valid key package";
  const std::string cannotOpen =
      "urmessage: this join code was made by a build that cannot open a sealed invite; ask for a join "
      "code from an updated URmessage";
  const std::string hasALeaf =
      "urmessage: this identity already has an entry in this group, perhaps from an invitation that "
      "was never opened; remove that entry, then add the new join code";
  const std::string noWrapKey =
      "urmessage: this device holds no x-wing seed for the leaf it publishes, so it cannot open an "
      "encapsulation addressed to that leaf";
  const std::string notReadNow =
      "urmessage: a file of a group on this device's disk could not be read just now, so that group "
      "was not restored this time; nothing of it was changed, and the next Restore tries it again";
  // the same sentinel as 09018b10dd23 wrote it, before R26's round reworded its subject
  const std::string notReadNowAt09018b1 =
      "urmessage: a group record on this device's disk could not be read just now, so that group was "
      "not restored this time; nothing of it was changed, and the next Restore tries it again";
  const std::string damaged =
      "urmessage: a group record on this device's disk does not read, so that group cannot be "
      "restored; ForgetGroup leaves it, and it can then be joined again";
  const std::string heldWhole =
      "urmessage: this device already holds this group, so an invite into it is refused; leave it "
      "first with ForgetGroup to join it again";
  const std::string restoreSentence = "urmessage: this group could not be restored from the durable state store";
  const std::string storeState = "urmessage: the durable state store could not be read or written";
  const std::string engineNoCode =
      "urmessage: JoinFromWelcomeInto: messagegroup: no key package this store holds is addressed "
      "by this welcome";
  // An OS's own words for the failed reads R26 tells apart (a sharing or a lock violation is for a
  // moment; a denial is not), and paths as the store writes them: a group's directory is the SHA-256
  // of its id, and a join code's file is the SHA-256 of its reference.
  const std::string sharing = "The process cannot access the file because it is being used by another process.";
  const std::string lockedPortion =
      "The process cannot access the file because another process has locked a portion of the file.";
  const std::string denied = "Access is denied.";
  const std::string dirName(64, 'd');
  const std::string groupDir = "C:\\u\\live\\state\\state\\group\\" + dirName;
  const std::string kpFile = "C:\\u\\live\\state\\state\\kp\\" + std::string(64, 'e');
  // THE SECOND FIX ROUND'S ROADS (T1 FIX 2's report, "The sentences a person meets now"), each as
  // e0d16ea formats it: restore.go and GroupRecords for the restore, heldgroup.go and forget.go for
  // one group's record, group.go's window for a join code.
  const std::string restoreRecordHeld =
      restoreSentence + ": " + notReadNow + ": " + storeState + ": " + groupDir +
      "\\meta could not be read: open " + groupDir + "\\meta: " + sharing +
      " (in all, 1 of the 1 group directories on this disk: " + dirName + ")";
  const std::string restoreEpochHeld =
      restoreSentence + ": " + notReadNow + ": group " + hex + ": " + restoreSentence + ": group " + hex +
      " at epoch 1: " + storeState + ": " + groupDir + "\\epoch\\0000000000000001 could not be read: open " +
      groupDir + "\\epoch\\0000000000000001: " + lockedPortion;
  const std::string restoreRecordDenied =
      restoreSentence + ": " + damaged + ": the record in " + dirName + ": " + storeState + ": " + groupDir +
      "\\meta could not be read: open " + groupDir + "\\meta: " + denied +
      " (in all, 1 of the 1 group directories on this disk: " + dirName + ")";
  const std::string heldOverDenied =
      heldWhole + ": group " + hex + " has a record on this device's disk that does not read: " + damaged +
      ": the record in " + dirName + ": " + storeState + ": " + groupDir + "\\meta could not be read: open " +
      groupDir + "\\meta: " + denied;
  const std::string joinRecordHeld =
      "urmessage: whether this device already holds group " + hex +
      " could not be read, so the invite into it is refused: " + storeState + ": " + groupDir +
      "\\meta could not be read: open " + groupDir + "\\meta: " + sharing;
  const std::string leaveRecordHeld =
      "urmessage: whether this device holds group " + hex + " on its disk could not be read, so nothing "
      "was left: " + storeState + ": " + groupDir + "\\meta could not be read: open " + groupDir +
      "\\meta: " + sharing;
  const std::string takeTail =
      ": the welcome names 1 key package refs, this store refused 1 of them, and the last refusal it "
      "answered was ";
  const std::string joinCodeHeld = refused + ": " + engineNoCode + takeTail + storeState + ": " + kpFile +
                                   " could not be read: open " + kpFile + ": " + sharing;
  const std::string joinCodeNeverReads = noCode + ": " + engineNoCode + takeTail + storeState + ": " +
                                         kpFile + " could not be read: open " + kpFile + ": " + denied;
  const std::string joinCodeRemoved =
      spent + ": " + engineNoCode + takeTail +
      "urmessage: the join code was removed from the disk and the take then failed: " + storeState +
      ": /home/u/.urmessage/state/kp could not be flushed, so a value this store named may not survive "
      "a crash: input/output error";
  // the names the lines below used at 09018b10dd23, for the same roads
  const std::string& storeUnreadCode = joinCodeHeld;
  const std::string& notReadRestore = restoreRecordHeld;
  const std::string& notReadJoin = joinRecordHeld;
  const std::string& notReadLeave = leaveRecordHeld;
  // ROUND 3'S SENTENCES (907109f1385e, read there: errors.go, restore.go, heldgroup.go, forget.go,
  // statestore_durable.go, group.go). A failure this build cannot classify is UNPROVEN (R29; R30
  // puts a permission among them), each read clause now carries its class's sentence, the leave tries
  // the group's restore again before it erases anything (R30), and that re-try can find this device
  // not connected (R32).
  const std::string unproven =
      "urmessage: a file of a group on this device's disk could not be read, and this build cannot "
      "tell whether that will pass, so that group was not restored; nothing of it was changed, and "
      "it is not left until a Restore reads it or finds it damaged";
  const std::string notConnected =
      "urmessage: this device has not said Hello, and every authenticator is a mac over the "
      "connection's server_nonce";
  const std::string format = "urmessage: this does not read as a record this store writes";
  const std::string brokenSum = format + ": the record's checksum is not the hash of the record";
  const std::string epochFile = groupDir + "\\epoch\\0000000000000001";
  const std::string metaFile = groupDir + "\\meta";
  const std::string r3RestoreRecordDenied =
      restoreSentence + ": " + unproven + ": " + storeState + ": " + metaFile + " could not be read: open " +
      metaFile + ": " + denied + " (in all, 1 of the 1 group directories on this disk: " + dirName + ")";
  const std::string r3RestoreEpochDenied =
      restoreSentence + ": " + unproven + ": group " + hex + ": " + restoreSentence + ": group " + hex +
      " at epoch 1: " + storeState + ": " + epochFile + " could not be read: open " + epochFile + ": " + denied;
  const std::string r3RestoreRecordBroken =
      restoreSentence + ": " + damaged + ": the record in " + dirName + ": " + brokenSum +
      " (in all, 1 of the 1 group directories on this disk: " + dirName + ")";
  const std::string r3RestoreEpochBroken =
      restoreSentence + ": group " + hex + " at epoch 1: " + brokenSum;
  const std::string r3FreshEpochHeld = restoreSentence + ": group " + hex + " at epoch 1: " + storeState +
                                       ": " + epochFile + " could not be read: open " + epochFile + ": " + sharing;
  const std::string r3FreshEpochDenied = restoreSentence + ": group " + hex + " at epoch 1: " + storeState +
                                         ": " + epochFile + " could not be read: open " + epochFile + ": " + denied;
  const std::string r3LeaveFreshHeld = notReadNow + "; so nothing was left: group " + hex + ": " + r3FreshEpochHeld;
  const std::string r3LeaveFreshDenied = unproven + "; so nothing was left: group " + hex + ": " + r3FreshEpochDenied;
  const std::string r3LeaveRecordHeld =
      "urmessage: whether this device holds group " + hex + " on its disk could not be read, so nothing "
      "was left: " + notReadNow + ": " + storeState + ": " + metaFile + " could not be read: open " +
      metaFile + ": " + sharing;
  const std::string r3LeaveRecordDenied =
      "urmessage: whether this device holds group " + hex + " on its disk could not be read, so nothing "
      "was left: " + unproven + ": " + storeState + ": " + metaFile + " could not be read: open " +
      metaFile + ": " + denied;
  const std::string r3LeaveReadAgain =
      "urmessage: group " + hex + "'s record could not be read again for the leave, so nothing was left: " +
      unproven + ": " + storeState + ": " + metaFile + " could not be read: open " + metaFile + ": " + denied;
  const std::string r3LeaveNotTried =
      "urmessage: group " + hex + "'s restore could not be tried again for the leave, so nothing was left: " +
      notConnected;
  const std::string r3LeaveMarkUnwritten =
      "urmessage: the leave could not be recorded on the disk, so nothing was left: " + storeState + ": " +
      groupDir + "\\forgetting could not be written: open " + groupDir + "\\forgetting: " + denied;
  const std::string r3NotRestored =
      "urmessage: this group is on this device's disk and has not been restored; call Restore, and if "
      "it does not come back, ForgetGroup leaves it: " + hex;
  const std::string r3OwnerMustTransfer =
      "urmessage: you own this group and somebody else is in it, so hand it over first with "
      "TransferOwnership; leaving now would leave them with an owner nobody can remove: 1 other "
      "leaf/leaves";
  const std::string r3JoinRecordHeld =
      "urmessage: whether this device already holds group " + hex +
      " could not be read, so the invite into it is refused: " + notReadNow + ": " + storeState + ": " +
      metaFile + " could not be read: open " + metaFile + ": " + sharing;
  const std::string r3JoinRecordDenied =
      "urmessage: whether this device already holds group " + hex +
      " could not be read, so the invite into it is refused: " + unproven + ": " + storeState + ": " +
      metaFile + " could not be read: open " + metaFile + ": " + denied;
  const std::string r3JoinRecordBroken =
      heldWhole + ": group " + hex + " has a record on this device's disk that does not read: " + damaged +
      ": the record in " + dirName + ": " + brokenSum;
  const std::string r3JoinRecordReads = heldWhole + ": group " + hex + " has a record on this device's disk";
  const std::string r3TakeBroken = noCode + ": " + engineNoCode + takeTail + format +
                                   ": the record under this name is not the key package of " + hex;
  // ROUND 5'S SENTENCES (93cc0afb85b1, read there: heldgroup.go, forget.go, group.go, joinstage.go
  // and statestore_durable.go). The gate refuses a record missing from a folder where the store's own
  // files stand (R34 item 1); ForgetGroup answers ErrGroupNotHeld for a folder holding nothing of a
  // group (item 8); PutGroupRecord's double failure reaches a join and Open (item 3); and the
  // window's commit is a write of the store's own.
  const std::string r5GateRecordMissing = "urmessage: whether this device already holds group " + hex +
                                          " could not be read, so the invite into it is refused: " + unproven +
                                          ": its directory stands and its record is not in it";
  const std::string r5ForgetNotHeld = "urmessage: this device holds no such group: " + hex;
  const std::string r5Temp = groupDir + "\\.writing-2718281828";
  const std::string r5CreationUnretracted =
      storeState + ": group " + hex +
      ": its record is on the disk and this store could not record that its creation finished: the "
      "creation mark could not be removed: " + storeState + ": " + groupDir + "\\pending could not be "
      "removed: remove " + groupDir + "\\pending: The directory is not empty.; and its retraction could "
      "not be written: " + storeState + ": " + r5Temp + " could not be renamed onto " + groupDir +
      "\\recorded: rename " + r5Temp + " " + groupDir + "\\recorded: " + denied;
  const std::string r5JoinDoubleFailure = spent +
                                          ": this group joined and its record could not be persisted, so a "
                                          "restart would not come back into it: " + r5CreationUnretracted;
  const std::string r5OpenDoubleFailure =
      "urmessage: this group is open on the server and its record is on this device's disk, but this "
      "store could not record that the group's creation finished: " + r5CreationUnretracted;
  const std::string r5OpenNotPersisted =
      "urmessage: this group is open on the server and its record could not be persisted, so a restart "
      "would refuse to send in it: " + storeState + ": a temporary file in " + groupDir +
      " could not be created: open " + r5Temp + ": " + denied;
  const std::string r5TakeDenied = refused + ": " + engineNoCode + takeTail + storeState + ": " + kpFile +
                                   " could not be read: open " + kpFile + ": " + denied;
  const std::string r5CommitRefused = refused + ": " + storeState + ": a temporary file in " + groupDir +
                                      " could not be created: open " + r5Temp + ": " + denied;
  // ROUND 6'S SENTENCES (9df6fca8414f, read there: forget.go, restore.go, statestore_durable.go). The
  // leave mark is held against what stands beside it (R36 item 2): undecided on the leave, the join
  // and the restore; stale at the erase; and its epoch unread at MarkGroupForgetting.
  const std::string r6Forgetting = groupDir + "\\forgetting";
  const std::string r6EpochDir = groupDir + "\\epoch";
  const std::string r6LeaveUndecided = "urmessage: a leave of group " + hex +
                                       " this device recorded could not be held against what stands beside "
                                       "it on the disk, so nothing was left: ";
  const std::string r6JoinUndecided = "urmessage: a leave of group " + hex +
                                      " this device recorded could not be held against what stands beside "
                                      "it on the disk, so the invite into it is refused: ";
  const std::string r6MarkUnread = ": the leave mark in " + dirName + " could not be read, so it decides nothing: " +
                                   storeState + ": " + r6Forgetting + " could not be read: open " + r6Forgetting +
                                   ": ";
  const std::string r6RecordAboveUnread = ": the leave mark in " + dirName +
                                          " names epoch 3, and whether a record above it stands could not be "
                                          "read, so the mark decides nothing yet: " + storeState + ": " + metaFile +
                                          " could not be read: open " + metaFile + ": ";
  const std::string r6EpochAboveUnread = ": the leave mark in " + dirName +
                                         " names epoch 3, and whether an epoch state above it stands could not "
                                         "be read, so the mark decides nothing yet: " + storeState + ": " +
                                         r6EpochDir + " could not be read: open " + r6EpochDir + ": ";
  const std::string r6StaleUnremoved = ": the leave mark in " + dirName +
                                       " names epoch 3, something of a later join stands above it, and the mark "
                                       "could not be removed: " + storeState + ": " + r6Forgetting +
                                       " could not be removed: remove " + r6Forgetting + ": ";
  const std::string r6NoEpoch = ": the leave mark in " + dirName + " names no epoch it decided on, so it decides nothing";
  const std::string r6Foreign = format + ": the leave mark in " + dirName + " does not name its own group";
  const std::string r6StaleAtErase =
      "urmessage: this group is on this device's disk and has not been restored; call Restore, and if it "
      "does not come back because it is damaged, ForgetGroup leaves it: " + hex +
      ": urmessage: the leave mark named an earlier join of this group than what stands on the disk, so it "
      "was removed and nothing was erased: group " + hex;
  const std::string r6MarkUnlisted = "urmessage: the leave could not be recorded on the disk, so nothing was left: " +
                                     storeState + ": " + r6EpochDir +
                                     " could not be listed, so the epoch this leave decides on could not be read: "
                                     "open " + r6EpochDir + ": " + denied;

  // 1. which row a refusal of the join road is, and the order that decides it.
  {
    struct Case {
      const wchar_t* name;
      std::string text;
      JoinCopy want;
    };
    const Case cases[] = {
        {L"window-seal", refused + ": urmessage: the join seal's authenticator does not verify",
         JoinCopy::JoinRefused},
        {L"window-engine", refused + ": urmessage: JoinFromWelcomeInto: mls: welcome",
         JoinCopy::JoinRefused},
        {L"window-not-first", "urmessage: the session at epoch 1: " + refused, JoinCopy::Other},
        {L"no-code", noCode + ": " + engineNoCode, JoinCopy::NoJoinCodeForInvite},
        {L"no-code-under-window", refused + ": " + noCode + ": " + engineNoCode,
         JoinCopy::NoJoinCodeForInvite},
        {L"no-code-over-window", noCode + ": " + refused, JoinCopy::NoJoinCodeForInvite},
        {L"spent-putback",
         spent + ": urmessage: JoinFromWelcomeInto: x, and the join code could not be put back: io",
         JoinCopy::JoinCodeSpent},
        {L"spent-record",
         spent + ": this group joined and its record could not be persisted, so a restart would not "
                 "come back into it: io",
         JoinCopy::JoinCodeSpent},
        {L"version", inviteVersion + ": it is at version 0x0002, and this build reads only 0x0003",
         JoinCopy::InviteVersion},
        {L"predates-join", refused + ": " + predates, JoinCopy::GroupPredatesJoinSeal},
        {L"predates-restore", "urmessage: restore: group " + hex + " at epoch 1: " + predates,
         JoinCopy::GroupPredatesJoinSeal},
        // Task 57's F4 and R26's V3: a join code the store held and could not read FOR A MOMENT is
        // the window's refusal, and never ErrNoJoinCodeForInvite's row (the engine's own words for
        // it, "no key package this store holds", are not the sdk's sentence), and since R34 A1 it is
        // the window's row for a join the store would not serve; one that will never read is
        // ErrNoJoinCodeForInvite's; a take whose remove landed is spent
        {L"store-unread-code", storeUnreadCode, JoinCopy::JoinRefusedByStore},
        {L"code-never-reads", joinCodeNeverReads, JoinCopy::NoJoinCodeForInvite},
        {L"code-removed", joinCodeRemoved, JoinCopy::JoinCodeSpent},
        {L"not-read-restore-epoch", restoreEpochHeld, JoinCopy::GroupRecordNotReadNow},
        {L"restore-damaged", restoreRecordDenied, JoinCopy::Other},
        {L"not-read-restore", notReadRestore, JoinCopy::GroupRecordNotReadNow},
        {L"not-read-join", notReadJoin, JoinCopy::GroupRecordNotReadNow},
        {L"not-read-join-short-id",
         "urmessage: whether this device already holds group cc could not be read, so the invite into "
         "it is refused: io",
         JoinCopy::Other},
        {L"not-read-leave", notReadLeave, JoinCopy::Other},
        {L"held",
         "urmessage: this device already holds this group, so an invite into it is refused; leave it "
         "first with ForgetGroup to join it again: group " + hex + " is live on this device",
         JoinCopy::Other},
        {L"shape", "urmessage: this invite's join seal is not the shape an invite carries: x",
         JoinCopy::Other},
        {L"our-own-words", std::string(kSayJoinRefused), JoinCopy::Other},
        {L"empty", std::string(), JoinCopy::Other},
    };
    std::wstring table;
    int right = 0;
    for (auto const& c : cases) {
      const JoinCopy got = ClassifyJoinCopy(c.text);
      if (got == c.want) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{}->{}{}", c.name, Widen(JoinCopyName(got)), got == c.want ? L"" : L"(WRONG)");
    }
    const int total = static_cast<int>(std::size(cases));
    out.push_back(std::format(
        L"  join copy        : {}  {}/{} classified: {}   [query: ErrNoJoinCodeForInvite's sentence "
        L"anywhere is its own row, then ErrJoinCodeSpent's, a record not read just now (restore's "
        L"ErrGroupRecordNotReadNow, or the join gate's clause around a 64 digit id), "
        L"ErrGroupPredatesJoinSeal's and ErrInviteVersion's; ErrJoinRefused's is its row only as the "
        L"text's FIRST words, and R34 A1's when the store's own refusal follows them (a join code the "
        L"store failed to read, F4); anything else, our own words and the leave's clause included, is "
        L"the caller's fallback]",
        Verdict(right == total), right, total, table));
  }

  // 2. R13, on its own line: no spelling of ErrNoJoinCodeForInvite reaches the row that says the
  //    join code is still good, and its sentence claims nothing it cannot know (R17).
  {
    const std::string spellings[] = {noCode + ": " + engineNoCode,
                                     refused + ": " + noCode + ": " + engineNoCode,
                                     noCode + ": " + refused, noCode};
    int own = 0;
    for (auto const& text : spellings)
      if (ClassifyJoinCopy(text) == JoinCopy::NoJoinCodeForInvite) ++own;
    const std::string_view say = JoinCopySentence(JoinCopy::NoJoinCodeForInvite);
    const std::string_view good = JoinCopySentence(JoinCopy::JoinRefused);
    const bool ownSentence = say == kSayNoJoinCodeForInvite && say != good;
    const bool notStillGood = say.find("still good") == std::string_view::npos;
    const bool notUsed = say.find("was used") == std::string_view::npos;
    const bool notNoneHeld = say.find("has no join code") == std::string_view::npos &&
                             say.find("holds no join code") == std::string_view::npos;
    const bool windowSaysGood = good.find("still good") != std::string_view::npos;
    const int total = static_cast<int>(std::size(spellings));
    const bool ok = own == total && ownSentence && notStillGood && notUsed && notNoneHeld && windowSaysGood;
    out.push_back(std::format(
        L"  join copy R13    : {}  {}/{} spellings of ErrNoJoinCodeForInvite are its own row | its own "
        L"sentence {} | not \"still good\" {} | not \"was used\" {} | does not say the device holds "
        L"none {} | the window's row says \"still good\" {}   [query: an invitation no join code of "
        L"this device can be read for is never told its join code is still good, whatever text "
        L"surrounds the sentence (R13); its copy says neither that the code was used nor that the "
        L"device has none (R17)]",
        Verdict(ok), own, total, ownSentence, notStillGood, notUsed, notNoneHeld, windowSaysGood));
  }

  // 3. which row a refusal of the add is.
  {
    struct Case {
      const wchar_t* name;
      std::string text;
      AddCopy want;
    };
    const std::string& invalid = invalidKp;
    const Case cases[] = {
        {L"invalid-decode", invalid + ": it does not decode: syntax: short", AddCopy::KeyPackageInvalid},
        {L"invalid-signature", invalid + ": mls: the key package's signature does not verify",
         AddCopy::KeyPackageInvalid},
        {L"invalid-live", invalid + ": key package 1 of 1 does not decode: syntax: short",
         AddCopy::KeyPackageInvalid},
        {L"invalid-no-key", invalid + ": its leaf publishes no key a join seal can be made to: x",
         AddCopy::KeyPackageInvalid},
        {L"cannot-open", cannotOpen, AddCopy::JoinerCannotOpenSealedInvite},
        {L"has-a-leaf", hasALeaf + ": urmessage: this identity is claimed", AddCopy::JoinerAlreadyHasALeaf},
        {L"role", "urmessage: a member may not add a new identity to this group", AddCopy::Other},
        {L"join-text", refused + ": x", AddCopy::Other},
        {L"our-own-words", std::string(kSayKeyPackageInvalid), AddCopy::Other},
        {L"empty", std::string(), AddCopy::Other},
    };
    std::wstring table;
    int right = 0;
    for (auto const& c : cases) {
      const AddCopy got = ClassifyAddCopy(c.text);
      if (got == c.want) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{}->{}{}", c.name, Widen(AddCopyName(got)), got == c.want ? L"" : L"(WRONG)");
    }
    const int total = static_cast<int>(std::size(cases));
    out.push_back(std::format(
        L"  add copy         : {}  {}/{} classified: {}   [query: the adder's three refusals of a join "
        L"code, each by its own sentence wherever it stands; a refusal by role, a join's refusal and "
        L"our own words are the caller's fallback]",
        Verdict(right == total), right, total, table));
  }

  // 4. the startup refusal of a device with no X-Wing seed.
  {
    const bool wrapped =
        IsNoDeviceWrapKey(noWrapKey + ", and so it publishes no join code: an invite to one is sealed to that leaf");
    const bool bare = IsNoDeviceWrapKey(noWrapKey);
    const bool otherKp = !IsNoDeviceWrapKey("urmessage: this device's key package: mls: x");
    const bool empty = !IsNoDeviceWrapKey("");
    const bool says = kSayNoDeviceWrapKey.find("cannot make a join code") != std::string_view::npos &&
                      kSayNoDeviceWrapKey.find("urmessage:") == std::string_view::npos;
    const bool ok = wrapped && bare && otherKp && empty && says;
    out.push_back(std::format(
        L"  no wrap key      : {}  KeyPackage's refusal {} | the bare sentinel {} | another key package "
        L"error not it {} | empty not it {} | \"{}\" {}   [query: a device whose store holds no X-Wing "
        L"seed is told on screen that this installation cannot make a join code]",
        Verdict(ok), wrapped, bare, otherKp, empty, Widen(kSayNoDeviceWrapKey), says));
  }

  // 5. the table's words, as the plan writes them, and each row's sentence is its own.
  {
    struct Row {
      const wchar_t* name;
      std::string_view got;
      std::string_view table;
    };
    const Row rows[] = {
        {L"ErrJoinRefused", JoinCopySentence(JoinCopy::JoinRefused),
         "This invitation was refused. Your join code is still good: ask for the invitation to be "
         "sent again another way."},
        // R34 A1: the window's refusal of a join the store would not serve
        {L"ErrJoinRefused+ErrStateStoreState", JoinCopySentence(JoinCopy::JoinRefusedByStore),
         "URmessage could not use its own folder, so this invitation was not used. Your join code is "
         "still good: check that URmessage can read and write its folder, then paste the invitation "
         "again."},
        {L"ErrNoJoinCodeForInvite", JoinCopySentence(JoinCopy::NoJoinCodeForInvite),
         "This invitation cannot be used with any join code this device can read. Make a new join "
         "code and ask to be invited again."},
        {L"ErrJoinCodeSpent", JoinCopySentence(JoinCopy::JoinCodeSpent),
         "This join did not complete. Make a new join code and ask to be added again."},
        {L"ErrInviteVersion", JoinCopySentence(JoinCopy::InviteVersion),
         "This invitation was made by an older URmessage. Ask them to update and invite you again."},
        {L"ErrGroupPredatesJoinSeal", JoinCopySentence(JoinCopy::GroupPredatesJoinSeal),
         "This group was started before invitations were sealed. Leave it and start it again."},
        {L"ErrJoinerCannotOpenSealedInvite", AddCopySentence(AddCopy::JoinerCannotOpenSealedInvite),
         "This join code was made by an older URmessage. Ask them for a new one from an updated app."},
        {L"ErrKeyPackageInvalid", AddCopySentence(AddCopy::KeyPackageInvalid),
         "This is not a valid join code. Ask them to send it again."},
        // R24's F5, in R25 Q1's scoped form (no "not joined yet" mark ships in T1)
        {L"ErrJoinerAlreadyHasALeaf", AddCopySentence(AddCopy::JoinerAlreadyHasALeaf),
         "This join code claims to be someone already in this group. Remove their entry only if "
         "they tell you they never opened their invitation, then add this join code."},
        // R25 Q5, as R34 A2 amends it
        {L"ErrGroupRecordNotReadNow", JoinCopySentence(JoinCopy::GroupRecordNotReadNow),
         "This conversation could not be read just now. Close anything else that may be using "
         "URmessage's folder, then open URmessage again."},
        // R29, as R34 A3 amends it
        {L"ErrGroupReadUnproven", JoinCopySentence(JoinCopy::GroupReadUnproven),
         "This conversation could not be read on this device, and nothing was changed. Check that "
         "URmessage can read its folder and that no program has moved files out of it, then open "
         "URmessage again."},
        // R32
        {L"ErrNotConnected", LeaveStopCopy(HeldLeaveStep::NotConnected).sentence,
         "This device is not connected right now, and nothing was changed. Try again when it is "
         "connected."},
        // R35 A8, as R37 amends it: the founding road, for group_open's double failure
        {L"OpenCreationUnrecorded", kSayCreationUnfinished,
         "This conversation was created, but URmessage could not finish saving it, and its invitation "
         "was not shown. Close anything else that may be using URmessage's folder, then open "
         "URmessage again."},
        // R37 A9: the founding road, for group_open's record-persist failure
        {L"OpenRecordNotPersisted", kSayOpenRecordNotSaved,
         "URmessage could not save this new conversation on this device, so it cannot be used, and "
         "its invitation was not shown. Check that URmessage can read and write its folder, then "
         "start the conversation again."},
        // R40 A10: a leave mark that names another group (FIX 6, 9df6fca8414f)
        {L"LeaveMarkNamesAnotherGroup", JoinCopySentence(JoinCopy::LeaveMarkForeign),
         "This conversation's folder on this device holds a note about leaving a different "
         "conversation, so URmessage cannot tell whether this one was left. Nothing was changed. It "
         "cannot be joined or left on this device until the file named 'forgetting' is moved out of "
         "that folder."},
    };
    int verbatim = 0, raw = 0;
    std::wstring wrong;
    for (auto const& r : rows) {
      if (r.got == r.table) {
        ++verbatim;
      } else {
        if (!wrong.empty()) wrong += L", ";
        wrong += r.name;
      }
      if (r.got.find("urmessage:") != std::string_view::npos || r.got.find("Err") != std::string_view::npos)
        ++raw;
    }
    int distinct = 0;
    for (size_t i = 0; i < std::size(rows); ++i) {
      bool unique = !rows[i].got.empty();
      for (size_t j = 0; j < std::size(rows); ++j)
        if (i != j && rows[i].got == rows[j].got) unique = false;
      if (unique) ++distinct;
    }
    const bool othersEmpty =
        JoinCopySentence(JoinCopy::Other).empty() && AddCopySentence(AddCopy::Other).empty();
    const int total = static_cast<int>(std::size(rows));
    const bool ok = verbatim == total && raw == 0 && distinct == total && othersEmpty;
    out.push_back(std::format(
        L"  refusal copy     : {}  {}/{} verbatim from the table{}{} | {} carry the library's words | "
        L"{}/{} distinct | Other says nothing of its own {}   [query: each row a person is shown is "
        L"Task 58's sentence, word for word (its ErrJoinerAlreadyHasALeaf row as R25 Q1 amends it, "
        L"R25 Q5's row as R34 A2 amends it, R29's as A3 does, R34 A1's, R35 A8's as R37 amends it, R37 "
        L"A9's, and R40 A10's for a leave mark that names another group), and is its own; none is the "
        L"library's]",
        Verdict(ok), verbatim, total, wrong.empty() ? L"" : L", WRONG: ", wrong, raw, distinct, total,
        othersEmpty));
  }

  // 6. the dialog's own copy: R20's intro in the held state, the adder's label, the joiner's label.
  {
    const std::wstring held(kHeldIntro);
    const std::wstring join(kJoinIntro);
    const std::wstring adder(kAdderInviteLabel);
    const std::wstring paste(kJoinerPasteLabel);
    const bool heldTrue = !held.empty() && held.find(L"not in a group") == std::wstring::npos && held != join;
    const bool joinKept = join.find(L"not in a group yet") != std::wstring::npos;
    const bool adderVerbatim = adder == L"Only their device can open it. It works once. Send it privately.";
    // R40 A11: the joiner's label says what is true since invite version 3, word for word
    const bool pasteTrue = paste ==
                               L"The invitation they send back. Keep it private: it was made for this "
                               L"device's join code, so only this device can open it, and it works once." &&
                           paste.find(L"whoever has it") == std::wstring::npos;
    const bool ok = heldTrue && joinKept && adderVerbatim && pasteTrue;
    out.push_back(std::format(
        L"  dialog copy      : {}  held intro \"{}\" says nothing of \"not in a group\" {} | the join "
        L"road's intro kept {} | the adder's label verbatim {} (\"{}\") | the joiner's paste label "
        L"verbatim, and not \"whoever has it\" {}   [query: R20, the held state's intro is true of a "
        L"device that holds a group; Task 58 Step 3's adder copy word for word; R40 A11, the joiner's "
        L"label says only this device can open the invitation]",
        Verdict(ok), held, heldTrue, joinKept, adderVerbatim, adder, pasteTrue));
  }

  // 7. "Make a new join code" (R25 Q2): offered on exactly the two rows that ask for one.
  {
    const JoinCopy rows[] = {JoinCopy::Other,         JoinCopy::JoinRefused,
                             JoinCopy::NoJoinCodeForInvite, JoinCopy::JoinCodeSpent,
                             JoinCopy::InviteVersion, JoinCopy::GroupPredatesJoinSeal,
                             JoinCopy::GroupRecordNotReadNow, JoinCopy::GroupReadUnproven,
                             JoinCopy::JoinRefusedByStore, JoinCopy::LeaveMarkForeign};
    std::wstring offered;
    int right = 0;
    for (JoinCopy row : rows) {
      const bool offers = OffersNewJoinCode(row);
      const bool want = row == JoinCopy::NoJoinCodeForInvite || row == JoinCopy::JoinCodeSpent;
      // and every row that offers it is one whose sentence asks for a new join code
      const bool asks = JoinCopySentence(row).find("Make a new join code") != std::string_view::npos;
      if (offers == want && (!offers || asks)) ++right;
      if (offers) {
        if (!offered.empty()) offered += L", ";
        offered += Widen(JoinCopyName(row));
      }
    }
    const bool button = kNewJoinCodeButton == L"Make a new join code";
    const bool says = !kSayNewJoinCode.empty() && kSayNewJoinCode.find("not in a group") == std::string_view::npos &&
                      kSayNewJoinCode.find("urmessage:") == std::string_view::npos;
    const int total = static_cast<int>(std::size(rows));
    const bool ok = right == total && button && says;
    out.push_back(std::format(
        L"  new join code    : {}  {}/{} rows right, offered on: {} | the button reads \"{}\" {} | "
        L"after a press: \"{}\" {}   [query: R25 Q2, the button is on the rows that ask for a new "
        L"join code (ErrNoJoinCodeForInvite, ErrJoinCodeSpent) and on no other, R34 A1's among them]",
        Verdict(ok), right, total, offered, kNewJoinCodeButton, button, Widen(kSayNewJoinCode), says));
  }

  // 8. a record that could not be read just now (R25 Q5), on the restore, join and leave roads, and
  //    nothing else of the app's answers it.
  {
    const bool restoreRow = ClassifyJoinCopy(notReadRestore) == JoinCopy::GroupRecordNotReadNow;
    const bool joinRow = ClassifyJoinCopy(notReadJoin) == JoinCopy::GroupRecordNotReadNow;
    const bool leaveRow = IsLeaveNotReadNow(notReadLeave);
    const bool leaveNotJoin = !IsLeaveNotReadNow(notReadJoin) && !IsLeaveNotReadNow(notReadRestore);
    const bool leaveShortId =
        !IsLeaveNotReadNow("urmessage: whether this device holds group c on its disk could not be read, so "
                           "nothing was left");
    const bool notRestoredNot =
        !IsLeaveNotReadNow(std::string(kNotRestoredSentence) +
                           "; call Restore, and if it does not come back, ForgetGroup leaves it: " + hex);
    // Task 39's rules do not take it for a held group, so no leave is offered for it
    const bool notHeld = ClassifyJoinRefusal(notReadJoin) == JoinRefusal::Other &&
                         ClassifyJoinRefusal(notReadRestore) == JoinRefusal::Other;
    const bool noButton = !OffersNewJoinCode(JoinCopy::GroupRecordNotReadNow);
    const bool storeUnreadNot = ClassifyJoinCopy(storeUnreadCode) != JoinCopy::GroupRecordNotReadNow &&
                                !IsLeaveNotReadNow(storeUnreadCode);
    const std::string_view say = JoinCopySentence(JoinCopy::GroupRecordNotReadNow);
    const bool sentence = say == kSayGroupRecordNotReadNow && say.find("Leave") == std::string_view::npos &&
                          say.find("leave") == std::string_view::npos;
    const bool ok = restoreRow && joinRow && leaveRow && leaveNotJoin && leaveShortId && notRestoredNot &&
                    notHeld && noButton && storeUnreadNot && sentence;
    out.push_back(std::format(
        L"  not read now     : {}  restore {} | join {} | leave {} | the leave's rule takes neither "
        L"other road {} | a short id is not it {} | ErrGroupNotRestored is not it {} | Task 39's "
        L"held rules take it for no held group {} | no new join code offered {} | F4's unread join "
        L"code is not it {} | its sentence offers no leave {}   [query: R25 Q5, the T1 fix's three "
        L"sentences for a record not read just now get \"{}\", and nothing else does]",
        Verdict(ok), restoreRow, joinRow, leaveRow, leaveNotJoin, leaveShortId, notRestoredNot, notHeld,
        noButton, storeUnreadNot, sentence, Widen(say)));
  }

  // 9. what the join dialog says first after a restore that brought no group back.
  {
    struct Case {
      const wchar_t* name;
      std::string text;
      int restored;
      std::string_view want;
    };
    const Case cases[] = {
        {L"predates", "urmessage: restore: group " + hex + " at epoch 1: " + predates, 0,
         kSayGroupPredatesJoinSeal},
        {L"not-read", notReadRestore, 0, kSayGroupRecordNotReadNow},
        {L"unproven", r3RestoreRecordDenied, 0, kSayGroupReadUnproven},
        {L"unproven-epoch", r3RestoreEpochDenied, 0, kSayGroupReadUnproven},
        {L"unproven-one-back", r3RestoreRecordDenied, 1, {}},
        {L"damaged-epoch", r3RestoreEpochBroken, 0, {}},
        {L"not-connected", notConnected, 0, {}},
        {L"not-read-one-back", notReadRestore, 1, {}},
        {L"predates-one-back", "urmessage: restore: group " + hex + " at epoch 1: " + predates, 1, {}},
        {L"damaged",
         "urmessage: this group could not be restored from the durable state store: urmessage: a "
         "group record on this device's disk does not read, so that group cannot be restored; "
         "ForgetGroup leaves it, and it can then be joined again: the record in " + hex + ": short",
         0, {}},
        {L"empty", std::string(), 0, {}},
    };
    std::wstring table;
    int right = 0;
    for (auto const& c : cases) {
      const std::string_view got = RestoreNote(c.text, c.restored);
      const bool hit = got == c.want;
      if (hit) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{}->{}{}", c.name,
                           got.empty()                      ? L"none"
                           : got == kSayGroupRecordNotReadNow ? L"not-read"
                           : got == kSayGroupReadUnproven     ? L"unproven"
                                                              : L"predates",
                           hit ? L"" : L"(WRONG)");
    }
    const int total = static_cast<int>(std::size(cases));
    out.push_back(std::format(
        L"  restore note     : {}  {}/{}: {}   [query: a restore that brought no group back opens the "
        L"join dialog with the predates row, or R25 Q5's row; a restore that brought one back, a "
        L"damaged record and nothing at all say nothing]",
        Verdict(right == total), right, total, table));
  }

  // 10. R25 Q4: the Waiting text and the intro of a device whose group is on its disk and did not
  //     open say nothing of "not in a group yet"; the join road's are as they were.
  {
    const std::string before =
        "This device is not in a group yet. Send your join code to whoever is setting the group up, "
        "and paste the invitation they send back.";
    const bool fresh = WaitingText("", false) == before;
    const std::string afterLeave = WaitingText("Left.", false);
    const bool afterLeaveKept = afterLeave == "Left. " + before;
    const std::string predatesText = WaitingText(kSayGroupPredatesJoinSeal, true);
    const std::string notReadText = WaitingText(kSayGroupRecordNotReadNow, true);
    const bool heldNot = predatesText.find("not in a group") == std::string::npos &&
                         notReadText.find("not in a group") == std::string::npos;
    const bool heldFirst = predatesText.starts_with(kSayGroupPredatesJoinSeal) &&
                           notReadText.starts_with(kSayGroupRecordNotReadNow) &&
                           predatesText.ends_with(kWaitingSendJoinCode);
    const bool introHolds = OnboardIntro(false, false, true) == kHeldIntro;
    const bool introR20 = OnboardIntro(true, false, false) == kHeldIntro && OnboardIntro(false, true, false) == kHeldIntro;
    const bool introJoin = OnboardIntro(false, false, false) == kJoinIntro;
    const bool ok = fresh && afterLeaveKept && heldNot && heldFirst && introHolds && introR20 && introJoin;
    out.push_back(std::format(
        L"  waiting copy     : {}  a fresh device's text as before {} | after a leave as before {} | "
        L"a held group's says nothing of \"not in a group\" {} | its sentence first {} | the intro "
        L"of a device whose group did not open is \"{}\" {} | R20's intro kept {} | the join road's "
        L"intro kept {}   [query: R25 Q4, under the predates row and the not-read row neither the "
        L"Waiting text nor the intro says the device is not in a group]",
        Verdict(ok), fresh, afterLeaveKept, heldNot, heldFirst, kHeldIntro, introHolds, introR20, introJoin));
  }

  // 11. R27: every library message is matched by its STABLE PART. Each is asked whole, as e0d16ea
  //     writes it, and again with its tail reworded; both are typed here, never built from the
  //     header's constants, so a constant that matched a whole sentence again turns the reworded
  //     case FAIL. The sentence R26's round reworded is asked a third way, in 09018b10dd23's words.
  {
    enum class Ask { Join, Add, WrapKey, Leave, Sentinel, NotConnected, Open, OpenNotSaved };
    struct Case {
      const wchar_t* name;
      std::string whole;
      std::string reworded;
      Ask ask;
      JoinCopy join;
      AddCopy add;
    };
    const Case cases[] = {
        {L"ErrJoinRefused", refused + ": urmessage: the join seal's authenticator does not verify",
         "urmessage: this invite was refused before anything was written; its join code is still on "
         "this device: x",
         Ask::Join, JoinCopy::JoinRefused, AddCopy::Other},
        {L"ErrNoJoinCodeForInvite", noCode + ": " + engineNoCode,
         "urmessage: this device holds no join code this invitation was made for; ask for a new "
         "invitation: x",
         Ask::Join, JoinCopy::NoJoinCodeForInvite, AddCopy::Other},
        {L"ErrJoinCodeSpent", joinCodeRemoved,
         "urmessage: this join did not complete and its join code is spent; ask to be invited again: x",
         Ask::Join, JoinCopy::JoinCodeSpent, AddCopy::Other},
        {L"ErrInviteVersion", inviteVersion + ": it is at version 0x0002, and this build reads only 0x0003",
         "urmessage: this invite is at a version this build does not read: version 2", Ask::Join,
         JoinCopy::InviteVersion, AddCopy::Other},
        {L"ErrGroupPredatesJoinSeal", refused + ": " + predates,
         "urmessage: this group was founded before invitations were sealed, so it is not opened here: x",
         Ask::Join, JoinCopy::GroupPredatesJoinSeal, AddCopy::Other},
        {L"ErrGroupRecordNotReadNow", restoreRecordHeld,
         restoreSentence + ": urmessage: a file of a group on this device's disk could not be read just "
                           "now, and it is tried again at the next Restore: x",
         Ask::Join, JoinCopy::GroupRecordNotReadNow, AddCopy::Other},
        {L"ErrGroupRecordNotReadNow-09018b1", restoreSentence + ": " + notReadNowAt09018b1 + ": x",
         restoreSentence + ": urmessage: a group record on this device's disk could not be read just "
                           "now; try again: x",
         Ask::Join, JoinCopy::GroupRecordNotReadNow, AddCopy::Other},
        {L"sentinel", restoreEpochHeld,
         restoreSentence + ": urmessage: a file of a group on this device's disk could not be read just "
                           "now, and it is tried again at the next Restore: group " + hex + ": x",
         Ask::Sentinel, JoinCopy::Other, AddCopy::Other},
        {L"join-gate-unread", joinRecordHeld,
         "urmessage: whether this device already holds group " + hex +
             " could not be read, and this invite waits for it: x",
         Ask::Join, JoinCopy::GroupRecordNotReadNow, AddCopy::Other},
        {L"leave-unread", leaveRecordHeld,
         "urmessage: whether this device holds group " + hex +
             " on its disk could not be read, and it was kept as it was: x",
         Ask::Leave, JoinCopy::Other, AddCopy::Other},
        {L"ErrKeyPackageInvalid", invalidKp + ": it does not decode: syntax: short",
         "urmessage: this join code is not a valid key package: its signature: x", Ask::Add,
         JoinCopy::Other, AddCopy::KeyPackageInvalid},
        {L"ErrJoinerCannotOpenSealedInvite", cannotOpen,
         "urmessage: this join code was made by a build that cannot open a sealed invite; ask them to "
         "update",
         Ask::Add, JoinCopy::Other, AddCopy::JoinerCannotOpenSealedInvite},
        {L"ErrJoinerAlreadyHasALeaf", hasALeaf + ": urmessage: this identity is claimed",
         "urmessage: this identity already has an entry in this group; remove it before this join "
         "code: x",
         Ask::Add, JoinCopy::Other, AddCopy::JoinerAlreadyHasALeaf},
        {L"ErrNoDeviceWrapKey",
         noWrapKey + ", and so it publishes no join code: an invite to one is sealed to that leaf",
         "urmessage: this device holds no x-wing seed for the leaf it publishes, and it publishes no "
         "join code",
         Ask::WrapKey, JoinCopy::Other, AddCopy::Other},
        // round 3 (907109f): R29's sentinel, and R32's not connected, bare and in the leave's clause
        {L"ErrGroupReadUnproven", r3RestoreRecordDenied,
         restoreSentence + ": urmessage: a file of a group on this device's disk could not be read, and "
                           "this build cannot tell whether that will pass; it is kept as it was: x",
         Ask::Join, JoinCopy::GroupReadUnproven, AddCopy::Other},
        {L"ErrNotConnected", notConnected,
         "urmessage: this device has not said Hello, so nothing that needs the connection can run",
         Ask::NotConnected, JoinCopy::Other, AddCopy::Other},
        {L"leave-not-tried", r3LeaveNotTried,
         "urmessage: group " + hex + "'s restore could not be tried again for the leave; it is kept: x",
         Ask::NotConnected, JoinCopy::Other, AddCopy::Other},
        // round 5 (93cc0afb85b1): the store's own refusal under the window's sentence (R34 A1), and
        // group_open's double failure (R35 A8)
        {L"ErrStateStoreState-in-window", r5TakeDenied,
         refused + ": urmessage: the durable state store could not be read or written, so the join "
                   "code stayed where it was: x",
         Ask::Join, JoinCopy::JoinRefusedByStore, AddCopy::Other},
        {L"open-creation-unrecorded", r5OpenDoubleFailure,
         "urmessage: this group is open on the server and its record is on this device's disk, but this "
         "store could not record that the group's creation finished; the next Restore reads it: x",
         Ask::Open, JoinCopy::Other, AddCopy::Other},
        // FIX 6 (9df6fca8414f): a leave mark that names another group
        {L"leave-mark-foreign", r6Foreign,
         format + ": the leave mark in " + dirName + " does not name its own group, and it decides "
                                                     "nothing here: x",
         Ask::Join, JoinCopy::LeaveMarkForeign, AddCopy::Other},
        // R37 A9: group_open's record-persist failure
        {L"open-record-not-persisted", r5OpenNotPersisted,
         "urmessage: this group is open on the server and its record could not be persisted; nothing "
         "can be sent in it after a restart: x",
         Ask::OpenNotSaved, JoinCopy::Other, AddCopy::Other},
    };
    auto right = [](Case const& c, std::string const& text) {
      switch (c.ask) {
        case Ask::Join: return ClassifyJoinCopy(text) == c.join;
        case Ask::Add: return ClassifyAddCopy(text) == c.add;
        case Ask::WrapKey: return IsNoDeviceWrapKey(text);
        case Ask::Leave: return IsLeaveNotReadNow(text);
        case Ask::Sentinel: return IsGroupRecordNotReadNow(text);
        case Ask::NotConnected: return IsNotConnected(text);
        case Ask::Open: return IsCreationUnrecorded(text);
        case Ask::OpenNotSaved: return IsOpenRecordNotPersisted(text);
      }
      return false;
    };
    std::wstring table;
    int wholeRight = 0, rewordedRight = 0;
    for (auto const& c : cases) {
      const bool w = right(c, c.whole);
      const bool r = right(c, c.reworded);
      if (w) ++wholeRight;
      if (r) ++rewordedRight;
      if (!table.empty()) table += L", ";
      table += std::format(L"{} whole {} reworded {}", c.name, w ? L"ok" : L"WRONG", r ? L"ok" : L"WRONG");
    }
    const int total = static_cast<int>(std::size(cases));
    out.push_back(std::format(
        L"  stable parts     : {}  whole {}/{}, tail reworded {}/{}: {}   [query: R27, each library "
        L"message is matched by the clause that names its condition and never by its whole sentence, "
        L"so a reworded tail, or R26's reworded subject, still reaches its row]",
        Verdict(wholeRight == total && rewordedRight == total), wholeRight, total, rewordedRight, total,
        table));
  }

  // 12. T1 FIX 2's roads (R27), one case per road from e0d16ea's exact text, and what the app does
  //     on each.
  {
    const bool rRecord = RestoreNote(restoreRecordHeld, 0) == kSayGroupRecordNotReadNow &&
                         StepAfterRestoreFirst(false, restoreRecordHeld) == HeldLeaveStep::NotReadNow;
    const bool rEpoch = RestoreNote(restoreEpochHeld, 0) == kSayGroupRecordNotReadNow &&
                        StepAfterRestoreFirst(false, restoreEpochHeld) == HeldLeaveStep::NotReadNow;
    const bool rDamaged = RestoreNote(restoreRecordDenied, 0).empty() &&
                          StepAfterRestoreFirst(false, restoreRecordDenied) == HeldLeaveStep::Forget &&
                          ClassifyJoinRefusal(heldOverDenied) == JoinRefusal::HeldNotLive &&
                          HeldNotLiveCopy(RestoreReadClass(restoreRecordDenied)).offerLeave;
    const bool lHeld = IsLeaveNotReadNow(leaveRecordHeld) && ClassifyJoinCopy(leaveRecordHeld) == JoinCopy::Other &&
                       !IsGroupRecordNotReadNow(leaveRecordHeld);
    const bool jHeld = ClassifyJoinCopy(joinRecordHeld) == JoinCopy::GroupRecordNotReadNow &&
                       ClassifyJoinRefusal(joinRecordHeld) == JoinRefusal::Other;
    const bool cHeld = ClassifyJoinCopy(joinCodeHeld) == JoinCopy::JoinRefusedByStore &&
                       !OffersNewJoinCode(ClassifyJoinCopy(joinCodeHeld));
    const bool cNever = ClassifyJoinCopy(joinCodeNeverReads) == JoinCopy::NoJoinCodeForInvite &&
                        OffersNewJoinCode(ClassifyJoinCopy(joinCodeNeverReads));
    const bool cRemoved = ClassifyJoinCopy(joinCodeRemoved) == JoinCopy::JoinCodeSpent &&
                          OffersNewJoinCode(ClassifyJoinCopy(joinCodeRemoved));
    const bool ok = rRecord && rEpoch && rDamaged && lHeld && jHeld && cHeld && cNever && cRemoved;
    out.push_back(std::format(
        L"  e0d16ea roads    : {}  restore, a record held: R25 Q5's row, and a leave stops {} | restore, "
        L"an epoch file held: the same {} | restore, a record that will never read: no note, the held "
        L"refusal over it offers the leave, and the leave forgets {} | a leave while the record is "
        L"held (REFUSED): R25 Q5's row {} | a join while the record is held: R25 Q5's row {} | a join "
        L"code held for a moment: \"still good\" (R34 A1's row), no new code {} | a join code that "
        L"will never read: no join code, a new one offered {} | a take whose remove landed: spent, a "
        L"new one offered {}   "
        L"[query: T1 FIX 2's \"The sentences a person meets now\", each typed from e0d16ea and each "
        L"answered as R25, R27 and R28 rule]",
        Verdict(ok), rRecord, rEpoch, rDamaged, lHeld, jHeld, cHeld, cNever, cRemoved));
  }

  // 13. R28, R29, R32: the held-group leave after its restore-first, and a held refusal after a
  //     restore that could not read a file of a group.
  {
    const std::string listing = storeState + ": C:\\u\\live\\state\\state\\group could not be "
                                             "read: open C:\\u\\live\\state\\state\\group: " + denied;
    const bool cameBack = StepAfterRestoreFirst(true, restoreEpochHeld) == HeldLeaveStep::CameBack &&
                          StepAfterRestoreFirst(true, "") == HeldLeaveStep::CameBack &&
                          StepAfterRestoreFirst(true, r3RestoreEpochDenied) == HeldLeaveStep::CameBack;
    const bool recordStops = StepAfterRestoreFirst(false, restoreRecordHeld) == HeldLeaveStep::NotReadNow;
    const bool epochStops = StepAfterRestoreFirst(false, restoreEpochHeld) == HeldLeaveStep::NotReadNow;
    const bool unprovenStops = StepAfterRestoreFirst(false, r3RestoreRecordDenied) == HeldLeaveStep::Unproven &&
                               StepAfterRestoreFirst(false, r3RestoreEpochDenied) == HeldLeaveStep::Unproven;
    const bool notConnectedStops = StepAfterRestoreFirst(false, notConnected) == HeldLeaveStep::NotConnected;
    const bool damagedForgets = StepAfterRestoreFirst(false, restoreRecordDenied) == HeldLeaveStep::Forget &&
                                StepAfterRestoreFirst(false, r3RestoreRecordBroken) == HeldLeaveStep::Forget &&
                                StepAfterRestoreFirst(false, r3RestoreEpochBroken) == HeldLeaveStep::Forget;
    const bool listingForgets = StepAfterRestoreFirst(false, listing) == HeldLeaveStep::Forget;
    const bool emptyForgets = StepAfterRestoreFirst(false, "") == HeldLeaveStep::Forget;
    const HeldCopy stopQ5 = LeaveStopCopy(HeldLeaveStep::NotReadNow);
    const HeldCopy stopUnproven = LeaveStopCopy(HeldLeaveStep::Unproven);
    const HeldCopy stopOffline = LeaveStopCopy(HeldLeaveStep::NotConnected);
    const HeldCopy heldQ5 = HeldNotLiveCopy(ReadClass::NotReadNow);
    const HeldCopy heldUnproven = HeldNotLiveCopy(ReadClass::Unproven);
    const HeldCopy offer = HeldNotLiveCopy(ReadClass::None);
    const bool q5Says = stopQ5.sentence == kSayGroupRecordNotReadNow && !stopQ5.offerLeave &&
                        heldQ5.sentence == kSayGroupRecordNotReadNow && !heldQ5.offerLeave;
    const bool unprovenSays = stopUnproven.sentence == kSayGroupReadUnproven && !stopUnproven.offerLeave &&
                              heldUnproven.sentence == kSayGroupReadUnproven && !heldUnproven.offerLeave;
    const bool offlineSays = stopOffline.sentence == kSayNotConnected && stopOffline.offerLeave;
    const bool offerSays = offer.sentence == kHeldNotLiveSentence && offer.offerLeave;
    // the flag a restore leaves: each class by its own sentinel, nothing else, and a restore that was
    // not tried leaves it as it was
    const bool flagSet = RestoreReadClass(restoreRecordHeld) == ReadClass::NotReadNow &&
                         RestoreReadClass(restoreEpochHeld) == ReadClass::NotReadNow &&
                         RestoreReadClass(r3RestoreRecordDenied) == ReadClass::Unproven &&
                         RestoreReadClass(r3RestoreEpochDenied) == ReadClass::Unproven;
    const bool flagNot = RestoreReadClass(restoreRecordDenied) == ReadClass::None &&
                         RestoreReadClass(r3RestoreRecordBroken) == ReadClass::None &&
                         RestoreReadClass(listing) == ReadClass::None &&
                         RestoreReadClass(joinRecordHeld) == ReadClass::None &&
                         RestoreReadClass(leaveRecordHeld) == ReadClass::None && RestoreReadClass("") == ReadClass::None;
    const bool flagKept = NextRestoreClass(ReadClass::Unproven, notConnected) == ReadClass::Unproven &&
                          NextRestoreClass(ReadClass::NotReadNow, notConnected) == ReadClass::NotReadNow &&
                          NextRestoreClass(ReadClass::Unproven, r3RestoreRecordBroken) == ReadClass::None &&
                          NextRestoreClass(ReadClass::None, r3RestoreEpochDenied) == ReadClass::Unproven;
    const bool ok = cameBack && recordStops && epochStops && unprovenStops && notConnectedStops && damagedForgets &&
                    listingForgets && emptyForgets && q5Says && unprovenSays && offlineSays && offerSays && flagSet &&
                    flagNot && flagKept;
    out.push_back(std::format(
        L"  held leave       : {}  came back {} | a record held: stop, no forget {} | an epoch file "
        L"held: stop, no forget {} | unproven: stop, no forget {} | not connected: stop, no forget {} | "
        L"damage: forget {} | a listing that failed: forget {} | nothing said: forget {} | Q5's row, no "
        L"leave {} | the unproven row, no leave {} | the not-connected row, the leave kept {} | "
        L"otherwise Task 39's sentence and the leave {} | each sentinel sets its class {} | nothing "
        L"else does {} | a restore not tried keeps the flag {}   [query: R28, R29, R32, a leave whose "
        L"restore-first decided nothing calls no forget_group and says that row, and so does a held "
        L"refusal after a restore that could not read a file]",
        Verdict(ok), cameBack, recordStops, epochStops, unprovenStops, notConnectedStops, damagedForgets,
        listingForgets, emptyForgets, q5Says, unprovenSays, offlineSays, offerSays, flagSet, flagNot, flagKept));
  }

  // 14. ROUND 3'S ROADS (907109f): one case per road from its exact text, and what the app does on
  //     each.
  {
    struct Road {
      const wchar_t* name;
      bool ok;
    };
    auto leave = [](std::string const& text, LeaveRefusal want, std::string_view say, bool offer) {
      const HeldCopy copy = LeaveRefusalCopy(ClassifyLeaveRefusal(text));
      return ClassifyLeaveRefusal(text) == want && copy.sentence == say && copy.offerLeave == offer;
    };
    const Road roads[] = {
        {L"restore-record-held", RestoreNote(restoreRecordHeld, 0) == kSayGroupRecordNotReadNow &&
                                     RestoreReadClass(restoreRecordHeld) == ReadClass::NotReadNow},
        {L"restore-record-denied", RestoreNote(r3RestoreRecordDenied, 0) == kSayGroupReadUnproven &&
                                       RestoreReadClass(r3RestoreRecordDenied) == ReadClass::Unproven &&
                                       StepAfterRestoreFirst(false, r3RestoreRecordDenied) == HeldLeaveStep::Unproven},
        {L"restore-epoch-denied", RestoreNote(r3RestoreEpochDenied, 0) == kSayGroupReadUnproven &&
                                      RestoreReadClass(r3RestoreEpochDenied) == ReadClass::Unproven &&
                                      StepAfterRestoreFirst(false, r3RestoreEpochDenied) == HeldLeaveStep::Unproven},
        {L"restore-record-broken", RestoreNote(r3RestoreRecordBroken, 0).empty() &&
                                       StepAfterRestoreFirst(false, r3RestoreRecordBroken) == HeldLeaveStep::Forget},
        {L"restore-epoch-broken", RestoreNote(r3RestoreEpochBroken, 0).empty() &&
                                      StepAfterRestoreFirst(false, r3RestoreEpochBroken) == HeldLeaveStep::Forget},
        {L"restore-not-connected", RestoreNote(notConnected, 0).empty() &&
                                       StepAfterRestoreFirst(false, notConnected) == HeldLeaveStep::NotConnected},
        {L"leave-fresh-held", leave(r3LeaveFreshHeld, LeaveRefusal::NotReadNow, kSayGroupRecordNotReadNow, false)},
        {L"leave-fresh-denied", leave(r3LeaveFreshDenied, LeaveRefusal::Unproven, kSayGroupReadUnproven, false)},
        {L"leave-record-held", leave(r3LeaveRecordHeld, LeaveRefusal::NotReadNow, kSayGroupRecordNotReadNow, false)},
        {L"leave-record-denied", leave(r3LeaveRecordDenied, LeaveRefusal::Unproven, kSayGroupReadUnproven, false)},
        {L"leave-read-again", leave(r3LeaveReadAgain, LeaveRefusal::Unproven, kSayGroupReadUnproven, false)},
        {L"leave-not-tried", leave(r3LeaveNotTried, LeaveRefusal::NotConnected, kSayNotConnected, true)},
        {L"leave-mark-unwritten", ClassifyLeaveRefusal(r3LeaveMarkUnwritten) == LeaveRefusal::Other &&
                                      LeaveRefusalCopy(LeaveRefusal::Other).sentence.empty()},
        {L"leave-not-restored", ClassifyLeaveRefusal(r3NotRestored) == LeaveRefusal::Other &&
                                    IsNotRestoredRefusal(r3NotRestored)},
        // R33 X2 changed this road: the owner's question is told apart, so the app restores once
        // more (its own row is none; R33 roads asks the rest)
        {L"leave-owner", ClassifyLeaveRefusal(r3OwnerMustTransfer) == LeaveRefusal::OwnerMustTransfer &&
                             LeaveRefusalCopy(ClassifyLeaveRefusal(r3OwnerMustTransfer)).sentence.empty()},
        {L"join-record-held", ClassifyJoinCopy(r3JoinRecordHeld) == JoinCopy::GroupRecordNotReadNow &&
                                  ClassifyJoinRefusal(r3JoinRecordHeld) == JoinRefusal::Other &&
                                  !OffersNewJoinCode(ClassifyJoinCopy(r3JoinRecordHeld))},
        {L"join-record-denied", ClassifyJoinCopy(r3JoinRecordDenied) == JoinCopy::GroupReadUnproven &&
                                    JoinCopySentence(ClassifyJoinCopy(r3JoinRecordDenied)) == kSayGroupReadUnproven &&
                                    ClassifyJoinRefusal(r3JoinRecordDenied) == JoinRefusal::Other &&
                                    !OffersNewJoinCode(ClassifyJoinCopy(r3JoinRecordDenied))},
        {L"join-record-broken", ClassifyJoinRefusal(r3JoinRecordBroken) == JoinRefusal::HeldNotLive &&
                                    HeldNotLiveCopy(ReadClass::None).offerLeave},
        {L"held-after-held-restore", ClassifyJoinRefusal(r3JoinRecordReads) == JoinRefusal::HeldNotLive &&
                                         HeldNotLiveCopy(RestoreReadClass(restoreEpochHeld)).sentence ==
                                             kSayGroupRecordNotReadNow &&
                                         !HeldNotLiveCopy(RestoreReadClass(restoreEpochHeld)).offerLeave},
        {L"held-after-denied-restore", HeldNotLiveCopy(RestoreReadClass(r3RestoreEpochDenied)).sentence ==
                                           kSayGroupReadUnproven &&
                                       !HeldNotLiveCopy(RestoreReadClass(r3RestoreEpochDenied)).offerLeave},
        {L"take-held", ClassifyJoinCopy(joinCodeHeld) == JoinCopy::JoinRefusedByStore &&
                           !OffersNewJoinCode(ClassifyJoinCopy(joinCodeHeld))},
        {L"take-denied", ClassifyJoinCopy(joinCodeNeverReads) == JoinCopy::NoJoinCodeForInvite &&
                             OffersNewJoinCode(ClassifyJoinCopy(joinCodeNeverReads))},
        {L"take-broken", ClassifyJoinCopy(r3TakeBroken) == JoinCopy::NoJoinCodeForInvite &&
                             OffersNewJoinCode(ClassifyJoinCopy(r3TakeBroken))},
        {L"take-removed", ClassifyJoinCopy(joinCodeRemoved) == JoinCopy::JoinCodeSpent &&
                              OffersNewJoinCode(ClassifyJoinCopy(joinCodeRemoved))},
    };
    std::wstring table;
    int right = 0;
    for (auto const& r : roads) {
      if (r.ok) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{} {}", r.name, r.ok ? L"ok" : L"WRONG");
    }
    const int total = static_cast<int>(std::size(roads));
    out.push_back(std::format(
        L"  907109f roads    : {}  {}/{}: {}   [query: T1 FIX 3's roads, each typed from 907109f: the "
        L"unproven row on the restore, the leave's restore-first, the leave's own refusals and the join; "
        L"Q5's row for the transient class; the not-connected row, with the leave kept, for a leave not "
        L"tried; Task 39's fallback for a leave mark that could not be written; and a join code's take "
        L"never \"still good\" unless it was held for a moment]",
        Verdict(right == total), right, total, table));
  }

  // 15. R33 (the library's fourth round, FIX 4): the roads it changes, each typed from its head
  //     fdc1b0aa3c31: errors.go, group.go's Join (its window's one exit), messagegroup/engine.go's
  //     refused key packages, statestore_durable.go's readRecord, restore.go, forget.go's ForgetGroup
  //     and restoreForLeave, and heldgroup.go's joinOverHeldGroup. FIX 5 (93cc0afb85b1) reworded none
  //     of these sentences; the take's row is R34 A1's since then.
  {
    struct Road {
      const wchar_t* name;
      bool ok;
    };
    const std::string kpRef(64, 'e');
    // readRecord's absence: ErrStateNotFound, then the file's own name
    const std::string absentEpoch = "urmessage: this state store holds no such value: 0000000000000001";
    // X1: a take the store refused for a reason it cannot prove permanent (here a permission refused)
    // removed nothing, so it answers the window's sentence (group.go, Join's deferred refusal)
    const std::string x1TakeUnproven = refused + ": " + engineNoCode + takeTail + storeState + ": " + kpFile +
                                       " could not be read: open " + kpFile + ": " + denied;
    // and a take that finds no such join code is still no join code
    const std::string x1TakeNoSuchRef = noCode + ": " + engineNoCode + takeTail +
                                        "urmessage: this state store holds no such value: " + kpRef;
    // R33 (1): a group's file that is absent is unproven, at the restore (restore.go) and at the leave's
    // fresh re-try of it (forget.go, restoreForLeave)
    const std::string absentRestore = restoreSentence + ": " + unproven + ": group " + hex + ": " +
                                      restoreSentence + ": group " + hex + " at epoch 1: " + absentEpoch;
    const std::string absentLeave = unproven + "; so nothing was left: group " + hex + ": " + restoreSentence +
                                    ": group " + hex + " at epoch 1: " + absentEpoch;
    // and a record absent from a group directory that stands, at the leave (forget.go, ForgetGroup)
    const std::string absentRecordLeave = "urmessage: whether this device holds group " + hex +
                                          " on its disk could not be read, so nothing was left: " + unproven +
                                          ": its directory stands and its record is not in it";
    // X3: the gate's answer for a group the last Restore could not read, by the class's own sentinel
    // and with no held sentence (heldgroup.go, joinOverHeldGroup)
    const std::string x3Gate = "urmessage: group " + hex +
                               " is on this device's disk and the last Restore could not read it, so the "
                               "invite into it is refused: ";
    const std::string x3Unproven = x3Gate + unproven;
    const std::string x3Transient = x3Gate + notReadNow;
    // X3: ErrGroupNotRestored's remedy scoped to damage, its stable part unchanged (errors.go; forget.go
    // formats it "%w: %x")
    const std::string x3NotRestored = "urmessage: this group is on this device's disk and has not been "
                                      "restored; call Restore, and if it does not come back because it is "
                                      "damaged, ForgetGroup leaves it: " + hex;
    const Road roads[] = {
        {L"x1-take-unproven", ClassifyJoinCopy(x1TakeUnproven) == JoinCopy::JoinRefusedByStore &&
                                  !OffersNewJoinCode(ClassifyJoinCopy(x1TakeUnproven))},
        {L"x1-take-no-such-ref", ClassifyJoinCopy(x1TakeNoSuchRef) == JoinCopy::NoJoinCodeForInvite &&
                                     OffersNewJoinCode(ClassifyJoinCopy(x1TakeNoSuchRef))},
        {L"absent-restore", RestoreNote(absentRestore, 0) == kSayGroupReadUnproven &&
                                RestoreReadClass(absentRestore) == ReadClass::Unproven &&
                                StepAfterRestoreFirst(false, absentRestore) == HeldLeaveStep::Unproven},
        {L"absent-leave", ClassifyLeaveRefusal(absentLeave) == LeaveRefusal::Unproven &&
                              LeaveRefusalCopy(ClassifyLeaveRefusal(absentLeave)).sentence ==
                                  kSayGroupReadUnproven &&
                              !LeaveRefusalCopy(ClassifyLeaveRefusal(absentLeave)).offerLeave},
        {L"absent-record-leave", ClassifyLeaveRefusal(absentRecordLeave) == LeaveRefusal::Unproven &&
                                     LeaveRefusalCopy(ClassifyLeaveRefusal(absentRecordLeave)).sentence ==
                                         kSayGroupReadUnproven &&
                                     !LeaveRefusalCopy(ClassifyLeaveRefusal(absentRecordLeave)).offerLeave &&
                                     !IsNotRestoredRefusal(absentRecordLeave)},
        {L"x2-owner", ClassifyLeaveRefusal(r3OwnerMustTransfer) == LeaveRefusal::OwnerMustTransfer &&
                          RestoresOnceMore(ClassifyLeaveRefusal(r3OwnerMustTransfer)) &&
                          LeaveRefusalCopy(LeaveRefusal::OwnerMustTransfer).sentence.empty()},
        {L"x2-only-the-owner", !RestoresOnceMore(LeaveRefusal::Other) && !RestoresOnceMore(LeaveRefusal::NotReadNow) &&
                                   !RestoresOnceMore(LeaveRefusal::Unproven) &&
                                   !RestoresOnceMore(LeaveRefusal::NotConnected) &&
                                   !RestoresOnceMore(ClassifyLeaveRefusal(r3LeaveMarkUnwritten))},
        {L"x3-unproven", ClassifyJoinCopy(x3Unproven) == JoinCopy::GroupReadUnproven &&
                             JoinCopySentence(ClassifyJoinCopy(x3Unproven)) == kSayGroupReadUnproven &&
                             !OffersNewJoinCode(ClassifyJoinCopy(x3Unproven)) &&
                             ClassifyJoinRefusal(x3Unproven) == JoinRefusal::Other},
        {L"x3-transient", ClassifyJoinCopy(x3Transient) == JoinCopy::GroupRecordNotReadNow &&
                              JoinCopySentence(ClassifyJoinCopy(x3Transient)) == kSayGroupRecordNotReadNow &&
                              !OffersNewJoinCode(ClassifyJoinCopy(x3Transient)) &&
                              ClassifyJoinRefusal(x3Transient) == JoinRefusal::Other},
        {L"x3-damage-stays-held", ClassifyJoinRefusal(r3JoinRecordBroken) == JoinRefusal::HeldNotLive &&
                                      ClassifyJoinCopy(r3JoinRecordBroken) == JoinCopy::Other &&
                                      ClassifyJoinRefusal(r3JoinRecordReads) == JoinRefusal::HeldNotLive &&
                                      ClassifyJoinCopy(r3JoinRecordReads) == JoinCopy::Other},
        {L"x3-not-restored", IsNotRestoredRefusal(x3NotRestored) &&
                                 ClassifyLeaveRefusal(x3NotRestored) == LeaveRefusal::Other},
    };
    std::wstring table;
    int right = 0;
    for (auto const& r : roads) {
      if (r.ok) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{} {}", r.name, r.ok ? L"ok" : L"WRONG");
    }
    const int total = static_cast<int>(std::size(roads));
    out.push_back(std::format(
        L"  R33 roads        : {}  {}/{}: {}   [query: R33, typed from fdc1b0aa3c31: a group's absent "
        L"file, and a record absent from a directory that stands, is the unproven row with no leave; a "
        L"take refused for a reason not proven permanent is R34 A1's \"still good\" row, and no such "
        L"ref stays no join code (R13); the leave's owner question restores once more; the gate's "
        L"answer for a group the last Restore could not read is the class's row, never Task 39's held "
        L"row; ErrGroupNotRestored scoped to damage keeps its stable part]",
        Verdict(right == total), right, total, table));
  }

  // 16. R34 and R35 (the library's fifth round, FIX 5): the rows A1 to A8 rule, each road typed from
  //     93cc0afb85b1 (heldgroup.go's gate, forget.go's ForgetGroup, group.go's Join and Open,
  //     joinstage.go's commit, statestore_durable.go's PutGroupRecord, writeRecord and removeLocked).
  {
    struct Road {
      const wchar_t* name;
      bool ok;
    };
    // R34 A1's order: no library sentence at 93cc0afb85b1 carries a class's sentinel under the window's
    // sentence, and the order is the rule all the same: the class's row
    const std::string afterClass = refused + ": " + unproven + ": " + storeState + ": " + epochFile +
                                   " could not be read: open " + epochFile + ": " + denied;
    const std::string transit = refused + ": urmessage: the join seal's authenticator does not verify";
    const Road roads[] = {
        // A7: the gate's refusal of a record missing from a folder where the store's own files stand
        {L"a7-gate-record-missing",
         ClassifyJoinCopy(r5GateRecordMissing) == JoinCopy::GroupReadUnproven &&
             JoinCopySentence(ClassifyJoinCopy(r5GateRecordMissing)) == kSayGroupReadUnproven &&
             ClassifyJoinRefusal(r5GateRecordMissing) == JoinRefusal::Other &&
             !OffersNewJoinCode(ClassifyJoinCopy(r5GateRecordMissing)) && !IsLeaveNotReadNow(r5GateRecordMissing)},
        // ForgetGroup's not-held answer: the bridge answers it FORGET_FAILED, and its words are no row's
        {L"forget-not-held", ClassifyLeaveRefusal(r5ForgetNotHeld) == LeaveRefusal::Other &&
                                 !IsNotRestoredRefusal(r5ForgetNotHeld) &&
                                 ClassifyJoinCopy(r5ForgetNotHeld) == JoinCopy::Other},
        // A8: the double failure is the spent row at a join, and A8's row at the founding Open
        {L"a8-join-double-failure", ClassifyJoinCopy(r5JoinDoubleFailure) == JoinCopy::JoinCodeSpent &&
                                        OffersNewJoinCode(ClassifyJoinCopy(r5JoinDoubleFailure)) &&
                                        !IsCreationUnrecorded(r5JoinDoubleFailure)},
        {L"a8-open-double-failure", IsCreationUnrecorded(r5OpenDoubleFailure) &&
                                        ClassifyJoinCopy(r5OpenDoubleFailure) == JoinCopy::Other &&
                                        ClassifyAddCopy(r5OpenDoubleFailure) == AddCopy::Other},
        {L"a8-open-not-persisted", !IsCreationUnrecorded(r5OpenNotPersisted)},
        // A1: a refusal the store made inside the window, with no new join code
        {L"a1-take-denied", ClassifyJoinCopy(r5TakeDenied) == JoinCopy::JoinRefusedByStore &&
                                JoinCopySentence(ClassifyJoinCopy(r5TakeDenied)) == kSayJoinRefusedByStore &&
                                !OffersNewJoinCode(ClassifyJoinCopy(r5TakeDenied))},
        {L"a1-take-held", ClassifyJoinCopy(joinCodeHeld) == JoinCopy::JoinRefusedByStore},
        {L"a1-commit-refused", ClassifyJoinCopy(r5CommitRefused) == JoinCopy::JoinRefusedByStore},
        {L"a1-transit", ClassifyJoinCopy(transit) == JoinCopy::JoinRefused &&
                            JoinCopySentence(ClassifyJoinCopy(transit)) == kSayJoinRefused},
        {L"a1-after-class", ClassifyJoinCopy(afterClass) == JoinCopy::GroupReadUnproven},
    };
    std::wstring table;
    int right = 0;
    for (auto const& r : roads) {
      if (r.ok) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{} {}", r.name, r.ok ? L"ok" : L"WRONG");
    }
    const int total = static_cast<int>(std::size(roads));
    out.push_back(std::format(
        L"  R34 roads        : {}  {}/{}: {}   [query: R34 and R35, typed from 93cc0afb85b1: the gate's "
        L"refusal of a record missing from a folder where the store's files stand is A3's unproven "
        L"row with no leave (A7); ForgetGroup's not-held answer is no row's; the double failure is the "
        L"spent row at a join and A8's row at the founding open, and the open's other refusal keeps "
        L"its own words; a refusal the store made inside the window is A1's row with no new join code, "
        L"after every class row, and the window's other refusals keep its own row]",
        Verdict(right == total), right, total, table));
  }

  // 17. R37 A9: group_open's refusal of a group open on the server whose record could not be persisted,
  //     typed from 93cc0afb85b1 (group.go, Open), and the founding road's row for it. The road's own
  //     "could not be published to the server" stays for every other refusal of the open.
  {
    const std::string reworded =
        "urmessage: this group is open on the server and its record could not be persisted; nothing "
        "can be sent in it after a restart: x";
    const std::string alreadyOpen = "urmessage: this group is already open on the server";
    const std::string sealing = "urmessage: sealing the founding commit: x";
    const bool whole = OpenRefusalCopy(r5OpenNotPersisted) == kSayOpenRecordNotSaved;
    const bool tail = OpenRefusalCopy(reworded) == kSayOpenRecordNotSaved;
    const bool notTheDoubleFailure = OpenRefusalCopy(r5OpenDoubleFailure) == kSayCreationUnfinished;
    const bool othersKeep = OpenRefusalCopy(alreadyOpen).empty() && OpenRefusalCopy(sealing).empty() &&
                            OpenRefusalCopy("").empty();
    const std::string_view say = OpenRefusalCopy(r5OpenNotPersisted);
    const bool verbatim =
        say == "URmessage could not save this new conversation on this device, so it cannot be used, and "
               "its invitation was not shown. Check that URmessage can read and write its folder, then "
               "start the conversation again.";
    const bool notShown = say.find("its invitation was not shown") != std::string_view::npos;
    const bool own = say.find("urmessage:") == std::string_view::npos && say != kSayCreationUnfinished;
    const bool ok = whole && tail && notTheDoubleFailure && othersKeep && verbatim && notShown && own;
    out.push_back(std::format(
        L"  open not saved   : {}  the whole sentence {} | its tail reworded {} | the double failure "
        L"keeps A8's row {} | the open's other refusals keep the road's own words {} | the row verbatim "
        L"{} | it says the invitation was not shown {} | its own words {}   [query: R37 A9, a group "
        L"open on the server whose record could not be persisted is told \"{}\", and not that it could "
        L"not be published]",
        Verdict(ok), whole, tail, notTheDoubleFailure, othersKeep, verbatim, notShown, own, Widen(say)));
  }

  // 18. R35 A8, as R37 amends it: group_open's double failure, and the founding road's row for it.
  {
    const std::string reworded =
        "urmessage: this group is open on the server and its record is on this device's disk, but this "
        "store could not record that the group's creation finished; the next Restore reads it: x";
    const bool whole = OpenRefusalCopy(r5OpenDoubleFailure) == kSayCreationUnfinished;
    const bool tail = OpenRefusalCopy(reworded) == kSayCreationUnfinished;
    const bool notTheRecordFailure = OpenRefusalCopy(r5OpenNotPersisted) != kSayCreationUnfinished;
    const std::string_view say = OpenRefusalCopy(r5OpenDoubleFailure);
    const bool verbatim =
        say == "This conversation was created, but URmessage could not finish saving it, and its "
               "invitation was not shown. Close anything else that may be using URmessage's folder, then "
               "open URmessage again.";
    const bool notShown = say.find("its invitation was not shown") != std::string_view::npos;
    const bool own = say.find("urmessage:") == std::string_view::npos && say != kSayOpenRecordNotSaved;
    const bool ok = whole && tail && notTheRecordFailure && verbatim && notShown && own;
    out.push_back(std::format(
        L"  open unrecorded  : {}  the whole sentence {} | its tail reworded {} | the record failure is "
        L"not it {} | the row verbatim {} | it says the invitation was not shown {} | its own words {}   "
        L"[query: R35 A8 as R37 amends it, group_open's double failure is told \"{}\"]",
        Verdict(ok), whole, tail, notTheRecordFailure, verbatim, notShown, own, Widen(say)));
  }

  // 19. R38: FIX 6's sentences (9df6fca8414f), each road typed from forget.go, restore.go and
  //     statestore_durable.go there. Every mark that cannot be held against what stands carries its
  //     class's sentence, and the class's row answers it; the stale mark at the erase is
  //     ErrGroupNotRestored's answer; the epoch a leave could not read keeps Task 39's fallback; and a
  //     mark that names another group has its own row, a draft for the lead's ruling.
  {
    struct Road {
      const wchar_t* name;
      bool ok;
    };
    const std::string leaveTransient = r6LeaveUndecided + notReadNow + r6RecordAboveUnread + sharing;
    const std::string leaveUnproven = r6LeaveUndecided + unproven + r6MarkUnread + denied;
    const std::string leaveNoEpoch = r6LeaveUndecided + unproven + r6NoEpoch;
    const std::string joinUnproven = r6JoinUndecided + unproven + r6EpochAboveUnread + denied;
    const std::string joinTransient = r6JoinUndecided + notReadNow + r6StaleUnremoved + sharing;
    const std::string restoreUndecided = notReadNow + r6MarkUnread + sharing;
    const std::string recordNamesAnother = damaged + ": " + format + ": the record in " + dirName +
                                           " does not name its own group";
    // the order is the rule: a text that carries a class's sentence and the foreign clause is the
    // class's (no library sentence at 9df6fca8414f carries both)
    const std::string foreignUnderClass = r6LeaveUndecided + unproven + ": " + r6Foreign;
    auto leaveRow = [](std::string const& text, LeaveRefusal want, std::string_view say, bool leave) {
      const LeaveRefusal got = ClassifyLeaveRefusal(text);
      return got == want && LeaveRefusalCopy(got).sentence == say && LeaveRefusalCopy(got).offerLeave == leave &&
             !IsNotRestoredRefusal(text);
    };
    const Road roads[] = {
        {L"leave-undecided-transient", leaveRow(leaveTransient, LeaveRefusal::NotReadNow, kSayGroupRecordNotReadNow, false)},
        {L"leave-undecided-unproven", leaveRow(leaveUnproven, LeaveRefusal::Unproven, kSayGroupReadUnproven, false)},
        {L"leave-mark-no-epoch", leaveRow(leaveNoEpoch, LeaveRefusal::Unproven, kSayGroupReadUnproven, false)},
        {L"join-undecided-unproven", ClassifyJoinCopy(joinUnproven) == JoinCopy::GroupReadUnproven &&
                                         ClassifyJoinRefusal(joinUnproven) == JoinRefusal::Other &&
                                         !OffersNewJoinCode(ClassifyJoinCopy(joinUnproven))},
        {L"join-undecided-transient", ClassifyJoinCopy(joinTransient) == JoinCopy::GroupRecordNotReadNow &&
                                          ClassifyJoinRefusal(joinTransient) == JoinRefusal::Other},
        {L"restore-undecided", RestoreNote(restoreUndecided, 0) == kSayGroupRecordNotReadNow &&
                                   RestoreReadClass(restoreUndecided) == ReadClass::NotReadNow &&
                                   StepAfterRestoreFirst(false, restoreUndecided) == HeldLeaveStep::NotReadNow},
        {L"leave-stale-at-erase", IsNotRestoredRefusal(r6StaleAtErase) &&
                                      ClassifyLeaveRefusal(r6StaleAtErase) == LeaveRefusal::Other},
        {L"leave-mark-unlisted", ClassifyLeaveRefusal(r6MarkUnlisted) == LeaveRefusal::Other &&
                                     !IsNotRestoredRefusal(r6MarkUnlisted) &&
                                     LeaveRefusalCopy(LeaveRefusal::Other).sentence.empty()},
        {L"join-mark-foreign", ClassifyJoinCopy(r6Foreign) == JoinCopy::LeaveMarkForeign &&
                                   JoinCopySentence(ClassifyJoinCopy(r6Foreign)) == kSayLeaveMarkForeign &&
                                   !OffersNewJoinCode(ClassifyJoinCopy(r6Foreign)) &&
                                   ClassifyJoinRefusal(r6Foreign) == JoinRefusal::Other},
        {L"leave-mark-foreign", leaveRow(r6Foreign, LeaveRefusal::MarkForeign, kSayLeaveMarkForeign, false) &&
                                    !RestoresOnceMore(LeaveRefusal::MarkForeign)},
        {L"record-not-mark", !IsLeaveMarkForeign(recordNamesAnother) &&
                                 ClassifyJoinCopy(recordNamesAnother) != JoinCopy::LeaveMarkForeign},
        {L"foreign-under-class", ClassifyJoinCopy(foreignUnderClass) == JoinCopy::GroupReadUnproven &&
                                     ClassifyLeaveRefusal(foreignUnderClass) == LeaveRefusal::Unproven},
        // R40 A10: the library's own words follow the row, on both roads, so the folder is named; no
        // other row's do
        {L"foreign-names-the-folder",
         ShowsLibraryWords(JoinCopy::LeaveMarkForeign) && ShowsLibraryWords(LeaveRefusal::MarkForeign) &&
             WithLibraryWords(kSayLeaveMarkForeign, r6Foreign) == std::string(kSayLeaveMarkForeign) + " " + r6Foreign &&
             WithLibraryWords(kSayLeaveMarkForeign, r6Foreign).find(dirName) != std::string::npos &&
             !ShowsLibraryWords(JoinCopy::GroupReadUnproven) && !ShowsLibraryWords(JoinCopy::JoinRefused) &&
             !ShowsLibraryWords(LeaveRefusal::Unproven) && !ShowsLibraryWords(LeaveRefusal::Other)},
    };
    std::wstring table;
    int right = 0;
    for (auto const& r : roads) {
      if (r.ok) ++right;
      if (!table.empty()) table += L", ";
      table += std::format(L"{} {}", r.name, r.ok ? L"ok" : L"WRONG");
    }
    const int total = static_cast<int>(std::size(roads));
    out.push_back(std::format(
        L"  R38 roads        : {}  {}/{}: {}   [query: FIX 6, typed from 9df6fca8414f: a leave mark that "
        L"cannot be held against what stands is its class's row on the leave, the join and the restore, "
        L"with no leave; the stale mark at the erase is ErrGroupNotRestored's answer; the epoch a leave "
        L"could not read keeps Task 39's fallback; a mark that names another group is R40 A10's row, the "
        L"library's words after it, with no leave and no new join code, after every class]",
        Verdict(right == total), right, total, table));
  }
  return out;
}

}  // namespace

void StartupLogInit() {
  const std::filesystem::path logFile = LogDir() / ids::kLogFileName;
  const bool opened = LogInit(logFile, "app");
  g_logOpened = opened;
  // Every line below also goes to OutputDebugString, so a failed open costs the
  // file but not the log. It is reported in the diagnostics rather than in a
  // message box: it does not stop the app, and a box on every launch of a
  // machine with a full disk would be its own bug.
  LogInfo("startup: ----------------------------------------------------------");
  LogInfo("startup: wWinMain (pid {}) exe={}", ::GetCurrentProcessId(),
          Narrow(ExePath().wstring()));
  if (!opened)
    LogWarn("startup: could not open {} — logging to the debugger only",
            Narrow(logFile.wstring()));
}

// THE ICON FONT ON THE MACHINE THIS RUNS ON (urnetwork/windows#12, ported). Every glyph the app draws
// (urnw::kit::kIconGlyphs), and the family of urnw::kit::kIconFontFamily that draws it here: the
// first family of the list that is installed and maps the code point, the one a XAML family list
// falls back to, or, where none maps it, the first that maps its fallback (kIconGlyphs). A glyph that
// nothing draws is an empty box, and fails the line. Where every family of the list is installed the
// line also says which glyphs only the FIRST (Segoe Fluent Icons) maps HERE: on a machine with only the
// second (Segoe MDL2 Assets), each of those is drawn through its fallback, or is an empty box. Which
// those are is that machine's to say: Windows 10's Segoe MDL2 Assets maps fewer glyphs than Windows
// 11's. NO FAMILY IS NAMED HERE BUT THROUGH kIconFontFamily: app/tools/verify-icon-font.ps1 holds every
// string that names an icon font to the list, so the names come from splitting it.
std::wstring IconGlyphsLine() {
  using urnw::kit::kIconFontFamily;
  using urnw::kit::kIconGlyphs;
  // the list's families, in order, each trimmed
  std::vector<std::wstring> families;
  {
    const std::wstring_view list = kIconFontFamily;
    size_t at = 0;
    for (;;) {
      const size_t comma = list.find(L',', at);
      std::wstring_view one =
          list.substr(at, comma == std::wstring_view::npos ? std::wstring_view::npos : comma - at);
      while (!one.empty() && one.front() == L' ') one.remove_prefix(1);
      while (!one.empty() && one.back() == L' ') one.remove_suffix(1);
      if (!one.empty()) families.emplace_back(one);
      if (comma == std::wstring_view::npos) break;
      at = comma + 1;
    }
  }
  winrt::com_ptr<IDWriteFactory> factory;
  HRESULT hr = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<::IUnknown**>(factory.put()));
  winrt::com_ptr<IDWriteFontCollection> system;
  if (SUCCEEDED(hr)) hr = factory->GetSystemFontCollection(system.put(), FALSE);
  if (FAILED(hr) || !system)
    return std::format(L"  icon glyphs      : FAIL  DirectWrite could not list this machine's fonts "
                       L"(0x{:08X})",
                       static_cast<uint32_t>(hr));
  // the installed font of one family name, or none
  auto fontOf = [&](std::wstring_view name) -> winrt::com_ptr<IDWriteFont> {
    UINT32 index = 0;
    BOOL exists = FALSE;
    const std::wstring z(name);
    if (FAILED(system->FindFamilyName(z.c_str(), &index, &exists)) || !exists) return nullptr;
    winrt::com_ptr<IDWriteFontFamily> family;
    if (FAILED(system->GetFontFamily(index, family.put())) || !family) return nullptr;
    winrt::com_ptr<IDWriteFont> font;
    if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, font.put())))
      return nullptr;
    return font;
  };
  auto has = [](winrt::com_ptr<IDWriteFont> const& font, char32_t cp) {
    BOOL exists = FALSE;
    return font && SUCCEEDED(font->HasCharacter(static_cast<UINT32>(cp), &exists)) && exists;
  };
  std::vector<winrt::com_ptr<IDWriteFont>> listFonts;
  std::wstring installed;
  for (auto const& f : families) {
    listFonts.push_back(fontOf(f));
    installed += std::format(L"{}{} {}", installed.empty() ? L"" : L", ", f,
                             listFonts.back() ? L"installed" : L"not installed");
  }
  // THE FAMILY THAT DRAWS EACH GLYPH: the first of the list that maps it, as XAML falls back; where
  // none does, the first that maps its fallback (kIconGlyphs), which IconGlyphText then draws instead
  std::vector<std::wstring> drawnBy(families.size());
  std::wstring none;
  std::wstring throughFallback;
  size_t drawn = 0;
  size_t textAgrees = 0;
  auto firstMapping = [&](char32_t cp) -> int {
    for (size_t i = 0; i < families.size(); ++i)
      if (has(listFonts[i], cp)) return static_cast<int>(i);
    return -1;
  };
  for (auto const& g : kIconGlyphs) {
    const int by = firstMapping(g.codePoint);
    const int byFallback = (by < 0 && g.fallback != 0) ? firstMapping(g.fallback) : -1;
    if (by >= 0) {
      drawnBy[static_cast<size_t>(by)] += std::format(L" {:04X}", static_cast<uint32_t>(g.codePoint));
      ++drawn;
    } else if (byFallback >= 0) {
      throughFallback += std::format(L" {:04X} as its fallback {:04X} ({}, by {})",
                                     static_cast<uint32_t>(g.codePoint),
                                     static_cast<uint32_t>(g.fallback), g.what,
                                     families[static_cast<size_t>(byFallback)]);
      ++drawn;
    } else {
      none += std::format(L" {:04X} ({})", static_cast<uint32_t>(g.codePoint), g.what);
    }
    // AND IconGlyphText ANSWERS THAT GLYPH: the one, or its fallback, this line says is drawn. Every draw
    // of a glyph with a fallback goes through IconGlyphText (verify-icon-font.ps1 fails any other), so
    // what it answers is what the app draws
    const wchar_t primary[2] = {static_cast<wchar_t>(g.codePoint), L'\0'};
    const winrt::hstring text = urnw::kit::IconGlyphText(primary);
    const char32_t want = (by < 0 && byFallback >= 0) ? g.fallback : g.codePoint;
    if (text.size() == 1 && static_cast<char32_t>(text[0]) == want) ++textAgrees;
  }
  std::wstring by;
  for (size_t i = 0; i < families.size(); ++i)
    if (!drawnBy[i].empty())
      by += std::format(L"; drawn by {}:{}", families[i], drawnBy[i]);
  // ONLY IN THE FIRST FAMILY: asked where every family of the list is installed, and said not to be
  // askable elsewhere
  std::wstring onlyFluent;
  std::wstring missing;
  for (size_t i = 0; i < families.size(); ++i)
    if (!listFonts[i]) missing += std::format(L"{}{}", missing.empty() ? L"" : L", ", families[i]);
  if (families.size() < 2) {
    onlyFluent = L"only in the first family: not askable, the list has one family";
  } else if (!missing.empty()) {
    onlyFluent = std::format(L"only in {}: not askable here, {} is not installed", families[0], missing);
  } else {
    std::wstring which;
    for (auto const& g : kIconGlyphs) {
      bool elsewhere = false;
      for (size_t i = 1; i < families.size() && !elsewhere; ++i) elsewhere = has(listFonts[i], g.codePoint);
      if (has(listFonts[0], g.codePoint) && !elsewhere)
        which += std::format(L" {:04X} ({})", static_cast<uint32_t>(g.codePoint), g.what);
    }
    // THIS MACHINE'S second family, which is not every machine's: Windows 10's Segoe MDL2 Assets maps
    // fewer glyphs than Windows 11's, and only a machine without the first family shows which
    onlyFluent = which.empty()
                     ? std::format(L"only in {} here: none (this machine's {} maps every one)",
                                   families[0], families[1])
                     : std::format(L"only in {} here, each drawn through its fallback or as an empty box "
                                   L"where only {} is installed:{}",
                                   families[0], families[1], which);
  }
  const bool agrees = textAgrees == std::size(kIconGlyphs);
  const bool ok = !families.empty() && none.empty() && agrees;
  return std::format(
      L"  icon glyphs      : {}  family \"{}\" ({}); {} of {} glyphs drawn{}{}{}{}{}; IconGlyphText "
      L"draws that glyph for {} of {}; {}   [query: DirectWrite's system fonts, for each code point "
      L"of kIconGlyphs the first family of kIconFontFamily that maps it, or that maps its fallback; "
      L"urnetwork/windows#12, ported]",
      ok ? L"PASS" : L"FAIL", kIconFontFamily, installed, drawn, std::size(kIconGlyphs),
      none.empty() ? L"" : L"; drawn by NO family of the list, an empty box:", none,
      throughFallback.empty() ? L"" : L"; drawn through a fallback:", throughFallback, by, textAgrees,
      std::size(kIconGlyphs), onlyFluent);
}

std::vector<std::wstring> CollectDiagnostics() {
  const std::filesystem::path exe = ExePath();
  const std::filesystem::path dir = exe.parent_path();
  const std::filesystem::path log = LogFilePath();

#if defined(_M_ARM64)
  constexpr wchar_t kArch[] = L"ARM64";
#elif defined(_M_X64)
  constexpr wchar_t kArch[] = L"x64";
#else
  constexpr wchar_t kArch[] = L"unknown";
#endif

  std::vector<std::wstring> lines;
  lines.push_back(L"URmessage startup diagnostics");
  lines.push_back(std::format(L"  build            : {} ({} {})", kArch,
                              Widen(__DATE__), Widen(__TIME__)));
  lines.push_back(std::format(L"  windows          : {}", OsVersion()));
  lines.push_back(std::format(L"  process          : pid {}", ::GetCurrentProcessId()));
  lines.push_back(std::format(L"  executable       : {}", exe.wstring()));
  lines.push_back(std::format(L"  command line     : {}", ::GetCommandLineW()));
  std::wstring logLine = log.empty() ? std::wstring(L"(none — debugger only)") : log.wstring();
  if (g_logOpened && !*g_logOpened)
    logLine += L"  ** COULD NOT BE OPENED — nothing is being written to it **";
  lines.push_back(std::format(L"  log file         : {}", logLine));
  lines.push_back(std::format(L"  storage root     : {}", StorageRoot().wstring()));
  lines.push_back(std::format(L"  single-inst key  : {}", EffectiveSingleInstanceKey()));
  lines.push_back(std::format(L"  built against    : Windows App SDK {}",
                              Widen(URM_WINDOWSAPPSDK_VERSION)));
  lines.push_back(std::format(L"  app runtime      : {}", AppRuntimeProbe()));
  lines.push_back(std::format(L"  bootstrap dll    : {}",
                              LoadedModule(kBootstrapDll, dir / kBootstrapDll)));
  // The file check only answers half of the resource question — see
  // ResourceProbe(), which answers the half that matters (does MRT actually
  // resolve a key) once there is an apartment to ask from.
  lines.push_back(std::format(L"  resources.pri    : {}", Presence(dir / L"resources.pri")));
  lines.push_back(std::format(L"  fonts            : {}",
                              Presence(dir / L"Assets" / L"Fonts" /
                                       L"pp_neue_bit_bold.ttf")));
  // THE ICON FONT ON THIS MACHINE (urnetwork/windows#12, ported): every glyph, and the family that
  // draws it here
  lines.push_back(IconGlyphsLine());
  {
    const urmsg::demo::DemoOptions o = urmsg::demo::ParseDemoOptions();
    constexpr const wchar_t* kScreens[] = {L"none", L"chats", L"thread", L"inspect",
                                           L"network", L"settings", L"developer"};
    lines.push_back(std::format(
        L"  demo switches    : enabled={} screen={} autoplay={} advanced={} watermark={}",
        o.enabled ? L"yes" : L"no", kScreens[static_cast<size_t>(o.screen)],
        o.autoplay ? L"yes" : L"no", o.advanced ? L"yes" : L"no",
        o.watermark ? L"on" : L"off"));
  }
  {
    const nlohmann::json prefs = urnw::LoadAppPrefs();
    const bool present = prefs.contains("advanced_mode") && prefs["advanced_mode"].is_boolean();
    lines.push_back(std::format(
        L"  advanced mode    : {}  (pref advanced_mode: {}; session override: {})",
        urmsg::AdvancedModeEnabled() ? L"ON " : L"off",
        present ? (prefs["advanced_mode"].get<bool>() ? L"true" : L"false") : L"absent",
        urmsg::demo::ParseDemoOptions().advanced ? L"yes" : L"no"));
  }
  for (auto& line : DemoWorldAssertions()) lines.push_back(std::move(line));
  // The Developer surface's session switches (design §6.6). Immediately after
  // the world assertions, per the d7 audit's A2 override — there is no
  // AdvancedModeDiagnostics() in this tree; the Advanced Mode content above is
  // an inline report line, not an assertion loop.
  for (auto& line : urmsg::demo::DeveloperSwitchDiagnostics())
    lines.push_back(std::move(line));
  // The Developer surface's world dump (design §6.6). Immediately after the
  // session-switch assertions, per the d7 audit's A2/A6 sequencing. The dump
  // module is pure C++ with PrecompiledHeader=NotUsing for the same reason
  // DemoWorld.cpp is: this runs before winrt::init_apartment.
  for (auto& line : urmsg::views::WorldDumpDiagnostics())
    lines.push_back(std::move(line));
  // THE HONESTY COPY THAT DIFFERS BETWEEN THE TWO WORLDS, asserted in BOTH modes from
  // whichever mode this launch happens to be in. It sits here, beside the dump, because
  // the dump's own framing line is one of the strings it covers.
  //
  // The reason this can gate the live wording at all from a default launch is that every
  // function it calls takes the mode as a PARAMETER: nothing in RunMode.cpp reads
  // ActiveRunMode(), so both arms are evaluable at any time. A gate that asked "what does
  // the app say right now" would have gated exactly half the copy and would have gated
  // the half that was already correct.
  for (auto& line : urmsg::RunModeCopyDiagnostics()) lines.push_back(std::move(line));
  // The reply and reaction affordances: their names in both arms, the picker against the ABI's
  // rule, the chip words, and the live mapping over an adversarial group. Beside the copy gate
  // because the action names are copy of the same kind - true by which arm is chosen - and
  // because "a reaction that has not been sealed is not sent" is the honesty rule again, one
  // affordance further along.
  for (auto& line : ReplyReactDiagnostics()) lines.push_back(std::move(line));
  // The roster (item 242 R3): the role words, the control set per own role, the control names in
  // both arms, the outcome notes, and the live mapping over a roster built here. Beside the
  // reply/react lines because it is the same shape of gate one surface further along.
  for (auto& line : RosterDiagnostics()) lines.push_back(std::move(line));
  // OBSERVER read-only (item 242 R4): the composer's three states, the hidden row over the whole
  // fabricated world with both sets printed, and the two observer sentences held apart. Beside the
  // roster because it is the same surface one rule further along - R3 put the roles on the screen,
  // R4 is the first piece that changes what the APPLICATION RECORD path does with one.
  for (auto& line : ObserverDiagnostics()) lines.push_back(std::move(line));
  // An invitation refused over a group this device holds, and the leave that answers it (Task 39):
  // which refusal it is, where the group id comes from, the leave's own refusal, and the two
  // sentences. Pure, like the roster's lines above.
  for (auto& line : HeldGroupDiagnostics()) lines.push_back(std::move(line));
  // What the app says for each refusal of a join or an add, and for a device that cannot make a
  // join code (Task 58): the rule per row, R13 on its own line, the table's words, and the dialog's
  // own copy. Pure, like the held-group lines above.
  for (auto& line : RefusalCopyDiagnostics()) lines.push_back(std::move(line));
  {
    using namespace urmsg::demo;
    World const& w = GetWorld();
    std::vector<Seed> seeds;
    for (auto const& c : w.conversations) {
      seeds.push_back(c.identityKey);
      for (auto const& m : c.members) seeds.push_back(m.identityKey);
    }
    size_t stable = 0, symmetric = 0, inRange = 0;
    for (auto const& s : seeds) {
      const auto a = urmsg::MakeIdenticonPattern(s);
      const auto b = urmsg::MakeIdenticonPattern(s);
      if (a.cells == b.cells && a.colorIndex == b.colorIndex) ++stable;
      bool mirrored = true;
      size_t on = 0;
      for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col) {
          const bool v = a.cells[static_cast<size_t>(row * 5 + col)];
          if (v) ++on;
          if (v != a.cells[static_cast<size_t>(row * 5 + (4 - col))]) mirrored = false;
        }
      if (mirrored) ++symmetric;
      if (1 <= on && on <= 23) ++inRange;
    }
    const size_t n = seeds.size();

    // P4 (fix round 1): m-mira and m-tobias are admins of BOTH groups, so
    // two of the 30 seed slots are byte-identical repeats of the same
    // identity - those two SHOULD produce the same identicon, and are not a
    // collision. "distinguishable" only means something once repeats of one
    // identity are folded down to one, so dedupe by identity key first.
    std::vector<Seed> uniqueSeeds;
    for (auto const& s : seeds) {
      bool seen = false;
      for (auto const& u : uniqueSeeds)
        if (u == s) { seen = true; break; }
      if (!seen) uniqueSeeds.push_back(s);
    }
    const size_t uniqueIdentities = uniqueSeeds.size();

    // This is a closed set of hand-authored demo seeds, not an open
    // population, so an exact bound is available and is the right one: if
    // two DIFFERENT people ever end up with the same avatar, the fix is to
    // change a seed, not to tolerate a rate. colorIndex is included in the
    // comparison because IdenticonPattern's visible result is cells AND
    // colour together - two identities that share a cell layout but differ
    // only in colour are still distinguishable, and the reverse.
    size_t distinctIdentities = 0;
    for (size_t i = 0; i < uniqueSeeds.size(); ++i) {
      const auto pi = urmsg::MakeIdenticonPattern(uniqueSeeds[i]);
      bool seen = false;
      for (size_t j = 0; j < i; ++j) {
        const auto pj = urmsg::MakeIdenticonPattern(uniqueSeeds[j]);
        if (pj.cells == pi.cells && pj.colorIndex == pi.colorIndex) { seen = true; break; }
      }
      if (!seen) ++distinctIdentities;
    }

    lines.push_back(std::format(
        L"  identicons       : {} seeds from the demo world, {} unique identities",
        n, uniqueIdentities));
    lines.push_back(std::format(L"    P1  deterministic       {}  {}/{} seeds identical on a second call",
                                n && stable == n ? L"PASS" : L"FAIL", stable, n));
    lines.push_back(std::format(L"    P2  mirrored            {}  {}/{} patterns symmetric",
                                n && symmetric == n ? L"PASS" : L"FAIL", symmetric, n));
    lines.push_back(std::format(L"    P3  density in range    {}  {}/{} have 1..23 cells set",
                                n && inRange == n ? L"PASS" : L"FAIL", inRange, n));
    lines.push_back(std::format(
        L"    P4  no collisions       {}  {} distinct of {} unique identities",
        uniqueIdentities && distinctIdentities == uniqueIdentities ? L"PASS" : L"FAIL",
        distinctIdentities, uniqueIdentities));

    // P5: the corner-radius rule, asserted as ARITHMETIC because MakeIdenticon
    // constructs a Border and is therefore unreachable from this pre-apartment
    // harness — what IS reachable is the pure function the render spends
    // (IdenticonCornerRadius, Identicon.h), the same pattern as the
    // entrance/typing timeline tables. The pairs below pin the rule, not a
    // count: 20px keeps the 8 the old fixed literal gave every size, and every
    // other size rounds proportionally, so a regression to a fixed literal or
    // a changed proportion fails here rather than in a screenshot review.
    const bool radiusOk = urmsg::IdenticonCornerRadius(20.0) == 8.0 &&
                          urmsg::IdenticonCornerRadius(28.0) == 11.2 &&
                          urmsg::IdenticonCornerRadius(40.0) == 16.0;
    lines.push_back(std::format(L"    P5  proportional radius {}  20px->8, 28px->11.2, 40px->16",
                                radiusOk ? L"PASS" : L"FAIL"));
  }
  lines.push_back(DemoLayoutCheck());
  lines.push_back(DemoDeepLinkCheck());
  lines.push_back(DemoAutoplayCheck());
  lines.push_back(DemoScrollPinCheck());
  // The status strip's own pure rules. Unguarded for the same reason the two
  // lines above are: it builds no world and names no hostname, so a plain
  // launch's log gains nothing fabricated by carrying it.
  lines.push_back(StatusStripFieldsAssertion());
  // S3: the relay drawer's rows and the strip fit their own width. The same terms: a typed
  // client_id and platform url, and stand-in widths, so nothing fabricated reaches the log.
  lines.push_back(RelayRowFitAssertion());
  lines.push_back(StatusStripFitAssertion());
  // The demo surfaces' own invariants. One collector per surface, and every one
  // of them must be pure C++: this function runs before winrt::init_apartment
  // (main.cpp:168 vs :180), so a WinRT object built from here would die on the
  // line that constructs it, inside the code whose job is to explain deaths.
  // Unconditional on purpose -- these assert a pure function of seeded data, not
  // app state, so they are as true on a plain launch as under --demo.
  for (auto const& line : urmsg::views::CollectConversationListDiagnostics())
    lines.push_back(line);
  // THE CONVERSATION LIST'S OWN RULES (the conversation-list design): pure, on worlds built in place.
  for (auto const& line : urmsg::live::CollectConversationRulesDiagnostics())
    lines.push_back(line);

  // ---- thread layout rules (Demo/ThreadLayout.h) ---------------------------
  {
    namespace demo = urmsg::demo;
    namespace views = urmsg::views;
    auto const& world = demo::GetWorld();

    // 1. Width. The probes bracket the cap crossover (640 / 0.68 = 941.18), so
    //    neither `return 640;` nor `return 0.68 * w;` survives, and 0 proves the
    //    unmeasured-column guard.
    struct WidthProbe { double column; double expect; };
    static constexpr WidthProbe kProbes[] = {
        {0.0, 640.0}, {400.0, 272.0}, {941.0, 639.88}, {942.0, 640.0}, {1560.0, 640.0}};
    int widthOk = 0;
    for (auto const& p : kProbes)
      if (std::fabs(views::BubbleMaxWidthDip(p.column) - p.expect) < 0.01) ++widthOk;
    lines.push_back(std::format(
        L"  thread T1 width  : {} - {}/5 probes at 0/400/941/942/1560 dip",
        widthOk == 5 ? L"PASS" : L"FAIL", widthOk));

    // 2. Sender header. The view rule must agree with DemoWorld's own
    //    run-continuation rule on EVERY message row: senderName is non-empty
    //    exactly when the bubble draws a name and an identicon.
    //
    //    THE ROW ABOVE IS NOT ALWAYS c.rows[i - 1] SINCE ITEM 242 R4. A collapsed
    //    observer row draws no bubble, so the row under it starts a new run — and
    //    this clause asked ShowsSenderHeader with the RAW neighbour, which made it
    //    agree with a fixture that thought the same row was a continuation. It
    //    passed while the bubble on screen carried an IDENTICON AND NO NAME: the
    //    plan supplied the first and the data the second, and this was the one
    //    check whose whole job is that they agree. It was the screenshot that
    //    caught it. The substitution is recomputed here off the row's own word,
    //    the way T4's is.
    auto collapses = [](demo::MessageRow const& x) {
      return x.kind == demo::RowKind::Message && x.senderRoleAtSend == demo::kRoleObserver;
    };
    int msgRows = 0, headers = 0, disagree = 0;
    for (auto const& c : world.conversations) {
      const bool group = (c.kind == demo::ConversationKind::Group);
      for (std::size_t i = 0; i < c.rows.size(); ++i) {
        auto const& row = c.rows[i];
        if (row.kind != demo::RowKind::Message) continue;
        ++msgRows;
        demo::MessageRow const* prev =
            (i > 0 && !collapses(c.rows[i - 1])) ? &c.rows[i - 1] : nullptr;
        const bool shows = views::ShowsSenderHeader(prev, row, group);
        if (shows) ++headers;
        if (shows != !row.senderName.empty()) ++disagree;
      }
    }
    lines.push_back(std::format(
        L"  thread T1 header : {} - {} message rows, {} draw a sender header, "
        L"{} disagree with DemoWorld::senderName",
        (disagree == 0 && headers > 0) ? L"PASS" : L"FAIL", msgRows, headers, disagree));

    // 3. Delivery glyph. runStarts is counted from the run TRANSITION, i.e.
    //    independently of CarriesDeliveryGlyph, so `return true` (carriers ==
    //    outgoing) and a rule that forgot the Failed exception both fail.
    int outgoing = 0, runStarts = 0, carriers = 0, failed = 0, failedMidRun = 0,
        failedCarrying = 0;
    for (auto const& c : world.conversations) {
      bool inRun = false;
      for (std::size_t i = 0; i < c.rows.size(); ++i) {
        auto const& row = c.rows[i];
        const bool isOut = (row.kind == demo::RowKind::Message && row.outgoing);
        if (isOut && !inRun) ++runStarts;
        inRun = isOut;
        if (!isOut) continue;
        ++outgoing;
        demo::MessageRow const* next = (i + 1 < c.rows.size()) ? &c.rows[i + 1] : nullptr;
        const bool lastOfRun =
            !(next && next->kind == demo::RowKind::Message && next->outgoing);
        const bool isFailed = (row.state == demo::DeliveryState::Failed);
        if (isFailed) { ++failed; if (!lastOfRun) ++failedMidRun; }
        if (views::CarriesDeliveryGlyph(row, next)) {
          ++carriers;
          if (isFailed) ++failedCarrying;
        }
      }
    }
    // 3b. Synthetic case. DemoWorld's ONLY multi-row outgoing run pairs a
    //     Failed row with the last-of-run exception (DemoWorld.cpp:277-279),
    //     so no real row is both non-Failed and non-last-of-run: the real-
    //     world counts above pass identically whether or not the ordinary
    //     "last of run" rule is even implemented. Constructed locally rather
    //     than by editing DemoWorld, whose exact bytes F3 fingerprints: an
    //     ordinary (non-Failed) three-row outgoing run, checked at all three
    //     positions -- the middle row is the case the world cannot provide.
    std::vector<demo::MessageRow> synthGlyphRows(3);
    for (auto& r : synthGlyphRows) {
      r.kind = demo::RowKind::Message;
      r.outgoing = true;
      r.state = demo::DeliveryState::Sent;  // deliberately NOT Failed
    }
    int glyphSynthChecked = 0, glyphSynthOk = 0;
    for (std::size_t i = 0; i < synthGlyphRows.size(); ++i) {
      demo::MessageRow const* next =
          (i + 1 < synthGlyphRows.size()) ? &synthGlyphRows[i + 1] : nullptr;
      const bool expectGlyph = (i + 1 == synthGlyphRows.size());  // last row only
      ++glyphSynthChecked;
      if (views::CarriesDeliveryGlyph(synthGlyphRows[i], next) == expectGlyph) ++glyphSynthOk;
    }

    const bool glyphOk = (carriers == runStarts + failedMidRun) && (failed > 0) &&
                         (failedCarrying == failed) && (outgoing > runStarts) &&
                         (glyphSynthChecked == 3) && (glyphSynthOk == glyphSynthChecked);
    lines.push_back(std::format(
        L"  thread T1 glyph  : {} - {} outgoing rows in {} runs, {} carry a glyph, "
        L"{}/{} failed carry one ({} mid-run); synthetic non-failed 3-row run "
        L"{}/{} correct (middle false, last true)",
        glyphOk ? L"PASS" : L"FAIL", outgoing, runStarts, carriers, failedCarrying,
        failed, failedMidRun, glyphSynthOk, glyphSynthChecked));

    // 4. Day separators. Never unlabelled, never doubled, never last.
    views::DaySeparatorAudit total;
    int convsWithSeparators = 0;
    for (auto const& c : world.conversations) {
      const auto a = views::AuditDaySeparators(c.rows);
      if (a.separators > 0) ++convsWithSeparators;
      total.separators += a.separators;
      total.unlabelled += a.unlabelled;
      total.adjacent += a.adjacent;
      total.trailing += a.trailing;
    }
    // 4b. Synthetic case. DemoWorld structurally never places two separators
    //     back to back or a separator last in a conversation, so the
    //     adjacent/trailing DETECTION logic above is never exercised by real
    //     data: a stub that hardcodes adjacent=0, trailing=0 and ignores its
    //     input would pass exactly as the real AuditDaySeparators does.
    //     Constructed locally: two adjacent DaySeparator rows, then a
    //     Message row, then a trailing DaySeparator.
    std::vector<demo::MessageRow> synthDayRows(4);
    synthDayRows[0].kind = demo::RowKind::DaySeparator;
    synthDayRows[0].body = L"Synthetic Day A";
    synthDayRows[1].kind = demo::RowKind::DaySeparator;
    synthDayRows[1].body = L"Synthetic Day B";
    synthDayRows[2].kind = demo::RowKind::Message;
    synthDayRows[3].kind = demo::RowKind::DaySeparator;
    synthDayRows[3].body = L"Synthetic Day C";
    const auto daySynth = views::AuditDaySeparators(synthDayRows);
    const bool daySynthOk = daySynth.adjacent >= 1 && daySynth.trailing >= 1;

    const bool daysOk = total.separators >= 2 && total.unlabelled == 0 &&
                        total.adjacent == 0 && total.trailing == 0 && daySynthOk;
    lines.push_back(std::format(
        L"  thread T1 days   : {} - {} separators across {} conversations; "
        L"{} unlabelled, {} adjacent, {} trailing; synthetic case {} adjacent, "
        L"{} trailing",
        daysOk ? L"PASS" : L"FAIL", total.separators, convsWithSeparators,
        total.unlabelled, total.adjacent, total.trailing, daySynth.adjacent,
        daySynth.trailing));

    // 5. Delivery vocabulary. Six states -> six NON-EMPTY, DISTINCT words.
    //
    //    THE GLYPH HALF OF THIS CHECK IS GONE, with DeliveryGlyph() itself
    //    (T5 fix round 1). It asserted six DISTINCT glyphs, and the rendered
    //    table deliberately violates that: BadgeFor() gives Sent and Delivered
    //    the same E930 and tells them apart by COUNT, which is Spec C §5.3's
    //    own reading. A gate asserting a property the render contradicts is
    //    worse than no gate. What draws is asserted by `T5 delivery badges`.
    //
    //    The WORD is still live and still load-bearing: BubbleAutomationName
    //    puts it in every outgoing bubble's name, and check 6 below searches
    //    each name for it — so two states sharing a word would make a screen
    //    reader unable to tell them apart, which is what this line prevents.
    static constexpr demo::DeliveryState kStates[] = {
        demo::DeliveryState::Pending, demo::DeliveryState::Sent,
        demo::DeliveryState::Delivered, demo::DeliveryState::Read,
        demo::DeliveryState::Failed, demo::DeliveryState::Expired};
    std::vector<std::wstring> words;
    for (auto s : kStates) words.push_back(views::DeliveryWord(s));
    int nonEmpty = 0, distinctWords = 0;
    for (std::size_t i = 0; i < words.size(); ++i) {
      if (!words[i].empty()) ++nonEmpty;
      bool dupW = false;
      for (std::size_t j = 0; j < i; ++j)
        if (words[j] == words[i]) dupW = true;
      if (!dupW) ++distinctWords;
    }
    lines.push_back(std::format(
        L"  thread T2 words  : {} - 6 states, {} non-empty, {} distinct words",
        (nonEmpty == 6 && distinctWords == 6) ? L"PASS" : L"FAIL", nonEmpty,
        distinctWords));

    // 6. Bubble names. Every bubble is named; every name carries its time; every
    //    outgoing name carries its delivery WORD (so state is never colour-only);
    //    the failed one carries its reason; and in a group every incoming name
    //    carries a sender EVEN ON A CONTINUATION, where the bubble draws none.
    int named = 0, empty = 0, timed = 0, missingTime = 0, bodied = 0,
        missingBody = 0, outNamed = 0, missingWord = 0;
    int groupIncoming = 0, senderKnown = 0, missingSender = 0, failedRows = 0,
        withReason = 0;
    for (auto const& c : world.conversations) {
      const bool group = (c.kind == demo::ConversationKind::Group);
      for (auto const& row : c.rows) {
        if (row.kind != demo::RowKind::Message) continue;
        const std::wstring n = views::BubbleAutomationName(row, group);
        ++named;
        if (n.empty()) ++empty;
        // COUNT THE DENOMINATOR, exactly as the body condition below does and
        // for the same reason: the check is guarded by "the row has a time", so
        // a fixture that emptied timeLabel would skip every row, leave
        // missingTime at 0 and print PASS while no announcement carried a time.
        // timed is required to EQUAL named, and is printed as {timed}/{named}.
        if (!row.timeLabel.empty()) {
          ++timed;
          if (n.find(row.timeLabel) == std::wstring::npos) ++missingTime;
        }
        // The BODY, which nothing above looked at. Delete the one line that
        // appends it in BubbleAutomationName and every announcement in the app
        // loses the message itself while all nine other conditions here still
        // pass - a gate that cannot fail on the defect it exists to catch.
        // Demonstrated by making exactly that deletion and watching this line
        // report FAIL with 60 missing bodies, then reverting.
        //
        // COUNT THE DENOMINATOR TOO. The per-row check is guarded by "the row
        // has a body", so a fixture that ever emptied the bodies would skip
        // EVERY row, leave missingBody at 0 and print PASS while announcing
        // nothing - the numerator alone cannot tell "all covered" from "none
        // looked at". bodied is therefore counted, printed as {bodied}/{named}
        // and required to EQUAL named, so an emptied body is a FAIL with the
        // shortfall visible on the line. Demonstrated against a mutant that
        // empties one body, then reverted.
        if (!row.body.empty()) {
          ++bodied;
          if (n.find(row.body) == std::wstring::npos) ++missingBody;
        }
        if (row.outgoing) {
          ++outNamed;
          if (n.find(views::DeliveryWord(row.state)) == std::wstring::npos) ++missingWord;
          if (row.state == demo::DeliveryState::Failed) {
            ++failedRows;
            if (!row.failureReason.empty() &&
                n.find(row.failureReason) != std::wstring::npos) ++withReason;
          }
        } else if (group) {
          ++groupIncoming;
          // Same guarded-numerator hazard, same fix: senderKnown is the
          // denominator and must equal groupIncoming, so an emptied
          // inspect.senderDisplayName is a FAIL rather than a skipped row.
          if (!row.inspect.senderDisplayName.empty()) {
            ++senderKnown;
            if (n.find(row.inspect.senderDisplayName) == std::wstring::npos) ++missingSender;
          }
        }
      }
    }
    const bool namesOk = named > 0 && empty == 0 && timed == named &&
                         missingTime == 0 && bodied == named && missingBody == 0 &&
                         outNamed > 0 && missingWord == 0 && groupIncoming > 0 &&
                         senderKnown == groupIncoming && missingSender == 0 &&
                         failedRows > 0 && withReason == failedRows;
    lines.push_back(std::format(
        L"  thread T2 names  : {} - {} bubbles named ({} empty, {}/{} carry a time, "
        L"{} missing time, {}/{} carry a body, {} missing body); {} outgoing, {} "
        L"missing a delivery word; {} group incoming, {}/{} carry a sender, {} "
        L"missing a sender; {}/{} failed carry a reason",
        namesOk ? L"PASS" : L"FAIL", named, empty, timed, named, missingTime,
        bodied, named, missingBody, outNamed, missingWord,
        groupIncoming, senderKnown, groupIncoming, missingSender,
        withReason, failedRows));
  }

  // ---- T4: the thread layout planner -------------------------------------
  // PlanThreadRows is pure C++, which is the only reason any of this can run
  // here at all: CollectDiagnostics is called from wWinMain BEFORE
  // winrt::init_apartment(), so a planner that touched a winrt type could not be
  // asserted about, only looked at.
  {
    using Shape = urmsg::views::ThreadRowShape;

    // THE EXPECTED SHAPE, recomputed here from the world alone.
    //
    // This is the assertion. Counting how many rows wear each shape is NOT: a
    // planner that mapped RowKind::Message onto DaySeparator keeps every total
    // balanced (66 planned of 66 rows, 2 plain + 1 permanent of 3 system rows,
    // 1 record with 0 empty text and 0 permanent-flag mismatch) and would have
    // read PASS on every line while DaySeparatorLabel() returned an empty label
    // for all 60 message rows and the thread drew NOTHING. Inverting the
    // incoming/outgoing ternary is equally invisible to a count. So the shape of
    // EVERY row is compared against a locally recomputed expectation, across all
    // five shapes, and every shape is required to be worn by a real row.
    auto expectedShape = [](urmsg::demo::MessageRow const& r) {
      switch (r.kind) {
        case urmsg::demo::RowKind::DaySeparator:
          return Shape::DaySeparator;
        case urmsg::demo::RowKind::System:
          return r.permanentRecord ? Shape::SystemPermanentRecord : Shape::SystemLine;
        case urmsg::demo::RowKind::Message:
          // Item 242 R4, and recomputed from the ROW rather than asked of the
          // planner's own predicate: a mutant that made IsHiddenObserverRow
          // answer true for everything would otherwise be agreed with rather
          // than caught. The word is compared here, in this file.
          if (r.senderRoleAtSend == urmsg::demo::kRoleObserver)
            return Shape::HiddenObserverRecord;
          return r.outgoing ? Shape::OutgoingBubble : Shape::IncomingBubble;
      }
      return Shape::SystemLine;
    };
    auto shapeSlot = [](Shape sh) -> std::size_t {
      switch (sh) {
        case Shape::IncomingBubble: return 0;
        case Shape::OutgoingBubble: return 1;
        case Shape::DaySeparator: return 2;
        case Shape::SystemLine: return 3;
        case Shape::SystemPermanentRecord: return 4;
        case Shape::HiddenObserverRecord: return 5;
      }
      return 0;
    };

    std::size_t convs = 0, rows = 0, planned = 0, outOfOrder = 0, wrongShape = 0;
    std::size_t seen[6] = {0, 0, 0, 0, 0, 0};
    std::size_t system = 0, plain = 0, permanent = 0, emptyText = 0, mismatched = 0;
    std::size_t msgRows = 0, headerByRule = 0, headerByPlan = 0, headerDisagree = 0;
    // THE THIRD OPINION, and the one that was not being asked. showSenderHeader is what the PLAN
    // says; MessageRow::senderName is what the DATA carries; and a bubble draws the IDENTICON from
    // the first and the NAME from the second. Demo/DemoWorld.cpp keeps its own run tracking
    // (previousSender), which is a second statement of "starts a run" — and when the two
    // disagreed across a collapsed observer row the bubble rendered an identicon with NO NAME,
    // every gate on this surface passed, and only a screenshot showed it. T2 could not see it
    // (it reads inspect.senderDisplayName, populated on every row) and the clause above could not
    // (it compares the plan against the RULE, never against the bytes). They must be equal on
    // every row: showSenderHeader <=> the row carries a name.
    std::size_t nameDisagree = 0;
    std::wstring firstNameDisagree = L"(none)";

    for (auto const& c : urmsg::demo::GetWorld().conversations) {
      ++convs;
      rows += c.rows.size();
      const bool group = (c.kind == urmsg::demo::ConversationKind::Group);
      for (auto const& r : c.rows)
        if (r.kind == urmsg::demo::RowKind::System) ++system;

      std::size_t at = 0;
      for (auto const& p : urmsg::views::PlanThreadRows(c)) {
        ++planned;
        // "One entry per row, IN ORDER, always" is part of the contract, so it
        // is checked rather than trusted; a plan that reordered or duplicated
        // rows would otherwise still count 66 of 66.
        if (p.rowIndex != at++ || c.rows.size() <= p.rowIndex) {
          ++outOfOrder;
          continue;
        }
        auto const& r = c.rows[p.rowIndex];

        if (p.shape != expectedShape(r)) ++wrongShape;
        ++seen[shapeSlot(p.shape)];

        if (p.shape == Shape::SystemLine) ++plain;
        if (p.shape == Shape::SystemPermanentRecord) {
          ++permanent;
          if (r.systemText.empty()) ++emptyText;
        }
        // The permanent shape appears on exactly the rows the world marks
        // permanentRecord, and on no others - Spec C 7.4 stated as a property.
        if ((p.shape == Shape::SystemPermanentRecord) !=
            (r.kind == urmsg::demo::RowKind::System && r.permanentRecord))
          ++mismatched;

        // The plan's sender header must BE the app's one sender-run rule, not a
        // second opinion about it. Before that was delegated the rule said 20
        // and the plan said 15; this gate is why that cannot come back without
        // a FAIL on the line.
        if (r.kind == urmsg::demo::RowKind::Message) {
          ++msgRows;
          // A COLLAPSED OBSERVER ROW IS NOT THE ROW ABOVE (item 242 R4): it draws
          // no bubble, so the row under it starts a new run and gets its own name
          // and identicon. Recomputed here off the row's own word, exactly as
          // expectedShape is, so the planner is held to the rule rather than
          // asked for it. The fixture carries the case one row under the
          // collapsed line, or this substitution would be unobservable.
          auto collapses = [](urmsg::demo::MessageRow const& x) {
            return x.kind == urmsg::demo::RowKind::Message &&
                   x.senderRoleAtSend == urmsg::demo::kRoleObserver;
          };
          urmsg::demo::MessageRow const* prev =
              (p.rowIndex > 0 && !collapses(c.rows[p.rowIndex - 1])) ? &c.rows[p.rowIndex - 1]
                                                                     : nullptr;
          // The collapsed row itself draws no header of its own and the plan has
          // no opinion about it, the way it has none about a day separator — so
          // it is not compared, rather than compared against a rule that does
          // not apply to it.
          if (collapses(r)) continue;
          const bool byRule = urmsg::views::ShowsSenderHeader(prev, r, group);
          if (byRule) ++headerByRule;
          if (p.showSenderHeader) ++headerByPlan;
          if (byRule != p.showSenderHeader) ++headerDisagree;
          if (p.showSenderHeader != !r.senderName.empty()) {
            ++nameDisagree;
            if (firstNameDisagree == L"(none)")
              firstNameDisagree = std::format(L"{} (plan {}, name \"{}\")", r.id,
                                              p.showSenderHeader ? L"header" : L"no header",
                                              r.senderName);
          }
        }
      }
    }
    // Every one of the SIX shapes must be worn by at least one row, or
    // "0 wrong shapes" would be a claim about branches no row ever took. The
    // sixth joined the set with item 242 R4 and the fixture grew a row to wear
    // it in the same commit, which is exactly what this clause is for.
    std::size_t shapesUnused = 0;
    for (std::size_t k = 0; k < 6; ++k)
      if (seen[k] == 0) ++shapesUnused;

    lines.push_back(std::format(
        L"  T4 plan covers rows  : {} — planned {} of {} rows over {} "
        L"conversations, {} out of order",
        (planned == rows && 0 < rows && outOfOrder == 0) ? L"PASS" : L"FAIL",
        planned, rows, convs, outOfOrder));
    lines.push_back(std::format(
        L"  T4 row shapes        : {} — {} wrong of {} planned; in {} out {} "
        L"day {} sys {} perm {} hidden {}; {} of 6 shapes unused",
        (wrongShape == 0 && shapesUnused == 0 && 0 < planned) ? L"PASS" : L"FAIL",
        wrongShape, planned, seen[0], seen[1], seen[2], seen[3], seen[4], seen[5],
        shapesUnused));
    lines.push_back(std::format(
        L"  T4 system rows       : {} — {} system rows -> {} plain + {} permanent",
        (plain + permanent == system && 1 <= plain && 1 <= permanent) ? L"PASS" : L"FAIL",
        system, plain, permanent));
    lines.push_back(std::format(
        L"  T4 permanent record  : {} — {} record(s), {} with empty systemText, "
        L"{} shape/flag mismatch over {} planned rows",
        (1 <= permanent && emptyText == 0 && mismatched == 0) ? L"PASS" : L"FAIL",
        permanent, emptyText, mismatched, planned));
    lines.push_back(std::format(
        L"  T4 sender headers    : {} — rule {} vs plan {} over {} message rows, "
        L"{} disagree; plan vs the NAME the row carries: {} disagree (first: {})",
        (headerDisagree == 0 && nameDisagree == 0 && 1 <= headerByRule && 1 <= headerByPlan)
            ? L"PASS"
            : L"FAIL",
        headerByRule, headerByPlan, msgRows, headerDisagree, nameDisagree, firstNameDisagree));
  }


  // ---- T5: the delivery cluster and bubble selection ----------------------
  // Everything asserted here is pure C++ (Views/ThreadLayout.h, Demo/ThreadLayout.h),
  // which is why it can run from wWinMain before winrt::init_apartment().
  //
  // WHAT THESE THREE LINES CANNOT SEE, said plainly: none of them looks at a
  // XAML tree, so no gate here can catch a RENDER that reads the wrong field.
  // What they do catch is the rule and the table underneath it going wrong, and
  // line 1 in particular is built so that the rule collapsing into
  // ThreadRowPlan::endsOutgoingRun — the exact mutation that would delete the
  // 12:09 cluster from the shipped thread — is a FAIL rather than a silence.
  {
    using DState = urmsg::demo::DeliveryState;

    // 1. WHICH ROWS CARRY A CLUSTER — per row, and against the field the
    //    renderer must NOT use.
    //
    //    CarriesDeliveryGlyph() and ThreadRowPlan::endsOutgoingRun are NOT the
    //    same function: the first additionally fires on ANY Failed row wherever
    //    it sits. This line asserts that the difference is REAL on the shipped
    //    world (`divergent >= 1`) and that every divergence is exactly that case
    //    (rule says carry, plan field says no, the row is Failed and is not last
    //    of its run).
    //
    //    MUTATION CAUGHT: dropping the Failed exception from CarriesDeliveryGlyph,
    //    or endsOutgoingRun growing one — either collapses `divergent` to 0 and
    //    FAILS. THE ROW THAT EXERCISES IT: DemoWorld.cpp:277, c0-r22, outgoing
    //    Failed at 12:09, followed at :279 by an outgoing Pending row. If the
    //    fixture ever loses that row the gate FAILS rather than going quietly
    //    vacuous, which is the whole reason `divergent >= 1` is required rather
    //    than merely reported.
    std::size_t outRows = 0, byRule = 0, byPlanField = 0, divergent = 0,
                divergentFailedMidRun = 0, convsWithCluster = 0, convsWithOutgoing = 0;
    for (auto const& c : urmsg::demo::GetWorld().conversations) {
      bool anyOut = false, anyCluster = false;
      for (auto const& p : urmsg::views::PlanThreadRows(c)) {
        auto const& r = c.rows[p.rowIndex];
        urmsg::demo::MessageRow const* next =
            (p.rowIndex + 1 < c.rows.size()) ? &c.rows[p.rowIndex + 1] : nullptr;
        const bool rule = urmsg::views::CarriesDeliveryGlyph(r, next);
        if (r.kind == urmsg::demo::RowKind::Message && r.outgoing) {
          ++outRows;
          anyOut = true;
        }
        if (rule) {
          ++byRule;
          anyCluster = true;
        }
        if (p.endsOutgoingRun) ++byPlanField;
        if (rule != p.endsOutgoingRun) {
          ++divergent;
          const bool lastOfRun = !(next && next->kind == urmsg::demo::RowKind::Message &&
                                   next->outgoing);
          if (rule && !p.endsOutgoingRun && r.state == DState::Failed && !lastOfRun)
            ++divergentFailedMidRun;
        }
      }
      if (anyOut) ++convsWithOutgoing;
      if (anyCluster) ++convsWithCluster;
    }
    // 1b. THE CASE THE SHIPPED WORLD CANNOT PROVIDE, built locally.
    //
    //     The counts above are blind to half the rule, and the arithmetic says
    //     why: 24 outgoing rows in 23 runs means exactly ONE multi-row outgoing
    //     run exists, and its first row is the Failed one. So the world contains
    //     NO row that is outgoing, not-last-of-run and not-Failed — and deleting
    //     the last-of-run clause from CarriesDeliveryGlyph (making it `return
    //     true` for every outgoing row) leaves byRule 24, divergent 1 and
    //     divergentFailedMidRun 1: every number above identical, PASS unchanged.
    //     This is the same fixture-blindness `thread T1 glyph` states at check 3
    //     and fixes with a synthetic run; the pattern is reused here rather than
    //     reinvented, and NEVER by editing Demo/DemoWorld.cpp, whose exact bytes
    //     I10 fingerprints.
    //
    //     A whole Conversation is built rather than a bare row vector, because
    //     this gate's subject is the rule AGAINST the plan — so PlanThreadRows
    //     has to run on the same fixture and endsOutgoingRun has to be real.
    //
    //     Four rows, and every position of the rule is exercised once:
    //       r0 outgoing Sent,   next outgoing -> NOT last of run, not Failed -> no
    //       r1 outgoing Failed, next outgoing -> NOT last of run, Failed     -> YES
    //       r2 outgoing Sent,   next incoming -> last of run                 -> YES
    //       r3 incoming Sent                                                 -> no
    //     r0 is the row the world does not have. r1 is the only place rule and
    //     endsOutgoingRun may disagree, and r2 is the only place they must agree
    //     while both being true.
    urmsg::demo::Conversation synth{};
    synth.kind = urmsg::demo::ConversationKind::Direct;
    synth.rows.resize(4);
    for (auto& r : synth.rows) {
      r.kind = urmsg::demo::RowKind::Message;
      r.outgoing = true;
      r.state = DState::Sent;
    }
    synth.rows[1].state = DState::Failed;
    synth.rows[3].outgoing = false;
    const bool kSynthRule[4] = {false, true, true, false};
    const bool kSynthPlan[4] = {false, false, true, false};

    std::size_t synthChecked = 0, synthRuleOk = 0, synthPlanOk = 0;
    for (auto const& p : urmsg::views::PlanThreadRows(synth)) {
      urmsg::demo::MessageRow const* next =
          (p.rowIndex + 1 < synth.rows.size()) ? &synth.rows[p.rowIndex + 1] : nullptr;
      ++synthChecked;
      if (urmsg::views::CarriesDeliveryGlyph(synth.rows[p.rowIndex], next) ==
          kSynthRule[p.rowIndex])
        ++synthRuleOk;
      if (p.endsOutgoingRun == kSynthPlan[p.rowIndex]) ++synthPlanOk;
    }

    lines.push_back(std::format(
        L"  T5 cluster rows      : {} — {} outgoing rows; rule {} vs endsOutgoingRun {}, "
        L"{} disagree ({} Failed mid-run); {} of {} conversations carry one; synthetic "
        L"4-row fixture {}/{} rule and {}/{} plan (mid-run non-failed false, Failed true, "
        L"last-of-run true)",
        (1 <= byRule && byRule == byPlanField + divergent && 1 <= divergent &&
         divergent == divergentFailedMidRun && convsWithCluster == convsWithOutgoing &&
         synthChecked == 4 && synthRuleOk == synthChecked && synthPlanOk == synthChecked)
            ? L"PASS"
            : L"FAIL",
        outRows, byRule, byPlanField, divergent, divergentFailedMidRun, convsWithCluster,
        convsWithOutgoing, synthRuleOk, synthChecked, synthPlanOk, synthChecked));

    // 2. THE BADGE TABLE — per state and per CHANNEL, not a census.
    //
    //    Spec C §5.3 carries delivery state on three channels and colour is
    //    never one of them:
    //      COUNT  Sent and Delivered share a glyph and differ ONLY in repeat;
    //      SHAPE  Delivered and Read share a repeat and differ ONLY in glyph
    //             (E930 ring vs EC61 disc — the font's only true outline/filled
    //             check pair; the bare checks E10B/E001/E0E7/E73E/E8FB are
    //             indistinguishable from one another at 13px);
    //      WORD   all six distinct, which is what a greyscale screenshot keeps.
    //
    //    MUTATION CAUGHT, and why a census would not: substituting a bare check
    //    for E930 on Sent leaves "6 distinct badges" and "1 danger" untouched,
    //    and so does moving `danger` from Failed onto Expired — permuting two
    //    categories leaves every total identical (the lesson T4's shape gate
    //    paid for). So every flag is compared against the STATE it belongs to,
    //    and the two shared-attribute properties are asserted by name.
    const DState kBadgeStates[] = {DState::Pending, DState::Sent,   DState::Delivered,
                                   DState::Read,    DState::Failed, DState::Expired};
    std::vector<std::wstring> keys;
    int emptyGlyph = 0, emptyWord = 0, flagWrong = 0, repeatWrong = 0;
    for (auto s : kBadgeStates) {
      const auto b = urmsg::views::BadgeFor(s);
      if (b.glyph == nullptr || *b.glyph == L'\0') ++emptyGlyph;
      if (b.word == nullptr || *b.word == L'\0') ++emptyWord;
      if (b.danger != (s == DState::Failed)) ++flagWrong;
      if (b.solid != (s == DState::Read || s == DState::Failed)) ++flagWrong;
      if (b.repeat != ((s == DState::Delivered || s == DState::Read) ? 2 : 1)) ++repeatWrong;
      keys.push_back(std::format(L"{}x{}|{}", b.glyph ? b.glyph : L"", b.repeat,
                                 b.word ? b.word : L""));
    }
    std::sort(keys.begin(), keys.end());
    const std::size_t distinct =
        std::size_t(std::unique(keys.begin(), keys.end()) - keys.begin());

    const auto bSent = urmsg::views::BadgeFor(DState::Sent);
    const auto bDeliv = urmsg::views::BadgeFor(DState::Delivered);
    const auto bRead = urmsg::views::BadgeFor(DState::Read);
    // wstring_view, not the raw pointers: comparing wchar_t const* with == is a
    // comparison of ADDRESSES, which the compiler's string pooling can make
    // accidentally true.
    const bool countChannel =
        std::wstring_view{bSent.glyph} == std::wstring_view{bDeliv.glyph} &&
        bSent.repeat != bDeliv.repeat;
    const bool shapeChannel =
        bDeliv.repeat == bRead.repeat &&
        std::wstring_view{bDeliv.glyph} != std::wstring_view{bRead.glyph};
    lines.push_back(std::format(
        L"  T5 delivery badges   : {} — 6 states -> {} distinct (glyph,count,word); "
        L"{} empty glyph(s), {} empty word(s), {} flag(s) and {} count(s) on the wrong "
        L"state; count channel {} shape channel {}",
        (distinct == 6 && emptyGlyph == 0 && emptyWord == 0 && flagWrong == 0 &&
         repeatWrong == 0 && countChannel && shapeChannel)
            ? L"PASS"
            : L"FAIL",
        distinct, emptyGlyph, emptyWord, flagWrong, repeatWrong, countChannel,
        shapeChannel));

    // 3. THE SELECTION INDEX. -1 for "no match" is the point of the function:
    //    an index of 0 would silently paint bubble 0 on every deselect, and in
    //    any screenshot where bubble 0 was the selected one that would look
    //    exactly right.
    //
    //    MUTATIONS CAUGHT: returning 0 rather than -1 for a miss (`deselects`);
    //    an off-by-one (`hits`); a substring implementation — `find` in either
    //    direction — which would resolve the PREFIX "m-" or the EXTENSION
    //    "m-1x" to a real bubble.
    //
    //    And then the same three questions asked of the REAL ids the thread will
    //    hand it. That last count is not decoration: SelectedBubbleIndex returns
    //    the FIRST match, so a world with two message rows sharing an id would
    //    resolve the later one to the earlier index and select the wrong bubble.
    //    `worldHits == worldIds.size()` is that uniqueness stated as a property.
    const std::vector<std::wstring> ids{L"m-1", L"m-2", L"m-3"};
    int hits = 0;
    for (int i = 0; i < 3; ++i)
      if (urmsg::views::SelectedBubbleIndex(ids, ids[std::size_t(i)]) == i) ++hits;
    const bool deselects = urmsg::views::SelectedBubbleIndex(ids, L"") == -1 &&
                           urmsg::views::SelectedBubbleIndex(ids, L"nope") == -1 &&
                           urmsg::views::SelectedBubbleIndex(ids, L"m-") == -1 &&
                           urmsg::views::SelectedBubbleIndex(ids, L"m-1x") == -1;

    std::vector<std::wstring> worldIds;
    for (auto const& c : urmsg::demo::GetWorld().conversations)
      for (auto const& r : c.rows)
        if (r.kind == urmsg::demo::RowKind::Message) worldIds.push_back(r.id);
    std::size_t worldHits = 0;
    for (std::size_t i = 0; i < worldIds.size(); ++i)
      if (urmsg::views::SelectedBubbleIndex(worldIds, worldIds[i]) == static_cast<int>(i))
        ++worldHits;
    lines.push_back(std::format(
        L"  T5 selection index   : {} — {} of 3 synthetic ids resolve to their own index; "
        L"empty/unknown/prefix/extension -> {}; {} of {} world bubble ids resolve to "
        L"their own index (so no two share one)",
        (hits == 3 && deselects && !worldIds.empty() && worldHits == worldIds.size())
            ? L"PASS"
            : L"FAIL",
        hits, deselects ? L"-1" : L"NOT -1", worldHits, worldIds.size()));
  }

  // ---- T6: motion, asserted against the table the RENDER spends -----------
  //
  //  The first version of this line was a tautology and shipped as one:
  //  `EntranceTimelineCount(true) == 4` compared a literal against the literal
  //  in `return animate ? 4 : 0;`, and the builder beside it wrote four add()
  //  calls by hand. Deleting the ScaleY timeline from the builder left this
  //  line printing PASS.
  //
  //  So the count now comes from EntranceTimelines() / TypingTimelines(), the
  //  vectors RunBubbleEntrance and SetThreadTyping ITERATE. Every timeline is
  //  named and its endpoints checked, so deleting one is a FAIL rather than a
  //  smaller number nobody reads.
  //
  //  WHAT THIS STILL CANNOT SEE, said plainly rather than left to be assumed:
  //  that the builders pass ShouldAnimate() into the table rather than `true`.
  //  That call sits in a winrt translation unit CollectDiagnostics cannot enter.
  //  It is why RunBubbleEntrance consults the gate exactly ONCE and treats an
  //  empty table AS the reduce-motion path. That removes the duplicate LIST and
  //  the second read of the gate; it does NOT remove every deletable branch —
  //  RunBubbleEntrance's `plan.empty()` early return still is one, and deleting
  //  it leaves a permanently invisible reduce-motion bubble while these lines
  //  still print PASS. The same blindness covers whether the render CONSUMES
  //  each spec field: drop `if (spec.forever)` and the dots blink once.
  {
    namespace dv = urmsg::views;
    const auto on = dv::EntranceTimelines(true);
    const auto off = dv::EntranceTimelines(false);

    // Per CHANNEL, not a total: permuting two entries, or writing ScaleX twice
    // and never ScaleY, leaves the count at 4 (the lesson T4's shape gate paid
    // for). Each path is looked up by name and its endpoints checked.
    auto find = [&on](wchar_t const* path) -> dv::TimelineSpec const* {
      for (auto const& t : on)
        if (t.path && std::wstring_view{t.path} == std::wstring_view{path}) return &t;
      return nullptr;
    };
    const auto* opacity = find(L"Opacity");
    const auto* ty = find(L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)");
    const auto* sx = find(L"(UIElement.RenderTransform).(CompositeTransform.ScaleX)");
    const auto* sy = find(L"(UIElement.RenderTransform).(CompositeTransform.ScaleY)");

    std::vector<std::wstring> paths;
    bool endpointsOk = true;
    for (auto const& t : on) {
      if (t.path == nullptr || *t.path == L'\0') endpointsOk = false;
      // An entrance is a one-shot: a bubble that reversed or repeated would
      // never settle.
      if (t.autoReverse || t.forever || t.beginMs != 0) endpointsOk = false;
      paths.push_back(t.path ? t.path : L"");
    }
    std::sort(paths.begin(), paths.end());
    const std::size_t distinct =
        std::size_t(std::unique(paths.begin(), paths.end()) - paths.begin());

    const bool channelsOk =
        opacity && ty && sx && sy && opacity->from == 0.0 && opacity->to == 1.0 &&
        ty->from == dv::kBubbleRiseDip && ty->to == 0.0 &&
        sx->from == dv::kBubbleFromScale && sx->to == 1.0 &&
        sy->from == dv::kBubbleFromScale && sy->to == 1.0;

    lines.push_back(std::format(
        L"  T6 bubble entrance   : {} — {} timelines with motion on ({} distinct paths: "
        L"opacity {} rise {} scaleX {} scaleY {}), {} with it off; rise {:.1f} dip, "
        L"scale {:.2f}->1.00, {} ms",
        (on.size() == 4 && off.empty() && distinct == 4 && channelsOk && endpointsOk &&
         dv::EntranceTimelineCount(true) == 4 && dv::EntranceTimelineCount(false) == 0 &&
         dv::kBubbleRiseDip == 10.0 && dv::kBubbleFromScale == 0.96 &&
         urnw::motion::kBaseMs == 250)
            ? L"PASS"
            : L"FAIL",
        on.size(), distinct, opacity != nullptr, ty != nullptr, sx != nullptr,
        sy != nullptr, off.size(), dv::kBubbleRiseDip, dv::kBubbleFromScale,
        urnw::motion::kBaseMs));

    // design d2 §8.2: the conversation-OPEN stagger, asserted on both of its
    // pure halves.
    //
    // Half one: EntranceTimelines(animate, staggerMs) must carry staggerMs
    // into EVERY beginMs while leaving the four timelines otherwise identical
    // to the staggerMs=0 shape — a staggered entrance is still ONE gesture,
    // so the channels never slide relative to one another. `on` above is the
    // staggerMs=0 table the pair is compared against.
    //
    // Half two: OpenStaggerBeginMs must name exactly the visible foot — -1
    // above it (those rows do not animate at all), 0-based kStaggerMs steps
    // within it, capped at kMaxStaggerSteps rows, newest settling last —
    // probed exhaustively at counts 0/1/3/10/50 rather than sampled.
    //
    // WHAT THIS CANNOT SEE: RenderTransformOrigin, which d2 §8.1 makes
    // direction-aware in RunBubbleEntrance — a pure function of one bool did
    // not earn a table row, and the winrt write sits in a TU this harness
    // cannot enter. The bloom's side is capture-verified instead.
    const auto stag = dv::EntranceTimelines(true, urnw::motion::kStaggerMs);
    bool staggerShape = (stag.size() == on.size() && !stag.empty());
    for (std::size_t i = 0; i < stag.size() && i < on.size(); ++i) {
      if (stag[i].path == nullptr || on[i].path == nullptr ||
          std::wstring_view{stag[i].path} != std::wstring_view{on[i].path} ||
          stag[i].from != on[i].from || stag[i].to != on[i].to ||
          stag[i].autoReverse != on[i].autoReverse || stag[i].forever != on[i].forever ||
          stag[i].beginMs != urnw::motion::kStaggerMs || on[i].beginMs != 0)
        staggerShape = false;
    }
    const int64_t step = urnw::motion::kStaggerMs;
    bool footOk = true;
    for (std::size_t i = 0; i < 10; ++i) {
      // count 10: the last kMaxStaggerSteps (indices 4..9) animate, 0..5*step
      // oldest-to-newest; everything above the foot gets -1.
      const int64_t want =
          (i < 10 - static_cast<std::size_t>(urnw::motion::kMaxStaggerSteps))
              ? -1
              : static_cast<int64_t>(i - (10 - static_cast<std::size_t>(
                                               urnw::motion::kMaxStaggerSteps))) *
                    step;
      if (dv::OpenStaggerBeginMs(i, 10) != want) footOk = false;
    }
    // count 3: every row animates. count 1: the one row begins at 0 (it is
    // the whole foot). count 0 and an out-of-range index: -1, guarded.
    if (dv::OpenStaggerBeginMs(0, 3) != 0 || dv::OpenStaggerBeginMs(2, 3) != 2 * step ||
        dv::OpenStaggerBeginMs(0, 1) != 0 || dv::OpenStaggerBeginMs(0, 0) != -1 ||
        dv::OpenStaggerBeginMs(3, 3) != -1)
      footOk = false;
    // count 50: the foot is still exactly kMaxStaggerSteps, and its newest
    // still settles at (steps-1)*step — an uncapped stagger would keep
    // growing with the backlog.
    if (dv::OpenStaggerBeginMs(43, 50) != -1 || dv::OpenStaggerBeginMs(44, 50) != 0 ||
        dv::OpenStaggerBeginMs(49, 50) !=
            static_cast<int64_t>(urnw::motion::kMaxStaggerSteps - 1) * step)
      footOk = false;
    lines.push_back(std::format(
        L"  T6 open stagger      : {} — staggered table keeps the 4 timelines, every "
        L"beginMs {} ms, endpoints identical to the unstaggered shape; open foot -1 "
        L"above the last {}, 0..{} ms oldest->newest within it (counts 0/1/3/10/50 "
        L"probed)",
        (staggerShape && footOk && urnw::motion::kStaggerMs == 40 &&
         urnw::motion::kMaxStaggerSteps == 6)
            ? L"PASS"
            : L"FAIL",
        urnw::motion::kStaggerMs, urnw::motion::kMaxStaggerSteps,
        (urnw::motion::kMaxStaggerSteps - 1) * urnw::motion::kStaggerMs));

    // The three phases are 0/140/280 and every one of them must be INSIDE the
    // pulse it offsets: a phase >= kPulseMs is not a wave, it is three dots
    // taking turns. Read off the SPECS, so a builder that dropped a dot's
    // BeginTime would be caught and not merely the helper that computes it.
    const auto typeOn = dv::TypingTimelines(true);
    const auto typeOff = dv::TypingTimelines(false);
    std::wstring phases;
    bool phasesOk = (dv::TypingDotPhaseMs(-1) == -1 &&
                     dv::TypingDotPhaseMs(dv::kTypingDots) == -1 &&
                     typeOn.size() == std::size_t(dv::kTypingDots));
    for (std::size_t i = 0; i < typeOn.size(); ++i) {
      auto const& t = typeOn[i];
      const int64_t want = dv::TypingDotPhaseMs(static_cast<int>(i));
      if (t.beginMs != want || want != dv::kTypingPhaseMs * static_cast<int64_t>(i) ||
          urnw::motion::kPulseMs <= want)
        phasesOk = false;
      // A dot that did not come back, or did not repeat, is one blink.
      if (!t.autoReverse || !t.forever || t.from != 0.30 || t.to != 1.0 ||
          t.path == nullptr || std::wstring_view{t.path} != std::wstring_view{L"Opacity"})
        phasesOk = false;
      phases += (i ? L"/" : L"") + std::to_wstring(t.beginMs);
    }
    lines.push_back(std::format(
        L"  T6 typing indicator  : {} — {} dots at {} ms (offset {}), each 0.30->1.00 "
        L"auto-reversed and repeating; {} timelines on / {} off, all phases < kPulseMs {}",
        (phasesOk && typeOff.empty() && dv::TypingTimelineCount(true) == dv::kTypingDots &&
         dv::TypingTimelineCount(false) == 0)
            ? L"PASS"
            : L"FAIL",
        dv::kTypingDots, phases, dv::kTypingPhaseMs, typeOn.size(), typeOff.size(),
        urnw::motion::kPulseMs));
  }

  // ---- T6: ONE reading per run, ACROSS an append ---------------------------
  //
  //  The property: appending a row to a column must leave it drawing exactly
  //  what a full rebuild of the grown conversation would draw. That is the
  //  one-drawer rule of design §6.2 stated so it survives the incremental
  //  path, and it is asserted PER ROW against the rebuild rather than as a
  //  total — two rows swapping cluster and no-cluster leaves every count
  //  identical (the lesson T4's shape gate paid for).
  //
  //  TWO ATTRIBUTES, because a per-row comparison is only as wide as the
  //  attributes it compares. The delivery cluster is one. The SENDER HEADER is
  //  the other, and it was added in fix round 1 after AppendThreadRow was found
  //  hard-coding it to false: that gates the sender name AND the identicon, so
  //  an ambient incoming group row drew neither while a rebuild drew both, and
  //  the cluster-only gate certified it.
  //
  //  THREE COUNTERFACTUALS, each a way the append path has actually been
  //  written wrong, each REQUIRED to differ from the rebuild — a gate whose
  //  wrong answers agree with its right one is not comparing anything:
  //    append-only     never revisits the row above          (the brief's bug)
  //    message-only    remembers only Message rows as `prev` (the tempting
  //                    simplification; ThreadParts records EVERY row instead)
  //    header-false    hard-codes showSenderHeader           (fix round 1)
  //
  //  WHAT THIS GATE CAN AND CANNOT REACH, stated rather than implied. It is
  //  pure and runs before winrt::init_apartment(), so it cannot execute
  //  AppendThreadRow. What it asserts is the RULES that function calls, and
  //  that each wrong policy really would draw a different column — so adopting
  //  one is a bug this line has already named and measured. The render half is
  //  covered by the capture: the ambient incoming group row drawing a sender
  //  name and an identicon is what shows AppendThreadRow asking
  //  ShowsSenderHeader rather than hard-coding false.
  //
  //  MUTATIONS RUN, each against the thing named, with the observed effect —
  //  no predicted numbers (task-T6-report.md §4 records the output):
  //    - PlanAppendCluster stops removing (prevCarriesNow = prevCarried):
  //      incremental collapses onto `append-only` and is wrong on 1.
  //    - The Failed exception deleted from CarriesDeliveryGlyph: incremental
  //      still matches the rebuild — both consult the same broken rule — and it
  //      is `Failed kept` and `Failed extra` going to 0 that FAIL. A gate that
  //      only compared incremental against rebuild would be blind to it.
  //    - The SYSTEM row dropped from this scenario: `message-only` stops
  //      differing. That row is the only thing that makes the record-every-row
  //      decision testable, and without it the clause is vacuous — which is
  //      exactly what the first version of this gate shipped.
  //    - The fixture made Direct rather than Group: every header goes false and
  //      `header-false` stops differing, so the header half goes vacuous too.
  //
  //  Built LOCALLY, and as a whole Conversation so PlanThreadRows really runs
  //  on it — never by editing Demo/DemoWorld.cpp, whose exact bytes I10
  //  fingerprints. The shipped world cannot stand in: nothing appends to it.
  {
    using DState = urmsg::demo::DeliveryState;
    namespace dv = urmsg::views;

    auto mkMsg = [](bool outgoing, DState st, uint8_t senderKey) {
      urmsg::demo::MessageRow r{};
      r.kind = urmsg::demo::RowKind::Message;
      r.outgoing = outgoing;
      r.state = st;
      r.senderKey[0] = senderKey;  // ShowsSenderHeader breaks a run on THIS
      return r;
    };
    auto mkSystem = []() {
      urmsg::demo::MessageRow r{};
      r.kind = urmsg::demo::RowKind::System;
      r.systemText = L"synthetic system row";
      return r;
    };

    // A GROUP, because ShowsSenderHeader is false in a DM by definition and a
    // Direct fixture could not exercise the header half at all.
    //
    // The seed column, as SetThreadConversation would have drawn it:
    //   s0 outgoing Sent   — next is outgoing, so NOT last of run -> no cluster
    //   s1 outgoing Failed — newest, and Failed                   -> cluster
    urmsg::demo::Conversation seed{};
    seed.kind = urmsg::demo::ConversationKind::Group;
    seed.rows.push_back(mkMsg(true, DState::Sent, 0));
    seed.rows.push_back(mkMsg(true, DState::Failed, 0));

    // Eight appends. Every position of both rules is exercised once:
    //   a0 outgoing Pending  — lands under the FAILED row, which must KEEP its
    //                          cluster (the exception; blanket removal fails here)
    //   a1 outgoing Sent     — lands under a0, which must LOSE its cluster
    //                          (the run-end rule; append-only fails here)
    //   a2 incoming, key 1   — a1 stays last of its run and KEEPS its cluster;
    //                          and it STARTS a run, so header TRUE
    //   a3 incoming, key 1   — same sender -> continuation, header FALSE
    //   a4 incoming, key 2   — different sender -> header TRUE again
    //   a5 outgoing Read     — previous row is incoming; nothing to revisit
    //   a6 SYSTEM            — a5 stays last of its outgoing run and KEEPS its
    //                          cluster, because a system row is not an outgoing
    //                          message
    //   a7 outgoing Sent     — its `prev` is the SYSTEM row, so nothing changes.
    //                          An append path that remembered only MESSAGE rows
    //                          would see a5 as `prev` here and strip a5's
    //                          cluster. That is the whole reason ThreadParts
    //                          records every row, and a6/a7 are the two rows
    //                          that make the claim testable.
    const urmsg::demo::MessageRow kAppends[] = {
        mkMsg(true, DState::Pending, 0), mkMsg(true, DState::Sent, 0),
        mkMsg(false, DState::Sent, 1),   mkMsg(false, DState::Sent, 1),
        mkMsg(false, DState::Sent, 2),   mkMsg(true, DState::Read, 0),
        mkSystem(),                      mkMsg(true, DState::Sent, 0),
    };

    // What a FULL build of a conversation draws, taken exactly the way
    // SetThreadConversation takes it: the plan, then CarriesDeliveryGlyph with
    // the following row, and the plan's own showSenderHeader. This is the
    // reference the incremental path must match on BOTH attributes.
    struct Drawn {
      std::vector<bool> cluster;
      std::vector<bool> header;
    };
    auto renderAll = [](urmsg::demo::Conversation const& c) {
      Drawn d;
      for (auto const& pl : dv::PlanThreadRows(c)) {
        urmsg::demo::MessageRow const* next =
            (pl.rowIndex + 1 < c.rows.size()) ? &c.rows[pl.rowIndex + 1] : nullptr;
        d.cluster.push_back(dv::CarriesDeliveryGlyph(c.rows[pl.rowIndex], next));
        d.header.push_back(pl.showSenderHeader);
      }
      return d;
    };

    const Drawn seedDrawn = renderAll(seed);
    std::vector<bool> live = seedDrawn.cluster;   // AppendThreadRow's path
    std::vector<bool> naive = seedDrawn.cluster;  // ...minus the removal
    std::vector<bool> msgOnly = seedDrawn.cluster;  // ...remembering Message rows only
    std::vector<bool> head = seedDrawn.header;      // AppendThreadRow's header
    std::vector<bool> headFalse = seedDrawn.header;  // ...hard-coded to false

    urmsg::demo::Conversation grown = seed;
    // The message-rows-only counterfactual keeps its own idea of `prev`: the
    // last MESSAGE row rather than the last row. Indices line up with
    // grown.rows because every row contributes exactly one entry, in order.
    std::optional<std::size_t> lastMsg;
    if (!seed.rows.empty()) lastMsg = seed.rows.size() - 1;
    std::size_t removals = 0, additions = 0, failedKept = 0;
    for (auto const& a : kAppends) {
      urmsg::demo::MessageRow const* prev =
          grown.rows.empty() ? nullptr : &grown.rows.back();
      const dv::AppendClusterPlan plan = dv::PlanAppendCluster(prev, a);
      if (prev) {
        if (plan.prevMustLose) ++removals;
        if (plan.prevMustGain) ++additions;
        if (prev->outgoing && prev->state == DState::Failed && plan.prevCarried &&
            plan.prevCarriesNow)
          ++failedKept;
        live.back() = plan.prevCarriesNow;  // the half the brief dropped
      }
      live.push_back(plan.appendedCarries);
      naive.push_back(plan.appendedCarries);

      // message-only: the same code, but `prev` skips back over the system row.
      {
        urmsg::demo::MessageRow const* mprev = lastMsg ? &grown.rows[*lastMsg] : nullptr;
        const dv::AppendClusterPlan mp = dv::PlanAppendCluster(mprev, a);
        if (mprev) msgOnly[*lastMsg] = mp.prevCarriesNow;
        msgOnly.push_back(mp.appendedCarries);
      }

      // The kind comes from the FIXTURE, the way AppendThreadRow takes it from
      // parts->group, so a Direct fixture cannot leave this half asserting a
      // header the rebuild does not draw.
      head.push_back(dv::ShowsSenderHeader(
          prev, a, grown.kind == urmsg::demo::ConversationKind::Group));
      headFalse.push_back(false);

      grown.rows.push_back(a);
      if (a.kind == urmsg::demo::RowKind::Message) lastMsg = grown.rows.size() - 1;
    }
    const Drawn rebuilt = renderAll(grown);

    std::size_t liveWrong = 0, naiveWrong = 0, msgOnlyWrong = 0, headWrong = 0,
                headFalseWrong = 0;
    for (std::size_t i = 0; i < rebuilt.cluster.size(); ++i) {
      if (live.size() <= i || live[i] != rebuilt.cluster[i]) ++liveWrong;
      if (naive.size() <= i || naive[i] != rebuilt.cluster[i]) ++naiveWrong;
      if (msgOnly.size() <= i || msgOnly[i] != rebuilt.cluster[i]) ++msgOnlyWrong;
      if (head.size() <= i || head[i] != rebuilt.header[i]) ++headWrong;
      if (headFalse.size() <= i || headFalse[i] != rebuilt.header[i]) ++headFalseWrong;
    }

    // And the property said directly, over the grown column: each outgoing run
    // ends in exactly ONE reading, and any other reading inside a run sits on a
    // Failed row. `stray` is a second drawer in a run, which is the bug.
    auto isOutgoingMessage = [](urmsg::demo::MessageRow const& r) {
      return r.kind == urmsg::demo::RowKind::Message && r.outgoing;
    };
    std::size_t runs = 0, runEndReadings = 0, failedExtras = 0, stray = 0, clusters = 0;
    for (std::size_t i = 0; i < grown.rows.size(); ++i) {
      auto const& r = grown.rows[i];
      if (!isOutgoingMessage(r)) continue;
      if (rebuilt.cluster[i]) ++clusters;
      if (i == 0 || !isOutgoingMessage(grown.rows[i - 1])) ++runs;
      const bool endsRun =
          (i + 1 == grown.rows.size()) || !isOutgoingMessage(grown.rows[i + 1]);
      if (endsRun) {
        if (rebuilt.cluster[i]) ++runEndReadings;
      } else if (rebuilt.cluster[i]) {
        if (r.state == DState::Failed)
          ++failedExtras;
        else
          ++stray;
      }
    }

    auto bits = [](std::vector<bool> const& v) {
      std::wstring out;
      for (bool b : v) out += (b ? L'1' : L'0');
      return out;
    };

    lines.push_back(std::format(
        L"  T6 append cluster    : {} — {} seed + {} appended rows (1 system); clusters "
        L"incremental {} vs rebuild {} ({} wrong); append-only {} off by {}, message-only "
        L"{} off by {}; headers incremental {} vs rebuild {} ({} wrong), header-false off "
        L"by {}; {} removal(s), {} addition(s) (0 by construction), {} Failed row(s) kept "
        L"theirs; {} outgoing run(s), {} run-end reading(s), {} stray second reading(s), "
        L"{} Failed extra(s), {} cluster(s)",
        (liveWrong == 0 && 1 <= naiveWrong && 1 <= msgOnlyWrong && headWrong == 0 &&
         1 <= headFalseWrong && 1 <= removals && additions == 0 && failedKept == 1 &&
         runs == 3 && runEndReadings == runs && stray == 0 && failedExtras == 1 &&
         clusters == 4)
            ? L"PASS"
            : L"FAIL",
        seed.rows.size(), std::size(kAppends), bits(live), bits(rebuilt.cluster), liveWrong,
        bits(naive), naiveWrong, bits(msgOnly), msgOnlyWrong, bits(head),
        bits(rebuilt.header), headWrong, headFalseWrong, removals, additions, failedKept,
        runs, runEndReadings, stray, failedExtras, clusters));
  }

  // ---- T7: run-shape geometry (design d2 §1) -------------------------------
  //
  //  The corner language is pure planner work (Views/ThreadLayout.h), which is
  //  the only reason any of it is assertable here. Four parts, each aimed at
  //  a different failure class:
  //
  //  1. THE CORNER TABLE, cell by cell against a hand-written expectation. A
  //     census of radii would not feel two cells swapped — permuting
  //     categories leaves totals identical (the lesson T4's shape gate paid
  //     for). MUTATION CAUGHT: swapping the incoming/outgoing tables, or
  //     tightening TL where BL belongs — both read as plausible corners in a
  //     screenshot and both FAIL here. GapAboveDip is asserted with it (2
  //     continuation / 10 run-start — the rhythm that replaced
  //     stack.Spacing(6)).
  //
  //  2. PER ROW over the whole world: the plan's runPos against the rule
  //     recomputed here from the rows alone. This catches the plan mis-wiring
  //     its prev/next (runPos silently Single everywhere, or judged from the
  //     wrong neighbour) — the recomputation shares the RULE, so it cannot
  //     catch the rule itself being wrong. That is what part 3 is for.
  //
  //  3. THE NAMED ROWS, d2 §1's concrete anchors: c0's Mira/Mira/Tobias
  //     stretch (DemoWorld.cpp:256-258) is First/Last/SINGLE — a boundary the
  //     senderKey comparison draws and direction alone does not — and the
  //     12:09-12:11 outgoing pair (:277-279) is First/Last. A direction-only
  //     run rule (the historical bug class here: T4's sender-header gate was
  //     born of exactly it, 20 headers vs 15) reads the stretch as
  //     First/Middle/Last and FAILS. Anchored by row id, and all five are
  //     REQUIRED to be found — a fixture that loses one fails rather than
  //     going quietly vacuous (the same reason `divergent >= 1` is required
  //     in `T5 cluster rows`).
  //
  //  4. THE CASES THE WORLD CANNOT PROVIDE, built locally — never by editing
  //     Demo/DemoWorld.cpp, whose bytes I10 fingerprints. The world HAS
  //     exactly one Middle (conv-freya's 11:29-11:44 three-row stretch — the
  //     walk below counts it), so the position is exercised on real data.
  //     What the world still cannot prove: an OUTGOING Middle — every
  //     outgoing row there carries the same "me" senderKey, so unification
  //     under one speaker is indistinguishable from merely sharing a key —
  //     and a system row / day separator breaking a run mid-stretch. The
  //     synthetic fixture covers all three.
  {
    namespace dv = urmsg::views;
    using RunPos = dv::BubbleRunPos;

    struct CornerProbe {
      RunPos pos;
      bool outgoing;
      double want[4];  // TL,TR,BR,BL — the order CornerRadiusFromCorners writes
    };
    const CornerProbe kCorners[] = {
        {RunPos::Single, false, {12, 12, 12, 12}},
        {RunPos::Single, true, {12, 12, 12, 12}},
        {RunPos::First, false, {12, 12, 12, 4}},
        {RunPos::First, true, {12, 12, 4, 12}},
        {RunPos::Middle, false, {4, 12, 12, 4}},
        {RunPos::Middle, true, {12, 4, 4, 12}},
        {RunPos::Last, false, {4, 12, 12, 12}},
        {RunPos::Last, true, {12, 4, 12, 12}},
    };
    std::size_t cornersOk = 0;
    for (auto const& probe : kCorners) {
      double got[4] = {0, 0, 0, 0};
      dv::BubbleCornerDip(probe.pos, probe.outgoing, got);
      if (got[0] == probe.want[0] && got[1] == probe.want[1] && got[2] == probe.want[2] &&
          got[3] == probe.want[3])
        ++cornersOk;
    }
    const bool gapsOk = dv::GapAboveDip(RunPos::Single) == 10.0 &&
                        dv::GapAboveDip(RunPos::First) == 10.0 &&
                        dv::GapAboveDip(RunPos::Middle) == 2.0 &&
                        dv::GapAboveDip(RunPos::Last) == 2.0;

    // The rule, restated from the spec for the per-row walk (part 2).
    auto expectedRunPos = [](urmsg::demo::MessageRow const* prev,
                             urmsg::demo::MessageRow const& cur,
                             urmsg::demo::MessageRow const* next) {
      // A COLLAPSED OBSERVER ROW IS NOT A BUBBLE AND BREAKS EVERY RUN (item 242
      // R4), recomputed here off the row's own word rather than by asking the
      // planner's predicate — a mutant that made IsHiddenObserverRow answer for
      // every row would otherwise be agreed with instead of caught.
      auto collapses = [](urmsg::demo::MessageRow const& r) {
        return r.kind == urmsg::demo::RowKind::Message &&
               r.senderRoleAtSend == urmsg::demo::kRoleObserver;
      };
      auto cont = [&collapses](urmsg::demo::MessageRow const& a,
                               urmsg::demo::MessageRow const& b) {
        if (a.kind != urmsg::demo::RowKind::Message ||
            b.kind != urmsg::demo::RowKind::Message)
          return false;
        if (collapses(a) || collapses(b)) return false;
        if (a.outgoing != b.outgoing) return false;
        return a.outgoing || a.senderKey == b.senderKey;
      };
      // The collapsed row keeps the planner's default, exactly as a day
      // separator does: it draws no corners, so its position in a run is not a
      // thing the plan has an opinion about.
      if (collapses(cur)) return RunPos::Single;
      const bool up = prev != nullptr && cont(*prev, cur);
      const bool down = next != nullptr && cont(cur, *next);
      if (!up) return down ? RunPos::First : RunPos::Single;
      return down ? RunPos::Middle : RunPos::Last;
    };

    std::size_t msgRows = 0, runWrong = 0;
    std::size_t singles = 0, firsts = 0, middles = 0, lasts = 0;
    std::size_t anchorsFound = 0, anchorsOk = 0;
    for (auto const& c : urmsg::demo::GetWorld().conversations) {
      for (auto const& p : dv::PlanThreadRows(c)) {
        auto const& r = c.rows[p.rowIndex];
        if (r.kind != urmsg::demo::RowKind::Message) continue;
        ++msgRows;
        urmsg::demo::MessageRow const* prev =
            (p.rowIndex > 0) ? &c.rows[p.rowIndex - 1] : nullptr;
        urmsg::demo::MessageRow const* next =
            (p.rowIndex + 1 < c.rows.size()) ? &c.rows[p.rowIndex + 1] : nullptr;
        if (p.runPos != expectedRunPos(prev, r, next)) ++runWrong;
        switch (p.runPos) {
          case RunPos::Single: ++singles; break;
          case RunPos::First: ++firsts; break;
          case RunPos::Middle: ++middles; break;
          case RunPos::Last: ++lasts; break;
        }
        // d2 §1's named rows, by id. Any message row not named here adds
        // nothing to the anchor count.
        // c0's last outgoing pair moved down by TWO with item 242 R4: the
        // collapsed observer row went in at c0-r22 and the run-break control
        // under it at c0-r23, so the Failed/Pending First/Last pair the anchor
        // names is now r24/r25. The ids are the anchor, so they move with the
        // rows rather than the rule moving.
        RunPos anchor = RunPos::Single;
        bool isAnchor = true;
        if (r.id == L"c0-r1" || r.id == L"c0-r24")
          anchor = RunPos::First;
        else if (r.id == L"c0-r2" || r.id == L"c0-r25")
          anchor = RunPos::Last;
        else if (r.id == L"c0-r3")
          anchor = RunPos::Single;
        else
          isAnchor = false;
        if (isAnchor) {
          ++anchorsFound;
          if (p.runPos == anchor) ++anchorsOk;
        }
      }
    }

    // The synthetic fixture (part 4). Keys distinguish senders; a single byte
    // is enough because ContinuesBubbleRun compares the whole Seed for
    // equality (the T6 append gate's mkMsg sets senderKey the same way).
    auto mkMsg = [](bool outgoing, uint8_t key) {
      urmsg::demo::MessageRow r{};
      r.kind = urmsg::demo::RowKind::Message;
      r.outgoing = outgoing;
      r.senderKey[0] = key;
      return r;
    };
    urmsg::demo::Conversation synth{};
    synth.kind = urmsg::demo::ConversationKind::Group;
    synth.rows.push_back(mkMsg(false, 1));  // s0 First  } incoming 3-run in a
    synth.rows.push_back(mkMsg(false, 1));  // s1 Middle } GROUP (the world's
    synth.rows.push_back(mkMsg(false, 1));  // s2 Last   } one Middle is in a DM)
    synth.rows.push_back(mkMsg(false, 2));  // s3 Single — senderKey breaks it
    {
      urmsg::demo::MessageRow sys{};
      sys.kind = urmsg::demo::RowKind::System;
      sys.systemText = L"synthetic system row";
      synth.rows.push_back(sys);  // s4 — not a message; breaks runs
    }
    synth.rows.push_back(mkMsg(false, 1));  // s5 Single — the system row broke it
    synth.rows.push_back(mkMsg(true, 1));   // s6 First  } outgoing, three
    synth.rows.push_back(mkMsg(true, 9));   // s7 Middle } DIFFERENT keys and
    synth.rows.push_back(mkMsg(true, 2));   // s8 Last   } still one run ("You")
    {
      urmsg::demo::MessageRow sep{};
      sep.kind = urmsg::demo::RowKind::DaySeparator;
      sep.body = L"Today";
      synth.rows.push_back(sep);  // s9 — breaks runs
    }
    synth.rows.push_back(mkMsg(true, 3));  // s10 Single — the separator broke it
    const RunPos kSynthWant[] = {RunPos::First, RunPos::Middle, RunPos::Last,
                                 RunPos::Single, RunPos::Single, RunPos::First,
                                 RunPos::Middle, RunPos::Last,   RunPos::Single};
    std::size_t synthChecked = 0, synthOk = 0;
    for (auto const& p : dv::PlanThreadRows(synth)) {
      auto const& r = synth.rows[p.rowIndex];
      if (r.kind != urmsg::demo::RowKind::Message) continue;
      // s0..s3 are 0..3, s5..s8 are 4..7, s10 is 8 in the want table.
      const std::size_t slot = (p.rowIndex <= 3) ? p.rowIndex
                               : (p.rowIndex <= 8) ? p.rowIndex - 1
                                                   : 8;
      ++synthChecked;
      if (p.runPos == kSynthWant[slot]) ++synthOk;
    }

    lines.push_back(std::format(
        L"  T7 run geometry      : {} — corner table {}/8 cells (base 12, attached 4), "
        L"gaps 2/10 {}; {} message rows per-row vs rule ({} wrong): {} single {} first "
        L"{} middle {} last; named rows {}/5 found and correct (c0-r1/r2/r3 "
        L"First/Last/Single, c0-r24/r25 First/Last); synthetic fixture {}/{} message rows "
        L"(Middle in+out, sender break, system break, separator break, outgoing unifies "
        L"across keys)",
        (cornersOk == 8 && gapsOk && runWrong == 0 && 1 <= singles && 1 <= firsts &&
         1 <= lasts && anchorsFound == 5 && anchorsOk == 5 && synthChecked == 9 &&
         synthOk == 9)
            ? L"PASS"
            : L"FAIL",
        cornersOk, gapsOk ? L"ok" : L"WRONG", msgRows, runWrong, singles, firsts, middles,
        lasts, anchorsOk, synthOk, synthChecked));
  }

  // ---- T8: the sliding window over the backlog ------------------------------
  //
  //  A conversation renders at most the 500 most-relevant rows; older history
  //  materializes in 100-row chunks as the scroller nears an edge of the
  //  loaded range, and the window slides back down the same way. Every
  //  decision about it is pure arithmetic in Views/ThreadLayout, which is the
  //  only reason any of it is assertable here (CollectDiagnostics runs before
  //  winrt::init_apartment()).
  //
  //  WHAT THESE LINES CANNOT SEE, said plainly: the ScrollViewer events that
  //  feed NearTopOfLoaded/NearFootOfLoaded, the loading shimmer, and the
  //  one-unit chunk fade all live in ThreadView.cpp behind winrt. What is
  //  asserted here is the arithmetic they spend — bounds, chunk tiling, seam
  //  order against the real planner, thresholds, offset deltas, ambient
  //  bookkeeping, refresh placement. The render half is capture-verified.
  {
    namespace dv = urmsg::views;
    using dv::ThreadWindow;

    // 1. BOUNDS. The window never exceeds 500 rendered, always covers the
    //    newest row at open, and is DISABLED at or under the cap — [0, total),
    //    the whole conversation, so a <=500-row render is exactly what it was
    //    before this wave. Compared against literals/min recomputed HERE, not
    //    the constants, so a wrong constant fails rather than agreeing with
    //    itself (the DemoLayoutCheck rule).
    //
    //    MUTATION CAUGHT: InitialWindow capping at 400, or WindowActive going
    //    >= instead of > — the 500 case is the boundary and it is probed on
    //    BOTH sides (499/500/501), not sampled.
    const std::size_t kSizes[] = {0, 1, 100, 499, 500, 501, 2024, 4024};
    bool boundsOk = true;
    for (std::size_t total : kSizes) {
      const ThreadWindow w = dv::InitialWindow(total);
      const std::size_t wantStart = total > 500 ? total - 500 : 0;
      if (w.end != total || w.start != wantStart) boundsOk = false;
      if (dv::WindowRowCount(w) != (total > 500 ? 500 : total)) boundsOk = false;
      if (dv::WindowActive(total) != (total > 500)) boundsOk = false;
      if (total > 0 && !dv::WindowCovers(w, total - 1)) boundsOk = false;
      // The oldest row is covered exactly when the window is disabled.
      if (dv::WindowCovers(w, 0) != (total <= 500 && total > 0)) boundsOk = false;
    }
    lines.push_back(std::format(
        L"  T8 window bounds     : {} — sizes 0/1/100/499/500/501/2024/4024: never "
        L">500 rendered, newest row covered at open, disabled (covers ALL rows) at "
        L"or under 500",
        boundsOk ? L"PASS" : L"FAIL"));

    // 2. SLIDE ARITHMETIC, walked as a whole session over a 2024-row world:
    //    open at [1524, 2024), slide up until start == 0 (16 slides — 15 full
    //    chunks and a partial 24), slide back down until end == 2024.
    //
    //    THE TILING IS THE ASSERTION, not the counts: every index ENTERS the
    //    window exactly once going up (no gaps, no dupes against the full
    //    range [0, 1524)) and the down-slides re-enter exactly the ranges the
    //    up-slides trimmed, landing back on [1524, 2024) — the window a fresh
    //    open would draw. A census would not feel two chunks swapped (the
    //    lesson T4's shape gate paid for), so indices are tracked one by one.
    //
    //    MUTATIONS CAUGHT: the up-slide trimming the HEAD instead of the foot
    //    (the entering index set stops tiling), a 99-row chunk (per-slide
    //    arithmetic fails), and the endpoints not being idempotent.
    {
      constexpr std::size_t kTotal = 2024;
      std::vector<bool> seenUp(kTotal, false), seenDown(kTotal, false);
      std::size_t enteredUp = 0, enteredDown = 0, slidesUp = 0, slidesDown = 0;
      bool arithmeticOk = true, sizeOk = true, monotoneOk = true;

      ThreadWindow w = dv::InitialWindow(kTotal);
      if (w.start != 1524 || w.end != 2024) arithmeticOk = false;
      while (w.start > 0) {
        const ThreadWindow before = w;
        w = dv::SlideWindowUp(w);
        ++slidesUp;
        // The head grows by min(chunk, old start); the foot trims back to the
        // cap. Both halves are checked against recomputation, per slide.
        const std::size_t wantAdd = (std::min)(std::size_t{100}, before.start);
        if (before.start - w.start != wantAdd) arithmeticOk = false;
        if (w.end != (w.start + 500 < before.end ? w.start + 500 : before.end))
          arithmeticOk = false;
        for (std::size_t i = w.start; i < before.start; ++i) {
          if (seenUp[i]) arithmeticOk = false;  // a dupe entering
          seenUp[i] = true;
          ++enteredUp;
        }
        if (dv::WindowRowCount(w) != 500) sizeOk = false;
        if (!(w.start < before.start && w.end <= before.end)) monotoneOk = false;
        if (256 < slidesUp) break;  // loop guard: a stuck slide must not hang the gate
      }
      // The top: start == 0, and sliding again changes NOTHING (idempotent).
      if (w.start != 0 || dv::SlideWindowUp(w).start != 0 || dv::SlideWindowUp(w).end != w.end)
        arithmeticOk = false;
      // Every index below the open window entered exactly once: [0, 1524).
      std::size_t tiled = 0;
      for (std::size_t i = 0; i < 1524; ++i)
        if (seenUp[i]) ++tiled;
      for (std::size_t i = 1524; i < kTotal; ++i)
        if (seenUp[i]) arithmeticOk = false;  // rows that never left must not "enter"

      while (w.end < kTotal) {
        const ThreadWindow before = w;
        w = dv::SlideWindowDown(w, kTotal);
        ++slidesDown;
        const std::size_t wantAdd = (std::min)(std::size_t{100}, kTotal - before.end);
        if (w.end - before.end != wantAdd) arithmeticOk = false;
        if (w.start != (before.end + wantAdd > 500 ? before.end + wantAdd - 500 : 0))
          arithmeticOk = false;
        for (std::size_t i = before.end; i < w.end; ++i) {
          if (seenDown[i]) arithmeticOk = false;
          seenDown[i] = true;
          ++enteredDown;
        }
        if (dv::WindowRowCount(w) != 500) sizeOk = false;
        if (!(w.end > before.end && w.start >= before.start)) monotoneOk = false;
        if (256 < slidesDown) break;
      }
      // Home: end == total, idempotent, and EXACTLY the window a fresh open
      // draws — the slide is reversible, which is what makes "the window
      // re-covers them as it slides home" true rather than aspirational.
      const ThreadWindow fresh = dv::InitialWindow(kTotal);
      const bool home = w.start == fresh.start && w.end == fresh.end &&
                        dv::SlideWindowDown(w, kTotal).end == kTotal;
      // The down-slides re-entered exactly what the up-slides had trimmed:
      // seenDown must be the complement of what stayed rendered throughout.
      std::size_t retiled = 0;
      for (std::size_t i = 500; i < kTotal; ++i)
        if (seenDown[i]) ++retiled;
      const bool ok = arithmeticOk && sizeOk && monotoneOk && slidesUp == 16 &&
                      slidesDown == 16 && enteredUp == 1524 && tiled == 1524 &&
                      enteredDown == 1524 && retiled == 1524 && home;
      lines.push_back(std::format(
          L"  T8 window slide      : {} — 2024 rows: {} slides up ({} rows entered, "
          L"{} tiled [0,1524) exactly once), {} slides down ({} re-entered, {} "
          L"retiled), size 500 throughout {}, monotone {}, home == fresh open {}",
          ok ? L"PASS" : L"FAIL", slidesUp, enteredUp, tiled, slidesDown, enteredDown,
          retiled, sizeOk, monotoneOk, home));
    }

    // 3. THE SEAM, against the REAL planner over a stressed world. The chunk
    //    entering at each slide must be exactly the next slice of
    //    Conversation::rows, in order, no gaps, no dupes — and every row's
    //    planned shape must match the expectation recomputed HERE from the row
    //    kind, so a slide that built from the wrong indices (off by one at the
    //    seam) FAILS on shape or order, not on a count. Built LOCALLY through
    //    the stress generator — never by editing Demo/DemoWorld.cpp, whose
    //    bytes I10 fingerprints — the same pattern the `demo stress` gate uses.
    {
      namespace dd = urmsg::demo;
      dd::World a = dd::GetWorld();
      const int added = dd::PrependStressHistory(a, 500);
      dd::Conversation const& c = a.conversations.front();
      const std::size_t total = c.rows.size();  // 26 fixture + 500 synthetic
      const auto plan = dv::PlanThreadRows(c);
      // Recomputed here, off the row's own word (item 242 R4): a collapsed
      // observer row draws no bubble and breaks every run, so the neighbour a
      // run predicate is asked about is nullptr exactly where one stands.
      auto collapses = [](dd::MessageRow const& r) {
        return r.kind == dd::RowKind::Message && r.senderRoleAtSend == dd::kRoleObserver;
      };
      auto expectedShape = [&collapses](dd::MessageRow const& r) {
        switch (r.kind) {
          case dd::RowKind::DaySeparator: return dv::ThreadRowShape::DaySeparator;
          case dd::RowKind::System:
            return r.permanentRecord ? dv::ThreadRowShape::SystemPermanentRecord
                                     : dv::ThreadRowShape::SystemLine;
          case dd::RowKind::Message:
            if (collapses(r)) return dv::ThreadRowShape::HiddenObserverRecord;
            return r.outgoing ? dv::ThreadRowShape::OutgoingBubble
                              : dv::ThreadRowShape::IncomingBubble;
        }
        return dv::ThreadRowShape::SystemLine;
      };

      bool seamOk = (added == 500 && total == 526 && plan.size() == total);
      std::size_t slices = 0, rowsChecked = 0;
      // Walk the slides; at each step the window must slice the plan
      // EXACTLY: plan[window.start + k].rowIndex == window.start + k, in
      // order, shapes right — the render loop reads the same slice.
      ThreadWindow w = dv::InitialWindow(total);
      while (true) {
        ++slices;
        for (std::size_t i = w.start; i < w.end && seamOk; ++i) {
          const auto& p = plan[i];
          if (p.rowIndex != i) { seamOk = false; break; }
          if (p.shape != expectedShape(c.rows[i])) { seamOk = false; break; }
          // Sender header and run position agree with the rules over the FULL
          // history: the seam row must render with the header/corners a full
          // rebuild gives it, which is exactly why the plan walks the whole
          // conversation and the window only slices it.
          dd::MessageRow const* prev = (i > 0 && !collapses(c.rows[i - 1])) ? &c.rows[i - 1]
                                                                           : nullptr;
          dd::MessageRow const* next =
              (i + 1 < total && !collapses(c.rows[i + 1])) ? &c.rows[i + 1] : nullptr;
          if (c.rows[i].kind == dd::RowKind::Message && !collapses(c.rows[i])) {
            if (p.showSenderHeader != dv::ShowsSenderHeader(prev, c.rows[i], true))
              seamOk = false;
            if (p.runPos != dv::RunPosFor(prev, c.rows[i], next)) seamOk = false;
          }
          ++rowsChecked;
        }
        if (w.start == 0) break;       // the top: this slice started at row 0
        w = dv::SlideWindowUp(w);
        if (16 < slices) break;        // loop guard, as above
      }
      // And the entering chunk of the ONE slide is the contiguous head slice:
      // initial [26, 526), one slide up -> [0, 500): entering [0, 26). (24 ->
      // 26 with item 242 R4: the fixture's collapsed observer row and the
      // run-break control under it.)
      const ThreadWindow w0 = dv::InitialWindow(total);
      const ThreadWindow w1 = dv::SlideWindowUp(w0);
      bool chunkOk = w0.start == 26 && w1.start == 0 && w1.end == 500;
      for (std::size_t i = w1.start; i < w0.start && chunkOk; ++i)
        if (!dv::WindowCovers(w1, i) || dv::WindowCovers(w0, i)) chunkOk = false;
      // The initial window [26,526) renders 500 rows; one slide up renders
      // [0,500) — 500 more. 1000 rows checked across 2 slices.
      const bool ok = seamOk && slices == 2 && rowsChecked == 1000 && chunkOk;
      lines.push_back(std::format(
          L"  T8 window seam       : {} — stressed 526-row world: {} slices walked, "
          L"{} rows match the full plan in order and shape (seam header/run-pos "
          L"from full-history rules); one slide enters contiguous [0,26) {}",
          ok ? L"PASS" : L"FAIL", slices, rowsChecked, chunkOk ? L"clean" : L"BROKEN"));
    }

    // 4. THRESHOLDS. The near-edge rule is 1.5 viewports of remaining
    //    distance, INCLUSIVE — probed at the boundary on both sides rather
    //    than sampled, the same shape as DemoLayoutCheck's f(t) && !f(t-eps).
    //
    //    MUTATION CAUGHT: `<` for `<=` fails the exact-boundary probe; a 2.0
    //    or 1.0 multiplier fails the mid probes. Zero viewport is probed too:
    //    it must behave as "only the exact edge", not divide-by-anything.
    {
      const double vp = 700.0;
      const bool topAt = dv::NearTopOfLoaded(1050.0, vp);       // exactly 1.5x
      const bool topBeyond = !dv::NearTopOfLoaded(1050.1, vp);
      const bool topInside = dv::NearTopOfLoaded(0.0, vp);
      const bool footAt = dv::NearFootOfLoaded(18950.0, 20000.0, vp);  // 1050 left
      const bool footBeyond = !dv::NearFootOfLoaded(18949.9, 20000.0, vp);
      const bool footInside = dv::NearFootOfLoaded(20000.0, 20000.0, vp);
      const bool zeroVp = dv::NearTopOfLoaded(0.0, 0.0) && !dv::NearTopOfLoaded(0.1, 0.0) &&
                          dv::NearFootOfLoaded(5.0, 5.0, 0.0) &&
                          !dv::NearFootOfLoaded(4.9, 5.0, 0.0);
      const bool ok = topAt && topBeyond && topInside && footAt && footBeyond &&
                      footInside && zeroVp && dv::kWindowEdgeViewports == 1.5;
      lines.push_back(std::format(
          L"  T8 window threshold  : {} — 1.5 viewports, inclusive: top "
          L"1050.0/700 {} beyond {} | foot 1050 remaining {} beyond {} | zero "
          L"viewport {}",
          ok ? L"PASS" : L"FAIL", topAt, topBeyond, footAt, footBeyond, zeroVp));
    }

    // 5. OFFSET DELTAS over synthetic extents. The property, stated so a sign
    //    flip fails it: after the slide, the anchor row sits at the SAME
    //    viewport position as before — anchorAfter - newOffset ==
    //    anchorBefore - oldOffset — for BOTH directions of travel (a prepend
    //    pushes anchors down; a head trim pulls them up). The clamp is walked
    //    at both ends.
    //
    //    MUTATION CAUGHT: subtracting the delta instead of adding it flips the
    //    invariant on every probe; dropping the clamp fails the negative probe
    //    (a head trim near the top would ask ScrollViewer for a negative
    //    offset, which clamps silently — the gate says so out loud instead).
    {
      bool offsetOk = true;
      const double kCases[][3] = {
          // oldOffset, anchorBefore, anchorAfter
          {100.0, 120.0, 2120.0},    // prepend ~2000dip above the viewport
          {2100.0, 2120.0, 120.0},   // the head trim undoing it
          {0.0, 30.0, 30.0},         // a slide that moved nothing
          {48.5, 60.25, 3200.75},    // fractional dips, no rounding anywhere
      };
      for (auto const& c : kCases) {
        const double nu = dv::OffsetAfterSlide(c[0], c[1], c[2]);
        if (std::fabs((c[2] - nu) - (c[1] - c[0])) > 1e-9) offsetOk = false;
      }
      const bool clampOk = dv::ClampScrollOffset(-1900.0, 20000.0) == 0.0 &&
                           dv::ClampScrollOffset(25000.0, 20000.0) == 20000.0 &&
                           dv::ClampScrollOffset(48.5, 20000.0) == 48.5;
      // The extent-delta identity for a PURE top insert (nothing trimmed):
      // the anchor's travel IS the extent's growth, so the two ways of
      // measuring a prepend agree. The view feeds exactly that: each mutation
      // (insert, trim) is measured as an EXTENT delta across its own layout
      // pass — per-row height sums lose UseLayoutRounding's pixel snapping.
      const bool identity = dv::OffsetAfterSlide(100.0, 120.0, 120.0 + 1985.0) ==
                            100.0 + (22000.0 - 20015.0);
      const bool ok = offsetOk && clampOk && identity;
      lines.push_back(std::format(
          L"  T8 window offset     : {} — anchor viewport position invariant over "
          L"{} cases (both directions, fractional), clamp [-, +, inside] {}, "
          L"extent-delta identity {}",
          ok ? L"PASS" : L"FAIL", std::size(kCases), clampOk, identity));
    }

    // 6. AMBIENT ARRIVALS, as bookkeeping. A row arriving at the world's foot
    //    renders exactly when the window covers the foot; deep in history it
    //    must NOT materialize (no tree change) and must still be re-covered as
    //    the window slides home. Walked as a scenario, not probed pointwise:
    //    three arrivals while deep, then the slide chain home.
    //
    //    MUTATION CAUGHT: rendering when deep (after != window, render=true on
    //    the very first probe), or trimming MORE than the growth at the foot
    //    (the window shrinks below 500), or a published window that stops
    //    short of the grown foot (the slide-closure probe — SlideWindowDown
    //    then still has room to move).
    {
      bool ambientOk = true;
      // Deep: window [300,500) of 2024. Arrival at 2024 must change NOTHING.
      ThreadWindow deep{300, 500};
      const auto p0 = dv::PlanAmbientAppend(deep, 2024);
      if (p0.render || p0.after.start != 300 || p0.after.end != 500) ambientOk = false;
      if (dv::WindowCovers(p0.after, 2024)) ambientOk = false;
      // At the foot: window [1524,2024). The arrival renders, the window
      // grows to cover it, and the head trims back to the cap.
      const auto p1 = dv::PlanAmbientAppend(ThreadWindow{1524, 2024}, 2024);
      if (!p1.render || p1.after.start != 1525 || p1.after.end != 2025) ambientOk = false;
      if (!dv::WindowCovers(p1.after, 2024) || dv::WindowCovers(p1.after, 1524))
        ambientOk = false;
      if (dv::WindowRowCount(p1.after) != 500) ambientOk = false;
      // SLIDE-CLOSED, the pure half of the animfix2 defect-A ordering fix.
      // AppendThreadRow publishes p1.after BEFORE the head trim's layout pass
      // precisely so that the synchronous ViewChanged that pass fires finds
      // NO slide owed: MaybeSlideWindow's foot branch tests window.end <
      // total, and the published window's end IS the grown world's size
      // (2025), so the branch is dead on arrival and no nested slide can
      // re-materialize the arriving row. Asserted as arithmetic here: end
      // lands on the grown foot exactly, and SlideWindowDown — the slide the
      // trigger would have run — is a no-op on the published window. What the
      // gate CANNOT reach is the publish's ORDER against the trim's layout
      // pass; that half is winrt-side and capture-verified (handoff lesson
      // 5), and the AppendBusyGuard comment in ThreadView.cpp says so.
      const auto closed = dv::SlideWindowDown(p1.after, 2025);
      if (closed.start != p1.after.start || closed.end != p1.after.end) ambientOk = false;
      // Under the cap there is no window: an arrival renders and nothing
      // trims — today's small-conversation behaviour, stated as arithmetic.
      const auto p2 = dv::PlanAmbientAppend(ThreadWindow{0, 24}, 24);
      if (!p2.render || p2.after.start != 0 || p2.after.end != 25) ambientOk = false;
      // The scenario: three arrivals while deep, then slide home over the
      // grown world (2027 rows) — the final window must cover all three.
      std::size_t total = 2024;
      ThreadWindow w = deep;
      for (int k = 0; k < 3; ++k) {
        const auto p = dv::PlanAmbientAppend(w, total);
        if (p.render || p.after.start != w.start || p.after.end != w.end) ambientOk = false;
        w = p.after;
        ++total;  // the world grew; the window did not
      }
      std::size_t slides = 0;
      while (w.end < total) {
        w = dv::SlideWindowDown(w, total);
        if (256 < ++slides) break;
      }
      if (!(dv::WindowCovers(w, 2024) && dv::WindowCovers(w, 2025) &&
            dv::WindowCovers(w, 2026)))
        ambientOk = false;
      lines.push_back(std::format(
          L"  T8 window ambient    : {} — arrival renders exactly when the window "
          L"covers the foot (deep: no tree change; at foot: grow + head trim to "
          L"500; under cap: always renders); the published window is "
          L"slide-closed against the grown world; 3 arrivals while deep "
          L"re-covered after {} slides home",
          ambientOk ? L"PASS" : L"FAIL", slides));
    }

    // 7. REFRESH PLACEMENT. Re-setting the SAME conversation (autoplay's
    //    delivery-advance path) must not yank a deep reader: the window is
    //    re-clamped AROUND its start (the world grows at the foot only, so
    //    start still names the same row), never re-based at the foot, and the
    //    pin stays off. A reader at the foot gets the pre-window behaviour:
    //    re-based at the newest rows, pin lands. Under the cap the refresh is
    //    indistinguishable from before this wave.
    //
    //    MUTATION CAUGHT: re-basing a deep refresh at the foot (the first
    //    probe fails on BOTH fields), or pinToFoot leaking onto the deep path.
    //    The probe windows are all exactly 500 rows — the only size an ACTIVE
    //    window can be, since every slide trims back to the cap.
    {
      bool refreshOk = true;
      // Deep, world grew by 2: start 300 still names the same row.
      const auto r0 = dv::PlanRefreshWindow(ThreadWindow{300, 800}, 2026, false);
      if (r0.pinToFoot || r0.window.start != 300 || r0.window.end != 800)
        refreshOk = false;
      // Deep, window nearly home: clamps around start, caps at 500.
      const auto r1 = dv::PlanRefreshWindow(ThreadWindow{1424, 1924}, 2026, false);
      if (r1.pinToFoot || r1.window.start != 1424 || r1.window.end != 1924)
        refreshOk = false;
      // At the foot: re-based at the newest rows, pin on.
      const auto r2 = dv::PlanRefreshWindow(ThreadWindow{1524, 2024}, 2026, true);
      if (!r2.pinToFoot || r2.window.start != 1526 || r2.window.end != 2026)
        refreshOk = false;
      // Under the cap: the whole conversation, pinned — the pre-window path.
      const auto r3 = dv::PlanRefreshWindow(ThreadWindow{0, 300}, 301, false);
      if (!r3.pinToFoot || r3.window.start != 0 || r3.window.end != 301)
        refreshOk = false;
      // The boundary itself: exactly 500 after the refresh is still under it.
      const auto r4 = dv::PlanRefreshWindow(ThreadWindow{0, 499}, 500, false);
      if (!r4.pinToFoot || r4.window.start != 0 || r4.window.end != 500)
        refreshOk = false;
      lines.push_back(std::format(
          L"  T8 window refresh    : {} — same-conversation refresh: deep keeps "
          L"start and pin off (world +2 rows), near-home clamps at the cap, at "
          L"foot re-bases + pins, under-cap and exactly-500 are the pre-window "
          L"path",
          refreshOk ? L"PASS" : L"FAIL"));
    }
  }

  // ---- T9: progressive window hydration --------------------------------------
  //
  //  The open/switch path renders only the viewport-covering INITIAL SET
  //  synchronously and fills the rest of the 500-row window in background
  //  beats. Every decision in that is pure arithmetic in Views/ThreadLayout —
  //  sizing from the viewport and the plan's row shapes, the beat partition,
  //  and the coalescing rules between the fill and a scroll-triggered slide.
  //
  //  WHAT THESE LINES CANNOT SEE, said plainly: the DispatcherQueue scheduling
  //  (Low priority TryEnqueue), the view-side `keepOffset < 0` clause that
  //  excludes the position-preserving refresh from hydration, and the
  //  extent-delta offset correction the beats perform — all winrt-side in
  //  ThreadView.cpp, capture-verified. What is asserted here is the
  //  arithmetic they spend.
  {
    namespace dv = urmsg::views;
    namespace dd = urmsg::demo;
    using dv::ThreadWindow;

    // A stressed 524-row world (the T8 seam gate's pattern: built LOCALLY,
    // DemoWorld.cpp's bytes untouched) whose window is exactly 500 rows.
    dd::World sw = dd::GetWorld();
    dd::PrependStressHistory(sw, 500);
    dd::Conversation const& sc = sw.conversations.front();
    const auto splan = dv::PlanThreadRows(sc);
    const ThreadWindow swindow = dv::InitialWindow(sc.rows.size());  // [24,524)

    // 1. INITIAL-SET SIZING. The formula: walk back from the window's foot
    //    accumulating EstimatedRowDip until 2.0 viewport heights are covered,
    //    clamped into [min(32, windowRows), windowRows]. Asserted as
    //    PROPERTIES over probes (the T4 lesson: recomputing the loop here
    //    would agree with a broken loop):
    //      BOUNDS     min(32,windowRows) <= K <= windowRows at every probe
    //      COVERAGE   estExtent(K newest) >= 2*vp, unless K == windowRows
    //      MONOTONE   K non-decreasing in viewport height
    //      FALLBACK   vp=0 answers EXACTLY what the 800-dip fallback answers
    //      DIRECTION  a hand-built plan whose foot rows are TALLER than its
    //                 head rows: the count from the foot differs from the
    //                 count a head-walk would give, and K must be the foot's
    //      FLOOR/CAP  tiny viewport -> exactly 32; a viewport taller than the
    //                 window's whole estimated extent -> exactly windowRows
    //      MINIMALITY with floor and cap not binding: estExtent(K-1) < cover
    //
    //    MUTATIONS CAUGHT: halving the cover multiplier (1.0) fails DIRECTION
    //    (the probe then yields 16, floored to 32, not 44); dropping the
    //    floor fails FLOOR; walking from the head fails DIRECTION; a 600-dip
    //    fallback value fails nothing here by itself — the fallback's VALUE
    //    is a judgement call, and what is gated is that 0 is not a
    //    degenerate answer.
    {
      bool sizingOk = true;
      const double vps[] = {0.0, 1.0, 300.0, 700.0, 1400.0, 5000.0, 100000.0};
      std::size_t prevK = 0;
      std::vector<std::size_t> ks;
      for (double vp : vps) {
        const std::size_t k = dv::InitialViewportRows(vp, splan, swindow);
        ks.push_back(k);
        if (k < 32 || k > 500) sizingOk = false;  // BOUNDS (windowRows == 500)
        if (k < prevK) sizingOk = false;          // MONOTONE
        prevK = k;
        // COVERAGE: the estimated extent of the newest k rows must reach the
        // cover target unless the whole window was taken.
        if (k != 500) {
          const double effVp = vp > 0.0 ? vp : dv::kHydrateFallbackViewportDip;
          double est = 0.0;
          for (std::size_t i = swindow.end - k; i < swindow.end; ++i)
            est += dv::EstimatedRowDip(splan[i].shape);
          if (est + 1e-9 < dv::kHydrateViewportCover * effVp) sizingOk = false;
        }
      }
      // FALLBACK: 0 is the unrealized-tree case and must be the fallback, not
      // a degenerate answer.
      const bool fallbackOk =
          ks[0] == dv::InitialViewportRows(dv::kHydrateFallbackViewportDip, splan, swindow);
      // DIRECTION / FLOOR / CAP / MINIMALITY on a hand-built plan: rows
      // [0,90) are 36-dip system lines, rows [90,100) are 80-dip permanent
      // records, window is the whole thing. At vp=1000 the cover is 2000 dip:
      // from the FOOT that is 10 records (800) + 34 lines (1224) = 2024, so
      // K = 44; a head-walk would give ceil(2000/36) = 56. The floor never
      // binds at 44, the cap never binds under 100.
      std::vector<dv::ThreadRowPlan> hand(100);
      for (std::size_t i = 0; i < 100; ++i)
        hand[i].shape = i < 90 ? dv::ThreadRowShape::SystemLine
                               : dv::ThreadRowShape::SystemPermanentRecord;
      const ThreadWindow hw{0, 100};
      const std::size_t dir = dv::InitialViewportRows(1000.0, hand, hw);
      const bool directionOk = (dir == 44);  // the foot's answer, not the head's 56
      // MINIMALITY at the same probe: one row fewer must NOT cover.
      bool minimalOk = false;
      {
        double est = 0.0;
        for (std::size_t i = 100 - dir; i < 100; ++i) est += dv::EstimatedRowDip(hand[i].shape);
        double estLess = 0.0;
        for (std::size_t i = 100 - (dir - 1); i < 100; ++i)
          estLess += dv::EstimatedRowDip(hand[i].shape);
        minimalOk = (est >= 2000.0) && (estLess < 2000.0);
      }
      const bool floorOk = dv::InitialViewportRows(60.0, hand, hw) == 32;
      const bool capOk = dv::InitialViewportRows(100000.0, hand, hw) == 100;
      const bool ok = sizingOk && fallbackOk && directionOk && minimalOk && floorOk && capOk;
      lines.push_back(std::format(
          L"  T9 hydrate sizing    : {} — initial set = newest K rows covering 2.0 "
          L"viewports of estimated shape dip, clamped [32, window]: 524-row plan "
          L"K at vp 0/1/300/700/1400/5000/100000 = {}/{}/{}/{}/{}/{}/{}; fallback "
          L"{}; foot-vs-head probe K=44 (head would say 56) {}; floor 32 {}, cap "
          L"window {}, minimal {}",
          ok ? L"PASS" : L"FAIL", ks[0], ks[1], ks[2], ks[3], ks[4], ks[5], ks[6],
          fallbackOk ? L"ok" : L"BROKEN", directionOk ? L"ok" : L"BROKEN",
          floorOk ? L"ok" : L"BROKEN", capOk ? L"ok" : L"BROKEN",
          minimalOk ? L"ok" : L"BROKEN"));
    }

    // 2. BEAT PARTITION. The fill walks from the initial set's top (cursor)
    //    down to the initial window's start (target) in
    //    kHydrateFillBeatRows-sized slices (18 — the frame budget; the
    //    constant's comment in Views/ThreadLayout.h carries the derivation).
    //    THE TILING IS THE ASSERTION: every index in [target, cursor) is
    //    covered by exactly one beat — no gaps, no dupes, no overrun past the
    //    target (an overrun would overshoot into history the window never
    //    asked for and, past the cap, trim the FOOT — the yank the beat clamp
    //    exists to avoid). Beat count, the exact landing on the target, and
    //    idempotence below it are asserted against recomputation.
    //
    //    MUTATIONS CAUGHT: a dropped target clamp (the PARTIAL probe overruns
    //    8 rows — 468 divides 18 exactly, so the main walk alone would miss
    //    it), a beat that starts from cursor+1 (the dupe check fails), and a
    //    clamp of target-1 (the partial probe overruns below it).
    {
      const std::size_t k = dv::InitialViewportRows(800.0, splan, swindow);
      const std::size_t target = swindow.start;         // 24
      const std::size_t open = swindow.end - k;         // the initial cursor
      std::vector<bool> tiled(sc.rows.size(), false);
      std::size_t beats = 0, coveredRows = 0;
      bool beatsOk = true;
      std::size_t cursor = open;
      while (dv::HydrateFillActive(cursor, target)) {
        const auto b = dv::PlanHydrateBeat(cursor, target, 3, 3);
        if (!b.run) { beatsOk = false; break; }
        const std::size_t wantAdd = (std::min)(dv::kHydrateFillBeatRows, cursor - target);
        if (cursor - b.newStart != wantAdd) beatsOk = false;
        if (b.newStart < target) beatsOk = false;  // overrun past the target
        for (std::size_t i = b.newStart; i < cursor; ++i) {
          if (tiled[i]) beatsOk = false;  // a dupe
          tiled[i] = true;
          ++coveredRows;
        }
        cursor = b.newStart;
        ++beats;
        if (b.reschedule != dv::HydrateFillActive(cursor, target)) beatsOk = false;
        if (256 < beats) break;  // loop guard, as in the T8 slide walk
      }
      const std::size_t wantBeats =
          (open - target + dv::kHydrateFillBeatRows - 1) / dv::kHydrateFillBeatRows;
      if (cursor != target || beats != wantBeats || coveredRows != open - target)
        beatsOk = false;
      for (std::size_t i = target; i < open; ++i)
        if (!tiled[i]) beatsOk = false;  // a gap
      // The PARTIAL last beat, probed directly: 468 = 26*18 exactly, so the
      // walk above NEVER exercises the target clamp (the 100-row slice did it
      // incidentally — 468 = 4*100 + 68). A beat 10 rows above the target
      // must add exactly those 10 and land ON it; a dropped min-clamp
      // overruns 8 rows into history the window never asked for.
      const auto partial = dv::PlanHydrateBeat(target + 10, target, 3, 3);
      if (!partial.run || partial.newStart != target || partial.reschedule) beatsOk = false;
      // Idempotent AT the target and silent BELOW it (a scroll slide that
      // jumped past mid-fill must not owe a negative-size beat).
      const auto atEnd = dv::PlanHydrateBeat(target, target, 3, 3);
      const auto pastEnd = dv::PlanHydrateBeat(target > 10 ? target - 10 : 0, target, 3, 3);
      const bool endsOk = !atEnd.run && !atEnd.reschedule && !pastEnd.run &&
                          !pastEnd.reschedule &&
                          !dv::HydrateFillActive(target, target) &&
                          !dv::HydrateFillActive(target - 10, target);
      lines.push_back(std::format(
          L"  T9 hydrate beats     : {} — 524-row window, K={} at 800 dip: fill "
          L"[{},{}) in {} beats of <=18 ({} rows tiled exactly once, landed on "
          L"the target exactly), reschedule matches fill-active, partial last "
          L"beat clamps to the target, idempotent at and below it {}",
          beatsOk && endsOk ? L"PASS" : L"FAIL", k, target, open, beats, coveredRows,
          endsOk ? L"ok" : L"BROKEN"));
    }

    // 3. COALESCING, as scripted scenarios over the pure arithmetic — the
    //    rules the view's beats and slides BOTH spend, so neither can
    //    double-materialize the other's rows:
    //
    //    A. fill mid-way + scroll prepend: two beats land, then a slide takes
    //       its 100 rows from the LIVE cursor, then the fill resumes from the
    //       slide's new start. Every index in [target, open) must be covered
    //       exactly once across BOTH mechanisms. A beat that cached its next
    //       range at schedule time (instead of recomputing from the live
    //       window.start) re-covers the slide's rows HERE and fails.
    //    B. a slide jumping PAST the target ends the fill: cursor < target
    //       means the slide already covered the remainder; the beat must not
    //       run, and must not reschedule.
    //    C. the switch guard: a beat carrying a stale generation neither runs
    //       nor reschedules — the dead conversation's chain ends this turn —
    //       while the new conversation's own generation runs normally.
    //
    //    MUTATION CAUGHT for A: PlanHydrateBeat answered from a cursor the
    //    caller snapshotted — the dupe check fires. For C: `>` for `!=`
    //    passes every probe here (beat < current is never scheduled), so the
    //    stale case is probed in BOTH directions (7-after-8 stale, 8-after-7
    //    stale).
    {
      bool coalesceOk = true;
      // The gate-2 geometry, recomputed rather than copied: the 524-row
      // window's start as target, and the cursor the 800-dip sizing yields.
      const std::size_t target = swindow.start;
      const std::size_t open = swindow.end - dv::InitialViewportRows(800.0, splan, swindow);
      std::vector<bool> covered(sc.rows.size(), false);
      auto mark = [&](std::size_t lo, std::size_t hi) {
        for (std::size_t i = lo; i < hi; ++i) {
          if (covered[i]) coalesceOk = false;  // double-materialized
          covered[i] = true;
        }
      };
      // A: two beats, a slide, the fill to completion. The scenario needs
      // working room — two beats and a slide must all fit above the target —
      // and that dependency on the sizing constants is stated outright rather
      // than failing mysteriously inside the script.
      const bool room = open >= target + 400;
      if (!room) coalesceOk = false;
      std::size_t cursor = open;
      ThreadWindow win{open, sc.rows.size()};
      for (int step = 0; step < 2; ++step) {
        const auto b = dv::PlanHydrateBeat(cursor, target, 7, 7);
        if (!b.run) coalesceOk = false;
        mark(b.newStart, cursor);
        cursor = b.newStart;
        win.start = cursor;
      }
      win = dv::SlideWindowUp(win);  // the scroll prepend takes the next 100
      if (win.start != cursor - 100) coalesceOk = false;
      mark(win.start, cursor);
      cursor = win.start;  // the fill resumes from the LIVE cursor
      std::size_t steps = 0;
      while (dv::HydrateFillActive(cursor, target)) {
        const auto b = dv::PlanHydrateBeat(cursor, target, 7, 7);
        if (!b.run) { coalesceOk = false; break; }
        mark(b.newStart, cursor);
        cursor = b.newStart;
        if (256 < ++steps) { coalesceOk = false; break; }  // a stuck beat must not hang the gate
      }
      for (std::size_t i = target; i < open; ++i)
        if (!covered[i]) coalesceOk = false;  // the fill forgot a range
      // B: the slide jumps past the target. SlideWindowUp({target+36, 524})
      // has only target+36 rows above it, lands at 0, and the fill must stop
      // on BOTH the exact landing and the overshoot.
      const ThreadWindow jump{target + 36, sc.rows.size()};
      const ThreadWindow jumped = dv::SlideWindowUp(jump);
      if (jumped.start != 0) coalesceOk = false;
      const auto afterJump = dv::PlanHydrateBeat(jumped.start, target, 7, 7);
      if (afterJump.run || afterJump.reschedule) coalesceOk = false;
      const auto exactEnd = dv::PlanHydrateBeat(target, target, 7, 7);
      if (exactEnd.run || exactEnd.reschedule) coalesceOk = false;
      // C: the switch guard, both directions of staleness.
      const auto stale = dv::PlanHydrateBeat(open - 100, target, 7, 8);
      const auto staleBack = dv::PlanHydrateBeat(open - 100, target, 8, 7);
      const auto live = dv::PlanHydrateBeat(open - 100, target, 8, 8);
      if (stale.run || stale.reschedule || staleBack.run || staleBack.reschedule)
        coalesceOk = false;
      if (!live.run || !live.reschedule ||
          live.newStart != open - 100 - dv::kHydrateFillBeatRows)
        coalesceOk = false;
      lines.push_back(std::format(
          L"  T9 hydrate coalesce  : {} — fill + scroll-prepend share one cursor: "
          L"2 beats, a 100-row slide, fill resumes — [{},{}) covered exactly once "
          L"across both; slide past the target ends the fill; a stale-generation "
          L"beat dies unrescheduled (both directions), the live one runs",
          coalesceOk ? L"PASS" : L"FAIL", target, open));
    }

    // 4. THE UNDER-500 NO-OP. Every real fixture conversation is under the
    //    cap; for them the initial set must be the WHOLE window and no beat
    //    may exist — the open is pixel-identical to the pre-hydration path.
    //    Probed at 300/500/501 with locally built worlds (276/476/477
    //    synthetic rows over the 24-row fixture): the boundary sits BETWEEN
    //    500 and 501 and both sides are stated, not sampled.
    //
    //    The composition asserted here is the view's own:
    //      hydrate = WindowActive(total) && keepOffset < 0   (ThreadView.cpp)
    //      initial = hydrate ? InitialViewportRows(...) : WindowRowCount(...)
    //    The keepOffset clause is the half the gate cannot reach (it excludes
    //    the position-preserving refresh — view state, no pure probe); what
    //    IS asserted is the WindowActive half, the one with pixel-identity
    //    consequences.
    {
      bool noopOk = true;
      for (int synth : {276, 476, 477}) {  // 300 / 500 / 501 rows total
        dd::World w = dd::GetWorld();
        dd::PrependStressHistory(w, synth);
        dd::Conversation const& c = w.conversations.front();
        const std::size_t total = c.rows.size();
        const auto plan = dv::PlanThreadRows(c);
        const ThreadWindow win = dv::InitialWindow(total);
        // The view's composition, restated: under the cap the sizing function
        // is never the one that decides — the window is taken WHOLE.
        const std::size_t k = dv::WindowActive(total)
                                  ? dv::InitialViewportRows(800.0, plan, win)
                                  : dv::WindowRowCount(win);
        const std::size_t cursor = win.end - k;
        const bool fills = dv::HydrateFillActive(cursor, win.start);
        if (total <= 500) {
          // Whole window synchronously, cursor lands ON the target, no beat.
          if (k != total || cursor != win.start || fills) noopOk = false;
        } else {
          // Over the cap by ONE row: the fill exists and walks exactly the
          // rows the initial set did not take, landing on the target.
          if (k >= dv::WindowRowCount(win) || !fills) noopOk = false;
          const auto b = dv::PlanHydrateBeat(cursor, win.start, 1, 1);
          if (!b.run ||
              cursor - b.newStart != (std::min)(dv::kHydrateFillBeatRows, cursor - win.start) ||
              !b.reschedule)
            noopOk = false;
          std::size_t cur = b.newStart;
          std::size_t guard = 0;
          while (dv::HydrateFillActive(cur, win.start)) {
            cur = dv::PlanHydrateBeat(cur, win.start, 1, 1).newStart;
            if (256 < ++guard) { noopOk = false; break; }
          }
          if (cur != win.start) noopOk = false;
        }
      }
      lines.push_back(std::format(
          L"  T9 hydrate noop      : {} — 300/500-row worlds: initial set IS the "
          L"whole window, zero beats (pixel-identical default path); 501 rows: "
          L"initial 32 + 18-row beats tiling [1,469) to the target",
          noopOk ? L"PASS" : L"FAIL"));
    }

    // 5. OPEN VS REFRESH — who gets the entrance, and what the fill's first
    //    beat pays for it (the animfix2 wave's defect-B pure halves). The
    //    decision the view spends is ShouldRunEntrance(openConvId, nextConvId):
    //    TRUE on a conversation switch and on the first open (openConvId ""),
    //    FALSE on re-setting the conversation already open — autoplay's
    //    delivery-advance refresh, which must re-render the foot WITHOUT
    //    replaying the open stagger over bubbles the reader is watching.
    //    OpenStaggerTailMs is the first-beat delay on a true open: the last
    //    staggered bubble begins at (kMaxStaggerSteps - 1) * kStaggerMs and
    //    runs kBaseMs, so their sum is the entrance's tail. The recompute
    //    below pins the TOKENS' current values (5*40 + 250 = 450) on purpose:
    //    a motion-token change moves the fill's delay with it, and this line
    //    going red is what makes that coupling a decision rather than an
    //    accident.
    //
    //    WHAT THIS GATE CANNOT SEE: that the view actually spends either
    //    answer (the `runEntrance` gate in SetThreadConversation's render
    //    loop and the QueueHydrateBeat delay are winrt-side) — capture-
    //    verified in .verify-animfix2. MUTATIONS CAUGHT: == for != (every
    //    probe inverts), a first-open answer of false, a tail built from the
    //    wrong token (kFastMs for kBaseMs breaks the derivation check).
    {
      const bool switchRuns = dv::ShouldRunEntrance(L"conv-a", L"conv-b");
      const bool firstOpenRuns = dv::ShouldRunEntrance(L"", L"conv-a");
      const bool refreshSkips = !dv::ShouldRunEntrance(L"conv-a", L"conv-a");
      const bool emptyPairSkips = !dv::ShouldRunEntrance(L"", L"");
      const int64_t tail = dv::OpenStaggerTailMs();
      const bool tailOk = tail == 5 * 40 + 250;
      const bool ok =
          switchRuns && firstOpenRuns && refreshSkips && emptyPairSkips && tailOk;
      lines.push_back(std::format(
          L"  T9 entrance gate     : {} — open stagger on a true open/switch "
          L"only: switch {}, first open {}, same-conversation refresh skipped "
          L"{}, empty pair skipped {}; fill's first beat waits out the "
          L"entrance tail, {} ms = (kMaxStaggerSteps-1)*kStaggerMs + kBaseMs "
          L"{}",
          ok ? L"PASS" : L"FAIL", switchRuns ? L"ok" : L"BROKEN",
          firstOpenRuns ? L"ok" : L"BROKEN", refreshSkips ? L"ok" : L"BROKEN",
          emptyPairSkips ? L"ok" : L"BROKEN", tail, tailOk ? L"ok" : L"BROKEN"));
    }

    // 5b. THE WHOLE-THREAD ENTRANCE'S PURE HALF — the container timeline table
    //     the view's RunThreadEntrance spends on a true open/switch (the same
    //     ShouldRunEntrance gate as 5). TWO timelines with motion on (fade +
    //     a kDist8 rise), ZERO with it off — the empty half is the gate, the
    //     bubble table's rule. The rise is pinned against the TOKEN
    //     (urnw::motion::kDist8), not a restated literal, so a token change
    //     turns this line red rather than drifting the entrance. Per-entry
    //     field checks, not a census (the T4 lesson): path, from/to, begin,
    //     and both flags on each of the two.
    //
    //     WHAT THIS GATE CANNOT SEE: that the view targets the SCROLLER, runs
    //     the board only under runEntrance, or lands the final pose — those
    //     are winrt-side; the .verify-switchentrance captures cover them.
    //     MUTATIONS CAUGHT: a dropped TranslateY timeline (count), a rise
    //     re-typed to kDist4 (the token pin), a begin offset off 0, a flag
    //     flipped on either entry, the animate=false table left non-empty.
    {
      const auto on = dv::ThreadEntranceTimelines(true);
      const auto off = dv::ThreadEntranceTimelines(false);
      const bool countOk = dv::ThreadEntranceTimelineCount(true) == 2 &&
                           dv::ThreadEntranceTimelineCount(false) == 0;
      bool fieldsOk = on.size() == 2 && off.empty();
      if (fieldsOk) {
        const dv::TimelineSpec& fade = on[0];
        const dv::TimelineSpec& rise = on[1];
        fieldsOk =
            std::wstring{fade.path} == L"Opacity" && fade.from == 0.0 && fade.to == 1.0 &&
            fade.beginMs == 0 && !fade.autoReverse && !fade.forever &&
            std::wstring{rise.path} ==
                L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)" &&
            rise.from == urnw::motion::kDist8 && rise.to == 0.0 && rise.beginMs == 0 &&
            !rise.autoReverse && !rise.forever;
      }
      const bool ok = countOk && fieldsOk;
      lines.push_back(std::format(
          L"  SE thread entrance : {} — whole-thread container entrance on a "
          L"true open/switch: {} timelines with motion on ({}), {} with it off "
          L"({}); fade 0->1 {}, rise {:.0f} dip -> 0 (the kDist8 token) {}",
          ok ? L"PASS" : L"FAIL", on.size(), countOk ? L"count ok" : L"COUNT BROKEN",
          off.size(), off.empty() ? L"off ok" : L"OFF BROKEN",
          (on.size() == 2 && std::wstring{on[0].path} == L"Opacity") ? L"ok" : L"BROKEN",
          (on.size() == 2 ? on[1].from : -1.0), fieldsOk ? L"ok" : L"BROKEN"));
    }
  }

  // GUARDED, and the guard is not a nicety. CollectDiagnostics() is NOT the
  // --diagnose path: it runs on EVERY launch, before winrt::init_apartment and
  // before WantsDiagnose(), inside the try/catch in wWinMain whose failure path
  // shows "URmessage could not start" and returns 1 with NO WINDOW AT ALL
  // (main.cpp:161-177). These two lines build the whole demo world. Behind the
  // guard, a defect in DemoWorld costs the --diagnose run a line; in front of
  // it, the same defect costs every launch its window.
  if (WantsDiagnose()) {
    lines.push_back(urmsg::views::InspectRailFieldsProbe());
    lines.push_back(urmsg::views::InspectRailDeviceProbe());
    lines.push_back(urmsg::views::RailRowFitProbe());
  }

  // ---- demo stress: the --demo-stress=N generator ---------------------------
  //
  //  THE EXTENSION POINT, and the placement is the load-bearing part. Every
  //  world gate above — I10's fingerprint first among them — runs on the
  //  UN-stressed world, because the synthetic history is prepended HERE, at
  //  the end of CollectDiagnostics, never before. I10 therefore stays green
  //  with AND without the switch, and the fixture file keeps its bytes. The
  //  mutation itself is the wave-9 pattern: rows pushed into
  //  MutableWorld().conversations.front().rows at runtime. CollectDiagnostics
  //  runs on EVERY launch (the guard comment above), so extending here — once,
  //  before any window exists — is what makes --demo=thread/--demo=inspect
  //  open the stressed thread without touching MainWindow at all. Without the
  //  switch nothing is touched: the default --demo path and the shipping path
  //  never reach PrependStressHistory.
  {
    namespace dd = urmsg::demo;
    const dd::DemoOptions o = dd::ParseDemoOptions();
    if (o.stressRows <= 0) {
      // Switch ABSENT: the generator is proven over a locally built 500-row
      // sample instead — the T4/T5 synthetic-fixture pattern, because the
      // world this process is about to show must stay byte-identical.
      dd::World a = dd::GetWorld();
      dd::World b = dd::GetWorld();
      const std::size_t base = a.conversations.front().rows.size();
      const int addedA = dd::PrependStressHistory(a, 500);
      const int addedB = dd::PrependStressHistory(b, 500);

      // Determinism: two independent builds must agree field for field on
      // every synthetic row. A broad per-row comparison, not a census — the
      // lesson of T4's shape gate.
      std::size_t compared = 0, mismatched = 0;
      if (addedA == 500 && addedB == 500) {
        auto const& ra = a.conversations.front().rows;
        auto const& rb = b.conversations.front().rows;
        for (std::size_t i = 0; i < 500 && i < ra.size() && i < rb.size(); ++i) {
          ++compared;
          auto const& x = ra[i];
          auto const& y = rb[i];
          if (!(x.kind == y.kind && x.id == y.id && x.senderName == y.senderName &&
                x.senderKey == y.senderKey && x.body == y.body &&
                x.timeLabel == y.timeLabel && x.outgoing == y.outgoing &&
                x.state == y.state && x.systemText == y.systemText &&
                x.permanentRecord == y.permanentRecord &&
                x.inspect.epoch == y.inspect.epoch &&
                x.inspect.senderLeafIndex == y.inspect.senderLeafIndex &&
                x.inspect.sentAtLabel == y.inspect.sentAtLabel &&
                x.inspect.deliveredTo.size() == y.inspect.deliveredTo.size() &&
                x.inspect.readBy.size() == y.inspect.readBy.size()))
            ++mismatched;
        }
      }

      // Row-shape validity over the stressed conversation: the planner
      // against expectations recomputed HERE (shapes from kinds, runPos and
      // sender headers from the rules in Demo/ThreadLayout.h), plus the T1
      // separator audit — the same shape of proof T4/T7 run on the fixture.
      std::size_t planned = 0, wrongShape = 0, headerDisagree = 0, runWrong = 0;
      std::size_t incoming = 0, outgoing = 0, badTime = 0;
      std::size_t maxRun = 0, singletonRuns = 0;
      if (addedA == 500) {
        auto const& c = a.conversations.front();
        const bool group = (c.kind == dd::ConversationKind::Group);
        std::size_t at = 0;
        for (auto const& p : urmsg::views::PlanThreadRows(c)) {
          if (p.rowIndex != at++ || c.rows.size() <= p.rowIndex) continue;
          ++planned;
          auto const& r = c.rows[p.rowIndex];
          // Item 242 R4's collapsed row, recomputed off the word and not asked
          // of the planner's predicate: it draws no bubble, so it is a run
          // breaker in both directions and has no run position of its own.
          auto collapses = [](urmsg::demo::MessageRow const& x) {
            return x.kind == dd::RowKind::Message && x.senderRoleAtSend == dd::kRoleObserver;
          };
          urmsg::demo::MessageRow const* prev =
              (p.rowIndex > 0 && !collapses(c.rows[p.rowIndex - 1])) ? &c.rows[p.rowIndex - 1]
                                                                     : nullptr;
          urmsg::demo::MessageRow const* next =
              (p.rowIndex + 1 < c.rows.size() && !collapses(c.rows[p.rowIndex + 1]))
                  ? &c.rows[p.rowIndex + 1]
                  : nullptr;
          const auto wantShape =
              r.kind == dd::RowKind::DaySeparator
                  ? urmsg::views::ThreadRowShape::DaySeparator
              : r.kind == dd::RowKind::System
                  ? (r.permanentRecord
                         ? urmsg::views::ThreadRowShape::SystemPermanentRecord
                         : urmsg::views::ThreadRowShape::SystemLine)
              : collapses(r) ? urmsg::views::ThreadRowShape::HiddenObserverRecord
              : (r.outgoing ? urmsg::views::ThreadRowShape::OutgoingBubble
                            : urmsg::views::ThreadRowShape::IncomingBubble);
          if (p.shape != wantShape) ++wrongShape;
          if (r.kind == dd::RowKind::Message && !collapses(r)) {
            if (urmsg::views::ShowsSenderHeader(prev, r, group) != p.showSenderHeader)
              ++headerDisagree;
            if (urmsg::views::RunPosFor(prev, r, next) != p.runPos) ++runWrong;
            if (r.outgoing) ++outgoing; else ++incoming;
            // "14:22" shape on every synthetic message row.
            if (p.rowIndex < 500 &&
                (r.timeLabel.size() != 5 || r.timeLabel[2] != L':'))
              ++badTime;
          }
        }
        // Run lengths in the SYNTHETIC region: 2..4 by construction, with a
        // singleton possible only where a day boundary truncated a streak —
        // so at most one per separator.
        std::size_t run = 0;
        for (std::size_t i = 0; i < 500 && i < c.rows.size(); ++i) {
          auto const& r = c.rows[i];
          if (r.kind != dd::RowKind::Message) continue;
          ++run;
          const bool ends =
              i + 1 >= c.rows.size() || i + 1 >= 500 ||
              c.rows[i + 1].kind != dd::RowKind::Message ||
              c.rows[i + 1].outgoing != r.outgoing ||
              (!r.outgoing && c.rows[i + 1].senderKey != r.senderKey);
          if (ends) {
            if (run > maxRun) maxRun = run;
            if (run == 1) ++singletonRuns;
            run = 0;
          }
        }
      }
      urmsg::views::DaySeparatorAudit audit;
      if (addedA == 500)
        audit = urmsg::views::AuditDaySeparators(a.conversations.front().rows);
      // The seam: the last synthetic row is a MESSAGE (never a separator —
      // the fixture's own "Yesterday" label follows it), and the row after
      // the block is the fixture's old first row. Both pin the insertion
      // POINT: front, not back.
      const bool seamOk =
          addedA == 500 &&
          a.conversations.front().rows[499].kind == dd::RowKind::Message &&
          a.conversations.front().rows[500].id == L"c0-r0";
      const bool ok =
          addedA == 500 && addedB == 500 && compared == 500 && mismatched == 0 &&
          planned == base + 500 && wrongShape == 0 && headerDisagree == 0 &&
          runWrong == 0 && badTime == 0 && incoming > 0 && outgoing > 0 &&
          audit.separators >= 3 && audit.unlabelled == 0 && audit.adjacent == 0 &&
          audit.trailing == 0 && maxRun <= 4 && singletonRuns <= audit.separators &&
          seamOk;
      lines.push_back(std::format(
          L"  demo stress      : {} — switch absent; local 500-row sample: "
          L"determinism {}/{} rows identical, plan {}/{} rows ({} wrong shapes, "
          L"{} header and {} run-pos disagreements), {} in/{} out, {} separators "
          L"({} unlabelled {} adjacent {} trailing), longest run {}, seam {}",
          ok ? L"PASS" : L"FAIL", compared - mismatched, compared, planned,
          base + 500, wrongShape, headerDisagree, runWrong, incoming, outgoing,
          audit.separators, audit.unlabelled, audit.adjacent, audit.trailing,
          maxRun, seamOk ? L"clean" : L"BROKEN"));
    } else {
      // Switch PRESENT: extend the real world now — once, here, after every
      // gate above has seen the fixture as shipped. The position clauses
      // matter as much as the count: appended at the BACK the rows would be
      // NEWER than the fixture's foot, and "grew" alone would still read
      // true — so the synthetic prefix at rows[0] and the fixture's old
      // first row sitting exactly N deep are both asserted.
      const std::size_t before = dd::GetWorld().conversations.front().rows.size();
      const int added = dd::PrependStressHistory(dd::MutableWorld(), o.stressRows);
      auto const& rows = dd::GetWorld().conversations.front().rows;
      const bool position =
          static_cast<std::size_t>(added) < rows.size() &&
          rows.front().id.starts_with(L"c0-s") &&
          rows[static_cast<std::size_t>(added)].id == L"c0-r0";
      const bool ok = added == o.stressRows &&
                      rows.size() == before + static_cast<std::size_t>(added) && position;
      lines.push_back(std::format(
          L"  demo stress      : {} — --demo-stress={} grew conversation 0: "
          L"{} + {} = {} rows, synthetic at front {} (I10 above fingerprinted "
          L"the un-stressed world)",
          ok ? L"PASS" : L"FAIL", o.stressRows, before, added, rows.size(),
          position ? L"yes" : L"NO"));
    }
  }

  return lines;
}

void LogDiagnostics(const std::vector<std::wstring>& lines) {
  for (const auto& line : lines) LogInfo("startup: {}", Narrow(line));
}

std::wstring ResourceProbe() {
  // The app's own honest test. Localized() returns the key id itself when MRT
  // could not resolve it (Localization.cpp), so "app_name" coming back as
  // "app_name" means the pri did not load — the UI would render raw keys where
  // the product name and every label belong. A file-size check cannot tell that
  // apart from a working one.
  std::wstring value;
  try {
    value = Localized("app_name");
  } catch (...) {
    return L"  resources (mrt)  : FAILED — the resource loader threw";
  }
  if (value == L"app_name") {
    return L"  resources (mrt)  : NOT RESOLVING — the UI would render key ids "
           L"(\"app_name\") instead of text; resources.pri is missing, unindexed, "
           L"or not beside the exe";
  }
  return std::format(L"  resources (mrt)  : resolving (app_name -> \"{}\")", value);
}

std::wstring EffectiveSingleInstanceKey() {
  // The override is read here and not through StorageRoot(), because what must key the
  // registration is the thing the PERSON set: two different overrides are two installs even if
  // both resolve under one parent, and StorageRoot() has already folded the default in.
  wchar_t* raw = nullptr;
  size_t len = 0;
  std::wstring root;
  if (::_wdupenv_s(&raw, &len, L"URMESSAGE_APP_ROOT") == 0 && raw != nullptr) {
    root = raw;
    ::free(raw);
  }
  if (root.empty()) return ids::kSingleInstanceKey;

  // FNV-1a over the case-folded path. Not a cryptographic hash and it does not need to be: the
  // only property required is that two different roots give two different keys often enough to
  // stop a silent redirect, and a collision costs a developer one confusing launch rather than
  // anybody any safety. Case-folded because Windows paths are, so C:\X and c:\x are one install.
  uint64_t h = 1469598103934665603ULL;
  for (wchar_t c : root) {
    const wchar_t lower = (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    h ^= static_cast<uint64_t>(lower);
    h *= 1099511628211ULL;
  }
  return std::format(L"{}.{:016x}", ids::kSingleInstanceKey, h);
}

int WriteDiagnosticsToConsole(const std::vector<std::wstring>& lines) {
  std::wstring text(L"\r\n");
  for (const auto& line : lines) text += line + L"\r\n";

  // A GUI-subsystem process has no console of its own. Attaching to the
  // parent's is what makes `URmessage.exe --diagnose` from a terminal print
  // there; the shell has already returned its prompt (it does not wait on a GUI
  // app), so the output lands under it. Redirected or piped, the std handle is
  // already valid and this is a no-op.
  const bool attached = ::AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;

  // Attaching a console does not reliably give this process std handles — a
  // process launched without inheritable handles keeps its null ones — so open
  // the console's own device when they are missing. Getting this wrong prints
  // nothing at all, which is the failure mode this command exists to end.
  HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
  bool ownsHandle = false;
  if ((out == nullptr || out == INVALID_HANDLE_VALUE) && attached) {
    out = ::CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, 0, nullptr);
    ownsHandle = (out != INVALID_HANDLE_VALUE);
  }

  const bool wrote = WriteStdout(out, text);
  if (ownsHandle) ::CloseHandle(out);
  if (attached) ::FreeConsole();

  // Double-clicked from Explorer there is no console and no redirection, so the
  // box is the whole output.
  if (!wrote) {
    ::MessageBoxW(nullptr, text.c_str(), L"URmessage diagnostics",
                  MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
  }
  return 0;
}

bool WantsDiagnose() {
  int argc = 0;
  wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argv) return false;
  bool wants = false;
  for (int i = 1; i < argc && !wants; ++i) {
    const std::wstring_view arg = argv[i];
    wants = (arg == L"--diagnose" || arg == L"-diagnose" || arg == L"/diagnose" ||
             arg == L"diagnose");
  }
  ::LocalFree(argv);
  return wants;
}

void MarkLaunched() { g_launched.store(true); }
bool WasLaunched() { return g_launched.load(); }
bool HadVisibleFailure() { return g_failed.load(); }

void FailVisible(std::wstring_view cause, std::wstring_view detail) {
  g_failed.store(true);
  LogError("startup: FAILED: {}{}{}", Narrow(cause), detail.empty() ? "" : " | ",
           Narrow(detail));

  std::wstring body(cause);
  if (!detail.empty()) body += L"\n\n" + std::wstring(detail);
  const std::filesystem::path log = LogFilePath();
  if (!log.empty()) body += L"\n\nLog file:\n" + log.wstring();
  body += L"\n\nFor the full startup diagnostics, run in a terminal:\n"
          L"    URmessage.exe --diagnose";

  // MB_SETFOREGROUND + MB_TOPMOST: nothing else of this app is on screen, and a
  // failure box behind the window that launched us is a silent failure again.
  ::MessageBoxW(nullptr, body.c_str(), L"URmessage could not start",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

}  // namespace urnw
