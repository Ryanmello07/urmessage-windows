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
#include <string_view>
#include <utility>
#include <vector>

#include "Live/LiveWorld.h"
#include "Live/RefusalCopy.h"
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

// conv gate: EACH ACTION GOES TO THE GROUP IT NAMES. Three groups, each in a different state; an
// action for each, one for a group not held, and stamps from an older incarnation and from another
// group, before and after one of the groups is left.
std::wstring ConversationGateLine() {
  const std::string g1(64, 'a');
  const std::string g2(64, 'b');
  const std::string g3(64, 'c');
  std::vector<GroupGate> gates = {{g1, 7, true, false}, {g2, 8, false, false}, {g3, 9, true, true}};
  struct Ask {
    std::string const* gid;
    ActionClass cls;
    GateVerdict want;
    wchar_t const* what;
  };
  const std::string none(64, 'f');
  const std::vector<Ask> asks = {
      {&g1, ActionClass::Commit, GateVerdict::Take, L"a send to an open group is taken"},
      {&g2, ActionClass::Commit, GateVerdict::Closed, L"a send to a closed group is refused"},
      {&g2, ActionClass::Leave, GateVerdict::Take, L"a closed group can still be left"},
      {&g2, ActionClass::LeaveHandOver, GateVerdict::Closed, L"a hand-over needs an open group"},
      {&g3, ActionClass::Commit, GateVerdict::Leaving, L"nothing is taken while its leave is in flight"},
      {&g3, ActionClass::Leave, GateVerdict::Leaving, L"one leave at a time"},
      {&g2, ActionClass::Add, GateVerdict::Take, L"an add to a group not open yet (a group of one) is taken"},
      {&g3, ActionClass::Add, GateVerdict::Leaving, L"no add while its leave is in flight"},
      {&none, ActionClass::Add, GateVerdict::NotHeld, L"an add to a group not held is refused"},
      {&none, ActionClass::Commit, GateVerdict::NotHeld, L"a group not held takes nothing"},
  };
  std::size_t asked = 0;
  std::size_t right = 0;
  std::wstring firstBad;
  for (auto const& a : asks) {
    ++asked;
    const GateVerdict got = GateAction(gates, *a.gid, a.cls);
    if (got == a.want) {
      ++right;
    } else if (firstBad.empty()) {
      firstBad = std::format(L"{} (got {})", a.what, GateVerdictName(got));
    }
  }
  // the stamps: the current incarnation holds; an older one of the same group, or another
  // group's number under this group's id, does not
  const bool stampsOk = ActionStillHeld(gates, g1, 7) && !ActionStillHeld(gates, g1, 6) &&
                        !ActionStillHeld(gates, g2, 7) && !ActionStillHeld(gates, none, 7);
  // A LEAVE ENDS ONE GROUP: g2 goes, and g1 and g3 are exactly as they were
  gates.erase(gates.begin() + 1);
  const bool leaveOk = GateAction(gates, g1, ActionClass::Commit) == GateVerdict::Take &&
                       ActionStillHeld(gates, g1, 7) && !ActionStillHeld(gates, g2, 8) &&
                       GateAction(gates, g2, ActionClass::Commit) == GateVerdict::NotHeld &&
                       GateAction(gates, g3, ActionClass::Commit) == GateVerdict::Leaving;
  const bool ok = firstBad.empty() && stampsOk && leaveOk;
  return Line(L"conv gate", ok,
              std::format(L"{} of {} actions answered by their own group's gate{}{}; stamps of an "
                          L"older incarnation or another group dropped {}; a leave of one group "
                          L"leaves the others as they were {}   [query: GateAction and "
                          L"ActionStillHeld over three gates, the conversation-list design 2.2]",
                          right, asked,
                          firstBad.empty() ? L"" : L", first wrong: ", firstBad,
                          stampsOk ? L"yes" : L"NO", leaveOk ? L"yes" : L"NO"));
}

// conv push: A PUSH WAKES THE LOOP OF THE GROUP IT NAMES, AND NO OTHER. Three groups held; a push for
// each of two, one in upper case, the empty id, an id not held, and the id of a group just left. A
// router that wakes every loop for every push (one loop's rule, kept for many) fails it, and so does
// one that wakes a loop for a group this device does not hold.
std::wstring ConversationPushLine() {
  const std::string g1(64, 'a');
  const std::string g2(64, 'b');
  const std::string g3 = std::string(62, 'c') + "0d";
  std::vector<std::string> held = {g1, g2, g3};
  struct Push {
    std::string pushed;
    PushRoute want;
    std::string wantKey;
    wchar_t const* what;
  };
  std::string upper3 = g3;
  for (char& ch : upper3)
    if (ch >= 'a' && ch <= 'f') ch = static_cast<char>(ch - 'a' + 'A');
  const std::vector<Push> pushes = {
      {g2, PushRoute::One, g2, L"a push for a held group wakes that group's loop"},
      {upper3, PushRoute::One, g3, L"an id in upper case is the same group"},
      {std::string(), PushRoute::All, std::string(), L"the empty id wakes every loop"},
      {std::string(64, 'f'), PushRoute::None, std::string(), L"an id not held wakes none"},
  };
  std::size_t asked = 0;
  std::size_t right = 0;
  std::wstring firstBad;
  for (auto const& p : pushes) {
    ++asked;
    const PushRoute got = RoutePush(held, p.pushed);
    const bool keyOk = got != PushRoute::One || PushGroupKey(p.pushed) == p.wantKey;
    if (got == p.want && keyOk) {
      ++right;
    } else if (firstBad.empty()) {
      firstBad = std::format(L"{} (got {})", p.what, PushRouteName(got));
    }
  }
  // A LEFT GROUP'S PUSH WAKES NOTHING: g2 goes, and a push for it is a push for a group not held,
  // while g1's still wakes g1's loop
  held.erase(held.begin() + 1);
  const bool leftOk = RoutePush(held, g2) == PushRoute::None && RoutePush(held, g1) == PushRoute::One;
  // AND WITH NO GROUP HELD, a named push wakes nothing
  const bool noneOk = RoutePush({}, g1) == PushRoute::None;
  const bool ok = firstBad.empty() && leftOk && noneOk;
  return Line(L"conv push", ok,
              std::format(L"{} of {} pushes routed to their own group's loop{}{}; a push for a group "
                          L"just left wakes nothing {}; with no group held a named push wakes "
                          L"nothing {}   [query: RoutePush over three held ids, the "
                          L"conversation-list design 2.1]",
                          right, asked, firstBad.empty() ? L"" : L", first wrong: ", firstBad,
                          leftOk ? L"yes" : L"NO", noneOk ? L"yes" : L"NO"));
}

// conv create: A NEW CONVERSATION IS REFUSED ONLY WHILE A GROUP OF ONE WAITS. None held, three open,
// three held of which one is closed on the server but not a group of one, one of them a group of
// one, and that group after its first member opened it. The one-group refusal (no create while
// anything is held) fails it, and so does a create that ignores the group of one.
std::wstring ConversationCreateLine() {
  const std::string g1(64, 'a');
  const std::string g2(64, 'b');
  const std::string g3(64, 'c');
  struct Case {
    std::vector<HeldForCreate> held;
    CreateVerdict want;
    std::string wantWaiting;
    wchar_t const* what;
  };
  const std::vector<Case> cases = {
      {{}, CreateVerdict::Take, std::string(), L"none held: made"},
      {{{g1, false}, {g2, false}, {g3, false}}, CreateVerdict::Take, std::string(),
       L"three conversations held: a fourth is made"},
      {{{g1, false}, {g2, true}, {g3, false}}, CreateVerdict::GroupOfOneWaits, g2,
       L"a group of one waits: refused, naming it"},
      {{{g2, true}}, CreateVerdict::GroupOfOneWaits, g2, L"only a group of one: refused, naming it"},
      {{{g1, false}, {g2, false}}, CreateVerdict::Take, std::string(),
       L"the group of one opened by its first member: made"},
  };
  std::size_t asked = 0;
  std::size_t right = 0;
  std::wstring firstBad;
  for (auto const& c : cases) {
    ++asked;
    std::string waiting = "unset";
    const CreateVerdict got = CreateAction(c.held, waiting);
    const bool waitingOk = got == CreateVerdict::Take ? waiting.empty() : waiting == c.wantWaiting;
    if (got == c.want && waitingOk) {
      ++right;
    } else if (firstBad.empty()) {
      firstBad = std::format(L"{} (got {})", c.what, CreateVerdictName(got));
    }
  }
  const bool ok = firstBad.empty();
  return Line(L"conv create", ok,
              std::format(L"{} of {} creates answered by the rule{}{}: refused only while a group "
                          L"of one waits, and the refusal names it   [query: CreateAction over five "
                          L"tables of held groups, the conversation-list design 2.3]",
                          right, asked, firstBad.empty() ? L"" : L", first wrong: ", firstBad));
}

// conv notice: A GROUP THAT DOES NOT RESTORE IS SAID BESIDE THE ONES THAT DO. Each class's restore
// error with no group back and with two back, a clean restore, a damaged record (Task 58 says
// nothing of it), and a restore that could not be tried. The one-group rule (RestoreNote's
// suppression, which drops the sentence when any group came back) fails the two-back cases.
std::wstring ConversationNoticeLine() {
  const std::string hex(64, 'a');
  const std::string predates =
      "urmessage: restore: group " + hex + " at epoch 1: " + std::string(kLibGroupPredatesJoinSeal);
  const std::string notRead = "urmessage: restore: group " + hex + ": " +
                              std::string(kLibGroupRecordNotReadNow);
  const std::string unproven = "urmessage: restore: group " + hex + ": " +
                               std::string(kLibGroupReadUnproven);
  const std::string notConnected = std::string(kLibNotConnected);
  struct Case {
    std::string error;
    int restored;
    std::string_view want;
    wchar_t const* what;
  };
  const std::vector<Case> cases = {
      {predates, 0, kSayGroupPredatesJoinSeal, L"predates, none back"},
      {predates, 2, kSayGroupPredatesJoinSeal, L"predates, two back"},
      {notRead, 2, kSayGroupRecordNotReadNow, L"not read now, two back"},
      {unproven, 1, kSayGroupReadUnproven, L"unproven, one back"},
      {std::string(), 2, std::string_view(), L"a clean restore, two back"},
      {"urmessage: this group could not be restored from the durable state store: the record in " +
           hex + ": short",
       2, std::string_view(), L"a damaged record, two back"},
  };
  std::size_t asked = 0;
  std::size_t right = 0;
  std::wstring firstBad;
  for (auto const& c : cases) {
    ++asked;
    if (ListNoticeAfterRestore(c.error, c.restored) == c.want) {
      ++right;
    } else if (firstBad.empty()) {
      firstBad = c.what;
    }
  }
  // the next notice: a restore not tried keeps it, a clean one clears it, another class replaces it
  const std::string kept = NextListNotice(kSayGroupPredatesJoinSeal, notConnected, 0);
  const std::string cleared = NextListNotice(kSayGroupPredatesJoinSeal, std::string(), 1);
  const std::string replaced = NextListNotice(kSayGroupPredatesJoinSeal, notRead, 1);
  const bool nextOk = kept == kSayGroupPredatesJoinSeal && cleared.empty() &&
                      replaced == kSayGroupRecordNotReadNow;
  const bool ok = firstBad.empty() && nextOk;
  return Line(L"conv notice", ok,
              std::format(L"{} of {} restores give the list the sentence for their class, whatever "
                          L"came back{}{}; a restore not tried keeps the notice, a clean one clears "
                          L"it, another class replaces it {}   [query: ListNoticeAfterRestore and "
                          L"NextListNotice over Task 58's restore errors, the conversation-list "
                          L"design 2.5]",
                          right, asked, firstBad.empty() ? L"" : L", first wrong: ", firstBad,
                          nextOk ? L"yes" : L"NO"));
}

}  // namespace

std::string_view ListNoticeAfterRestore(std::string_view restoreError, int /*restoredCount*/) {
  // THE DIALOG'S RULE WITH NOTHING COUNTED AS BACK: RestoreNote drops the sentence when a group came
  // back, because the join dialog is then not opened at all; the list draws it beside those groups.
  return RestoreNote(restoreError, 0);
}

std::string NextListNotice(std::string_view previous, std::string_view restoreError,
                           int restoredCount) {
  if (IsNotConnected(restoreError)) return std::string(previous);
  return std::string(ListNoticeAfterRestore(restoreError, restoredCount));
}

CreateVerdict CreateAction(std::vector<HeldForCreate> const& held, std::string& waiting) {
  waiting.clear();
  for (auto const& group : held) {
    if (group.groupOfOne) {
      waiting = group.groupIdHex;
      return CreateVerdict::GroupOfOneWaits;
    }
  }
  return CreateVerdict::Take;
}

std::wstring_view CreateVerdictName(CreateVerdict verdict) {
  switch (verdict) {
    case CreateVerdict::Take: return L"Take";
    case CreateVerdict::GroupOfOneWaits: return L"GroupOfOneWaits";
  }
  return L"?";
}

std::string PushGroupKey(std::string_view pushed) {
  std::string key(pushed);
  for (char& ch : key)
    if (ch >= 'A' && ch <= 'F') ch = static_cast<char>(ch - 'A' + 'a');
  return key;
}

PushRoute RoutePush(std::vector<std::string> const& heldIds, std::string_view pushed) {
  if (pushed.empty()) return PushRoute::All;
  const std::string key = PushGroupKey(pushed);
  for (auto const& id : heldIds)
    if (id == key) return PushRoute::One;
  return PushRoute::None;
}

std::wstring_view PushRouteName(PushRoute route) {
  switch (route) {
    case PushRoute::One: return L"One";
    case PushRoute::All: return L"All";
    case PushRoute::None: return L"None";
  }
  return L"?";
}

GroupGate const* FindGate(std::vector<GroupGate> const& gates, std::string const& groupIdHex) {
  for (auto const& gate : gates)
    if (gate.groupIdHex == groupIdHex) return &gate;
  return nullptr;
}

GateVerdict GateAction(std::vector<GroupGate> const& gates, std::string const& groupIdHex,
                       ActionClass cls) {
  GroupGate const* gate = FindGate(gates, groupIdHex);
  if (gate == nullptr) return GateVerdict::NotHeld;
  if (gate->leaving) return GateVerdict::Leaving;
  if (cls != ActionClass::Leave && cls != ActionClass::Add && !gate->open) return GateVerdict::Closed;
  return GateVerdict::Take;
}

bool ActionStillHeld(std::vector<GroupGate> const& gates, std::string const& groupIdHex,
                     uint64_t incarnation) {
  GroupGate const* gate = FindGate(gates, groupIdHex);
  return gate != nullptr && gate->incarnation == incarnation;
}

std::wstring_view GateVerdictName(GateVerdict verdict) {
  switch (verdict) {
    case GateVerdict::Take: return L"Take";
    case GateVerdict::NotHeld: return L"NotHeld";
    case GateVerdict::Closed: return L"Closed";
    case GateVerdict::Leaving: return L"Leaving";
  }
  return L"?";
}

std::vector<std::wstring> CollectConversationRulesDiagnostics() {
  std::vector<std::wstring> lines;
  lines.push_back(ConversationWorldLine());
  lines.push_back(ConversationOrderLine());
  lines.push_back(ConversationGateLine());
  lines.push_back(ConversationPushLine());
  lines.push_back(ConversationCreateLine());
  lines.push_back(ConversationNoticeLine());
  return lines;
}

}  // namespace urmsg::live
