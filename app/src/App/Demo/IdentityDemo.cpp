// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit <PrecompiledHeader>NotUsing</PrecompiledHeader>,
// the same as RunMode.cpp and Demo/DemoWorld.cpp. Startup.cpp's CollectDiagnostics() calls
// IdentityDemoDiagnostics() from wWinMain BEFORE winrt::init_apartment().
//
// Read Demo/IdentityDemo.h first: it carries the rule each string here keeps, and why the words
// are not a phrase.
#include "Demo/IdentityDemo.h"

#include <array>
#include <format>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::demo::identity {
namespace {

// ---- the copy that is the same on every fabricated screen -------------------------------------

constexpr wchar_t kBanner[] =
    L"Demo model: made-up data. Nothing on this screen is a real account, device or phrase.";
constexpr wchar_t kWordFieldsLabel[] =
    L"Demo check: the words are checked against the demo list, and nothing else.";
constexpr wchar_t kPhraseFieldLabel[] =
    L"Demo phrase: type the demo words. Nothing is checked against an account.";
constexpr wchar_t kFillWords[] = L"Demo only: fill in the demo words";

// A word that is not its field's word, for the typed-back state with a wrong word: the letters of
// word 7 ("chocolate") out of order, so the screen shows a typo and the check names it.
constexpr wchar_t kWrongWord[] = L"cholocate";
constexpr int kWrongIndex = 6;

// ---- the fixture's own data, keyed by the fabricated world's device ids -------------------------

struct DeviceData {
  wchar_t const* worldId;
  wchar_t const* deviceId;
  wchar_t const* addedOn;
  int addedBy;  // index into the world's myDevices; -1: the first device, set up on its own
  bool phraseDevice;
};

constexpr DeviceData kDeviceData[] = {
    {L"dev-desktop", L"5c1e 9a07 d3b2 4f68", L"2 Oct 2026", -1, true},
    {L"dev-pixel9", L"a20f 63d9 18c4 7be1", L"5 Oct 2026", 0, false},
    {L"dev-laptop", L"e7b4 0c5a 92f1 3d86", L"9 Oct 2026", 0, true},
};

// Two fixed demo lists. Every word has 9 letters or more; the BIP-39 English list has none.
constexpr std::array<wchar_t const*, 25> kWordsSetUp = {
    L"lighthouse", L"butterfly", L"telescope", L"waterfall", L"adventure",
    L"blueberry",  L"chocolate", L"dragonfly", L"evergreen", L"fireplace",
    L"grassland",  L"harmonica", L"jellyfish", L"kingfisher", L"landscape",
    L"moonlight",  L"nightfall", L"orchestra", L"pineapple", L"quicksand",
    L"raspberry",  L"sunflower", L"thunderstorm", L"umbrellas", L"watermelon",
};
constexpr std::array<wchar_t const*, 25> kWordsChanged = {
    L"afternoon",  L"bookshelf",  L"carnation",   L"dandelion",  L"earthworm",
    L"flowerbed",  L"goldfinch",  L"hailstorm",   L"horseshoe",  L"icebreaker",
    L"jackfruit",  L"kaleidoscope", L"lemongrass", L"marigolds", L"nightingale",
    L"octopuses",  L"peppermint", L"quarterback", L"rainstorm",  L"snowflake",
    L"tangerine",  L"undergrowth", L"violinist",  L"windmills",  L"yesterday",
};

// Seven of Matrix's 64 emoji, with their English names. Escapes, never pasted glyphs: an editing
// pass that re-encodes a pasted character leaves no build error behind, only a wrong byte.
const std::array<EmojiItem, 7>& DemoEmoji() {
  static const std::array<EmojiItem, 7> kEmoji = {{
      {L"\U0001F436", L"Dog"},
      {L"\U0001F680", L"Rocket"},
      {L"\U0001F511", L"Key"},
      {L"\U0001F355", L"Pizza"},
      {L"\U0001F319", L"Moon"},
      {L"\U0001F3B8", L"Guitar"},
      {L"\u2693\uFE0F", L"Anchor"},
  }};
  return kEmoji;
}

// ---- small helpers ---------------------------------------------------------------------------

std::wstring Lower(std::wstring s) {
  for (wchar_t& c : s)
    if (L'A' <= c && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
  return s;
}

std::wstring Trim(std::wstring const& s) {
  const auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
  size_t a = 0, b = s.size();
  while (a < b && isSpace(s[a])) ++a;
  while (b > a && isSpace(s[b - 1])) --b;
  return s.substr(a, b - a);
}

bool NamesDemo(std::wstring const& s) { return Lower(s).find(L"demo") != std::wstring::npos; }

std::wstring CodeForDisplay(std::wstring const& code) {
  std::wstring out;
  for (size_t i = 0; i < code.size(); ++i) {
    if (i > 0 && i % 4 == 0) out.push_back(L' ');
    out.push_back(code[i]);
  }
  return out;
}

Block BannerBlock() {
  Block b;
  b.type = BlockType::Banner;
  b.kind = TextKind::Fact;
  b.text = kBanner;
  return b;
}

Block TitleBlock(std::wstring text, TextKind kind = TextKind::Chrome) {
  Block b;
  b.type = BlockType::Title;
  b.kind = kind;
  b.text = std::move(text);
  return b;
}

Block TextBlock(std::wstring text, TextKind kind, TextStyle style = TextStyle::Body) {
  Block b;
  b.type = BlockType::Text;
  b.kind = kind;
  b.style = style;
  b.text = std::move(text);
  return b;
}

Button Go(Screen to, std::wstring label, bool primary = false, TextKind kind = TextKind::Chrome) {
  Button x;
  x.action = Action::Go;
  x.arg = static_cast<int>(to);
  x.label = std::move(label);
  x.kind = kind;
  x.primary = primary;
  return x;
}

Button Act(Action a, std::wstring label, bool primary = false, TextKind kind = TextKind::Chrome,
           int arg = 0) {
  Button x;
  x.action = a;
  x.arg = arg;
  x.label = std::move(label);
  x.kind = kind;
  x.primary = primary;
  return x;
}

Block ButtonsBlock(std::vector<Button> buttons) {
  Block b;
  b.type = BlockType::Buttons;
  b.buttons = std::move(buttons);
  return b;
}

std::vector<std::wstring> ToVector(std::array<wchar_t const*, 25> const& words) {
  return std::vector<std::wstring>(words.begin(), words.end());
}

Block WordsBlock(RunMode mode, std::vector<std::wstring> words) {
  Block b;
  b.type = BlockType::Words;
  b.label = PhraseWordsLabel(mode);
  b.labelKind = TextKind::Fact;
  b.words = std::move(words);
  return b;
}

Block WordFieldsBlock(std::wstring label, std::vector<std::wstring> words) {
  Block b;
  b.type = BlockType::WordFields;
  b.label = std::move(label);
  b.labelKind = TextKind::Fact;
  b.words = std::move(words);
  return b;
}

std::wstring DeviceDetail(FixtureDevice const& d) {
  std::wstring s = L"Demo device ID " + d.deviceId;
  if (d.thisDevice) s += L" \u00B7 this device";
  if (d.phraseDevice) s += L" \u00B7 phrase device";
  return s;
}

bool Has(Selection const& sel, size_t index) { return (sel.devices >> index) & 1u; }

// This session's renames (Rename's Save). Index-aligned with Fixture::devices; empty: no rename.
std::vector<std::wstring>& Renames() {
  static std::vector<std::wstring> renames;
  return renames;
}

// The devices a Remove screen lists: every one, or only this device on an only device.
std::vector<size_t> ListedDevices(Fixture const& f, Selection const& sel) {
  std::vector<size_t> out;
  for (size_t i = 0; i < f.devices.size(); ++i)
    if (!sel.onlyDevice || f.devices[i].thisDevice) out.push_back(i);
  return out;
}

size_t ThisDeviceIndex(Fixture const& f) {
  for (size_t i = 0; i < f.devices.size(); ++i)
    if (f.devices[i].thisDevice) return i;
  return 0;
}

// The Remove dialog and the lock-out, one builder: the selection decides what it asks.
void BuildRemove(ScreenModel& m, RunMode mode, Fixture const& f, Selection const& sel) {
  const bool asked = PhraseAsked(f, sel);
  // a dialog that takes the 25 words is drawn at the full width, or the fifth column of fields is
  // cut off (measured: at the dialog width only words 1 to 20 showed, ci/i1-shots-2)
  m.dialog = !asked;
  const size_t self = ThisDeviceIndex(f);
  m.blocks.push_back(BannerBlock());
  if (sel.lockOut) {
    m.blocks.push_back(TitleBlock(L"Lock out a phrase device"));
    m.blocks.push_back(TextBlock(
        L"Demo model: " + f.selfRemoved +
            L" held your phrase's key. A lock-out makes a new key for your phrase, so the old "
            L"one can do nothing. Type your phrase, and choose what happens to each device.",
        TextKind::Fact));
  } else {
    m.blocks.push_back(TitleBlock(sel.onlyDevice ? L"Remove this device" : L"Remove devices"));
    m.blocks.push_back(TextBlock(L"Choose the devices to remove. One removal can name several.",
                                 TextKind::Chrome));
    Block list;
    list.type = BlockType::Devices;
    list.label = L"Demo devices, made up";
    list.labelKind = TextKind::Fact;
    for (size_t i : ListedDevices(f, sel)) {
      DeviceLine line;
      line.index = static_cast<int>(i);
      line.name = DemoDeviceName(i);
      line.detail = DeviceDetail(f.devices[i]);
      line.selected = Has(sel, i);
      list.devices.push_back(std::move(line));
    }
    m.blocks.push_back(std::move(list));
    m.blocks.push_back(TextBlock(
        L"Demo model: a real removal acts at once, with no delay. Nothing here is removed.",
        TextKind::Fact, TextStyle::Muted));
    if (Has(sel, self)) {
      if (sel.onlyDevice) {
        m.blocks.push_back(TextBlock(OnlyDeviceLine(mode), TextKind::Fact, TextStyle::Strong));
      } else {
        m.blocks.push_back(TextBlock(
            L"Demo model: this device would remove itself. Your other devices would take it out "
            L"of your conversations when they next run.",
            TextKind::Fact));
      }
    }
  }
  if (asked) {
    std::wstring phraseDevice;
    for (size_t i = 0; i < f.devices.size(); ++i)
      if (i != self && Has(sel, i) && f.devices[i].phraseDevice) phraseDevice = DemoDeviceName(i);
    if (!sel.lockOut) {
      m.blocks.push_back(TextBlock(
          phraseDevice.empty()
              ? std::wstring(L"Demo model: this device does not keep your phrase's key, so it "
                             L"asks for the phrase, and becomes a phrase device.")
              : L"Demo model: " + phraseDevice +
                    L" keeps your phrase's key, so removing it is a lock-out. Type your phrase, "
                    L"and choose what happens to each other device.",
          TextKind::Fact));
    }
    m.blocks.push_back(WordFieldsBlock(kPhraseFieldLabel, f.wordsSetUp));
    if (sel.lockOut || !phraseDevice.empty()) {
      Block keep;
      keep.type = BlockType::KeepOrRemove;
      keep.label = L"Demo model: after a lock-out, your other phrase devices need the phrase again.";
      keep.labelKind = TextKind::Fact;
      for (size_t i = 0; i < f.devices.size(); ++i) {
        if (i == self || Has(sel, i)) continue;
        DeviceLine line;
        line.index = static_cast<int>(i);
        line.name = DemoDeviceName(i);
        line.detail = DeviceDetail(f.devices[i]);
        keep.devices.push_back(std::move(line));
      }
      m.blocks.push_back(std::move(keep));
    }
    m.blocks.push_back(ButtonsBlock({Act(Action::FillWords, kFillWords, false, TextKind::DemoOnly)}));
  }
  // A selected phrase device other than this one makes the act a lock-out (G3); anything else the
  // dialog does is a removal, whether or not it asked for the phrase (G2).
  bool lockOutOfSelected = false;
  for (size_t i = 0; i < f.devices.size(); ++i)
    if (i != self && Has(sel, i) && f.devices[i].phraseDevice) lockOutOfSelected = true;
  std::wstring removeLabel = L"Remove";
  if (sel.lockOut)
    removeLabel = L"Lock out";
  else if (lockOutOfSelected)
    removeLabel = L"Lock out and remove";
  else if (sel.onlyDevice)
    removeLabel = L"End this identity";
  m.blocks.push_back(ButtonsBlock(
      {Act(Action::RemoveChosen, removeLabel, true), Act(Action::Close, L"Cancel")}));
  m.excludedFromCapture = asked;
}

void BuildEnding(ScreenModel& m, Fixture const& f, Screen s) {
  std::wstring title, text;
  switch (s) {
    case Screen::EndingTimedOut:
      title = L"Demo ending: the code timed out";
      text = L"Demo model: a real code works for 10 minutes. Start again on the new computer, "
             L"and type its new code here.";
      break;
    case Screen::EndingCancelled:
      title = L"Demo ending: adding the device was cancelled";
      text = L"Demo model: one of the two computers stopped. Nothing was added, and nothing "
             L"changed.";
      break;
    case Screen::EndingNoMatch:
      title = L"Demo ending: the emoji did not match";
      text = L"Demo model: nothing was added. Emoji that do not match can mean that someone "
             L"else has the code, so start again with a new one.";
      break;
    case Screen::EndingRemoveFirst:
      title = L"Demo ending: remove a device first";
      text = L"Demo model: an account holds at most 10 devices. Remove one in Settings, "
             L"Account, then add this one.";
      break;
    default:
      title = L"Demo ending: this device can add no more until " + f.otherPhraseDevice +
              L" is opened";
      text = L"Demo model: too many changes to your devices are waiting for a phrase device to "
             L"fold them in. Open URmessage on " + f.otherPhraseDevice + L", then try again.";
      break;
  }
  m.blocks.push_back(BannerBlock());
  m.blocks.push_back(TitleBlock(title, TextKind::Fact));
  m.blocks.push_back(TextBlock(text, TextKind::Fact));
  m.blocks.push_back(ButtonsBlock(
      {Go(Screen::AddDevice, L"Try again", true), Act(Action::Close, L"Close")}));
  m.blocks.push_back(TextBlock(L"Demo only: see the other endings", TextKind::DemoOnly,
                               TextStyle::Muted));
  m.blocks.push_back(ButtonsBlock({
      Go(Screen::EndingTimedOut, L"Demo only: timed out", false, TextKind::DemoOnly),
      Go(Screen::EndingCancelled, L"Demo only: cancelled", false, TextKind::DemoOnly),
      Go(Screen::EndingNoMatch, L"Demo only: no match", false, TextKind::DemoOnly),
      Go(Screen::EndingRemoveFirst, L"Demo only: remove a device first", false,
         TextKind::DemoOnly),
      Go(Screen::EndingPhraseDevice, L"Demo only: a phrase device must open", false,
         TextKind::DemoOnly),
  }));
}

void BuildAlarm(ScreenModel& m, Screen s) {
  m.alarm = true;
  m.blocks.push_back(BannerBlock());
  std::vector<Button> offers = {Act(Action::OpenAccount, L"Check my devices", true),
                                Go(Screen::LockOut, L"Lock out a device")};
  if (s == Screen::AlarmList) {
    m.blocks.push_back(
        TitleBlock(L"Demo alarm: a device list this device cannot explain", TextKind::Fact));
    m.blocks.push_back(TextBlock(
        L"Demo model, made up: a real alarm here means that your list of devices changed in a "
        L"way none of your devices explains, such as a second successor of one of your keys.",
        TextKind::Fact));
    offers.push_back(Go(Screen::ChangeOld, L"Change my recovery phrase"));
  } else if (s == Screen::AlarmKey) {
    m.blocks.push_back(TitleBlock(
        L"Demo alarm: this device's key signed a change that this device did not make",
        TextKind::Fact));
    m.blocks.push_back(TextBlock(
        L"Demo model, made up: a real alarm here means that a copy of this device's key is in "
        L"use somewhere else.",
        TextKind::Fact));
  } else {
    m.blocks.push_back(TitleBlock(L"Demo alarm: a phrase you replaced was used", TextKind::Fact));
    m.blocks.push_back(TextBlock(
        L"Demo model, made up: a real alarm here means that someone used a recovery phrase you "
        L"had replaced, in a conversation where it still counted.",
        TextKind::Fact));
    m.blocks.push_back(TextBlock(
        L"Demo model: changing your phrase again cannot undo this. To start over with a new ID, "
        L"remove each device, end the account on the last one, and set up again.",
        TextKind::Fact, TextStyle::Strong));
  }
  m.blocks.push_back(ButtonsBlock(std::move(offers)));
}

// Every string a model carries, with its kind, in reading order: what Property 1 reads.
struct KindedString {
  TextKind kind;
  std::wstring text;
};

std::vector<KindedString> StringsOf(ScreenModel const& m) {
  std::vector<KindedString> out;
  for (Block const& b : m.blocks) {
    switch (b.type) {
      case BlockType::Banner:
      case BlockType::Title:
      case BlockType::Text:
        out.push_back({b.kind, b.text});
        break;
      case BlockType::Code:
        out.push_back({b.labelKind, b.label});
        // the code itself is a fact; its label carries the framing (the code cannot)
        break;
      case BlockType::WordFields:
        out.push_back({b.labelKind, b.label});
        if (!b.text.empty()) out.push_back({TextKind::Fact, b.text});
        break;
      case BlockType::Words:
      case BlockType::Emoji:
      case BlockType::CodeField:
      case BlockType::NameField:
        out.push_back({b.labelKind, b.label});
        break;
      case BlockType::Devices:
      case BlockType::KeepOrRemove:
        out.push_back({b.labelKind, b.label});
        for (DeviceLine const& d : b.devices) out.push_back({TextKind::Fact, d.detail});
        break;
      case BlockType::Buttons:
        for (Button const& x : b.buttons) out.push_back({x.kind, x.label});
        break;
      case BlockType::Divider:
        break;
    }
  }
  return out;
}

std::vector<KindedString> StringsOfAccount(AccountModel const& a) {
  std::vector<KindedString> out = {
      {TextKind::Fact, a.caption},        {TextKind::Chrome, a.idTitle},
      {TextKind::Fact, a.idNote},         {TextKind::Fact, a.idValue},
      {TextKind::Fact, a.copied},         {a.copy.kind, a.copy.label},
      {TextKind::Chrome, a.changeTitle},  {TextKind::Fact, a.changeNote},
      {a.change.kind, a.change.label},    {TextKind::Fact, a.devicesCaption},
      {TextKind::Fact, a.devicesNote},    {a.add.kind, a.add.label},
      {TextKind::DemoOnly, a.moreCaption},
  };
  for (AccountDevice const& d : a.devices) {
    out.push_back({TextKind::Fact, d.detail});
    out.push_back({TextKind::Fact, d.added});
    for (Button const& b : d.buttons) out.push_back({b.kind, b.label});
  }
  for (Button const& b : a.more) out.push_back({b.kind, b.label});
  return out;
}

bool ShowsOrTakesWords(ScreenModel const& m) {
  for (Block const& b : m.blocks)
    if (b.type == BlockType::Words || b.type == BlockType::WordFields) return true;
  return false;
}

std::wstring AsciiOnly(std::wstring const& s) {
  std::wstring out;
  for (wchar_t c : s) {
    if (c < 0x20 || 0x7E < c)
      out += std::format(L"<U+{:04X}>", static_cast<unsigned int>(c));
    else
      out.push_back(c);
  }
  return out;
}

bool IsLowerHex(wchar_t c) { return (L'0' <= c && c <= L'9') || (L'a' <= c && c <= L'f'); }

// Four groups of four lowercase hex characters, with one space between groups (SCOPE B2).
bool IsDeviceIdShape(std::wstring const& s) {
  if (s.size() != 19) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i % 5 == 4) {
      if (s[i] != L' ') return false;
    } else if (!IsLowerHex(s[i])) {
      return false;
    }
  }
  return true;
}

// A UUID of 36 characters whose version nibble is 8 and whose variant is RFC 4122's (SCOPE B1).
bool IsUuidV8Shape(std::wstring const& s) {
  if (s.size() != 36) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != L'-') return false;
    } else if (!IsLowerHex(s[i])) {
      return false;
    }
  }
  return s[14] == L'8' && (s[19] == L'8' || s[19] == L'9' || s[19] == L'a' || s[19] == L'b');
}

}  // namespace

// ---- the fixture ------------------------------------------------------------------------------

Fixture const& GetFixture() {
  static const Fixture fixture = [] {
    Fixture f;
    f.accountId = L"3b9e2f4c-1a7d-8e05-9c62-4f1d0a8b7e33";
    f.code = L"7KQ4M2XD9PRF";
    f.wordsSetUp = ToVector(kWordsSetUp);
    f.wordsChanged = ToVector(kWordsChanged);
    f.emoji.assign(DemoEmoji().begin(), DemoEmoji().end());
    World const& w = GetWorld();
    f.complete = !w.myDevices.empty();
    for (DeviceRef const& ref : w.myDevices) {
      FixtureDevice d;
      d.worldId = ref.id;
      d.name = ref.name;
      d.thisDevice = ref.isThisComputer;
      DeviceData const* data = nullptr;
      for (auto const& row : kDeviceData)
        if (ref.id == row.worldId) data = &row;
      if (data == nullptr) {
        f.complete = false;
        d.deviceId = L"0000 0000 0000 0000";
        d.added = L"no demo data for this device";
      } else {
        d.deviceId = data->deviceId;
        d.phraseDevice = data->phraseDevice;
        if (data->addedBy < 0 || static_cast<size_t>(data->addedBy) >= w.myDevices.size()) {
          d.added = std::wstring(L"Demo: set up on ") + data->addedOn + L", the first device";
        } else {
          d.added = std::wstring(L"Demo: added on ") + data->addedOn + L" by " +
                    w.myDevices[static_cast<size_t>(data->addedBy)].name;
        }
      }
      f.devices.push_back(std::move(d));
    }
    for (FixtureDevice const& d : f.devices) {
      if (d.phraseDevice && !d.thisDevice && f.otherPhraseDevice.empty())
        f.otherPhraseDevice = d.name;
      if (!d.phraseDevice && !d.thisDevice && f.noticeAdded.empty()) f.noticeAdded = d.name;
    }
    if (f.otherPhraseDevice.empty()) f.otherPhraseDevice = L"your phrase device";
    const std::wstring self = f.devices.empty() ? std::wstring(L"this device")
                                                : f.devices[ThisDeviceIndex(f)].name;
    if (f.noticeAdded.empty()) f.noticeAdded = self;
    f.noticeAddedOn = L"5 Oct 2026";
    f.noticeAddedBy = self;
    f.noticeRemoved = L"Old laptop";
    f.noticeRemovedOn = L"11 Oct 2026";
    f.noticeRemovedBy = self;
    f.selfRemoved = L"Old laptop";
    f.removedOn = L"14 Oct 2026";
    f.removedBy = f.otherPhraseDevice;
    return f;
  }();
  return fixture;
}

Selection PresetSelection(Screen s) {
  Fixture const& f = GetFixture();
  Selection sel;
  if (s == Screen::Remove || s == Screen::NoticeAdded) {
    for (size_t i = 0; i < f.devices.size(); ++i)
      if (f.devices[i].name == f.noticeAdded && !f.devices[i].thisDevice) sel.devices |= 1u << i;
  } else if (s == Screen::RemovePhrase) {
    for (size_t i = 0; i < f.devices.size(); ++i)
      if (f.devices[i].phraseDevice && !f.devices[i].thisDevice) {
        sel.devices |= 1u << i;
        break;
      }
  } else if (s == Screen::RemoveOnly) {
    sel.onlyDevice = true;
    sel.devices = 1u << ThisDeviceIndex(f);
  } else if (s == Screen::LockOut) {
    sel.lockOut = true;
  } else if (s == Screen::Rename) {
    for (size_t i = 0; i < f.devices.size(); ++i)
      if (!f.devices[i].thisDevice) {
        sel.device = static_cast<int>(i);
        break;
      }
  }
  return sel;
}

bool PhraseAsked(Fixture const& f, Selection const& sel) {
  if (sel.lockOut) return true;
  bool otherPhraseDevice = false, otherDevice = false, selfIsPhraseDevice = false;
  for (size_t i = 0; i < f.devices.size(); ++i) {
    if (f.devices[i].thisDevice) {
      selfIsPhraseDevice = f.devices[i].phraseDevice;
      continue;
    }
    if (!Has(sel, i)) continue;
    otherDevice = true;
    if (f.devices[i].phraseDevice) otherPhraseDevice = true;
  }
  if (otherPhraseDevice) return true;              // G3: removing a phrase device is a lock-out
  if (otherDevice && !selfIsPhraseDevice) return true;  // G2: a device that is not asks
  return false;                                    // G2: a phrase device asks nothing; RM1 itself
}

std::wstring DemoDeviceName(size_t index) {
  Fixture const& f = GetFixture();
  auto const& renames = Renames();
  if (index < renames.size() && !renames[index].empty()) return renames[index];
  return index < f.devices.size() ? f.devices[index].name : std::wstring();
}

void SetDemoDeviceName(size_t index, std::wstring name) {
  auto& renames = Renames();
  if (renames.size() <= index) renames.resize(index + 1);
  renames[index] = Trim(name);
}

// ---- Settings -> Account ----------------------------------------------------------------------

AccountModel BuildAccount(RunMode mode) {
  AccountModel a;
  // NOT IN LIVE MODE, for the reason BuildScreen gives: a live launch keeps the inert ACCOUNT group
  // until the library's identity is there to draw (Task 16).
  if (mode == RunMode::Live) return a;
  Fixture const& f = GetFixture();
  a.built = true;
  a.caption = L"DEMO MODEL: ACCOUNT";
  a.idTitle = L"Your ID";
  a.idNote = AccountIdNote(mode);
  a.idValue = L"Demo ID " + f.accountId;
  a.copied = L"Demo ID, made up, not an account: " + f.accountId;
  a.copy = Act(Action::Close, L"Copy");
  a.changeTitle = L"Change my recovery phrase";
  a.changeNote = L"Demo model: type the demo words, then see new ones. Your demo ID stays the same.";
  a.change = Go(Screen::ChangeOld, L"Change");
  a.devicesCaption = L"DEMO MODEL: DEVICES";
  a.devicesNote =
      L"Demo model: made-up devices, device IDs and dates, over the fabricated world. A real "
      L"account holds at most 10 devices.";
  for (size_t i = 0; i < f.devices.size(); ++i) {
    FixtureDevice const& d = f.devices[i];
    AccountDevice row;
    row.index = static_cast<int>(i);
    row.name = DemoDeviceName(i);
    row.detail = DeviceDetail(d);
    row.added = d.added;
    row.thisDevice = d.thisDevice;
    row.phraseDevice = d.phraseDevice;
    Button rename = Go(Screen::Rename, L"Rename");
    rename.device = static_cast<int>(i);
    Button remove = Go(Screen::Remove, L"Remove");
    remove.device = static_cast<int>(i);
    row.buttons.push_back(std::move(rename));
    row.buttons.push_back(std::move(remove));
    if (d.thisDevice && !d.phraseDevice)
      row.buttons.push_back(Go(Screen::MakePhrase, L"Make this a phrase device"));
    a.devices.push_back(std::move(row));
  }
  a.add = Go(Screen::AddDevice, L"Add a device", true);
  a.moreCaption = L"DEMO ONLY: MORE SCREENS";
  a.more = {
      Go(Screen::SetUp, L"Demo only: Set up this device", false, TextKind::DemoOnly),
      Go(Screen::Removed, L"Demo only: the removed screen", false, TextKind::DemoOnly),
      Go(Screen::NoticeAdded, L"Demo only: a notice, a device added", false, TextKind::DemoOnly),
      Go(Screen::NoticeRemoved, L"Demo only: a notice, a device removed", false,
         TextKind::DemoOnly),
      Go(Screen::NoticeSelfRemoved, L"Demo only: a notice, a phrase device removed itself", false,
         TextKind::DemoOnly),
      Go(Screen::AlarmList, L"Demo only: the alarm, a device list", false, TextKind::DemoOnly),
      Go(Screen::AlarmKey, L"Demo only: the alarm, this device's key", false, TextKind::DemoOnly),
      Go(Screen::AlarmRival, L"Demo only: the alarm, a replaced phrase", false,
         TextKind::DemoOnly),
      Go(Screen::EndingTimedOut, L"Demo only: the endings of adding a device", false,
         TextKind::DemoOnly),
      Go(Screen::RemoveOnly, L"Demo only: removing an only device", false, TextKind::DemoOnly),
      Go(Screen::MakePhrase, L"Demo only: make this a phrase device", false, TextKind::DemoOnly),
  };
  return a;
}

// ---- the screens ------------------------------------------------------------------------------

ScreenModel BuildScreen(Screen s, RunMode mode, Selection const& sel) {
  ScreenModel m;
  m.screen = s;
  // NOT ONE IDENTITY SCREEN IS BUILT IN LIVE MODE IN THIS BUILD. The live screens draw the library's
  // identity (Tasks 16, 28, 38, 45); until they exist, a live launch must never draw these made-up
  // ones, and an unbuilt model is how the window knows not to.
  if (mode == RunMode::Live || s == Screen::None || s == Screen::Account || s == Screen::kCount)
    return m;
  m.built = true;
  Fixture const& f = GetFixture();

  switch (s) {
    case Screen::SetUp: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(L"Set up this device"));
      m.blocks.push_back(TextBlock(L"Already use URmessage on another computer?", TextKind::Chrome,
                                   TextStyle::Strong));
      m.blocks.push_back(TextBlock(
          L"Type this code on a computer you already use: Settings, then Account, then Add a "
          L"device.",
          TextKind::Chrome));
      Block code;
      code.type = BlockType::Code;
      code.text = CodeForDisplay(f.code);
      code.label = L"Demo code, made up: no computer can use it. A real code works for 10 minutes.";
      code.labelKind = TextKind::Fact;
      m.blocks.push_back(std::move(code));
      m.blocks.push_back(TextBlock(L"Demo model: no other computer will answer this code.",
                                   TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(ButtonsBlock({Go(Screen::EmojiNew,
                                          L"Demo only: go on as if the code was typed", false,
                                          TextKind::DemoOnly)}));
      {
        Block rule;
        rule.type = BlockType::Divider;
        m.blocks.push_back(std::move(rule));
      }
      m.blocks.push_back(TextBlock(L"New to URmessage?", TextKind::Chrome, TextStyle::Strong));
      m.blocks.push_back(TextBlock(
          L"Demo model: a real first device shows its recovery phrase of 25 words here, once. "
          L"This demo shows 25 made-up words instead.",
          TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(ButtonsBlock({Go(Screen::Words, L"This is my first device", true)}));
      m.blocks.push_back(ButtonsBlock(
          {Act(Action::Close, L"Demo only: skip to the app", false, TextKind::DemoOnly)}));
      break;
    }
    case Screen::Words:
    case Screen::ChangeNew: {
      const bool change = s == Screen::ChangeNew;
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(
          TitleBlock(change ? L"Write down your new 25 words" : L"Write down these 25 words"));
      m.blocks.push_back(WordsBlock(mode, change ? f.wordsChanged : f.wordsSetUp));
      m.blocks.push_back(TextBlock(PhraseWordsCaveat(mode), TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(ButtonsBlock({Go(change ? Screen::ChangeTypeBack : Screen::TypeBack,
                                          L"I wrote them down", true)}));
      m.excludedFromCapture = true;
      break;
    }
    case Screen::TypeBack:
    case Screen::TypeBackWrong:
    case Screen::ChangeTypeBack: {
      const bool change = s == Screen::ChangeTypeBack;
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(
          TitleBlock(change ? L"Type the new words back" : L"Type the 25 words back"));
      m.blocks.push_back(TextBlock(
          change ? L"Type each new word in its box, in order, from your paper."
                 : L"Type each word in its box, in order, from your paper.",
          TextKind::Chrome));
      Block fields = WordFieldsBlock(kWordFieldsLabel, change ? f.wordsChanged : f.wordsSetUp);
      if (s == Screen::TypeBackWrong) {
        fields.prefill = fields.words;
        fields.prefill[kWrongIndex] = kWrongWord;
        fields.wrongIndex = kWrongIndex;
        fields.text = WordCheckLine(mode, FirstWrongWord(fields.prefill, fields.words), false);
      }
      m.blocks.push_back(std::move(fields));
      m.blocks.push_back(ButtonsBlock(
          {Act(Action::FillWords, kFillWords, false, TextKind::DemoOnly),
           Act(Action::CheckWords, L"Check the words", true, TextKind::Chrome,
               static_cast<int>(change ? Screen::ChangeDone : Screen::SetUpDone))}));
      m.excludedFromCapture = true;
      break;
    }
    case Screen::SetUpDone: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(
          TitleBlock(L"Demo model: this device would be set up now", TextKind::Fact));
      m.blocks.push_back(TextBlock(
          L"Demo model: a real setup makes your ID and this device's ID here, and this device "
          L"keeps the phrase's key. Nothing was made.",
          TextKind::Fact));
      m.blocks.push_back(ButtonsBlock({Act(Action::OpenAccount, L"See Your ID in Settings", true),
                                       Act(Action::Close, L"Open the app")}));
      break;
    }
    case Screen::EmojiNew:
    case Screen::EmojiExisting: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(L"Compare the emoji"));
      m.blocks.push_back(TextBlock(
          s == Screen::EmojiNew
              ? L"Check that your other computer shows these seven emoji, in this order."
              : L"Check that the new computer shows these seven emoji, in this order.",
          TextKind::Chrome));
      Block emoji;
      emoji.type = BlockType::Emoji;
      emoji.label = L"Demo emoji, made up: no session ran, and no other computer shows them.";
      emoji.labelKind = TextKind::Fact;
      emoji.emoji = f.emoji;
      m.blocks.push_back(std::move(emoji));
      m.blocks.push_back(ButtonsBlock(
          {Go(Screen::Granted, L"They match", true), Go(Screen::EndingNoMatch, L"They don't match")}));
      break;
    }
    case Screen::AddDevice: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(L"Add a device"));
      m.blocks.push_back(TextBlock(
          L"On the new computer, open URmessage and choose Set up this device. Type the code it "
          L"shows.",
          TextKind::Chrome));
      Block field;
      field.type = BlockType::CodeField;
      field.label = L"Code";
      field.labelKind = TextKind::Chrome;
      m.blocks.push_back(std::move(field));
      m.blocks.push_back(TextBlock(L"Demo model: no code is checked here. Any code goes on to the emoji.",
                                   TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(ButtonsBlock(
          {Go(Screen::EmojiExisting, L"Continue", true), Act(Action::Close, L"Cancel")}));
      break;
    }
    case Screen::Granted: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(
          TitleBlock(L"Demo model: the new device would be added now", TextKind::Fact));
      m.blocks.push_back(TextBlock(
          L"Demo model: after a real match on both computers, the new one is added to your "
          L"account and joins your conversations by itself. Nothing was added.",
          TextKind::Fact));
      m.blocks.push_back(ButtonsBlock({Act(Action::Close, L"Done", true)}));
      break;
    }
    case Screen::EndingTimedOut:
    case Screen::EndingCancelled:
    case Screen::EndingNoMatch:
    case Screen::EndingRemoveFirst:
    case Screen::EndingPhraseDevice:
      BuildEnding(m, f, s);
      break;
    case Screen::Remove:
    case Screen::RemovePhrase:
    case Screen::RemoveOnly:
    case Screen::LockOut:
      BuildRemove(m, mode, f, sel);
      break;
    case Screen::Removed: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(RemovedTitle(mode), TextKind::Fact));
      m.blocks.push_back(TextBlock(
          L"Demo model, made up: removed on " + f.removedOn + L" by " + f.removedBy + L".",
          TextKind::Fact));
      m.blocks.push_back(ButtonsBlock({Go(Screen::SetUp, L"Add this device again", true)}));
      m.blocks.push_back(TextBlock(
          L"Demo model: a real device comes back as a new device, with a new device ID, under the "
          L"same Your ID.",
          TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(TextBlock(L"If this was not you", TextKind::Chrome, TextStyle::Strong));
      m.blocks.push_back(TextBlock(
          L"Open URmessage on a device that is still on your account, and check your devices "
          L"there.",
          TextKind::Chrome));
      m.blocks.push_back(ButtonsBlock(
          {Act(Action::Close, L"Demo only: back to the app", false, TextKind::DemoOnly)}));
      break;
    }
    case Screen::NoticeAdded:
    case Screen::NoticeRemoved:
    case Screen::NoticeSelfRemoved: {
      m.dialog = true;
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(L"A change to your devices"));
      if (s == Screen::NoticeAdded) {
        m.blocks.push_back(TextBlock(L"Demo notice, made up: " + f.noticeAdded + L" was added on " +
                                         f.noticeAddedOn + L" by " + f.noticeAddedBy + L".",
                                     TextKind::Fact));
        m.blocks.push_back(ButtonsBlock({Go(Screen::Remove, L"Remove " + f.noticeAdded, true),
                                         Act(Action::Close, L"Dismiss")}));
      } else if (s == Screen::NoticeRemoved) {
        m.blocks.push_back(TextBlock(L"Demo notice, made up: " + f.noticeRemoved +
                                         L" was removed on " + f.noticeRemovedOn + L" by " +
                                         f.noticeRemovedBy + L".",
                                     TextKind::Fact));
        m.blocks.push_back(ButtonsBlock({Act(Action::Close, L"Dismiss", true)}));
      } else {
        m.blocks.push_back(TextBlock(SelfRemovalNotice(mode, f.selfRemoved), TextKind::Fact));
        m.blocks.push_back(ButtonsBlock(
            {Go(Screen::LockOut, L"Lock it out", true), Act(Action::Close, L"Dismiss")}));
      }
      break;
    }
    case Screen::AlarmList:
    case Screen::AlarmKey:
    case Screen::AlarmRival:
      BuildAlarm(m, s);
      break;
    case Screen::Rename: {
      m.dialog = true;
      const size_t at = sel.device >= 0 && static_cast<size_t>(sel.device) < f.devices.size()
                            ? static_cast<size_t>(sel.device)
                            : ThisDeviceIndex(f);
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(L"Rename " + DemoDeviceName(at)));
      Block field;
      field.type = BlockType::NameField;
      field.text = DemoDeviceName(at);
      field.label = L"Demo name: kept until the app closes, and no other device sees it.";
      field.labelKind = TextKind::Fact;
      m.blocks.push_back(std::move(field));
      m.blocks.push_back(ButtonsBlock(
          {Act(Action::SaveName, L"Save", true, TextKind::Chrome, static_cast<int>(at)),
           Act(Action::Close, L"Cancel")}));
      break;
    }
    case Screen::MakePhrase: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(L"Make this a phrase device"));
      m.blocks.push_back(TextBlock(L"Type your recovery phrase, the one you have on paper.",
                                   TextKind::Chrome));
      m.blocks.push_back(TextBlock(
          L"Demo model: a phrase device keeps the phrase's key, so it can remove your other "
          L"devices without asking for the phrase.",
          TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(WordFieldsBlock(kPhraseFieldLabel, f.wordsSetUp));
      m.blocks.push_back(ButtonsBlock(
          {Act(Action::FillWords, kFillWords, false, TextKind::DemoOnly),
           Act(Action::CheckWords, L"Make it a phrase device", true, TextKind::Chrome,
               static_cast<int>(Screen::Account)),
           Act(Action::Close, L"Cancel")}));
      m.excludedFromCapture = true;
      break;
    }
    case Screen::ChangeOld: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(TitleBlock(L"Change my recovery phrase"));
      m.blocks.push_back(TextBlock(L"Type your recovery phrase, the one you have on paper.",
                                   TextKind::Chrome));
      m.blocks.push_back(TextBlock(
          L"Demo model: a real change makes a new phrase at once. Your ID, your devices and your "
          L"conversations stay, and your contacts see no change.",
          TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(WordFieldsBlock(kWordFieldsLabel, f.wordsSetUp));
      m.blocks.push_back(ButtonsBlock(
          {Act(Action::FillWords, kFillWords, false, TextKind::DemoOnly),
           Act(Action::CheckWords, L"Continue", true, TextKind::Chrome,
               static_cast<int>(Screen::ChangeNew)),
           Act(Action::Close, L"Cancel")}));
      m.excludedFromCapture = true;
      break;
    }
    case Screen::ChangeDone: {
      m.blocks.push_back(BannerBlock());
      m.blocks.push_back(
          TitleBlock(L"Demo model: the phrase would be changed now", TextKind::Fact));
      m.blocks.push_back(TextBlock(PhraseChangedLine(mode, f.otherPhraseDevice), TextKind::Fact));
      m.blocks.push_back(TextBlock(
          L"Demo model: a phrase you replace still counts in a conversation until one of your "
          L"devices in it has run since the change, so open URmessage on your other devices soon "
          L"after.",
          TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(ButtonsBlock({Act(Action::OpenAccount, L"Done", true)}));
      break;
    }
    default:
      m.built = false;
      break;
  }
  return m;
}

// ---- the refusal ------------------------------------------------------------------------------

ScreenModel CaptureRefusal(ScreenModel const& screen) {
  ScreenModel m;
  m.screen = screen.screen;
  m.built = screen.built;
  m.dialog = screen.dialog;
  // The banner and the first title only: every other block is left behind, the words, the word
  // fields and the buttons that fill or check them with the rest.
  bool titled = false;
  for (Block const& b : screen.blocks) {
    if (b.type == BlockType::Banner) m.blocks.push_back(b);
    if (b.type == BlockType::Title && !titled) {
      m.blocks.push_back(b);
      titled = true;
    }
  }
  m.blocks.push_back(TextBlock(kCaptureRefusedLine, TextKind::Chrome, TextStyle::Error));
  m.blocks.push_back(ButtonsBlock({Act(Action::Close, L"Close", true)}));
  m.excludedFromCapture = false;
  return m;
}

// ---- the check --------------------------------------------------------------------------------

int FirstWrongWord(std::vector<std::wstring> const& typed,
                   std::vector<std::wstring> const& expected) {
  for (size_t i = 0; i < expected.size(); ++i) {
    const std::wstring got = i < typed.size() ? Lower(Trim(typed[i])) : std::wstring();
    if (got != Lower(expected[i])) return static_cast<int>(i);
  }
  return -1;
}

std::wstring WordCheckLine(RunMode mode, int firstWrong, bool empty) {
  if (mode == RunMode::Live) {
    if (firstWrong < 0) return L"All 25 words match.";
    return empty ? std::format(L"Word {} is empty.", firstWrong + 1)
                 : std::format(L"Word {} is not the word you were shown.", firstWrong + 1);
  }
  if (firstWrong < 0) return L"Demo check: all 25 words match the demo list.";
  return empty ? std::format(L"Demo check: word {} is empty.", firstWrong + 1)
               : std::format(L"Demo check: word {} is not the word you were shown.", firstWrong + 1);
}

// ---- the live screens (PLAN Task 16: slice 1) --------------------------------------------------
//
// BUILT FROM WHAT THE LIBRARY ANSWERED AND NOTHING ELSE: no fixture value and no banner, since there
// is no demo here to frame. Every string is chrome, a pair's live arm (RunMode.h's PhraseWordsLabel,
// PhraseWordsCaveat and AccountIdNote, gated in RunModeCopyDiagnostics), or a fact the library
// handed over. Live/LiveIdentityDiagnostics.cpp holds that no string here names the demo, that the
// screens which show or take the words are excluded from capture, and that "Your ID" is the
// library's account ID character for character.

namespace {

constexpr wchar_t kLiveFieldsLabel[] =
    L"Type each word in its box, in order, from your paper. A word is hidden when you leave its box.";

Block LiveRemoteBlock() {
  return TextBlock(urmsg::live::kRemoteSessionWarning, TextKind::Fact, TextStyle::Error);
}

std::wstring AsciiWide(std::string const& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace

ScreenModel BuildLiveScreen(Screen s, LiveInputs const& in) {
  ScreenModel m;
  m.screen = s;
  auto failure = [&] {
    if (!in.failure.empty()) m.blocks.push_back(TextBlock(in.failure, TextKind::Fact, TextStyle::Error));
  };
  switch (s) {
    case Screen::SetUp: {
      // THE IDENTITY COULD NOT BE READ (X-APP-3): the session has ended, and the screen says why, in
      // the worker's sentence, with nothing to press. Not "Set up this device": that would claim
      // this device has no identity, which nothing has read.
      if (in.stopped) {
        m.built = true;
        m.blocks.push_back(TitleBlock(L"URmessage cannot go on"));
        failure();
        break;
      }
      // SCOPE E1's second road only: the code road, which adds this computer to an account that
      // already exists, is slice 2's (Task 28), so this screen says it cannot, and shows no code
      m.built = true;
      m.blocks.push_back(TitleBlock(L"Set up this device"));
      m.blocks.push_back(TextBlock(L"New to URmessage?", TextKind::Chrome, TextStyle::Strong));
      m.blocks.push_back(TextBlock(
          L"Your account is made on this device. It comes with a recovery phrase of 25 words: you "
          L"write them down, then type them back.",
          TextKind::Chrome));
      m.blocks.push_back(TextBlock(
          L"This build cannot add this computer to an account you already use.", TextKind::Fact,
          TextStyle::Muted));
      failure();
      if (in.waiting) {
        m.blocks.push_back(TextBlock(L"Making your words...", TextKind::Chrome, TextStyle::Muted));
      } else {
        m.blocks.push_back(ButtonsBlock({Act(Action::MakeWords, L"This is my first device", true)}));
      }
      break;
    }
    case Screen::Words: {
      m.built = true;
      // BY SESSION KIND (Spec C section 6.2; X-APP-5): on the machine itself, excluded from
      // capture; in a remote session the exclusion would black the window for the only person who
      // can read it, so it is skipped, and the inline warning says what that means
      m.excludedFromCapture = !in.remoteSession;
      m.blocks.push_back(TitleBlock(L"Write down these 25 words"));
      if (in.remoteSession) m.blocks.push_back(LiveRemoteBlock());
      m.blocks.push_back(WordsBlock(RunMode::Live, in.words));
      m.blocks.push_back(TextBlock(PhraseWordsCaveat(RunMode::Live), TextKind::Fact, TextStyle::Muted));
      m.blocks.push_back(TextBlock(
          L"Write them on paper, in order. They are shown here once, and URmessage never stores "
          L"them.",
          TextKind::Chrome));
      m.blocks.push_back(ButtonsBlock({Go(Screen::TypeBack, L"I wrote them down", true)}));
      break;
    }
    case Screen::TypeBack:
    case Screen::TypeBackWrong: {
      m.built = true;
      m.excludedFromCapture = !in.remoteSession;  // by session kind, as the words screen
      m.blocks.push_back(TitleBlock(L"Type the 25 words back"));
      if (in.remoteSession) m.blocks.push_back(LiveRemoteBlock());
      Block fields;
      fields.type = BlockType::WordFields;
      fields.label = kLiveFieldsLabel;
      fields.labelKind = TextKind::Chrome;
      // 25 fields; the words they are checked against are the library's, and never here
      fields.words = std::vector<std::wstring>(urmsg::live::kPhraseWords);
      fields.fields = kLiveWordFieldPolicy;
      // WHAT WAS TYPED STAYS AS TYPED: a wrong word is named, and nothing is corrected (SCOPE A1)
      fields.prefill = in.typed;
      if (s == Screen::TypeBackWrong) {
        fields.wrongPositions = in.wrong;
        fields.text = urmsg::live::WrongWordsLine(in.wrong);
        fields.markNote = L"Not the word you were shown.";
      } else if (!in.boxes.empty()) {
        // THE APP NAMED THESE BEFORE SENDING (X-APP-1): an empty box, a box of several words, or one
        // whose word the list's own check refuses. Nothing was sent.
        for (auto const& issue : in.boxes) fields.wrongPositions.push_back(issue.position);
        fields.text = urmsg::live::BoxesLine(in.boxes);
        fields.markNote = L"Type one word from your paper in this box.";
      }
      m.blocks.push_back(std::move(fields));
      failure();
      if (in.waiting) {
        m.blocks.push_back(TextBlock(L"Checking the words...", TextKind::Chrome, TextStyle::Muted));
      } else {
        m.blocks.push_back(ButtonsBlock({Act(Action::ConfirmWords, L"Check the words", true),
                                         Act(Action::MakeWords, L"Back to the words")}));
      }
      break;
    }
    case Screen::SetUpDone: {
      m.built = true;
      m.blocks.push_back(TitleBlock(L"This device is set up"));
      m.blocks.push_back(TextBlock(
          L"Your account and this device have their IDs now, and this device keeps your recovery "
          L"phrase's key.",
          TextKind::Fact));
      m.blocks.push_back(ButtonsBlock({Act(Action::OpenAccount, L"See Your ID in Settings", true),
                                       Act(Action::Close, L"Open the app")}));
      break;
    }
    default:
      break;
  }
  return m;
}

void WipeScreenWords(ScreenModel& m) {
  for (auto& b : m.blocks) {
    urmsg::live::Wipe(b.words);
    urmsg::live::Wipe(b.prefill);
  }
}

void WipeLiveInputs(LiveInputs& in) {
  urmsg::live::Wipe(in.words);
  urmsg::live::Wipe(in.typed);
}

bool RebuildForSessionKind(Screen shown, bool liveShown, bool builtRemote, bool remoteNow) {
  if (!liveShown || builtRemote == remoteNow) return false;
  // exactly the screens whose build turns on the kind: the ones that show or take the words
  return shown == Screen::Words || shown == Screen::TypeBack || shown == Screen::TypeBackWrong;
}

AccountModel BuildLiveAccount(urmsg::live::IdentityInfo const& id) {
  AccountModel a;
  if (!id.read || !id.hasIdentity) return a;
  a.built = true;
  a.caption = L"ACCOUNT";
  a.idTitle = L"Your ID";
  a.idNote = AccountIdNote(RunMode::Live);
  // PROPERTY 3: the library's account ID, character for character, and Copy puts exactly that on
  // the clipboard
  a.idValue = AsciiWide(id.accountId);
  a.copied = a.idValue;
  a.copy = Act(Action::Close, L"Copy");
  // "Change my recovery phrase" (slice 4) and "Add a device" (slice 2) are not in this build, so
  // they are not drawn; nor are Rename and Remove (slices 2 and 3)
  a.devicesCaption = L"DEVICES";
  a.devicesNote = L"The devices on your account, as your account's newest device list names them.";
  for (size_t i = 0; i < id.devices.size(); ++i) {
    urmsg::live::IdentityDevice const& d = id.devices[i];
    AccountDevice row;
    row.index = static_cast<int>(i);
    row.name = d.thisDevice ? L"This device" : L"Another device";
    row.detail = L"Device ID " + AsciiWide(d.deviceId);
    if (d.thisDevice) row.detail += L" \u00B7 this device";
    if (d.phraseDevice) row.detail += L" \u00B7 phrase device";
    row.thisDevice = d.thisDevice;
    row.phraseDevice = d.phraseDevice;
    a.devices.push_back(std::move(row));
  }
  return a;
}

// ---- the gate ---------------------------------------------------------------------------------

std::vector<std::wstring> IdentityDemoDiagnostics() {
  std::vector<std::wstring> lines;
  Fixture const& f = GetFixture();

  // 1. PROPERTY 1 OVER EVERY SCREEN. Each overlay screen is built in fabricated mode and read:
  // its first block is the banner, and every Fact and DemoOnly string names the demo. The chrome
  // is the complement, printed on the next line in full.
  size_t total = 0, built = 0, facts = 0, demoOnly = 0, bad = 0;
  std::wstring firstBad = L"(none)";
  std::vector<std::wstring> chrome;
  for (int i = 1; i < static_cast<int>(Screen::kCount); ++i) {
    const Screen s = static_cast<Screen>(i);
    if (s == Screen::Account) continue;
    ++total;
    const ScreenModel m = BuildScreen(s, RunMode::Fabricated);
    if (!m.built) {
      ++bad;
      if (firstBad == L"(none)") firstBad = std::wstring(ScreenName(s)) + L" (not built)";
      continue;
    }
    ++built;
    const bool bannerFirst = !m.blocks.empty() && m.blocks.front().type == BlockType::Banner &&
                             NamesDemo(m.blocks.front().text);
    if (!bannerFirst) {
      ++bad;
      if (firstBad == L"(none)") firstBad = std::wstring(ScreenName(s)) + L" (no banner first)";
    }
    for (KindedString const& k : StringsOf(m)) {
      if (k.kind == TextKind::Chrome) {
        bool seen = false;
        for (auto const& c : chrome) seen = seen || c == k.text;
        if (!seen) chrome.push_back(k.text);
        continue;
      }
      (k.kind == TextKind::Fact ? facts : demoOnly) += 1;
      if (!NamesDemo(k.text)) {
        ++bad;
        if (firstBad == L"(none)")
          firstBad = std::wstring(ScreenName(s)) + L": \"" + AsciiOnly(k.text) + L"\"";
      }
    }
  }
  // Settings -> Account, read the same way: its caption, the ID, what Copy copies, each device's
  // detail and date, and every button.
  {
    ++total;
    const AccountModel a = BuildAccount(RunMode::Fabricated);
    if (!a.built) {
      ++bad;
      if (firstBad == L"(none)") firstBad = L"account (not built)";
    } else {
      ++built;
      for (KindedString const& k : StringsOfAccount(a)) {
        if (k.kind == TextKind::Chrome) {
          bool seen = false;
          for (auto const& c : chrome) seen = seen || c == k.text;
          if (!seen) chrome.push_back(k.text);
          continue;
        }
        (k.kind == TextKind::Fact ? facts : demoOnly) += 1;
        if (!NamesDemo(k.text)) {
          ++bad;
          if (firstBad == L"(none)") firstBad = L"account: \"" + AsciiOnly(k.text) + L"\"";
        }
      }
    }
  }
  lines.push_back(std::format(
      L"  identity screens : {}  {}/{} screens built in fabricated mode (Settings -> Account "
      L"among them), each overlay opening with its demo banner; {} fact strings and {} demo-only "
      L"controls, every one naming the demo (first bad: {})   [query: for every Screen but None, "
      L"BuildScreen(s, Fabricated) - BuildAccount for Account - is built, an overlay's first block "
      L"is a Banner that contains \"demo\", and every Fact and DemoOnly string - titles, texts, "
      L"the labels over codes, words, emoji, fields and devices, each device's detail and date, "
      L"the check's sentence, what Copy copies, and every button - contains \"demo\", "
      L"case-insensitive. Property 1: the live arms never name the demo (run mode identity), so no "
      L"fabricated fact can be a live one. Device names are the fabricated world's, framed by the "
      L"caption over them]",
      bad == 0 && built == total ? L"PASS" : L"FAIL", built, total, facts, demoOnly, firstBad));
  {
    std::wstring list;
    for (auto const& c : chrome) {
      if (!list.empty()) list += L" | ";
      list += AsciiOnly(c);
    }
    lines.push_back(std::format(
        L"  identity chrome  : the {} chrome strings, the same in both modes and claiming nothing "
        L"about an account, printed so the classification can be read: {}",
        chrome.size(), list));
  }

  // 2. THE WORDS. Two fixed lists; every word 9 letters or more, so none is on the BIP-39 English
  // list (3 to 8 letters) and neither list is a phrase; no word in both; and every Words block of
  // every screen carries the label pair's fabricated arm.
  {
    auto longEnough = [](std::vector<std::wstring> const& words, std::wstring& shortest) {
      bool ok = words.size() == 25;
      for (auto const& w : words) {
        bool letters = !w.empty();
        for (wchar_t c : w) letters = letters && L'a' <= c && c <= L'z';
        if (w.size() < 9 || !letters) {
          ok = false;
          if (shortest.empty()) shortest = w;
        }
      }
      return ok;
    };
    std::wstring shortSetUp, shortChanged;
    const bool setUpOk = longEnough(f.wordsSetUp, shortSetUp);
    const bool changedOk = longEnough(f.wordsChanged, shortChanged);
    size_t shared = 0;
    for (auto const& a : f.wordsSetUp)
      for (auto const& b : f.wordsChanged) shared += a == b ? 1 : 0;
    size_t wordsBlocks = 0, labelled = 0;
    for (int i = 1; i < static_cast<int>(Screen::kCount); ++i) {
      const ScreenModel m = BuildScreen(static_cast<Screen>(i), RunMode::Fabricated);
      for (Block const& b : m.blocks) {
        if (b.type != BlockType::Words) continue;
        ++wordsBlocks;
        if (b.label == PhraseWordsLabel(RunMode::Fabricated)) ++labelled;
      }
    }
    const bool ok = setUpOk && changedOk && shared == 0 && wordsBlocks >= 2 &&
                    labelled == wordsBlocks;
    lines.push_back(std::format(
        L"  identity words   : {}  two fixed demo lists of 25 (setup \"{} ... {}\", change \"{} "
        L"... {}\"), every word of lowercase letters and 9 letters or more (shortest offender: "
        L"{}), so none is on the BIP-39 English list, whose words have 3 to 8 letters, and neither "
        L"list is a recovery phrase; words in both lists: {}; Words blocks labelled \"{}\": {}/{}",
        ok ? L"PASS" : L"FAIL", f.wordsSetUp.empty() ? L"" : f.wordsSetUp.front(),
        f.wordsSetUp.empty() ? L"" : f.wordsSetUp.back(),
        f.wordsChanged.empty() ? L"" : f.wordsChanged.front(),
        f.wordsChanged.empty() ? L"" : f.wordsChanged.back(),
        shortSetUp.empty() && shortChanged.empty() ? std::wstring(L"none")
                                                   : shortSetUp + L" " + shortChanged,
        shared, AsciiOnly(PhraseWordsLabel(RunMode::Fabricated)), labelled, wordsBlocks));
  }

  // 3. PROPERTY 2's POLICY. A screen is excluded from capture exactly when its model shows the
  // words or takes them, over every screen and every selection the Remove dialog can be given.
  // The WINDOW is held to this policy by the shots, which assert a black capture.
  //
  // AND IT FAILS CLOSED: for every model that asks for the exclusion, the refusal a window that
  // cannot be kept out of captures draws in its place (CaptureRefusal) shows no words and takes
  // none, asks for nothing, opens with the screen's banner, keeps its first title, says why, and
  // offers Close and nothing else. The window's use of it is held by the shots'
  // --demo-capture=refused leg.
  {
    std::wstring excluded, notExcluded, firstWrong = L"(none)", refusalWrong = L"(none)";
    size_t nExcluded = 0, nNot = 0, refusals = 0, refusalsRight = 0;
    auto judgeRefusal = [&](ScreenModel const& m, std::wstring const& what) {
      if (!m.excludedFromCapture) return;
      ++refusals;
      const ScreenModel r = CaptureRefusal(m);
      std::wstring title;
      for (Block const& b : m.blocks)
        if (b.type == BlockType::Title && title.empty()) title = b.text;
      bool titleKept = false, sentence = false, demoNamed = true;
      size_t buttons = 0, closes = 0;
      for (Block const& b : r.blocks) {
        if (b.type == BlockType::Title && b.text == title) titleKept = true;
        if (b.type == BlockType::Text && b.text == kCaptureRefusedLine) sentence = true;
        for (Button const& x : b.buttons) {
          ++buttons;
          closes += x.action == Action::Close ? 1 : 0;
        }
      }
      for (KindedString const& k : StringsOf(r))
        if (k.kind != TextKind::Chrome && !NamesDemo(k.text)) demoNamed = false;
      const bool bannerFirst = !r.blocks.empty() && r.blocks.front().type == BlockType::Banner;
      const bool ok = r.built && !r.excludedFromCapture && !ShowsOrTakesWords(r) && bannerFirst &&
                      titleKept && sentence && buttons == 1 && closes == 1 && demoNamed;
      if (ok) {
        ++refusalsRight;
      } else if (refusalWrong == L"(none)") {
        refusalWrong = what;
      }
    };
    for (int i = 1; i < static_cast<int>(Screen::kCount); ++i) {
      const Screen s = static_cast<Screen>(i);
      if (s == Screen::Account) continue;
      const ScreenModel m = BuildScreen(s, RunMode::Fabricated);
      const bool words = ShowsOrTakesWords(m);
      if (m.excludedFromCapture != words && firstWrong == L"(none)") firstWrong = ScreenName(s);
      std::wstring& into = m.excludedFromCapture ? excluded : notExcluded;
      if (!into.empty()) into += L", ";
      into += ScreenName(s);
      (m.excludedFromCapture ? nExcluded : nNot) += 1;
      judgeRefusal(m, ScreenName(s));
    }
    size_t selections = 0;
    const unsigned all = f.devices.size() >= 32 ? 0u : (1u << f.devices.size());
    for (unsigned bits = 0; bits < all; ++bits) {
      for (int only = 0; only < 2; ++only) {
        Selection sel;
        sel.devices = bits;
        sel.onlyDevice = only == 1;
        sel.preset = false;
        const ScreenModel m = BuildScreen(Screen::Remove, RunMode::Fabricated, sel);
        ++selections;
        const std::wstring what =
            std::format(L"remove with selection {}{}", bits, only ? L" (only)" : L"");
        if (m.excludedFromCapture != ShowsOrTakesWords(m) && firstWrong == L"(none)")
          firstWrong = what;
        judgeRefusal(m, what);
      }
    }
    const bool ok = firstWrong == L"(none)" && nExcluded > 0 && refusalsRight == refusals;
    lines.push_back(std::format(
        L"  identity capture : {}  {} screens excluded from capture, exactly those whose model "
        L"shows or takes the words: {}; the other {} are not: {}; and the Remove dialog over all "
        L"{} selections of the fixture's devices (first wrong: {}); the refusal drawn in place of "
        L"each that asks, when the window cannot be kept out of captures: {}/{} show no words, "
        L"take none, ask for nothing, keep the banner and the first title, say \"{}\" and offer "
        L"Close alone (first wrong: {})   [query: excludedFromCapture == (the model has a Words or "
        L"a WordFields block), for every screen and selection; CaptureRefusal of every model that "
        L"asks. The window asks for WDA_EXCLUDEFROMCAPTURE before it draws such a screen and draws "
        L"the refusal when it is not held; the shots assert that the capture is black, and the "
        L"--demo-capture=refused leg that the refusal is drawn]",
        ok ? L"PASS" : L"FAIL", nExcluded, excluded, nNot, notExcluded, selections, firstWrong,
        refusalsRight, refusals, kCaptureRefusedLine, refusalWrong));
  }

  // 4. THE CHECK: a wrong word is named, and nothing is corrected (SCOPE A1).
  {
    std::vector<std::wstring> typed = f.wordsSetUp;
    const int allRight = FirstWrongWord(typed, f.wordsSetUp);
    typed[kWrongIndex] = kWrongWord;
    const int typo = FirstWrongWord(typed, f.wordsSetUp);
    typed = f.wordsSetUp;
    typed[2].clear();
    const int empty = FirstWrongWord(typed, f.wordsSetUp);
    typed = f.wordsSetUp;
    typed.pop_back();
    const int short24 = FirstWrongWord(typed, f.wordsSetUp);
    typed = f.wordsSetUp;
    typed[0] = L"  Lighthouse ";
    const int spaced = FirstWrongWord(typed, f.wordsSetUp);
    typed = f.wordsSetUp;
    typed[0] = L"lighthous";
    const int oneLetter = FirstWrongWord(typed, f.wordsSetUp);
    const std::wstring sentence = WordCheckLine(RunMode::Fabricated, typo, false);
    // THE VERDICT, and no fields at all is not a match: FirstWrongWord over none answers -1, and a
    // Check queued before a Cancel lands on a view with no fields (WordsAccepted says why).
    const int none = FirstWrongWord({}, {});
    const bool acceptsAll = WordsAccepted(f.wordsSetUp.size(), allRight);
    const bool acceptsTypo = WordsAccepted(f.wordsSetUp.size(), typo);
    const bool acceptsNone = WordsAccepted(0, none);
    const bool ok = allRight == -1 && typo == kWrongIndex && empty == 2 && short24 == 24 &&
                    spaced == -1 && oneLetter == 0 && NamesDemo(sentence) &&
                    sentence.find(L"word 7") != std::wstring::npos &&
                    sentence.find(f.wordsSetUp[kWrongIndex]) == std::wstring::npos && acceptsAll &&
                    !acceptsTypo && !acceptsNone;
    lines.push_back(std::format(
        L"  identity check   : {}  a wrong word is named and nothing is corrected: 25 right -> {}; "
        L"\"{}\" at word 7 -> {}; word 3 empty -> {}; 24 of 25 -> {}; \"  Lighthouse \" at word 1 "
        L"-> {} (letter case and spaces are not errors); one letter short -> {}; the sentence: "
        L"\"{}\", which names the word's place and never the word; the verdict: 25 right -> {}, "
        L"one wrong -> {}, no fields at all (FirstWrongWord answers {}) -> {}   [query: "
        L"FirstWrongWord, the 0-based index of the first field that is not its word, -1 when all "
        L"match; the sentence must not contain the right word; WordsAccepted opens the next "
        L"screen only over fields that were there]",
        ok ? L"PASS" : L"FAIL", allRight, kWrongWord, typo, empty, short24, spaced, oneLetter,
        AsciiOnly(sentence), acceptsAll ? L"accepted" : L"NOT accepted",
        acceptsTypo ? L"ACCEPTED" : L"not accepted", none,
        acceptsNone ? L"ACCEPTED" : L"not accepted"));
  }

  // 5. THE PHRASE FIELD'S RULE, over every selection of the fixture's devices.
  {
    std::wstring table;
    size_t rows = 0, right = 0;
    const size_t self = ThisDeviceIndex(f);
    const unsigned all = f.devices.size() >= 32 ? 0u : (1u << f.devices.size());
    for (unsigned bits = 0; bits < all; ++bits) {
      Selection sel;
      sel.devices = bits;
      sel.preset = false;
      // the expectation, written from SCOPE G2 and G3 and not from PhraseAsked
      bool expect = false;
      for (size_t i = 0; i < f.devices.size(); ++i) {
        if (i == self || !Has(sel, i)) continue;
        if (f.devices[i].phraseDevice || !f.devices[self].phraseDevice) expect = true;
      }
      const bool got = PhraseAsked(f, sel);
      ++rows;
      right += got == expect ? 1 : 0;
      std::wstring names;
      for (size_t i = 0; i < f.devices.size(); ++i) {
        if (!Has(sel, i)) continue;
        if (!names.empty()) names += L"+";
        names += f.devices[i].name;
      }
      if (!table.empty()) table += L"; ";
      table += std::format(L"{} -> {}", names.empty() ? std::wstring(L"nothing") : names,
                           got ? L"asks" : L"no");
    }
    Selection lock;
    lock.lockOut = true;
    const bool lockAsks = PhraseAsked(f, lock);
    lines.push_back(std::format(
        L"  identity phrase  : {}  the Remove dialog asks for the phrase by SCOPE G2 and G3 over "
        L"{}/{} selections ({}), and the lock-out with no device named asks too: {}   [query: asked "
        L"when a selected device other than this one is a phrase device (a lock-out), or when this "
        L"device is not a phrase device and removes another; not when a phrase device removes "
        L"devices that are not, nor when a device removes itself]",
        right == rows && lockAsks ? L"PASS" : L"FAIL", right, rows, AsciiOnly(table),
        lockAsks ? L"yes" : L"NO"));
  }

  // 6. THE FIXTURE: the ID's shape, the device IDs' shape, one this device, every world device
  // covered, and the code's length.
  {
    size_t shaped = 0, self = 0, phrase = 0;
    std::wstring names;
    for (FixtureDevice const& d : f.devices) {
      shaped += IsDeviceIdShape(d.deviceId) ? 1 : 0;
      self += d.thisDevice ? 1 : 0;
      phrase += d.phraseDevice ? 1 : 0;
      if (!names.empty()) names += L", ";
      names += d.name + (d.thisDevice ? L" (this device)" : L"") +
               (d.phraseDevice ? L" (phrase device)" : L"");
    }
    const bool ok = f.complete && IsUuidV8Shape(f.accountId) && shaped == f.devices.size() &&
                    self == 1 && phrase >= 1 && f.code.size() == 12 && f.emoji.size() == 7;
    lines.push_back(std::format(
        L"  identity fixture : {}  over the fabricated world's {} devices ({}), every one with demo "
        L"identity data: {}; the ID \"{}\" is a UUIDv8 of 36 characters: {}; device IDs of four "
        L"groups of four lowercase hex: {}/{}; this device: {}; phrase devices: {}; the code has "
        L"{} characters; {} emoji",
        ok ? L"PASS" : L"FAIL", f.devices.size(), AsciiOnly(names), f.complete ? L"yes" : L"NO",
        f.accountId, IsUuidV8Shape(f.accountId) ? L"yes" : L"NO", shaped, f.devices.size(), self,
        phrase, f.code.size(), f.emoji.size()));
  }

  // 7. LIVE MODE BUILDS NOTHING, AND NEITHER DOES A LAUNCH THAT IS NOT A FABRICATED ONE. Until the
  // live screens exist, a live launch draws none of these, and that includes a plain launch whose
  // mesh has not answered yet, or never will: ActiveRunMode() is still Fabricated there, and
  // DrawnRunModeFor (RunMode.h) is what keeps the made-up account off its empty world.
  {
    const int count = static_cast<int>(Screen::kCount);
    auto screensBuilt = [count](RunMode mode) {
      size_t n = 0;
      for (int i = 1; i < count; ++i) n += BuildScreen(static_cast<Screen>(i), mode).built ? 1 : 0;
      return n;
    };
    const size_t asked = static_cast<size_t>(count - 1);
    const size_t liveBuilt = screensBuilt(RunMode::Live);
    const bool accountLive = BuildAccount(RunMode::Live).built;
    // the launches, as the window reads them (MainWindow::IdentityRunMode): a plain launch before
    // any live world, the same after one, and --live --demo after one
    const RunMode plain = DrawnRunModeFor(false, RunMode::Fabricated);
    const RunMode plainLatched = DrawnRunModeFor(false, RunMode::Live);
    const RunMode demoLatched = DrawnRunModeFor(true, RunMode::Live);
    const size_t plainBuilt = screensBuilt(plain);
    const bool accountPlain = BuildAccount(plain).built;
    const size_t latchedBuilt = screensBuilt(plainLatched) + screensBuilt(demoLatched);
    const bool accountLatched = BuildAccount(plainLatched).built || BuildAccount(demoLatched).built;
    // the control: a fabricated launch before any live world (--demo) builds every overlay and the
    // Account group, so the zeros above are the mode's doing and not an empty table's
    const RunMode demo = DrawnRunModeFor(true, RunMode::Fabricated);
    const size_t demoBuilt = screensBuilt(demo);
    const bool accountDemo = BuildAccount(demo).built;
    const bool ok = liveBuilt == 0 && !accountLive && plainBuilt == 0 && !accountPlain &&
                    latchedBuilt == 0 && !accountLatched && demoBuilt + 1 == asked && accountDemo;
    lines.push_back(std::format(
        L"  identity live    : {}  identity screens built in live mode: {}/{}, and the Account "
        L"group: {}; a plain launch (no --demo) whose mesh has not answered, ActiveRunMode() still "
        L"Fabricated: {}/{}, and the Account group: {}; after a live world lands, plain or --live "
        L"--demo: {} screens, and the Account group: {} (control: --demo, nothing latched, builds "
        L"{}/{} overlays, Account being Settings', and the Account group: {}); the live screens "
        L"are BuildLiveScreen and BuildLiveAccount over the library's identity (Task 16; Tasks "
        L"28, 38 and 45 to come), so no made-up screen or ACCOUNT group reaches any launch but a "
        L"fabricated one   [query: BuildScreen and BuildAccount in DrawnRunModeFor(fabricated "
        L"launch, ActiveRunMode()) for each pair]",
        ok ? L"PASS" : L"FAIL", liveBuilt, asked, accountLive ? L"BUILT (wrong)" : L"not built",
        plainBuilt, asked, accountPlain ? L"BUILT (wrong)" : L"not built", latchedBuilt,
        accountLatched ? L"BUILT (wrong)" : L"not built", demoBuilt, asked - 1,
        accountDemo ? L"built" : L"NOT BUILT"));
  }
  return lines;
}

}  // namespace urmsg::demo::identity
