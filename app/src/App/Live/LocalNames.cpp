// SPDX-License-Identifier: MPL-2.0
//
// Read LocalNames.h first: it carries the rule this file implements and the reasons for it.
//
// NO PCH, NO winrt, NO XAML. App.vcxproj marks this unit PrecompiledHeader=NotUsing, as it does
// LiveWorld.cpp, because Startup.cpp's --diagnose calls every function here before
// winrt::init_apartment().

#include "Live/LocalNames.h"

#include <format>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "Log.h"
#include "Paths.h"
#include "Strings.h"

namespace urmsg::live {
namespace {

// White space a label is trimmed of at its two ends. U+2028 and U+2029 are here AND refused below:
// at an end they are trimmed, inside a label they are a line break.
bool IsTrimmable(wchar_t c) {
  switch (c) {
    case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D: case 0x0020:
    case 0x0085: case 0x00A0: case 0x1680: case 0x2028: case 0x2029: case 0x202F:
    case 0x205F: case 0x3000: case 0xFEFF:
      return true;
    default:
      return 0x2000 <= c && c <= 0x200A;
  }
}

bool IsRefused(wchar_t c) {
  if (c < 0x20) return true;                    // C0
  if (0x7F <= c && c <= 0x9F) return true;      // DEL and C1
  if (c == 0x2028 || c == 0x2029) return true;  // line and paragraph separators
  if (0x202A <= c && c <= 0x202E) return true;  // bidirectional embeddings and overrides
  if (0x2066 <= c && c <= 0x2069) return true;  // bidirectional isolates
  return false;
}

// A key is the ABI's own spelling: lower-case hex, an even number of digits, and not absurdly long.
bool IsLowerHex(std::string_view s) {
  if (s.empty() || s.size() % 2 != 0 || 256 < s.size()) return false;
  for (char c : s) {
    if (!(('0' <= c && c <= '9') || ('a' <= c && c <= 'f'))) return false;
  }
  return true;
}

// A label is storable only if CheckLocalName would store it EXACTLY as written. That one test
// covers a label that is not trimmed, too long, holds a refused character, or is not valid UTF-8
// (Widen replaces a bad sequence, and the narrowed result then differs from the original).
bool IsStorable(std::string_view utf8) {
  const LocalNameCheck check = CheckLocalName(urnw::Widen(utf8));
  return check.verdict == LocalNameVerdict::Ok && check.utf8 == utf8;
}

std::mutex g_mutex;
bool g_loaded = false;
LocalNames g_names;

std::filesystem::path NamesFile() { return urnw::StorageRoot() / L"local_names.json"; }

void EnsureLoadedLocked() {
  if (g_loaded) return;
  g_names = LoadLocalNamesFrom(NamesFile());
  g_loaded = true;
}

bool Store(std::map<std::string, std::string> LocalNames::*which, std::string_view key,
           std::string_view utf8, const char* what) {
  if (!IsLowerHex(key)) {
    urnw::LogWarn("names: a label for a {} was refused: its key is not lower-case hex", what);
    return false;
  }
  if (!utf8.empty() && !IsStorable(utf8)) {
    urnw::LogWarn("names: a label for a {} was refused: it is not one CheckLocalName passed", what);
    return false;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  EnsureLoadedLocked();
  LocalNames next = g_names;
  auto& map = next.*which;
  if (utf8.empty()) {
    map.erase(std::string(key));
  } else {
    map[std::string(key)] = std::string(utf8);
  }
  if (!SaveLocalNamesTo(NamesFile(), next)) {
    urnw::LogWarn("names: a label for a {} was NOT saved: the file could not be written", what);
    return false;
  }
  g_names = std::move(next);
  // W10: never the label, at any verbosity. That a label exists is all the log may hold.
  urnw::LogInfo("names: a label for a {} was {}", what, utf8.empty() ? "removed" : "saved");
  return true;
}

}  // namespace

LocalNameCheck CheckLocalName(std::wstring_view typed) {
  LocalNameCheck out;
  std::size_t begin = 0;
  std::size_t end = typed.size();
  while (begin < end && IsTrimmable(typed[begin])) ++begin;
  while (begin < end && IsTrimmable(typed[end - 1])) --end;
  const std::wstring_view trimmed = typed.substr(begin, end - begin);
  if (trimmed.empty()) {
    out.verdict = LocalNameVerdict::Empty;
    return out;
  }
  for (wchar_t c : trimmed) {
    if (IsRefused(c)) {
      out.verdict = LocalNameVerdict::ControlCharacter;
      return out;
    }
  }
  std::string utf8 = urnw::Narrow(trimmed);
  out.octets = utf8.size();
  if (kMaxLocalNameOctets < utf8.size()) {
    out.verdict = LocalNameVerdict::TooLong;
    return out;
  }
  out.verdict = LocalNameVerdict::Ok;
  out.utf8 = std::move(utf8);
  return out;
}

std::wstring LocalNameRefusal(LocalNameCheck const& check) {
  switch (check.verdict) {
    case LocalNameVerdict::Ok:
      return {};
    case LocalNameVerdict::Empty:
      return L"Type a name first.";
    case LocalNameVerdict::TooLong:
      // U+2014 EM DASH as an escape, never a pasted glyph (the house non-ASCII rule)
      return std::format(L"Too long — {} of {} bytes.", check.octets, kMaxLocalNameOctets);
    case LocalNameVerdict::ControlCharacter:
      return L"A name cannot hold a line break, a control character or a text-direction control.";
  }
  return {};
}

LocalNames ParseLocalNames(std::string_view json) {
  LocalNames out;
  try {
    const nlohmann::json j = nlohmann::json::parse(json.begin(), json.end());
    if (!j.is_object()) return out;
    auto take = [](nlohmann::json const& section, std::map<std::string, std::string>& into) {
      if (!section.is_object()) return;
      for (auto it = section.begin(); it != section.end(); ++it) {
        if (!it.value().is_string()) continue;
        const std::string label = it.value().get<std::string>();
        if (!IsLowerHex(it.key()) || !IsStorable(label)) continue;
        into[it.key()] = label;
      }
    };
    if (auto m = j.find("members"); m != j.end()) take(*m, out.members);
    if (auto c = j.find("conversations"); c != j.end()) take(*c, out.conversations);
  } catch (...) {
    return LocalNames{};
  }
  return out;
}

std::string SerializeLocalNames(LocalNames const& names) {
  nlohmann::json j = nlohmann::json::object();
  j["version"] = 1;
  nlohmann::json members = nlohmann::json::object();
  for (auto const& [key, label] : names.members) members[key] = label;
  nlohmann::json conversations = nlohmann::json::object();
  for (auto const& [key, label] : names.conversations) conversations[key] = label;
  j["members"] = std::move(members);
  j["conversations"] = std::move(conversations);
  try {
    return j.dump(2);
  } catch (...) {
    // dump refuses a string that is not UTF-8; nothing IsStorable passed can be one, and an empty
    // answer makes the save refuse rather than write half a file
    return {};
  }
}

LocalNames LoadLocalNamesFrom(std::filesystem::path const& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream text;
  text << f.rdbuf();
  return ParseLocalNames(text.str());
}

bool SaveLocalNamesTo(std::filesystem::path const& path, LocalNames const& names) {
  const std::string text = SerializeLocalNames(names);
  if (text.empty()) return false;
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::filesystem::path temporary = path;
  temporary += L".tmp";
  {
    std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    f.flush();
    if (!f) {
      f.close();
      std::filesystem::remove(temporary, ec);
      return false;
    }
  }
  // Renamed OVER the target: the old labels or the new ones are on disk at every instant.
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  return true;
}

LocalNames CurrentLocalNames() {
  std::lock_guard<std::mutex> lock(g_mutex);
  EnsureLoadedLocked();
  return g_names;
}

bool StoreMemberLocalName(std::string_view identityPubHex, std::string_view utf8) {
  return Store(&LocalNames::members, identityPubHex, utf8, "member");
}

bool StoreConversationLocalName(std::string_view groupIdHex, std::string_view utf8) {
  return Store(&LocalNames::conversations, groupIdHex, utf8, "conversation");
}

urmsg::demo::World WithLocalNames(urmsg::demo::World world, LocalNames const& names) {
  for (auto& c : world.conversations) {
    for (auto& m : c.members) {
      m.localName.clear();
      if (m.identityPubHex.empty()) continue;
      if (auto it = names.members.find(urnw::Narrow(m.identityPubHex)); it != names.members.end())
        m.localName = urnw::Widen(it->second);
    }
    // BY THE SIGNING IDENTITY, which is what a line is attributed by: a member who left keeps the
    // label on the lines they wrote, and a newcomer on their leaf does not inherit it.
    for (auto& r : c.rows) {
      r.senderLocalName.clear();
      if (r.kind != urmsg::demo::RowKind::Message || r.senderIdentityHex.empty()) continue;
      auto it = names.members.find(urnw::Narrow(r.senderIdentityHex));
      if (it == names.members.end()) continue;
      r.senderLocalName = urnw::Widen(it->second);
      if (!r.outgoing) r.senderName = r.senderLocalName;
    }
    c.localName.clear();
    if (!c.groupIdHex.empty()) {
      if (auto it = names.conversations.find(urnw::Narrow(c.groupIdHex));
          it != names.conversations.end())
        c.localName = urnw::Widen(it->second);
    }
    if (!c.localName.empty()) {
      c.name = c.localName;
    } else if (c.kind == urmsg::demo::ConversationKind::Direct) {
      // A two-party conversation is called what you call the other party. Exactly one member that
      // is not this device, or the name stays what the protocol said.
      const urmsg::demo::MemberRef* other = nullptr;
      int others = 0;
      for (auto const& m : c.members) {
        if (m.mine) continue;
        other = &m;
        ++others;
      }
      if (others == 1 && !other->localName.empty()) c.name = other->localName;
    }
  }
  return world;
}

}  // namespace urmsg::live
