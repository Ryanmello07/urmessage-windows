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
// never logged, never put on the clipboard, never stored, and every buffer this app owns them in is
// overwritten (Wipe) as soon as they are off the screen. The library erases its own copy and
// cannot erase this app's. What this app cannot erase is the text the window's own controls copy:
// a stated residual, as Go's memory is the library's (PLAN's promise table, "Your phrase is never
// stored").
//
// PURE C++, NO winrt, NO XAML: App.vcxproj compiles LiveIdentity.cpp with PrecompiledHeader
// NotUsing, because CollectDiagnostics runs LiveIdentityDiagnostics before winrt::init_apartment.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

// THE WORDS TYPED BACK, as urnet_message_device_confirm_words reads them: the fields in order,
// each trimmed of the spaces around it, joined by single spaces. A field left empty stays a
// position (two spaces in a row), so the library names that position rather than shifting every
// word after it onto the wrong number. Nothing is corrected or completed here: case and spacing are
// the library's own fold (urnetwork_message.h).
std::string JoinTypedWords(std::vector<std::wstring> const& fields);

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
