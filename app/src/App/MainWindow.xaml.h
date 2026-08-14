// URmessage main window — CHECKPOINT 1 shell.
//
// Written fresh against the VPN client's MainWindow, not copied from it: its
// title bar, NavigationView and ApplyBreakpoint are the reusable parts, and its
// ~1700 lines of destination code are not.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

// The cppwinrt projection base. It transitively includes the markup
// MainWindow.xaml.g.h, which references MainWindow_base defined HERE — so this
// is the correct include. Do NOT swap it for MainWindow.xaml.g.h, which then
// cannot find MainWindow_base. The generated InitializeComponent/Connect impls
// are compiled from the XamlTypeInfo*.g.cpp units App.vcxproj's
// UrmCompileGeneratedXamlImpl adds to the build, not from this header.
#include "MainWindow.g.h"

#include <string>
#include <string_view>

#include "UrComponents.h"
#include "WindowReveal.h"

namespace winrt::URmessage::implementation {

struct MainWindow : MainWindowT<MainWindow> {
  MainWindow();

  // NavigationView selection -> which destination is visible. Public because
  // the XAML markup binds to it by name.
  void OnNavSelectionChanged(
      winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
      winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

  // Called by App::OnLaunched immediately after Activate(): the window reveal
  // is armed in this constructor (it must write its start pose BEFORE the first
  // composed frame) and started here.
  void StartReveal();

 private:
  // Every label in the window, from the localization store. One place, so a
  // missing key is one line to find rather than a hunt through the markup.
  void ApplyStrings();

  // The conversation-list placeholder. Sample rows only — there is no protocol
  // behind them yet, and the file says so where the data is built rather than
  // in a comment somewhere else.
  void BuildConversationList();

  // The ONE desktop breakpoint (urnw::kit::kWideBreakpointDip). Below it the
  // list pane fills the window and the thread pane does not exist; at or above,
  // the two panes sit side by side with a 1px rule between them. One function
  // at window level: there is exactly one place where this app decides what
  // "wide" means.
  void ApplyBreakpoint();

  void ShowDestination(std::wstring_view tag);

  urnw::WindowReveal reveal_;
  urnw::kit::PaneSearchRow search_{};
  bool wide_ = false;
  // Whether ApplyBreakpoint has ever actually WRITTEN the layout. Without it,
  // the first pass early-outs whenever the initial width is narrow (wide_
  // already being false), and the window is only correct because the markup
  // defaults happen to spell the narrow state. That is an invariant nothing
  // enforces, living in two files — the VPN client carries the same flag for
  // the same reason. The early-out has to test every state it applies.
  bool breakpointApplied_ = false;
};

}  // namespace winrt::URmessage::implementation

namespace winrt::URmessage::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::URmessage::factory_implementation
