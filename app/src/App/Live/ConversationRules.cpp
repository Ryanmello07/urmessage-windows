// SPDX-License-Identifier: MPL-2.0
//
// THE CONVERSATION LIST'S PURE RULES. Read ConversationRules.h first.
//
// NO PCH, NO winrt, NO XAML: App.vcxproj marks this unit PrecompiledHeader=NotUsing, for the reason
// Live\LiveWorld.cpp is marked so. CollectDiagnostics runs it before winrt::init_apartment.

#include "Live/ConversationRules.h"

#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "Live/LiveWorld.h"
#include "Strings.h"

// The kind codes. This TU does not link the SDK; the header is include-only for these defines, as
// it is for LiveWorld.cpp.
extern "C" {
#include "urnetwork_message.h"
}

namespace urmsg::live {
namespace {

// "  key              : PASS  detail": two spaces, a 17-wide key, ": " (Startup.cpp's own shape).
std::wstring Line(wchar_t const* key, bool pass, std::wstring const& detail) {
  return std::format(L"  {:<17}: {}  {}", key, pass ? L"PASS" : L"FAIL", detail);
}

// A group built here, with a peer who wrote `lines` (body, sent_at_ms) and this device beside it.
// Every value is typed for this check; nothing in it is a fabricated conversation that reaches a
// screen, and nothing is read from the fixture.
LiveGroup MakeGroup(std::string const& gid, uint64_t epoch,
                    std::vector<std::pair<std::string, int64_t>> const& lines, char peerKey,
                    int64_t adoptedAtMs = 0) {
  LiveGroup g;
  g.groupIdHex = gid;
  g.epoch = epoch;
  g.open = true;
  g.myRole = "member";
  g.adoptedAtMs = adoptedAtMs;
  LiveMember me;
  me.leafIndex = 0;
  me.senderHandle = std::string(32, 'a');
  me.identityPub = std::string(64, '1');
  me.role = "member";
  me.mine = true;
  LiveMember peer;
  peer.leafIndex = 1;
  peer.senderHandle = std::string(32, 'b');
  peer.identityPub = std::string(64, peerKey);
  peer.role = "owner";
  peer.mine = false;
  g.members = {me, peer};
  int n = 0;
  for (auto const& [body, at] : lines) {
    ++n;
    LiveMessage m;
    m.recordId = static_cast<uint64_t>(n);
    // a 64-hex message id unique to this group and line
    m.messageId = std::string(60, gid.empty() ? '0' : gid[0]) + std::format("{:04x}", n);
    m.senderHandle = peer.senderHandle;
    m.senderIdentity = peer.identityPub;
    m.mine = false;
    m.sentAtMs = at;
    m.kind = URNET_MESSAGE_KIND_TEXT;
    m.body = body;
    m.bodyLen = static_cast<int32_t>(body.size());
    g.messages.push_back(std::move(m));
  }
  return g;
}

std::vector<std::wstring> MessageBodies(urmsg::demo::Conversation const& c) {
  std::vector<std::wstring> out;
  for (auto const& row : c.rows)
    if (row.kind == urmsg::demo::RowKind::Message) out.push_back(row.body);
  return out;
}

// conv world: THREE GROUPS GIVE THREE CONVERSATIONS, each with only its own rows, members and epoch,
// and ONE GROUP GIVES TODAY'S WORLD. A world built from the first group alone (the one-group
// design) fails it, and so does any mix of one group's rows into another's conversation.
std::wstring ConversationWorldLine() {
  const int64_t now = 1'800'000'000'000;
  const LiveGroup g1 = MakeGroup(std::string(64, 'a'), 1, {{"first group, line 1", now - 3000}}, '2');
  const LiveGroup g2 = MakeGroup(std::string(64, 'b'), 2,
                                 {{"second group, line 1", now - 2000}, {"second group, line 2", now - 1500}},
                                 '3');
  const LiveGroup g3 = MakeGroup(std::string(64, 'c'), 3, {{"third group, line 1", now - 1000}}, '4');
  LiveSession session;
  session.serverClientId = "diagnostic-server";
  session.platformUrl = "wss://diagnostic.invalid/urmessage/v1";
  session.host = "direct";
  session.keyPinned = true;
  std::vector<LiveConversation> three;
  three.push_back(BuildLiveConversation(g1, now));
  three.push_back(BuildLiveConversation(g2, now));
  three.push_back(BuildLiveConversation(g3, now));
  const urmsg::demo::World w = BuildWorld(session, std::move(three));

  struct Want {
    LiveGroup const* group;
    std::vector<std::wstring> bodies;
  };
  const std::vector<Want> wants = {
      {&g1, {L"first group, line 1"}},
      {&g2, {L"second group, line 1", L"second group, line 2"}},
      {&g3, {L"third group, line 1"}},
  };
  std::size_t own = 0;
  for (auto const& want : wants) {
    const std::wstring id = L"live-" + urnw::Widen(want.group->groupIdHex);
    for (auto const& c : w.conversations) {
      if (c.id != id) continue;
      if (MessageBodies(c) == want.bodies && c.epoch == want.group->epoch && c.members.size() == 2 &&
          c.groupIdHex == urnw::Widen(want.group->groupIdHex))
        ++own;
      break;
    }
  }
  const bool sessionOnce = w.relayPath.size() == 3 && w.server.keyVerified &&
                           w.server.host == L"direct" &&
                           w.relayPath[2].subLabel == L"diagnostic-server";

  // ONE GROUP, as the one-group callers build it: one conversation, the same rows and epoch, and
  // the world's epoch that group's own.
  const urmsg::demo::World one = BuildWorld(g2);
  const bool oneOk = one.conversations.size() == 1 &&
                     MessageBodies(one.conversations.front()) == wants[1].bodies &&
                     one.conversations.front().epoch == 2 && one.currentEpoch == 2;

  const bool ok = w.conversations.size() == 3 && own == 3 && sessionOnce && oneOk;
  return Line(L"conv world", ok,
              std::format(L"3 groups give {} conversations, {} of them with only their own rows, "
                          L"members and epoch (1/2/3), the session's facts once {}; one group "
                          L"gives {} conversation, its epoch the world's {}   [query: BuildWorld "
                          L"over three LiveGroups and over one, the conversation-list design A14]",
                          w.conversations.size(), own, sessionOnce ? L"yes" : L"NO",
                          one.conversations.size(), oneOk ? L"yes" : L"NO"));
}

// conv order: NEWEST ACTIVITY FIRST, a future time CLAMPED to now, a group with no line at the time
// it was taken up, and a tie broken by the group id. Each clause has a group that only it orders.
std::wstring ConversationOrderLine() {
  const int64_t now = 1'800'000'000'000;
  std::vector<LiveConversation> list;
  // A and E tie on time; E's id is smaller, so E comes first
  list.push_back(BuildLiveConversation(MakeGroup("aa" + std::string(62, '0'), 1, {{"a", now - 9000}}, '2'), now));
  list.push_back(BuildLiveConversation(MakeGroup("bb" + std::string(62, '0'), 1, {{"b", now - 5000}}, '2'), now));
  // C claims a line from the future: clamped to now, it ties with F, whose id is smaller
  list.push_back(BuildLiveConversation(MakeGroup("c3" + std::string(62, '0'), 1, {{"c", now + 3'600'000}}, '2'), now));
  // D has no line yet: it is placed at the time it was taken up
  list.push_back(BuildLiveConversation(MakeGroup("dd" + std::string(62, '0'), 1, {}, '2', now - 2000), now));
  list.push_back(BuildLiveConversation(MakeGroup("1e" + std::string(62, '0'), 1, {{"e", now - 9000}}, '2'), now));
  list.push_back(BuildLiveConversation(MakeGroup("0f" + std::string(62, '0'), 1, {{"f", now}}, '2'), now));
  OrderConversations(list);
  std::wstring got;
  for (auto const& c : list) {
    if (!got.empty()) got += L" ";
    got += urnw::Widen(c.groupIdHex.substr(0, 2));
  }
  const std::wstring want = L"0f c3 dd bb 1e aa";
  bool clampOk = false;
  bool adoptedOk = false;
  for (auto const& c : list) {
    if (c.groupIdHex.starts_with("c3")) clampOk = c.activityMs == now;
    if (c.groupIdHex.starts_with("dd")) adoptedOk = c.activityMs == now - 2000;
  }
  const bool ok = got == want && clampOk && adoptedOk;
  return Line(L"conv order", ok,
              std::format(L"order \"{}\" (want \"{}\"), a line from the future clamped to now {}, "
                          L"a group with no line at the time it was taken up {}   [query: "
                          L"OrderConversations over six built conversations, the conversation-list "
                          L"design 2.4]",
                          got, want, clampOk ? L"yes" : L"NO", adoptedOk ? L"yes" : L"NO"));
}

}  // namespace

std::vector<std::wstring> CollectConversationRulesDiagnostics() {
  std::vector<std::wstring> lines;
  lines.push_back(ConversationWorldLine());
  lines.push_back(ConversationOrderLine());
  return lines;
}

}  // namespace urmsg::live
