// THE IDENTITY, READ AND HANDED OVER (identity phase 1, slice 1: PLAN Task 16).
//
// WHAT THIS IS. The pure half of the app's first identity: what the library's identity json says,
// how the 25 words cross from the library to the screen and back, which answer of
// urnet_message_device_confirm_words lets the setup go on, and which folder this slice's store
// opens. The live worker (LiveMesh.cpp) calls the library and the window draws the screens
// (Demo/IdentityDemo.h's BuildLiveScreen and BuildLiveAccount); every decision between the two is
// here, so --diagnose holds it without a window and without the library.
//
// THE WORDS ARE THE ACCOUNT (urnetwork_message.h, urnet_message_device_create_identity). They are
// never logged, never put on the clipboard and never stored, and every buffer this app owns them or
// the typed phrase in - the strings the worker and the window hold, the screens' models and their
// inputs - is overwritten (Wipe) before it is freed. What this app cannot overwrite is the text the
// window's own controls hold: XAML's copies, and the string a control hands back when it is read.
// That is a stated residual, as Go's memory is the library's (PLAN's promise table, "Your phrase is
// never stored").
//
// PURE C++, NO winrt, NO XAML: App.vcxproj compiles LiveIdentity.cpp with PrecompiledHeader
// NotUsing, because CollectDiagnostics runs LiveIdentityDiagnostics before winrt::init_apartment.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace urmsg::live {

// ---- the store -------------------------------------------------------------------------------

// THIS SLICE'S STORE FOLDER, under StorageRoot()/live (PLAN Task 16; P6; REDTEAM M1). Build 03 kept
// its stream and state directly under live/, and identity phase 1 reads nothing of it: no build-03
// group, and no store an earlier slice's build wrote, is ever opened by this one. So this build's
// stream/ and state/ sit in a folder named for the slice, and Tasks 28, 38 and 45 name their own.
// Build 03's folder is kept, and the read-me says how to delete it by hand (P6, taken).
inline constexpr wchar_t kSliceStoreFolder[] = L"identity-slice1";

// The folder this build's stream/ and state/ are made in, given StorageRoot()/live.
std::filesystem::path SliceStoreFolder(std::filesystem::path const& liveRoot);

// ---- the identity json ------------------------------------------------------------------------

// One device of the newest list, as urnet_message_device_identity answers it.
struct IdentityDevice {
  std::string deviceId;       // 19 characters: four groups of four lower-case hex, by hyphens
  bool phraseDevice = false;  // it holds the recovery phrase's key
  bool thisDevice = false;
};

// urnet_message_device_identity's json, read. `read` is false, with `problem` saying why, when the
// text is not that json: not json, not an object, or a key the header documents missing or of the
// wrong type. A first run reads as read && !hasIdentity.
struct IdentityInfo {
  bool read = false;
  std::string problem;
  bool hasIdentity = false;
  std::string accountId;      // "Your ID": 36 characters, the same on every device (SCOPE B1)
  std::string deviceId;       // this device's own, 19 characters (SCOPE B2)
  bool phraseDevice = false;
  std::vector<IdentityDevice> devices;
};

IdentityInfo ParseIdentityJson(std::string_view json);

// The shapes SCOPE B1 and B2 give the two IDs, as the library writes them (messagegroup's
// AccountId.String and DeviceId.String): a UUIDv8 of 36 characters, 8-4-4-4-12 lower-case hex with
// the version nibble 8 and the variant bits 10; and four groups of four lower-case hex joined by
// hyphens, 19 characters, so that nobody takes one for the other.
bool IsAccountIdShape(std::string_view id);
bool IsDeviceIdShape(std::string_view id);

// ---- the words, both ways ---------------------------------------------------------------------

inline constexpr size_t kPhraseWords = 25;

// THE WORDS OFFERED, split for the screen: the library's octets are lower-case UTF-8 words joined
// by single spaces, with no NUL. Exactly 25 words of ASCII letters, or nothing: a buffer of any
// other shape is not drawn (the caller says it could not be read), so the screen never shows a
// guess at a phrase.
std::vector<std::wstring> SplitOfferedWords(std::string_view utf8);

// EACH BOX HOLDS EXACTLY ONE WORD BEFORE ANYTHING IS SENT (slice 1's review, X-APP-1; LRI R84).
// The library reads white space as the separator and keeps no empty position, so an empty box, or
// a box of two words, would shift every later word onto the wrong number and name right words as
// wrong. So the app names such a box itself - "Word 7 is empty", "Word 7 holds two words" - and
// sends nothing until every box holds one word. It needs no word to know this: only where a box's
// white space is.
// THE LIST'S OWN CHECK HAS THE LAST WORD on a box of one word. The library's fold is NFKD first,
// and some characters hold white space only once decomposed: a spacing diaeresis (U+00A8) is a
// space and a mark there. A box ending in one is one word to the app and two to the library, and
// every later word would shift again. urnet_message_device_check_word answers true only for
// exactly one word of the list in the library's own fold, so a box it refuses is named too ("Word 7
// is not a word of the list"), and what is sent is 25 words of the list, one to a box. Then the
// library's positions are the boxes'.
struct BoxIssue {
  int position = 0;      // the box, from 1
  size_t words = 0;      // by white space. 0: the box is empty; 2 or more: the words it holds
  bool offList = false;  // one word by white space, which the list's own check refuses
};
// `isListWord` is the list's own check (urnet_message_device_check_word), asked of every box that
// holds one word by white space, and of no other.
std::vector<BoxIssue> CheckBoxes(std::vector<std::wstring> const& fields,
                                 std::function<bool(std::wstring const&)> const& isListWord);
// The sentence over the boxes for those issues; empty when there is none.
std::wstring BoxesLine(std::vector<BoxIssue> const& issues);

// THE WORDS TYPED BACK, as urnet_message_device_confirm_words reads them: the boxes in order, each
// trimmed of the white space around it, joined by single spaces. Called only once CheckBoxes found
// no issue, so each box is one word of the list and the library's positions are the boxes'. Nothing is
// corrected or completed here: case is the library's own fold (urnetwork_message.h).
std::string JoinTypedWords(std::vector<std::wstring> const& fields);
// One word, as the list's check reads it (urnet_message_device_check_word): no copy of it but the
// one returned.
std::string OneWordUtf8(std::wstring const& word);

// PROPERTY 2's ONE GATE IN THE APP (PLAN Task 16): the identity exists only after all 25 words
// match, and the library says so with URNET_MESSAGE_WORDS_OK. Every other kind leaves the screen
// where it is: WRONG names the words, however few (24 of 25 is a wrong phrase), and the rest are a
// sentence. `namedPositions` is how many words the answer named.
bool ConfirmAccepted(int32_t kind, size_t namedPositions);

// The positions WRONG handed back, as the screen marks them: from 1 to 25, in order, each once. A
// position outside 1..25 is the library's mistake and is dropped from the marks, never guessed.
std::vector<int> NamedPositions(std::vector<int32_t> const& positions, int32_t count);

// The sentence over the fields for a WRONG answer: every named word, by its number, and that
// nothing was corrected.
std::wstring WrongWordsLine(std::vector<int> const& positions);

// Overwrite a buffer that held the words before it is freed.
void Wipe(std::string& s);
void Wipe(std::wstring& s);
void Wipe(std::vector<std::wstring>& words);

// THE FIRST SCREEN'S ROAD (R89 item 1). urnet_message_device_identity_state answers which of four
// states this device's identity is in, and it is asked before setup is offered: an identity whose
// identity record was moved away (an antivirus quarantine, a sync tool) is UNREADABLE, and setting
// up over it would replace it. Setup is offered for exactly one answer, NONE with a json that read
// and holds no identity. Anything else that is not MADE with an identity is the Unreadable step:
// UNREADABLE, FAILED (which a recovered panic answers), a value this build does not know, and a
// state and a json that disagree. Never setup on a doubt.
enum class IdentityRoad { SetUp, Made, Unreadable };
IdentityRoad RoadFor(bool jsonRead, bool hasIdentity, int32_t state);
// The state's name, for the log line.
const char* IdentityStateName(int32_t state);

// WHAT A LEAVE ANSWERED (R89 item 5; urnet_message_device_forget_group's KINDS). UNKNOWN (a
// recovered panic inside the leave) is never "left": the group may still be held, so its handles
// are kept and the device's groups are read again, and that answer decides (AfterUnknownLeave).
enum class ForgetOutcome { Left, LeftUnfinished, NotHeld, Refused, Unknown };
ForgetOutcome ForgetOutcomeOf(int32_t kind);
// After an UNKNOWN: still held is not left; no longer held is as good as left.
ForgetOutcome AfterUnknownLeave(bool stillHeld);

// A WORD'S CELL NAME ON THE WORDS SCREEN ("Word 7: <word>", what UI Automation reads), written into
// `out`, which the caller reserved and wipes once the name is set (R89, X2-3). Nothing is built in
// a temporary: a buffer that grew or was swapped in would be let go unwiped.
void WriteWordCellName(size_t position, std::wstring const& word, std::wstring& out);

// THE REMOTE-SESSION WARNING (F3 section 2.11.3; Spec C section 6.2's sentence; REDTEAM L4), shown
// on every words screen while GetSystemMetrics(SM_REMOTESESSION) is non-zero.
inline constexpr wchar_t kRemoteSessionWarning[] =
    L"You are on a remote session. These words are travelling over that connection, and "
    L"screenshots, screen sharing and Windows Recall on the far end can see them. Consider doing "
    L"this on the machine itself.";

// --diagnose: the json, the words both ways, the gate, the positions, the store folder and the
// roster by identity. Pure; printed PASS/FAIL with the query and the complement.
std::vector<std::wstring> LiveIdentityDiagnostics();

}  // namespace urmsg::live
