// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit NotUsing, as it does LiveWorld.cpp and
// RunMode.cpp. CollectDiagnostics runs LiveIdentityDiagnostics before winrt::init_apartment.
//
// Read LiveIdentity.h first: it carries what each function decides and why.
#include "Live/LiveIdentity.h"

#include <algorithm>
#include <format>

#include "nlohmann/json.hpp"
#include "urnetwork_message.h"

namespace urmsg::live {
namespace {

bool IsLowerHex(char c) { return ('0' <= c && c <= '9') || ('a' <= c && c <= 'f'); }

// The overwrite that the compiler may not drop: a store through a volatile pointer is observable
// behaviour, so it stays even though the buffer is about to be freed.
void Overwrite(void* data, size_t bytes) {
  volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
  for (size_t i = 0; i < bytes; ++i) p[i] = 0;
}

// WHITE SPACE, as a box's separator: the ASCII kinds, the no-break space and the Unicode spaces.
// A box is held to one word by it, so the app is stricter than the library's fold, never looser.
bool IsSpaceChar(wchar_t c) {
  return c == L' ' || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
         (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
         c == 0x205F || c == 0x3000;
}

std::wstring TrimSpaces(std::wstring const& s) {
  size_t b = 0, e = s.size();
  while (b < e && IsSpaceChar(s[b])) ++b;
  while (e > b && IsSpaceChar(s[e - 1])) --e;
  return s.substr(b, e - b);
}

// UTF-16 to UTF-8, by hand: the field text is the person's, and a word with a letter outside ASCII
// must reach the library as the octets they typed, for its own fold to read (it names a word that
// is not on the list; nothing here does).
void AppendUtf8(std::string& out, std::wstring const& w) {
  for (size_t i = 0; i < w.size(); ++i) {
    uint32_t c = static_cast<uint16_t>(w[i]);
    if (0xD800 <= c && c <= 0xDBFF && i + 1 < w.size()) {
      const uint32_t lo = static_cast<uint16_t>(w[i + 1]);
      if (0xDC00 <= lo && lo <= 0xDFFF) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        ++i;
      }
    }
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (c >> 18)));
      out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
}

}  // namespace

std::filesystem::path SliceStoreFolder(std::filesystem::path const& liveRoot) {
  return liveRoot / kSliceStoreFolder;
}

bool IsAccountIdShape(std::string_view id) {
  if (id.size() != 36) return false;
  for (size_t i = 0; i < id.size(); ++i) {
    const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? id[i] != '-' : !IsLowerHex(id[i])) return false;
  }
  // a UUIDv8 (SCOPE B1; messagegroup's accountIdOfHash): the version nibble is 8, and the variant's
  // two top bits are 10
  if (id[14] != '8') return false;
  const char v = id[19];
  return v == '8' || v == '9' || v == 'a' || v == 'b';
}

bool IsDeviceIdShape(std::string_view id) {
  if (id.size() != 19) return false;
  for (size_t i = 0; i < id.size(); ++i) {
    const bool dash = i == 4 || i == 9 || i == 14;
    if (dash ? id[i] != '-' : !IsLowerHex(id[i])) return false;
  }
  return true;
}

IdentityInfo ParseIdentityJson(std::string_view json) {
  IdentityInfo info;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json.begin(), json.end());
  } catch (std::exception const& e) {
    info.problem = std::string("not json: ") + e.what();
    return info;
  }
  if (!j.is_object()) {
    info.problem = "not a json object";
    return info;
  }
  // EVERY KEY THE HEADER DOCUMENTS, OF ITS OWN TYPE (urnetwork_message.h, urnet_message_device_
  // identity), so a json that lost one is "could not be read" and never an identity with a blank
  // in it. Keys the header does not name yet (the notices and the alarm, Spec A 9.3a) are ignored.
  auto need = [&](char const* key, bool (nlohmann::json::*is)() const noexcept) -> bool {
    auto it = j.find(key);
    if (it == j.end()) {
      info.problem = std::string("no '") + key + "'";
      return false;
    }
    if (!((*it).*is)()) {
      info.problem = std::string("'") + key + "' is not of its type";
      return false;
    }
    return true;
  };
  if (!need("has_identity", &nlohmann::json::is_boolean) ||
      !need("account_id", &nlohmann::json::is_string) ||
      !need("device_id", &nlohmann::json::is_string) ||
      !need("phrase_device", &nlohmann::json::is_boolean) ||
      !need("devices", &nlohmann::json::is_array)) {
    return info;
  }
  info.hasIdentity = j["has_identity"].get<bool>();
  info.accountId = j["account_id"].get<std::string>();
  info.deviceId = j["device_id"].get<std::string>();
  info.phraseDevice = j["phrase_device"].get<bool>();
  for (auto const& d : j["devices"]) {
    if (!d.is_object() || !d.contains("device_id") || !d["device_id"].is_string() ||
        !d.contains("phrase_device") || !d["phrase_device"].is_boolean() ||
        !d.contains("this_device") || !d["this_device"].is_boolean()) {
      info.problem = "a device of 'devices' is not {device_id, phrase_device, this_device}";
      info.devices.clear();
      return info;
    }
    IdentityDevice dev;
    dev.deviceId = d["device_id"].get<std::string>();
    dev.phraseDevice = d["phrase_device"].get<bool>();
    dev.thisDevice = d["this_device"].get<bool>();
    info.devices.push_back(std::move(dev));
  }
  // AN IDENTITY IS ITS TWO IDS IN THEIR SHAPES: one that answers has_identity with an ID of another
  // shape is not drawn as one, since "Your ID" is the library's text character for character and a
  // screen must never show a guess at it.
  if (info.hasIdentity && (!IsAccountIdShape(info.accountId) || !IsDeviceIdShape(info.deviceId))) {
    info.problem = "has_identity, but an ID is not in its shape";
    return info;
  }
  info.read = true;
  return info;
}

std::vector<std::wstring> SplitOfferedWords(std::string_view utf8) {
  // RESERVED FIRST, so no buffer that held a word is let go unwiped as the vector or a word grows
  // (X-APP-2): 25 words, and room for the list's longest
  std::vector<std::wstring> words;
  words.reserve(kPhraseWords);
  std::wstring word;
  word.reserve(32);
  for (size_t i = 0; i <= utf8.size(); ++i) {
    const bool end = i == utf8.size();
    const char c = end ? ' ' : utf8[i];
    if (c == ' ') {
      if (word.empty()) {  // two spaces in a row, a space first or last, or nothing at all
        Wipe(words);
        return {};
      }
      words.push_back(std::move(word));
      word = std::wstring();
      word.reserve(32);
      continue;
    }
    if (c < 'a' || c > 'z') {  // the list's words are lower-case ASCII letters, and nothing else
      Wipe(word);
      Wipe(words);
      return {};
    }
    word.push_back(static_cast<wchar_t>(c));
  }
  if (words.size() != kPhraseWords) {
    Wipe(words);
    return {};
  }
  return words;
}

std::vector<BoxIssue> CheckBoxes(std::vector<std::wstring> const& fields,
                                 std::function<bool(std::wstring const&)> const& isListWord) {
  std::vector<BoxIssue> issues;
  for (size_t i = 0; i < fields.size(); ++i) {
    // counted by where the white space is, so no word is copied out of its box
    size_t words = 0;
    bool inWord = false;
    for (wchar_t c : fields[i]) {
      if (IsSpaceChar(c)) {
        inWord = false;
      } else if (!inWord) {
        inWord = true;
        ++words;
      }
    }
    if (words != 1) {
      issues.push_back(BoxIssue{static_cast<int>(i + 1), words, false});
    } else if (!isListWord || !isListWord(fields[i])) {
      // ONE WORD, AND THE LIST'S OWN CHECK REFUSES IT: in the library's fold it is not exactly one
      // word of the list (perhaps two, split where NFKD finds white space). With no check to ask,
      // nothing is let through.
      issues.push_back(BoxIssue{static_cast<int>(i + 1), 1, true});
    }
  }
  return issues;
}

std::wstring BoxesLine(std::vector<BoxIssue> const& issues) {
  std::wstring line;
  for (auto const& issue : issues) {
    if (!line.empty()) line += L" ";
    if (issue.words == 0) {
      line += std::format(L"Word {} is empty.", issue.position);
    } else if (issue.offList) {
      line += std::format(L"Word {} is not a word of the list.", issue.position);
    } else if (issue.words == 2) {
      line += std::format(L"Word {} holds two words.", issue.position);
    } else {
      line += std::format(L"Word {} holds {} words.", issue.position, issue.words);
    }
  }
  if (!line.empty()) line += L" Type one word from your paper in each box. Nothing was sent.";
  return line;
}

std::string OneWordUtf8(std::wstring const& word) {
  std::string out;
  out.reserve(word.size() * 4);
  AppendUtf8(out, word);
  return out;
}

std::string JoinTypedWords(std::vector<std::wstring> const& fields) {
  // RESERVED FIRST, for the reason SplitOfferedWords gives: no grown buffer is let go unwiped
  size_t room = fields.size();
  for (auto const& f : fields) room += f.size() * 4;
  std::string out;
  out.reserve(room);
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i > 0) out.push_back(' ');
    std::wstring w = TrimSpaces(fields[i]);
    AppendUtf8(out, w);
    Wipe(w);
  }
  return out;
}

bool ConfirmAccepted(int32_t kind, size_t namedPositions) {
  // ONLY OK. A WRONG answer is never "nearly", however few words it names: 24 of 25 is a wrong
  // phrase, and the identity the library did not make does not exist.
  (void)namedPositions;
  return kind == URNET_MESSAGE_WORDS_OK;
}

std::vector<int> NamedPositions(std::vector<int32_t> const& positions, int32_t count) {
  std::vector<int> out;
  const size_t n = count < 0 ? 0 : std::min(positions.size(), static_cast<size_t>(count));
  for (size_t i = 0; i < n; ++i) {
    const int32_t p = positions[i];
    if (p < 1 || p > static_cast<int32_t>(kPhraseWords)) continue;
    out.push_back(static_cast<int>(p));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::wstring WrongWordsLine(std::vector<int> const& positions) {
  if (positions.empty()) return {};
  std::wstring list;
  for (size_t i = 0; i < positions.size(); ++i) {
    if (i > 0) list += (i + 1 == positions.size()) ? L" and " : L", ";
    list += std::to_wstring(positions[i]);
  }
  if (positions.size() == 1)
    return std::format(L"Word {} is not the word you were shown. Nothing was corrected: type it "
                       L"again from your paper.",
                       list);
  return std::format(L"Words {} are not the words you were shown. Nothing was corrected: type "
                     L"them again from your paper.",
                     list);
}

void Wipe(std::string& s) {
  if (!s.empty()) Overwrite(s.data(), s.size());
  s.clear();
}

void Wipe(std::wstring& s) {
  if (!s.empty()) Overwrite(s.data(), s.size() * sizeof(wchar_t));
  s.clear();
}

void Wipe(std::vector<std::wstring>& words) {
  for (auto& w : words) Wipe(w);
  words.clear();
}

}  // namespace urmsg::live
