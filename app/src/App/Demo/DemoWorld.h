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

// A PERSON. A group of 5 members has 5 MemberRefs and may have more devices.
struct MemberRef {
  std::wstring id;
  std::wstring displayName;
  Seed identityKey;             // identicon seed
  bool admin;
  std::vector<DeviceRef> devices;
};

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
// group names, no member lists, no sender names, no receipts past "the server took it", and no
// read cursor. It lives here, beside the structs, because every view that renders one of those
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
