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
#include "Views/DeveloperView.h"
#include "Views/InspectRailView.h"
#include "Views/NetworkPageView.h"
#include "Views/SettingsView.h"
#include "Views/StatusStripView.h"
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

  // The click graph (design doc 9.1) and the demo's three content views.
  // BuildDemoViews is the SOLE builder of list_, thread_ and rail_ (the d7
  // audit's W5 Step-0 override: the placeholder-era BuildThread /
  // BuildInspectRail / OnConversationSelected are deleted, not kept as second
  // writers). Demo-only for the same reason the old builders were: a normal
  // launch is 480x760 and must behave exactly as it does today (design D7),
  // and building the views would build the demo world on the shipping path.
  //
  // Every one of the Select*/Clear* entry points is reached through a callback
  // a view was CONSTRUCTED with, so MainWindow never walks a view's element
  // tree looking for something to attach to.
  void BuildDemoViews();
  void RebuildConversationList();
  void SelectConversation(int index);
  // By value, not string_view: this is called with selectedMessageId_ and it
  // writes selectedMessageId_. A view aliasing the member it is about to change
  // is a use-after-free one edit away, with no compiler warning.
  void SelectMessage(std::wstring id);
  void ClearMessageSelection();
  // -1 when nothing is open. Derived from the world every time rather than
  // cached: a Conversation const* into World::conversations is invalidated by
  // anything that appends, and ambient activity appends.
  int OpenConversationIndex() const;

  urmsg::views::ThreadView thread_{};
  urmsg::views::InspectRailView rail_{};
  std::wstring openConversationId_;
  std::wstring selectedMessageId_;

  // The Network destination's whole content, built in code into NetworkHost
  // (MainWindow.xaml:282 — the d7 audit's N3 override; there is no
  // NetworkBody). Demo-gated: a normal
  // launch must behave exactly as it does today (design D7/D8), and building
  // the page would build the demo world on the shipping path.
  void BuildNetworkPage();

  urmsg::views::NetworkPageView network_{};

  // The Settings destination's whole content, built in code into SettingsHost
  // (MainWindow.xaml:284 — the d7 audit's A3 override: mount into the existing
  // host, do NOT add a SettingsPage Grid). Demo-gated for the same reason
  // BuildNetworkPage is: the page reads the demo world, so building it on a
  // normal launch would construct that world on the shipping path (design
  // D7/D8).
  void BuildSettings();

  urmsg::views::SettingsView settings_{};

  // The Developer destination's whole content, built in code into
  // DeveloperHost (MainWindow.xaml:286 — the d7 audit's A5 override: mount
  // into the existing host, do NOT add a DeveloperPage Grid). Demo-gated for
  // the same reason BuildSettings is: the page reads the demo world, so
  // building it on a normal launch would construct that world on the
  // shipping path (design D7/D8). Built ONCE here: contract §4 gives the
  // view no Set*Advanced, and the live rebuild on a mode toggle belongs to
  // the wiring task's ONE OnAdvancedModeChanged subscriber (W7, the d7
  // distillation's §1.2 ruling) — this wave registers no subscription.
  void BuildDeveloper();

  urmsg::views::DeveloperView developer_{};

  // The connect indicator (design 6.5, D4). Built ONCE and never rebuilt: the
  // strip is window chrome, so it outlives every destination change. Returns
  // without building anything when the demo is off — design 8: "Without it
  // the app behaves exactly as it does today", and a build with no protocol
  // must not show a message-server hostname as chrome on every launch
  // (design 2, 11). The d7 audit's ownership ruling (resolution alpha) gives
  // the strip group — not the wiring task — the member, the mount and the
  // toggle; the member name stays `statusStrip_`.
  void BuildStatusStrip();

  // Raise or dismiss the strip's preview drawer. The ONLY thing that opens it
  // is an activation of the strip — design 9.2: the autoplay loop "never
  // opens or closes the rail or the drawer", and everything in the demo that
  // is not ambient activity happens because a person clicked it.
  //
  // Click-outside and Escape dismissal are deliberately NOT built, and that
  // is a decision rather than an omission: the strip is a toggle, so the same
  // control both raises and dismisses, it is reachable by Tab and invoked by
  // Enter or Space, and the drawer is chrome rather than a modal — nothing
  // behind it is blocked while it stands. ApplyBreakpoint closes it when the
  // strip collapses, which is the one case where the toggle would otherwise
  // become unreachable. Click-outside would be a RevealRoot-level pointer
  // handler and belongs to whoever owns RevealRoot's input, not to this
  // surface.
  void ToggleStatusDrawer();

  urmsg::views::StatusStripView statusStrip_{};

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
  // Empty on a non-demo launch: BuildDemoViews only fills it under --demo.
  urmsg::views::ConversationListView list_{};
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
