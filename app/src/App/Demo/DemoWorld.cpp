// SPDX-License-Identifier: MPL-2.0
//
// NO pch.h include, and App.vcxproj compiles this unit with
// PrecompiledHeader=NotUsing: the project pch pulls in every winrt/ header,
// and this file runs before init_apartment. See DemoWorld.h.
#include "Demo/DemoWorld.h"

#include <string_view>

namespace urmsg::demo {
namespace {

// FNV-1a 64 over the utf-16 code units, then splitmix64 to fill 32 bytes.
// Pure arithmetic: same id -> same seed in every process, on every machine,
// for ever. That is the whole contract of this file.
constexpr uint64_t Fnv1a64(std::wstring_view s) {
  uint64_t h = 1469598103934665603ull;
  for (wchar_t c : s) {
    h ^= static_cast<uint64_t>(static_cast<uint16_t>(c));
    h *= 1099511628211ull;
  }
  return h;
}

constexpr uint64_t SplitMix64(uint64_t& state) {
  state += 0x9E3779B97F4A7C15ull;
  uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

Seed SeedFrom(std::wstring_view id) {
  uint64_t state = Fnv1a64(id);
  Seed out{};
  for (size_t i = 0; i < out.size(); i += 8) {   // 32 / 8 = exactly 4 rounds
    const uint64_t v = SplitMix64(state);
    for (size_t b = 0; b < 8; ++b)
      out[i + b] = static_cast<uint8_t>((v >> (b * 8)) & 0xFFu);
  }
  return out;
}

std::wstring HexOf(Seed const& s, size_t bytes) {
  static constexpr wchar_t kDigits[] = L"0123456789abcdef";
  std::wstring out;
  out.reserve(bytes * 2);
  for (size_t i = 0; i < bytes && i < s.size(); ++i) {
    out.push_back(kDigits[(s[i] >> 4) & 0x0Fu]);
    out.push_back(kDigits[s[i] & 0x0Fu]);
  }
  return out;
}

constexpr uint64_t kCurrentEpoch = 4182;

DeviceRef MakeDevice(std::wstring id, std::wstring name, std::wstring ownerName,
                     std::wstring lastSeen, bool online, bool thisComputer) {
  DeviceRef d;
  d.id = std::move(id);
  d.name = std::move(name);
  d.ownerName = std::move(ownerName);
  d.ownerKey = SeedFrom(d.ownerName);
  d.lastSeenLabel = std::move(lastSeen);
  d.online = online;
  d.isThisComputer = thisComputer;
  return d;
}

// `role` is one of DemoWorld.h's four spellings. A FABRICATED role, like every other value in this
// file: the fixture's viewer ("Rowan Ashby", the myDevices owner) is not among the members it
// lists, so `mine` is never set here and the viewer's own role is Conversation::myRole, written
// per conversation below.
struct PersonSpec { const wchar_t* id; const wchar_t* name; const wchar_t* role; };

MemberRef MakeMember(PersonSpec const& p) {
  MemberRef m;
  m.id = p.id;
  m.displayName = p.name;
  m.identityKey = SeedFrom(m.id);
  m.role = p.role;
  m.mine = false;
  m.identityPubHex.clear();  // the fixture carries no identity key; the rail offers no control
  // One device each; a member who administers the group (owner or admin) gets
  // a second, so the rail's delivered-to and read-by lists are longer than the
  // member list and cannot be mistaken for it. Spec C 5.3: delivered is a
  // statement by a DEVICE, never by a person.
  m.devices.push_back(
      MakeDevice(m.id + L"-phone", L"Phone", m.displayName, L"now", true, false));
  if (CanAdminister(m))
    m.devices.push_back(
        MakeDevice(m.id + L"-laptop", L"Laptop", m.displayName, L"12 min ago", false, false));
  return m;
}

struct RowSpec {
  RowKind kind;
  const wchar_t* senderId;   // member id; L"" for outgoing and non-message rows
  const wchar_t* body;       // message body, day label, or system sentence
  const wchar_t* time;       // L"14:22"; empty on a separator
  bool outgoing;
  DeliveryState state;       // meaningful only on an outgoing Message row
  const wchar_t* failure;    // non-empty ONLY when state == Failed
  bool permanent;            // the key-change record
  // MessageRow::senderRoleAtSend (item 242 R4): the role this row's sender held
  // at the epoch it was sealed at. DEFAULTED TO EMPTY, which is what the fixture
  // says about almost every row and is the honest answer for a world with no
  // protocol under it — "" is "no source carries one", and it is NOT "observer".
  // Two rows set it on purpose (c0's 12:02 and 12:05 below): one "member" and
  // one "observer", so the collapse rule has a positive control standing beside
  // its subject in the same conversation.
  const wchar_t* roleAtSend = L"";
};

MemberRef const* FindMember(Conversation const& c, std::wstring_view id) {
  for (auto const& m : c.members)
    if (m.id == id) return &m;
  return nullptr;
}

// Every device of every member, for the delivered-to / read-by lists.
std::vector<DeviceRef> AllMemberDevices(Conversation const& c) {
  std::vector<DeviceRef> out;
  for (auto const& m : c.members)
    for (auto const& d : m.devices) out.push_back(d);
  return out;
}

void AppendRows(Conversation& c, size_t conversationIndex,
                std::vector<DeviceRef> const& myDevices,
                std::initializer_list<RowSpec> specs) {
  std::wstring day = L"Today";      // a conversation with no separator is today
  size_t index = 0;
  const size_t total = specs.size();
  std::wstring previousSender;      // for the run-continuation rule

  for (RowSpec const& s : specs) {
    MessageRow r{};
    r.kind = s.kind;
    r.id = L"c" + std::to_wstring(conversationIndex) + L"-r" + std::to_wstring(index);
    r.body = s.body;
    r.timeLabel = s.time;
    r.outgoing = s.outgoing;
    r.state = s.state;
    r.failureReason = s.failure;
    r.permanentRecord = s.permanent;
    r.senderRoleAtSend = s.roleAtSend;

    if (s.kind == RowKind::DaySeparator) {
      day = s.body;
      previousSender.clear();
    } else if (s.kind == RowKind::System) {
      r.systemText = s.body;
      r.body.clear();
      previousSender.clear();
    }

    MemberRef const* sender = FindMember(c, s.senderId);
    const bool group = (c.kind == ConversationKind::Group);
    const std::wstring senderDisplay =
        s.outgoing ? std::wstring(L"You")
                   : (sender ? sender->displayName : std::wstring(L"System"));

    if (s.kind == RowKind::Message) {
      r.senderKey = s.outgoing ? SeedFrom(L"me-rowan-ashby")
                               : (sender ? sender->identityKey : Seed{});
      // senderName is the BUBBLE's name line: empty on an outgoing row,
      // empty in a DM (there is only one other person), and empty on a run
      // continuation. inspect.senderDisplayName is always populated - the
      // rail must never say "unknown" for a message it is inspecting.
      const bool continuation = (!previousSender.empty() && previousSender == s.senderId);
      if (group && !s.outgoing && !continuation) r.senderName = senderDisplay;
      // A COLLAPSED OBSERVER ROW BREAKS THE RUN HERE TOO (item 242 R4), exactly
      // as a separator and a system row do two branches up. This IS the rule the
      // view keeps (Views/ThreadLayout.h's planner and Demo/ThreadLayout.h's
      // StartsRun), stated where the fixture states runs — and without it the two
      // disagreed, silently and only on screen: the row under the collapsed line
      // got the identicon the PLAN asked for and no name, because the DATA still
      // thought it was a continuation. No gate saw it; the screenshot did. T2
      // reads inspect.senderDisplayName, which is populated on every row, and T4
      // compares the plan against the rule rather than against the bytes.
      previousSender = (s.outgoing || s.roleAtSend == std::wstring_view(kRoleObserver))
                           ? std::wstring()
                           : std::wstring(s.senderId);
    }

    MessageInspect& n = r.inspect;
    n.epoch = kCurrentEpoch - (static_cast<uint64_t>(total) - 1 - index);
    n.senderLeafIndex = 0;
    if (!s.outgoing && sender) {
      for (size_t i = 0; i < c.members.size(); ++i)
        if (c.members[i].id == sender->id) n.senderLeafIndex = static_cast<uint32_t>(i + 1);
    }
    n.retention = c.disappearing ? RetentionClass::Eph : RetentionClass::Permanent;
    const size_t len = std::wstring_view(s.body).size();
    n.sizeBucket = (len < 80) ? L"<= 1 KiB" : L"<= 4 KiB";
    n.wireSizeBytes = static_cast<uint32_t>(288 + len * 2);
    n.attestationVerified = (s.state != DeliveryState::Failed);
    n.cipher = L"XChaCha20-Poly1305";
    n.groupIdHex = c.groupIdHex;
    n.senderDisplayName = senderDisplay;
    n.sentAtLabel = day + L" " + std::wstring(s.time);

    if (s.outgoing) {
      // Nothing has been received until a DEVICE said so.
      const bool reached = (s.state == DeliveryState::Delivered ||
                            s.state == DeliveryState::Read);
      if (reached) {
        n.receivedAtLabel = n.sentAtLabel;
        n.deliveredTo = AllMemberDevices(c);
        if (s.state == DeliveryState::Read) n.readBy = n.deliveredTo;
      }
    } else if (s.kind == RowKind::Message) {
      n.receivedAtLabel = n.sentAtLabel;
      n.deliveredTo = myDevices;
      n.readBy = myDevices;
    }

    c.rows.push_back(std::move(r));
    ++index;
  }
}

// `myRole` is the VIEWER's fabricated role in this conversation (Conversation::myRole). The two
// groups are given different ones on purpose, so the fixture exercises both control sets the
// rail can draw (Views/RosterRules.h): in "Design team" the viewer is an ADMIN under Mira, who
// owns it; in "URnetwork core" the viewer is the OWNER and Mira and Hana are its admins. A group
// has exactly one owner (MASTER section 11), so whichever of the two holds it, no member row of
// that group may also say owner. A DM is a two-member group the viewer founded: owner over one
// member.
Conversation MakeConversation(const wchar_t* id, ConversationKind kind, const wchar_t* name,
                              const wchar_t* myRole,
                              std::initializer_list<PersonSpec> people,
                              const wchar_t* preview, const wchar_t* time, int unread,
                              bool muted, bool disappearing,
                              const wchar_t* retention, const wchar_t* mediaRetention) {
  Conversation c;
  c.id = id;
  c.kind = kind;
  c.name = name;
  c.identityKey = SeedFrom(c.id);
  c.groupIdHex = (kind == ConversationKind::Group) ? HexOf(c.identityKey, 8) : L"";
  c.preview = preview;                       // never empty, INCLUDING the EPH one
  c.timeLabel = time;
  c.unread = unread;
  c.muted = muted;
  c.disappearing = disappearing;
  c.myRole = myRole;
  for (PersonSpec const& p : people) c.members.push_back(MakeMember(p));
  c.memberCount = static_cast<int>(c.members.size());
  c.retentionLabel = retention;
  c.mediaRetentionLabel = mediaRetention;
  return c;
}

constexpr auto kSent = DeliveryState::Sent;
constexpr auto kDeliv = DeliveryState::Delivered;
constexpr auto kRead = DeliveryState::Read;
constexpr auto kPend = DeliveryState::Pending;
constexpr auto kFail = DeliveryState::Failed;
constexpr auto kMsg = RowKind::Message;
constexpr auto kSep = RowKind::DaySeparator;
constexpr auto kSys = RowKind::System;

World BuildWorld() {
  World w;
  w.currentEpoch = kCurrentEpoch;
  w.connectState = ConnectState::Connected;
  w.sessionMode = L"direct";
  w.recordsPerSecond = 12;

  // THREE devices, and myDevices[0] is this computer. Exactly one true.
  w.myDevices.push_back(MakeDevice(L"dev-desktop", L"Windows desktop", L"Rowan Ashby",
                                   L"now", true, true));
  w.myDevices.push_back(MakeDevice(L"dev-pixel9", L"Pixel 9", L"Rowan Ashby",
                                   L"2 min ago", true, false));
  w.myDevices.push_back(MakeDevice(L"dev-laptop", L"Linux laptop", L"Rowan Ashby",
                                   L"3 days ago", false, false));

  // c0 - Design team, 5 members, 24 rows.
  Conversation c0 = MakeConversation(
      L"conv-design-team", ConversationKind::Group, L"Design team", kRoleAdmin,
      {{L"m-mira", L"Mira Okonkwo", kRoleOwner},
       {L"m-tobias", L"Tobias Lind", kRoleAdmin},
       {L"m-saoirse", L"Saoirse Kelly", kRoleMember},
       {L"m-ravi", L"Ravi Menon", kRoleMember},
       // ELENA IS THE FIXTURE'S OBSERVER (item 242 R4). She is here so three
       // surfaces have a subject without the mesh: the rail's observer row and
       // its Spec C section 5.6 caveat, the admin's one control over an observer
       // (Make member), and the collapsed row her 12:05 line draws below.
       {L"m-elena", L"Elena Vasquez", kRoleObserver}},
      L"Retrying the attachment.", L"12:11", 0, false, false,
      L"Kept until deleted", L"Media kept 30 days");
  AppendRows(c0, 0, w.myDevices, {
    {kSep, L"", L"Yesterday", L"", false, kSent, L"", false},
    {kMsg, L"m-mira", L"Pushed the rail spec - the message pane is the bit I want eyes on.", L"09:12", false, kSent, L"", false},
    {kMsg, L"m-mira", L"Two modes in one column, no navigation.", L"09:12", false, kSent, L"", false},
    {kMsg, L"m-tobias", L"Reading it now. The delivered-by device list is the part that sells it.", L"09:20", false, kSent, L"", false},
    {kMsg, L"", L"Agreed. Nobody else can show you which device decrypted a message.", L"09:26", true, kRead, L"", false},
    {kMsg, L"m-saoirse", L"Do we have a name for the key-change record yet?", L"09:31", false, kSent, L"", false},
    {kSys, L"", L"Ravi Menon was added to the group", L"09:40", false, kSent, L"", false},
    {kMsg, L"m-ravi", L"Thanks. Catching up on the thread.", L"09:44", false, kSent, L"", false},
    {kMsg, L"", L"Welcome. Start at the rail spec, section 3.", L"09:51", true, kRead, L"", false},
    {kMsg, L"m-elena", L"The 360 rail width holds at 1560 but not at 1280.", L"10:03", false, kSent, L"", false},
    {kSep, L"", L"Today", L"", false, kSent, L"", false},
    {kMsg, L"m-mira", L"Overnight build is green on both architectures.", L"11:02", false, kSent, L"", false},
    {kMsg, L"", L"Nice. I'll take the identicon pass this morning.", L"11:08", true, kRead, L"", false},
    {kMsg, L"m-tobias", L"One note on the palette: nothing that reads as a state colour.", L"11:15", false, kSent, L"", false},
    {kMsg, L"m-tobias", L"No green, no red, no amber.", L"11:15", false, kSent, L"", false},
    {kMsg, L"", L"That is the rule in the header. It fails the build otherwise.", L"11:22", true, kDeliv, L"", false},
    {kSys, L"", L"Disappearing messages set to 7 days", L"11:30", false, kSent, L"", false},
    {kMsg, L"m-saoirse", L"Good. The old 24 hours was losing decisions.", L"11:34", false, kSent, L"", false},
    {kMsg, L"", L"Rail mode swap is a crossfade now, not a slide.", L"11:41", true, kDeliv, L"", false},
    {kMsg, L"m-ravi", L"Slide implied navigation. Crossfade is right.", L"11:48", false, kSent, L"", false},
    {kMsg, L"", L"Pushing the branch in ten minutes.", L"11:55", true, kSent, L"", false},
    // THE POSITIVE CONTROL, and it stands one row above its subject on purpose:
    // an explicit "member" at 12:02 renders as an ordinary bubble, so the
    // collapse rule below is shown to be about the ROLE and not about the row
    // carrying one at all.
    {kMsg, L"m-elena", L"I'll review it after standup.", L"12:02", false, kSent, L"", false,
            kRoleMember},
    // THE HIDDEN ROW (item 242 R4 step 8; Spec C section 5.6 and section 5.1).
    // Elena is an OBSERVER on the roster above and this line was sealed while
    // she was one, so it renders COLLAPSED to "A message from an observer was
    // hidden." with the body one expansion away. THE BODY IS REAL AND IS STILL
    // HERE - that is ruling 16's "hide, not drop", and it is why this is a
    // Message row rather than a System one. It is not a gap: a gap is a record
    // this device could not open.
    {kMsg, L"m-elena", L"Rail measurements are in the shared folder if anyone needs them.",
            L"12:05", false, kSent, L"", false, kRoleObserver},
    // THE RUN-BREAK CONTROL, and it is here because the rule was stated and not
    // HELD. A collapsed row draws no bubble, so it breaks a run in both
    // directions exactly as a system line does — but with 12:09 (outgoing)
    // directly under 12:05 the substitution that implements that made NO
    // difference to any row, and a mutant that deleted the upward half of it
    // survived every gate. This row is the case that makes it observable: an
    // INCOMING row by the SAME sender immediately under the collapsed one, which
    // must start a new run (its own name and identicon) rather than read as a
    // continuation across a line that says a message was hidden.
    //
    // It carries NO role, which is the fixture's ordinary state and is not
    // "member": Elena is an OBSERVER on the roster now, so a line of hers whose
    // sending role no source carries is exactly what a real log of a demoted
    // member looks like.
    {kMsg, L"m-elena", L"That folder is the one from the rail spec, not the old share.",
            L"12:07", false, kSent, L"", false},
    {kMsg, L"", L"Attaching the rail measurements now.", L"12:09", true, kFail,
            L"The message server did not acknowledge this message.", false},
    {kMsg, L"", L"Retrying the attachment.", L"12:11", true, kPend, L"", false},
  });

  // c1 - URnetwork core, 11 members, 18 rows, and the PERMANENT key-change
  // record. Spec C 7.4: non-dismissible.
  Conversation c1 = MakeConversation(
      L"conv-urnetwork-core", ConversationKind::Group, L"URnetwork core", kRoleOwner,
      {{L"m-mira", L"Mira Okonkwo", kRoleAdmin},
       {L"m-hana", L"Hana Sato", kRoleAdmin},
       {L"m-viktor", L"Viktor Halden", kRoleMember},
       {L"m-noor", L"Noor Haddad", kRoleMember},
       {L"m-jonas", L"Jonas Weber", kRoleMember},
       {L"m-amara", L"Amara Diallo", kRoleMember},
       {L"m-kai", L"Kai Lindqvist", kRoleMember},
       {L"m-priya", L"Priya Raman", kRoleMember},
       {L"m-oskar", L"Oskar Bremer", kRoleMember},
       {L"m-lucia", L"Lucia Ferrari", kRoleMember},
       {L"m-tobias", L"Tobias Lind", kRoleMember}},
      L"Will do.", L"10:36", 2, false, false,
      L"Kept until deleted", L"Media kept 30 days");
  AppendRows(c1, 1, w.myDevices, {
    {kSep, L"", L"Today", L"", false, kSent, L"", false},
    {kMsg, L"m-hana", L"Relay path is answering on all three hops this morning.", L"08:41", false, kSent, L"", false},
    {kMsg, L"m-viktor", L"Latency on the middle hop is down to 41 ms.", L"08:47", false, kSent, L"", false},
    {kMsg, L"", L"That is the lowest it has been this week.", L"08:52", true, kRead, L"", false},
    {kMsg, L"m-noor", L"Iceland node came back after the maintenance window.", L"09:05", false, kSent, L"", false},
    {kMsg, L"m-noor", L"No key rotation needed on our side.", L"09:05", false, kSent, L"", false},
    {kSys, L"", L"Bo Nakamura's identity key changed. Verify this contact before sending anything sensitive.", L"09:14", false, kSent, L"", true},
    {kMsg, L"m-jonas", L"Seen. I'll verify Bo out of band before the release call.", L"09:19", false, kSent, L"", false},
    {kMsg, L"", L"Do not clear that record - it stays in the thread.", L"09:25", true, kDeliv, L"", false},
    {kMsg, L"m-amara", L"It is non-dismissible by design, per the spec.", L"09:33", false, kSent, L"", false},
    {kMsg, L"m-kai", L"Epoch advanced to 4182 an hour ago.", L"09:41", false, kSent, L"", false},
    {kMsg, L"", L"Confirmed on this device.", L"09:48", true, kRead, L"", false},
    {kMsg, L"m-priya", L"Records per second is holding at 12.", L"09:56", false, kSent, L"", false},
    {kMsg, L"m-oskar", L"That is the number the advanced strip shows.", L"10:04", false, kSent, L"", false},
    {kMsg, L"", L"I'll fold the strip figures into the demo build.", L"10:12", true, kSent, L"", false},
    {kMsg, L"m-lucia", L"Ping me when it is up.", L"10:20", false, kSent, L"", false},
    {kMsg, L"m-tobias", L"Same here.", L"10:29", false, kSent, L"", false},
    {kMsg, L"", L"Will do.", L"10:36", true, kDeliv, L"", false},
  });

  w.conversations.push_back(std::move(c0));
  w.conversations.push_back(std::move(c1));

  struct DmSpec {
    const wchar_t* id; const wchar_t* person; const wchar_t* name;
    const wchar_t* preview; const wchar_t* time;
    int unread; bool muted; bool disappearing;
    const wchar_t* retention; const wchar_t* mediaRetention;
    RowSpec rows[4];
  };

  const DmSpec kDms[] = {
    {L"conv-bo", L"d-bo", L"Bo Nakamura", L"Got it. Will confirm after standup.", L"09:58",
     0, false, false, L"Kept until deleted", L"Media kept 30 days", {
      {kMsg, L"d-bo", L"Rotated my key this morning - you will see the record in core.", L"09:31", false, kSent, L"", false},
      {kMsg, L"", L"Saw it. Verifying out of band before the call.", L"09:39", true, kRead, L"", false},
      {kMsg, L"d-bo", L"Fingerprint is in my profile.", L"09:50", false, kSent, L"", false},
      {kMsg, L"", L"Got it. Will confirm after standup.", L"09:58", true, kDeliv, L"", false}}},
    {L"conv-freya", L"d-freya", L"Freya Nilsson", L"Bring the rail screenshots.", L"11:44",
     3, false, false, L"Kept until deleted", L"Media kept 30 days", {
      {kMsg, L"", L"Yes, 15:00 works.", L"11:12", true, kRead, L"", false},
      {kMsg, L"d-freya", L"I moved it to 15:30 - the room was taken.", L"11:29", false, kSent, L"", false},
      {kMsg, L"d-freya", L"Same agenda.", L"11:40", false, kSent, L"", false},
      {kMsg, L"d-freya", L"Bring the rail screenshots.", L"11:44", false, kSent, L"", false}}},
    {L"conv-idris", L"d-idris", L"Idris Bello", L"I'll try it tonight.", L"10:47",
     0, false, false, L"Kept until deleted", L"Media kept 30 days", {
      {kMsg, L"d-idris", L"The Linux build finally links.", L"10:21", false, kSent, L"", false},
      {kMsg, L"", L"Good. Which toolchain?", L"10:30", true, kRead, L"", false},
      {kMsg, L"d-idris", L"Clang 19, nothing exotic.", L"10:38", false, kSent, L"", false},
      {kMsg, L"", L"I'll try it tonight.", L"10:47", true, kDeliv, L"", false}}},
    {L"conv-marta", L"d-marta", L"Marta Kowalska", L"Let me know before it goes.", L"09:12",
     0, false, true, L"Disappears after 24 hours", L"Media disappears with the message", {
      {kMsg, L"d-marta", L"Sending the draft - it disappears in 24 hours.", L"08:35", false, kSent, L"", false},
      {kMsg, L"", L"Reading it now.", L"08:49", true, kRead, L"", false},
      {kMsg, L"d-marta", L"Let me know before it goes.", L"09:04", false, kSent, L"", false},
      {kMsg, L"", L"Will do.", L"09:12", true, kDeliv, L"", false}}},
    {L"conv-sam", L"d-sam", L"Sam Ortega", L"No problem.", L"Tue",
     0, true, false, L"Kept until deleted", L"Media kept 30 days", {
      {kMsg, L"d-sam", L"Muting this one, I am on call all week.", L"14:02", false, kSent, L"", false},
      {kMsg, L"", L"Understood.", L"14:09", true, kRead, L"", false},
      {kMsg, L"d-sam", L"I will still read it, just not chime in.", L"14:18", false, kSent, L"", false},
      {kMsg, L"", L"No problem.", L"14:26", true, kDeliv, L"", false}}},
    {L"conv-yuki", L"d-yuki", L"Yuki Tanaka", L"Any time.", L"Mon",
     0, false, false, L"Kept until deleted", L"Media kept 30 days", {
      {kMsg, L"d-yuki", L"Do you have the jurisdiction list?", L"16:41", false, kSent, L"", false},
      {kMsg, L"", L"Iceland, Switzerland, Canada.", L"16:52", true, kRead, L"", false},
      {kMsg, L"d-yuki", L"Perfect, thanks.", L"16:58", false, kSent, L"", false},
      {kMsg, L"", L"Any time.", L"17:03", true, kDeliv, L"", false}}},
  };

  size_t ci = 2;
  for (DmSpec const& dm : kDms) {
    Conversation c = MakeConversation(dm.id, ConversationKind::Direct, dm.name, kRoleOwner,
                                      {{dm.person, dm.name, kRoleMember}},
                                      dm.preview, dm.time, dm.unread, dm.muted,
                                      dm.disappearing, dm.retention, dm.mediaRetention);
    AppendRows(c, ci, w.myDevices,
               {dm.rows[0], dm.rows[1], dm.rows[2], dm.rows[3]});
    w.conversations.push_back(std::move(c));
    ++ci;
  }

  // EXACTLY THREE relay nodes, and no glyph is ever empty. The three
  // codepoints were RENDERED in Segoe Fluent Icons and looked at before they
  // were written down, not read off a list: E977 is the monitor+phone
  // "Devices" pair, E774 is the globe, E968 is the server tower.
  w.relayPath.push_back({L"This device", L"This computer", L"\uE977", 0, true});   // Devices
  w.relayPath.push_back({L"URnetwork", L"3 hops", L"\uE774", 41, true});           // Globe
  w.relayPath.push_back({L"Message server", L"urmsg-01.ur.io", L"\uE968", 18, true}); // Server

  w.server = {L"urmsg-01.ur.io", L"Iceland", 59, true};
  return w;
}

}  // namespace

const World& GetWorld() { return MutableWorld(); }

World& MutableWorld() {
  // Built once. A const-ref-only accessor is what ambient activity cannot
  // use, so both accessors name the SAME object: a second copy would let the
  // rail cache pointers into storage the append invalidates.
  static World world = BuildWorld();
  return world;
}

}  // namespace urmsg::demo
