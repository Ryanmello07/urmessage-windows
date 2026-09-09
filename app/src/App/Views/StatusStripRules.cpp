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
  // Segoe Fluent Icons, named explicitly wherever this app draws one
  // (App.xaml's UrIconFontFamily): FontIcon otherwise falls back to Segoe MDL2
  // Assets, whose metrics differ.
  return keyVerified ? std::wstring{L"\ue72e"}   // Lock
                     : std::wstring{L"\ue785"};  // Unlock
}

std::wstring StatusEpochValue(uint64_t epoch) { return std::to_wstring(epoch); }

std::wstring StatusRecordsValue(int recordsPerSecond) {
  // No unit suffix: the field's caption carries "rec/s", the same division of
  // labour every other field of the strip uses.
  return std::to_wstring(recordsPerSecond);
}

}  // namespace urmsg::views
