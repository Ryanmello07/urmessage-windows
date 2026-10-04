// THE NAMES THIS PERSON GAVE, AND NOBODY ELSE'S.
//
// The protocol carries no names: there is no identity layer in this build, so every member and
// every conversation renders as the placeholder (LiveWorld.h says why that is the honest draw). A
// group of three people whose lines all read "unavailable" cannot be followed, though, and that is
// what the alpha's testers would meet first. This file is the answer that needs no protocol: a
// name the person types FOR a member, keyed by that member's identity public key, or for a
// conversation, keyed by its group id. It is kept in one JSON file under StorageRoot(), and IT IS
// NEVER SENT. Nobody in the group learns it, the server never sees it, and the member it names
// never chose it.
//
// IT IS A LABEL, NOT A DISPLAY NAME, AND THE VIEWS SAY SO. MemberRef::displayName stays exactly
// what the protocol said (the placeholder); the rail shows this beside it under the words "Your
// name for them". Nothing here may write displayName or MessageInspect::senderDisplayName.
//
// KEYED BY THE IDENTITY, NOT THE HANDLE. A message is attributed by sender_identity, the key MLS
// authenticated as its signer, and the roster answers the same value as identity_pub. The sixteen
// octet sender handle is a LEAF's label: a newcomer on a removed member's leaf inherits it, so a
// name keyed by the handle would follow the leaf onto the next person (ledger item 245).
//
// W10: A NAME IS NEVER LOGGED, at any verbosity. A contact display name is on Spec C's list of
// things the log may not hold, and a label the person typed for somebody is the same thing.
//
// PURE C++, NO winrt, NO XAML, so --diagnose can hold every rule here before init_apartment.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "Demo/DemoWorld.h"

namespace urmsg::live {

// Spec A's contact card bounds a display_name at 64 UTF-8 octets. A label takes the same bound, so
// the day cards arrive a label never has to be cut to sit beside the name a card carries.
inline constexpr std::size_t kMaxLocalNameOctets = 64;

enum class LocalNameVerdict {
  Ok,
  Empty,             // nothing left once white space is trimmed from both ends
  TooLong,           // more than kMaxLocalNameOctets once trimmed
  ControlCharacter,  // a control character, a line break, or a text-direction control
};

struct LocalNameCheck {
  LocalNameVerdict verdict = LocalNameVerdict::Empty;
  std::string utf8;        // the label as it is stored: trimmed, UTF-8. Empty unless Ok
  std::size_t octets = 0;  // the trimmed label's UTF-8 length, on Ok and on TooLong
};

// Trim white space from both ends, then refuse: nothing left; more than kMaxLocalNameOctets; any C0
// or C1 control, DEL, U+2028 or U+2029; and the bidirectional embeddings, overrides and isolates
// (U+202A..U+202E, U+2066..U+2069), because a label that reorders the text drawn after it can make
// a roster row read as something it is not.
LocalNameCheck CheckLocalName(std::wstring_view typed);

// The sentence for a refusal, in the unit the bound is in; "" for Ok.
std::wstring LocalNameRefusal(LocalNameCheck const& check);

// Every label, by key. A plain value: what CurrentLocalNames() answers is a copy.
struct LocalNames {
  std::map<std::string, std::string> members;        // identity_pub hex -> label, UTF-8
  std::map<std::string, std::string> conversations;  // group id hex     -> label, UTF-8
};

// The file's grammar, both ways:
//   {"version":1,"members":{"<hex>":"label"},"conversations":{"<hex>":"label"}}
// Parsing never throws. A file that does not parse is NO labels, and the next save rewrites it
// whole. An entry whose key is not lower-case hex, or whose label CheckLocalName would not store
// exactly as written, is dropped: the file is this app's own, so such an entry is damage, and a
// damaged label shown as a name is worse than none.
LocalNames ParseLocalNames(std::string_view json);
std::string SerializeLocalNames(LocalNames const& names);

// The same load and save against an explicit path, which is what --diagnose drives. The save
// writes a temporary file beside the target and renames it over the target, so an interrupted
// save leaves the old labels or the new ones and never half of either.
LocalNames LoadLocalNamesFrom(std::filesystem::path const& path);
bool SaveLocalNamesTo(std::filesystem::path const& path, LocalNames const& names);

// THE PROCESS'S LABELS: StorageRoot()/local_names.json, loaded on first use. Safe from any thread.
LocalNames CurrentLocalNames();

// Store `utf8` as the label for one key, or remove the key's label when `utf8` is empty. `utf8`
// must be what CheckLocalName answered for an Ok label; anything else is refused, as is a key that
// is not lower-case hex. Answers true only when the file on disk now says so.
bool StoreMemberLocalName(std::string_view identityPubHex, std::string_view utf8);
bool StoreConversationLocalName(std::string_view groupIdHex, std::string_view utf8);

// ── the overlay ──────────────────────────────────────────────────────────────────────────────
// The world with this person's labels applied, and nothing else moved:
//   * MemberRef::localName, on every member whose identity key has a label;
//   * MessageRow::senderLocalName on every message row whose SIGNING identity has a label, and
//     MessageRow::senderName as well on an INCOMING one (the thread's sender line);
//   * Conversation::localName, and Conversation::name: the conversation's own label if it has one,
//     else, in a DIRECT conversation, the label of the one member that is not this device, else
//     unchanged.
// displayName and inspect.senderDisplayName are never written: they are what the protocol said.
urmsg::demo::World WithLocalNames(urmsg::demo::World world, LocalNames const& names);

}  // namespace urmsg::live
