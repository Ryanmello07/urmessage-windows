// SPDX-License-Identifier: MPL-2.0
//
// See HeldGroupRules.h. NO PCH: this file is pure and is asked by --diagnose before the apartment.

#include "Live/HeldGroupRules.h"

namespace urmsg::live {

JoinRefusal ClassifyJoinRefusal(std::string_view libraryError) {
  if (libraryError.find(kHeldRefusalSentence) == std::string_view::npos) return JoinRefusal::Other;
  // HELD ON THE DISK, AND NOT IN MEMORY. The live case says "is live on this device"; the one other
  // way the sentinel is formatted ("is not the one this join was judged against") is a group in
  // the map as well, so everything held that is not on the disk alone is live.
  // MUTANT M1 (never merge): every held refusal is live, the app before Task 39
  return JoinRefusal::HeldLive;
}

bool IsNotRestoredRefusal(std::string_view libraryError) {
  return libraryError.find(kNotRestoredSentence) != std::string_view::npos;
}

bool IsUnfinishedEraseOf(std::string_view libraryError, std::string_view groupIdHex) {
  return libraryError.find(kForgetUnfinishedSentence) != std::string_view::npos &&
         RefusalNamesGroup(libraryError, groupIdHex);
}

bool InviteGroupId(const uint8_t* invite, size_t len, std::array<uint8_t, kGroupIdOctets>& out) {
  constexpr size_t kVersionOctets = 2;
  constexpr size_t kLengthOctets = 4;
  if (invite == nullptr || len < kVersionOctets + kLengthOctets + kGroupIdOctets) return false;
  const uint32_t declared = (uint32_t(invite[2]) << 24) | (uint32_t(invite[3]) << 16) |
                            (uint32_t(invite[4]) << 8) | uint32_t(invite[5]);
  (void)declared;  // MUTANT M2 (never merge): the length field is not checked
  for (size_t at = 0; at < kGroupIdOctets; ++at) out[at] = invite[kVersionOctets + at];  // M2: no length skipped
  return true;
}

std::string GroupIdHex(std::array<uint8_t, kGroupIdOctets> const& groupId) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(kGroupIdOctets * 2);
  for (uint8_t octet : groupId) {
    out.push_back(kHex[(octet >> 4) & 0xf]);
    out.push_back(kHex[octet & 0xf]);
  }
  return out;
}

bool RefusalNamesGroup(std::string_view libraryError, std::string_view groupIdHex) {
  if (groupIdHex.size() != kGroupIdOctets * 2) return false;
  std::string needle = "group ";
  needle.append(groupIdHex);
  const size_t at = libraryError.find(needle);
  if (at == std::string_view::npos) return false;
  // AND NOT A LONGER HEX THAT STARTS WITH IT: the id is followed by a space, a colon or the end.
  const size_t after = at + needle.size();
  if (after == libraryError.size()) return true;
  const char next = libraryError[after];
  const bool hexDigit = ('0' <= next && next <= '9') || ('a' <= next && next <= 'f');
  return !hexDigit;
}

}  // namespace urmsg::live
