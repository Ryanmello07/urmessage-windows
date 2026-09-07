// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <array>
#include <utility>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.Foundation.h>

#include "Demo/AdvancedMode.h"
#include "Demo/DemoShellState.h"
#include "Demo/DemoSwitches.h"
#include "Localization.h"
#include "Log.h"
#include "Strings.h"
#include "UrColors.h"
#include "UrMotion.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::URmessage::implementation {
namespace {

// urnw::Localized() returns std::wstring; the UrComponents builders take
// `winrt::hstring const&` and NOT winrt::param::hstring, so there is no
// implicit conversion for them the way there is for TextBlock().Text(). One
// named adapter rather than a hstring{...} at forty call sites. Named Loc, not
// L: L is the wide-string-literal prefix and this file uses both.
winrt::hstring Loc(std::string_view key) { return winrt::hstring{urnw::Localized(key)}; }

// Demo-only labels. Strings/en/Resources.resw is GENERATED from the separate
// urnetwork/localizations repository (Localization.h:3-4), so a demo surface
// cannot add keys to it and must not hand-edit it. These literals ship only
// behind --demo and sit in one block for the same reason the demo world is one
// directory: the diff that deletes the demo must be short.
constexpr const wchar_t* kDemoNavNetwork = L"Network";
constexpr const wchar_t* kDemoNavDeveloper = L"Developer";
constexpr const wchar_t* kDemoWatermark = L"DEMO";

// The conversation-list placeholder.
//
// THIS IS SAMPLE DATA AND IT IS DECLARED HERE, IN ONE BLOCK, ON PURPOSE. There
// is no messaging protocol behind this window yet: nothing is stored, nothing
// is fetched, and nobody is on the other end of any of these. Keeping the fake
// rows in a single named table means the day a real store arrives, the diff
// that removes them is one hunk and there is no second copy hiding in a helper.
//
// The names are deliberately obvious placeholders rather than plausible ones —
// a screenshot of this window must not be mistakable for a screenshot of a
// working messenger.
struct SampleConversation {
  const wchar_t* title;
  const wchar_t* preview;
  const wchar_t* when;
};

constexpr std::array kSampleConversations{
    SampleConversation{L"Sample conversation 1", L"No message store is wired up yet", L"now"},
    SampleConversation{L"Sample conversation 2", L"Rows come from kit::MakePaneTwoLineRowButton", L"12:04"},
    SampleConversation{L"Sample conversation 3", L"One row height per list, by construction", L"11:58"},
    SampleConversation{L"Sample conversation 4", L"Pane model: no radius, no margin, no shadow", L"Tue"},
    SampleConversation{L"Sample conversation 5", L"Separation is a hairline and a hover fill", L"Tue"},
    SampleConversation{L"Sample conversation 6", L"The search row above is kit::MakePaneSearchRow", L"Mon"},
    SampleConversation{L"Sample conversation 7", L"Scroll to prove the rows do not drift", L"Mon"},
    SampleConversation{L"Sample conversation 8", L"Placeholder", L"Sun"},
    SampleConversation{L"Sample conversation 9", L"Placeholder", L"Sun"},
    SampleConversation{L"Sample conversation 10", L"Placeholder", L"Sat"},
};

}  // namespace

MainWindow::MainWindow() {
  InitializeComponent();

  // The window chrome: content extends under the system caption buttons, and
  // AppTitleBar is the DRAG region. Naming a drag region is what separates an
  // actual title bar from a wordmark drawn where one would be. The rest of the
  // native shell (caption-button colours, size, placement) needs the HWND and
  // is applied by urnw::shell::ApplyNativeShell from App::OnLaunched.
  ExtendsContentIntoTitleBar(true);
  SetTitleBar(AppTitleBar());

  options_ = urmsg::demo::ParseDemoOptions();
  ApplyStrings();
  // Seeded before anything can navigate: CrossfadePageSwap with a null outgoing
  // fades the incoming page in and never collapses the old one, so both would be
  // drawn on top of each other.
  currentPage_ = ChatsPage();
  if (options_.enabled) {
    EnterDemoMode();
  } else {
    BuildConversationList();
  }

  // The window reveal: bind now that the content tree exists, then arm BEFORE
  // Activate() so the first composed frame is already the start pose rather
  // than the settled one corrected a frame later. No tray icon yet, so there is
  // no anchor to spring from — nullopt gives the plain centred scale, which is
  // the third of WindowReveal's three documented fallbacks and never fails.
  const FrameworkElement listRing = options_.enabled
                                        ? ListHost().as<FrameworkElement>()
                                        : ConversationList().as<FrameworkElement>();
  reveal_.Bind(WindowPlate(), RevealRoot(),
               {
                   {ListPaneTitle(), 0},
                   {listRing, 0},
                   {HomeNav(), 40},
                   {AppTitleBar(), 80},
               });
  reveal_.Arm(/*enabled=*/true, std::nullopt, RECT{});

  // The responsive switch. SizeChanged on the window's own content root fires
  // on the first layout pass, so this also seeds the initial state; the handler
  // is cheap on the resizes that do not cross the breakpoint.
  if (auto root = Content().try_as<FrameworkElement>()) {
    root.SizeChanged([weak = get_weak()](auto const&, auto const&) {
      auto self = weak.get();
      if (!self) return;
      self->ApplyBreakpoint();
      // AFTER ApplyBreakpoint, never before: the deep link's message half needs
      // the rail to exist, and the rail is decided by the line above.
      self->DrainDeepLink();
    });
  }
  ApplyBreakpoint();
  urnw::LogInfo("window: main window constructed");
}

void MainWindow::StartReveal() { reveal_.Start(); }

void MainWindow::ApplyStrings() {
  // The wordmark. app_name is also what Startup's ResourceProbe asks for, so if
  // this renders as the literal "app_name" the resource stack did not load and
  // the log already says so.
  BrandText().Text(Loc("app_name"));
  Title(Loc("app_name"));

  ChatsNavItem().Content(box_value(Loc("nav_chats")));
  ContactsNavItem().Content(box_value(Loc("nav_contacts")));
  SettingsNavItem().Content(box_value(Loc("nav_settings")));

  ListPaneTitle().Text(Loc("pane_conversations"));
  ThreadPaneTitle().Text(Loc("pane_thread"));
  HomeNav().Header(box_value(Loc("nav_chats")));

  // The thread pane's empty state: ONE muted line centred in a full-height
  // pane, not a card. A card inside a pane is two edges 16px apart, which is
  // the reading the pane model exists to delete.
  ThreadBody().Children().Clear();
  ThreadBody().Children().Append(
      urnw::kit::MakePaneEmptyLine(Loc("thread_none_selected")));

  StubBody().Children().Clear();
  StubBody().Children().Append(
      urnw::kit::MakePaneEmptyLine(Loc("destination_not_built")));
}

void MainWindow::BuildConversationList() {
  // The search field sits on the pane's own 40px row metrics with the row's
  // bottom hairline, so it reads as part of the column rather than as a control
  // dropped on top of it.
  search_ = urnw::kit::MakePaneSearchRow(Loc("search_conversations"));
  SearchHost().Children().Clear();
  SearchHost().Children().Append(search_.root);

  auto list = ConversationList().Children();
  list.Clear();

  // A group header on the 28px rhythm, then the rows. Both come out of the kit,
  // which is what makes a list built in code and a pane declared in markup the
  // same pane.
  auto group = urnw::kit::MakePaneGroupHeader(
      Loc("group_recent"),
      winrt::to_hstring(static_cast<int>(kSampleConversations.size())));
  list.Append(group.root);

  for (auto const& sample : kSampleConversations) {
    auto row = urnw::kit::MakePaneTwoLineRowButton(sample.title, sample.preview);
    row.value.Text(sample.when);
    list.Append(row.root);
  }

  ListPaneCount().Text(winrt::to_hstring(static_cast<int>(kSampleConversations.size())));
  urnw::LogInfo("window: conversation list built with {} placeholder rows",
                kSampleConversations.size());
}

void MainWindow::EnterDemoMode() {
  const auto link = urmsg::demo::DeepLinkFor(options_.screen);

  // Advanced Mode resolves FIRST: the Developer nav item and every view built
  // below depends on it. --demo-advanced and a --demo=developer deep link are
  // both session-only and write nothing (AdvancedMode.h).
  urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced);
  advanced_ = urmsg::AdvancedModeEnabled();

  // The shipped scaffold steps aside; the hosts take over.
  ListScaffold().Visibility(Visibility::Collapsed);
  ThreadPane().Visibility(Visibility::Collapsed);
  ListHost().Visibility(Visibility::Visible);

  NetworkNavItem().Content(box_value(hstring{kDemoNavNetwork}));
  DeveloperNavItem().Content(box_value(hstring{kDemoNavDeveloper}));
  NetworkNavItem().Visibility(Visibility::Visible);
  DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
                                          : Visibility::Collapsed);

  DemoChipText().Text(kDemoWatermark);
  DemoChip().Visibility(options_.watermark ? Visibility::Visible
                                           : Visibility::Collapsed);

  // Armed, not run. See DrainDeepLink.
  pendingLink_ = link;
  pendingLinkArmed_ = true;

  urnw::LogInfo("window: demo mode on (screen tag {}, autoplay {}, advanced {}, watermark {})",
                urnw::Narrow(std::wstring{link.navTag}), options_.autoplay, advanced_,
                options_.watermark);
}

void MainWindow::SelectNavTag(std::wstring_view tag) {
  // Setting SelectedItem raises SelectionChanged, so the destination swap runs
  // through the one path OnNavSelectionChanged already owns.
  auto match = [&](auto const& items) -> bool {
    for (auto const& entry : items) {
      auto item = entry.template try_as<NavigationViewItem>();
      if (!item) continue;
      auto itemTag = item.Tag().template try_as<hstring>();
      if (itemTag && std::wstring_view{*itemTag} == tag) {
        HomeNav().SelectedItem(item);
        return true;
      }
    }
    return false;
  };
  if (!match(HomeNav().MenuItems())) match(HomeNav().FooterMenuItems());
}

void MainWindow::DrainDeepLink() {
  // One-shot, and only once the layout has actually been written. breakpoint
  // state, window size and NavigationView's own load-time selection are all
  // settled by the first SizeChanged; running from the constructor instead put
  // the nav selection before NavigationView loaded (where the markup's
  // IsSelected="True" on ChatsNavItem wins) and the message selection before the
  // rail existed.
  if (!pendingLinkArmed_ || !breakpointApplied_) return;
  pendingLinkArmed_ = false;
  SelectNavTag(pendingLink_.navTag);
  urnw::LogInfo("window: demo deep link -> tag={} conversation={} message={}",
                urnw::Narrow(std::wstring{pendingLink_.navTag}),
                pendingLink_.selectConversation, pendingLink_.selectMessage);
}

void MainWindow::ApplyBreakpoint() {
  auto root = Content().try_as<FrameworkElement>();
  if (!root) return;
  const double width = root.ActualWidth();
  if (width <= 0) return;

  const bool wide = urnw::kit::kWideBreakpointDip <= width;
  // breakpointApplied_, and not just `wide == wide_`: on the very first pass
  // wide_ is already false, so a narrow start early-outs having written
  // NOTHING, and the window is then correct only because the markup defaults
  // happen to spell the narrow state — an invariant living in two files that
  // nothing enforces. Write it once for real, then early-out on genuine no-ops.
  if (breakpointApplied_ && wide == wide_) return;
  wide_ = wide;
  breakpointApplied_ = true;

  // Wide: a fixed 320dip list rail and the thread takes what is left — the
  // reading a two-pane messenger wants, and the reason the list column is not a
  // star weight (a proportional list column grows into dead space on a 2000dip
  // window). Narrow: the list IS the window and the thread does not exist.
  ListColumn().Width(wide ? GridLengthHelper::FromPixels(320)
                          : GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  ThreadColumn().Width(wide ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                            : GridLengthHelper::FromPixels(0));
  const auto paneVisibility = wide ? Visibility::Visible : Visibility::Collapsed;
  PaneRule().Visibility(paneVisibility);
  ThreadPane().Visibility(paneVisibility);
  urnw::LogInfo("window: breakpoint -> {} ({:.0f} dip)", wide ? "wide" : "narrow", width);
}

void MainWindow::ShowDestination(std::wstring_view tag) {
  // There is no Frame in this window - sibling Grids toggled by Visibility - so
  // the page change goes through motion::CrossfadePageSwap (UrMotion.h:120), the
  // app's standing replacement for NavigationTransitionInfo and design doc 7's
  // page-change row. It owns the Visibility of BOTH elements and falls back to
  // an instant swap when ShouldAnimate() is false, so nothing here writes
  // Visibility itself.
  FrameworkElement incoming = StubPage();
  hstring header = Loc("nav_settings");

  if (tag == L"chats") {
    incoming = ChatsPage();
    header = Loc("nav_chats");
  } else if (tag == L"contacts") {
    incoming = StubPage();
    header = Loc("nav_contacts");
  } else if (tag == L"network" && options_.enabled) {
    incoming = NetworkHost();
    header = hstring{kDemoNavNetwork};
  } else if (tag == L"developer" && options_.enabled) {
    incoming = DeveloperHost();
    header = hstring{kDemoNavDeveloper};
  } else if (tag == L"settings" && options_.enabled) {
    incoming = SettingsHost();
    header = Loc("nav_settings");
  }

  HomeNav().Header(box_value(header));
  if (incoming == StubPage()) StubPaneTitle().Text(header);

  currentTag_ = std::wstring{tag};
  urnw::motion::CrossfadePageSwap(currentPage_, incoming);
  currentPage_ = incoming;
}

void MainWindow::OnNavSelectionChanged(NavigationView const&,
                                       NavigationViewSelectionChangedEventArgs const& args) {
  auto item = args.SelectedItem().try_as<NavigationViewItem>();
  if (!item) return;
  auto tag = item.Tag().try_as<hstring>();
  if (!tag) return;
  urnw::LogInfo("window: destination -> {}", winrt::to_string(*tag));
  ShowDestination(std::wstring_view{*tag});
}

}  // namespace winrt::URmessage::implementation
