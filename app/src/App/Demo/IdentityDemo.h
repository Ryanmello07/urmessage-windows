// THE IDENTITY SCREENS OVER THE FABRICATED WORLD (identity phase 1, Task 6: slice 0).
//
// WHAT THIS IS. Every screen SCOPE H1 to H6 and K1 to K9 name, as a MODEL: the blocks a screen shows,
// in order, each string with its KIND, and whether the screen is excluded from capture. The window
// draws a model and nothing else (Views/IdentityView.h); Settings' Account group draws the fixture
// below (Views/SettingsView.cpp). Being pure is what lets --diagnose hold Property 1 over EVERY
// screen without a window: IdentityDemoDiagnostics builds each one and reads its strings.
//
// WHAT IT IS NOT. A live screen. In this build no identity screen is built in live mode at all:
// BuildScreen answers an empty model there, and Settings keeps its inert ACCOUNT group in live mode.
// A launch that is not a fabricated one asks for live mode here even before a live world lands
// (RunMode.h's DrawnRunMode, which MainWindow::IdentityRunMode answers with), so a plain launch
// never draws these screens over its empty world.
// The live screens are Tasks 16, 28, 38 and 45, over the library's identity; the copy SCOPE gives
// them is in RunMode.h as the live arms of the identity pairs, so the two arms are held apart from
// the first commit that has either.
//
// THE RULE (RunMode.h:1-60, PLAN Task 6 Property 1). No fabricated screen shows a string that the
// live screen would show as a fact. Every string here is one of three kinds:
//   Fact      true or false of an account, a device, a phrase or a session: an ID, a word, an emoji,
//             a date, a removal, an ending. It must NAME THE DEMO ("demo", case-insensitive).
//   DemoOnly  a control the live screen will not have ("Demo only: fill in the demo words"). It must
//             name the demo too, so nobody reads it as a feature.
//   Chrome    an instruction or a button that is the same in both modes ("Set up this device",
//             "They match"). It claims nothing about an account. --diagnose PRINTS every chrome
//             string, so the classification can be read rather than trusted.
// The live arms never name the demo (RunModeCopyDiagnostics' identity clause), so no string can be
// both a fabricated fact and a live one.
//
// THE WORDS ARE NOT A PHRASE. Two fixed demo lists of 25 words, each word 9 letters or more. The
// BIP-39 English list that SCOPE A1 takes has only words of 3 to 8 letters, so no demo word is on it
// and no demo list is a recovery phrase of any account: the library's per-word check would name
// every one. That is a property of the lists, and --diagnose asserts it, so this task needs neither
// the library's list nor its checksum (PLAN Task 6; REDTEAM L5).
//
// CAPTURE (Property 2). A screen that shows the words, or takes them, is excluded from capture:
// MainWindow asks for WDA_EXCLUDEFROMCAPTURE while it is up, and PrintWindow then answers a black
// window (measured on windows-2022 and windows-2025, ci/i1-affinity-probe run 38894632625: 100% of
// pixels exactly black, one colour, against 2.8% without it). `excludedFromCapture` below is the
// screen's declared policy; --diagnose holds it to the blocks (every screen with a words or a word
// fields block, and no other), and the shots hold the WINDOW to it. The window asks BEFORE it draws
// such a screen and draws CaptureRefusal's model instead when the answer is no; it gives the
// exclusion back only after frames without the words have been drawn.
//
// PURE C++, NO winrt, NO XAML, and App.vcxproj compiles IdentityDemo.cpp with
// PrecompiledHeader=NotUsing: CollectDiagnostics runs IdentityDemoDiagnostics before
// winrt::init_apartment, the same as RunModeCopyDiagnostics.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Live/LiveIdentity.h"
#include "RunMode.h"

namespace urmsg::demo::identity {

// Every identity state a shot, a click or --demo-identity=<name> can open. Account is not an
// overlay: it is Settings, whose ACCOUNT group the fabricated world fills.
enum class Screen : int {
  None = 0,
  SetUp,            // "Set up this device": the code, or "This is my first device" (SCOPE E1)
  Words,            // the 25 demo words, once (H2)                                  [no capture]
  TypeBack,         // typing them back (H2)                                         [no capture]
  TypeBackWrong,    // ...with a wrong word named, nothing corrected (A1)            [no capture]
  SetUpDone,        // the first device, set up
  EmojiNew,         // the new device's emoji, after its code was typed (E3, E4)
  AddDevice,        // Settings -> Account -> Add a device: type the code (E3)
  EmojiExisting,    // the existing device's emoji (E4)
  Granted,          // both said "They match" (E5)
  EndingTimedOut,   // the five endings, one screen (E6)
  EndingCancelled,
  EndingNoMatch,
  EndingRemoveFirst,
  EndingPhraseDevice,
  Remove,           // the Remove dialog: another device, and this device keeps the phrase's key (G2)
  RemovePhrase,     // ...removing a phrase device: the lock-out, with the phrase (G3)  [no capture]
  RemoveOnly,       // ...on an only device: "This ends this identity" (G1)
  LockOut,          // the lock-out that names no device, which a self-removal offers (G3, G6)
                    //                                                               [no capture]
  Removed,          // "This device is no longer on your account" (G5)
  NoticeAdded,      // the notices (G6)
  NoticeRemoved,
  NoticeSelfRemoved,
  AlarmList,        // the alarm's two reasons (G6), and the rival of an earlier key (R61)
  AlarmKey,
  AlarmRival,
  Rename,           // a device's name (H4)
  MakePhrase,       // "Make this a phrase device" (G4, AD5)                         [no capture]
  ChangeOld,        // "Change my recovery phrase": the old words (K1)               [no capture]
  ChangeNew,        // ...the new words, once (K1)                                   [no capture]
  ChangeTypeBack,   // ...typed back (K1)                                            [no capture]
  ChangeDone,       // ...done (PLAN Task 45's done line)
  Account,          // Settings -> Account: "Your ID" and Devices (H4)
  kCount
};

// The switch spelling (--demo-identity=<name>) and back, one table. ScreenFromName answers
// Screen::None for a name it does not know: a typo must never light up the demo (DemoSwitches.cpp's
// carve-out). constexpr, so DemoSwitches.cpp's parse table can assert the switch at compile time.
struct NamedScreen {
  Screen screen;
  wchar_t const* name;
};

inline constexpr NamedScreen kScreenNames[] = {
    {Screen::SetUp, L"setup"},
    {Screen::Words, L"words"},
    {Screen::TypeBack, L"typeback"},
    {Screen::TypeBackWrong, L"typeback-wrong"},
    {Screen::SetUpDone, L"setup-done"},
    {Screen::EmojiNew, L"emoji-new"},
    {Screen::AddDevice, L"add"},
    {Screen::EmojiExisting, L"emoji-existing"},
    {Screen::Granted, L"granted"},
    {Screen::EndingTimedOut, L"ending-timeout"},
    {Screen::EndingCancelled, L"ending-cancelled"},
    {Screen::EndingNoMatch, L"ending-nomatch"},
    {Screen::EndingRemoveFirst, L"ending-full"},
    {Screen::EndingPhraseDevice, L"ending-phrase"},
    {Screen::Remove, L"remove"},
    {Screen::RemovePhrase, L"remove-phrase"},
    {Screen::RemoveOnly, L"remove-only"},
    {Screen::LockOut, L"lockout"},
    {Screen::Removed, L"removed"},
    {Screen::NoticeAdded, L"notice-added"},
    {Screen::NoticeRemoved, L"notice-removed"},
    {Screen::NoticeSelfRemoved, L"notice-selfremoved"},
    {Screen::AlarmList, L"alarm-list"},
    {Screen::AlarmKey, L"alarm-key"},
    {Screen::AlarmRival, L"alarm-rival"},
    {Screen::Rename, L"rename"},
    {Screen::MakePhrase, L"make-phrase"},
    {Screen::ChangeOld, L"change-old"},
    {Screen::ChangeNew, L"change-new"},
    {Screen::ChangeTypeBack, L"change-typeback"},
    {Screen::ChangeDone, L"change-done"},
    {Screen::Account, L"account"},
};
static_assert(sizeof(kScreenNames) / sizeof(kScreenNames[0]) ==
                  static_cast<size_t>(Screen::kCount) - 1,
              "every Screen but None has exactly one switch name");

constexpr wchar_t const* ScreenName(Screen s) {
  for (auto const& n : kScreenNames)
    if (n.screen == s) return n.name;
  return L"none";
}

constexpr Screen ScreenFromName(std::wstring_view name) {
  for (auto const& n : kScreenNames)
    if (name == n.name) return n.screen;
  return Screen::None;
}

// THE IDENTITY PREVIEW BUILD (Task 6's zip): Directory.Build.props defines URM_IDENTITY_PREVIEW=1
// for every project when the build is given /p:UrmIdentityPreview=true. In that build a plain
// launch opens the fabricated world on "Set up this device" (DemoSwitches.cpp), and nothing is ever
// live (LiveMesh.cpp's IsEnabled).
#if defined(URM_IDENTITY_PREVIEW) && URM_IDENTITY_PREVIEW
inline constexpr bool kIdentityPreviewBuild = true;
#else
inline constexpr bool kIdentityPreviewBuild = false;
#endif

// WHICH MODE THE IDENTITY SCREENS ARE BUILT IN, AND IT IS NOT ActiveRunMode() ALONE. That latch
// says Fabricated until a live world lands (RunMode.h), and a plain launch of the standard build is
// live: it draws the EMPTY world while the mesh dials, and for the whole session when it never
// answers (no credential, no join code, offline). Read alone, the latch would put the made-up
// account and every made-up screen over that empty world. So the fabricated screens need both a
// FABRICATED LAUNCH (--demo, any --demo-* switch, or the preview build's plain launch: the window's
// DemoOptions::enabled) and a latch that has not flipped - which is RunMode.h's DrawnRunMode, the
// rule every other string in the app now takes too. MainWindow::IdentityRunMode answers with it.
// Anything else asks for the live screens, which this build does not have (Tasks 16, 28, 38 and
// 45): BuildScreen and BuildAccount build nothing. --diagnose's identity live line reads every
// pair through DrawnRunModeFor.

enum class TextKind { Fact, Chrome, DemoOnly };
enum class TextStyle { Body, Strong, Muted, Error };

// What a button does. The window carries it out (MainWindow::OnIdentityAction).
enum class Action : int {
  Go,            // show `arg`, a Screen
  Close,         // close the identity screens, back to the app
  OpenAccount,   // close them and open Settings, whose Account group is at the top
  FillWords,     // demo only: fill the word fields with the demo list they are checked against
  CheckWords,    // check the word fields; all match -> show `arg`, else name the first wrong word
  ToggleDevice,  // the Remove dialog: `arg` is the device's index in the fixture
  RemoveChosen,  // the Remove dialog's Remove
  SaveName,      // the Rename dialog's Save
  // THE LIVE SETUP'S OWN (Task 16). The window hands each to the live worker, which answers with a
  // published state; nothing here makes or checks anything itself.
  MakeWords,     // "This is my first device", and "Back to the words": ask the library for them
  ConfirmWords,  // the type-back's Check: the fields, joined, to urnet_message_device_confirm_words
};

struct Button {
  Action action = Action::Close;
  int arg = 0;
  std::wstring label;
  TextKind kind = TextKind::Chrome;
  bool primary = false;
  int device = -1;  // Go(Remove) and Go(Rename) from a device's row: that device
};

struct EmojiItem {
  std::wstring glyph;  // UTF-16, written as escapes in the .cpp (the house non-ASCII rule)
  std::wstring name;   // Matrix's English name for it
};

enum class BlockType {
  Banner,        // the demo framing, first on every fabricated screen
  Title,
  Text,
  Code,          // `text` is the code; `label` frames it
  Words,         // `words` shown in a 5 x 5 grid, numbered, column-major; `label` frames them
  WordFields,    // 25 fields, checked against `words`; `prefill`, `wrongIndex`; `label` frames them
  Emoji,         // `emoji`; `label` frames them
  Devices,       // `devices` as a checklist (the Remove dialog); `label` frames them
  CodeField,     // a field for a code; `label`
  NameField,     // a field holding `text`; `label`
  KeepOrRemove,  // `devices`: keep or remove each, after a lock-out; `label`
  Buttons,
  Divider,       // a rule between two parts of one screen; no text
};

struct DeviceLine {
  int index = 0;            // in Fixture::devices
  std::wstring name;
  std::wstring detail;      // a Fact: "Demo device ID ... - phrase device"
  bool selected = false;
};

// HOW A WORD FIELD TAKES A WORD (F3 section 2.11.3; PLAN Task 16). The view builds each field from
// this and from nothing else, so --diagnose's field case reads what the window draws.
struct FieldPolicy {
  bool maskWhenFocusLeaves = false;  // the word is hidden once focus leaves its field
  bool revealToggle = false;         // one switch on the screen shows every word again
  bool spellCheck = true;
  bool prediction = true;
  bool passwordScope = false;        // the touch keyboard's password layout
  bool copyAllowed = true;           // false: a field's word can be neither copied nor cut, so no
                                     // word reaches the clipboard or its history
};
// The fabricated screens' fields, as slice 0 drew them (made-up words).
inline constexpr FieldPolicy kDemoWordFieldPolicy{false, false, false, false, true, true};
// The live screens' fields: every protection F3 lists for a screen that takes the words.
inline constexpr FieldPolicy kLiveWordFieldPolicy{true, true, false, false, true, false};

struct Block {
  BlockType type = BlockType::Text;
  TextKind kind = TextKind::Chrome;  // of `text` (Banner, Title, Text, Code)
  TextStyle style = TextStyle::Body;
  std::wstring text;
  std::wstring label;                // Words, WordFields, Emoji, Devices, CodeField, NameField, Code,
                                     // KeepOrRemove: always a Fact or Chrome line above the block
  TextKind labelKind = TextKind::Fact;
  std::vector<std::wstring> words;
  std::vector<std::wstring> prefill;
  int wrongIndex = -1;
  std::vector<int> wrongPositions;   // WordFields, live: every box marked, from 1
  std::wstring markNote;             // WordFields, live: what a marked box's help text says
  FieldPolicy fields = kDemoWordFieldPolicy;  // WordFields
  std::vector<EmojiItem> emoji;
  std::vector<DeviceLine> devices;
  std::vector<Button> buttons;
};

struct ScreenModel {
  Screen screen = Screen::None;
  bool built = false;               // false: no such screen in this mode (every live one, in Task 6)
  bool excludedFromCapture = false;  // the declared policy; --diagnose holds it to the blocks
  bool dialog = false;              // drawn as a smaller card (the Remove and Rename dialogs)
  bool alarm = false;               // drawn with the danger colour (the alarm, SCOPE G6)
  std::vector<Block> blocks;
};

// ---- the made-up account, over the fabricated world --------------------------------------------

struct FixtureDevice {
  std::wstring worldId;   // demo::DeviceRef::id
  std::wstring name;      // demo::DeviceRef::name
  std::wstring deviceId;  // four groups of four lowercase hex (SCOPE B2)
  std::wstring added;     // "set up on 2 Oct 2026, the first device" / "added on ... by ..."
  bool thisDevice = false;
  bool phraseDevice = false;
};

struct Fixture {
  std::wstring accountId;            // a UUIDv8, 36 characters (SCOPE B1)
  std::wstring code;                 // 12 characters (SCOPE E1)
  std::vector<FixtureDevice> devices;
  std::vector<std::wstring> wordsSetUp;    // 25, the setup's demo list
  std::vector<std::wstring> wordsChanged;  // 25, the phrase change's demo list
  std::vector<EmojiItem> emoji;            // 7
  std::wstring otherPhraseDevice;    // the phrase device that is not this one
  std::wstring removedOn, removedBy;
  std::wstring noticeAdded, noticeAddedOn, noticeAddedBy;
  std::wstring noticeRemoved, noticeRemovedOn, noticeRemovedBy;
  std::wstring selfRemoved;
  bool complete = false;             // every world device has its demo identity data
};

// Built once, over demo::GetWorld().myDevices: only a fabricated launch, or --diagnose, calls it.
Fixture const& GetFixture();

// The Remove dialog's selection: a bit per Fixture::devices index. `onlyDevice` draws the fixture
// as though this device were the account's only one (RemoveOnly); `lockOut` is the lock-out that
// names no device (LockOut); `device` is the device the Rename dialog renames.
struct Selection {
  unsigned devices = 0;
  bool onlyDevice = false;
  bool lockOut = false;
  int device = -1;
  bool preset = true;  // false once the person has ticked a box: the bits are theirs then
};

// A demo device's name, as renamed in this session (Rename's Save), else the world's. Kept in
// memory only, and the Rename dialog says so.
std::wstring DemoDeviceName(size_t index);
void SetDemoDeviceName(size_t index, std::wstring name);

// ---- Settings -> Account (SCOPE H4), over the same fixture ------------------------------------

struct AccountDevice {
  int index = 0;
  std::wstring name;    // the world's name (or this session's rename); framed by the caption
  std::wstring detail;  // Fact: "Demo device ID ... - this device - phrase device"
  std::wstring added;   // Fact: "Demo: added on ... by ..."
  bool thisDevice = false;
  bool phraseDevice = false;
  std::vector<Button> buttons;  // Rename, Remove, and Make this a phrase device where it applies
};

struct AccountModel {
  bool built = false;           // false in live mode: Settings keeps its inert ACCOUNT group
  std::wstring caption;         // Fact
  std::wstring idTitle;         // Chrome: "Your ID"
  std::wstring idNote;          // Fact: RunMode.h's AccountIdNote
  std::wstring idValue;         // Fact
  std::wstring copied;          // Fact: what Copy puts on the clipboard, framing and all
  Button copy;                  // Chrome
  std::wstring changeTitle;     // Chrome
  std::wstring changeNote;      // Fact
  Button change;                // Chrome
  std::wstring devicesCaption;  // Fact
  std::wstring devicesNote;     // Fact
  std::vector<AccountDevice> devices;
  Button add;                   // Chrome
  std::wstring moreCaption;     // DemoOnly
  std::vector<Button> more;     // DemoOnly: every overlay screen a click cannot reach from here
};

AccountModel BuildAccount(RunMode mode);

// The selection each Remove screen opens with.
Selection PresetSelection(Screen s);

// THE RULE OF THE PHRASE FIELD (SCOPE G2, G3). Removing a phrase device other than this one always
// asks for the phrase (the lock-out); removing any other device from a device that is not a phrase
// device asks for it too; a phrase device removing devices that are not, or a device removing
// itself, asks nothing.
bool PhraseAsked(Fixture const& f, Selection const& sel);

// The model of one screen. Every live screen is unbuilt in this build (built == false).
ScreenModel BuildScreen(Screen s, RunMode mode, Selection const& sel);
inline ScreenModel BuildScreen(Screen s, RunMode mode) {
  return BuildScreen(s, mode, PresetSelection(s));
}

// ---- the live screens (PLAN Task 16: slice 1) -------------------------------------------------
//
// SCOPE H1, H2 and H4 over the library's identity: "Set up this device" with its first-device road,
// the 25 words, typing them back with every wrong word named, the setup done, and Settings ->
// Account's "Your ID" and this device. Built only through these two functions and from what the
// library answered: BuildScreen and BuildAccount above still build nothing in live mode, so no
// made-up value can reach a live screen. The code road of "Set up this device" is slice 2's (Task
// 28): this build's screen says it cannot add this computer to an account that already exists.

struct LiveInputs {
  std::vector<std::wstring> words;   // Words: the 25 words offered (Wipe them once off screen)
  std::vector<std::wstring> typed;   // TypeBackWrong: what the fields held, kept as typed
  std::vector<int> wrong;            // TypeBackWrong: the positions the library named, from 1
  std::wstring failure;              // a sentence when the library could not make or check
  bool remoteSession = false;        // SM_REMOTESESSION: the warning, and no capture affinity
  bool waiting = false;              // a request is with the library: no second one is offered
  std::vector<urmsg::live::BoxIssue> boxes;  // TypeBack: the boxes the app named before sending
  bool stopped = false;              // the identity could not be read: the sentence, nothing to do
};

// Overwrite every word a model or its inputs hold, before they are freed (X-APP-2).
void WipeScreenWords(ScreenModel& m);
void WipeLiveInputs(LiveInputs& in);

// SetUp, Words, TypeBack, TypeBackWrong and SetUpDone; every other screen is unbuilt.
ScreenModel BuildLiveScreen(Screen s, LiveInputs const& in);

// THE SESSION KIND, READ AGAIN WHEN IT CHANGES (Spec C section 6.2; the lead's addition to LRI R84).
// The window reads SM_REMOTESESSION again on WM_WTSSESSION_CHANGE, and this answers whether the
// live screen up must be built again for what it read: exactly when it shows or takes the words
// and the session is no longer the kind it was built for. Built again by BuildLiveScreen's own
// rule, the screen asks for the exclusion at once on the machine itself, and in a remote session
// gives it back and shows the warning. `liveShown`: the screen up is a live one; `builtRemote`:
// the kind it was built for; `remoteNow`: the kind read again. The message's own event decides
// nothing: anything on the desktop can post one.
bool RebuildForSessionKind(Screen shown, bool liveShown, bool builtRemote, bool remoteNow);

// Settings -> Account over the library's identity: "Your ID" with Copy, and the newest list's
// devices with this device marked. Unbuilt when the json was not read or holds no identity.
AccountModel BuildLiveAccount(urmsg::live::IdentityInfo const& id);

// THE REFUSAL: PROPERTY 2 FAILS CLOSED. What the window draws in place of a screen that shows or
// takes the words when it cannot be kept out of captures (SetWindowDisplayAffinity said no, or the
// affinity it reads back is not one that blacks a capture out): the screen's banner and its first
// title, one sentence that says why, and Close. No words, no fields and nothing to check, so
// nothing on it needs the exclusion. --diagnose's identity capture line builds it for every such
// screen and every such Remove selection.
inline constexpr wchar_t kCaptureRefusedLine[] =
    L"This window could not be kept out of screen captures, so the words are not shown.";
ScreenModel CaptureRefusal(ScreenModel const& screen);

// THE CHECK OF THE TYPED-BACK WORDS (SCOPE A1: a wrong word is named, and nothing is guessed). The
// 0-based index of the first field that is not its word, or -1 when every one matches. Spaces
// around a word and letter case are not errors; nothing else is forgiven, and no word is ever
// suggested.
int FirstWrongWord(std::vector<std::wstring> const& typed, std::vector<std::wstring> const& expected);

// The check's verdict: every word right, AND there were words to check. Zero fields is not 25 right
// ones: a Check that a click queued lands a beat later (MainWindow::QueueIdentityAction), after a
// Cancel or a Close has emptied the view, and FirstWrongWord over no fields answers -1, which
// would open the next screen.
constexpr bool WordsAccepted(size_t fields, int firstWrong) { return fields > 0 && firstWrong < 0; }

// The sentence the word fields show for the answer above.
std::wstring WordCheckLine(RunMode mode, int firstWrong, bool empty);

// --diagnose: every screen built and read, the lists, the capture policy against the blocks, the
// check, the phrase rule, the fixture, and live mode unbuilt. Pure; printed PASS/FAIL with the
// query and the complement.
std::vector<std::wstring> IdentityDemoDiagnostics();

}  // namespace urmsg::demo::identity
