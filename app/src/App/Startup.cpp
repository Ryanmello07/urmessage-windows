// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Startup.h"

#include <shellapi.h>  // CommandLineToArgvW

#include <algorithm>  // sort/unique, for the T5 badge-table assertion
#include <atomic>
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
#include "UrMotion.h"
#include "Views/ConversationRowModel.h"
#include "Views/DeveloperDump.h"
#include "Views/InspectRailFields.h"
#include "Views/StatusStripRules.h"
#include "Demo/DemoShellState.h"
#include "Demo/AdvancedMode.h"
#include "Demo/DemoAutoplay.h"
#include "Demo/DeveloperSwitches.h"
#include "Demo/DemoWorld.h"
#include "Demo/ThreadLayout.h"
#include "Views/ThreadLayout.h"
#include "Views/ThreadView.h"  // ShouldPinToBottom, for the scroll-pin line below
#include "Demo/DemoSwitches.h"

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
    for (auto const& m : c.members) {
      MixStr(h, m.id); MixStr(h, m.displayName); MixSeed(h, m.identityKey);
      MixU64(h, m.admin ? 1u : 0u);
      for (auto const& d : m.devices) mixDevice(d);
    }
    for (auto const& r : c.rows) {
      MixU64(h, static_cast<uint64_t>(r.kind)); MixStr(h, r.id);
      MixStr(h, r.senderName); MixSeed(h, r.senderKey); MixStr(h, r.body);
      MixStr(h, r.timeLabel); MixU64(h, r.outgoing ? 1u : 0u);
      MixU64(h, static_cast<uint64_t>(r.state)); MixStr(h, r.failureReason);
      MixStr(h, r.systemText); MixU64(h, r.permanentRecord ? 1u : 0u);
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
constexpr uint64_t kExpectedWorldFingerprint = 0x97B1C149D13010C3ull;

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
  lines.push_back(std::format(L"  single-inst key  : {}", ids::kSingleInstanceKey));
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
  // The demo surfaces' own invariants. One collector per surface, and every one
  // of them must be pure C++: this function runs before winrt::init_apartment
  // (main.cpp:168 vs :180), so a WinRT object built from here would die on the
  // line that constructs it, inside the code whose job is to explain deaths.
  // Unconditional on purpose -- these assert a pure function of seeded data, not
  // app state, so they are as true on a plain launch as under --demo.
  for (auto const& line : urmsg::views::CollectConversationListDiagnostics())
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
    int msgRows = 0, headers = 0, disagree = 0;
    for (auto const& c : world.conversations) {
      const bool group = (c.kind == demo::ConversationKind::Group);
      for (std::size_t i = 0; i < c.rows.size(); ++i) {
        auto const& row = c.rows[i];
        if (row.kind != demo::RowKind::Message) continue;
        ++msgRows;
        demo::MessageRow const* prev = (i > 0) ? &c.rows[i - 1] : nullptr;
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
      }
      return 0;
    };

    std::size_t convs = 0, rows = 0, planned = 0, outOfOrder = 0, wrongShape = 0;
    std::size_t seen[5] = {0, 0, 0, 0, 0};
    std::size_t system = 0, plain = 0, permanent = 0, emptyText = 0, mismatched = 0;
    std::size_t msgRows = 0, headerByRule = 0, headerByPlan = 0, headerDisagree = 0;

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
          urmsg::demo::MessageRow const* prev =
              (p.rowIndex > 0) ? &c.rows[p.rowIndex - 1] : nullptr;
          const bool byRule = urmsg::views::ShowsSenderHeader(prev, r, group);
          if (byRule) ++headerByRule;
          if (p.showSenderHeader) ++headerByPlan;
          if (byRule != p.showSenderHeader) ++headerDisagree;
        }
      }
    }
    // Every one of the five shapes must be worn by at least one row, or
    // "0 wrong shapes" would be a claim about branches no row ever took.
    std::size_t shapesUnused = 0;
    for (std::size_t k = 0; k < 5; ++k)
      if (seen[k] == 0) ++shapesUnused;

    lines.push_back(std::format(
        L"  T4 plan covers rows  : {} — planned {} of {} rows over {} "
        L"conversations, {} out of order",
        (planned == rows && 0 < rows && outOfOrder == 0) ? L"PASS" : L"FAIL",
        planned, rows, convs, outOfOrder));
    lines.push_back(std::format(
        L"  T4 row shapes        : {} — {} wrong of {} planned; in {} out {} "
        L"day {} sys {} perm {}; {} of 5 shapes unused",
        (wrongShape == 0 && shapesUnused == 0 && 0 < planned) ? L"PASS" : L"FAIL",
        wrongShape, planned, seen[0], seen[1], seen[2], seen[3], seen[4],
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
        L"{} disagree",
        (headerDisagree == 0 && 1 <= headerByRule && 1 <= headerByPlan) ? L"PASS"
                                                                       : L"FAIL",
        headerByRule, headerByPlan, msgRows, headerDisagree));
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
      auto cont = [](urmsg::demo::MessageRow const& a, urmsg::demo::MessageRow const& b) {
        if (a.kind != urmsg::demo::RowKind::Message ||
            b.kind != urmsg::demo::RowKind::Message)
          return false;
        if (a.outgoing != b.outgoing) return false;
        return a.outgoing || a.senderKey == b.senderKey;
      };
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
        RunPos anchor = RunPos::Single;
        bool isAnchor = true;
        if (r.id == L"c0-r1" || r.id == L"c0-r22")
          anchor = RunPos::First;
        else if (r.id == L"c0-r2" || r.id == L"c0-r23")
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
        L"First/Last/Single, c0-r22/r23 First/Last); synthetic fixture {}/{} message rows "
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
