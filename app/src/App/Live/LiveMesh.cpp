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
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Live/LiveWorld.h"
#include "Log.h"
#include "Paths.h"
#include "Strings.h"
#include "ThreadGuard.h"

// The vendored SDK C ABI. urnetwork_sdk.h carries urnet_release /
// urnet_free_string / urnet_live_handle_count; urnetwork_message.h carries the
// 44 messaging exports. Both live in app/third_party/vendor-include, which
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
struct Session {
  uint64_t ctx = 0;
  uint64_t client = 0;
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
      urnet_message_client_close(client);
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
struct Outbound {
  std::string localId;      // this session's name for the attempt
  std::string body;         // utf-8 octets, exactly as the composer held them
  std::string replaces;     // a failed entry this one supersedes, or empty
};

std::mutex g_sendMutex;
std::condition_variable g_sendWake;
std::deque<Outbound> g_sendQueue;
// Written by the worker (one writer), read by the UI thread. Relaxed is enough: a stale read costs
// one refused click or one accepted send the worker then refuses by name, and the fetch loop
// rewrites it every poll.
std::atomic<bool> g_canSend{false};
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
  g_sendWake.wait_for(lock, std::chrono::milliseconds(ms), [] { return !g_sendQueue.empty(); });
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

  // ── wait for the invite ─────────────────────────────────────────────────────
  const int64_t waitStartedMs = NowMs();
  std::vector<uint8_t> encoded;
  std::vector<uint8_t> previous;
  bool have = false;
  while (NowMs() - waitStartedMs < kInviteWaitMs) {
    std::vector<uint8_t> raw;
    if (ReadOctets(invitePath, raw)) {
      // Two identical reads before accepting it: the writer is another process
      // and a partially written file read whole is a checksum failure at the
      // parse, where it looks like corruption rather than like a race.
      if (!previous.empty() && previous == raw) {
        encoded = std::move(raw);
        have = true;
        break;
      }
      previous = std::move(raw);
    }
    ::Sleep(static_cast<DWORD>(kInvitePollMs));
  }
  if (!have) {
    urnw::LogError(
        "live: no invite at {} after {} ms. The peer never answered. Nothing here can mint one: "
        "the group is founded by the OTHER account and this device is the member it adds.",
        Utf8Path(invitePath), NowMs() - waitStartedMs);
    return false;
  }
  urnw::LogInfo("live: invite read from {} ({} octets) after {} ms", Utf8Path(invitePath),
                encoded.size(), NowMs() - waitStartedMs);

  // An invite ends with a checksum of everything before it, so a damaged one is
  // refused HERE rather than joining something wrong.
  const uint64_t invite =
      urnet_message_parse_invite(encoded.data(), static_cast<int32_t>(encoded.size()), &err);
  if (invite == 0) {
    urnw::LogError("live: parse_invite refused it: {}", TakeError(&err));
    return false;
  }
  s.group = urnet_message_device_join(s.device, s.ctx, invite, &err);
  urnet_release(invite);
  if (s.group == 0) {
    urnw::LogError(
        "live: device_join: {} — if this names a key package this device does not hold, the peer "
        "built the invite from a STALE key package file. Delete both handshake files and run both "
        "sides again.",
        TakeError(&err));
    return false;
  }
  urnw::LogInfo("live: *** JOINED the group *** at epoch {}", urnet_message_group_epoch(s.group));

  // CONSUMED, SO DELETED. It is key material and it has done its job; leaving it
  // on disk is a group anyone who reads that file is in.
  std::error_code ec;
  if (!std::filesystem::remove(invitePath, ec)) {
    urnw::LogWarn("live: the invite at {} could not be deleted ({}). Delete it by hand: it is key "
                  "material and whoever reads it is in this group.",
                  Utf8Path(invitePath), ec.message());
  } else {
    urnw::LogInfo("live: the invite has been consumed and deleted");
  }
  return true;
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
      m.mine = j.value("mine", false);
      m.sentAtMs = j.value("sent_at_ms", int64_t{0});
      m.kind = static_cast<uint8_t>(j.value("kind", 0));
      m.gap = j.value("gap", std::string{});
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
void LogMessages(urmsg::live::LiveGroup const& live) {
  urnw::LogInfo("live: ---- the conversation: {} message(s) ----", live.messages.size());
  for (size_t i = 0; i < live.messages.size(); ++i) {
    urmsg::live::LiveMessage const& m = live.messages[i];
    std::string reactions;
    for (auto const& r : m.reactions) {
      if (!reactions.empty()) reactions += " ";
      reactions += r.emoji;
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
    std::string text = m.body;
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
  s.client = urnet_message_client_new(credential.c_str(), kHost, nullptr, nullptr, kAppVersion, &err);
  if (s.client == 0) {
    urnw::LogError("live: client_new refused the credential: {}", TakeError(&err));
    s.Close();
    return;
  }
  // Scrub the credential from this frame now that the library has copied what it
  // needs. Belt and braces: it is one std::string in one thread, but a secret
  // that is gone cannot be logged by a line somebody adds later.
  ::SecureZeroMemory(credential.data(), credential.size());
  credential.clear();

  const std::string clientId = TakeString(urnet_message_client_id(s.client));
  const std::string platformUrl = TakeString(urnet_message_client_platform_url(s.client));
  urnw::LogInfo("live: client_id {} dialling {}", clientId, platformUrl);

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
  // THERE IS NO RECEIVE PUSH: this is a poll and that is what the transport is,
  // so a conversation that updates is a LOOP. It runs for the life of the
  // process; the worker is detached and the OS reclaims it at exit, which is why
  // Session::Close below is only reached on the paths that give up.
  urnw::LogInfo("live: entering the fetch loop, every {} ms", kFetchPollMs);
  int64_t lastPublishedCount = -1;

  // WHAT THIS DEVICE HAS TRIED TO SEND AND THE SERVER HAS NOT TAKEN. Owned by this thread and by
  // nothing else — the UI hands over octets through the queue and reads the result back as a
  // published world, so this vector needs no lock. See LiveWorld.h for why a row that is not a
  // record is still not a fabrication.
  std::vector<urmsg::live::LiveOutboxEntry> outbox;

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
    live.host = kHost;
    live.statsJson = TakeString(urnet_message_group_stats(s.group));
    live.outbox = outbox;
    CollectMessages(s.group, live.messages);

    // THE SEND BUTTON'S ONE GATE, re-read off the library every time rather than latched when the
    // group opened: a group the server has closed under us stops accepting sends, and a button
    // that learns that only from a failed click is the enabled-but-dead control design §9.1 bans.
    g_canSend.store(live.open, std::memory_order_relaxed);

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

    for (auto const& out : queued) {
      // A RETRY SUPERSEDES THE ENTRY IT CAME FROM rather than joining it. Without this the failed
      // row stays on screen beside the second attempt and one message reads as two.
      if (!out.replaces.empty()) {
        outbox.erase(std::remove_if(outbox.begin(), outbox.end(),
                                    [&](urmsg::live::LiveOutboxEntry const& e) {
                                      return e.localId == out.replaces;
                                    }),
                     outbox.end());
      }
      urmsg::live::LiveOutboxEntry entry;
      entry.localId = out.localId;
      entry.body = out.body;
      entry.attemptedAtMs = WallClockMs();
      outbox.push_back(std::move(entry));
    }
    publishWorld(false);

    for (auto const& out : queued) {
      char* sendErr = nullptr;
      const int64_t startedMs = NowMs();
      // COUNTED OCTETS, NOT A char*. The body is whatever was typed and a UTF-8 encoding of it can
      // hold a 0x00 nowhere except by a caller putting one there — but the ABI's rule for every
      // binary value going in is a pointer and a length (cgo/ctest/message_abi_test.c's 21-octet
      // body is two NULs and two multi-byte sequences precisely to hold this), and a length is what
      // is passed here so that the rule is kept rather than relied on.
      const std::string info = TakeString(urnet_message_group_send(
          s.group, s.ctx, reinterpret_cast<const uint8_t*>(out.body.data()),
          static_cast<int32_t>(out.body.size()), &sendErr));
      const std::string failure = TakeError(&sendErr);
      const int64_t tookMs = NowMs() - startedMs;

      auto at = std::find_if(
          outbox.begin(), outbox.end(),
          [&](urmsg::live::LiveOutboxEntry const& e) { return e.localId == out.localId; });
      if (!info.empty()) {
        // The info json carries the record id, the message_id and body_len — and no body.
        urnw::LogInfo("live: *** SENT *** {} octets in {} ms: {}", out.body.size(), tookMs, info);
        if (at != outbox.end()) outbox.erase(at);
      } else {
        urnw::LogError("live: send of {} octets was REFUSED after {} ms: {}", out.body.size(),
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

    // NOT ::Sleep. A send queued during the wait wakes this at once; nothing queued and it ticks on
    // its own timer exactly as the sleep did.
    WaitForSendOrPoll(kFetchPollMs);
  }
}

}  // namespace

bool IsEnabled() { return EnvVar(L"URMESSAGE_LIVE") == L"1" || HasCommandLineFlag(L"--live"); }

bool StartIfEnabled() {
  if (!IsEnabled()) return false;
  urnw::LogInfo("live: --live / %URMESSAGE_LIVE% is set; starting the live mesh worker");

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

bool QueueSend(std::string utf8Body, std::string replacesLocalId) {
  // REFUSED HERE RATHER THAN QUEUED AND REFUSED LATER, and the difference is what the caller can
  // do about it: a false answer lets the composer keep the text the person typed. A queued send
  // that the worker then refuses has already emptied the box.
  if (utf8Body.empty()) return false;
  if (!g_canSend.load(std::memory_order_relaxed)) return false;

  Outbound out;
  out.localId = std::to_string(g_nextLocalId.fetch_add(1));
  out.body = std::move(utf8Body);
  out.replaces = std::move(replacesLocalId);
  {
    std::lock_guard<std::mutex> lock(g_sendMutex);
    g_sendQueue.push_back(std::move(out));
  }
  // OUTSIDE THE LOCK: the worker wakes, takes the same mutex, and would be woken only to block on
  // the thread that woke it.
  g_sendWake.notify_one();
  return true;
}

}  // namespace urmsg::live
