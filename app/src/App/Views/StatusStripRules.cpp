// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader>, the same as
// Demo/DemoWorld.cpp and Views/InspectRailFields.cpp. Startup.cpp's
// CollectDiagnostics() calls these from wWinMain BEFORE
// winrt::init_apartment(), so "pure C++, no winrt" has to be a property the
// compiler enforces rather than a comment at the top of a file.
#include "Views/StatusStripRules.h"

namespace urmsg::views {

std::wstring_view StatusStateWord(demo::ConnectState state) {
  switch (state) {
    case demo::ConnectState::Connected:
      // This must never be read as a claim that a real session exists: the
      // demo has no protocol (design §2). It is a STATE WORD, not a claim —
      // the strip's honesty framing lives in the drawer's DEMO MODEL header
      // and the padlock's framed automation name, not in a suffix here that a
      // crop would keep anyway.
      return L"Connected";
    case demo::ConnectState::Connecting:
      return L"Connecting";
    case demo::ConnectState::Offline:
      break;
  }
  return L"Offline";
}

std::wstring StatusLockGlyph(bool keyVerified) {
  // Code points of the one icon font (App.xaml's UrIconFontFamily,
  // UrComponents.h's kIconFontFamily): Segoe Fluent Icons, falling back to
  // Segoe MDL2 Assets where Windows 10 lacks it.
  return keyVerified ? std::wstring{L"\ue72e"}   // Lock
                     : std::wstring{L"\ue785"};  // Unlock
}

std::wstring StatusEpochValue(uint64_t epoch) { return std::to_wstring(epoch); }

std::wstring StatusRecordsValue(int recordsPerSecond) {
  // No unit suffix: the field's caption carries "rec/s", the same division of
  // labour every other field of the strip uses.
  return std::to_wstring(recordsPerSecond);
}

RelayRowFit RelayDrawerRowFit() {
  // The drawer's labels are three short fixed words ("This device", "URnetwork", "Message
  // server") and its values are what the session observed: the platform url and a 36-character
  // client_id. The label is the one that must always read.
  RelayRowFit fit;
  fit.label = TextCut::None;
  fit.value = TextCut::Ellipsis;
  fit.labelSizesToContent = true;
  return fit;
}

std::wstring RelayRowValue(std::wstring_view subLabel, bool healthy) {
  // The state is in WORDS as well as in the dot's colour (the drawer's own note says why).
  std::wstring value{subLabel};
  if (!healthy) value += L"  offline";
  return value;
}

std::wstring RelayRowName(std::wstring_view label, std::wstring_view subLabel, bool healthy) {
  std::wstring name{label};
  name += L", ";
  name += subLabel;
  name += healthy ? L", online" : L", offline";
  return name;
}

StripFit FitStatusStrip(std::vector<StripItem> const& items, double room) {
  StripFit out;
  out.fit.assign(items.size(), StripItemFit::Whole);
  out.width.reserve(items.size());
  for (auto const& item : items) {
    out.width.push_back(item.width);
    out.total += item.width;
  }
  // A hair of tolerance, so a strip that fits to the last fraction of a DIP is not cut by the
  // arithmetic's rounding.
  const auto over = [&out, room] { return out.total > room + 0.01; };

  // 1. The Advanced Mode fields go first, rightmost first: they are the extras a person asked
  //    for, and the Normal facts outrank them where there is no room for both.
  for (size_t i = items.size(); i-- > 0 && over();) {
    if (items[i].part != StripPart::Optional) continue;
    out.fit[i] = StripItemFit::Dropped;
    out.total -= out.width[i];
    out.width[i] = 0;
  }
  // 2. Then the caption, which names the value and says nothing the value's own automation name
  //    does not.
  for (size_t i = items.size(); i-- > 0 && over();) {
    if (items[i].part != StripPart::Caption) continue;
    out.fit[i] = StripItemFit::Dropped;
    out.total -= out.width[i];
    out.width[i] = 0;
  }
  // 3. Then the value is cut with an ellipsis, to no narrower than its floor.
  for (size_t i = items.size(); i-- > 0 && over();) {
    if (items[i].part != StripPart::Value) continue;
    const double floor = items[i].floor < items[i].width ? items[i].floor : items[i].width;
    const double wanted = out.width[i] - (out.total - room);
    const double cut = wanted < floor ? floor : wanted;
    if (cut < out.width[i]) {
      out.fit[i] = StripItemFit::Cut;
      out.total -= out.width[i] - cut;
      out.width[i] = cut;
    }
  }
  out.fits = !over();
  return out;
}

std::wstring StatusStripFacts(std::vector<std::wstring> const& facts) {
  std::wstring out;
  for (auto const& fact : facts) {
    if (fact.empty()) continue;
    if (!out.empty()) out += L"\n";
    out += fact;
  }
  return out;
}

}  // namespace urmsg::views
