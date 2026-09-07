// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Startup.h"

#include <shellapi.h>  // CommandLineToArgvW

#include <atomic>
#include <filesystem>
#include <format>
#include <optional>

#include "Ids.h"
#include "Identicon.h"
#include "Localization.h"
#include "Log.h"
#include "Paths.h"
#include "Strings.h"
#include "Demo/DemoWorld.h"
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
  for (auto& line : DemoWorldAssertions()) lines.push_back(std::move(line));
  {
    using namespace urmsg::demo;
    World const& w = GetWorld();
    std::vector<Seed> seeds;
    for (auto const& c : w.conversations) {
      seeds.push_back(c.identityKey);
      for (auto const& m : c.members) seeds.push_back(m.identityKey);
    }
    size_t stable = 0, symmetric = 0, inRange = 0;
    std::vector<std::array<bool, 25>> patterns;
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
      patterns.push_back(a.cells);
    }
    size_t distinct = 0;
    for (size_t i = 0; i < patterns.size(); ++i) {
      bool seen = false;
      for (size_t j = 0; j < i; ++j) if (patterns[j] == patterns[i]) seen = true;
      if (!seen) ++distinct;
    }
    const size_t n = seeds.size();
    lines.push_back(std::format(L"  identicons       : {} seeds from the demo world", n));
    lines.push_back(std::format(L"    P1  deterministic       {}  {}/{} seeds identical on a second call",
                                n && stable == n ? L"PASS" : L"FAIL", stable, n));
    lines.push_back(std::format(L"    P2  mirrored            {}  {}/{} patterns symmetric",
                                n && symmetric == n ? L"PASS" : L"FAIL", symmetric, n));
    lines.push_back(std::format(L"    P3  density in range    {}  {}/{} have 1..23 cells set",
                                n && inRange == n ? L"PASS" : L"FAIL", inRange, n));
    lines.push_back(std::format(L"    P4  distinguishable     {}  {} distinct of {} patterns",
                                distinct * 4 >= n * 3 ? L"PASS" : L"FAIL", distinct, n));
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
