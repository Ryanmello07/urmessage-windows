// SPDX-License-Identifier: MPL-2.0
//
// THE WORKER THAT REACHES THE REAL MESH. Read LiveMesh.h first for the two
// rules this file exists to keep (never block the UI thread; never two clients
// under one account).
//
// NO PCH, ON PURPOSE. Everything here is Win32 and the SDK's C ABI; this TU
// must not acquire a dependency on winrt/ or on XAML, because the thread it
// runs on is not the UI thread and must never touch either. App.vcxproj marks
// it PrecompiledHeader=NotUsing for that reason.
//
// THE SECRET. The credential is an operator-minted by_client_jwt. It is read
// from disk at run time and handed straight to the ABI. It is NEVER logged,
// never formatted into a message, never copied anywhere, and the only thing
// this file will say about it is how many bytes it was and which path it came
// from. If you add a log line here, do not add that one.

#include <windows.h>

#include "Live/LiveMesh.h"

#include <shellapi.h>  // CommandLineToArgvW
#include <bcrypt.h>    // BCryptGenRandom, for the group id
#pragma comment(lib, "bcrypt.lib")

#include <stdlib.h>  // _wdupenv_s, free
#include <string.h>  // _wcsicmp

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Live/ConversationRules.h"
#include "Live/HeldGroupRules.h"
#include "Live/RefusalCopy.h"
#include "Live/LiveWorld.h"
#include "Live/LocalNames.h"
#include "Log.h"
#include "AppPrefs.h"
#include "Paths.h"
#include "Strings.h"
#include "ThreadGuard.h"

// The vendored SDK C ABI. urnetwork_sdk.h carries urnet_release /
// urnet_free_string / urnet_live_handle_count; urnetwork_message.h carries the
// messaging exports (the roster and the two role verbs among them since item
// 242 R3). Both live in app/third_party/vendor-include, which
// Directory.Build.props:68 already puts on every project's include path.
//
// urnetwork_message.h SHIPS BECAUSE THIS TASK MADE IT SHIP. cgo/Makefile copied
// only urnetwork_sdk.h/.hpp/.def into the Windows output, so the messaging
// header — the one that declares everything below — reached no consumer at all.
extern "C" {
#include "urnetwork_message.h"
#include "urnetwork_sdk.h"
}

namespace urmsg::live {
namespace {

// ── the live target ───────────────────────────────────────────────────────────

// The operator host. The platform and api urls are DERIVED from it inside the
// library: env "" or "main" gives wss://connect.<host>. Passing env is how you
// reach a staging authority, and a hand-built "wss://connect." + host — which is
// what an earlier probe did — silently dials production. We pass NULL and let
// the library derive, then LOG WHAT IT ACTUALLY DIALLED, because dialling the
// wrong authority looks exactly like working.
constexpr const char* kHost = "beta-test.net";

// The alpha message server's client_id on that operator.
constexpr const char* kServerClientId = "01a0a199-5b06-5117-e86a-4bf5c01db15c";

// Named in the connect client so a server-side operator can tell this app's
// sessions from the probe's.
constexpr const char* kAppVersion = "urmessage-windows-alpha";

// ── the server's own endpoint (ledger 268) ────────────────────────────────────
//
// The alpha message server also listens on its own TLS endpoint, and this is the SHA-256 of
// that key's SubjectPublicKeyInfo. A session to anything presenting another key is refused
// before a single frame is written, so neither an exit provider nor any network between can
// stand in for the server. An IP address sends no TLS server name: an exit sees an address,
// a port and TLS records, and nothing else.
constexpr const char* kServerEndpoint = "wss://74.50.11.53/urmessage/v1";
constexpr const char* kServerEndpointPin =
    "868fd5ea59c78915b3e2feb6d4834fb8c74e3532591f6d5b605ef285f014787b";

constexpr const char* kRoutePrefKey = "route_through_urnetwork";

// URNETWORK and DIRECT reach that endpoint; PLATFORM is the operator path this app used
// before, kept for %URMESSAGE_ROUTE%=platform while the server still serves it.
enum class Route { Urnetwork, Direct, Platform };

const char* RouteName(Route route) {
  switch (route) {
    case Route::Urnetwork: return "urnetwork";
    case Route::Direct: return "direct";
    case Route::Platform: return "platform";
  }
  return "unknown";
}

// ── the reconnect window, which is the whole reason for the retry below ───────
//
// MEASURED ON THE DEPLOYED OPERATOR: a client_id that has just re-dialled is NOT
// ROUTED TO for about sixty seconds, and a request sent into that window is
// LOST — not queued, not retried underneath us. Waiting longer inside one call
// does not help; only SENDING AGAIN does. Every launch of this app after the
// first re-dials the same client_id, so every launch after the first meets this.
//
// TWO LAYERS OF RE-SENDING, AND THEY ARE NOT REDUNDANT:
//   * INSIDE one urnet_message_device_connect: the ABI sends a fresh Hello per
//     attempt until the budget is spent, firing OnConnectAttempt for each one
//     that was not answered. That is the inner ladder.
//   * AROUND it: the budget being spent is documented as "not yet, ask again"
//     and explicitly NOT a failure, so a spent budget here means loop, not stop.
//
// The budget is deliberately SHORTER than the window (25s against ~60s) rather
// than longer. A 90s budget would ride the window inside a single call and this
// app would learn nothing about how many sends it took; with 25s the outer loop
// runs two or three times on a cold start, and the count it prints is real
// evidence about the operator rather than a constant.
constexpr int64_t kConnectBudgetMs = 25000;
constexpr int64_t kConnectAttemptMs = 8000;

// 1, 2, 4, 8, 8, 8 — the ladder the live probe rides the window with. Seven
// connect calls in all (~206s worst case), against a window measured at ~60s
// and a probe that gets through in ~75s.
constexpr int64_t kBackoffMs[] = {1000, 2000, 4000, 8000, 8000, 8000};
constexpr int kMaxConnectCalls = 1 + static_cast<int>(sizeof(kBackoffMs) / sizeof(kBackoffMs[0]));

// ── the two-party handshake, which is what makes a group OPEN ─────────────────
//
// THIS APP DOES NOT FOUND A GROUP ANY MORE, and the reason is structural rather
// than a preference. Group.Open comes AFTER add_member; the alpha accepts
// exactly ONE add_member; and one credential means no second key package. A
// solo device therefore founds a group at epoch 0 that never opens and whose
// every send the server refuses — which is precisely what the previous shape of
// this file logged, by name, on every run.
//
// So the app is the JOINER. The second party is sdk/livepeer, a separate
// process under a SEPARATE ACCOUNT, and the seam between them is two files:
//
//     this app                                      sdk/livepeer
//     ────────                                      ────────────
//     device_key_package() -> kKeyPackageName  →    reads it, deletes it
//                                                   create_group, add_member, open
//     reads it, deletes it                     ←    invite.encode() -> kInviteName
//     device_join(invite)                           send / send_reply / react
//
// EACH SIDE DELETES WHAT IT CONSUMES. A key package is SINGLE USE — the private
// halves are taken destructively at the join — so a stale key package file read
// by a later peer run builds an invite this device CANNOT open, and the failure
// would land at the join rather than at the read. Deleting on consumption is
// what makes "the file is there" mean "the file is fresh".
//
// BOTH FILES ARE KEY MATERIAL and neither may ever enter a repository. They are
// written beside the credential, whose directory is by construction outside
// one, and the invite is deleted the moment it has been used.
constexpr const wchar_t* kKeyPackageName = L"app.keypackage";
constexpr const wchar_t* kInviteName = L"app.invite";

// How long the app waits for the peer to answer with an invite. Generous: the
// peer has to ride the same ~60s reconnect window this app just rode, and the
// operator does not stagger them.
constexpr int64_t kInviteWaitMs = 300000;
constexpr int64_t kInvitePollMs = 2000;

// ── the fetch loop ────────────────────────────────────────────────────────────
// THERE IS NO RECEIVE PUSH — the transport is a poll and nothing arrives on its
// own — so a conversation that updates is a loop and not a subscription. The
// loop runs for the life of the process; the worker is detached and the OS
// reclaims it at exit.
constexpr int64_t kFetchPollMs = 3000;

// With a subscription the server PUSHES (spec B 4.3.5) the moment the group gains a record, and
// the poll is only the safety net under a push that was lost. Ledger 269.
constexpr int64_t kPushedPollMs = 15000;

// ── small helpers ─────────────────────────────────────────────────────────────

// Take ownership of an out_error, free it, and answer it as a std::string. The
// ABI mallocs these and the caller frees with urnet_free_string; leaving one
// behind is a leak the handle-count check cannot see.
std::string TakeError(char** err) {
  if (err == nullptr || *err == nullptr) return {};
  std::string text(*err);
  urnet_free_string(*err);
  *err = nullptr;
  return text;
}

// Take ownership of a returned char*, free it, answer a std::string.
std::string TakeString(char* s) {
  if (s == nullptr) return {};
  std::string text(s);
  urnet_free_string(s);
  return text;
}

std::string Utf8Path(const std::filesystem::path& p) { return urnw::Narrow(p.wstring()); }

std::wstring EnvVar(const wchar_t* name) {
  wchar_t buf[32767];
  const DWORD n = ::GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
  if (n == 0 || n >= std::size(buf)) return {};
  return std::wstring(buf, buf + n);
}

bool HasCommandLineFlag(const wchar_t* flag) {
  int argc = 0;
  wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (argv == nullptr) return false;
  bool found = false;
  for (int i = 1; i < argc && !found; ++i) found = (::_wcsicmp(argv[i], flag) == 0);
  ::LocalFree(argv);
  return found;
}

// Where the credential lives. The PATH is the configuration; the token never is.
// Overridable so a second identity can be pointed at without an edit — but an
// override names a FILE, and a file is all this ever accepts.
std::filesystem::path CredentialPath() {
  const std::wstring override = EnvVar(L"URMESSAGE_LIVE_JWT");
  if (!override.empty()) return std::filesystem::path(override);
  wchar_t* raw = nullptr;
  size_t len = 0;
  std::filesystem::path local;
  if (::_wdupenv_s(&raw, &len, L"LOCALAPPDATA") == 0 && raw != nullptr) {
    local = raw;
    ::free(raw);
  }
  return local / L"URmessage" / L"dev" / L"user1.jwt";
}

// Where the two handshake files live: BESIDE THE CREDENTIAL. That directory is
// by construction outside any repository — a credential in one would be the
// error, not the shortcut — so deriving from it is what keeps key material out
// of a working tree without a second rule anybody has to remember.
std::filesystem::path HandshakeDir() {
  const std::wstring override = EnvVar(L"URMESSAGE_LIVE_HANDSHAKE_DIR");
  if (!override.empty()) return std::filesystem::path(override);
  // AND IT FOLLOWS %URMESSAGE_APP_ROOT% WHEN THAT IS SET, which the credential's parent does not.
  //
  // MEASURED, BY DOING IT: two instances launched with two different app roots and no explicit
  // %URMESSAGE_LIVE_JWT% both fell back to the DEFAULT credential and therefore to the default
  // handshake directory -- so both read one account's credential and both wrote app.keypackage to
  // one path, each clobbering the other. The roots looked isolated and the IDENTITY was not. The
  // state directories were genuinely separate, which is the only reason that was a clobbered file
  // rather than two devices at one MLS leaf.
  //
  // The whole point of the root override is "this is a separate install", and a handshake file is
  // part of an install. The credential still needs %URMESSAGE_LIVE_JWT% named explicitly, and that
  // is right: a credential is the one thing that must never be guessed at from a directory layout.
  const std::wstring root = EnvVar(L"URMESSAGE_APP_ROOT");
  if (!root.empty()) return std::filesystem::path(root) / L"dev";
  return CredentialPath().parent_path();
}

// Read a whole file as octets. Answers false when it is absent or empty; an
// empty file is a writer that has not finished, not a file.
bool ReadOctets(const std::filesystem::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return !out.empty();
}

// Write octets through a temp file and a rename, so a peer polling the path
// never reads a half-written key package. The temp sits in the destination's
// OWN directory: a cross-volume rename is not atomic and on Windows is not a
// rename at all.
bool WriteOctets(const std::filesystem::path& path, const uint8_t* data, size_t len) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const std::filesystem::path temp = std::filesystem::path(path).concat(L".partial");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    if (!out) return false;
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    // A rename over an existing file is allowed by std::filesystem, so a
    // failure here is a real one. Take the partial file with us.
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

// Read the credential and trim surrounding whitespace. THE RETURN VALUE IS A
// SECRET: it goes to urnet_message_client_new and nowhere else.
bool ReadCredential(const std::filesystem::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto first = raw.find_first_not_of(" \t\r\n");
  const auto last = raw.find_last_not_of(" \t\r\n");
  if (first == std::string::npos) return false;
  out = raw.substr(first, last - first + 1);
  return !out.empty();
}

// ── the connect-attempt callback ──────────────────────────────────────────────

struct Hellos {
  std::atomic<int> unanswered{0};
};

// Fires on the thread INSIDE urnet_message_device_connect, once per Hello that
// was not answered. It is a progress report from a blocking call, not an async
// completion, and the contract forbids calling back into the device from here —
// so this only counts and logs.
//
// NOTHING MAY ESCAPE THIS FUNCTION. It is called from Go, across the C ABI, and
// an exception unwinding into that frame is undefined behaviour long before it
// could reach anything that would catch it.
void OnConnectAttempt(void* user_data, int32_t attempt, int64_t elapsed_ms, int64_t backoff_ms,
                      const char* err) noexcept {
  try {
    if (auto* hellos = static_cast<Hellos*>(user_data)) hellos->unanswered.fetch_add(1);
    urnw::LogInfo(
        "live: reconnecting — Hello attempt {} was not answered after {} ms; waiting {} ms ({})",
        attempt, elapsed_ms, backoff_ms, err != nullptr ? err : "no reason given");
  } catch (...) {
    // A logging failure must not become a crash on a Go-owned thread.
  }
}

// ── the session ───────────────────────────────────────────────────────────────

// Every handle one session owns, closed in the order the ABI requires:
// stop-then-release, innermost first. The client is closed LAST because the
// transport and the device are built over it and neither closes it.
// %URMESSAGE_ROUTE% for this launch, else the Settings switch.
// A DEVELOPMENT SWITCH, AND THE ONLY ONE IN THIS FILE THAT DESTROYS ANYTHING.
// URMESSAGE_DEV_LEAVE_AFTER_MS queues ONE plain leave (QueueLeave) that many milliseconds into a
// group's fetch loop, so the leave road -- the worker's half, the empty world, and the way back to
// the join dialog -- can be driven end to end without a synthesized click (ledger 273). IT IS
// HONOURED ONLY UNDER AN OVERRIDDEN APP ROOT (URMESSAGE_APP_ROOT): a launch over the real per-user
// state ignores it and says so, so it can never erase a person's own group. 0 means off.
int64_t DevLeaveAfterMs() {
  const std::wstring raw = EnvVar(L"URMESSAGE_DEV_LEAVE_AFTER_MS");
  if (raw.empty()) return 0;
  if (EnvVar(L"URMESSAGE_APP_ROOT").empty()) {
    urnw::LogWarn("live: URMESSAGE_DEV_LEAVE_AFTER_MS is IGNORED: it is honoured only under an "
                  "overridden URMESSAGE_APP_ROOT, and this launch is over the real per-user state");
    return 0;
  }
  // AND NOT THE REAL ROOT SPELLED OUT: an override that names %LOCALAPPDATA%\\URmessage\\app is the
  // per-user state all the same.
  {
    wchar_t* raw_local = nullptr;
    size_t len = 0;
    std::filesystem::path real;
    if (::_wdupenv_s(&raw_local, &len, L"LOCALAPPDATA") == 0 && raw_local != nullptr) {
      real = std::filesystem::path(raw_local) / L"URmessage" / L"app";
      ::free(raw_local);
    }
    std::error_code ec;
    // the root IN USE, and not the variable: StorageRoot falls back to the real root when the
    // override is longer than MAX_PATH (re-check NIT)
    if (!real.empty() && std::filesystem::equivalent(urnw::StorageRoot(), real, ec)) {
      urnw::LogWarn("live: URMESSAGE_DEV_LEAVE_AFTER_MS is IGNORED: URMESSAGE_APP_ROOT names the real "
                    "per-user state");
      return 0;
    }
  }
  const long long value = std::wcstoll(raw.c_str(), nullptr, 10);
  return value > 0 ? static_cast<int64_t>(value) : 0;
}

Route ChosenRoute() {
  const std::wstring forced = EnvVar(L"URMESSAGE_ROUTE");
  if (!forced.empty()) {
    if (_wcsicmp(forced.c_str(), L"urnetwork") == 0) return Route::Urnetwork;
    if (_wcsicmp(forced.c_str(), L"direct") == 0) return Route::Direct;
    if (_wcsicmp(forced.c_str(), L"platform") == 0) return Route::Platform;
    urnw::LogWarn("live: URMESSAGE_ROUTE is not urnetwork, direct or platform; the Settings switch decides");
  }
  return RouteThroughUrnetwork() ? Route::Urnetwork : Route::Direct;
}

struct Session {
  uint64_t ctx = 0;
  uint64_t client = 0;
  // a route client (urnet_message_route_client_new) rather than the operator client; the two
  // are closed by different exports
  bool routeClient = false;
  uint64_t transport = 0;
  uint64_t streamStore = 0;
  uint64_t reserver = 0;
  uint64_t stateStore = 0;
  uint64_t device = 0;
  // NO GROUP: each group this device holds is its own loop's (the conversation-list design 2.1).

  void Close() {
    char* err = nullptr;
    if (device != 0) {
      if (!urnet_message_device_close(device, &err)) {
        const std::string text = TakeError(&err);
        if (!text.empty()) urnw::LogWarn("live: device_close: {}", text);
      }
      urnet_release(device);
    }
    if (stateStore != 0) {
      if (!urnet_message_durable_state_store_close(stateStore, &err)) {
        const std::string text = TakeError(&err);
        if (!text.empty()) urnw::LogWarn("live: state_store_close: {}", text);
      }
      urnet_release(stateStore);
    }
    if (reserver != 0) urnet_release(reserver);
    if (streamStore != 0) {
      if (!urnet_message_stream_store_close(streamStore, &err)) {
        const std::string text = TakeError(&err);
        if (!text.empty()) urnw::LogWarn("live: stream_store_close: {}", text);
      }
      urnet_release(streamStore);
    }
    if (transport != 0) {
      urnet_message_transport_close(transport);
      urnet_release(transport);
    }
    if (client != 0) {
      if (routeClient) {
        urnet_message_route_client_close(client);
      } else {
        urnet_message_client_close(client);
      }
      urnet_release(client);
    }
    if (ctx != 0) {
      urnet_message_context_cancel(ctx);
      urnet_release(ctx);
    }
    *this = Session{};
  }
};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// WALL CLOCK, and a second function rather than a parameter on the one above: NowMs is steady_clock
// and is used for DURATIONS, where a clock that can step backwards over an NTP correction would
// print a negative elapsed. This one is the other kind of time — the instant a send was attempted,
// which is formatted as a clock face beside message timestamps the protocol carries in the same
// units (sent_at_ms, unix epoch milliseconds). Mixing the two would put a row at 03:14 on 1970.
int64_t WallClockMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// ── the send queue ────────────────────────────────────────────────────────────
//
// THE UI THREAD PUTS OCTETS IN; THE WORKER TAKES THEM OUT AND CALLS THE ABI. Nothing else crosses:
// the group handle, the context and every store stay owned by the worker, so there is no moment
// where two threads are inside the library over one group.
//
// A CONDITION VARIABLE RATHER THAN THE POLL INTERVAL. The fetch loop sleeps 3 s between fetches and
// a send dropped into that sleep would sit there for up to 3 s with the sender watching an empty
// composer. The loop therefore WAITS on this instead of sleeping, so a click wakes it at once and a
// quiet loop still ticks on its own timer.
// SIX VERBS, ONE QUEUE. Text and reply become rows of the outbox; react and unreact become
// entries of the reaction outbox on their target; set-role and transfer become entries of the
// role outbox on the member they name (item 242 R3). They share the queue because they share the
// constraint that put the queue here: each one is a round trip inside a single ABI call, and only
// the worker may hold the group handle.
enum class OutboundKind {
  Text, Reply, ReactAdd, ReactRemove, SetRole, TransferOwnership, RemoveMember, Delete, Leave,
  // AN ADD FROM A JOIN CODE (the conversation-list design 2.3): `body` holds the code as pasted,
  // and the outcome is the group's add state, not a row
  AddMember
};

struct Outbound {
  OutboundKind kind = OutboundKind::Text;
  std::string localId;      // this session's name for the attempt
  std::string body;         // utf-8 octets, exactly as the composer held them (Text, Reply)
  std::string replaces;     // a failed entry this one supersedes, or empty (Text, Reply)
  std::string targetHex;    // the message named: the parent (Reply) or the target (React*, Delete)
  std::vector<uint8_t> target;  // the same 32 octets, decoded once at the queue
  std::string emoji;        // RAW utf-8 (React*)
  std::string identityPub;  // the member named, as the roster spells it (SetRole, Transfer,
                            // RemoveMember), or the hand-over before a Leave ("" for none)
  std::string role;         // "admin" | "member" | "observer" (SetRole); "owner" (Transfer)
  // THE GROUP THIS WAS QUEUED FOR, by id, and its incarnation (the conversation-list design 2.2):
  // the worker does it to that group in that incarnation, and drops it otherwise.
  std::string groupIdHex;
  uint64_t incarnation = 0;
};

// ── the table: one slot per group this device holds (the conversation-list design 2.1) ─────────
//
// ONE LOOP PER GROUP, each with its own queue, its own wake and its own push flag, so a fetch that
// stalls in one conversation (S4 measured six one-minute stalls in 90 minutes on the URnetwork
// route) holds up no other conversation's sends. The handle is the loop's alone: no other thread
// touches it, which keeps the rule this file has always kept, one thread per group handle.
//
// LOCK ORDER: g_tableMutex, then LiveWorld's own (Publish). A slot's own mutex guards its queue and
// its push flag only, and is never held while g_tableMutex is taken.
struct GroupSlot {
  std::string gidHex;
  uint64_t incarnation = 0;
  int64_t adoptedAtMs = 0;  // wall clock: a group with no line yet is ordered by this
  uint64_t handle = 0;      // the loop's alone
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<Outbound> queue;  // guarded by mutex
  bool pushPending = false;    // guarded by mutex
  // The conversation this group's loop built last, for the world every loop publishes. Guarded by
  // g_tableMutex.
  urmsg::live::LiveConversation conversation;
  bool built = false;
  // A GROUP OF ONE: founded here, and nobody else in it yet. It is not open on the server, so its
  // loop fetches nothing until its first add opens it, and a create is refused while it waits
  // (ConversationRules.h CreateAction). Guarded by g_tableMutex.
  bool groupOfOne = false;
};

std::mutex g_tableMutex;
std::vector<std::shared_ptr<GroupSlot>> g_slots;  // guarded by g_tableMutex, in adoption order
// The session's facts, which every conversation of the world shares: the newest any loop read.
urmsg::live::LiveSession g_liveSession;           // guarded by g_tableMutex
// Signalled whenever a slot comes or goes; the session thread waits on it for an empty table.
std::condition_variable g_slotsChanged;
// A push for an id this device does not hold is logged once per id (the conversation-list design
// 2.1, the self group: a push never adopts anything).
std::set<std::string> g_unknownPushLogged;        // guarded by g_tableMutex
// THE GROUPS THIS DEVICE LEFT IN THIS SESSION. A reconciliation (ReconcileTable) that listed the
// device's groups just before a leave's forget must not take the group up again behind it; only a
// join or a create of that id, which a person asked for, takes it up (StartGroupLoop clears it).
std::set<std::string> g_leftGroups;               // guarded by g_tableMutex
// ONE SLOT LEAVES AT A TIME (RetireSlot): whether it is the last conversation, the neutral state that
// then goes first, and its removal are one step, so two loops leaving at once cannot both find
// another conversation still there and leave the window an empty world with no neutral state first.
std::mutex g_retireMutex;
// ── the gates (the conversation-list design 2.2, Live/ConversationRules.h) ──────────────────────
//
// ONE GATE PER GROUP THIS DEVICE HOLDS, written by the worker and read by the verbs on the UI thread,
// under one mutex. A gate says whether the server holds the group open (a commit may go), whether a
// leave of it is in flight (nothing else is taken), and the group's INCARNATION: the number this
// device gave it when it took it up. Every verb stamps its entry with both, and the worker does an
// entry only to the group it names and only in that incarnation, so a line typed, or a second leave
// confirmed, while a leave was in flight is never done to anything else. These replace the one send
// gate, the one "pass" and the one leave flag a device that held a single group had.
std::mutex g_gateMutex;
std::vector<urmsg::live::GroupGate> g_gates;  // guarded by g_gateMutex
uint64_t g_nextIncarnation = 1;               // guarded by g_gateMutex

urmsg::live::GroupGate* MutableGate(std::string const& gid) {
  for (auto& gate : g_gates)
    if (gate.groupIdHex == gid) return &gate;
  return nullptr;
}

// TAKE A GROUP UP: a fresh incarnation, closed and not leaving until the worker reads it open.
uint64_t AdoptGate(std::string const& gid) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  const uint64_t incarnation = g_nextIncarnation++;
  if (auto* gate = MutableGate(gid)) {
    *gate = urmsg::live::GroupGate{gid, incarnation, false, false};
  } else {
    g_gates.push_back(urmsg::live::GroupGate{gid, incarnation, false, false});
  }
  return incarnation;
}

void SetGateOpen(std::string const& gid, bool open) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  if (auto* gate = MutableGate(gid)) gate->open = open;
}

void SetGateLeaving(std::string const& gid, bool leaving) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  if (auto* gate = MutableGate(gid)) gate->leaving = leaving;
}

bool GateOpen(std::string const& gid) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  auto const* gate = urmsg::live::FindGate(g_gates, gid);
  return gate != nullptr && gate->open;
}

// A GROUP LEFT: its gate goes, and with it every entry stamped for it. No other gate moves.
void DropGate(std::string const& gid) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  g_gates.erase(std::remove_if(g_gates.begin(), g_gates.end(),
                               [&](urmsg::live::GroupGate const& gate) { return gate.groupIdHex == gid; }),
                g_gates.end());
}

void DropAllGates() {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  g_gates.clear();
}

std::vector<urmsg::live::GroupGate> GatesSnapshot() {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  return g_gates;
}

// THE ONE ADMISSION every verb makes: the gate's verdict, and the incarnation to stamp, in one look.
// A leave also marks the gate leaving in the same look, so two leaves of one group cannot both pass.
bool Admit(std::string const& gid, urmsg::live::ActionClass cls, uint64_t& incarnation,
           bool markLeaving = false) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  const urmsg::live::GateVerdict verdict = urmsg::live::GateAction(g_gates, gid, cls);
  if (verdict != urmsg::live::GateVerdict::Take) {
    urnw::LogWarn("live: an action for group {} was refused at its gate ({})",
                  gid.substr(0, (std::min)(gid.size(), size_t{16})),
                  urnw::Narrow(std::wstring(urmsg::live::GateVerdictName(verdict))));
    return false;
  }
  auto* gate = MutableGate(gid);
  incarnation = gate->incarnation;
  if (markLeaving) gate->leaving = true;
  return true;
}

std::atomic<uint64_t> g_nextLocalId{1};

// A group's whole queue, taken at once. Called on that group's loop only.
std::deque<Outbound> TakeSlotQueue(GroupSlot& slot) {
  std::lock_guard<std::mutex> lock(slot.mutex);
  std::deque<Outbound> taken;
  taken.swap(slot.queue);
  return taken;
}

// Sleep until this group has something to send, a push named it, or `ms` has passed.
void WaitSlot(GroupSlot& slot, int64_t ms) {
  std::unique_lock<std::mutex> lock(slot.mutex);
  slot.wake.wait_for(lock, std::chrono::milliseconds(ms),
                     [&slot] { return !slot.queue.empty() || slot.pushPending; });
  slot.pushPending = false;
}

std::shared_ptr<GroupSlot> FindSlot(std::string const& gid) {
  std::lock_guard<std::mutex> lock(g_tableMutex);
  for (auto const& slot : g_slots)
    if (slot->gidHex == gid) return slot;
  return nullptr;
}

size_t SlotCount() {
  std::lock_guard<std::mutex> lock(g_tableMutex);
  return g_slots.size();
}

bool IsGroupOfOne(GroupSlot& slot) {
  std::lock_guard<std::mutex> lock(g_tableMutex);
  return slot.groupOfOne;
}

void SetGroupOfOne(GroupSlot& slot, bool groupOfOne) {
  std::lock_guard<std::mutex> lock(g_tableMutex);
  slot.groupOfOne = groupOfOne;
}

// The table as the create rule reads it (ConversationRules.h CreateAction).
std::vector<urmsg::live::HeldForCreate> HeldForCreateSnapshot() {
  std::lock_guard<std::mutex> lock(g_tableMutex);
  std::vector<urmsg::live::HeldForCreate> held;
  held.reserve(g_slots.size());
  for (auto const& slot : g_slots) held.push_back({slot->gidHex, slot->groupOfOne});
  return held;
}

// THE WORLD OF EVERY CONVERSATION BUILT SO FAR, published. g_tableMutex is held by the caller.
void PublishTableLocked() {
  std::vector<urmsg::live::LiveConversation> conversations;
  conversations.reserve(g_slots.size());
  for (auto const& slot : g_slots)
    if (slot->built) conversations.push_back(slot->conversation);
  urmsg::live::Publish(urmsg::live::BuildWorld(g_liveSession, std::move(conversations)));
}

// ONE GROUP'S NEW CONVERSATION, into the table, and the world of all of them out. Called on that
// group's loop, off the group as it stands.
void PublishSlot(GroupSlot& slot, urmsg::live::LiveGroup const& live) {
  urmsg::live::LiveConversation conversation =
      urmsg::live::BuildLiveConversation(live, WallClockMs());
  std::lock_guard<std::mutex> lock(g_tableMutex);
  slot.conversation = std::move(conversation);
  slot.built = true;
  g_liveSession = urmsg::live::SessionOf(live);
  PublishTableLocked();
}

// A GROUP LEFT: its slot goes, and the world without it is published. Answers whether the table is
// now empty. The gate went first (DropGate), so nothing can be queued for it any more.
bool RemoveSlot(std::string const& gid) {
  bool empty = false;
  {
    std::lock_guard<std::mutex> lock(g_tableMutex);
    g_slots.erase(std::remove_if(g_slots.begin(), g_slots.end(),
                                 [&](std::shared_ptr<GroupSlot> const& slot) {
                                   return slot->gidHex == gid;
                                 }),
                  g_slots.end());
    empty = g_slots.empty();
    PublishTableLocked();
  }
  g_slotsChanged.notify_all();
  return empty;
}

// A PUSH, ROUTED BY ITS GROUP ID (the conversation-list design 2.1; ConversationRules.h RoutePush):
// a group this device holds wakes that group's loop, "" (the session was replaced) wakes every
// loop, and an id this device does not hold wakes none, adopts nothing, and is logged once.
void RoutePushToLoops(std::string const& pushed) {
  std::vector<std::shared_ptr<GroupSlot>> wake;
  {
    std::lock_guard<std::mutex> lock(g_tableMutex);
    std::vector<std::string> held;
    held.reserve(g_slots.size());
    for (auto const& slot : g_slots) held.push_back(slot->gidHex);
    const urmsg::live::PushRoute route = urmsg::live::RoutePush(held, pushed);
    if (route == urmsg::live::PushRoute::All) {
      wake = g_slots;
    } else if (route == urmsg::live::PushRoute::One) {
      const std::string id = urmsg::live::PushGroupKey(pushed);
      for (auto const& slot : g_slots)
        if (slot->gidHex == id) wake.push_back(slot);
    } else if (g_unknownPushLogged.insert(pushed).second) {
      urnw::LogWarn("live: a push named group {}, which this device does not hold; it wakes no "
                    "conversation and adopts nothing",
                    pushed.substr(0, (std::min)(pushed.size(), size_t{16})));
    }
  }
  for (auto const& slot : wake) {
    {
      std::lock_guard<std::mutex> lock(slot->mutex);
      slot->pushPending = true;
    }
    slot->wake.notify_all();
  }
}

// The push waiter: ONE PER DEVICE, for the life of the session, blocked in the library until the
// server announces a record, which it routes to the loop of the group it names. It owns nothing but
// the device handle it is given, and it is stopped and joined before that handle can be closed
// (its destructor). GUARDED, now that routing a push allocates: a throw is named in the log.
class PushWaiter {
 public:
  explicit PushWaiter(uint64_t device)
      : device_(device),
        thread_(urnw::StartGuardedThread("urmessage-live-push", [this] { Run(); })) {}
  ~PushWaiter() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
  }
  PushWaiter(const PushWaiter&) = delete;
  PushWaiter& operator=(const PushWaiter&) = delete;

 private:
  void Run() {
    while (!stop_.load()) {
      // a short wait, so a stop is noticed within a second
      char* groupHex = urnet_message_device_wait_push(device_, 1000);
      if (groupHex == nullptr) continue;
      const std::string pushed(groupHex);
      urnet_free_string(groupHex);
      RoutePushToLoops(pushed);
    }
  }
  const uint64_t device_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

// The reference is cgo/ctest/message_abi_test.c's hex_to_id: exactly 64 hex characters in, 32
// octets out, and any other width or any non-hex character is a refusal rather than a best effort.
// A message_id comes BACK from the ABI as this hex and goes IN as counted octets; this is the one
// place in the app that turns the one into the other.
bool FromHexId(const std::string& hex, std::vector<uint8_t>& out) {
  constexpr size_t kIdOctets = 32;
  if (hex.size() != kIdOctets * 2) return false;
  auto nibble = [](char c) -> int {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  out.assign(kIdOctets, 0);
  for (size_t at = 0; at < kIdOctets; ++at) {
    const int high = nibble(hex[at * 2]);
    const int low = nibble(hex[at * 2 + 1]);
    if (high < 0 || low < 0) return false;
    out[at] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

std::string ToHex(const uint8_t* data, int32_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(static_cast<size_t>(len) * 2);
  for (int32_t i = 0; i < len; ++i) {
    out.push_back(kHex[(data[i] >> 4) & 0xf]);
    out.push_back(kHex[data[i] & 0xf]);
  }
  return out;
}

// ── codes: the text a person can carry between two machines ───────────────────
//
// RFC 4648 base64 with padding and no line breaks, written out here rather than taken from
// Windows: CryptBinaryToStringW inserts CRLF unless asked not to, and the one thing that must not
// happen to a code is for a mail client to wrap it and a reader to paste back something that no
// longer decodes.
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string EncodeBase64(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    out += kB64[(v >> 6) & 63];
    out += kB64[v & 63];
  }
  if (i < len) {
    const bool two = (i + 1 < len);
    const uint32_t v = (uint32_t(data[i]) << 16) | (two ? (uint32_t(data[i + 1]) << 8) : 0);
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    out += two ? kB64[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

// Answers false on anything that is not a decodable code. WHITESPACE IS SKIPPED, not refused: a
// code that has been through an email client or a chat app arrives wrapped, and refusing it would
// blame the person for their mail client. Every other stray character IS refused, because a code
// that decodes to the wrong octets fails later as a checksum error that looks like corruption.
bool DecodeBase64(std::string_view text, std::vector<uint8_t>& out) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  out.clear();
  uint32_t acc = 0;
  int bits = 0;
  size_t pad = 0;
  for (char c : text) {
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
    if (c == '=') { pad += 1; continue; }
    if (pad != 0) return false;  // data after padding
    const int v = value(c);
    if (v < 0) return false;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
    }
  }
  if (pad > 2) return false;
  return !out.empty();
}

// ── the published onboarding state ────────────────────────────────────────────
std::mutex g_onboardMutex;
urmsg::live::OnboardPtr g_onboard;          // guarded by g_onboardMutex
std::string g_pastedInvite;                 // guarded by g_onboardMutex; consumed by the worker
// WHAT THE JOIN DIALOG SAYS FIRST after a leave whose erase did not finish (review H1), or after a
// restore that refused a group founded before invitations were sealed (Task 58), or "".
// Guarded by g_onboardMutex; written by the worker and consumed when the join road opens again.
std::string g_afterLeaveNote;
bool g_createGroupAsked = false;            // guarded by g_onboardMutex; consumed by the worker
bool g_openRoadAsked = false;               // guarded by g_onboardMutex; consumed by the join road
std::atomic<uint64_t> g_onboardGeneration{0};
// THE LEAVE OF A GROUP THAT IS NOT LIVE (Task 39). Offered: the state on screen carries the offer,
// set by every publication to what that publication says, so any later state withdraws it. Asked:
// the person pressed the button; guarded by g_onboardMutex and consumed by the worker.
std::atomic<bool> g_heldLeaveOffered{false};
bool g_heldLeaveAsked = false;
// "MAKE A NEW JOIN CODE" (R25 Q2), on the same terms as the leave above: offered by the state on
// screen, asked by the press, and consumed by the worker.
std::atomic<bool> g_newJoinCodeOffered{false};
bool g_newJoinCodeAsked = false;            // guarded by g_onboardMutex; consumed by the worker
// THIS DEVICE HOLDS A GROUP ON ITS DISK THAT DID NOT OPEN (R25 Q4, Q5): set when a restore, a join
// or a leave says so, cleared by a join and by a leave that left it. Every publication carries it,
// so no state in between says "not in a group yet" of this device. Guarded by g_onboardMutex.
bool g_holdsUnopenedGroup = false;
// WHICH "NOT READ" CLASS THE LAST RESTORE REPORTED (R28, R29): ErrGroupRecordNotReadNow's or
// ErrGroupReadUnproven's, or none. Set by the startup restore and by every restore a leave runs
// first; a restore that could not be tried because this device is not connected leaves it as it
// was (Live/RefusalCopy.h, NextRestoreClass). While it names a class, a held refusal says that
// class's row and offers no leave, because that failure names no group (R12) and a leave erases.
// Guarded by g_onboardMutex.
urmsg::live::ReadClass g_lastRestoreClass = urmsg::live::ReadClass::None;

void PublishOnboard(urmsg::live::OnboardStep step, std::string joinCode, std::string message,
                    bool offerLeaveHeldGroup = false, bool offerNewJoinCode = false,
                    std::string groupIdHex = {}) {
  auto next = std::make_shared<urmsg::live::OnboardState>();
  next->step = step;
  next->joinCode = std::move(joinCode);
  next->message = std::move(message);
  next->groupIdHex = std::move(groupIdHex);
  next->offerLeaveHeldGroup = offerLeaveHeldGroup;
  next->offerNewJoinCode = offerNewJoinCode;
  // THE TABLE AS IT STANDS, read before the onboarding lock (never the table's lock inside it)
  next->holdsConversation = SlotCount() > 0;
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    if (step == urmsg::live::OnboardStep::Joined) g_holdsUnopenedGroup = false;
    // "NOT IN A GROUP YET" COUNTS THE CONVERSATIONS HELD TOO (the conversation-list design A13)
    next->holdsGroup = g_holdsUnopenedGroup || next->holdsConversation;
    g_onboard = std::move(next);
    g_heldLeaveOffered.store(offerLeaveHeldGroup, std::memory_order_release);
    g_newJoinCodeOffered.store(offerNewJoinCode, std::memory_order_release);
  }
  g_onboardGeneration.fetch_add(1, std::memory_order_release);
  // THE SAME BELL THE WORLD RINGS. The window's beat re-reads the onboarding state along with
  // everything else, so a join code that has just been minted reaches the screen on the next beat
  // rather than on the next fetch.
  urmsg::live::NotifyPublished();
}

// ── the published add states, one per group (the conversation-list design 2.3, A8) ─────────────
std::mutex g_addMutex;
std::vector<urmsg::live::AddPtr> g_adds;  // guarded by g_addMutex; at most one per group
std::atomic<uint64_t> g_addGeneration{0};

// The add state of one group, replacing the one before it: an invitation is shown until the
// group's next add, and a group's add never shows under another group's.
void PublishAdd(std::string const& gid, urmsg::live::AddStep step, std::string message,
                std::string inviteCode = {}) {
  auto next = std::make_shared<urmsg::live::AddState>();
  next->groupIdHex = gid;
  next->step = step;
  next->message = std::move(message);
  next->inviteCode = std::move(inviteCode);
  {
    std::lock_guard<std::mutex> lock(g_addMutex);
    next->generation = g_addGeneration.load(std::memory_order_relaxed) + 1;
    g_adds.erase(std::remove_if(g_adds.begin(), g_adds.end(),
                                [&](urmsg::live::AddPtr const& a) { return a->groupIdHex == gid; }),
                 g_adds.end());
    g_adds.push_back(std::move(next));
    g_addGeneration.fetch_add(1, std::memory_order_release);
  }
  urmsg::live::NotifyPublished();
}

// ── the list's notice (the conversation-list design 2.5, A12) ──────────────────────────────────
std::mutex g_noticeMutex;
std::string g_restoreNotice;  // guarded by g_noticeMutex

// Set after every restore (ConversationRules.h NextListNotice). The window reads it when it builds
// the list, which every published world does, so a restore before the first world is drawn with it.
void SetRestoreNotice(std::string next) {
  bool changed = false;
  const bool empty = next.empty();
  {
    std::lock_guard<std::mutex> lock(g_noticeMutex);
    changed = next != g_restoreNotice;
    g_restoreNotice = std::move(next);
  }
  if (changed)
    urnw::LogInfo("live: the list's notice {}", empty ? "is cleared" : "names a group that did not restore");
}

// A GROUP LEFT TAKES ITS ADD STATE WITH IT, the invitation it may still hold included.
void DropAdd(std::string const& gid) {
  bool dropped = false;
  {
    std::lock_guard<std::mutex> lock(g_addMutex);
    const auto before = g_adds.size();
    g_adds.erase(std::remove_if(g_adds.begin(), g_adds.end(),
                                [&](urmsg::live::AddPtr const& a) { return a->groupIdHex == gid; }),
                 g_adds.end());
    dropped = g_adds.size() != before;
    if (dropped) g_addGeneration.fetch_add(1, std::memory_order_release);
  }
  if (dropped) urmsg::live::NotifyPublished();
}

// THE FOUNDER'S SENTENCE (R43 A13): the add panel's join-code box is above it, and a group of one
// cannot send until its first member is added.
constexpr const char* kFoundedSentence =
    "Your group is made, but it has only you in it and nothing can be sent yet. Ask somebody for "
    "their join code and paste it above - adding them is what opens the group.";

// A group's id as 64 hex, read off its handle, or "" when it will not say.
std::string GroupIdHexOf(uint64_t group) {
  uint8_t gid[32] = {};
  int32_t gidLen = static_cast<int32_t>(sizeof(gid));
  if (!urnet_message_group_id(group, gid, &gidLen)) return std::string();
  return ToHex(gid, gidLen);
}

// THE SYSTEM ENTROPY SOURCE, and it is BCryptGenRandom rather than anything in <random>. A group
// id is not a secret - the server indexes records by it - but it must not be GUESSABLE either, or
// anybody can ask the server whether a particular group has traffic. std::random_device is allowed
// by the standard to be a deterministic sequence and on some toolchains is; BCRYPT_USE_SYSTEM_
// PREFERRED_RNG is the one call on this platform that is documented to be neither.
bool RandomOctets(uint8_t* out, size_t len) {
  return ::BCryptGenRandom(nullptr, out, static_cast<ULONG>(len),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

// BRING SOMEBODY IN FROM THEIR JOIN CODE, on the worker, and publish the invitation to send back.
// Answers true when the group moved.
//
// THE TWO ADDS ARE ONE DECISION AND THE GROUP MAKES IT, not a counter here.
// urnet_message_group_add_member builds the FOUNDING commit and is refused once the group is
// open; urnet_message_group_add_member_and_publish is refused before it. Asking
// urnet_message_group_is_open is the only reading that cannot drift from the truth -- a count of
// how many people this session has added would be wrong after a restart, and wrong in the
// direction that refuses every later add with a sentence about the wrong call.
bool AddMemberFromCode(Session& s, uint64_t group, std::string const& gidHex,
                       const std::string& theirCode) {
  std::vector<uint8_t> keyPackage;
  if (!DecodeBase64(theirCode, keyPackage)) {
    PublishAdd(gidHex, urmsg::live::AddStep::Refused,
               "That join code could not be read. Ask for the whole code again - it is one "
               "long line with no spaces inside it.");
    urnw::LogWarn("live: a pasted join code was not decodable base64; refused");
    return false;
  }

  char* err = nullptr;
  const bool wasOpen = urnet_message_group_is_open(group);
  uint64_t invite = 0;
  if (wasOpen) {
    invite = urnet_message_group_add_member_and_publish(
        group, s.ctx, keyPackage.data(), static_cast<int32_t>(keyPackage.size()), &err);
  } else {
    invite = urnet_message_group_add_member(
        group, keyPackage.data(), static_cast<int32_t>(keyPackage.size()), &err);
  }
  if (invite == 0) {
    const std::string why = TakeError(&err);
    // THE ADDER'S THREE REFUSALS OF A JOIN CODE (Task 58), each in the person's words and each
    // made before anything was staged, so the group is where it was. Told apart by the library's
    // own sentence (Live/RefusalCopy.h); anything else is said as it was before.
    const urmsg::live::AddCopy row = urmsg::live::ClassifyAddCopy(why);
    urnw::LogError("live: {} refused the add ({}): {}",
                   wasOpen ? "add_member_and_publish" : "add_member", urmsg::live::AddCopyName(row),
                   why);
    PublishAdd(gidHex, urmsg::live::AddStep::Refused,
               row != urmsg::live::AddCopy::Other ? std::string(urmsg::live::AddCopySentence(row))
                                                  : "That person could not be added: " + why);
    return false;
  }

  // The invitation, buffer-out, then the handle goes.
  int32_t needed = 0;
  urnet_message_invite_encode(invite, nullptr, &needed, &err);
  TakeError(&err);
  std::string code;
  if (needed > 0) {
    std::vector<uint8_t> encoded(static_cast<size_t>(needed));
    int32_t capacity = needed;
    if (urnet_message_invite_encode(invite, encoded.data(), &capacity, &err)) {
      encoded.resize(static_cast<size_t>(capacity));
      code = EncodeBase64(encoded.data(), encoded.size());
      // ERASED THE MOMENT IT IS TEXT. The octets are the group's secrets in full and this buffer
      // is about to be freed into a heap this process keeps using.
      ::SecureZeroMemory(encoded.data(), encoded.size());
    }
  }
  const std::string encodeError = TakeError(&err);
  urnet_release(invite);

  if (code.empty()) {
    urnw::LogError("live: the add succeeded and the invitation would not encode: {}",
                   encodeError.empty() ? "no reason given" : encodeError);
    // THE ADD ALREADY HAPPENED, which is why this is not a plain refusal: the commit is on the
    // server and that person IS in the group's next epoch. They simply cannot be told how to
    // open it from here. Saying "could not be added" would be false and would invite a second
    // add of the same person.
    PublishAdd(gidHex, urmsg::live::AddStep::Refused,
               "They were added, but the invitation could not be written out, so there is "
               "nothing to send them. Ask them for a fresh join code and add them again.");
    return true;
  }

  // THE OPEN, and only on the founding road. add_member_and_publish has already published its
  // own commit; add_member has not, and group_open is what tells the server the group exists.
  if (!wasOpen) {
    if (!urnet_message_group_open(group, s.ctx, &err)) {
      const std::string why = TakeError(&err);
      // THE GROUP IS OPEN ON THE SERVER, and this device could not finish saving it: its creation
      // could not be recorded as finished (R34 item 3, R35 A8), or its record could not be persisted
      // (R37 A9). "Could not be published" would be false of either, so its own row says what
      // happened, and that the invitation was not shown. Every other refusal of the open is said as
      // it was before.
      const std::string_view row = urmsg::live::OpenRefusalCopy(why);
      urnw::LogError("live: group_open{}: {}",
                     row.empty() ? "" : " (the group is open on the server and this device could not finish saving it)",
                     why);
      PublishAdd(gidHex, urmsg::live::AddStep::Refused,
                 !row.empty() ? std::string(row)
                              : "They were added, but the group could not be published to the server: " + why);
      return false;
    }
  }

  urnw::LogInfo("live: *** MEMBER ADDED *** by {}; the group is at epoch {}, open: {}",
                wasOpen ? "add_member_and_publish" : "add_member + open",
                urnet_message_group_epoch(group),
                urnet_message_group_is_open(group) ? "yes" : "no");
  // WHAT THE INVITATION IS, the dialog's label says (Live/RefusalCopy.h, kAdderInviteLabel). Since
  // invite version 3 only the joiner's device can open it, so "whoever has it is in the group" is
  // no longer true, and it is no longer said.
  PublishAdd(gidHex, urmsg::live::AddStep::Invited, "Send this invitation to that person.", code);
  return true;
}

// ── an invitation refused over a group this device holds and cannot open (Task 39) ─────────────
//
// THE LIBRARY REFUSES AN INVITATION INTO A GROUP THIS DEVICE HOLDS (ErrGroupAlreadyHeld), and a
// group that did not restore, or whose record on the disk does not read, is held all the same: its
// record is on the disk. The remedy the refusal names is to leave it first. This app's own leave
// (LeaveGroup above) reads the group id off a live group, and such a group has no handle, so until
// this the app told a person to leave a group and gave them no way to.
//
// THE INVITATION THAT WAS REFUSED, KEPT FOR THE LEAVE AND FOR THE JOIN AFTER IT. Its octets are
// the group's secrets in full, exactly as the pasted code they came from is: kept in this worker's
// memory only, never written down and never logged. What is ERASED, with SecureZeroMemory, is this
// struct's copy when it is dropped, and each pass of the wait loop's own copy (`encoded`, by
// ErasedOnExit) when that pass ends. The file road's `previous` and the pasted base64 are not
// erased; they never were.
// The group id is the invitation's own first field (HeldGroupRules.h says why there is no bridge
// accessor for it).
struct HeldInvite {
  std::vector<uint8_t> octets;
  std::array<uint8_t, urmsg::live::kGroupIdOctets> groupId{};
  bool fromPaste = false;
  bool valid = false;

  void Drop() {
    if (!octets.empty()) ::SecureZeroMemory(octets.data(), octets.size());
    octets.clear();
    groupId.fill(0);
    fromPaste = false;
    valid = false;
  }
  ~HeldInvite() { Drop(); }
};

// One pass's copy of an invitation's octets, erased when the pass ends however it ends (review F5).
struct ErasedOnExit {
  std::vector<uint8_t>& octets;
  ~ErasedOnExit() {
    if (!octets.empty()) ::SecureZeroMemory(octets.data(), octets.size());
  }
};

enum class HeldLeave {
  Left,      // this device holds nothing under the id now: open the same invitation again
  CameBack,  // the restore brought the group back, and `cameBack` is it
  Offered,   // the group is still held; the state says why, and whether the leave is on offer
};

// THE RESTORE THE LEAVE RUNS FIRST: device_restore, and the group it brings back under the held
// id, if any. `back` is true with `group` set to it, for the caller to take up; every other group it
// answered is handed to a loop of its own (`adopt`, A10).
// `error` is the restore's own report, which says whether a file of a group could not be read just
// now (R28): the leave reads it before it decides anything.
struct HeldRestore {
  bool back = false;
  uint64_t group = 0;
  std::string error;
};

using AdoptFn = std::function<void(uint64_t, bool)>;
// The reconciliation (ReconcileTable), handed to the join road the same way.
using ReconcileFn = std::function<void()>;

HeldRestore RestoreHeldGroup(Session& s, HeldInvite const& held, AdoptFn const& adopt) {
  char* err = nullptr;
  const int32_t gidLen = static_cast<int32_t>(held.groupId.size());
  const uint64_t restored = urnet_message_device_restore(s.device, s.ctx, &err);
  HeldRestore out;
  out.error = TakeError(&err);
  const std::string& restoreError = out.error;
  const int32_t count = urnet_message_group_list_count(restored);
  if (!restoreError.empty())
    urnw::LogWarn("live: held leave: device_restore reported: {} (and answered {} group(s))",
                  restoreError, count);
  uint64_t back = 0;
  for (int32_t i = 0; i < count; ++i) {
    const uint64_t group = urnet_message_group_list_at(restored, i);
    if (group == 0) continue;
    uint8_t gid[urmsg::live::kGroupIdOctets] = {};
    int32_t len = static_cast<int32_t>(sizeof(gid));
    const bool same = back == 0 && urnet_message_group_id(group, gid, &len) && len == gidLen &&
                      std::memcmp(gid, held.groupId.data(), sizeof(gid)) == 0;
    if (same) {
      back = group;
    } else {
      // A GROUP THE LIBRARY NOW HOLDS OPEN: it gets a loop of its own, like every other group
      // handed over (the conversation-list design 2.1, A10). Released, it would be held by the
      // library and served by nobody. One already served keeps its loop (StartGroupLoop).
      adopt(group, false);
    }
  }
  if (restored != 0) urnet_release(restored);
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_lastRestoreClass = urmsg::live::NextRestoreClass(g_lastRestoreClass, restoreError);
  }
  // THE LIST'S NOTICE FOLLOWS EVERY RESTORE (A12), this one's too
  SetRestoreNotice(urmsg::live::NextListNotice(RestoreNotice(), restoreError, count));
  if (back == 0) return out;
  out.group = back;
  out.back = true;
  return out;
}

// THE LEAVE ITSELF, on the worker: forget_group with the invitation's group id, and no roster and
// no owner's question, because ForgetGroup asks none of a group with no live roster (R9).
//
// THE RESTORE RUNS FIRST, ON EVERY PRESS (review F1). ForgetGroup erases at once a group that a
// restore has noted as one that will not come back, every epoch state and every line this device
// sent with it, and a note made at startup stands for the rest of the run. So the restore is tried
// again before anything is erased: a group that opens now is this device's again (CameBack) and
// nothing is left.
//
// AND A RESTORE THAT DECIDED NOTHING STOPS THE LEAVE (R28, R29, R32). It could not read a file of a
// group just now (another process held it), or could not read one for a reason this build cannot
// classify (a permission, the medium), or was not tried at all because this device is not
// connected. Nothing is known to be wrong with the group. The leave stops there, with that row, and
// forget_group is NOT called: a leave decided on no evidence is what R28 to R30 exist to stop. Q5's
// and the unproven row offer no leave; the not-connected row keeps it offered, because a press once
// connected restores first again. The report names no group for a record that could not be read
// (R12), so a sentinel stops the leave of any group. Only a group that did not come back for some
// other reason is forgotten, and the library then decides on its own fresh evidence (R30).
HeldLeave LeaveHeldGroup(Session& s, HeldInvite const& held, std::string const& joinCode,
                         AdoptFn const& adopt, uint64_t& cameBack) {
  char* err = nullptr;
  const std::string gidHex = urmsg::live::GroupIdHex(held.groupId);
  const int32_t gidLen = static_cast<int32_t>(held.groupId.size());
  urnw::LogInfo("live: held leave: restoring first, in case group {} opens now", gidHex);
  const HeldRestore restore = RestoreHeldGroup(s, held, adopt);
  const urmsg::live::HeldLeaveStep step = urmsg::live::StepAfterRestoreFirst(restore.back, restore.error);
  // A GROUP THAT CAME BACK IS SERVED, whatever the step: the caller takes it up on CameBack, and
  // any other step that finds it back hands it to a loop here rather than hold it unserved.
  if (restore.back && step != urmsg::live::HeldLeaveStep::CameBack) adopt(restore.group, false);
  switch (step) {
    case urmsg::live::HeldLeaveStep::CameBack:
      urnw::LogInfo("live: *** RESTORED *** group {} came back at epoch {}; this device is in it, "
                    "and nothing was left",
                    gidHex, urnet_message_group_epoch(restore.group));
      cameBack = restore.group;
      return HeldLeave::CameBack;
    case urmsg::live::HeldLeaveStep::NotReadNow:
    case urmsg::live::HeldLeaveStep::Unproven:
    case urmsg::live::HeldLeaveStep::NotConnected: {
      const urmsg::live::HeldCopy copy = urmsg::live::LeaveStopCopy(step);
      const bool notConnected = step == urmsg::live::HeldLeaveStep::NotConnected;
      urnw::LogWarn("live: held leave: the restore before the leave {} ({}); group {} did not come "
                    "back, nothing was left, and forget_group was not called ({}): {}",
                    notConnected ? "could not be tried, because this device is not connected"
                    : step == urmsg::live::HeldLeaveStep::Unproven
                        ? "could not read a file of a group, for a reason this build cannot classify"
                        : "could not read a file of a group just now",
                    notConnected ? "ErrNotConnected"
                    : step == urmsg::live::HeldLeaveStep::Unproven ? "ErrGroupReadUnproven"
                                                                   : "ErrGroupRecordNotReadNow",
                    gidHex,
                    notConnected ? "R32" : step == urmsg::live::HeldLeaveStep::Unproven ? "R29" : "R28",
                    restore.error);
      if (!notConnected) {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        g_holdsUnopenedGroup = true;
      }
      PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode, std::string(copy.sentence),
                     copy.offerLeave);
      return HeldLeave::Offered;
    }
    case urmsg::live::HeldLeaveStep::Forget:
      break;
  }
  urnw::LogWarn("live: held leave: group {} did not come back; leaving it", gidHex);
  int32_t kind = urnet_message_device_forget_group(s.device, held.groupId.data(), gidLen, &err);
  std::string why = TakeError(&err);
  // UNFINISHED IS A DEVICE THAT HAS LEFT, as it is for LeaveGroup above; asked once more, and the
  // join below finishes the erase otherwise (the library finishes a leave under way before it
  // joins the same group).
  if (kind == URNET_MESSAGE_FORGET_UNFINISHED) {
    urnw::LogWarn("live: held leave: left group {}, and the erase did not finish: {}; asking once more",
                  gidHex, why);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    kind = urnet_message_device_forget_group(s.device, held.groupId.data(), gidLen, &err);
    why = TakeError(&err);
  }
  switch (kind) {
    case URNET_MESSAGE_FORGET_OK:
      urnw::LogInfo("live: *** LEFT *** the old copy of group {}: its record and its epoch states are "
                    "erased; opening the same invitation again",
                    gidHex);
      return HeldLeave::Left;
    case URNET_MESSAGE_FORGET_UNFINISHED:
      urnw::LogWarn("live: held leave: left group {}, and the erase is still unfinished ({}); the join "
                    "finishes it",
                    gidHex, why);
      return HeldLeave::Left;
    case URNET_MESSAGE_FORGET_FAILED:
      urnw::LogWarn("live: held leave: the library holds no group {} any more ({}); opening the "
                    "invitation again",
                    gidHex, why);
      return HeldLeave::Left;
    default:
      break;
  }

  // ErrGroupNotRestored: the record reads and no restore holds a verdict on this group, so the
  // library will not leave it unasked -- a device that has merely restarted is asked the owner's
  // question like any other. WHAT REACHES THIS NOW (R28, R30): a restore above that did not reach
  // this group at all. Its listing of the groups failed, say, so it tried, noted and kept no group.
  // A group it did try and could not read is in the library's not-read set, and its leave is
  // refused by class (below); a restore that could not read a file of a group (just now, or for a
  // reason this build cannot classify), or could not be tried because this device is not
  // connected, stopped above, before forget_group. Nothing was erased, and the leave is offered
  // again: the next press restores first again.
  if (urmsg::live::IsNotRestoredRefusal(why)) {
    urnw::LogWarn("live: held leave: the restore did not reach group {} ({}); the leave is offered "
                  "again",
                  gidHex, why);
    PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                   std::string(urmsg::live::kHeldNotLiveSentence), true);
    return HeldLeave::Offered;
  }

  const urmsg::live::LeaveRefusal refusal = urmsg::live::ClassifyLeaveRefusal(why);
  // THE LEAVE REACHED THE OWNER'S QUESTION (R33 X2). The library's own re-try brought the group back,
  // and this device owns a group others are in, so the leave asks for the ownership to be handed over
  // first. The library keeps nothing held after that refusal, and the next Restore hands the group
  // back: the app restores once more, so the conversation shows, and the hand-over is made there.
  if (urmsg::live::RestoresOnceMore(refusal)) {
    urnw::LogWarn("live: held leave: forget_group asked the owner's question for group {} "
                  "(ErrOwnerMustTransfer); restoring once more, so the conversation shows (R33): {}",
                  gidHex, why);
    const HeldRestore again = RestoreHeldGroup(s, held, adopt);
    if (again.back) {
      urnw::LogInfo("live: *** RESTORED *** group {} came back at epoch {} after the owner's question; "
                    "this device is in it, and nothing was left",
                    gidHex, urnet_message_group_epoch(again.group));
      cameBack = again.group;
      return HeldLeave::CameBack;
    }
    urnw::LogWarn("live: held leave: group {} did not come back after the owner's question: {}", gidHex,
                  again.error.empty() ? std::string("the restore gave no reason") : again.error);
  }

  // THE LIBRARY'S OWN REFUSAL, BY THE CLASS INSIDE IT (R25 Q5, R29, R32): its fresh re-try of the
  // group's restore, or its read of the record, could not read a file (just now, or for a reason it
  // cannot classify), or the re-try could not be tried because this device is not connected. The
  // library left nothing. Q5's and the unproven row offer no leave; the not-connected row keeps it. A
  // leave mark that names another group (FIX 6) has its own row, with no leave: no press moves it. A
  // refusal of any other kind (the leave mark could not be written, say) keeps Task 39's fallback.
  if (const urmsg::live::HeldCopy copy = urmsg::live::LeaveRefusalCopy(refusal); !copy.sentence.empty()) {
    const bool notConnected = refusal == urmsg::live::LeaveRefusal::NotConnected;
    urnw::LogWarn("live: held leave: forget_group refused group {} ({}); nothing was left, and the "
                  "leave is {}: {}",
                  gidHex, urmsg::live::LeaveRefusalName(refusal),
                  copy.offerLeave ? "offered again" : "not offered", why);
    if (!notConnected) {
      std::lock_guard<std::mutex> lock(g_onboardMutex);
      g_holdsUnopenedGroup = true;
    }
    // R40 A10: a row that names a folder is followed by the library's own words, which name it
    PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                   urmsg::live::ShowsLibraryWords(refusal) ? urmsg::live::WithLibraryWords(copy.sentence, why)
                                                           : std::string(copy.sentence),
                   copy.offerLeave);
    return HeldLeave::Offered;
  }

  urnw::LogError("live: held leave: forget_group refused group {}: {}", gidHex, why);
  PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                 "The old copy of this group could not be left: " +
                     (why.empty() ? std::string("the library gave no reason") : why),
                 true);
  return HeldLeave::Offered;
}

// ── the two-party handshake ───────────────────────────────────────────────────

enum class Mint {
  Ok,         // `joinCode` holds a fresh key package, as base64, and the handshake file holds it
  NoWrapKey,  // ErrNoDeviceWrapKey: this installation cannot make a join code (Task 58)
  Failed,     // anything else; `why` says what
};

// MINT A JOIN CODE: a fresh key package off urnet_message_device_key_package, written to the
// handshake file for the developer's road and kept as text for the screen. At startup, and again
// on "Make a new join code" (R25 Q2). Buffer-out: call once with out == NULL to size, allocate,
// call again.
Mint MintJoinCode(Session& s, std::string& joinCode, size_t& octets, std::string& why) {
  char* err = nullptr;
  int32_t needed = 0;
  if (!urnet_message_device_key_package(s.device, nullptr, &needed, &err)) {
    const std::string sizing = TakeError(&err);
    if (needed <= 0) {
      why = sizing.empty() ? std::string("no reason given") : sizing;
      return urmsg::live::IsNoDeviceWrapKey(sizing) ? Mint::NoWrapKey : Mint::Failed;
    }
  }
  std::vector<uint8_t> keyPackage(static_cast<size_t>(needed));
  int32_t capacity = needed;
  if (!urnet_message_device_key_package(s.device, keyPackage.data(), &capacity, &err)) {
    why = TakeError(&err);
    return urmsg::live::IsNoDeviceWrapKey(why) ? Mint::NoWrapKey : Mint::Failed;
  }
  keyPackage.resize(static_cast<size_t>(capacity));

  // A KEY PACKAGE IS SINGLE USE AND THIS ONE HAS JUST BEEN MINTED, so any file
  // already at that path is a previous run's and is worthless to a peer.
  // Overwriting it is the point.
  const std::filesystem::path keyPackagePath = HandshakeDir() / kKeyPackageName;
  if (!WriteOctets(keyPackagePath, keyPackage.data(), keyPackage.size())) {
    why = "could not write this device's key package to " + Utf8Path(keyPackagePath);
    return Mint::Failed;
  }
  // THE SAME OCTETS AS TEXT. A key package is a public offer to be added, so this is not a secret
  // and the UI may show it, copy it and let a person send it down any channel they like. The code
  // itself is still never LOGGED: a log line is a different audience from a screen, it outlives
  // the moment, and there is no reason to put two kilobytes of base64 in one.
  joinCode = EncodeBase64(keyPackage.data(), keyPackage.size());
  octets = keyPackage.size();
  return Mint::Ok;
}

// THE JOIN ROAD (the conversation-list design 2.3): publish this device's key package, wait for an
// invitation, join, and keep waiting. It runs for the life of the session, beside every conversation
// this device holds: what it joins, founds or brings back is handed to a loop of its own (`adopt`).
// It returns only when this installation cannot make a join code, having said why.
//
// THE KEY PACKAGE IS NOT A SECRET, AND THE INVITE IS PRIVATE. A key package is a
// public offer to be added. An invite carries the Welcome, and since invite
// version 3 its join secrets are sealed to the join code it was made for, so only
// this device can open it (R40 A12). It still names the group and its members, so
// it is never logged, and the invite is deleted the moment it has been used. The
// key package is deleted by the peer that consumes it.
void JoinRoad(Session& s, AdoptFn const& adopt, ReconcileFn const& reconcile) {
  char* err = nullptr;
  const std::filesystem::path dir = HandshakeDir();
  const std::filesystem::path keyPackagePath = dir / kKeyPackageName;
  const std::filesystem::path invitePath = dir / kInviteName;

  // A DEVICE THAT CANNOT MAKE A JOIN CODE IS TOLD SO ON THE SCREEN (Task 58). Its store holds no
  // X-Wing seed (ErrNoDeviceWrapKey), so the library will not publish a join code for it. The
  // session ends here as it did before, with the reason in the window and not only in the log.
  auto cannotMakeJoinCode = [](std::string const& why) {
    urnw::LogError("live: device_key_package refused (ErrNoDeviceWrapKey): {}", why);
    PublishOnboard(urmsg::live::OnboardStep::NoJoinCode, std::string(),
                   std::string(urmsg::live::kSayNoDeviceWrapKey));
  };
  std::string joinCode;
  // THE ROAD'S OPENING, as a device that holds no group has always seen it: a fresh join code, the
  // line that says it is published, and the Waiting state with the note first. At the start of a
  // session that holds no conversation, and again whenever the last one is left (ledger 273). A
  // session that restored a conversation mints nothing until then, as before.
  auto openRoad = [&]() -> bool {
    std::string fresh;
    size_t keyPackageOctets = 0;
    std::string why;
    switch (MintJoinCode(s, fresh, keyPackageOctets, why)) {
      case Mint::Ok:
        break;
      case Mint::NoWrapKey:
        cannotMakeJoinCode(why);
        return false;
      case Mint::Failed:
        urnw::LogError("live: device_key_package: {}", why);
        return false;
    }
    joinCode = std::move(fresh);
    urnw::LogInfo(
        "live: published this device's key package ({} octets) to {}. WAITING for a second party: a "
        "solo device cannot open a group — group_open comes after add_member, the alpha accepts "
        "exactly one, and one credential is one key package. Run sdk/livepeer under the OTHER "
        "account, pointed at this file.",
        keyPackageOctets, Utf8Path(keyPackagePath));

    // THE NOTE FIRST, and "not in a group yet" only when it is true (R25 Q4; A13): a device whose
    // group is on its disk and did not open, or that holds a conversation, is not told it holds none
    // (Live/RefusalCopy.h, WaitingText).
    const bool conversations = SlotCount() > 0;
    std::string waiting;
    {
      std::lock_guard<std::mutex> lock(g_onboardMutex);
      waiting = urmsg::live::WaitingText(g_afterLeaveNote, g_holdsUnopenedGroup || conversations);
      g_afterLeaveNote.clear();
    }
    PublishOnboard(urmsg::live::OnboardStep::Waiting, joinCode, waiting);
    return true;
  };
  size_t heldBefore = SlotCount();
  if (heldBefore == 0 && !openRoad()) return;
  // THE WAITING STATE AGAIN, with the code this road holds (QueueOpenJoinRoad): the dialog opened on
  // the join road beside the conversations this device holds.
  auto publishWaiting = [&]() {
    const bool conversations = SlotCount() > 0;
    std::string waiting;
    {
      std::lock_guard<std::mutex> lock(g_onboardMutex);
      waiting = urmsg::live::WaitingText(std::string_view(), g_holdsUnopenedGroup || conversations);
    }
    PublishOnboard(urmsg::live::OnboardStep::Waiting, joinCode, waiting);
  };

  // ── wait for an invite, from EITHER road ────────────────────────────────────
  //
  // THE FILE IS THE DEVELOPER'S ROAD and sdk/livehost still uses it unchanged. THE PASTED CODE IS
  // THE PERSON'S, and it is the one that works between two machines. Both land in the same place
  // and are applied by the same code below, so there is one join path and not two.
  //
  // THERE IS NO LONGER A DEADLINE, and that is a behaviour change with a reason. The old loop gave
  // up after kInviteWaitMs and returned false, which CLOSED THE WHOLE SESSION: the app went dead
  // until somebody relaunched it. That is defensible for a scripted handshake between two
  // processes started seconds apart. It is wrong for a person, who may paste their code into an
  // email and come back after lunch. The worker sleeps kInvitePollMs between looks, so waiting
  // costs nothing.
  const int64_t waitStartedMs = NowMs();
  std::vector<uint8_t> previous;
  // The invitation last refused over a group this device holds and cannot open, while its leave is
  // on offer (Task 39). Erased when it is superseded and when this function returns.
  HeldInvite held;
  for (;;) {
    // THE LAST CONVERSATION WAS LEFT: the road opens again, as it always did after a leave.
    {
      const size_t heldNow = SlotCount();
      if (heldBefore > 0 && heldNow == 0 && !openRoad()) return;
      heldBefore = heldNow;
    }
    // THE DIALOG ASKED FOR THE ROAD ("Join with an invitation"): a code minted if this run has none,
    // and the Waiting state published again either way.
    {
      bool asked = false;
      {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        asked = g_openRoadAsked;
        g_openRoadAsked = false;
      }
      if (asked && joinCode.empty()) {
        if (!openRoad()) return;
      } else if (asked) {
        publishWaiting();
      }
    }

    std::vector<uint8_t> encoded;
    const ErasedOnExit encodedErased{encoded};
    bool fromPaste = false;

    // The person's road first: somebody who has just pasted is waiting at the screen.
    {
      std::string pasted;
      {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        pasted.swap(g_pastedInvite);
      }
      if (!pasted.empty()) {
        if (!DecodeBase64(pasted, encoded)) {
          // Refused BY NAME and the loop CONTINUES: a person who mistyped gets to try again, and
          // a session that closed here would make a typo cost a relaunch.
          PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                         "That invitation could not be read. Copy the whole code and paste it "
                         "again - it is one long line with no spaces inside it.");
          urnw::LogWarn("live: a pasted invite was not decodable base64; refused, still waiting");
          encoded.clear();
        } else {
          fromPaste = true;
        }
      }
    }

    // A NEW INVITATION SUPERSEDES THE ONE THAT WAS REFUSED: the person has moved on from it, and
    // its leave is no longer theirs to ask for. (The file road's own new invitation drops it below.)
    if (!encoded.empty()) held.Drop();

    // "MAKE A NEW JOIN CODE" (R25 Q2): the press on a row that asked for one. The fresh code
    // replaces the one in the box and in the handshake file; the one shown before stays valid
    // until it is used.
    {
      bool asked = false;
      {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        asked = g_newJoinCodeAsked;
        g_newJoinCodeAsked = false;
      }
      if (asked) {
        std::string fresh;
        size_t octets = 0;
        std::string why;
        switch (MintJoinCode(s, fresh, octets, why)) {
          case Mint::Ok:
            joinCode = std::move(fresh);
            urnw::LogInfo("live: *** NEW JOIN CODE *** minted ({} octets) and published to {}",
                          octets, Utf8Path(keyPackagePath));
            PublishOnboard(urmsg::live::OnboardStep::Waiting, joinCode,
                           std::string(urmsg::live::kSayNewJoinCode));
            break;
          case Mint::NoWrapKey:
            // THAT ROW, as at startup: this installation cannot make a join code at all.
            cannotMakeJoinCode(why);
            return;
          case Mint::Failed:
            urnw::LogError("live: a new join code could not be minted: {}", why);
            PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                           "A new join code could not be made: " + why, false, true);
            break;
        }
      }
    }

    // THE LEAVE OF A GROUP THAT IS NOT LIVE, AND THEN THE SAME INVITATION AGAIN (Task 39). Served
    // before the file road, so a file still on the disk cannot race the leave the person asked for.
    {
      bool asked = false;
      {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        asked = g_heldLeaveAsked;
        g_heldLeaveAsked = false;
      }
      if (asked && held.valid) {
        uint64_t cameBack = 0;
        switch (LeaveHeldGroup(s, held, joinCode, adopt, cameBack)) {
          case HeldLeave::Left:
            // THE SAME INVITATION: the refusal came before the join code was touched, so it is
            // still good. It goes through the one join path below, like any other.
            {
              std::lock_guard<std::mutex> lock(g_onboardMutex);
              g_holdsUnopenedGroup = false;
            }
            encoded = held.octets;
            fromPaste = held.fromPaste;
            break;
          case HeldLeave::CameBack: {
            // THE GROUP IS THIS DEVICE'S AGAIN, and a loop of its own serves it from here; the road
            // keeps waiting. The invitation into it has done its job. Only this device can open it,
            // but a file copy of it still names the group and its members, so it goes (R40 A12).
            const std::string backHex = GroupIdHexOf(cameBack);
            adopt(cameBack, false);
            PublishOnboard(urmsg::live::OnboardStep::Joined, joinCode, "You are in the group.", false,
                           false, backHex);
            std::error_code ec;
            if (!held.fromPaste && std::filesystem::exists(invitePath, ec) &&
                !std::filesystem::remove(invitePath, ec)) {
              urnw::LogWarn("live: the invite at {} could not be deleted ({}). Delete it by hand: only "
                            "this device can open it, but it names the group and its members.",
                            Utf8Path(invitePath), ec.message());
            }
            held.Drop();
            previous.clear();
            reconcile();
            continue;
          }
          case HeldLeave::Offered:
            reconcile();
            break;
        }
      } else if (asked) {
        urnw::LogWarn("live: a held leave was asked for and no refused invitation stands to answer "
                      "it; nothing was left");
      }
    }

    // THE FOUNDER'S ROAD, and it is checked before the two joining roads because somebody who
    // has pressed "Start a group" is not waiting for anybody: they want the group that the next
    // person's join code will open.
    {
      bool make = false;
      {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        make = g_createGroupAsked;
        g_createGroupAsked = false;
      }
      // THE RULE AGAIN, ON THE ROAD (ConversationRules.h CreateAction): QueueCreateGroup asked it,
      // and this is the one thread that makes a group of one, so nothing can have changed it since.
      std::string waitingGroup;
      if (make && urmsg::live::CreateAction(HeldForCreateSnapshot(), waitingGroup) !=
                      urmsg::live::CreateVerdict::Take) {
        urnw::LogWarn("live: a create reached the road while group {} is still a group of one; "
                      "nothing was made",
                      waitingGroup.substr(0, (std::min)(waitingGroup.size(), size_t{16})));
        make = false;
      }
      if (make) {
        // A GROUP ID IS 32 OCTETS AND IT IS NOT A SECRET, but it must not be guessable either:
        // the server indexes records by it, so a predictable one lets anybody ask whether a
        // particular group has traffic. Taken from the OS entropy source, like every other
        // random value in this process.
        uint8_t gid[32] = {};
        if (!RandomOctets(gid, sizeof(gid))) {
          urnw::LogError("live: could not draw a group id from the system entropy source");
          PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                         "This computer would not give a random number, so no group was made. "
                         "That is unusual - try again, and tell us if it keeps happening.");
        } else {
          char* mkErr = nullptr;
          const uint64_t made = urnet_message_device_create_group(
              s.device, s.ctx, gid, static_cast<int32_t>(sizeof(gid)), &mkErr);
          if (made == 0) {
            const std::string why = TakeError(&mkErr);
            urnw::LogError("live: device_create_group: {}", why);
            PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                           "The group could not be made: " + why);
          } else {
            const std::string madeHex = ToHex(gid, static_cast<int32_t>(sizeof(gid)));
            urnw::LogInfo("live: *** GROUP CREATED *** {} at epoch {}, open on the server: {}",
                          madeHex, urnet_message_group_epoch(made),
                          urnet_message_group_is_open(made) ? "yes" : "no");
            // AND IT IS NOT USABLE YET, WHICH THE MESSAGE SAYS RATHER THAN THE UI GUESSING. A
            // group of one is at epoch 0 and the server has never been told about it; the commit
            // that opens it is the one adding the first other person. Its add state says so first,
            // then it is taken up as a group of one, whose own loop serves every add (the first of
            // them opens it) and fetches nothing until then, and then the road says what it made.
            PublishAdd(madeHex, urmsg::live::AddStep::Founded, kFoundedSentence);
            adopt(made, /*groupOfOne=*/true);
            PublishOnboard(urmsg::live::OnboardStep::Founded, joinCode, kFoundedSentence, false, false,
                           madeHex);
            reconcile();
          }
        }
      }
    }

    // The developer's road: the file sdk/livehost writes. Read only once this run has published a
    // join code for it to have been made from (a session that restored its conversations has not).
    if (encoded.empty() && !joinCode.empty()) {
      std::vector<uint8_t> raw;
      if (ReadOctets(invitePath, raw)) {
        // Two identical reads before accepting it: the writer is another process and a partially
        // written file read whole is a checksum failure at the parse, where it looks like
        // corruption rather than like a race.
        if (held.valid && raw == held.octets) {
          // THE INVITATION WHOSE LEAVE IS ON OFFER, still in its file (review F4). Joined again it
          // is refused again, and the Joining state between withdraws the button, so a press in
          // that moment finds nothing to leave. It is skipped while its leave is on offer.
          ::SecureZeroMemory(raw.data(), raw.size());
        } else if (!previous.empty() && previous == raw) {
          encoded = std::move(raw);
          held.Drop();
        } else {
          previous = std::move(raw);
        }
      }
    }

    if (encoded.empty()) {
      ::Sleep(static_cast<DWORD>(kInvitePollMs));
      continue;
    }

    PublishOnboard(urmsg::live::OnboardStep::Joining, joinCode, "Opening the invitation...");
    urnw::LogInfo("live: an invite arrived from {} ({} octets) after {} ms",
                  fromPaste ? "the app" : "the handshake file", encoded.size(),
                  NowMs() - waitStartedMs);

    // An invite ends with a checksum of everything before it, so a damaged one is refused HERE
    // rather than joining something wrong.
    const uint64_t invite =
        urnet_message_parse_invite(encoded.data(), static_cast<int32_t>(encoded.size()), &err);
    if (invite == 0) {
      const std::string why = TakeError(&err);
      // AN INVITATION FROM AN OLDER URMESSAGE is refused here, by its version (Task 58). Every other
      // refusal of the parse is said as it was before.
      const bool older =
          urmsg::live::ClassifyJoinCopy(why) == urmsg::live::JoinCopy::InviteVersion;
      urnw::LogError("live: parse_invite refused it ({}): {}", older ? "ErrInviteVersion" : "other",
                     why);
      PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                     older ? std::string(urmsg::live::kSayInviteVersion)
                           : std::string("That invitation was refused: it did not survive the "
                                         "journey intact. Ask for a fresh one - an invitation can "
                                         "only be used once."));
      previous.clear();
      ::Sleep(static_cast<DWORD>(kInvitePollMs));
      continue;
    }
    const uint64_t joined = urnet_message_device_join(s.device, s.ctx, invite, &err);
    urnet_release(invite);
    if (joined == 0) {
      const std::string why = TakeError(&err);
      // A REFUSAL OVER A GROUP THAT COULD NOT BE READ KEEPS ITS INVITATION, with no leave on offer, so
      // the file road does not open the same file again and again while nothing has changed. This
      // keeps it for the gate's refusals that carry a class's own sentinel and no held sentence: its
      // read of the group's record (heldUnread); its refusal of a record missing from a folder where
      // the store's own files stand (R34 item 1, in heldUnread's clause, carrying the unproven
      // sentinel); and its answer for a group the last Restore could not read (R33 X3, "group <hex>
      // is on this device's disk and the last Restore could not read it, ..."). The class's row
      // answers each below, with no leave: the leave Task 39's sentence names would be decided on no
      // evidence.
      auto keepRefusedInvite = [&]() {
        std::array<uint8_t, urmsg::live::kGroupIdOctets> gid{};
        const bool read = urmsg::live::InviteGroupId(encoded.data(), encoded.size(), gid);
        held.Drop();
        held.octets = encoded;
        if (read) held.groupId = gid;
        held.fromPaste = fromPaste;
        held.valid = true;
      };
      // A GROUP THIS DEVICE ALREADY HOLDS (Task 39): told apart by the library's own sentence,
      // because the bridge carries sentences and not types (HeldGroupRules.h).
      switch (urmsg::live::ClassifyJoinRefusal(why)) {
        case urmsg::live::JoinRefusal::HeldLive:
          urnw::LogWarn("live: device_join refused: this device is already in that group: {}", why);
          PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                         std::string(urmsg::live::kHeldLiveSentence));
          break;
        case urmsg::live::JoinRefusal::HeldNotLive: {
          // THE GROUP ID OF THE INVITATION THIS PERSON WAS REFUSED OVER, read off its own octets,
          // and offered only when it is the id the library's sentence names.
          std::array<uint8_t, urmsg::live::kGroupIdOctets> gid{};
          const bool read = urmsg::live::InviteGroupId(encoded.data(), encoded.size(), gid);
          const std::string gidHex = read ? urmsg::live::GroupIdHex(gid) : std::string();
          if (read && urmsg::live::RefusalNamesGroup(why, gidHex)) {
            held.Drop();
            held.octets = encoded;
            held.groupId = gid;
            held.fromPaste = fromPaste;
            held.valid = true;
            // AFTER A RESTORE THAT COULD NOT READ A FILE OF A GROUP (R28, R29: just now, or for a
            // reason this build cannot classify), the group may be that one, and nothing is known
            // to be wrong with it: that class's row, and no leave. The invitation is kept all the
            // same, so the file road does not open it again and again.
            urmsg::live::ReadClass lastClass = urmsg::live::ReadClass::None;
            {
              std::lock_guard<std::mutex> lock(g_onboardMutex);
              lastClass = g_lastRestoreClass;
              if (lastClass != urmsg::live::ReadClass::None) g_holdsUnopenedGroup = true;
            }
            const urmsg::live::HeldCopy copy = urmsg::live::HeldNotLiveCopy(lastClass);
            if (copy.offerLeave) {
              urnw::LogWarn("live: device_join refused: this device holds group {} on its disk and "
                            "it is not live; the leave is offered: {}",
                            gidHex, why);
            } else if (lastClass == urmsg::live::ReadClass::Unproven) {
              urnw::LogWarn("live: device_join refused: this device holds group {} on its disk and "
                            "it is not live, and the last restore could not read a file of a group, "
                            "for a reason this build cannot classify; no leave is offered (R29): {}",
                            gidHex, why);
            } else {
              urnw::LogWarn("live: device_join refused: this device holds group {} on its disk and "
                            "it is not live, and the last restore could not read a file of a group "
                            "just now; no leave is offered (R28): {}",
                            gidHex, why);
            }
            PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode, std::string(copy.sentence),
                           copy.offerLeave);
          } else {
            // NOT EXPECTED, AND NOT PAPERED OVER: a sentence that says to leave would name an
            // action this screen cannot take, so it is not shown.
            urnw::LogError("live: device_join refused over a held group that is not live, and the "
                           "invitation's own group id {} is not the one the refusal names; no leave "
                           "is offered: {}",
                           read ? gidHex : std::string("(unreadable)"), why);
            PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                           "This invitation was refused because this device already holds its "
                           "group, and this computer could not tell which group that is.");
          }
          break;
        }
        case urmsg::live::JoinRefusal::Other:
          // THE LEAVE LEFT ITS ERASE UNFINISHED, AND THE JOIN AFTER IT WAITS FOR THAT ERASE (review
          // F3). The invitation and the join code are both still good, so this is not "not meant
          // for this device": the leave is offered again, and another press finishes the erase.
          if (held.valid &&
              urmsg::live::IsUnfinishedEraseOf(why, urmsg::live::GroupIdHex(held.groupId))) {
            urnw::LogWarn("live: device_join refused: the erase of the leave of group {} has not "
                          "finished; the leave is offered again: {}",
                          urmsg::live::GroupIdHex(held.groupId), why);
            PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                           std::string(urmsg::live::kHeldNotLiveSentence), true);
            break;
          }
          // THE JOIN SEAL'S REFUSALS (Task 58), each in the person's words and each true about what
          // it cost: the window's own sentence says the join code is still good, and the one
          // refusal that does not carry it (ErrNoJoinCodeForInvite) is never said that way (R13).
          // The two rows that ask for a new join code offer the button that makes one (R25 Q2). A
          // record that could not be read (just now, R25 Q5, or for a reason this build cannot
          // classify, R29), and a group the last Restore could not read (R33 X3), are of a group this
          // device holds: the class's row, no leave, and the invitation kept. So is a leave mark in the
          // group's folder that names another group (FIX 6), which no press moves. The window's
          // refusal of a join the store would not serve (R34 A1) is handled as the window's own row
          // is: no button, and the invitation is not kept, so a person pastes it again once the
          // folder is fixed.
          if (const urmsg::live::JoinCopy row = urmsg::live::ClassifyJoinCopy(why);
              row != urmsg::live::JoinCopy::Other) {
            const bool notReadRow = row == urmsg::live::JoinCopy::GroupRecordNotReadNow ||
                                    row == urmsg::live::JoinCopy::GroupReadUnproven ||
                                    row == urmsg::live::JoinCopy::LeaveMarkForeign;
            urnw::LogWarn("live: device_join refused ({}){}: {}", urmsg::live::JoinCopyName(row),
                          notReadRow ? "; no leave is offered" : "", why);
            if (notReadRow) keepRefusedInvite();
            if (notReadRow) {
              std::lock_guard<std::mutex> lock(g_onboardMutex);
              g_holdsUnopenedGroup = true;
            }
            // R40 A10: a row that names a folder is followed by the library's own words, which name it
            PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                           urmsg::live::ShowsLibraryWords(row)
                               ? urmsg::live::WithLibraryWords(urmsg::live::JoinCopySentence(row), why)
                               : std::string(urmsg::live::JoinCopySentence(row)),
                           false, urmsg::live::OffersNewJoinCode(row));
            break;
          }
          urnw::LogError(
              "live: device_join: {} - if this names a key package this device does not hold, the "
              "invite was built from a STALE join code. Send the code the app is showing NOW.",
              why);
          PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                         "That invitation was not meant for this device. Send the join code this "
                         "screen is showing now, and ask for a new invitation built from it.");
          break;
      }
      previous.clear();
      ::Sleep(static_cast<DWORD>(kInvitePollMs));
      continue;
    }

    held.Drop();
    urnw::LogInfo("live: *** JOINED the group *** at epoch {}", urnet_message_group_epoch(joined));
    const std::string joinedHex = GroupIdHexOf(joined);
    adopt(joined, false);
    // THE JOIN SPENT THE JOIN CODE, because a key package is single use. A fresh one replaces it, in
    // the box and in the handshake file, and the road keeps waiting for the next invitation (the
    // conversation-list design 2.3). A code that cannot be minted now is not shown at all, rather
    // than a spent one.
    {
      std::string fresh;
      size_t octets = 0;
      std::string why;
      if (MintJoinCode(s, fresh, octets, why) == Mint::Ok) {
        joinCode = std::move(fresh);
        urnw::LogInfo("live: the join spent this device's join code; a fresh one ({} octets) is in "
                      "{} for the next invitation",
                      octets, Utf8Path(keyPackagePath));
      } else {
        joinCode.clear();
        urnw::LogError("live: after the join, a fresh join code could not be minted: {}", why);
      }
    }
    PublishOnboard(urmsg::live::OnboardStep::Joined, joinCode, "You are in the group.", false, false,
                   joinedHex);
    reconcile();

    // CONSUMED, SO DELETED. It has done its job. Only this device can open it, but left on disk it
    // still names the group and its members (R40 A12). Only the FILE road leaves anything behind - a
    // pasted code was never written down by this process, which is the better of the two for that
    // reason.
    std::error_code ec;
    if (!fromPaste && std::filesystem::exists(invitePath, ec)) {
      if (!std::filesystem::remove(invitePath, ec)) {
        urnw::LogWarn("live: the invite at {} could not be deleted ({}). Delete it by hand: only "
                      "this device can open it, but it names the group and its members.",
                      Utf8Path(invitePath), ec.message());
      } else {
        urnw::LogInfo("live: the invite has been consumed and deleted");
      }
    }
    previous.clear();
  }
}

// ── reading the conversation off the ABI ──────────────────────────────────────

// Every message the group holds, in the order it learned them. NOT just what a
// fetch answered: a reaction and a tombstone change a message that arrived
// earlier and appear in NEITHER fetch list.
void CollectMessages(uint64_t group, std::vector<urmsg::live::LiveMessage>& out) {
  out.clear();
  const uint64_t all = urnet_message_group_messages(group);
  const int32_t count = urnet_message_list_count(all);
  out.reserve(static_cast<size_t>(count < 0 ? 0 : count));
  for (int32_t i = 0; i < count; ++i) {
    urmsg::live::LiveMessage m;
    const std::string info = TakeString(urnet_message_list_info(all, i));
    // A malformed info string is a programming error on this side of the ABI,
    // not a protocol event — but it must not take the whole conversation down,
    // so the row is kept with whatever parsed and named as unreadable metadata.
    try {
      const nlohmann::json j = nlohmann::json::parse(info);
      m.recordId = j.value("record_id", uint64_t{0});
      m.messageId = j.value("message_id", std::string{});
      m.senderHandle = j.value("sender_handle", std::string{});
      m.senderIdentity = j.value("sender_identity", std::string{});
      m.mine = j.value("mine", false);
      m.sentAtMs = j.value("sent_at_ms", int64_t{0});
      m.kind = static_cast<uint8_t>(j.value("kind", 0));
      m.gap = j.value("gap", std::string{});
      // Item 242 R4. A DEFAULT OF "" AND NOT "member": absent means the library
      // said nothing about this record's sender, and inventing a role there
      // would be a claim. The thread's collapse rule asks for the exact word
      // "observer", so "" asks for nothing — which is also what the ABI answers
      // for a record that did not open.
      m.senderRoleAtSend = j.value("sender_role_at_send", std::string{});
      m.replyToId = j.value("reply_to_id", std::string{});
      m.deleted = j.value("deleted", false);
      m.bodyLen = j.value("body_len", int32_t{0});
    } catch (const std::exception& e) {
      urnw::LogWarn("live: message[{}] metadata did not parse ({}); the row is kept and its "
                    "metadata is unreadable",
                    i, e.what());
    }

    // THE BODY IS OCTETS AND NOT TEXT — the ABI does not validate it as UTF-8
    // and does not promise it is NUL-free — which is why it is a second call
    // and a buffer-out rather than a field of the json above.
    if (0 < m.bodyLen) {
      int32_t capacity = m.bodyLen;
      std::vector<uint8_t> body(static_cast<size_t>(capacity));
      if (urnet_message_list_body(all, i, body.data(), &capacity)) {
        m.body.assign(reinterpret_cast<const char*>(body.data()), static_cast<size_t>(capacity));
      }
    }

    // Reactions are a count and an accessor rather than an array inside the
    // json, because NOTHING caps how many one message can carry.
    const int32_t reactions = urnet_message_list_reaction_count(all, i);
    for (int32_t k = 0; k < reactions; ++k) {
      const std::string one = TakeString(urnet_message_list_reaction_info(all, i, k));
      if (one.empty()) continue;
      try {
        const nlohmann::json j = nlohmann::json::parse(one);
        m.reactions.push_back({j.value("emoji", std::string{}), j.value("mine", false)});
      } catch (const std::exception&) {
        // One unreadable reaction is not a reason to drop the message.
      }
    }
    out.push_back(std::move(m));
  }
  if (all != 0) urnet_release(all);
}

// THE ROSTER, off urnet_message_group_members: every leaf at the current epoch,
// in leaf order, with its role. Read on every publish, because a commit from
// another member may have changed a role since the last one. An unreadable
// roster (the ABI answers 0 with an error) leaves `out` EMPTY, which the world
// draws as the placeholder, and is logged - it is a fact about the group, not a
// row to invent.
void CollectMembers(uint64_t group, std::vector<urmsg::live::LiveMember>& out) {
  out.clear();
  char* err = nullptr;
  const uint64_t list = urnet_message_group_members(group, &err);
  const std::string failure = TakeError(&err);
  if (list == 0) {
    urnw::LogWarn("live: group_members could not be read: {}",
                  failure.empty() ? "no reason given" : failure);
    return;
  }
  const int32_t count = urnet_message_member_list_count(list);
  out.reserve(static_cast<size_t>(count < 0 ? 0 : count));
  for (int32_t i = 0; i < count; ++i) {
    const std::string info = TakeString(urnet_message_member_list_info(list, i));
    urmsg::live::LiveMember m;
    try {
      const nlohmann::json j = nlohmann::json::parse(info);
      m.leafIndex = j.value("leaf_index", uint32_t{0});
      m.senderHandle = j.value("sender_handle", std::string{});
      m.identityPub = j.value("identity_pub", std::string{});
      m.role = j.value("role", std::string{});
      m.mine = j.value("mine", false);
    } catch (const std::exception& e) {
      urnw::LogWarn("live: member[{}] info did not parse ({}); the row is kept with what parsed",
                    i, e.what());
    }
    out.push_back(std::move(m));
  }
  urnet_release(list);
}

// This device's own role, off urnet_message_group_my_role. Empty when it could
// not be read, and the world draws the placeholder for that.
std::string ReadMyRole(uint64_t group) {
  char* err = nullptr;
  std::string role = TakeString(urnet_message_group_my_role(group, &err));
  const std::string failure = TakeError(&err);
  if (role.empty()) {
    urnw::LogWarn("live: group_my_role could not be read: {}",
                  failure.empty() ? "no reason given" : failure);
  }
  return role;
}

// One line for the roster, in the shape sdk/livepeer prints its own, so the two
// sides' views of one group can be laid beside each other in two logs.
std::string RosterLine(urmsg::live::LiveGroup const& live) {
  std::string rows;
  for (auto const& m : live.members) {
    if (!rows.empty()) rows += "; ";
    rows += "leaf " + std::to_string(m.leafIndex) + " " + m.role + " " +
            m.identityPub.substr(0, std::min<size_t>(m.identityPub.size(), 16)) +
            (m.mine ? " (this device)" : "");
  }
  return "epoch " + std::to_string(live.epoch) + ", my role " + live.myRole + ": " + rows;
}

// What a kind code is called, by name rather than by number. An UNKNOWN code is
// not refused and not assumed away: the set is explicitly open.
const char* KindName(uint8_t kind) {
  switch (kind) {
    case URNET_MESSAGE_KIND_TEXT: return "text";
    case URNET_MESSAGE_KIND_REPLY: return "reply";
    case URNET_MESSAGE_KIND_ATTACHMENT: return "attachment";
    case URNET_MESSAGE_KIND_TOMBSTONE: return "tombstone";
    case URNET_MESSAGE_KIND_REACTION_ADD: return "reaction-add";
    case URNET_MESSAGE_KIND_REACTION_REMOVE: return "reaction-remove";
    case URNET_MESSAGE_KIND_COVER: return "cover";
    default: return "unknown-kind";
  }
}

// One line per message, with the TEXT in it.
//
// THIS FILE USED TO REFUSE TO LOG A BODY, on the reasoning that the text is not
// ours to put in a file. The owner has ruled otherwise FOR THIS PATH and the
// reason is that a log without the text cannot be the proof that the path works
// — "12 records opened" is equally true of twelve records of garbage. The
// narrowing that makes it acceptable: this runs only under --live, which is off
// by default and off in CI, and it is the two development accounts talking to
// each other. It is NOT a pattern for a shipping build.
//
// AND THE NARROWING STOPPED HOLDING (ledger 271). The alpha runs live on TESTERS' machines -
// a plain launch is live now - and its README tells them this log "contains no message text
// ... it is safe to send on". So the ruling's own scope is kept by a switch rather than by
// the launch: the text, and a reaction's emoji, are written only when %URMESSAGE_LOG_BODIES%
// is "1", which the development loop sets and no tester is told about. Without it every line
// below still says which record, from whom, of what kind and how long, and nothing it said.
bool LogBodies() {
  static const bool on = EnvVar(L"URMESSAGE_LOG_BODIES") == L"1";
  return on;
}

void LogMessages(urmsg::live::LiveGroup const& live) {
  urnw::LogInfo("live: ---- the conversation: {} message(s) ----", live.messages.size());
  for (size_t i = 0; i < live.messages.size(); ++i) {
    urmsg::live::LiveMessage const& m = live.messages[i];
    std::string reactions;
    for (auto const& r : m.reactions) {
      if (!reactions.empty()) reactions += " ";
      // the emoji is content; without the switch a reaction is only its owner
      reactions += LogBodies() ? r.emoji : std::string("r");
      reactions += r.mine ? "(mine)" : "";
    }
    // A gap is something that IS at this position and cannot be shown. It is
    // branched on BEFORE kind, because on a gap `kind` is the code the record
    // arrived under and not what the record is.
    if (!m.gap.empty()) {
      urnw::LogInfo(
          "live:   [{}] record {} from {} GAP={} (arrived as kind {}) — a line IS here and this "
          "build cannot show it",
          i, m.recordId, m.senderHandle, m.gap, KindName(m.kind));
      continue;
    }
    std::string text = LogBodies() ? m.body : std::string("(text not logged)");
    // One line per message: a body with a newline in it would otherwise make
    // the log's own structure a thing the sender chooses.
    for (char& c : text) {
      if (c == '\n' || c == '\r') c = ' ';
    }
    if (200 < text.size()) text = text.substr(0, 200) + "…";
    urnw::LogInfo(
        "live:   [{}] record {} {} sender {} kind={} deleted={} reply_to={} reactions=[{}] "
        "body({} octets)=\"{}\"",
        i, m.recordId, m.mine ? "MINE" : "THEIRS", m.senderHandle, KindName(m.kind), m.deleted,
        m.replyToId.empty() ? "-" : m.replyToId, reactions, m.bodyLen, text);
  }
}

// What the status strip says about the route: the operator host on the operator path; for a
// route, the way out and, through URnetwork, the countries of the exits the tunnel holds.
std::string RouteLabel(Session const& s, Route route) {
  if (!s.routeClient) return kHost;
  if (route == Route::Direct) return "direct";
  const nlohmann::json status =
      nlohmann::json::parse(TakeString(urnet_message_route_client_status(s.client)), nullptr, false);
  std::string countries;
  if (!status.is_discarded() && status.contains("window_countries") &&
      status["window_countries"].is_array()) {
    for (auto const& country : status["window_countries"]) {
      if (!country.is_string()) continue;
      if (!countries.empty()) countries += ", ";
      countries += country.get<std::string>();
    }
  }
  if (countries.empty()) return "URnetwork (finding an exit)";
  return "URnetwork exit \u00B7 " + countries;
}

// ── leaving (the owner's ruling of 2026-10-02; msgrepo ledger 273) ───────────────
//
// "DELETE FOR ME AND LEAVE", on the worker: the hand-over first when one is asked for, then this
// device's own half, urnet_message_device_forget_group, which erases its whole copy of the
// conversation and tells nobody (ruling 48). True means the device has left. False puts the
// library's sentence in `refusal` and leaves the group held as it was -- except that a hand-over
// granted before a refused forget has already moved the ownership.
//
// THE HAND-OVER IS A COMMIT AND THE LEAVE IS NOT, and the order is the owner rule's: an owner who
// is the last of its identity's leaves is refused while anybody else is in the group (MASTER
// section 11), so ownership moves first, and a hand-over that is refused leaves nothing done.
bool LeaveGroup(Session& s, uint64_t group, std::string const& transferTo, std::string& refusal) {
  char* err = nullptr;
  refusal.clear();
  if (!transferTo.empty()) {
    const int32_t kind =
        urnet_message_group_transfer_ownership(group, s.ctx, transferTo.c_str(), &err);
    const std::string failure = TakeError(&err);
    if (kind != URNET_MESSAGE_COMMIT_OK) {
      refusal = failure.empty()
                    ? std::string("handing the conversation over was refused, and the library "
                                  "gave no reason")
                    : failure;
      urnw::LogError("live: leave: the hand-over to {} answered kind {}: {}", transferTo, kind,
                     refusal);
      return false;
    }
    urnw::LogInfo("live: leave: ownership handed to {}; the group is at epoch {}", transferTo,
                  urnet_message_group_epoch(group));
  }
  // ONE RECEIVE FIRST, while there is a session to receive on (review L4). A transfer of ownership
  // TO this device that it has not fetched would make it an owner who leaves, and strand the
  // group; fetched, the library refuses the leave by name. Offline it is skipped: a leave needs no
  // session, and a device that cannot fetch cannot learn of the transfer either way.
  uint8_t openGid[32] = {};
  int32_t openGidLen = static_cast<int32_t>(sizeof(openGid));
  const bool open = urnet_message_group_id(group, openGid, &openGidLen) &&
                    GateOpen(ToHex(openGid, openGidLen));
  if (open) {
    const uint64_t fetched = urnet_message_group_receive(group, s.ctx, &err);
    const std::string fetchError = TakeError(&err);
    if (!fetchError.empty()) urnw::LogWarn("live: leave: the receive before it reported: {}", fetchError);
    if (fetched != 0) urnet_release(fetched);
  }
  uint8_t gid[32] = {};
  int32_t gidLen = static_cast<int32_t>(sizeof(gid));
  if (!urnet_message_group_id(group, gid, &gidLen)) {
    refusal = "this computer could not read which group it is in, so it did not leave";
    urnw::LogError("live: leave: group_id answered nothing; not leaving");
    return false;
  }
  int32_t kind = urnet_message_device_forget_group(s.device, gid, gidLen, &err);
  std::string why = TakeError(&err);
  // UNFINISHED IS A DEVICE THAT HAS LEFT (review H1): the group is closed and no longer held, and only
  // the erase on the disk is owed. A file another process held open is the usual cause, so it is
  // asked once more a second later; the next launch's restore finishes it otherwise.
  if (kind == URNET_MESSAGE_FORGET_UNFINISHED) {
    urnw::LogWarn("live: leave: left, and the erase did not finish: {}; asking once more", why);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    kind = urnet_message_device_forget_group(s.device, gid, gidLen, &err);
    why = TakeError(&err);
  }
  switch (kind) {
    case URNET_MESSAGE_FORGET_OK:
      urnw::LogInfo("live: *** LEFT *** this device's copy of the group is erased; nobody was told");
      break;
    case URNET_MESSAGE_FORGET_UNFINISHED: {
      urnw::LogWarn("live: *** LEFT *** but the erase is still unfinished: {}", why);
      std::lock_guard<std::mutex> lock(g_onboardMutex);
      g_afterLeaveNote =
          "This computer left the conversation but could not finish erasing it from the disk; "
          "URmessage finishes that the next time it starts.";
      break;
    }
    case URNET_MESSAGE_FORGET_FAILED:
      // the library holds no such group, so there is nothing left to leave: this handle is stale
      urnw::LogWarn("live: leave: the library holds no such group ({}); treating it as left", why);
      break;
    default:
      refusal = why.empty() ? std::string("the library refused to leave and gave no reason") : why;
      urnw::LogError("live: leave: forget_group refused: {}", refusal);
      return false;
  }
  // THE CONVERSATION'S OWN LABEL GOES WITH IT (review M4): a name given to a conversation this device
  // has left names nothing. The labels given to PEOPLE stay: they are this person's address book.
  if (!StoreConversationLocalName(ToHex(gid, gidLen), ""))
    urnw::LogWarn("live: leave: the conversation's own label could not be removed");
  return true;
}

// ── one group's loop (the conversation-list design 2.1) ─────────────────────────────────────────
//
// THE SESSION, SHARED: the handles every loop uses, on the heap and held by each loop, so no loop
// ever outlives what it calls. RunSession closes the session only when no loop runs.
struct SessionContext {
  Session s;
  Hellos hellos;
  Route route = Route::Direct;
  std::string clientId;
  std::string platformUrl;
  // THE FOLDERS THIS SESSION GIVES THE LIBRARY, the store's root first, and the user's profile: a
  // receive report is drawn with them named, never spelled (DrawnReceiveReport, lead ruling R71).
  std::vector<std::string> appRoots;
  std::string profileRoot;
  // ONE PUSH WAITER FOR THE DEVICE (RoutePushToLoops), made once the device is connected.
  std::unique_ptr<PushWaiter> pushWaiter;

  SessionContext() = default;
  SessionContext(const SessionContext&) = delete;
  SessionContext& operator=(const SessionContext&) = delete;
  // CLOSED BY WHOEVER LETS GO OF IT LAST: RunSession, unless a loop still runs over its handles, and
  // then the last loop to end. The waiter stops first, because it waits on the device. Close is
  // idempotent, so a session RunSession closed on a path that gave up is not closed twice.
  ~SessionContext() {
    pushWaiter.reset();
    s.Close();
  }
};

// A GROUP'S SLOT GOES: its handle, its gate and with it every entry stamped for it, its queue, and
// its conversation, and the world is published without it. No other conversation, gate or queue is
// touched (the conversation-list design 2.2). Called on the group's own loop, which owns the handle.
// Answers whether this device now holds no group.
//
// ITS ADD STATE GOES WITH IT, the invitation it may hold included, and so does its queued add (the
// conversation-list design 2.2). A pasted invitation is the device's, and stays for the join road.
//
// THE LAST CONVERSATION publishes a neutral onboarding state (`sentence`) BEFORE the empty world
// (re-check R4): the window re-arms its join dialog when the empty world arrives and draws the last
// state it holds, which would otherwise say this device is in the group it just left. The join road
// opens again a moment later, with its join code (JoinRoad).
bool RetireSlot(GroupSlot& slot, std::string const& sentence) {
  if (slot.handle != 0) {
    urnet_release(slot.handle);
    slot.handle = 0;
  }
  DropGate(slot.gidHex);
  if (const auto stale = TakeSlotQueue(slot); !stale.empty())
    urnw::LogWarn("live: {} action(s) queued for group {} were dropped with it", stale.size(),
                  slot.gidHex);
  DropAdd(slot.gidHex);
  std::lock_guard<std::mutex> retire(g_retireMutex);
  if (SlotCount() == 1) PublishOnboard(urmsg::live::OnboardStep::Idle, std::string(), sentence);
  return RemoveSlot(slot.gidHex);
}

// WHAT ONE PASS WAS WHEN A DEVICE HELD ONE GROUP, bound to one slot: its fetch, its sends, its
// subscription, its outboxes, its roster line and its leave. It ends only when its group is left.
void GroupLoop(std::shared_ptr<SessionContext> session, std::shared_ptr<GroupSlot> slot) {
  Session& s = session->s;
  const Route route = session->route;
  const std::string clientId = session->clientId;
  const std::string platformUrl = session->platformUrl;
  const uint64_t group = slot->handle;
  const std::string gidHex = slot->gidHex;
  // THIS GROUP'S GATE was adopted with its slot; every action it runs was queued for this
  // incarnation (review M1; the conversation-list design 2.2)
  const uint64_t incarnation = slot->incarnation;
  char* err = nullptr;
  urnw::LogInfo("live: group {} at epoch {}, open on the server: {}", gidHex,
                urnet_message_group_epoch(group),
                urnet_message_group_is_open(group) ? "yes" : "no");

  // ── fetch, forever ──────────────────────────────────────────────────────────
  // A LOOP THAT A PUSH WAKES. The server announces each new record to a subscribed
  // connection (spec B 4.3.5, ledger 269) and the loop answers with the same fetch
  // it polls with, so a push changes WHEN a record is read and never HOW. It runs
  // until this device leaves its group (ledger 273), and otherwise for the life of the
  // process; the worker is detached and the OS reclaims it at
  // exit, which is why Session::Close below is only reached on the paths that give up.
  urnw::LogInfo("live: entering the fetch loop, every {} ms until the push subscription holds", kFetchPollMs);
  int64_t lastPublishedCount = -1;
  // THE PUSH (ledger 269): the device's one waiter wakes this loop the moment the server announces
  // a record for this group (RoutePushToLoops), and the subscription state the loop keeps current
  // below
  // the development switch above, read once per pass; it fires at most once per pass
  const int64_t devLeaveAfterMs = DevLeaveAfterMs();
  const int64_t passStartedMs = NowMs();
  bool devLeaveQueued = false;
  bool subscribed = false;
  std::string lastSubscribeError;

  // WHAT THIS DEVICE HAS TRIED TO SEND AND THE SERVER HAS NOT TAKEN. Owned by this thread and by
  // nothing else — the UI hands over octets through the queue and reads the result back as a
  // published world, so this vector needs no lock. See LiveWorld.h for why a row that is not a
  // record is still not a fabrication.
  std::vector<urmsg::live::LiveOutboxEntry> outbox;
  // And the reactions and un-reactions it has tried. Same ownership, same reason.
  std::vector<urmsg::live::LiveReactionOutboxEntry> reactionOutbox;
  // And the deletions it has asked for. Same ownership, same reason.
  std::vector<urmsg::live::LiveDeleteOutboxEntry> deleteOutbox;
  // And the role changes it has asked for (item 242 R3). Same ownership, same reason.
  std::vector<urmsg::live::LiveRoleOutboxEntry> roleOutbox;
  // The roster line last logged, so the log carries a roster only when it moved.
  std::string lastRosterLine;
  std::string lastRouteLine;

  // Build the world off the group AS IT STANDS and hand it to the UI. Every publish below goes
  // through here, so the log and the outbox can never be drawn from two different moments — a send
  // that succeeded between them would otherwise render as both a record and a pending row.
  // A REFUSED LEAVE'S SENTENCE, drawn at the foot of the conversation until the next attempt, and
  // the flag a granted one sets (ledger 273): drainSends sets it and the loop below reads it.
  std::string leaveError;
  bool left = false;
  // WHAT THIS GROUP'S LAST RECEIVE REPORTED, or "" (the conversation-list design 2.1): drawn at the
  // foot of this conversation and no other, and cleared by the next receive that reports nothing.
  std::string receiveError;
  // FALSE UNTIL THIS GROUP'S FIRST RECEIVE: a restored group, or one joined above epoch 1, will not
  // seal before it (urnetwork_message.h), so its gate stays closed and nothing is queued for it.
  bool gateMayOpen = false;
  // A GROUP THIS DEVICE WAS REMOVED FROM (urnet_message_group_removal; the conversation-list design
  // 2.1): read at adoption, after a receive that failed, and after the sends. Once it says so it never
  // clears, and the group turns read-only: no subscription, no fetch, its gate closed, its transcript
  // kept. A leave is still served: a removed device can erase its own copy.
  bool removed = false;
  uint64_t removedEpoch = 0;
  // Answers true the moment the removal is first read, so the caller publishes it once.
  auto readRemoval = [&]() -> bool {
    if (removed) return false;
    const nlohmann::json r =
        nlohmann::json::parse(TakeString(urnet_message_group_removal(group)), nullptr, false);
    if (r.is_discarded() || !r.is_object() || !r.contains("removed") || !r["removed"].is_boolean() ||
        !r["removed"].get<bool>())
      return false;
    removed = true;
    if (r.contains("removed_epoch") && r["removed_epoch"].is_number_unsigned())
      removedEpoch = r["removed_epoch"].get<uint64_t>();
    SetGateOpen(gidHex, false);
    urnw::LogWarn("live: *** REMOVED *** this device was removed from group {} after epoch {}; the "
                  "conversation turns read-only and its loop stops fetching",
                  gidHex, removedEpoch);
    return true;
  };

  auto publishWorld = [&](bool logIfChanged) {
    // THE WHOLE LOG, not just what a fetch answered — a reaction and a tombstone change a message
    // that ALREADY arrived and are never in the fetch's own list, so a reader of the fetch alone
    // never sees either.
    urmsg::live::LiveGroup live;
    live.groupIdHex = gidHex;
    live.adoptedAtMs = slot->adoptedAtMs;
    live.groupOfOne = IsGroupOfOne(*slot);
    live.removed = removed;
    live.removedEpoch = removedEpoch;
    live.receiveError = receiveError;
    live.epoch = urnet_message_group_epoch(group);
    live.open = urnet_message_group_is_open(group);
    live.clientId = clientId;
    live.serverClientId = kServerClientId;
    live.platformUrl = platformUrl;
    live.host = RouteLabel(s, route);
    live.keyPinned = s.routeClient;
    if (s.routeClient) {
      if (const std::string line = TakeString(urnet_message_route_client_status(s.client));
          line != lastRouteLine) {
        lastRouteLine = line;
        urnw::LogInfo("live: ROUTE {}", line);
      }
    }
    live.statsJson = TakeString(urnet_message_group_stats(group));
    live.outbox = outbox;
    live.reactionOutbox = reactionOutbox;
    live.deleteOutbox = deleteOutbox;
    live.roleOutbox = roleOutbox;
    live.leaveError = leaveError;
    CollectMessages(group, live.messages);
    // THE ROSTER AND THIS DEVICE'S ROLE, on every publish and not once at the open: a role change
    // is a commit another member makes, and it lands here on some later fetch as a moved row.
    CollectMembers(group, live.members);
    live.myRole = ReadMyRole(group);
    if (const std::string roster = RosterLine(live); roster != lastRosterLine) {
      lastRosterLine = roster;
      urnw::LogInfo("live: ROSTER {}", roster);
    }

    // THE SEND BUTTON'S ONE GATE, re-read off the library every time rather than latched when the
    // group opened: a group the server has closed under us stops accepting sends, and a button
    // that learns that only from a failed click is the enabled-but-dead control design §9.1 bans.
    // AND NOT BEFORE THIS GROUP'S FIRST RECEIVE (gateMayOpen, below), AND NEVER IN A GROUP THAT
    // REMOVED THIS DEVICE.
    SetGateOpen(gidHex, gateMayOpen && live.open && !removed);

    // Log a changed log, and only a changed one: this loop runs every three
    // seconds for the life of the process and an unconditional dump would bury
    // the session it is evidence about.
    if (logIfChanged && static_cast<int64_t>(live.messages.size()) != lastPublishedCount) {
      lastPublishedCount = static_cast<int64_t>(live.messages.size());
      LogMessages(live);
      urnw::LogInfo("live: group stats {}", live.statsJson);
    }

    // PUBLISH EVERY TIME, changed or not. The count is a poor change detector —
    // a reaction landing on an existing message, or a tombstone, moves nothing —
    // and the UI's own generation counter already drops a beat it has drawn.
    PublishSlot(*slot, live);
  };

  // ── the send verb, on the thread that owns the handles ──────────────────────
  //
  // THREE PUBLISHES PER BATCH AND EACH ONE IS A DIFFERENT SENTENCE:
  //   before the call  — "Sending", so the composer empties into a row the reader can see rather
  //                      than into nothing for however long the server takes;
  //   after each call  — the record itself (it is in this device's own log the instant the submit
  //                      is acknowledged, so the pending row is dropped in the same beat it
  //                      appears), or "Not sent" carrying the library's own reason;
  //   the loop's own   — everything the far side has said since.
  auto drainSends = [&] {
    std::deque<Outbound> queued = TakeSlotQueue(*slot);
    // NOTHING QUEUED FOR ANOTHER GROUP, OR FOR AN EARLIER INCARNATION OF THIS ONE, IS DONE HERE
    // (review M1; the conversation-list design 2.2): it was asked of a group this device has since
    // left, or of another conversation, and done here it would be done to the wrong one.
    const size_t asked = queued.size();
    const std::vector<urmsg::live::GroupGate> gates = GatesSnapshot();
    queued.erase(std::remove_if(queued.begin(), queued.end(),
                                [&](Outbound const& o) {
                                  return o.groupIdHex != gidHex || o.incarnation != incarnation ||
                                         !urmsg::live::ActionStillHeld(gates, o.groupIdHex,
                                                                       o.incarnation);
                                }),
                 queued.end());
    if (queued.size() != asked)
      urnw::LogWarn("live: {} queued action(s) were for a group this device has left; dropped",
                    asked - queued.size());
    if (queued.empty()) return;

    for (auto& out : queued) {
      // A LEAVE ADDS NO ROW: it is granted, and the conversation goes, or it is refused, and the
      // refusal is drawn at the foot (leaveError). Nor does an add: its outcome is the group's add
      // state.
      if (out.kind == OutboundKind::Leave || out.kind == OutboundKind::AddMember) continue;
      // THE THREE COMMIT VERBS, and the name stays `isRole` because what it selects is "this
      // entry's outcome is drawn as a note under a MEMBER", which is true of a removal too.
      const bool isRole = out.kind == OutboundKind::SetRole ||
                          out.kind == OutboundKind::TransferOwnership ||
                          out.kind == OutboundKind::RemoveMember;
      if (isRole) {
        // ONE ATTEMPT PER MEMBER ON SCREEN, for the same reason a reaction retry supersedes its
        // failure: a second press after a refusal is that change asked for again, not a second
        // note under the first.
        roleOutbox.erase(std::remove_if(roleOutbox.begin(), roleOutbox.end(),
                                        [&](urmsg::live::LiveRoleOutboxEntry const& e) {
                                          return e.done && e.identityPub == out.identityPub;
                                        }),
                         roleOutbox.end());
        urmsg::live::LiveRoleOutboxEntry entry;
        entry.localId = out.localId;
        entry.identityPub = out.identityPub;
        entry.role = out.role;
        entry.attemptedAtMs = WallClockMs();
        roleOutbox.push_back(std::move(entry));
        continue;
      }
      if (out.kind == OutboundKind::Delete) {
        // ONE ATTEMPT PER LINE ON SCREEN: a second press on a line whose last deletion failed is
        // that deletion again, not a second note beside the first failure.
        deleteOutbox.erase(std::remove_if(deleteOutbox.begin(), deleteOutbox.end(),
                                          [&](urmsg::live::LiveDeleteOutboxEntry const& e) {
                                            return e.targetId == out.targetHex;
                                          }),
                           deleteOutbox.end());
        urmsg::live::LiveDeleteOutboxEntry entry;
        entry.localId = out.localId;
        entry.targetId = out.targetHex;
        entry.attemptedAtMs = WallClockMs();
        deleteOutbox.push_back(std::move(entry));
        continue;
      }
      const bool isReaction =
          out.kind == OutboundKind::ReactAdd || out.kind == OutboundKind::ReactRemove;
      if (isReaction) {
        // ONE ATTEMPT PER (target, emoji) ON SCREEN. A second tap on an emoji whose last attempt
        // failed is that attempt again, not a second reaction beside the first failure - so the
        // failed entry it supersedes goes in the same beat, exactly as a text retry replaces its
        // failed row.
        reactionOutbox.erase(
            std::remove_if(reactionOutbox.begin(), reactionOutbox.end(),
                           [&](urmsg::live::LiveReactionOutboxEntry const& e) {
                             return e.failed && e.targetId == out.targetHex && e.emoji == out.emoji;
                           }),
            reactionOutbox.end());
        urmsg::live::LiveReactionOutboxEntry entry;
        entry.localId = out.localId;
        entry.targetId = out.targetHex;
        entry.emoji = out.emoji;
        entry.remove = out.kind == OutboundKind::ReactRemove;
        entry.attemptedAtMs = WallClockMs();
        reactionOutbox.push_back(std::move(entry));
        continue;
      }
      // A RETRY SUPERSEDES THE ENTRY IT CAME FROM rather than joining it. Without this the failed
      // row stays on screen beside the second attempt and one message reads as two.
      //
      // AND IT INHERITS THE PARENT. The two [ Try again ] buttons name the row and nothing else -
      // MessageRow has no reply-to slot - so a retry of a failed REPLY arrives here as a plain
      // text naming the failed entry, and the entry is the one place that still knows which line
      // it answered. Taken from there, before the entry goes, so the retry seals as the same
      // reply rather than as a text that happens to have the same words.
      if (!out.replaces.empty()) {
        auto old = std::find_if(outbox.begin(), outbox.end(),
                                [&](urmsg::live::LiveOutboxEntry const& e) {
                                  return e.localId == out.replaces;
                                });
        if (old != outbox.end() && out.targetHex.empty() && !old->replyToId.empty() &&
            FromHexId(old->replyToId, out.target)) {
          out.kind = OutboundKind::Reply;
          out.targetHex = old->replyToId;
        }
        outbox.erase(std::remove_if(outbox.begin(), outbox.end(),
                                    [&](urmsg::live::LiveOutboxEntry const& e) {
                                      return e.localId == out.replaces;
                                    }),
                     outbox.end());
      }
      urmsg::live::LiveOutboxEntry entry;
      entry.localId = out.localId;
      entry.body = out.body;
      entry.replyToId = out.targetHex;
      entry.attemptedAtMs = WallClockMs();
      outbox.push_back(std::move(entry));
    }
    publishWorld(false);

    for (auto const& out : queued) {
      // FIRST IN THE LOOP, because nothing queued behind a granted leave is for a group this
      // device still holds: it ends the batch and the pass.
      if (out.kind == OutboundKind::Leave) {
        if (LeaveGroup(s, group, out.identityPub, leaveError)) {
          left = true;
          return;
        }
        // REFUSED: nothing changed, and another leave of this group may be asked for
        SetGateLeaving(gidHex, false);
        publishWorld(false);
        continue;
      }
      // THE ADD, this group's own (the conversation-list design 2.3): the founding add of a group of
      // one, which opens it, and every add after it. The roster moved, so the world is published.
      if (out.kind == OutboundKind::AddMember) {
        AddMemberFromCode(s, group, gidHex, out.body);
        if (urnet_message_group_is_open(group)) SetGroupOfOne(*slot, false);
        publishWorld(true);
        continue;
      }
      char* sendErr = nullptr;
      const int64_t startedMs = NowMs();
      std::string info;
      const char* verb = "send";
      // THE TWO ROLE VERBS ANSWER A KIND, NOT A HANDLE (urnetwork_message.h: "BRANCH ON THE KIND
      // AND SHOW THE TEXT"), so they are handled here before the switch that reads `info`. OK
      // drops the entry - the roster read on the publish below already shows the change; every
      // other kind is kept on the entry with the library's own sentence, for the rail to draw as
      // the outcome it is. The commit is one round trip inside the call, like a send.
      if (out.kind == OutboundKind::SetRole || out.kind == OutboundKind::TransferOwnership ||
          out.kind == OutboundKind::RemoveMember) {
        const bool transfer = out.kind == OutboundKind::TransferOwnership;
        const bool removal = out.kind == OutboundKind::RemoveMember;
        verb = removal ? "remove_member" : transfer ? "transfer_ownership" : "set_role";
        // THE REMOVAL TAKES NO ROLE, which is why it is a third arm here rather than a set_role
        // with a different string: urnet_message_group_remove_member's only member argument is the
        // identity, and every leaf that identity holds goes in the one commit it builds.
        const int32_t kind =
            removal  ? urnet_message_group_remove_member(group, s.ctx,
                                                         out.identityPub.c_str(), &sendErr)
            : transfer ? urnet_message_group_transfer_ownership(group, s.ctx,
                                                                out.identityPub.c_str(), &sendErr)
                       : urnet_message_group_set_role(group, s.ctx, out.identityPub.c_str(),
                                                      out.role.c_str(), &sendErr);
        const std::string failure = TakeError(&sendErr);
        const int64_t tookMs = NowMs() - startedMs;
        auto at = std::find_if(roleOutbox.begin(), roleOutbox.end(),
                               [&](urmsg::live::LiveRoleOutboxEntry const& e) {
                                 return e.localId == out.localId;
                               });
        const char* kindName = kind == URNET_MESSAGE_COMMIT_OK        ? "OK"
                               : kind == URNET_MESSAGE_COMMIT_REFUSED ? "REFUSED"
                               : kind == URNET_MESSAGE_COMMIT_LOST    ? "LOST"
                               : kind == URNET_MESSAGE_COMMIT_INVALID ? "INVALID"
                                                                      : "FAILED";
        if (kind == URNET_MESSAGE_COMMIT_OK) {
          // The removal's line names no role, because it asked for none. Printing `-> member`
          // beside a member that is no longer in the group would be the log claiming the opposite
          // of what the call did.
          urnw::LogInfo("live: *** {} *** {}{} answered OK in {} ms; the group is at epoch {}",
                        removal    ? "MEMBER REMOVED"
                        : transfer ? "OWNERSHIP TRANSFERRED"
                                   : "ROLE SET",
                        out.identityPub, removal ? std::string() : " -> " + out.role, tookMs,
                        urnet_message_group_epoch(group));
          if (at != roleOutbox.end()) roleOutbox.erase(at);
        } else {
          urnw::LogError("live: {} {}{} answered {} after {} ms: {}", verb, out.identityPub,
                         removal ? std::string() : " -> " + out.role, kindName, tookMs,
                         failure.empty() ? "no reason given" : failure);
          if (at != roleOutbox.end()) {
            at->done = true;
            at->kind = kind;
            at->error = failure;
          }
        }
        publishWorld(false);
        continue;
      }
      switch (out.kind) {
        case OutboundKind::Text:
          // COUNTED OCTETS, NOT A char*. The body is whatever was typed and a UTF-8 encoding of it
          // can hold a 0x00 nowhere except by a caller putting one there — but the ABI's rule for
          // every binary value going in is a pointer and a length (cgo/ctest/message_abi_test.c's
          // 21-octet body is two NULs and two multi-byte sequences precisely to hold this), and a
          // length is what is passed here so that the rule is kept rather than relied on.
          info = TakeString(urnet_message_group_send(
              group, s.ctx, reinterpret_cast<const uint8_t*>(out.body.data()),
              static_cast<int32_t>(out.body.size()), &sendErr));
          break;
        case OutboundKind::Reply:
          // The parent as 32 counted octets, decoded at the queue. The library does not require
          // the parent to be present - it may have been deleted or pruned - so a reply to a line
          // that has since vanished is sealed all the same, and the far side names what it cannot
          // show exactly as this side does.
          verb = "send_reply";
          info = TakeString(urnet_message_group_send_reply(
              group, s.ctx, out.target.data(), static_cast<int32_t>(out.target.size()),
              reinterpret_cast<const uint8_t*>(out.body.data()),
              static_cast<int32_t>(out.body.size()), &sendErr));
          break;
        case OutboundKind::ReactAdd:
          // The emoji IS a NUL-terminated char* here, and that is the ABI's own rule for this one
          // argument (urnetwork_message.h: "the emoji is a NUL-terminated utf-8 string"), unlike
          // every binary value above. It is checked as 1..64 octets of valid UTF-8 before anything
          // is sealed, and refused by name otherwise.
          verb = "react";
          info = TakeString(urnet_message_group_react(group, s.ctx, out.target.data(),
                                                      static_cast<int32_t>(out.target.size()),
                                                      out.emoji.c_str(), &sendErr));
          break;
        case OutboundKind::ReactRemove:
          verb = "unreact";
          info = TakeString(urnet_message_group_unreact(group, s.ctx, out.target.data(),
                                                        static_cast<int32_t>(out.target.size()),
                                                        out.emoji.c_str(), &sendErr));
          break;
        case OutboundKind::Delete:
          // The target as 32 counted octets, decoded at the queue. The library answers the
          // tombstone's own message info, or refuses a target this device did not write.
          verb = "delete";
          info = TakeString(urnet_message_group_delete(group, s.ctx, out.target.data(),
                                                       static_cast<int32_t>(out.target.size()),
                                                       &sendErr));
          break;
        case OutboundKind::SetRole:
        case OutboundKind::TransferOwnership:
        case OutboundKind::RemoveMember:
        case OutboundKind::Leave:
          break;  // handled above; unreachable
      }
      const std::string failure = TakeError(&sendErr);
      const int64_t tookMs = NowMs() - startedMs;

      if (out.kind == OutboundKind::Delete) {
        auto at = std::find_if(deleteOutbox.begin(), deleteOutbox.end(),
                               [&](urmsg::live::LiveDeleteOutboxEntry const& e) {
                                 return e.localId == out.localId;
                               });
        if (!info.empty()) {
          // the tombstone's own record id and message_id, and no body: the TARGET changes and the
          // next publish draws it as the placeholder
          urnw::LogInfo("live: *** DELETED *** message {} for everyone in {} ms: {}", out.targetHex,
                        tookMs, info);
          if (at != deleteOutbox.end()) deleteOutbox.erase(at);
        } else {
          urnw::LogError("live: delete of message {} was REFUSED after {} ms: {}", out.targetHex,
                         tookMs, failure.empty() ? "no reason given" : failure);
          if (at != deleteOutbox.end()) {
            at->failed = true;
            at->error = failure.empty() ? std::string("the library refused the deletion and gave no reason")
                                        : failure;
          }
        }
        publishWorld(false);
        continue;
      }
      if (out.kind == OutboundKind::ReactAdd || out.kind == OutboundKind::ReactRemove) {
        auto at = std::find_if(reactionOutbox.begin(), reactionOutbox.end(),
                               [&](urmsg::live::LiveReactionOutboxEntry const& e) {
                                 return e.localId == out.localId;
                               });
        if (!info.empty()) {
          // WHAT COMES BACK IS NOT A LINE: the record id and message_id of the reaction record
          // itself, which changes the TARGET and adds nothing to the conversation. The entry is
          // dropped, and the change is read off the target on the publish that follows.
          urnw::LogInfo("live: *** {} *** {} on message {} in {} ms: {}",
                        out.kind == OutboundKind::ReactAdd ? "REACTED" : "UNREACTED",
                        LogBodies() ? out.emoji : std::string("a reaction"), out.targetHex,
                        tookMs, info);
          if (at != reactionOutbox.end()) reactionOutbox.erase(at);
        } else {
          urnw::LogError("live: {} {} on message {} was REFUSED after {} ms: {}", verb,
                         LogBodies() ? out.emoji : std::string("a reaction"), out.targetHex,
                         tookMs, failure.empty() ? "no reason given" : failure);
          if (at != reactionOutbox.end()) {
            at->failed = true;
            at->error = failure.empty()
                            ? std::string("the library refused the ") + verb + " and gave no reason"
                            : failure;
          }
        }
        publishWorld(false);
        continue;
      }

      auto at = std::find_if(
          outbox.begin(), outbox.end(),
          [&](urmsg::live::LiveOutboxEntry const& e) { return e.localId == out.localId; });
      if (!info.empty()) {
        // The info json carries the record id, the message_id and body_len — and no body. On a
        // reply it also carries reply_to_id, which is the parent this side named; the far side
        // prints the same id, and the two agreeing is the whole proof.
        urnw::LogInfo("live: *** SENT *** {} octets via {} in {} ms: {}", out.body.size(), verb,
                      tookMs, info);
        if (at != outbox.end()) outbox.erase(at);
      } else {
        urnw::LogError("live: {} of {} octets was REFUSED after {} ms: {}", verb, out.body.size(),
                       tookMs, failure.empty() ? "no reason given" : failure);
        if (at != outbox.end()) {
          at->failed = true;
          // A refusal with no out_error is a library bug, not an empty reason — but the row still
          // has to say something, and "it failed and would not say why" is the true sentence.
          at->error = failure.empty() ? "the library refused the send and gave no reason" : failure;
        }
      }
      publishWorld(false);
    }
  };

  // THE ROW FIRST, off the group as this device holds it, with the gate kept closed: the list shows
  // the conversation while its first receive is still on the wire. A removal persisted while the app
  // was closed is read first, so the row is right from the start.
  readRemoval();
  publishWorld(false);

  for (;;) {
    // SENDS FIRST, BEFORE THE FETCH. A fetch takes as long as the server takes and the person who
    // just pressed Send is watching the composer; putting the send behind it would add a whole
    // round trip to every message this app writes.
    drainSends();
    if (left) break;

    // A SEND OR A COMMIT THE GROUP REFUSED MAY HAVE BEEN ITS REMOVAL: read it, publish it once, and
    // from then on fetch nothing (the library serves a removed device nothing above its last epoch).
    if (readRemoval()) publishWorld(false);
    if (removed) {
      WaitSlot(*slot, kFetchPollMs);
      continue;
    }

    // A GROUP OF ONE HAS NOTHING TO FETCH: it is not open on the server until its first member is
    // added, and that add is served by drainSends above. Its row stands, and it waits for the add.
    if (IsGroupOfOne(*slot)) {
      WaitSlot(*slot, kFetchPollMs);
      continue;
    }

    // SUBSCRIBE WHEN NOT CURRENT: no subscription yet, an epoch that moved past the one it was
    // authorized at, or a reconnect whose Hello the transport sent by itself. Cheap when current,
    // and the receive below reads whatever arrived before a new subscription took hold.
    {
      const int32_t subscribedNow = urnet_message_group_ensure_subscribed(group, s.ctx, &err);
      if (0 < subscribedNow) {
        urnw::LogInfo("live: subscribed to push at epoch {}", urnet_message_group_epoch(group));
        subscribed = true;
        lastSubscribeError.clear();
      } else if (subscribedNow < 0) {
        const std::string why = TakeError(&err);
        if (why != lastSubscribeError) {
          urnw::LogWarn("live: push subscription refused, polling every {} ms: {}", kFetchPollMs, why);
          lastSubscribeError = why;
        }
        subscribed = false;
      }
    }

    const int64_t fetchStartedMs = NowMs();
    const uint64_t fetched = urnet_message_group_receive(group, s.ctx, &err);
    const std::string fetchError = TakeError(&err);
    const int32_t fetchedCount = urnet_message_list_count(fetched);
    if (!fetchError.empty()) {
      // Code that reads this as "nothing arrived" drops real messages: a page
      // bound reached with more to come, a server that named a high water above
      // what it handed over, and a record given up on after every retry are all
      // answers that carry messages AND a reason.
      urnw::LogWarn("live: group_receive reported: {} (and still answered {} record(s))",
                    fetchError, fetchedCount);
    }
    // THIS CONVERSATION DRAWS IT, and only this one: a receive that reported nothing takes it away.
    // DRAWN WITH THE APP'S FOLDERS NAMED, NOT SPELLED (lead ruling R71): the log line above keeps the
    // library's sentence whole, and the window never draws a path under the user's profile.
    const std::string drawn =
        fetchError.empty()
            ? std::string()
            : urmsg::live::DrawnReceiveReport(fetchError, session->appRoots, session->profileRoot);
    if (drawn != receiveError) {
      urnw::LogInfo("live: group {}'s receive report is {}; its conversation draws it", gidHex,
                    drawn.empty() ? "cleared" : "standing");
      receiveError = drawn;
    }
    if (0 < fetchedCount) {
      urnw::LogInfo("live: group_receive answered {} NEW record(s) in {} ms", fetchedCount,
                    NowMs() - fetchStartedMs);
    }
    if (fetched != 0) urnet_release(fetched);
    // A RECEIVE MAY HAVE CARRIED THE COMMIT THAT REMOVED THIS DEVICE, and the next one fails by that
    // name: the removal is read after every receive, before the publish below
    readRemoval();

    // THE FIRST RECEIVE IS DONE, so the gate may open from here on.
    gateMayOpen = true;
    publishWorld(true);

    if (0 < devLeaveAfterMs && !devLeaveQueued && devLeaveAfterMs <= NowMs() - passStartedMs) {
      devLeaveQueued = true;
      urnw::LogWarn("live: DEV: URMESSAGE_DEV_LEAVE_AFTER_MS ({} ms) reached; a plain leave was {}",
                    devLeaveAfterMs, QueueLeave(gidHex) ? "queued" : "REFUSED");
    }

    // NOT ::Sleep. A send queued during the wait wakes this at once, and so does a push; with
    // neither it ticks on its own timer, which is long when the push subscription holds.
    WaitSlot(*slot, subscribed ? kPushedPollMs : kFetchPollMs);
  }

  // ── THIS DEVICE LEFT THIS GROUP (ledger 273) ─────────────────────────────────────────────────
  // The library has closed the group and erased this device's copy of it. What is left here is the
  // handle, the gate and the conversation, and RetireSlot takes all three. Noted as left first, so no
  // reconciliation takes it up again behind the leave.
  {
    std::lock_guard<std::mutex> lock(g_tableMutex);
    g_leftGroups.insert(gidHex);
  }
  const bool empty = RetireSlot(*slot, "This computer left the conversation.");
  urnw::LogInfo("live: left group {}; {}", gidHex,
                empty ? "this device holds no group now and waits for an invitation"
                      : "the other conversations go on");
}

// TAKE A GROUP UP: a slot, then its gate, then its loop. Every group the library hands over comes
// through here: each one restore answers, the one a join or a create makes, and each one a held
// leave's restore brings back (A2, A10).
void StartGroupLoop(std::shared_ptr<SessionContext> const& session, uint64_t group,
                    bool groupOfOne = false) {
  const std::string gidHex = GroupIdHexOf(group);
  if (gidHex.empty()) {
    urnw::LogError("live: a group was handed over with no id; it is released, not served");
    urnet_release(group);
    return;
  }
  auto slot = std::make_shared<GroupSlot>();
  slot->gidHex = gidHex;
  slot->handle = group;
  slot->adoptedAtMs = WallClockMs();
  slot->groupOfOne = groupOfOne;
  {
    std::lock_guard<std::mutex> lock(g_tableMutex);
    for (auto const& held : g_slots) {
      if (held->gidHex == gidHex) {
        // ONE LOOP PER GROUP: a second handle on a group already served is released, never
        // served twice
        urnw::LogWarn("live: group {} already has a loop; the second handle is released", gidHex);
        urnet_release(group);
        return;
      }
    }
    g_slots.push_back(slot);
    // TAKEN UP AGAIN, by a join or a create a person asked for: no longer a group this session left
    g_leftGroups.erase(gidHex);
  }
  // THE SLOT FIRST, THEN THE GATE: a verb is admitted only by a gate, so every admitted verb finds a
  // slot to queue into.
  slot->incarnation = AdoptGate(gidHex);
  g_slotsChanged.notify_all();
  urnw::LogInfo("live: group {} taken up (incarnation {}{}); its loop starts", gidHex,
                slot->incarnation, groupOfOne ? ", a group of one" : "");
  // A LOOP THAT THROWS ENDS ITS OWN GROUP AND NO OTHER, the way the session's worker ends the
  // session (StartIfEnabled): logged, its gate dark and its conversation gone, so nothing is queued
  // for a group nobody serves. The neutral state names nothing, because nothing was left.
  std::thread loop = urnw::StartGuardedThread("urmessage-live-group", [session, slot] {
    try {
      GroupLoop(session, slot);
    } catch (const std::exception& e) {
      urnw::LogError("live: the loop of group {} ended with an exception: {}", slot->gidHex,
                     e.what());
      RetireSlot(*slot, std::string());
    } catch (...) {
      urnw::LogError("live: the loop of group {} ended with a non-std exception", slot->gidHex);
      RetireSlot(*slot, std::string());
    }
  });
  // DETACHED, as the session's worker is: nothing waits on a loop, and it ends with its group.
  loop.detach();
}

// THE TABLE AGAINST THE LIBRARY (the conversation-list design 2.1): every group the device holds open
// has a loop. Asked after the device-level calls that can open a group (the startup restore, a join,
// a create, a held leave): a group the library holds that no loop serves is taken up, and a handle on
// one already served, or on one this session left, is released quietly. The library leaves system
// groups out of this list, so a self group is never taken up.
void ReconcileTable(std::shared_ptr<SessionContext> const& session) {
  const uint64_t list = urnet_message_device_groups(session->s.device);
  const int32_t count = urnet_message_group_list_count(list);
  int taken = 0;
  for (int32_t i = 0; i < count; ++i) {
    const uint64_t group = urnet_message_group_list_at(list, i);
    if (group == 0) continue;
    const std::string gid = GroupIdHexOf(group);
    bool served = gid.empty();
    if (!served) {
      std::lock_guard<std::mutex> lock(g_tableMutex);
      for (auto const& slot : g_slots)
        if (slot->gidHex == gid) served = true;
      if (g_leftGroups.count(gid) != 0) served = true;
    }
    if (served) {
      urnet_release(group);
      continue;
    }
    ++taken;
    StartGroupLoop(session, group);
  }
  if (list != 0) urnet_release(list);
  if (0 < taken)
    urnw::LogWarn("live: {} group(s) the library holds had no loop; each is taken up now", taken);
}

void RunSession() {
  const int64_t startedMs = NowMs();
  urnw::LogInfo("live: ==== live mesh session starting ====");

  // THE LOADER FIRST, AND BY HAND. The import is delay-loaded (App.vcxproj), so
  // a missing dll would otherwise surface as a structured exception at the first
  // ABI call — on a worker thread, with no useful text. Probing it here turns
  // that into one honest line naming the file and the Win32 error.
  if (::LoadLibraryW(L"URnetworkSdk.dll") == nullptr) {
    urnw::LogError(
        "live: URnetworkSdk.dll could not be loaded (GetLastError {}). It must sit next to "
        "URmessage.exe; App.vcxproj copies it there from app/third_party/urnetwork-sdk/bin/x64.",
        ::GetLastError());
    return;
  }
  urnw::LogInfo("live: URnetworkSdk.dll loaded; sdk version {}", TakeString(urnet_version()));

  // The leak check this used to close with is gone WITH THE ONE-SHOT SESSION:
  // the fetch loop below never returns, so there is no "at end" to compare
  // against. The count at the start is still worth a line — a non-zero one here
  // means a previous session in this process left handles behind.
  urnw::LogInfo("live: {} sdk handle(s) live before this session opened any", urnet_live_handle_count());

  // The credential. Read from disk, never logged.
  const std::filesystem::path credPath = CredentialPath();
  std::string credential;
  if (!ReadCredential(credPath, credential)) {
    urnw::LogError(
        "live: no usable credential at {} — the live path needs an operator-minted by_client_jwt "
        "at that path (or %URMESSAGE_LIVE_JWT% pointing at one). Nothing here mints one.",
        Utf8Path(credPath));
    // AND ON THE SCREEN, in the words the README uses, because a log line is not where a person
    // who skipped a step will look. The path is the one this launch actually read.
    PublishOnboard(urmsg::live::OnboardStep::NoCredential, std::string{},
                   std::format("This computer has no URmessage credential yet. Put the "
                               "credential file you were sent in {} and name it {}, then start "
                               "URmessage again.",
                               Utf8Path(credPath.parent_path()),
                               Utf8Path(credPath.filename())));
    return;
  }
  urnw::LogInfo("live: credential read from {} ({} bytes; its contents are never logged)",
                Utf8Path(credPath), credential.size());

  // THIS DEVICE'S OWN STATE DIRECTORY, AND IT IS NEVER A COPY OF ANOTHER ONE.
  // A copy of an MLS state directory is a second device at one leaf: one
  // sender_handle and one stream counter shared by two writers, which reuses a
  // nonce under a reused record key. It lives under StorageRoot() so the
  // per-worktree %URMESSAGE_APP_ROOT% override isolates concurrent worktrees too.
  const std::filesystem::path liveRoot = urnw::StorageRoot() / L"live";
  const std::filesystem::path streamDir = liveRoot / L"stream";
  const std::filesystem::path stateDir = liveRoot / L"state";
  std::error_code ec;
  std::filesystem::create_directories(streamDir, ec);
  std::filesystem::create_directories(stateDir, ec);

  // THE SESSION ON THE HEAP, SHARED WITH EVERY GROUP LOOP (SessionContext): a loop holds it, so the
  // handles it uses stay valid however this function ends.
  auto session = std::make_shared<SessionContext>();
  Session& s = session->s;
  session->appRoots = {Utf8Path(stateDir), Utf8Path(streamDir), Utf8Path(liveRoot),
                       Utf8Path(urnw::StorageRoot())};
  session->profileRoot = urnw::Narrow(EnvVar(L"USERPROFILE"));
  char* err = nullptr;

  s.ctx = urnet_message_context_new();

  // instance_id NULL draws a fresh installation uuid each launch. The client_id
  // is the credential's either way, and it is the client_id the operator routes
  // to — so a fresh instance_id does not avoid the reconnect window below.
  //
  // THE ROUTE (ledger 268). URNETWORK and DIRECT reach the server's own endpoint with its key
  // pinned; the credential is used only by URNETWORK, to mint the tunnel's exit clients.
  const Route route = ChosenRoute();
  session->route = route;
  if (route == Route::Platform) {
    s.client = urnet_message_client_new(credential.c_str(), kHost, nullptr, nullptr, kAppVersion, &err);
  } else {
    s.routeClient = true;
    s.client = urnet_message_route_client_new(
        credential.c_str(), kHost, nullptr, kServerEndpoint, kServerEndpointPin,
        route == Route::Urnetwork ? URNET_MESSAGE_ROUTE_URNETWORK : URNET_MESSAGE_ROUTE_DIRECT,
        kAppVersion, &err);
  }
  if (s.client == 0) {
    urnw::LogError("live: the {} client was refused: {}", RouteName(route), TakeError(&err));
    s.Close();
    return;
  }
  // Scrub the credential from this frame now that the library has copied what it
  // needs. Belt and braces: it is one std::string in one thread, but a secret
  // that is gone cannot be logged by a line somebody adds later.
  ::SecureZeroMemory(credential.data(), credential.size());
  credential.clear();

  std::string clientId;
  std::string platformUrl;
  if (s.routeClient) {
    platformUrl = kServerEndpoint;
    urnw::LogInfo("live: route {} to {}, key pinned; {}", RouteName(route), kServerEndpoint,
                  route == Route::Urnetwork
                      ? "the connection leaves through a URnetwork exit, so the server never sees this computer's address"
                      : "DIRECT, so the message server sees this computer's address");
  } else {
    clientId = TakeString(urnet_message_client_id(s.client));
    platformUrl = TakeString(urnet_message_client_platform_url(s.client));
    urnw::LogInfo("live: client_id {} dialling {}", clientId, platformUrl);
  }
  session->clientId = clientId;
  session->platformUrl = platformUrl;

  s.transport = urnet_message_transport_new(s.client, kServerClientId,
                                            URNET_MESSAGE_PROTOCOL_VERSION, 30000, &err);
  if (s.transport == 0) {
    urnw::LogError("live: transport_new to server {}: {}", kServerClientId, TakeError(&err));
    s.Close();
    return;
  }

  s.streamStore = urnet_message_stream_store_open(Utf8Path(streamDir).c_str(), &err);
  if (s.streamStore == 0) {
    urnw::LogError(
        "live: stream_store_open {}: {} — if this says the directory is held, another client is "
        "already running against this state and MUST NOT be joined by a second one.",
        Utf8Path(streamDir), TakeError(&err));
    s.Close();
    return;
  }
  s.reserver = urnet_message_stream_index_reserver_new(s.streamStore);
  if (s.reserver == 0) {
    urnw::LogError("live: stream_index_reserver_new answered 0");
    s.Close();
    return;
  }
  s.stateStore = urnet_message_durable_state_store_open(Utf8Path(stateDir).c_str(), &err);
  if (s.stateStore == 0) {
    urnw::LogError("live: durable_state_store_open {}: {}", Utf8Path(stateDir), TakeError(&err));
    s.Close();
    return;
  }

  Hellos& hellos = session->hellos;
  s.device = urnet_message_device_new(s.transport, s.reserver, s.stateStore, kConnectBudgetMs,
                                      kConnectAttemptMs, &OnConnectAttempt, &hellos, &err);
  if (s.device == 0) {
    urnw::LogError("live: device_new: {}", TakeError(&err));
    s.Close();
    return;
  }
  urnw::LogInfo("live: device built; durable state in {}", Utf8Path(liveRoot));

  // ── say Hello, across the reconnect window ──────────────────────────────────
  const int64_t connectStartedMs = NowMs();
  bool connected = false;
  int connectCalls = 0;
  std::string lastError;
  for (int i = 0; i < kMaxConnectCalls && !connected; ++i) {
    ++connectCalls;
    urnw::LogInfo("live: device_connect call {} of {} (budget {} ms)", connectCalls,
                  kMaxConnectCalls, kConnectBudgetMs);
    if (urnet_message_device_connect(s.device, s.ctx, &err)) {
      connected = true;
      break;
    }
    lastError = TakeError(&err);
    // A SPENT BUDGET IS "NOT YET, ASK AGAIN" AND NOT A FAILURE. Saying
    // "could not connect" here would tell the user something false.
    if (i + 1 < kMaxConnectCalls) {
      const int64_t waitMs = kBackoffMs[i];
      urnw::LogInfo("live: not routed to yet after {} ms total ({}); re-sending in {} ms",
                    NowMs() - connectStartedMs, lastError.empty() ? "no reason given" : lastError,
                    waitMs);
      ::Sleep(static_cast<DWORD>(waitMs));
    }
  }
  const int64_t connectMs = NowMs() - connectStartedMs;

  if (!connected) {
    urnw::LogError(
        "live: not routed to after {} device_connect calls over {} ms and {} unanswered Hellos. "
        "Last reason: {}. This is the operator's reconnect window outlasting the retry ladder, "
        "which is a different finding from a broken credential.",
        connectCalls, connectMs, hellos.unanswered.load(),
        lastError.empty() ? "no reason given" : lastError);
    s.Close();
    return;
  }

  urnw::LogInfo(
      "live: *** CONNECTED to the alpha mesh *** after {} device_connect call(s), {} unanswered "
      "Hello(s), {} ms",
      connectCalls, hellos.unanswered.load(), connectMs);

  // THE SESSION'S FACTS, which every conversation of the world shares, and ONE PUSH WAITER FOR THE
  // DEVICE, routing each push to the loop of the group it names (the conversation-list design 2.1).
  {
    std::lock_guard<std::mutex> lock(g_tableMutex);
    g_liveSession.clientId = clientId;
    g_liveSession.serverClientId = kServerClientId;
    g_liveSession.platformUrl = platformUrl;
    g_liveSession.host = RouteLabel(s, route);
    g_liveSession.keyPinned = s.routeClient;
  }
  session->pushWaiter = std::make_unique<PushWaiter>(s.device);

  // ── restore, or join ────────────────────────────────────────────────────────
  const uint64_t restored = urnet_message_device_restore(s.device, s.ctx, &err);
  // A NON-ZERO LIST AND A NON-NULL out_error CAN BOTH COME BACK.
  const std::string restoreError = TakeError(&err);
  const int32_t restoredCount = urnet_message_group_list_count(restored);
  if (!restoreError.empty()) {
    urnw::LogWarn("live: device_restore reported: {} (and still answered {} group(s))",
                  restoreError, restoredCount);
  }
  urnw::LogInfo("live: device_restore rebuilt {} group(s) from durable state", restoredCount);
  // THE LIST'S NOTICE, before any group's loop publishes a world (A12): a group that did not restore
  // is said beside the ones that did, in Task 58's sentence for its class.
  SetRestoreNotice(urmsg::live::NextListNotice(RestoreNotice(), restoreError, restoredCount));

  // EVERY GROUP RESTORE ANSWERS GETS A LOOP OF ITS OWN (A2), and not only the first: the library
  // holds each of them open, and one not served here would be served by nobody.
  for (int32_t i = 0; i < restoredCount; ++i) {
    const uint64_t group = urnet_message_group_list_at(restored, i);
    if (group == 0) continue;
    // THE LINE A RESTORE HAS ALWAYS LOGGED, now once for each group it brings back: the screenshot
    // legs wait for it, and a launch that restores one group logs exactly what it did.
    urnw::LogInfo("live: restored an existing group from disk — no handshake needed");
    StartGroupLoop(session, group);
  }
  if (restored != 0) urnet_release(restored);
  // A GROUP FOUNDED BEFORE INVITATIONS WERE SEALED does not restore (ErrGroupPredatesJoinSeal), and
  // R9 has the app say so in a sentence, never in the library's words (Task 58). Nor does a group
  // whose record could not be read just now (ErrGroupRecordNotReadNow, R25 Q5). The join dialog's
  // first state opens with its sentence, and does not say this device is not in a group (R25 Q4).
  if (const std::string_view note = urmsg::live::RestoreNote(restoreError, restoredCount);
      !note.empty()) {
    urnw::LogWarn("live: device_restore refused a group ({}); the join dialog says so",
                  urmsg::live::JoinCopyName(urmsg::live::ClassifyJoinCopy(restoreError)));
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_afterLeaveNote = std::string(note);
    g_holdsUnopenedGroup = true;
  }
  // R28, R29: whether a held refusal after this restore offers the leave. Asked of the report
  // whatever came back, because a group that did come back is the one this device is in, and the one
  // a held refusal is about is another.
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_lastRestoreClass = urmsg::live::RestoreReadClass(restoreError);
  }

  // ── THE JOIN ROAD, for the life of the session (the conversation-list design 2.3) ──────────────
  // Beside every conversation this device holds, each served by its own loop: what the road joins,
  // founds or brings back is handed to a loop of its own, and it keeps waiting for the next
  // invitation. It returns only when this installation cannot make a join code, and the session
  // then ends as it did, closed by whoever lets go of it last (~SessionContext): here, unless a loop
  // still runs over its handles.
  const AdoptFn adopt = [&session](uint64_t group, bool groupOfOne) {
    StartGroupLoop(session, group, groupOfOne);
  };
  const ReconcileFn reconcile = [&session] { ReconcileTable(session); };
  // THE RESTORE ABOVE TOOK UP WHAT IT ANSWERED; the library may hold more
  reconcile();
  JoinRoad(s, adopt, reconcile);
}

}  // namespace

// No arguments at all: the launch a person makes by double-clicking the exe. LiveMesh.h says
// why that launch is live and every other keeps the explicit rule.
bool IsPlainLaunch() {
  int argc = 0;
  wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (argv == nullptr) return false;
  ::LocalFree(argv);
  return argc <= 1;
}

bool IsEnabled() {
  const std::wstring env = EnvVar(L"URMESSAGE_LIVE");
  if (env == L"1" || HasCommandLineFlag(L"--live")) return true;
  if (env == L"0") return false;
  return IsPlainLaunch();
}

bool RouteThroughUrnetwork() {
  const nlohmann::json prefs = urnw::LoadAppPrefs();
  // is_boolean() and not just contains(): a hand-edited prefs file must not choose the route by
  // throwing, and anything that is not a boolean leaves the default, which is ON
  if (prefs.contains(kRoutePrefKey) && prefs[kRoutePrefKey].is_boolean()) {
    return prefs[kRoutePrefKey].get<bool>();
  }
  return true;
}

void SetRouteThroughUrnetwork(bool on) {
  urnw::SaveAppPref(kRoutePrefKey, on);
  urnw::LogInfo("live: route_through_urnetwork -> {} (applies the next time the app connects)", on);
}

bool StartIfEnabled() {
  if (!IsEnabled()) return false;
  urnw::LogInfo("live: this launch is live ({}); starting the live mesh worker",
                HasCommandLineFlag(L"--live") || EnvVar(L"URMESSAGE_LIVE") == L"1"
                    ? "--live or %URMESSAGE_LIVE%=1"
                    : "a plain launch");

  // StartGuardedThread gives this thread the per-thread terminate handler MSVC
  // does not inherit across threads, so a death in here is named in the log
  // instead of being a silent abort(). Its failure path ENDS IN abort() rather
  // than recovering — which is why RunSession catches its own errors and
  // returns normally instead of throwing.
  std::thread worker = urnw::StartGuardedThread("urmessage-live-mesh", [] {
    try {
      RunSession();
    } catch (const std::exception& e) {
      urnw::LogError("live: session ended with an exception: {}", e.what());
    } catch (...) {
      urnw::LogError("live: session ended with a non-std exception");
    }
    // EVERY WAY OUT OF RunSession ENDS HERE, including the exceptional ones, and there is nothing
    // left that could make a send work — so the button must go dark. Written here rather than on
    // each of RunSession's nine early returns because a tenth one added later would silently miss
    // it, and the failure mode of missing it is an enabled Send whose clicks vanish.
    // ...BUT A GROUP STILL SERVED BY ITS OWN LOOP KEEPS ITS GATE: the loop holds the session's
    // handles and goes on serving its conversation (the conversation-list design 2.1).
    if (SlotCount() == 0) DropAllGates();
  });

  // DETACHED, AND NOT AS A SHORTCUT. This is called from the UI thread of a
  // single-threaded apartment, and std::thread::join() does not pump messages —
  // joining a worker that blocks for tens of seconds inside device_connect
  // would freeze the window for exactly that long. The worker owns its handles
  // and closes them itself, so there is nothing for anyone to wait on.
  worker.detach();
  return true;
}

bool CanSend(std::string const& groupIdHex) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  return urmsg::live::GateAction(g_gates, groupIdHex, urmsg::live::ActionClass::Commit) ==
         urmsg::live::GateVerdict::Take;
}

namespace {

// The one enqueue, into the queue of the group the action names. Everything the public verbs check
// has been checked by the time this runs. False when no loop serves that group any more (it ended
// between the gate's look and this one).
bool Enqueue(Outbound out) {
  std::shared_ptr<GroupSlot> slot = FindSlot(out.groupIdHex);
  if (!slot) {
    urnw::LogWarn("live: an action for group {} found no loop to take it; refused",
                  out.groupIdHex.substr(0, (std::min)(out.groupIdHex.size(), size_t{16})));
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(slot->mutex);
    // AN ADD REPLACES THE ADD STILL QUEUED FOR ITS GROUP, as a pasted code always replaced the one
    // before it: somebody who pastes twice has corrected themselves, and adding both would spend two
    // epochs on one intention.
    if (out.kind == OutboundKind::AddMember) {
      slot->queue.erase(std::remove_if(slot->queue.begin(), slot->queue.end(),
                                       [](Outbound const& o) { return o.kind == OutboundKind::AddMember; }),
                        slot->queue.end());
    }
    slot->queue.push_back(std::move(out));
  }
  // OUTSIDE THE LOCK: the loop wakes, takes the same mutex, and would be woken only to block on
  // the thread that woke it.
  slot->wake.notify_one();
  return true;
}

}  // namespace

bool QueueSend(std::string groupIdHex, std::string utf8Body, std::string replacesLocalId,
               std::string replyToMessageIdHex) {
  // REFUSED HERE RATHER THAN QUEUED AND REFUSED LATER, and the difference is what the caller can
  // do about it: a false answer lets the composer keep the text the person typed. A queued send
  // that the worker then refuses has already emptied the box.
  if (utf8Body.empty()) return false;
  uint64_t incarnation = 0;
  if (!Admit(groupIdHex, urmsg::live::ActionClass::Commit, incarnation)) return false;

  Outbound out;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  out.body = std::move(utf8Body);
  out.replaces = std::move(replacesLocalId);
  if (!replyToMessageIdHex.empty()) {
    // A parent that does not decode is refused BEFORE the box empties, for the reason above. It
    // cannot happen from the thread view, which only ever offers Reply on a row whose id came off
    // urnet_message_list_info - but a caller that hands over anything else gets a false, not a
    // send_reply that the library refuses by name a beat later.
    if (!FromHexId(replyToMessageIdHex, out.target)) {
      urnw::LogWarn("live: a reply named a parent that is not a 64-hex message_id ({} chars); "
                    "refused before it was queued",
                    replyToMessageIdHex.size());
      return false;
    }
    out.kind = OutboundKind::Reply;
    out.targetHex = std::move(replyToMessageIdHex);
  }
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  return Enqueue(std::move(out));
}

namespace {

// Lower-case hex of even, non-zero length: the ABI parses identity_pub_hex itself and answers
// INVALID otherwise, but a name that does not decode is refused BEFORE it is queued so the rail
// hears "false" rather than a note a beat later - the same rule the reply parent follows.
bool IsHex(std::string const& hex) {
  if (hex.empty() || hex.size() % 2 != 0) return false;
  for (char c : hex) {
    const bool ok = ('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
    if (!ok) return false;
  }
  return true;
}

}  // namespace

bool QueueRoleChange(std::string groupIdHex, std::string identityPubHex, std::string role) {
  uint64_t incarnation = 0;
  if (!Admit(groupIdHex, urmsg::live::ActionClass::Commit, incarnation)) return false;
  // The three roles set_role takes, by name. "owner" is refused here on purpose: the ABI answers
  // INVALID for it (ownership moves through transfer_ownership), and a caller that reached for
  // it has the wrong verb, not a bad member.
  if (role != "admin" && role != "member" && role != "observer") {
    urnw::LogWarn("live: a role change asked for \"{}\", which set_role does not take; refused "
                  "before it was queued",
                  role);
    return false;
  }
  if (!IsHex(identityPubHex)) {
    urnw::LogWarn("live: a role change named an identity that is not hex ({} chars); refused "
                  "before it was queued",
                  identityPubHex.size());
    return false;
  }
  Outbound out;
  out.kind = OutboundKind::SetRole;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  out.identityPub = std::move(identityPubHex);
  out.role = std::move(role);
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  return Enqueue(std::move(out));
}

bool QueueTransferOwnership(std::string groupIdHex, std::string identityPubHex) {
  uint64_t incarnation = 0;
  if (!Admit(groupIdHex, urmsg::live::ActionClass::Commit, incarnation)) return false;
  if (!IsHex(identityPubHex)) {
    urnw::LogWarn("live: a transfer named an identity that is not hex ({} chars); refused before "
                  "it was queued",
                  identityPubHex.size());
    return false;
  }
  Outbound out;
  out.kind = OutboundKind::TransferOwnership;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  out.identityPub = std::move(identityPubHex);
  out.role = "owner";
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  return Enqueue(std::move(out));
}

std::string RestoreNotice() {
  std::lock_guard<std::mutex> lock(g_noticeMutex);
  return g_restoreNotice;
}

AddPtr AddSnapshot(std::string const& groupIdHex) {
  std::lock_guard<std::mutex> lock(g_addMutex);
  for (auto const& add : g_adds)
    if (add->groupIdHex == groupIdHex) return add;
  return nullptr;
}

uint64_t AddGeneration() {
  return g_addGeneration.load(std::memory_order_acquire);
}

OnboardPtr OnboardSnapshot() {
  std::lock_guard<std::mutex> lock(g_onboardMutex);
  return g_onboard;
}

uint64_t OnboardGeneration() {
  return g_onboardGeneration.load(std::memory_order_acquire);
}

bool QueueJoinFromInviteCode(std::string base64Invite) {
  // DECODED HERE, AT THE QUEUE, AND THE RESULT THROWN AWAY. The worker decodes it again for real;
  // this call exists only so that text which cannot possibly be an invitation is refused while the
  // person is still looking at the box they pasted into. Same reason QueueSend decodes a reply's
  // parent at the queue rather than on the worker.
  std::vector<uint8_t> probe;
  if (!DecodeBase64(base64Invite, probe)) {
    urnw::LogWarn("live: a pasted invitation was not decodable ({} characters); refused before it "
                  "was queued",
                  base64Invite.size());
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    // REPLACES rather than queues. Somebody who pastes twice has corrected themselves; applying
    // both would try to join two groups with one device.
    g_pastedInvite = std::move(base64Invite);
  }
  urnw::LogInfo("live: an invitation of {} octets was handed to the worker", probe.size());
  return true;
}

bool QueueLeaveHeldGroup() {
  // ONLY WHILE IT IS ON OFFER: the state on screen says to leave a group this device holds and
  // cannot open, and nothing else asks for this. A press that lands after a later state withdrew
  // the offer leaves nothing.
  if (!g_heldLeaveOffered.load(std::memory_order_acquire)) {
    urnw::LogWarn("live: the leave of a group that is not live was asked for while none is on "
                  "offer; refused");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_heldLeaveAsked = true;
  }
  urnw::LogInfo("live: the leave of a group that is not live was handed to the worker");
  return true;
}

bool QueueOpenJoinRoad() {
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_openRoadAsked = true;
  }
  urnw::LogInfo("live: the join road was asked to open for the dialog");
  return true;
}

bool QueueNewJoinCode() {
  // ONLY WHILE IT IS ON OFFER: the state on screen asks for a new join code, and nothing else asks
  // for this. A press that lands after a later state withdrew the offer mints nothing.
  if (!g_newJoinCodeOffered.load(std::memory_order_acquire)) {
    urnw::LogWarn("live: a new join code was asked for while none is on offer; refused");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_newJoinCodeAsked = true;
  }
  urnw::LogInfo("live: a new join code was asked for and handed to the worker");
  return true;
}

bool QueueCreateGroup(std::string* waitingGroupIdHex) {
  // REFUSED ONLY WHILE A GROUP OF ONE WAITS (ConversationRules.h CreateAction; the conversation-list
  // design 2.3, A7). A device holds many conversations, and a create beside them makes one more; but
  // a group of one is lost on a restart and a second would be a second invitation to send with one
  // panel to send it from. The refusal names that group, so the dialog opens its add panel again.
  std::string waiting;
  if (urmsg::live::CreateAction(HeldForCreateSnapshot(), waiting) != urmsg::live::CreateVerdict::Take) {
    urnw::LogWarn("live: a create was asked for while group {} is still a group of one; refused",
                  waiting.substr(0, (std::min)(waiting.size(), size_t{16})));
    if (waitingGroupIdHex != nullptr) *waitingGroupIdHex = waiting;
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_createGroupAsked = true;
  }
  urnw::LogInfo("live: a group create was handed to the worker");
  return true;
}

bool QueueAddMemberFromCode(std::string groupIdHex, std::string base64JoinCode) {
  // Decoded here and the result thrown away, for the same reason QueueJoinFromInviteCode does it:
  // text that cannot be a join code is refused while the person is still looking at the box.
  std::vector<uint8_t> probe;
  if (!DecodeBase64(base64JoinCode, probe)) {
    urnw::LogWarn("live: a pasted join code was not decodable ({} characters); refused before it "
                  "was queued",
                  base64JoinCode.size());
    return false;
  }
  // THE GROUP'S OWN GATE: held, and no leave of it in flight. Not open: the first add of a group of
  // one is the commit that opens it (ConversationRules.h, ActionClass::Add).
  uint64_t incarnation = 0;
  if (!Admit(groupIdHex, urmsg::live::ActionClass::Add, incarnation)) return false;
  Outbound out;
  out.kind = OutboundKind::AddMember;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  // REPLACES rather than queues, like a pasted invitation (Enqueue): somebody who pastes twice has
  // corrected themselves, and adding both would spend two epochs on one intention.
  out.body = std::move(base64JoinCode);
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  urnw::LogInfo("live: a join code of {} octets was handed to the worker", probe.size());
  return Enqueue(std::move(out));
}

bool QueueRemoveMember(std::string groupIdHex, std::string identityPubHex) {
  uint64_t incarnation = 0;
  if (!Admit(groupIdHex, urmsg::live::ActionClass::Commit, incarnation)) return false;
  if (!IsHex(identityPubHex)) {
    urnw::LogWarn("live: a removal named an identity that is not hex ({} chars); refused before "
                  "it was queued",
                  identityPubHex.size());
    return false;
  }
  Outbound out;
  out.kind = OutboundKind::RemoveMember;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  out.identityPub = std::move(identityPubHex);
  // NO ROLE, DELIBERATELY LEFT EMPTY. The two verbs above fill this because the ABI takes it; this
  // one does not, and the outbox entry carries the empty string so that any note drawn from it
  // cannot name a role the commit never asked for.
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  return Enqueue(std::move(out));
}

bool QueueLeave(std::string groupIdHex, std::string transferToIdentityHex) {
  // A HAND-OVER NAMES ITS MEMBER AS HEX, checked before the gate so a refusal here marks nothing.
  if (!transferToIdentityHex.empty() && !IsHex(transferToIdentityHex)) {
    urnw::LogWarn("live: a leave named a hand-over identity that is not hex ({} chars); "
                  "refused before it was queued",
                  transferToIdentityHex.size());
    return false;
  }
  // ONE LEAVE OF A GROUP AT A TIME (review M1), and only of a group this device holds: the admission
  // marks the gate leaving in the same look, so a second press finds it leaving and is refused. A
  // LEAVE NEEDS A GROUP AND NOT A SESSION: a device whose group the server closed, or that was
  // removed from it, can still erase its own copy. A HAND-OVER is a commit and needs both.
  uint64_t incarnation = 0;
  const urmsg::live::ActionClass cls = transferToIdentityHex.empty()
                                           ? urmsg::live::ActionClass::Leave
                                           : urmsg::live::ActionClass::LeaveHandOver;
  if (!Admit(groupIdHex, cls, incarnation, /*markLeaving=*/true)) return false;
  Outbound out;
  out.kind = OutboundKind::Leave;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  out.identityPub = std::move(transferToIdentityHex);
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  const std::string gid = out.groupIdHex;
  if (!Enqueue(std::move(out))) {
    // the gate's leaving mark goes with the refusal, so the leave can be asked for again
    SetGateLeaving(gid, false);
    return false;
  }
  return true;
}

bool LeaveInFlight(std::string const& groupIdHex) {
  std::lock_guard<std::mutex> lock(g_gateMutex);
  auto const* gate = urmsg::live::FindGate(g_gates, groupIdHex);
  return gate != nullptr && gate->leaving;
}

bool QueueDelete(std::string groupIdHex, std::string targetMessageIdHex) {
  uint64_t incarnation = 0;
  if (!Admit(groupIdHex, urmsg::live::ActionClass::Commit, incarnation)) return false;
  Outbound out;
  out.kind = OutboundKind::Delete;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  if (!FromHexId(targetMessageIdHex, out.target)) {
    urnw::LogWarn("live: a deletion named a target that is not a 64-hex message_id ({} chars); "
                  "refused before it was queued",
                  targetMessageIdHex.size());
    return false;
  }
  out.targetHex = std::move(targetMessageIdHex);
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  return Enqueue(std::move(out));
}

bool QueueReaction(std::string groupIdHex, std::string targetMessageIdHex, std::string utf8Emoji,
                   bool remove) {
  if (utf8Emoji.empty()) return false;
  uint64_t incarnation = 0;
  if (!Admit(groupIdHex, urmsg::live::ActionClass::Commit, incarnation)) return false;

  Outbound out;
  out.kind = remove ? OutboundKind::ReactRemove : OutboundKind::ReactAdd;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  if (!FromHexId(targetMessageIdHex, out.target)) {
    urnw::LogWarn("live: a reaction named a target that is not a 64-hex message_id ({} chars); "
                  "refused before it was queued",
                  targetMessageIdHex.size());
    return false;
  }
  out.targetHex = std::move(targetMessageIdHex);
  out.emoji = std::move(utf8Emoji);
  out.groupIdHex = std::move(groupIdHex);
  out.incarnation = incarnation;
  return Enqueue(std::move(out));
}

}  // namespace urmsg::live
