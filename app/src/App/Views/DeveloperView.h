// The Developer destination (design §6.6): a demo-state inspector, the two
// session switches (Demo/DeveloperSwitches.h), and the DemoWorld dump
// (Views/DeveloperDump.h).
//
// It exists so the things an engineer wants — a state readout, a pause, a
// motion kill switch, the world as text — have somewhere honest to live
// instead of leaking into the product surfaces an audience is looking at.
//
// Reachable only under Advanced Mode: the nav item's visibility is gated on
// the mode (MainWindow.xaml:151, flipped by the wiring the deep link drains),
// and --demo=developer forces the mode on for exactly that reason
// (Demo/DemoShellState.h's DeepLinkFor).
//
// Contract §4 gives this view no Set*Advanced and this wave registers no
// subscription — the d7 distillation's §1.2 ruling gives the ONE
// OnAdvancedModeChanged subscriber to the wiring task (W7), which also owns
// the live nav-item flip and the page rebuild. This view reads what it
// reports at BUILD time; both switches are seeded from process state, never
// from the widget, so a rebuild can never strand them.
//
// The composition is the inspector rail's section language (design d4 §4):
// one floating caption + one card per group, the same shape the Settings page
// ships. Copy is English string literals, NOT localization keys:
// Strings/en/Resources.resw is generated and this work adds no key to it
// (design §9.4).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// Contract v2 §4: exactly one element, the pane root. The two switch rows
// keep their own figures current through the handlers that change them, so
// nothing outside this file needs a handle on them.
struct DeveloperView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
};

DeveloperView MakeDeveloper(urmsg::demo::World const& world);

}  // namespace urmsg::views
