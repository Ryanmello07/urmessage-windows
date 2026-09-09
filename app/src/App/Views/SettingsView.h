// The Settings destination (design §6.6).
//
// Appearance and Privacy and security are POPULATED — real rows, values read
// from the demo world. Notifications, Storage and data and Account are present
// but VISIBLY INERT: a caption that says "not in this demo" and one faint
// line. §9.1 — "a dead-looking button is a demo bug" — is why the inert rows
// are Borders and not Buttons: a Border has no hover fill, no focus rect and
// no invoke, so it cannot promise something it will not do.
//
// The composition is the inspector rail's section language (design d4 §4):
// one floating caption + one card per group, not edge-to-edge ruled lines, so
// the page reads as the same app the rail and the Network page belong to.
// UrComponents.h's MakeSettingsCard is the wrong tool here despite its name —
// this page is groups of explained rows on the pane's 44 DIP rhythm, and that
// builder is one glyph-led row per card.
//
// Copy is English string literals, NOT localization keys: Strings/en/
// Resources.resw is generated from urnetwork/localizations and this work adds
// no key to it (design §9.4).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace urmsg::views {

// Contract v2 §4: exactly one element, the pane root. Everything the pane
// keeps consistent (the Normal/Advanced meta, the disclosure panel) is
// captured by the handler that changes it, so nothing outside this file needs
// a handle on it — and there is deliberately NO SetSettingsAdvanced, because
// this view OWNS the switch rather than following it.
struct SettingsView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
};

// `onAdvancedChanged` is raised when the user moves the switch; `advanced` is
// the value the pane is BUILT in. MainWindow wires the callback to
// urmsg::SetAdvancedModeEnabled and passes urmsg::AdvancedModeEnabled() — no
// view reads the preference itself (Demo/AdvancedMode.h).
SettingsView MakeSettings(std::function<void(bool)> onAdvancedChanged, bool advanced);

}  // namespace urmsg::views
