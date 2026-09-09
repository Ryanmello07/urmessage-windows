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

#include "Demo/AdvancedMode.h"
#include "Demo/DemoShellState.h"
#include "Demo/DemoSwitches.h"
#include "UrComponents.h"
#include "Views/ConversationListView.h"
#include "Views/InspectRailView.h"
#include "Views/ThreadView.h"
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

  // Read back by App::OnLaunched to choose the launch size, so the command line
  // is parsed EXACTLY ONCE. Not in MainWindow.idl: App.xaml.cpp already reaches
  // the implementation through winrt::get_self for StartReveal().
  urmsg::demo::DemoOptions const& DemoOptions() const { return options_; }

 private:
  // Every label in the window, from the localization store. One place, so a
  // missing key is one line to find rather than a hunt through the markup.
  void ApplyStrings();

  // The conversation-list placeholder. Sample rows only — there is no protocol
  // behind them yet, and the file says so where the data is built rather than
  // in a comment somewhere else.
  void BuildConversationList();

  // The demo thread. Built only under --demo: a normal launch is 480x760 and
  // must behave exactly as it does today (design D7), so the pane keeps its
  // "nothing selected" line. The conversation this opens on, and the two
  // callbacks, become the click graph's when that task wires the rail.
  void BuildThread();

  urmsg::views::ThreadView thread_{};

  // The inspector rail (design 6.3). Built only under --demo, for the same
  // reason BuildThread is: a normal launch is 480x760 and must behave exactly
  // as it does today (design D7), and a non-demo launch at 1600 dip must not
  // grow an empty third pane.
  //
  // It does NOT own the rail COLUMN. MainWindow.xaml declares RailRule and
  // RailHost, Demo/DemoShellState.h holds kRailWidthDip and
  // kRailBreakpointDip, and ApplyBreakpoint is the one writer of the column
  // width and both visibilities. This builds the rail's CONTENT and mounts it.
  void BuildInspectRail();

  urmsg::views::InspectRailView rail_{};

  // A row was clicked. Takes the row INDEX: ConversationListView::rows[i] is
  // world.conversations[i], and nothing reorders either.
  void OnConversationSelected(int index);

  // Reads search_.box and applies it to list_. One place, so the box's text and
  // the pane header's count cannot disagree.
  void ApplyConversationFilter();

  // The search empty state's one visibility writer (d3 2.5): fade in at
  // kBaseMs on the standard curve, out at kFastMs on the exit curve (exits
  // one step faster), an instant swap when motion::ShouldAnimate() is false.
  void SetSearchEmptyVisible(bool show);

  // The ONE window-level layout function. It consumes
  // urmsg::demo::LayoutFor(), which answers all three content-dip thresholds
  // (wide at kWideBreakpointDip, rail at kRailBreakpointDip, strip at
  // kStripMinHeightDip) - there is exactly one place where this app decides
  // what "wide", "rail" and "strip" mean.
  void ApplyBreakpoint();

  void ShowDestination(std::wstring_view tag);

  // The demo composer. Everything below is inert without --demo: the hosts stay
  // collapsed and the window draws what it drew before.
  void EnterDemoMode();
  void SelectNavTag(std::wstring_view tag);
  // The deep link runs LATE, not from the constructor: EnterDemoMode runs before
  // any layout pass (Content().ActualWidth() is 0, so ApplyBreakpoint has never
  // written the layout) and before App::OnLaunched resizes to 1560x900, and
  // NavigationView re-asserts the markup's IsSelected when it loads. Draining it
  // from the first SizeChanged puts it after all three.
  void DrainDeepLink();

  urmsg::demo::DemoOptions options_{};
  bool advanced_ = false;
  std::wstring currentTag_ = L"chats";
  winrt::Microsoft::UI::Xaml::FrameworkElement currentPage_{nullptr};
  urmsg::demo::DeepLink pendingLink_{};
  bool pendingLinkArmed_ = false;

  urnw::WindowReveal reveal_;
  urnw::kit::PaneSearchRow search_{};
  // The search empty state (d3 2.5), built only under --demo where the filter
  // is wired; null on a normal launch and SetSearchEmptyVisible no-ops.
  // searchEmptyShown_ is the TARGET state, so a late exit-fade Completed
  // handler never collapses a module that was re-shown mid-fade.
  winrt::Microsoft::UI::Xaml::Controls::Grid searchEmpty_{nullptr};
  bool searchEmptyShown_ = false;
  // Empty on a non-demo launch: BuildConversationList only fills it under --demo.
  urmsg::views::ConversationListView list_{};
  // -1 until something is selected. Held on the window because the window is
  // what will also drive the thread and the rail.
  int selectedConversation_ = -1;
  // The whole layout answer, not one bool: three thresholds now (list beside
  // thread at 1000, rail at 1500, strip at 560 of HEIGHT), all in CONTENT-root
  // dips, which is what ActualWidth/ActualHeight of Content() report.
  urmsg::demo::Layout layout_{};
  // The two layout decisions LayoutFor does not carry: the list-width step
  // stays OUT of the gate-asserted Layout struct (d3 section 4) and the nav
  // pane's docked/overlay state is the platform's own threshold, not a demo
  // constant. Tracked beside layout_ so ApplyBreakpoint's early-out cannot
  // skip a real change in either.
  double listWidth_ = 0.0;
  bool navDocked_ = false;
  // Whether ApplyBreakpoint has ever actually WRITTEN the layout. Without it,
  // the first pass early-outs whenever the initial size is narrow (layout_ is
  // already all-false), and the window is only correct because the markup
  // defaults happen to spell the narrow state - an invariant nothing enforces,
  // living in two files. The VPN client carries the same flag for the same
  // reason. It is also what DrainDeepLink waits on.
  bool layoutApplied_ = false;
};

}  // namespace winrt::URmessage::implementation

namespace winrt::URmessage::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::URmessage::factory_implementation
