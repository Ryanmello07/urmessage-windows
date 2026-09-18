// SPDX-License-Identifier: MPL-2.0
//
// REAL MESSAGES, IN THE SHAPE THE EXISTING VIEWS TAKE. Read LiveWorld.h first for the placeholder
// ruling this file implements.
//
// NO PCH, NO winrt, NO XAML — this is built on the live worker thread. App.vcxproj marks it
// PrecompiledHeader=NotUsing for the same reason Live\LiveMesh.cpp is marked.
//
// WHY THE World STRUCT IS NOT EXTENDED, which explains three of the decisions below. Startup.cpp's
// assertion I10 hashes EVERY BYTE of the fabricated world against a fixed fingerprint
// (kExpectedWorldFingerprint, 0x97B1C149D13010C3) and runs on EVERY launch, with CI reading the
// output for the token FAIL. Adding a field to MessageRow — a `deleted` bit, a reactions vector, a
// reply-to id — moves those bytes and turns that gate red for every launch, live or not. So every
// real thing the protocol carries that the struct has no slot for is rendered through a slot it
// DOES have, using a component that already exists:
//
//   * a DELETED message      -> a System line saying it was deleted by its sender
//   * a GAP                  -> a System line naming the reason (the ABI's "closed placeholder")
//   * a REPLY's parent link  -> a System line above the bubble naming the parent
//   * the REACTIONS standing -> a System line below the bubble listing them
//
// None of those is invented: each states something the record actually carries.

#include "Live/LiveWorld.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <format>
#include <mutex>

#include "Strings.h"

// The kind codes. This TU does not link the SDK; it only needs the numbers, and repeating them
// here would be a second copy to drift. The header is include-only for these defines.
extern "C" {
#include "urnetwork_message.h"
}

namespace urmsg::live {
namespace {

// ── the standing snapshot ─────────────────────────────────────────────────────
std::mutex g_mutex;
WorldPtr g_world;
std::function<void()> g_onPublish;
uint64_t g_generation = 0;
uint64_t g_digest = 0;

// FNV-1a over everything a reader can see. It is a change detector and not a checksum: a collision
// costs one skipped redraw, which the next fetch corrects.
void Mix(uint64_t& h, std::wstring const& s) {
  for (wchar_t c : s) {
    h ^= static_cast<uint64_t>(c);
    h *= 1099511628211ull;
  }
  h ^= 0x2Full;
  h *= 1099511628211ull;
}

uint64_t Digest(urmsg::demo::World const& world) {
  uint64_t h = 1469598103934665603ull;
  for (auto const& conv : world.conversations) {
    Mix(h, conv.id);
    Mix(h, conv.name);
    Mix(h, conv.preview);
    Mix(h, conv.timeLabel);
    for (auto const& row : conv.rows) {
      Mix(h, row.id);
      Mix(h, row.body);
      Mix(h, row.systemText);
      Mix(h, row.timeLabel);
      h ^= static_cast<uint64_t>(row.kind) * 31 + static_cast<uint64_t>(row.outgoing) * 7 +
           static_cast<uint64_t>(row.state);
      h *= 1099511628211ull;
    }
  }
  h ^= world.currentEpoch;
  return h;
}

// ── time ──────────────────────────────────────────────────────────────────────

// Local wall-clock formatting of a REAL sent_at_ms. The protocol carries the sender's claim about
// when it sealed the record; it is not a server timestamp and this is not presented as one.
std::wstring FormatClock(int64_t ms) {
  if (ms <= 0) return std::wstring(kUnavailable);
  const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  std::tm local{};
  if (::localtime_s(&local, &seconds) != 0) return std::wstring(kUnavailable);
  wchar_t buf[16]{};
  std::swprintf(buf, std::size(buf), L"%02d:%02d", local.tm_hour, local.tm_min);
  return buf;
}

std::wstring FormatDay(int64_t ms) {
  if (ms <= 0) return std::wstring(kUnavailable);
  const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  std::tm local{};
  if (::localtime_s(&local, &seconds) != 0) return std::wstring(kUnavailable);
  wchar_t buf[64]{};
  if (std::wcsftime(buf, std::size(buf), L"%A %d %B %Y", &local) == 0) {
    return std::wstring(kUnavailable);
  }
  return buf;
}

// The calendar day a timestamp falls on, as a comparable integer, so a day separator is emitted on
// a real date change rather than on a fixed row count.
int64_t DayOrdinal(int64_t ms) {
  if (ms <= 0) return 0;
  const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  std::tm local{};
  if (::localtime_s(&local, &seconds) != 0) return 0;
  return static_cast<int64_t>(local.tm_year) * 512 + static_cast<int64_t>(local.tm_yday);
}

// ── identicon seeds ───────────────────────────────────────────────────────────

// A seed drawn from a REAL identifier — the group id, or a sender_handle — rather than a made-up
// one. The identicon is then a rendering of something the protocol actually carries: two messages
// from one sender_handle draw the same mark, and two different handles draw different marks. It is
// NOT a name and the app never presents it as one.
urmsg::demo::Seed SeedFromHex(std::string const& hex) {
  urmsg::demo::Seed seed{};
  // Fill from the hex nibbles, repeating if the source is shorter than 32 octets. An empty source
  // gives the all-zero seed, which is what "no identity to draw" looks like.
  if (hex.empty()) return seed;
  for (size_t i = 0; i < seed.size(); ++i) {
    const char c = hex[(i * 2) % hex.size()];
    const char d = hex[(i * 2 + 1) % hex.size()];
    auto nibble = [](char ch) -> uint8_t {
      if ('0' <= ch && ch <= '9') return static_cast<uint8_t>(ch - '0');
      if ('a' <= ch && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
      if ('A' <= ch && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
      return 0;
    };
    seed[i] = static_cast<uint8_t>((nibble(c) << 4) | nibble(d));
  }
  return seed;
}

// The first few characters of an id, for a sentence that has to name a message without printing 64
// hex characters into a bubble.
std::wstring ShortId(std::string const& hex) {
  if (hex.empty()) return std::wstring(kUnavailable);
  return urnw::Widen(hex.substr(0, std::min<size_t>(hex.size(), 12)));
}

// A REAL size bucket, computed from the body's own octet count. This is an observation and is the
// one "size" figure on the message surface that is not a placeholder.
std::wstring SizeBucket(int32_t bytes) {
  if (bytes <= 0) return L"0 octets";
  if (bytes <= 1024) return L"<= 1 KiB";
  if (bytes <= 16 * 1024) return L"<= 16 KiB";
  if (bytes <= 256 * 1024) return L"<= 256 KiB";
  return L"> 256 KiB";
}

// Four kinds never appear as a line of their own: a reaction, an un-reaction and a tombstone
// CHANGE another message, and a cover is traffic that exists to look like a message. They arrive
// as the `reactions` and `deleted` of the message they name.
bool IsRenderableKind(uint8_t kind) {
  switch (kind) {
    case URNET_MESSAGE_KIND_REACTION_ADD:
    case URNET_MESSAGE_KIND_REACTION_REMOVE:
    case URNET_MESSAGE_KIND_TOMBSTONE:
    case URNET_MESSAGE_KIND_COVER:
      return false;
    default:
      return true;
  }
}

urmsg::demo::MessageRow MakeSystemRow(std::wstring id, std::wstring text) {
  urmsg::demo::MessageRow row;
  row.kind = urmsg::demo::RowKind::System;
  row.id = std::move(id);
  row.systemText = std::move(text);
  row.state = urmsg::demo::DeliveryState::Sent;
  row.permanentRecord = false;
  return row;
}

}  // namespace

urmsg::demo::World BuildWorld(LiveGroup const& group) {
  urmsg::demo::World world;

  // ── the conversation ────────────────────────────────────────────────────────
  urmsg::demo::Conversation conv;
  conv.id = L"live-" + urnw::Widen(group.groupIdHex);
  // DIRECT, and that is a statement of fact rather than a simplification. The alpha's group is
  // exactly two parties — the founder and the one member add_member admits — and the product model
  // is that a DM IS a two-member group. Calling it a Group would make the thread header render
  // "Group, N members" off a member list the protocol does not carry, which is the fabrication
  // this whole file exists to avoid.
  conv.kind = urmsg::demo::ConversationKind::Direct;
  conv.name = kUnavailable;  // there are no group names and no contact discovery
  conv.identityKey = SeedFromHex(group.groupIdHex);
  conv.groupIdHex = urnw::Widen(group.groupIdHex);
  conv.muted = false;
  conv.disappearing = false;
  // EMPTY, and the views render the placeholder for an empty one. The ABI carries no membership:
  // a member that has never spoken is invisible, so a list built from the senders seen would be a
  // guess presented as a roster.
  conv.members.clear();
  conv.memberCount = 0;
  conv.retentionLabel = kUnavailable;
  conv.mediaRetentionLabel = kUnavailable;
  // NO UNREAD COUNT EXISTS. There is no read cursor in this build and nothing reports one, so the
  // count is left at zero rather than invented. It is the one field in the owner's list with no
  // text channel to put the placeholder in: every slot that renders it is an integer.
  conv.unread = 0;

  int64_t lastDay = 0;
  int64_t newestMs = 0;
  std::wstring newestBody;

  for (size_t i = 0; i < group.messages.size(); ++i) {
    LiveMessage const& m = group.messages[i];
    const std::wstring wideId = urnw::Widen(m.messageId);

    // A DAY SEPARATOR ON A REAL DATE CHANGE. The label is the row's body, which is what
    // DaySeparatorLabel reads.
    const int64_t day = DayOrdinal(m.sentAtMs);
    if (day != 0 && day != lastDay) {
      lastDay = day;
      urmsg::demo::MessageRow sep;
      sep.kind = urmsg::demo::RowKind::DaySeparator;
      sep.id = wideId + L"-day";
      sep.body = FormatDay(m.sentAtMs);
      sep.state = urmsg::demo::DeliveryState::Sent;
      conv.rows.push_back(std::move(sep));
    }

    // A GAP IS BRANCHED ON FIRST, before kind — on a gap, `kind` is the code the record ARRIVED
    // under and not what the record is, so a malformed reply carries KIND_REPLY and is still a
    // gap. Something IS at this position and this build cannot show it; a closed placeholder that
    // keeps the position is the honest draw.
    if (!m.gap.empty()) {
      conv.rows.push_back(MakeSystemRow(
          wideId,
          std::format(L"A message is here that this build cannot show ({}).",
                      urnw::Widen(m.gap))));
      continue;
    }

    if (!IsRenderableKind(m.kind)) continue;

    // A DELETED MESSAGE. The record keeps its body and the ABI hands it back; showing it would
    // ignore the tombstone, and dropping the row would lose the fact that a line was here.
    if (m.deleted) {
      conv.rows.push_back(MakeSystemRow(wideId, L"This message was deleted by its sender."));
      continue;
    }

    // A REPLY'S PARENT LINK, above the bubble. The quoted text never travels — a reply carries the
    // parent's NAME — so this names it and does not pretend to quote it.
    if (m.kind == URNET_MESSAGE_KIND_REPLY && !m.replyToId.empty()) {
      conv.rows.push_back(MakeSystemRow(
          wideId + L"-reply",
          std::format(L"In reply to message {}…", ShortId(m.replyToId))));
    }

    urmsg::demo::MessageRow row;
    row.kind = urmsg::demo::RowKind::Message;
    row.id = wideId;
    // Drawn only on a GROUP's incoming run start, so on this Direct conversation it renders
    // nowhere; the rail's Sender field carries the same placeholder where it IS drawn.
    row.senderName = kUnavailable;
    row.senderKey = SeedFromHex(m.senderHandle);
    row.body = urnw::Widen(m.body);
    row.timeLabel = FormatClock(m.sentAtMs);
    // REAL: the ABI's `mine` is true when THIS device sealed the record.
    row.outgoing = m.mine;
    // SENT IS THE CEILING AND IT IS REAL: the server acknowledged the submit. Delivered and Read
    // are never set, because nothing in this protocol reports either — there are no receipts.
    row.state = urmsg::demo::DeliveryState::Sent;
    row.failureReason.clear();
    row.permanentRecord = false;

    row.inspect.epoch = group.epoch;                             // real
    row.inspect.senderLeafIndex = urmsg::demo::kUnknownUint32;   // unavailable
    row.inspect.retention = urmsg::demo::RetentionClass::Permanent;
    row.inspect.sizeBucket = SizeBucket(m.bodyLen);              // real
    row.inspect.wireSizeBytes = urmsg::demo::kUnknownUint32;     // unavailable
    // The attestation bit is FALSE so the rail reads "not verified": this build performs no
    // attestation check and the server advertises none, so an affirmative would be a claim.
    row.inspect.attestationVerified = false;
    // RENDERED NOWHERE AND IT MUST STAY THAT WAY, so it is left empty rather than named.
    row.inspect.cipher.clear();
    row.inspect.groupIdHex = conv.groupIdHex;                    // real
    row.inspect.senderDisplayName = kUnavailable;
    row.inspect.sentAtLabel = FormatClock(m.sentAtMs);           // real, the sender's own claim
    row.inspect.receivedAtLabel = kUnavailable;
    row.inspect.deliveredTo.clear();
    row.inspect.readBy.clear();
    conv.rows.push_back(std::move(row));

    // THE REACTIONS STANDING ON IT, below the bubble. They are real records from real senders and
    // there is no slot on MessageRow for them; a System line states what stands rather than
    // dropping it.
    if (!m.reactions.empty()) {
      std::wstring list;
      for (auto const& r : m.reactions) {
        if (!list.empty()) list += L"  ";
        list += urnw::Widen(r.emoji);
      }
      conv.rows.push_back(MakeSystemRow(wideId + L"-reactions", L"Reactions: " + list));
    }

    if (m.sentAtMs >= newestMs) {
      newestMs = m.sentAtMs;
      newestBody = row.body;
    }
  }

  // The preview is the newest real body. ALWAYS NON-EMPTY is the struct's own contract, so an
  // empty conversation says so rather than drawing a blank line.
  conv.preview = newestBody.empty() ? std::wstring(kUnavailable) : newestBody;
  conv.timeLabel = newestMs > 0 ? FormatClock(newestMs) : std::wstring(kUnavailable);
  world.conversations.push_back(std::move(conv));

  // ── the world around it ─────────────────────────────────────────────────────

  // EMPTY, and the views render the placeholder. The protocol carries no device list: this process
  // knows itself and nothing else, and listing one device would be a claim that the account has
  // one.
  world.myDevices.clear();

  // THREE REAL HOPS. Every label and sub-label below is something this session observed: the
  // platform url the library actually dialled, and the message server's own client_id. hopMs is
  // left at 0, which the network page reads as "no figure for this hop" and draws nothing —
  // a timing this build does not measure is not printed as 0 ms.
  world.relayPath = {
      {L"This device", L"This computer", L"", 0, true},
      {L"URnetwork",
       group.platformUrl.empty() ? std::wstring(kUnavailable) : urnw::Widen(group.platformUrl),
       L"", 0, true},
      {L"Message server",
       group.serverClientId.empty() ? std::wstring(kUnavailable)
                                    : urnw::Widen(group.serverClientId),
       L"", 0, true},
  };

  world.server.host = urnw::Widen(group.host);
  world.server.jurisdiction = kUnavailable;
  world.server.latencyMs = 0;  // not measured; the page draws the placeholder for a non-positive one
  // FALSE, so the rail and the network page read "not verified". This build verifies no server
  // key, so the affirmative would be a claim it cannot make.
  world.server.keyVerified = false;

  world.currentEpoch = group.epoch;   // real
  world.connectState = urmsg::demo::ConnectState::Connected;  // real: the device is connected
  world.sessionMode = kUnavailable;
  world.recordsPerSecond = 0;
  return world;
}

// ── the handover ──────────────────────────────────────────────────────────────

void Publish(urmsg::demo::World world) {
  // AN UNCHANGED WORLD IS NOT PUBLISHED, and that is not an optimisation. The fetch loop runs
  // every three seconds for the life of the process, and the only way the thread view has to show
  // a new line is SetThreadConversation, which rebuilds every bubble and replays the open
  // entrance. Publishing an identical world would therefore re-animate the conversation the viewer
  // is reading, three times a minute, for ever. The digest covers everything a reader can SEE —
  // ids, bodies, system lines, times, sides — so a reaction landing on an old message, which moves
  // no count, still gets through.
  const uint64_t digest = Digest(world);
  std::function<void()> notify;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_world && digest == g_digest) return;
    g_digest = digest;
    g_world = std::make_shared<const urmsg::demo::World>(std::move(world));
    g_generation += 1;
    notify = g_onPublish;
  }
  // OUTSIDE THE LOCK. The callback marshals to the UI thread and a UI thread that happened to be
  // inside Snapshot() would otherwise be waiting on this one.
  if (notify) notify();
}

WorldPtr Snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_world;
}

void SetOnPublish(std::function<void()> onPublish) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_onPublish = std::move(onPublish);
}

uint64_t Generation() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_generation;
}

}  // namespace urmsg::live
