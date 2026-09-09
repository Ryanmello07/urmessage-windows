// SPDX-License-Identifier: MPL-2.0
//
// The pure half of ambient activity (Demo/DemoAutoplay.h says why this TU has
// no pch and no winrt): the delivery ladder, the cadence tables and the next
// ambient row, all asserted from CollectDiagnostics().

#include "Demo/DemoAutoplay.h"

#include <array>
#include <format>

namespace urmsg::demo {
namespace {

// Plausible and neutral (design doc 5): no real people, no recognisable handles,
// and nothing that states a message WAS encrypted as a fact about an operation
// that did not happen.
constexpr std::array<const wchar_t*, 4> kIncomingBodies{
    L"Pushed the retention change - take a look when you get a minute.",
    L"That works for me. Shall we say Thursday?",
    L"The Reykjavik node came back up about ten minutes ago.",
    L"Much better. That reads far cleaner than the old copy.",
};

// 3-6s of typing and ~40s between rounds, from fixed tables. A table rather than
// a random draw because the demo world is seeded and this has to be
// reproducible; four entries rather than one so two consecutive rounds do not
// look metronomic.
constexpr std::array<int64_t, 4> kTypingMs{3200, 5400, 4100, 5900};
constexpr std::array<int64_t, 4> kIdleMs{40000, 38500, 41500, 39500};

size_t Slot(int round, size_t size) {
  return static_cast<size_t>(round < 0 ? 0 : round) % size;
}

// The ambient row's clock, DERIVED (the d7 audit's W8-class-4 override): the
// last message's HH:MM plus (round + 1) * 3 minutes. NOT ::GetLocalTime — the
// machine's wall clock is non-deterministic and wrong-formatted: a 23:47
// evening-demo bubble under a thread whose last row says 12:11, and a bare
// "23:47" in the rail's Sent field where every seeded row shows "Today 12:11".
std::wstring NextTimeLabel(Conversation const& conversation, int round) {
  std::wstring last;
  for (auto const& row : conversation.rows)
    if (row.kind == RowKind::Message) last = row.timeLabel;
  // DemoWorld's table zero-pads HH:MM. A missing or malformed label (no
  // message rows yet) falls back to 00:00 plus the round's step: the digit
  // walk below reads non-digits as 0 rather than throwing, and the demo's own
  // labels are always well-formed.
  int hours = 0, minutes = 0;
  if (auto const colon = last.find(L':');
      colon != std::wstring::npos && 0 < colon && colon + 1 < last.size()) {
    for (size_t i = 0; i < colon; ++i)
      if (L'0' <= last[i] && last[i] <= L'9') hours = hours * 10 + (last[i] - L'0');
    for (size_t i = colon + 1; i < last.size(); ++i)
      if (L'0' <= last[i] && last[i] <= L'9') minutes = minutes * 10 + (last[i] - L'0');
  }
  // Modulo the day rather than printing 24:02 past midnight.
  const int total = (hours * 60 + minutes + (round + 1) * 3) % (24 * 60);
  return std::format(L"{:02}:{:02}", total / 60, total % 60);
}

}  // namespace

DeliveryState AdvanceDelivery(DeliveryState state) {
  switch (state) {
    case DeliveryState::Pending:
      return DeliveryState::Sent;
    case DeliveryState::Sent:
      return DeliveryState::Delivered;
    case DeliveryState::Delivered:
      return DeliveryState::Read;
    case DeliveryState::Read:
    case DeliveryState::Failed:
    case DeliveryState::Expired:
      break;
  }
  return state;
}

int64_t IdleMsForRound(int round) { return kIdleMs[Slot(round, kIdleMs.size())]; }
int64_t TypingMsForRound(int round) { return kTypingMs[Slot(round, kTypingMs.size())]; }

MessageRow IncomingForRound(Conversation const& conversation, int round) {
  // Inherit the identity of the last INCOMING row of this conversation, so the
  // ambient message comes from someone the thread has already seen rather than
  // from a stranger with a brand-new identicon.
  MessageRow const* last = nullptr;
  for (auto const& row : conversation.rows)
    if (row.kind == RowKind::Message && !row.outgoing) last = &row;

  MessageRow row{};
  row.kind = RowKind::Message;
  row.id = std::format(L"{}-ambient-{}", conversation.id, round);
  row.body = kIncomingBodies[Slot(round, kIncomingBodies.size())];
  row.timeLabel = NextTimeLabel(conversation, round);
  row.outgoing = false;
  // An incoming message: `state` is OUR read state of it, and we are reading it
  // right now.
  row.state = DeliveryState::Read;
  row.permanentRecord = false;
  row.senderKey = last ? last->senderKey : conversation.identityKey;
  // senderName is empty in DMs by contract; groups name the sender.
  if (conversation.kind == ConversationKind::Group) {
    row.senderName = (last && !last->senderName.empty())
                         ? last->senderName
                         : (conversation.members.empty()
                                ? std::wstring{}
                                : conversation.members.front().displayName);
  }

  auto const& world = GetWorld();
  auto& inspect = row.inspect;
  inspect.epoch = world.currentEpoch;
  inspect.senderLeafIndex = last ? last->inspect.senderLeafIndex : 1u;
  inspect.retention =
      conversation.disappearing ? RetentionClass::Eph : RetentionClass::Permanent;
  inspect.sizeBucket = L"<= 1 KiB";
  inspect.wireSizeBytes = static_cast<uint32_t>(row.body.size() * 2 + 96);
  inspect.attestationVerified = true;
  inspect.cipher = L"XChaCha20-Poly1305";
  inspect.groupIdHex = conversation.groupIdHex;
  // ALWAYS populated, even where senderName is deliberately empty (contract v2
  // section 1): the rail's "from" field must never be blank.
  inspect.senderDisplayName =
      row.senderName.empty() ? conversation.name : row.senderName;
  // The day prefix every seeded row carries (DemoWorld.cpp:175): the rail
  // renders Sent/Received raw, and a bare "12:14" where every other message
  // shows "Today 12:11" is the format drift the W8 override names. Ambient
  // rows only ever land in the OPEN conversation, whose tail sits under the
  // "Today" separator in the seeded world.
  inspect.sentAtLabel = L"Today " + row.timeLabel;
  inspect.receivedAtLabel = L"Today " + row.timeLabel;
  // Delivered to every device of ours; read by the ones that are online, since
  // this one is on screen.
  inspect.deliveredTo = world.myDevices;
  inspect.readBy.clear();
  for (auto const& device : world.myDevices)
    if (device.online) inspect.readBy.push_back(device);
  return row;
}

}  // namespace urmsg::demo
