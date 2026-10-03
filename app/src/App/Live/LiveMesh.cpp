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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Live/LiveWorld.h"
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
  uint64_t group = 0;

  void Close() {
    char* err = nullptr;
    if (group != 0) {
      if (!urnet_message_group_close(group, &err)) {
        const std::string text = TakeError(&err);
        if (!text.empty()) urnw::LogWarn("live: group_close: {}", text);
      }
      urnet_release(group);
    }
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
  Text, Reply, ReactAdd, ReactRemove, SetRole, TransferOwnership, RemoveMember, Delete
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
                            // RemoveMember)
  std::string role;         // "admin" | "member" | "observer" (SetRole); "owner" (Transfer)
};

std::mutex g_sendMutex;
std::condition_variable g_sendWake;
std::deque<Outbound> g_sendQueue;
// Set by the push waiter under g_sendMutex and consumed by WaitForSendOrPoll: a push wakes the
// fetch loop exactly the way a queued send does.
bool g_pushPending = false;
// Written by the worker (one writer), read by the UI thread. Relaxed is enough: a stale read costs
// one refused click or one accepted send the worker then refuses by name, and the fetch loop
// rewrites it every poll.
std::atomic<bool> g_canSend{false};

// IS THERE A GROUP AT ALL? A WEAKER QUESTION THAN g_canSend ABOVE AND IT HAS TO BE. CanSend asks
// whether the SERVER says the group is open, which a freshly founded group of one is not; this
// asks only whether the device holds one. The create verb needs the weaker reading, because the
// state it must refuse in is exactly the one where a group exists and cannot send yet.
std::atomic<bool> g_hasGroup{false};
std::atomic<uint64_t> g_nextLocalId{1};

// The whole queue, taken at once. Called on the worker only.
std::deque<Outbound> TakeSendQueue() {
  std::lock_guard<std::mutex> lock(g_sendMutex);
  std::deque<Outbound> taken;
  taken.swap(g_sendQueue);
  return taken;
}

// Sleep until there is something to send or `ms` has passed, whichever comes first.
void WaitForSendOrPoll(int64_t ms) {
  std::unique_lock<std::mutex> lock(g_sendMutex);
  g_sendWake.wait_for(lock, std::chrono::milliseconds(ms),
                      [] { return !g_sendQueue.empty() || g_pushPending; });
  g_pushPending = false;
}

// The push waiter: one thread, blocked in the library until the server announces a record, which
// it turns into a wake of the fetch loop. It owns nothing but the device handle it is given, and
// it is stopped and joined before that handle can be closed (PushWaiter's destructor).
class PushWaiter {
 public:
  explicit PushWaiter(uint64_t device) : device_(device), thread_([this] { Run(); }) {}
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
      urnet_free_string(groupHex);
      {
        std::lock_guard<std::mutex> lock(g_sendMutex);
        g_pushPending = true;
      }
      g_sendWake.notify_all();
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
std::string g_pastedJoinCode;               // guarded by g_onboardMutex; consumed by the worker
bool g_createGroupAsked = false;            // guarded by g_onboardMutex; consumed by the worker
std::atomic<uint64_t> g_onboardGeneration{0};

void PublishOnboard(urmsg::live::OnboardStep step, std::string joinCode, std::string message,
                    std::string inviteCode = {}) {
  auto next = std::make_shared<urmsg::live::OnboardState>();
  next->step = step;
  next->joinCode = std::move(joinCode);
  next->inviteCode = std::move(inviteCode);
  next->message = std::move(message);
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_onboard = std::move(next);
  }
  g_onboardGeneration.fetch_add(1, std::memory_order_release);
  // THE SAME BELL THE WORLD RINGS. The window's beat re-reads the onboarding state along with
  // everything else, so a join code that has just been minted reaches the screen on the next beat
  // rather than on the next fetch.
  urmsg::live::NotifyPublished();
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
bool AddMemberFromCode(Session& s, const std::string& theirCode, const std::string& joinCode) {
  std::vector<uint8_t> keyPackage;
  if (!DecodeBase64(theirCode, keyPackage)) {
    PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                   "That join code could not be read. Ask for the whole code again - it is one "
                   "long line with no spaces inside it.");
    urnw::LogWarn("live: a pasted join code was not decodable base64; refused");
    return false;
  }

  char* err = nullptr;
  const bool wasOpen = urnet_message_group_is_open(s.group);
  uint64_t invite = 0;
  if (wasOpen) {
    invite = urnet_message_group_add_member_and_publish(
        s.group, s.ctx, keyPackage.data(), static_cast<int32_t>(keyPackage.size()), &err);
  } else {
    invite = urnet_message_group_add_member(
        s.group, keyPackage.data(), static_cast<int32_t>(keyPackage.size()), &err);
  }
  if (invite == 0) {
    const std::string why = TakeError(&err);
    urnw::LogError("live: {} refused the add: {}",
                   wasOpen ? "add_member_and_publish" : "add_member", why);
    PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                   "That person could not be added: " + why);
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
    PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                   "They were added, but the invitation could not be written out, so there is "
                   "nothing to send them. Ask them for a fresh join code and add them again.");
    return true;
  }

  // THE OPEN, and only on the founding road. add_member_and_publish has already published its
  // own commit; add_member has not, and group_open is what tells the server the group exists.
  if (!wasOpen) {
    if (!urnet_message_group_open(s.group, s.ctx, &err)) {
      const std::string why = TakeError(&err);
      urnw::LogError("live: group_open: {}", why);
      PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                     "They were added, but the group could not be published to the server: " +
                         why);
      return false;
    }
  }

  urnw::LogInfo("live: *** MEMBER ADDED *** by {}; the group is at epoch {}, open: {}",
                wasOpen ? "add_member_and_publish" : "add_member + open",
                urnet_message_group_epoch(s.group),
                urnet_message_group_is_open(s.group) ? "yes" : "no");
  PublishOnboard(urmsg::live::OnboardStep::Invited, joinCode,
                 "Send this invitation to that person. It works once, and whoever has it is in "
                 "the group - send it the way you would send a password.",
                 code);
  return true;
}

// ── the two-party handshake ───────────────────────────────────────────────────

// Publish this device's key package, wait for the peer's invite, and join.
// Answers true with s.group set, or false having said why.
//
// THE KEY PACKAGE IS NOT A SECRET and the INVITE IS. A key package is a public
// offer to be added; an invite carries the Welcome and the group's secrets, so
// whoever reads it is in the group. The invite is deleted the moment it has been
// used, and the key package is deleted by the peer that consumes it.
bool JoinFromPeer(Session& s) {
  char* err = nullptr;
  const std::filesystem::path dir = HandshakeDir();
  const std::filesystem::path keyPackagePath = dir / kKeyPackageName;
  const std::filesystem::path invitePath = dir / kInviteName;

  // Buffer-out: call once with out == NULL to size, allocate, call again.
  int32_t needed = 0;
  if (!urnet_message_device_key_package(s.device, nullptr, &needed, &err)) {
    const std::string sizing = TakeError(&err);
    if (needed <= 0) {
      urnw::LogError("live: device_key_package could not be sized: {}",
                     sizing.empty() ? "no reason given" : sizing);
      return false;
    }
  }
  std::vector<uint8_t> keyPackage(static_cast<size_t>(needed));
  int32_t capacity = needed;
  if (!urnet_message_device_key_package(s.device, keyPackage.data(), &capacity, &err)) {
    urnw::LogError("live: device_key_package: {}", TakeError(&err));
    return false;
  }
  keyPackage.resize(static_cast<size_t>(capacity));

  // A KEY PACKAGE IS SINGLE USE AND THIS ONE HAS JUST BEEN MINTED, so any file
  // already at that path is a previous run's and is worthless to a peer.
  // Overwriting it is the point.
  if (!WriteOctets(keyPackagePath, keyPackage.data(), keyPackage.size())) {
    urnw::LogError("live: could not write this device's key package to {}",
                   Utf8Path(keyPackagePath));
    return false;
  }
  urnw::LogInfo(
      "live: published this device's key package ({} octets) to {}. WAITING for a second party: a "
      "solo device cannot open a group — group_open comes after add_member, the alpha accepts "
      "exactly one, and one credential is one key package. Run sdk/livepeer under the OTHER "
      "account, pointed at this file.",
      keyPackage.size(), Utf8Path(keyPackagePath));

  // THE SAME OCTETS AS TEXT. A key package is a public offer to be added, so this is not a secret
  // and the UI may show it, copy it and let a person send it down any channel they like. The code
  // itself is still never LOGGED: a log line is a different audience from a screen, it outlives
  // the moment, and there is no reason to put two kilobytes of base64 in one.
  const std::string joinCode = EncodeBase64(keyPackage.data(), keyPackage.size());
  PublishOnboard(urmsg::live::OnboardStep::Waiting, joinCode,
                 "This device is not in a group yet. Send your join code to whoever is setting "
                 "the group up, and paste the invitation they send back.");

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
  for (;;) {
    std::vector<uint8_t> encoded;
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
            s.group = made;
            // SET HERE AND NOT ONLY IN THE FETCH LOOP. A founded group of one never reaches the
            // fetch loop until somebody is added, and between those two moments a second create
            // would replace it. QueueCreateGroup reads this.
            g_hasGroup.store(true, std::memory_order_relaxed);
            urnw::LogInfo("live: *** GROUP CREATED *** {} at epoch {}, open on the server: {}",
                          ToHex(gid, static_cast<int32_t>(sizeof(gid))),
                          urnet_message_group_epoch(s.group),
                          urnet_message_group_is_open(s.group) ? "yes" : "no");
            // AND IT IS NOT USABLE YET, WHICH THE MESSAGE SAYS RATHER THAN THE UI GUESSING. A
            // group of one is at epoch 0 and the server has never been told about it; the commit
            // that opens it is the one adding the first other person.
            PublishOnboard(urmsg::live::OnboardStep::Founded, joinCode,
                           "Your group is made, but it has only you in it and nothing can be "
                           "sent yet. Ask somebody for their join code and paste it below - "
                           "adding them is what opens the group.");
            // The fetch loop does NOT take over here: a group that is not open has nothing to
            // fetch, and the add below is served from this same loop.
          }
        }
      }
    }

    // THE ADD, which is the founder's second half and the only road out of Founded. Served here
    // as well as in the fetch loop, because a founder sits in THIS loop until their group opens.
    if (s.group != 0) {
      std::string theirCode;
      {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        theirCode.swap(g_pastedJoinCode);
      }
      if (!theirCode.empty()) {
        if (AddMemberFromCode(s, theirCode, joinCode)) {
          // An add that opened the group is the moment this device has a session. The fetch loop
          // below takes it from here.
          if (urnet_message_group_is_open(s.group)) return true;
        }
      }
    }

    // The developer's road: the file sdk/livehost writes.
    if (encoded.empty()) {
      std::vector<uint8_t> raw;
      if (ReadOctets(invitePath, raw)) {
        // Two identical reads before accepting it: the writer is another process and a partially
        // written file read whole is a checksum failure at the parse, where it looks like
        // corruption rather than like a race.
        if (!previous.empty() && previous == raw) {
          encoded = std::move(raw);
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
      urnw::LogError("live: parse_invite refused it: {}", TakeError(&err));
      PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                     "That invitation was refused: it did not survive the journey intact. Ask for "
                     "a fresh one - an invitation can only be used once.");
      previous.clear();
      ::Sleep(static_cast<DWORD>(kInvitePollMs));
      continue;
    }
    s.group = urnet_message_device_join(s.device, s.ctx, invite, &err);
    urnet_release(invite);
    if (s.group == 0) {
      urnw::LogError(
          "live: device_join: {} - if this names a key package this device does not hold, the "
          "invite was built from a STALE join code. Send the code the app is showing NOW.",
          TakeError(&err));
      PublishOnboard(urmsg::live::OnboardStep::Refused, joinCode,
                     "That invitation was not meant for this device. Send the join code this "
                     "screen is showing now, and ask for a new invitation built from it.");
      previous.clear();
      ::Sleep(static_cast<DWORD>(kInvitePollMs));
      continue;
    }

    urnw::LogInfo("live: *** JOINED the group *** at epoch {}", urnet_message_group_epoch(s.group));
    PublishOnboard(urmsg::live::OnboardStep::Joined, std::string(), "You are in the group.");

    // CONSUMED, SO DELETED. It is key material and it has done its job; leaving it on disk is a
    // group anyone who reads that file is in. Only the FILE road leaves anything behind - a pasted
    // code was never written down by this process, which is the better of the two for that reason.
    std::error_code ec;
    if (!fromPaste && std::filesystem::exists(invitePath, ec)) {
      if (!std::filesystem::remove(invitePath, ec)) {
        urnw::LogWarn("live: the invite at {} could not be deleted ({}). Delete it by hand: it is "
                      "key material and whoever reads it is in this group.",
                      Utf8Path(invitePath), ec.message());
      } else {
        urnw::LogInfo("live: the invite has been consumed and deleted");
      }
    }
    return true;
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

  Session s;
  char* err = nullptr;

  s.ctx = urnet_message_context_new();

  // instance_id NULL draws a fresh installation uuid each launch. The client_id
  // is the credential's either way, and it is the client_id the operator routes
  // to — so a fresh instance_id does not avoid the reconnect window below.
  //
  // THE ROUTE (ledger 268). URNETWORK and DIRECT reach the server's own endpoint with its key
  // pinned; the credential is used only by URNETWORK, to mint the tunnel's exit clients.
  const Route route = ChosenRoute();
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

  Hellos hellos;
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

  if (restoredCount > 0) {
    s.group = urnet_message_group_list_at(restored, 0);
    urnw::LogInfo("live: restored an existing group from disk — no handshake needed");
  }
  if (restored != 0) urnet_release(restored);

  if (s.group == 0 && !JoinFromPeer(s)) {
    s.Close();
    return;
  }

  uint8_t gid[32] = {};
  int32_t gidLen = static_cast<int32_t>(sizeof(gid));
  std::string gidHex;
  if (urnet_message_group_id(s.group, gid, &gidLen)) gidHex = ToHex(gid, gidLen);
  urnw::LogInfo("live: group {} at epoch {}, open on the server: {}", gidHex,
                urnet_message_group_epoch(s.group),
                urnet_message_group_is_open(s.group) ? "yes" : "no");

  // ── fetch, forever ──────────────────────────────────────────────────────────
  // A LOOP THAT A PUSH WAKES. The server announces each new record to a subscribed
  // connection (spec B 4.3.5, ledger 269) and the loop answers with the same fetch
  // it polls with, so a push changes WHEN a record is read and never HOW. It runs
  // for the life of the process; the worker is detached and the OS reclaims it at
  // exit, which is why Session::Close below is only reached on the paths that give up.
  urnw::LogInfo("live: entering the fetch loop, every {} ms until the push subscription holds", kFetchPollMs);
  int64_t lastPublishedCount = -1;
  // THE PUSH (ledger 269): a waiter that wakes this loop the moment the server announces a record,
  // and the subscription state the loop keeps current below
  PushWaiter pushWaiter(s.device);
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
  auto publishWorld = [&](bool logIfChanged) {
    // THE WHOLE LOG, not just what a fetch answered — a reaction and a tombstone change a message
    // that ALREADY arrived and are never in the fetch's own list, so a reader of the fetch alone
    // never sees either.
    urmsg::live::LiveGroup live;
    live.groupIdHex = gidHex;
    live.epoch = urnet_message_group_epoch(s.group);
    live.open = urnet_message_group_is_open(s.group);
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
    live.statsJson = TakeString(urnet_message_group_stats(s.group));
    live.outbox = outbox;
    live.reactionOutbox = reactionOutbox;
    live.deleteOutbox = deleteOutbox;
    live.roleOutbox = roleOutbox;
    CollectMessages(s.group, live.messages);
    // THE ROSTER AND THIS DEVICE'S ROLE, on every publish and not once at the open: a role change
    // is a commit another member makes, and it lands here on some later fetch as a moved row.
    CollectMembers(s.group, live.members);
    live.myRole = ReadMyRole(s.group);
    if (const std::string roster = RosterLine(live); roster != lastRosterLine) {
      lastRosterLine = roster;
      urnw::LogInfo("live: ROSTER {}", roster);
    }

    // THE SEND BUTTON'S ONE GATE, re-read off the library every time rather than latched when the
    // group opened: a group the server has closed under us stops accepting sends, and a button
    // that learns that only from a failed click is the enabled-but-dead control design §9.1 bans.
    g_canSend.store(live.open, std::memory_order_relaxed);
    g_hasGroup.store(true, std::memory_order_relaxed);

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
    urmsg::live::Publish(urmsg::live::BuildWorld(live));
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
    std::deque<Outbound> queued = TakeSendQueue();
    if (queued.empty()) return;

    for (auto& out : queued) {
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
            removal  ? urnet_message_group_remove_member(s.group, s.ctx,
                                                         out.identityPub.c_str(), &sendErr)
            : transfer ? urnet_message_group_transfer_ownership(s.group, s.ctx,
                                                                out.identityPub.c_str(), &sendErr)
                       : urnet_message_group_set_role(s.group, s.ctx, out.identityPub.c_str(),
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
                        urnet_message_group_epoch(s.group));
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
              s.group, s.ctx, reinterpret_cast<const uint8_t*>(out.body.data()),
              static_cast<int32_t>(out.body.size()), &sendErr));
          break;
        case OutboundKind::Reply:
          // The parent as 32 counted octets, decoded at the queue. The library does not require
          // the parent to be present - it may have been deleted or pruned - so a reply to a line
          // that has since vanished is sealed all the same, and the far side names what it cannot
          // show exactly as this side does.
          verb = "send_reply";
          info = TakeString(urnet_message_group_send_reply(
              s.group, s.ctx, out.target.data(), static_cast<int32_t>(out.target.size()),
              reinterpret_cast<const uint8_t*>(out.body.data()),
              static_cast<int32_t>(out.body.size()), &sendErr));
          break;
        case OutboundKind::ReactAdd:
          // The emoji IS a NUL-terminated char* here, and that is the ABI's own rule for this one
          // argument (urnetwork_message.h: "the emoji is a NUL-terminated utf-8 string"), unlike
          // every binary value above. It is checked as 1..64 octets of valid UTF-8 before anything
          // is sealed, and refused by name otherwise.
          verb = "react";
          info = TakeString(urnet_message_group_react(s.group, s.ctx, out.target.data(),
                                                      static_cast<int32_t>(out.target.size()),
                                                      out.emoji.c_str(), &sendErr));
          break;
        case OutboundKind::ReactRemove:
          verb = "unreact";
          info = TakeString(urnet_message_group_unreact(s.group, s.ctx, out.target.data(),
                                                        static_cast<int32_t>(out.target.size()),
                                                        out.emoji.c_str(), &sendErr));
          break;
        case OutboundKind::Delete:
          // The target as 32 counted octets, decoded at the queue. The library answers the
          // tombstone's own message info, or refuses a target this device did not write.
          verb = "delete";
          info = TakeString(urnet_message_group_delete(s.group, s.ctx, out.target.data(),
                                                       static_cast<int32_t>(out.target.size()),
                                                       &sendErr));
          break;
        case OutboundKind::SetRole:
        case OutboundKind::TransferOwnership:
        case OutboundKind::RemoveMember:
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

  for (;;) {
    // SENDS FIRST, BEFORE THE FETCH. A fetch takes as long as the server takes and the person who
    // just pressed Send is watching the composer; putting the send behind it would add a whole
    // round trip to every message this app writes.
    drainSends();

    // SUBSCRIBE WHEN NOT CURRENT: no subscription yet, an epoch that moved past the one it was
    // authorized at, or a reconnect whose Hello the transport sent by itself. Cheap when current,
    // and the receive below reads whatever arrived before a new subscription took hold.
    {
      const int32_t subscribedNow = urnet_message_group_ensure_subscribed(s.group, s.ctx, &err);
      if (0 < subscribedNow) {
        urnw::LogInfo("live: subscribed to push at epoch {}", urnet_message_group_epoch(s.group));
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
    const uint64_t fetched = urnet_message_group_receive(s.group, s.ctx, &err);
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
    if (0 < fetchedCount) {
      urnw::LogInfo("live: group_receive answered {} NEW record(s) in {} ms", fetchedCount,
                    NowMs() - fetchStartedMs);
    }
    if (fetched != 0) urnet_release(fetched);

    publishWorld(true);

    // AND THE ADDS, which is how the third person and the fifth get in. Served HERE as well as in
    // the wait loop because a founder leaves that loop the moment their group opens, and every
    // add after the first one happens with a live session and a fetch loop running. Same call,
    // same branch inside it: AddMemberFromCode asks the group whether it is open rather than
    // counting how many have been added.
    {
      std::string theirCode;
      {
        std::lock_guard<std::mutex> lock(g_onboardMutex);
        theirCode.swap(g_pastedJoinCode);
      }
      if (!theirCode.empty()) {
        AddMemberFromCode(s, theirCode, std::string());
        // The roster moved, so the world the UI is holding is a beat out of date.
        publishWorld(true);
      }
    }

    // NOT ::Sleep. A send queued during the wait wakes this at once, and so does a push; with
    // neither it ticks on its own timer, which is long when the push subscription holds.
    WaitForSendOrPoll(subscribed ? kPushedPollMs : kFetchPollMs);
  }
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
    g_canSend.store(false, std::memory_order_relaxed);
    g_hasGroup.store(false, std::memory_order_relaxed);
  });

  // DETACHED, AND NOT AS A SHORTCUT. This is called from the UI thread of a
  // single-threaded apartment, and std::thread::join() does not pump messages —
  // joining a worker that blocks for tens of seconds inside device_connect
  // would freeze the window for exactly that long. The worker owns its handles
  // and closes them itself, so there is nothing for anyone to wait on.
  worker.detach();
  return true;
}

bool CanSend() { return g_canSend.load(std::memory_order_relaxed); }

namespace {

// The one enqueue. Everything the two public verbs check has been checked by the time this runs.
void Enqueue(Outbound out) {
  {
    std::lock_guard<std::mutex> lock(g_sendMutex);
    g_sendQueue.push_back(std::move(out));
  }
  // OUTSIDE THE LOCK: the worker wakes, takes the same mutex, and would be woken only to block on
  // the thread that woke it.
  g_sendWake.notify_one();
}

}  // namespace

bool QueueSend(std::string utf8Body, std::string replacesLocalId, std::string replyToMessageIdHex) {
  // REFUSED HERE RATHER THAN QUEUED AND REFUSED LATER, and the difference is what the caller can
  // do about it: a false answer lets the composer keep the text the person typed. A queued send
  // that the worker then refuses has already emptied the box.
  if (utf8Body.empty()) return false;
  if (!g_canSend.load(std::memory_order_relaxed)) return false;

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
  Enqueue(std::move(out));
  return true;
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

bool QueueRoleChange(std::string identityPubHex, std::string role) {
  if (!g_canSend.load(std::memory_order_relaxed)) return false;
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
  Enqueue(std::move(out));
  return true;
}

bool QueueTransferOwnership(std::string identityPubHex) {
  if (!g_canSend.load(std::memory_order_relaxed)) return false;
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
  Enqueue(std::move(out));
  return true;
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

bool QueueCreateGroup() {
  // REFUSED WHEN THERE IS ALREADY A GROUP. A device holds one group in this build, and a second
  // create would silently replace the one on screen along with its whole transcript. The worker
  // can only act on this while it is in the wait loop, which is exactly when there is none - but
  // the UI can call at any time, so the refusal is stated here rather than relied on there.
  if (g_hasGroup.load(std::memory_order_relaxed)) {
    urnw::LogWarn("live: a create was asked for while this device already holds a group; refused");
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    g_createGroupAsked = true;
  }
  urnw::LogInfo("live: a group create was handed to the worker");
  return true;
}

bool QueueAddMemberFromCode(std::string base64JoinCode) {
  // Decoded here and the result thrown away, for the same reason QueueJoinFromInviteCode does it:
  // text that cannot be a join code is refused while the person is still looking at the box.
  std::vector<uint8_t> probe;
  if (!DecodeBase64(base64JoinCode, probe)) {
    urnw::LogWarn("live: a pasted join code was not decodable ({} characters); refused before it "
                  "was queued",
                  base64JoinCode.size());
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_onboardMutex);
    // REPLACES rather than queues, like a pasted invitation: somebody who pastes twice has
    // corrected themselves, and adding both would spend two epochs on one intention.
    g_pastedJoinCode = std::move(base64JoinCode);
  }
  urnw::LogInfo("live: a join code of {} octets was handed to the worker", probe.size());
  return true;
}

bool QueueRemoveMember(std::string identityPubHex) {
  if (!g_canSend.load(std::memory_order_relaxed)) return false;
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
  Enqueue(std::move(out));
  return true;
}

bool QueueDelete(std::string targetMessageIdHex) {
  if (!g_canSend.load(std::memory_order_relaxed)) return false;
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
  Enqueue(std::move(out));
  return true;
}

bool QueueReaction(std::string targetMessageIdHex, std::string utf8Emoji, bool remove) {
  if (utf8Emoji.empty()) return false;
  if (!g_canSend.load(std::memory_order_relaxed)) return false;

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
  Enqueue(std::move(out));
  return true;
}

}  // namespace urmsg::live
