// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit NotUsing. CollectDiagnostics runs
// LiveIdentityDiagnostics before winrt::init_apartment, so nothing here may reach winrt.
//
// THE CASES OF PLAN TASK 16 (identity phase 1, slice 1), STEP 1: "the words screen's capture flag,
// the field settings, the typed-back states, the ID's text, and a roster of three leaves and two
// identities", with the json, the words both ways and the store folder that those stand on. They
// were written before the code they hold, and seen failing against stubs that answered "not built"
// (the plan's rule: tests first, each failing for its own reason). Each line prints its query, the
// complement of what it found, and PASS or FAIL.
//
// No recovery phrase is in this file: the words below are made-up lower-case letters of a shape
// the library's list would refuse, so a diagnose line can print them.
#include "Live/LiveIdentity.h"

#include <algorithm>
#include <format>
#include <iterator>

#include "Demo/DemoWorld.h"
#include "Demo/IdentityDemo.h"
#include "Live/LiveWorld.h"
#include "Live/LocalNames.h"
#include "RunMode.h"
#include "Views/StatusStripRules.h"
#include "urnetwork_message.h"

namespace urmsg::live {
namespace {

namespace id = urmsg::demo::identity;

const wchar_t* Verdict(bool ok) { return ok ? L"PASS" : L"FAIL"; }
const wchar_t* YesNo(bool b) { return b ? L"yes" : L"NO"; }

std::wstring Widen(std::string const& s) { return std::wstring(s.begin(), s.end()); }

bool ContainsCI(std::wstring const& hay, std::wstring const& needleLower) {
  if (needleLower.size() > hay.size()) return false;
  for (size_t i = 0; i + needleLower.size() <= hay.size(); ++i) {
    bool hit = true;
    for (size_t j = 0; j < needleLower.size() && hit; ++j) {
      wchar_t c = hay[i + j];
      if (L'A' <= c && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
      hit = c == needleLower[j];
    }
    if (hit) return true;
  }
  return false;
}

// Every string a model shows, as the window would draw it.
std::vector<std::wstring> StringsOf(id::ScreenModel const& m) {
  std::vector<std::wstring> out;
  for (auto const& b : m.blocks) {
    if (!b.text.empty()) out.push_back(b.text);
    if (!b.label.empty()) out.push_back(b.label);
    for (auto const& w : b.words) out.push_back(w);
    for (auto const& w : b.prefill) out.push_back(w);
    for (auto const& d : b.devices) {
      out.push_back(d.name);
      out.push_back(d.detail);
    }
    for (auto const& btn : b.buttons) out.push_back(btn.label);
  }
  return out;
}

bool HasText(id::ScreenModel const& m, std::wstring const& text) {
  for (auto const& s : StringsOf(m))
    if (s == text) return true;
  return false;
}

bool NamesDemo(id::ScreenModel const& m) {
  for (auto const& s : StringsOf(m))
    if (ContainsCI(s, L"demo")) return true;
  return false;
}

id::Block const* FieldsOf(id::ScreenModel const& m) {
  for (auto const& b : m.blocks)
    if (b.type == id::BlockType::WordFields) return &b;
  return nullptr;
}

id::Block const* WordsOf(id::ScreenModel const& m) {
  for (auto const& b : m.blocks)
    if (b.type == id::BlockType::Words) return &b;
  return nullptr;
}

bool HasAction(id::ScreenModel const& m, id::Action a) {
  for (auto const& b : m.blocks)
    for (auto const& btn : b.buttons)
      if (btn.action == a) return true;
  return false;
}

bool SamePolicy(id::FieldPolicy const& a, id::FieldPolicy const& b) {
  return a.maskWhenFocusLeaves == b.maskWhenFocusLeaves && a.revealToggle == b.revealToggle &&
         a.spellCheck == b.spellCheck && a.prediction == b.prediction &&
         a.passwordScope == b.passwordScope && a.copyAllowed == b.copyAllowed;
}

std::wstring PolicyText(id::FieldPolicy const& p) {
  return std::format(L"mask {} reveal {} spell {} prediction {} password {} copy {}",
                     p.maskWhenFocusLeaves ? 1 : 0, p.revealToggle ? 1 : 0, p.spellCheck ? 1 : 0,
                     p.prediction ? 1 : 0, p.passwordScope ? 1 : 0, p.copyAllowed ? 1 : 0);
}

// 25 made-up words, each 9 letters (none on the 3-to-8-letter list), lower-case ASCII
std::vector<std::wstring> MadeUpWords() {
  std::vector<std::wstring> w;
  for (int i = 0; i < 25; ++i) {
    std::wstring s = L"qzqzqzqz";
    s.push_back(static_cast<wchar_t>(L'a' + i));
    w.push_back(s);
  }
  return w;
}

std::string Narrow(std::vector<std::wstring> const& words) {
  std::string out;
  for (size_t i = 0; i < words.size(); ++i) {
    if (i) out.push_back(' ');
    for (wchar_t c : words[i]) out.push_back(static_cast<char>(c));
  }
  return out;
}

// The sample identity: a UUIDv8 and a device ID in the library's own shapes, distinct, so a case
// that reads one where the other belongs fails.
constexpr char kSampleAccount[] = "0f1e2d3c-4b5a-8697-a8b9-cadbecfd0e1f";
constexpr char kSampleDevice[] = "9a8b-7c6d-5e4f-3a2b";
constexpr char kOtherDevice[] = "1111-2222-3333-4444";

std::string SampleIdentityJson() {
  return std::string("{\"has_identity\":true,\"account_id\":\"") + kSampleAccount +
         "\",\"device_id\":\"" + kSampleDevice +
         "\",\"phrase_device\":true,\"devices\":[{\"device_id\":\"" + kSampleDevice +
         "\",\"phrase_device\":true,\"this_device\":true},{\"device_id\":\"" + kOtherDevice +
         "\",\"phrase_device\":false,\"this_device\":false}]}";
}

}  // namespace

std::vector<std::wstring> LiveIdentityDiagnostics() {
  std::vector<std::wstring> lines;

  // 1. THE JSON (urnetwork_message.h, urnet_message_device_identity): a first run, an identity, and
  // three that must not read - not json, a key missing, an ID out of its shape. The shapes are
  // checked both ways, so neither ID can pass as the other.
  {
    const IdentityInfo none = ParseIdentityJson(
        "{\"has_identity\":false,\"account_id\":\"\",\"device_id\":\"\",\"phrase_device\":false,"
        "\"devices\":[]}");
    const IdentityInfo some = ParseIdentityJson(SampleIdentityJson());
    const IdentityInfo notJson = ParseIdentityJson("not json");
    const IdentityInfo missing = ParseIdentityJson(
        "{\"has_identity\":true,\"device_id\":\"9a8b-7c6d-5e4f-3a2b\",\"phrase_device\":true,"
        "\"devices\":[]}");
    const IdentityInfo swapped = ParseIdentityJson(
        std::string("{\"has_identity\":true,\"account_id\":\"") + kSampleDevice +
        "\",\"device_id\":\"" + kSampleAccount + "\",\"phrase_device\":true,\"devices\":[]}");
    const bool firstRun = none.read && !none.hasIdentity;
    const bool full = some.read && some.hasIdentity && some.accountId == kSampleAccount &&
                      some.deviceId == kSampleDevice && some.phraseDevice &&
                      some.devices.size() == 2 && some.devices[0].thisDevice &&
                      some.devices[0].phraseDevice && some.devices[1].deviceId == kOtherDevice &&
                      !some.devices[1].thisDevice;
    const bool refused = !notJson.read && !missing.read && !swapped.read;
    const bool shapes = IsAccountIdShape(kSampleAccount) && IsDeviceIdShape(kSampleDevice) &&
                        !IsAccountIdShape(kSampleDevice) && !IsDeviceIdShape(kSampleAccount) &&
                        !IsAccountIdShape("0f1e2d3c-4b5a-7697-a8b9-cadbecfd0e1f");
    const bool ok = firstRun && full && refused && shapes;
    lines.push_back(std::format(
        L"  id live json     : {}  a first run reads as no identity: {}; an identity reads whole "
        L"(Your ID, this device, the phrase mark, 2 devices): {}; not json / a key missing / the IDs "
        L"swapped are not read: {} ({} | {} | {}); the two shapes, both ways: {}   [query: "
        L"ParseIdentityJson over the header's json, and IsAccountIdShape / IsDeviceIdShape]",
        Verdict(ok), YesNo(firstRun), YesNo(full), YesNo(refused), Widen(notJson.problem),
        Widen(missing.problem), Widen(swapped.problem), YesNo(shapes)));
  }

  // 2. THE WORDS, BOTH WAYS. Offered: exactly 25 lower-case words joined by single spaces, or
  // nothing - never a guess. Typed back: 25 one-word boxes, each trimmed, joined by single spaces,
  // the case left as typed (the library folds it). And the copies this app owns are overwritten:
  // a model and the inputs it was built from hold no word once wiped (X-APP-2).
  {
    const std::vector<std::wstring> words = MadeUpWords();
    const std::string offered = Narrow(words);
    const std::vector<std::wstring> split = SplitOfferedWords(offered);
    const bool splitOk = split == words;
    const bool shortRefused = SplitOfferedWords(offered.substr(0, offered.rfind(' '))).empty();
    std::string doubled = offered;
    doubled.insert(doubled.find(' '), " ");
    const bool doubleRefused = SplitOfferedWords(doubled).empty();
    std::string upper = offered;
    upper[0] = 'Q';
    const bool upperRefused = SplitOfferedWords(upper).empty();
    std::vector<std::wstring> fields = words;
    fields[0] = L"  " + fields[0] + L" ";
    fields[4] = L"QZQZqzqze";
    const std::string joined = JoinTypedWords(fields);
    std::string want = offered;
    want.replace(want.find("qzqzqzqze"), 9, "QZQZqzqze");
    const bool joinOk = joined == want && OneWordUtf8(L"qzqzqzqza") == "qzqzqzqza";
    std::wstring wiped = L"qzqzqzqza";
    Wipe(wiped);
    id::LiveInputs in;
    in.words = MadeUpWords();
    in.typed = MadeUpWords();
    in.wrong = {3};
    id::ScreenModel shown = id::BuildLiveScreen(id::Screen::TypeBackWrong, in);
    id::ScreenModel words25 = id::BuildLiveScreen(id::Screen::Words, in);
    id::WipeScreenWords(shown);
    id::WipeScreenWords(words25);
    id::WipeLiveInputs(in);
    size_t left = 0;
    for (auto const* m : {&shown, &words25})
      for (auto const& b : m->blocks) left += b.words.size() + b.prefill.size();
    left += in.words.size() + in.typed.size();
    const bool wipesOk = wiped.empty() && left == 0;
    const bool ok = splitOk && shortRefused && doubleRefused && upperRefused && joinOk && wipesOk;
    lines.push_back(std::format(
        L"  id live words    : {}  25 offered words split as offered: {}; 24 words, a double space "
        L"and a capital are not drawn: {} {} {}; 25 one-word boxes joined (trimmed, case as typed), "
        L"and one word for the list's check: {} ({} octets); a wiped buffer, two wiped models and "
        L"their wiped inputs hold no word: {} ({} left)   [query: SplitOfferedWords, JoinTypedWords, "
        L"OneWordUtf8, Wipe, WipeScreenWords and WipeLiveInputs over 25 made-up 9-letter words]",
        Verdict(ok), YesNo(splitOk), YesNo(shortRefused), YesNo(doubleRefused), YesNo(upperRefused),
        YesNo(joinOk), joined.size(), YesNo(wipesOk), left));
  }

  // 2b. EACH BOX HOLDS EXACTLY ONE WORD OF THE LIST BEFORE ANYTHING IS SENT (X-APP-1, MAJOR). The
  // library keeps no empty position, so an empty box or a box of two words would shift every later
  // word. The app names such a box itself, by its number and without the words, and a box of one
  // word that the list's own check refuses as well; the type-back screen marks them and keeps every
  // box as typed. The list's check is stood in for here by the made-up words themselves; the live
  // rig asks the library's, over a box whose word ends in a spacing diaeresis (U+00A8), which the
  // library's NFKD splits in two. CONTROLS: 25 one-word boxes name nothing and are each checked
  // once, and white space around a word is not a second word.
  {
    const std::vector<std::wstring> listed = MadeUpWords();
    size_t asked = 0;
    auto isListWord = [&listed, &asked](std::wstring const& w) {
      ++asked;
      const size_t b = w.find_first_not_of(L' ');
      const size_t e = w.find_last_not_of(L' ');
      const std::wstring t = b == std::wstring::npos ? std::wstring() : w.substr(b, e - b + 1);
      return std::find(listed.begin(), listed.end(), t) != listed.end();
    };
    std::vector<std::wstring> boxes = MadeUpWords();
    boxes[0] = L"  " + boxes[0] + L" ";  // spaces around a word: still one word
    const bool clean = CheckBoxes(boxes, isListWord).empty() && asked == 25;
    boxes[2] = L"qzqzqzqzz";  // one word, and not one of the list's
    boxes[6] = L"";
    boxes[11] = L"qzqzqzqzl qzqzqzqzl";
    boxes[19] = L"qzqzqzqzt" + std::wstring(1, wchar_t(9)) + L"qzqzqzqzt qzqzqzqzt";
    const std::vector<BoxIssue> issues = CheckBoxes(boxes, isListWord);
    const bool named =
        issues.size() == 4 && issues[0].position == 3 && issues[0].words == 1 && issues[0].offList &&
        issues[1].position == 7 && issues[1].words == 0 && !issues[1].offList &&
        issues[2].position == 12 && issues[2].words == 2 && !issues[2].offList &&
        issues[3].position == 20 && issues[3].words == 3 && !issues[3].offList;
    const std::wstring line = BoxesLine(issues);
    const bool said = ContainsCI(line, L"word 3 is not a word of the list") &&
                      ContainsCI(line, L"word 7 is empty") &&
                      ContainsCI(line, L"word 12 holds two words") &&
                      ContainsCI(line, L"word 20 holds 3 words") &&
                      ContainsCI(line, L"nothing was sent") && BoxesLine({}).empty();
    id::LiveInputs in;
    in.typed = boxes;
    in.boxes = issues;
    const id::ScreenModel m = id::BuildLiveScreen(id::Screen::TypeBack, in);
    id::Block const* f = FieldsOf(m);
    const bool drawn = f && f->wrongPositions == std::vector<int>{3, 7, 12, 20} && f->text == line &&
                       f->prefill == boxes &&
                       f->markNote == L"Type one word from your paper in this box.";
    const bool ok = clean && named && said && drawn;
    lines.push_back(std::format(
        L"  id live boxes    : {}  25 one-word boxes name nothing, each checked once: {}; box 3 of "
        L"one word off the list, box 7 empty, box 12 of two words and box 20 of three are named by "
        L"number: {}; the line says so: {} ('{}'); the type-back screen marks those boxes and keeps "
        L"each as typed: {}   [query: CheckBoxes and BoxesLine over 25 made-up words with a word "
        L"off the list, an empty box, a doubled box and a tripled one]",
        Verdict(ok), YesNo(clean), YesNo(named), YesNo(said), line, YesNo(drawn)));
  }

  // 3. THE GATE: PROPERTY 2, "the identity exists only after all 25 words match". Only OK goes on;
  // a WRONG that names one word (24 of 25 right) does not, nor does any other kind. The positions
  // are marked from 1 to 25, in order, each once, and the sentence names every one of them.
  {
    const bool okGoes = ConfirmAccepted(URNET_MESSAGE_WORDS_OK, 0);
    const bool oneWrongStays = !ConfirmAccepted(URNET_MESSAGE_WORDS_WRONG, 1);
    const bool allWrongStays = !ConfirmAccepted(URNET_MESSAGE_WORDS_WRONG, 25);
    const bool othersStay = !ConfirmAccepted(URNET_MESSAGE_WORDS_NOT_OFFERED, 0) &&
                            !ConfirmAccepted(URNET_MESSAGE_WORDS_IDENTITY_EXISTS, 0) &&
                            !ConfirmAccepted(URNET_MESSAGE_WORDS_FAILED, 0) &&
                            !ConfirmAccepted(99, 0);
    const std::vector<int> named = NamedPositions({17, 3, 3, 0, 26, 1, 25}, 6);
    const bool namedOk = named == std::vector<int>{1, 3, 17};
    const std::wstring one = WrongWordsLine({3});
    const std::wstring two = WrongWordsLine({3, 17});
    const bool lineOk = ContainsCI(one, L"word 3 ") && ContainsCI(one, L"nothing was corrected") &&
                        ContainsCI(two, L"3 and 17") && WrongWordsLine({}).empty();
    const bool ok = okGoes && oneWrongStays && allWrongStays && othersStay && namedOk && lineOk;
    lines.push_back(std::format(
        L"  id live gate     : {}  OK goes on: {}; WRONG naming 1 word (24 of 25) stays: {}; WRONG "
        L"naming 25 stays: {}; not offered, identity exists, the library's failure and an unknown kind "
        L"stay: {}; "
        L"positions marked 1..25, in order, once (17,3,3,0,26,1 of 6 -> 1,3,17): {}; the line names "
        L"each and says nothing was corrected: {} ('{}')   [query: ConfirmAccepted, NamedPositions "
        L"and WrongWordsLine]",
        Verdict(ok), YesNo(okGoes), YesNo(oneWrongStays), YesNo(allWrongStays), YesNo(othersStay),
        YesNo(namedOk), YesNo(lineOk), two));
  }

  // 4. THE SCREENS, AND THE WORDS SCREENS' CAPTURE FLAG (Property 1): built in live mode from what
  // the library answered, with no made-up value - not one string names the demo - and excluded from
  // capture exactly where they show or take the words.
  {
    id::LiveInputs in;
    in.words = MadeUpWords();
    const id::Screen kLive[] = {id::Screen::SetUp, id::Screen::Words, id::Screen::TypeBack,
                                id::Screen::TypeBackWrong, id::Screen::SetUpDone};
    size_t built = 0, flagged = 0, demo = 0;
    std::wstring firstBad = L"(none)";
    for (id::Screen s : kLive) {
      const id::ScreenModel m = id::BuildLiveScreen(s, in);
      const bool words = s == id::Screen::Words || s == id::Screen::TypeBack ||
                         s == id::Screen::TypeBackWrong;
      built += m.built ? 1 : 0;
      flagged += (m.built && m.excludedFromCapture == words) ? 1 : 0;
      demo += NamesDemo(m) ? 1 : 0;
      if ((!m.built || m.excludedFromCapture != words || NamesDemo(m)) && firstBad == L"(none)")
        firstBad = id::ScreenName(s);
    }
    size_t others = 0;
    for (int i = 1; i < static_cast<int>(id::Screen::kCount); ++i) {
      const auto s = static_cast<id::Screen>(i);
      bool live = false;
      for (id::Screen l : kLive) live = live || l == s;
      if (!live && id::BuildLiveScreen(s, in).built) ++others;
    }
    const id::ScreenModel setUp = id::BuildLiveScreen(id::Screen::SetUp, in);
    bool codeShown = false;
    for (auto const& b : setUp.blocks) codeShown = codeShown || b.type == id::BlockType::Code;
    const bool setUpOk = setUp.built && HasAction(setUp, id::Action::MakeWords) && !codeShown;
    const id::ScreenModel wordsScreen = id::BuildLiveScreen(id::Screen::Words, in);
    id::Block const* shown = WordsOf(wordsScreen);
    const bool wordsOk = shown && shown->words == in.words &&
                         HasText(wordsScreen, PhraseWordsLabel(RunMode::Live)) &&
                         HasText(wordsScreen, PhraseWordsCaveat(RunMode::Live));
    const bool ok = built == std::size(kLive) && flagged == std::size(kLive) && demo == 0 &&
                    others == 0 && setUpOk && wordsOk;
    lines.push_back(std::format(
        L"  id live screens  : {}  {}/{} live screens built (setup, words, typeback, typeback-wrong, "
        L"setup-done), {}/{} excluded from capture exactly where they show or take the words, {} "
        L"naming the demo (first bad: {}); {} other screens built live; setup offers the first device "
        L"and no code: {}; the words screen shows the 25 offered, under '{}' and its caveat: {}   "
        L"[query: BuildLiveScreen for every Screen, with 25 made-up words offered]",
        Verdict(ok), built, std::size(kLive), flagged, std::size(kLive), demo, firstBad, others,
        YesNo(setUpOk), PhraseWordsLabel(RunMode::Live), YesNo(wordsOk)));
  }

  // 5. THE FIELD SETTINGS (F3 section 2.11.3; PLAN Task 16): every field that takes a live word
  // masks it when focus leaves, has the reveal switch, has no spelling or prediction, has a
  // password's input scope, and lets nothing be copied or cut - so no word reaches the clipboard or
  // its history. CONTROL: the fabricated type-back keeps slice 0's fields, so the case reads the
  // policy a live screen asks for and not a default.
  {
    id::LiveInputs in;
    in.typed = MadeUpWords();
    in.wrong = {3};
    id::Block const* back = nullptr;
    id::Block const* wrong = nullptr;
    const id::ScreenModel typeBack = id::BuildLiveScreen(id::Screen::TypeBack, in);
    const id::ScreenModel typeWrong = id::BuildLiveScreen(id::Screen::TypeBackWrong, in);
    back = FieldsOf(typeBack);
    wrong = FieldsOf(typeWrong);
    const bool liveOk = back && wrong && SamePolicy(back->fields, id::kLiveWordFieldPolicy) &&
                        SamePolicy(wrong->fields, id::kLiveWordFieldPolicy) &&
                        back->words.size() == kPhraseWords && wrong->words.size() == kPhraseWords;
    const id::ScreenModel demoBack = id::BuildScreen(id::Screen::TypeBack, RunMode::Fabricated);
    id::Block const* demoFields = FieldsOf(demoBack);
    const bool controlOk = demoFields && SamePolicy(demoFields->fields, id::kDemoWordFieldPolicy) &&
                           !SamePolicy(id::kDemoWordFieldPolicy, id::kLiveWordFieldPolicy);
    const bool policyOk = id::kLiveWordFieldPolicy.maskWhenFocusLeaves &&
                          id::kLiveWordFieldPolicy.revealToggle &&
                          !id::kLiveWordFieldPolicy.spellCheck &&
                          !id::kLiveWordFieldPolicy.prediction &&
                          id::kLiveWordFieldPolicy.passwordScope &&
                          !id::kLiveWordFieldPolicy.copyAllowed;
    const bool ok = liveOk && controlOk && policyOk;
    lines.push_back(std::format(
        L"  id live fields   : {}  the live type-back's 25 fields, and the wrong-word screen's: {} "
        L"({}); the policy is every protection F3 lists: {}; control, the fabricated fields keep "
        L"slice 0's ({}): {}   [query: the WordFields block of BuildLiveScreen(typeback) and "
        L"(typeback-wrong) against kLiveWordFieldPolicy]",
        Verdict(ok), YesNo(liveOk), PolicyText(id::kLiveWordFieldPolicy), YesNo(policyOk),
        PolicyText(id::kDemoWordFieldPolicy), YesNo(controlOk)));
  }

  // 6. THE CAPTURE, BY SESSION KIND (REDTEAM L4; Spec C section 6.2; X-APP-5). In a session on the
  // machine itself, every words screen is excluded from capture and carries no warning. In a remote
  // session (SM_REMOTESESSION) the exclusion would black the window for the only person who can
  // read it, so every words screen skips it and carries the inline warning instead. A screen
  // without the words is neither excluded nor warned, in either kind.
  {
    id::LiveInputs remote;
    remote.remoteSession = true;
    remote.words = MadeUpWords();
    id::LiveInputs local = remote;
    local.remoteSession = false;
    const std::wstring warning(kRemoteSessionWarning);
    size_t onRemote = 0, onLocal = 0, onOthers = 0;
    size_t excludedRemote = 0, excludedLocal = 0, excludedOthers = 0;
    for (id::Screen s : {id::Screen::Words, id::Screen::TypeBack, id::Screen::TypeBackWrong}) {
      const id::ScreenModel r = id::BuildLiveScreen(s, remote);
      const id::ScreenModel l = id::BuildLiveScreen(s, local);
      onRemote += HasText(r, warning) ? 1 : 0;
      onLocal += HasText(l, warning) ? 1 : 0;
      excludedRemote += r.excludedFromCapture ? 1 : 0;
      excludedLocal += l.excludedFromCapture ? 1 : 0;
    }
    for (id::Screen s : {id::Screen::SetUp, id::Screen::SetUpDone}) {
      onOthers += HasText(id::BuildLiveScreen(s, remote), warning) ? 1 : 0;
      excludedOthers += id::BuildLiveScreen(s, remote).excludedFromCapture ? 1 : 0;
      excludedOthers += id::BuildLiveScreen(s, local).excludedFromCapture ? 1 : 0;
    }
    const bool ok = onRemote == 3 && excludedRemote == 0 && onLocal == 0 && excludedLocal == 3 &&
                    onOthers == 0 && excludedOthers == 0;
    lines.push_back(std::format(
        L"  id live remote   : {}  a remote session: the warning on {}/3 words screens, {} of them "
        L"excluded from capture (want 0); a local one: the warning on {}, {}/3 excluded; the screens "
        L"without the words: warned {}, excluded {}   [query: BuildLiveScreen's strings and "
        L"excludedFromCapture, with LiveInputs::remoteSession set and not]",
        Verdict(ok), onRemote, excludedRemote, onLocal, excludedLocal, onOthers, excludedOthers));
  }

  // 7. THE TYPED-BACK STATES (Property 2): a wrong word is named in its own field, every one the
  // library named, and nothing is corrected - each field holds exactly what was typed. The plain
  // type-back offers the check and the way back to the words; while a request is with the library
  // no second one is offered.
  {
    id::LiveInputs in;
    in.typed = MadeUpWords();
    in.typed[2] = L"notaword";
    in.typed[16] = L"QZQZQZQZQ";
    in.wrong = {3, 17};
    const id::ScreenModel m = id::BuildLiveScreen(id::Screen::TypeBackWrong, in);
    id::Block const* f = FieldsOf(m);
    const bool kept = f && f->prefill == in.typed;
    const bool marked = f && f->wrongPositions == in.wrong;
    const bool said = f && f->text == WrongWordsLine(in.wrong);
    const id::ScreenModel plain = id::BuildLiveScreen(id::Screen::TypeBack, id::LiveInputs{});
    const bool plainOk = plain.built && HasAction(plain, id::Action::ConfirmWords) &&
                         HasAction(plain, id::Action::MakeWords) && FieldsOf(plain) &&
                         FieldsOf(plain)->prefill.empty() && FieldsOf(plain)->wrongPositions.empty();
    id::LiveInputs busy;
    busy.waiting = true;
    const id::ScreenModel waiting = id::BuildLiveScreen(id::Screen::TypeBack, busy);
    const bool waitOk = waiting.built && !HasAction(waiting, id::Action::ConfirmWords);
    const bool ok = kept && marked && said && plainOk && waitOk;
    lines.push_back(std::format(
        L"  id live typed    : {}  words 3 and 17 named, each in its own field: {}; every field "
        L"holds what was typed, nothing corrected: {}; the line is WrongWordsLine's: {}; the plain "
        L"type-back offers Check and Back to the words, with empty fields: {}; while a request is "
        L"out, no Check: {}   [query: BuildLiveScreen(typeback-wrong) with 25 typed words and the "
        L"library's positions 3 and 17]",
        Verdict(ok), YesNo(marked), YesNo(kept), YesNo(said), YesNo(plainOk), YesNo(waitOk)));
  }

  // 8. THE ID'S TEXT (Property 3): "Your ID" is the library's account ID, character for character,
  // and Copy puts exactly that on the clipboard; this device's row carries its device ID and the
  // phrase mark. Unbuilt when there is no identity, or a json that did not read.
  {
    const IdentityInfo some = ParseIdentityJson(SampleIdentityJson());
    const id::AccountModel a = id::BuildLiveAccount(some);
    const bool idOk = a.built && a.idValue == Widen(kSampleAccount) &&
                      a.copied == Widen(kSampleAccount) && a.idValue != Widen(kSampleDevice);
    const id::AccountDevice* self = nullptr;
    for (auto const& d : a.devices)
      if (d.thisDevice) self = &d;
    const bool selfOk = self && ContainsCI(self->detail, Widen(kSampleDevice)) &&
                        self->phraseDevice && ContainsCI(self->detail, L"phrase device") &&
                        ContainsCI(self->detail, L"this device");
    bool demo = false;
    for (auto const& s : {a.caption, a.idTitle, a.idNote, a.idValue, a.devicesCaption,
                          a.devicesNote})
      demo = demo || ContainsCI(s, L"demo");
    IdentityInfo empty;
    empty.read = true;
    IdentityInfo unread;
    const bool unbuilt = !id::BuildLiveAccount(empty).built && !id::BuildLiveAccount(unread).built;
    const bool ok = idOk && selfOk && !demo && unbuilt;
    lines.push_back(std::format(
        L"  id live your id  : {}  Your ID is the account ID, character for character, and so is "
        L"what Copy puts on the clipboard: {} ('{}'); this device's row carries its device ID and "
        L"the phrase mark: {}; no string names the demo: {}; no identity, or a json not read, builds "
        L"nothing: {}   [query: BuildLiveAccount over the sample json, whose account and device IDs "
        L"differ]",
        Verdict(ok), YesNo(idOk), a.idValue, YesNo(selfOk), YesNo(!demo), YesNo(unbuilt)));
  }

  // 9. THE ROSTER BY IDENTITY (Property 4; SCOPE H6): a conversation is direct or a group by how
  // many identities it holds, not leaves. Three leaves and two identities - one person on two
  // devices - is direct. CONTROLS: three identities is a group; two leaves of two identities is
  // direct; a leaf no identity binds counts as a person of its own.
  {
    auto member = [](uint32_t leaf, std::string const& identity, bool mine) {
      LiveMember m;
      m.leafIndex = leaf;
      m.senderHandle = std::string(32, static_cast<char>('a' + leaf));
      m.identityPub = std::string(64, static_cast<char>('a' + leaf));
      m.role = "member";
      m.mine = mine;
      m.identity = identity;
      m.deviceId = std::format("{:04x}-0000-0000-0000", leaf);
      return m;
    };
    const std::string x(64, '1'), y(64, '2'), z(64, '3');
    auto kindOf = [](std::vector<LiveMember> const& members) {
      LiveGroup g;
      g.groupIdHex = std::string(64, 'f');
      g.members = members;
      const urmsg::demo::World w = BuildWorld(g);
      return w.conversations.empty() ? std::wstring(L"none")
             : w.conversations.front().kind == urmsg::demo::ConversationKind::Direct
                 ? std::wstring(L"direct")
                 : std::wstring(L"group");
    };
    const std::wstring twoOfThree = kindOf({member(0, x, true), member(1, x, false),
                                            member(2, y, false)});
    const std::wstring threeOfThree = kindOf({member(0, x, true), member(1, y, false),
                                              member(2, z, false)});
    const std::wstring twoOfTwo = kindOf({member(0, x, true), member(1, y, false)});
    const std::wstring unbound = kindOf({member(0, x, true), member(1, y, false),
                                         member(2, std::string(), false)});
    const bool ok = twoOfThree == L"direct" && threeOfThree == L"group" && twoOfTwo == L"direct" &&
                    unbound == L"group";
    lines.push_back(std::format(
        L"  id live roster   : {}  3 leaves of 2 identities: {} (want direct); controls: 3 of 3 "
        L"{}, 2 of 2 {}, 2 identities and a leaf no identity binds {} (want group, direct, group)   "
        L"[query: BuildWorld over a LiveGroup whose members carry the member json's identity]",
        Verdict(ok), twoOfThree, threeOfThree, twoOfTwo, unbound));
  }

  // 10. THE STORE FOLDER (P6; REDTEAM M1): this build's stream and state open in a folder named for
  // the slice under live/, never in live/ itself, where build 03 kept its state.
  {
    const std::filesystem::path storage(L"C:/r");
    const std::filesystem::path root = storage / L"live";
    const std::filesystem::path folder = SliceStoreFolder(root);
    const std::filesystem::path names = LocalNamesFile(storage);
    const bool storeOk = folder == root / kSliceStoreFolder && folder != root &&
                         folder / L"state" != root / L"state" && folder.parent_path() == root;
    // AND THE NAMES (X-APP-6): this build reads no file an older build kept, local_names.json
    // included, so its names live in the slice's folder too
    const bool namesOk = names.parent_path() == folder && names != storage / L"local_names.json";
    const bool ok = storeOk && namesOk;
    lines.push_back(std::format(
        L"  id live store    : {}  this slice's stream and state open in '{}', beside build 03's "
        L"'{}' and never in it: {}; the names a person gives are kept in '{}', not in build 03's "
        L"'{}': {}   [query: SliceStoreFolder(<root>/live) and LocalNamesFile(<root>)]",
        Verdict(ok), folder.generic_wstring(), (root / L"state").generic_wstring(), YesNo(storeOk),
        names.generic_wstring(), (storage / L"local_names.json").generic_wstring(),
        YesNo(namesOk)));
  }

  // 11. THE UNREADABLE SENTENCE IS DRAWN (X-APP-3): when the identity cannot be read the session
  // ends, and the screen says why - the worker's sentence, and nothing to press, and not "Set up
  // this device", which would claim this device has no identity.
  {
    id::LiveInputs in;
    in.stopped = true;
    in.failure = L"URmessage could not read this device's identity. Close it and open it again.";
    const id::ScreenModel m = id::BuildLiveScreen(id::Screen::SetUp, in);
    size_t buttons = 0;
    for (auto const& b : m.blocks) buttons += b.buttons.size();
    const bool ok = m.built && HasText(m, in.failure) && buttons == 0 &&
                    !HasText(m, L"Set up this device") && !m.excludedFromCapture;
    lines.push_back(std::format(
        L"  id live stopped  : {}  the identity could not be read: the sentence is drawn: {}, with "
        L"{} button(s) to press (want 0), and no 'Set up this device': {}   [query: "
        L"BuildLiveScreen(setup) with LiveInputs::stopped and the worker's sentence]",
        Verdict(ok), YesNo(HasText(m, in.failure)), buttons,
        YesNo(!HasText(m, L"Set up this device"))));
  }

  // 12. THE STRIP WHILE A LIVE SESSION IS UP (X-APP-7): before any live world, a session that has
  // connected reads Connected - the setup and the account run under it - a launch that cannot go
  // on reads Offline, and anything else is still Connecting.
  {
    using urmsg::demo::ConnectState;
    const ConnectState up = urmsg::views::EmptyWorldConnectState(false, true);
    const ConnectState dialling = urmsg::views::EmptyWorldConnectState(false, false);
    const ConnectState stuck = urmsg::views::EmptyWorldConnectState(true, false);
    const ConnectState stuckUp = urmsg::views::EmptyWorldConnectState(true, true);
    const bool ok = up == ConnectState::Connected && dialling == ConnectState::Connecting &&
                    stuck == ConnectState::Offline && stuckUp == ConnectState::Offline;
    lines.push_back(std::format(
        L"  id live strip    : {}  before a live world: a connected session reads '{}', a dialling "
        L"one '{}', a launch that cannot go on '{}' (and '{}' if it had connected)   [query: "
        L"EmptyWorldConnectState for the four (stuck, connected) pairs]",
        Verdict(ok), urmsg::views::StatusStateWord(up), urmsg::views::StatusStateWord(dialling),
        urmsg::views::StatusStateWord(stuck), urmsg::views::StatusStateWord(stuckUp)));
  }
  return lines;
}

}  // namespace urmsg::live
