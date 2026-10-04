// SPDX-License-Identifier: MPL-2.0
//
// NO pch.h include, and App.vcxproj compiles this unit with
// PrecompiledHeader=NotUsing: the project pch pulls in every winrt/ header,
// and this file is reachable from CollectDiagnostics before init_apartment.
// See Demo/DemoWorld.cpp/.h for the pattern.
//
// UrMotion.h below still drags in winrt/Microsoft.UI.Composition.h and
// winrt/Microsoft.UI.Xaml.Media.Animation.h on its own account -- it needs
// those for Bezier and the spring types, which this file never touches. This
// file reads exactly two plain integers off it (kStaggerMs,
// kMaxStaggerSteps) and constructs nothing from either header. Confirmed by
// building this TU standalone (its .obj deleted, forcing a from-scratch
// compile, no pch.h) after switching to NotUsing: it compiles clean, so the
// projection being textually reachable here is not the same thing as a WinRT
// type actually being instantiated on this path -- that stays true only
// because nothing in this file names one, which is still a review
// obligation, not a compiler one (see the header's own comment).
#include "Views/ConversationRowModel.h"

#include <algorithm>
#include <cwctype>
#include <format>

#include "UrMotion.h"

namespace urmsg::views {
namespace {

std::wstring Lower(std::wstring const& text) {
  std::wstring out = text;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return out;
}

// The diagnostics line shape, measured off the existing output rather than
// guessed: "  build            : x64 ..." is two spaces, a 17-wide key, then
// ": " (Startup.cpp:190).
std::wstring Line(wchar_t const* key, bool pass, std::wstring const& detail) {
  return std::format(L"  {:<17}: {}  {}", key, pass ? L"PASS" : L"FAIL", detail);
}

}  // namespace

ConversationRowModel MakeConversationRowModel(demo::Conversation const& c) {
  ConversationRowModel model;
  model.name = c.name;
  model.showTimer = c.disappearing;
  model.showMuted = c.muted;
  model.timeLabel = c.timeLabel;
  // Not "copy it and collapse the TextBlock": the model never carries a preview
  // it is not allowed to draw (Spec C 4.2). The world's preview is non-empty on
  // this conversation, which is what makes the refusal visible in --diagnose.
  if (!c.disappearing) model.preview = c.preview;
  if (0 < c.unread)
    model.unread = (99 < c.unread) ? std::wstring(L"99+") : std::to_wstring(c.unread);
  if (c.kind == demo::ConversationKind::Group && !c.groupIdHex.empty()) {
    // (std::min) parenthesised, matching WindowShell.cpp:166. NOMINMAX IS
    // defined project-wide (app/Directory.Build.props:75), so the bare name
    // would compile -- this is the house form, kept so a TU that ever loses that
    // definition still builds.
    model.groupIdChip =
        c.groupIdHex.substr(0, (std::min)(static_cast<std::size_t>(6), c.groupIdHex.size()));
  }
  return model;
}

std::wstring ConversationRowAutomationName(demo::Conversation const& c, bool selected) {
  const ConversationRowModel model = MakeConversationRowModel(c);
  std::wstring name = model.name;
  if (c.kind == demo::ConversationKind::Group)
    name += L", " + std::to_wstring(c.memberCount) + L" members";
  if (model.showTimer) {
    // The timer FontIcon is AccessibilityView=Raw in the row, so this is the
    // ONLY place a screen reader hears that this conversation disappears.
    name += L", disappearing messages";
  } else {
    name += L", " + model.preview;
  }
  if (!model.unread.empty()) name += L", " + model.unread + L" unread";
  if (model.showMuted) name += L", muted";
  name += L", " + model.timeLabel;
  if (selected) name += L". Selected";
  return name;
}

int64_t ConversationRowDelayMs(std::size_t index) {
  const std::size_t steps =
      (std::min)(index, static_cast<std::size_t>(urnw::motion::kMaxStaggerSteps));
  return static_cast<int64_t>(steps) * urnw::motion::kStaggerMs;
}

bool ConversationRowMatches(demo::Conversation const& c, std::wstring const& query) {
  const std::wstring needle = Lower(query);
  if (needle.empty()) return true;
  if (Lower(c.name).find(needle) != std::wstring::npos) return true;
  // A disappearing conversation is searchable by NAME ONLY. Matching its preview
  // would put the text the row refuses to draw back on screen by way of the
  // result set -- the same leak, one indirection later.
  if (c.disappearing) return false;
  return Lower(c.preview).find(needle) != std::wstring::npos;
}

std::vector<std::wstring> CollectConversationListDiagnostics() {
  // GetWorld() is built once, seeded, and is pure data (std::wstring /
  // std::vector), so calling it here needs no apartment. Contract 1.
  demo::World const& world = demo::GetWorld();
  const std::size_t total = world.conversations.size();
  std::vector<std::wstring> lines;

  // A second disappearing conversation, built LOCALLY rather than added to
  // DemoWorld -- F3 fingerprints the world's exact bytes and asserts
  // determinism against a hardcoded constant (I10), so changing the seed data
  // would ripple backwards into a completed task. conv-marta is DemoWorld's
  // ONLY disappearing row, so without a second data point the timer and
  // search-leak checks below have a population of one: they cannot tell
  // MakeConversationRowModel gating on c.disappearing (correct) from a mutant
  // gating on c.id == L"conv-marta" (wrong) -- both produce identical output
  // against the world alone. Different id, name and preview from conv-marta,
  // so nothing below can be satisfied by an identity match standing in for
  // the flag. Purely a stack local: never touches demo::MutableWorld(), so
  // WorldFingerprint/I10 is untouched by its existence.
  demo::Conversation ghost{};
  ghost.id = L"diag-ghost-disappearing";
  ghost.name = L"Diagnostic second disappearing row";
  ghost.preview = L"a preview this row must refuse to show";
  ghost.disappearing = true;

  // 1. Every row says who it is and when.
  bool rowsOk = (0 < total);
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    if (m.name.empty() || m.timeLabel.empty()) rowsOk = false;
  }
  lines.push_back(Line(L"demo.list.rows", rowsOk,
                       std::format(L"{} rows, each with a name and a time label", total)));

  // 2. Spec C 4.2: the timer glyph REPLACES the preview. Stated as the claim
  //    actually is, and only in the direction that is falsifiable: the model
  //    drops a preview THAT EXISTS. Contract 1.1 rule 3 guarantees the EPH
  //    conversation's world preview is non-empty, so `dropped` cannot be 0 on a
  //    correct world -- and a 0 here fails rather than passing vacuously.
  //    The old form (m.preview.empty() != c.disappearing) was both vacuous on an
  //    empty EPH preview and a FALSE FAIL on any other empty preview.
  bool timerOk = true;
  std::size_t timerRows = 0;
  std::size_t dropped = 0;
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    if (m.showTimer != c.disappearing) timerOk = false;
    if (!c.disappearing) continue;
    ++timerRows;
    if (!m.preview.empty()) timerOk = false;
    if (!c.preview.empty()) ++dropped;
  }
  // The second, non-conv-marta disappearing row (see the top of this
  // function): same logic, same counters, so a model keyed on identity rather
  // than the flag shows up here as a FAIL instead of passing vacuously.
  {
    const ConversationRowModel m = MakeConversationRowModel(ghost);
    if (!m.showTimer || !m.preview.empty()) timerOk = false;
    ++timerRows;
    if (!ghost.preview.empty()) ++dropped;
  }
  // Zero-guard, matching demo.list.search's leakChecks==0 and
  // demo.list.group's chips==0: without it, a world with no disappearing
  // conversation at all would report "(0 checked)" and still PASS.
  if (timerRows == 0) timerOk = false;
  if (dropped != timerRows) timerOk = false;
  lines.push_back(Line(L"demo.list.timer", timerOk,
                       std::format(L"{} of {} rows show the timer; each dropped a "
                                   L"non-empty world preview ({} checked)",
                                   timerRows, total, dropped)));

  // 3. The unread pill, plus the clamp. The clamp is asserted on a LOCALLY built
  //    Conversation: DemoWorld has no 100-unread row, and an assertion that only
  //    ever sees 0..9 is not checking a clamp at all.
  bool unreadOk = true;
  std::size_t unreadRows = 0;
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    if (m.unread.empty() != (c.unread <= 0)) unreadOk = false;
    if (!m.unread.empty()) ++unreadRows;
  }
  // Boundary, not just a deep value: 99 is the last un-clamped count and 100
  // is the first clamped one. A single point at 120 cannot see an off-by-one
  // at the actual clamp edge -- it would still render "99+" whether the
  // threshold were 99, 100 or 105.
  demo::Conversation atCap{};
  atCap.unread = 99;
  demo::Conversation firstOver{};
  firstOver.unread = 100;
  demo::Conversation loud{};
  loud.unread = 120;
  const bool clampOk = (MakeConversationRowModel(atCap).unread == L"99") &&
                       (MakeConversationRowModel(firstOver).unread == L"99+") &&
                       (MakeConversationRowModel(loud).unread == L"99+");
  if (!clampOk) unreadOk = false;
  lines.push_back(Line(L"demo.list.unread", unreadOk,
                       std::format(L"pill shown iff unread > 0 ({} of {} rows); unread "
                                   L"99/100/120 render \"{}\"/\"{}\"/\"{}\"",
                                   unreadRows, total,
                                   MakeConversationRowModel(atCap).unread,
                                   MakeConversationRowModel(firstOver).unread,
                                   MakeConversationRowModel(loud).unread)));

  // 4. Selection reaches a screen reader, and a group says how many PEOPLE are
  //    in it (contract 1.1 rule 2 pins memberCount == members.size()).
  bool selectOk = (0 < total);
  std::size_t groupRows = 0;
  for (auto const& c : world.conversations) {
    const std::wstring off = ConversationRowAutomationName(c, false);
    const std::wstring on = ConversationRowAutomationName(c, true);
    if (off == on) selectOk = false;
    if (on.find(L"Selected") == std::wstring::npos) selectOk = false;
    if (off.find(L"Selected") != std::wstring::npos) selectOk = false;
    if (c.kind != demo::ConversationKind::Group) continue;
    ++groupRows;
    if (off.find(std::to_wstring(c.memberCount) + L" members") == std::wstring::npos)
      selectOk = false;
  }
  lines.push_back(Line(L"demo.list.select", selectOk,
                       std::format(L"the selected row's name differs and says "
                                   L"\"Selected\" ({} of {}); {} group rows name "
                                   L"their member count",
                                   total, total, groupRows)));

  // 5. The stagger is CAPPED. Uncapped, a longer list turns its entrance into a
  //    seconds-long wipe; kMaxStaggerSteps is what stops that and nothing else
  //    checks it.
  const int64_t step = urnw::motion::kStaggerMs;
  const int64_t cap = static_cast<int64_t>(urnw::motion::kMaxStaggerSteps) * step;
  const bool staggerOk = ConversationRowDelayMs(0) == 0 &&
                         ConversationRowDelayMs(5) == 5 * step &&
                         ConversationRowDelayMs(6) == cap &&
                         ConversationRowDelayMs(50) == cap;
  lines.push_back(Line(L"demo.list.stagger", staggerOk,
                       std::format(L"{}/{}/{}/{} ms at index 0/5/6/50 ({} ms x max {})",
                                   ConversationRowDelayMs(0), ConversationRowDelayMs(5),
                                   ConversationRowDelayMs(6), ConversationRowDelayMs(50),
                                   step, urnw::motion::kMaxStaggerSteps)));

  // 6. The filter cannot resurrect a hidden preview. leakChecks is asserted to
  //    be non-zero: contract 1.1 rule 3 guarantees the material exists, so a 0
  //    here means the world changed under this assertion, not that it passed.
  bool searchOk = true;
  std::size_t matchAll = 0;
  std::size_t leakChecks = 0;
  for (auto const& c : world.conversations) {
    if (ConversationRowMatches(c, L"")) ++matchAll;
    if (!ConversationRowMatches(c, c.name)) searchOk = false;
    if (!c.disappearing || c.preview.empty()) continue;
    ++leakChecks;
    if (ConversationRowMatches(c, c.preview)) searchOk = false;
  }
  // The second, non-conv-marta disappearing row (see the top of this
  // function): kept out of matchAll/total (it is not one of the world's rows)
  // so only the leak check itself runs against it, for the same reason the
  // timer check above adds it -- a population of one cannot tell a leak check
  // keyed on the flag from one keyed on conv-marta's identity.
  if (!ghost.preview.empty()) {
    ++leakChecks;
    if (ConversationRowMatches(ghost, ghost.preview)) searchOk = false;
  }
  if (matchAll != total || leakChecks == 0) searchOk = false;
  lines.push_back(Line(L"demo.list.search", searchOk,
                       std::format(L"empty query matches {} of {}; every row findable "
                                   L"by name; {} hidden-preview leak check(s)",
                                   matchAll, total, leakChecks)));

  // 7. The Advanced group-id chip: on groups only, and a real prefix of the
  //    world's hex rather than a string this file invented. Contract 1.1 rule 9
  //    guarantees a non-empty groupIdHex on every group.
  bool chipOk = true;
  std::size_t chips = 0;
  std::size_t dmRows = 0;
  for (auto const& c : world.conversations) {
    const ConversationRowModel m = MakeConversationRowModel(c);
    const bool isGroup = (c.kind == demo::ConversationKind::Group);
    if (!isGroup) {
      ++dmRows;
      if (!m.groupIdChip.empty()) chipOk = false;
      continue;
    }
    ++chips;
    if (m.groupIdChip.empty() || 6 < m.groupIdChip.size()) chipOk = false;
    if (c.groupIdHex.rfind(m.groupIdChip, 0) != 0) chipOk = false;
  }
  if (chips == 0) chipOk = false;
  lines.push_back(Line(L"demo.list.group", chipOk,
                       std::format(L"{} group rows carry a <=6-char prefix of their "
                                   L"groupIdHex; {} DM rows carry none",
                                   chips, dmRows)));
  return lines;
}

}  // namespace urmsg::views
