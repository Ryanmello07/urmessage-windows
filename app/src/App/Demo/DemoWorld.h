// The entire fabricated demo world: one seeded generator, no runtime
// randomness, identical bytes on every launch. Design doc section 5.
//
// PURE C++ AND IT HAS TO BE. GetWorld() is called from CollectDiagnostics(),
// which wWinMain runs at main.cpp:168 - BEFORE winrt::init_apartment() at
// main.cpp:183. A winrt type here would be constructed without an apartment.
// App.vcxproj compiles the .cpp with PrecompiledHeader=NotUsing so the
// project's pch (which pulls in every winrt/ header) cannot leak in either.
//
// The day a protocol arrives, the diff that deletes the demo is this
// directory and one switch.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace urmsg::demo {

enum class DeliveryState { Pending, Sent, Delivered, Read, Failed, Expired };
enum class RetentionClass { Permanent, Eph };
enum class ConversationKind { Direct, Group };
enum class RowKind { Message, DaySeparator, System };
enum class ConnectState { Connected, Connecting, Offline };

using Seed = std::array<uint8_t, 32>;   // identicon seed

// A DEVICE. Distinct from a person - see MemberRef.
struct DeviceRef {
  std::wstring id;              // stable: "dev-pixel9"
  std::wstring name;            // "Pixel 9"
  std::wstring ownerName;       // "Bo Nakamura"
  Seed ownerKey;                // identicon seed for the owner
  std::wstring lastSeenLabel;   // "now" | "2 min ago" | "3 days ago"
  bool online;
  bool isThisComputer;          // exactly one true in World::myDevices
};

// THE FOUR ROLES, spelled exactly as the message protocol spells them (MASTER section 11, and
// urnetwork_message.h's member info): these are the values MemberRef::role and
// Conversation::myRole carry in BOTH worlds, so a view branches on one spelling. The fabricated
// fixture writes them by hand; a protocol-backed world copies them off the roster. A row whose
// role is none of the four is drawn as the placeholder, never as a fifth role.
inline constexpr wchar_t kRoleOwner[] = L"owner";
inline constexpr wchar_t kRoleAdmin[] = L"admin";
inline constexpr wchar_t kRoleMember[] = L"member";
inline constexpr wchar_t kRoleObserver[] = L"observer";

// WHAT THIS DEVICE IS DOING, OR LAST DID, TO A MEMBER'S ROLE. None is the ordinary state. Pending
// means the call into the library has not returned. The other four are the four ways the ABI's
// two role verbs answer other than OK (urnetwork_message.h: URNET_MESSAGE_COMMIT_REFUSED / LOST /
// INVALID / FAILED), kept apart because each asks a different thing of the person: a refusal by
// role will answer the same way again, a lost epoch race means fetch and try again, an invalid
// request is this app's bug, and a failure is the transport. A change that SUCCEEDS leaves no
// state here at all - the roster itself moves, in the same publish.
enum class RoleActionState { None, Pending, Refused, Lost, Invalid, Failed };

// A PERSON. A group of 5 members has 5 MemberRefs and may have more devices.
//
// `role` is one of the four spellings above. `mine` is true on the row that is this device's own
// leaf - the fabricated fixture never sets it (its viewer is not in its own member lists), a
// protocol-backed world sets it on exactly one row per leaf this device holds. `identityPubHex` is
// what the two role verbs name a member BY (urnetwork_message.h: "IS THE VALUE THE TWO VERBS
// TAKE"); empty when no source carries one, and then no control is offered on the row.
struct MemberRef {
  std::wstring id;
  std::wstring displayName;
  Seed identityKey;             // identicon seed
  std::wstring role;            // kRoleOwner | kRoleAdmin | kRoleMember | kRoleObserver
  bool mine = false;
  std::wstring identityPubHex;  // the ABI's identity_pub, lower-case hex; "" when unsourced
  std::vector<DeviceRef> devices;
  // The change this device asked for on this member and its state; see RoleActionState. `verb`
  // is the role asked for (kRoleAdmin / kRoleMember / kRoleObserver, or kRoleOwner for a transfer)
  // and `reason` is the library's own out_error, verbatim, on the four non-OK answers.
  RoleActionState roleAction = RoleActionState::None;
  std::wstring roleActionVerb;
  std::wstring roleActionReason;
};

// Owner or admin: the two roles MASTER section 11's table lets administer a group. The fixture
// uses it for "who carries a second device"; the rail never does - the rail's control set comes
// from Views/RosterRules.h, which reads the roles by name.
inline bool CanAdminister(MemberRef const& m) {
  return m.role == kRoleOwner || m.role == kRoleAdmin;
}

struct MessageInspect {
  uint64_t epoch;
  uint32_t senderLeafIndex;
  RetentionClass retention;
  std::wstring sizeBucket;          // "<= 1 KiB"
  uint32_t wireSizeBytes;           // Advanced Mode only
  bool attestationVerified;
  std::wstring cipher;              // "XChaCha20-Poly1305"
  std::wstring groupIdHex;          // Advanced Mode only
  std::wstring senderDisplayName;   // ALWAYS populated, even on run continuations
  std::wstring sentAtLabel;
  std::wstring receivedAtLabel;
  std::vector<DeviceRef> deliveredTo;
  std::vector<DeviceRef> readBy;
};

// A REACTION STANDING ON A MESSAGE, or this device's own attempt to put one there or take one
// away. The fabricated fixture carries none (DemoWorld.cpp writes no reactions, and the I10
// fingerprint in Startup.cpp mixes named fields rather than struct bytes, so this slot's presence
// moves nothing there); a protocol-backed world (App\Live\LiveWorld.cpp) fills it from the
// records this device opened plus its own outbox.
//
// `state` REUSES DeliveryState AND IS DELIBERATELY NARROWER THAN IT: Sent means a record this
// device holds - the reaction STANDS - and is the ceiling, exactly as it is for a message row;
// Pending means this device's own react/unreact call has not returned; Failed means it returned a
// refusal, carried verbatim in failureReason. Delivered and Read are never written here, because
// nothing in the protocol reports either for a reaction any more than for a message.
struct MessageReaction {
  std::wstring emoji;               // RAW: two spellings of one emoji are two reactions
  bool mine = false;                // this device sealed it (or is trying to)
  DeliveryState state = DeliveryState::Sent;
  bool removing = false;            // the attempt is an UN-reaction; meaningful when state != Sent
  std::wstring failureReason;       // non-empty only when state == Failed
};

struct MessageRow {
  RowKind kind;
  std::wstring id;
  std::wstring senderName;              // empty in DMs and on run continuations
  Seed senderKey;
  std::wstring body;
  std::wstring timeLabel;               // "14:22"
  bool outgoing;
  DeliveryState state;
  std::wstring failureReason;           // non-empty only when state == Failed
  std::wstring systemText;              // used only when kind == System
  bool permanentRecord;                 // key-change record: non-dismissible
  MessageInspect inspect;
  std::vector<MessageReaction> reactions;  // empty in the fixture; see MessageReaction
};

struct Conversation {
  std::wstring id;
  ConversationKind kind;
  std::wstring name;
  Seed identityKey;
  std::wstring groupIdHex;              // non-empty for groups
  std::wstring preview;                 // ALWAYS NON-EMPTY, including the EPH one
  std::wstring timeLabel;
  int unread;
  bool muted;
  bool disappearing;                    // row draws the timer glyph INSTEAD of preview
  std::vector<MemberRef> members;       // PEOPLE
  int memberCount;                      // MUST equal members.size()
  // THIS DEVICE'S OWN ROLE in the conversation, one of the four spellings above: what decides
  // which controls the roster offers (Views/RosterRules.h). The fixture fabricates it per
  // conversation; a protocol-backed world reads it off urnet_message_group_my_role.
  std::wstring myRole;
  std::wstring retentionLabel;
  std::wstring mediaRetentionLabel;
  std::vector<MessageRow> rows;
};

struct ServerInfo {
  std::wstring host;            // "urmsg-01.ur.io"
  std::wstring jurisdiction;    // "Iceland"
  int latencyMs;
  bool keyVerified;
};

// One hop of the relay path. EXACTLY THREE in World::relayPath.
struct RelayNode {
  std::wstring label;       // "This device" | "URnetwork" | "Message server"
  std::wstring subLabel;    // "This computer" | "3 hops" | "urmsg-01.ur.io"
  std::wstring glyph;       // Segoe Fluent codepoint, NEVER EMPTY
  int hopMs;                // Advanced per-hop timing
  bool healthy;
};

struct World {
  std::vector<Conversation> conversations;
  std::vector<DeviceRef> myDevices;     // myDevices[0].isThisComputer == true
  std::vector<RelayNode> relayPath;     // exactly 3
  ServerInfo server;
  uint64_t currentEpoch;
  ConnectState connectState;
  std::wstring sessionMode;             // "direct" - Advanced strip only
  int recordsPerSecond;                 // Advanced strip only
};

// Row ids are "c<conversationIndex>-r<rowIndex>", assigned in build order.
//
// ADDED BEYOND CONTRACT v2 SECTION 1, and flagged as such: section 2 defines
// --demo=inspect as "select the message named by DemoWorld's designated
// inspect target", but section 1 gives no field to carry that name and adding
// one to a fixed struct is forbidden. A constant names it instead - it adds
// no field, renames nothing and changes no signature. It points at
// conversations[0].rows[12]: an OUTGOING message in state Read, so the rail's
// message mode opens with a populated deliveredTo AND readBy list, which is
// the state a screenshot needs.
inline constexpr wchar_t kInspectTargetRowId[] = L"c0-r12";

// THE ONE PLACEHOLDER FOR A FIELD NO SOURCE CAN FILL, and the only one: pick this, never a second
// spelling. It is VISIBLE by design — hiding the field would let a reader assume there is simply
// nothing to show, and filling it would be a fabrication. A protocol-backed world
// (App\Live\LiveWorld.cpp) writes it wherever the message protocol carries no value: there are no
// group names, no member NAMES (the roster and its roles are real; who a leaf belongs to is not),
// no sender names, no receipts past "the server took it", and no read cursor. It lives here, beside the structs, because every view that renders one of those
// fields already includes this header and none of them may reach into Live\.
inline constexpr wchar_t kUnavailable[] = L"unavailable";

// THE "NOT AVAILABLE" SENTINEL FOR THE TWO NUMERIC INSPECT FIELDS, and why it is a constant rather
// than a field. MessageInspect::senderLeafIndex and ::wireSizeBytes are uint32_t, so a source that
// cannot supply them has no text channel to say so in — 0 renders as "0" and as
// "0 bytes", which are claims. A protocol-backed world (App\Live\LiveWorld.cpp) writes this value
// instead and InspectRailFields renders the placeholder for it.
//
// A constexpr AND NOT A NEW STRUCT FIELD, which is load bearing: Startup.cpp's assertion I10
// hashes every byte of this world against a fixed fingerprint and runs on EVERY launch, so a new
// field in MessageInspect would turn that gate red for the fabricated world too. A constant adds
// no bytes. DemoWorld.cpp never writes it, so the demo's rendering is unchanged.
inline constexpr uint32_t kUnknownUint32 = 0xFFFFFFFFu;

// Built once, seeded, deterministic. Same bytes on every launch.
const World& GetWorld();

// Ambient activity (design doc 9.2) is the ONLY caller. It appends a
// MessageRow and advances one DeliveryState. Nothing else may mutate it.
World& MutableWorld();

}  // namespace urmsg::demo
