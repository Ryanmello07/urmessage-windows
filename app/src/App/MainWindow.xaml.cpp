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
#include "Demo/DemoWorld.h"
#include "Localization.h"
#include "Log.h"
#include "Strings.h"
#include "UrColors.h"
#include "UrMotion.h"
#include "Views/InspectRailFields.h"
#include "Views/InspectRailView.h"
#include "Views/ThreadView.h"

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
  // EnterDemoMode arms Advanced Mode, the DEMO chip and the deep link.
  // Previously this ordering was ALSO load-bearing for which of
  // ListScaffold/ListHost BuildConversationList had to fill; that split is
  // gone (EnterDemoMode no longer collapses ListScaffold -- see its own
  // comment -- and the demo's rows land in the same ConversationList the
  // shipped placeholder rows use), so nothing below depends on this ordering
  // for the list any more. Left as-is regardless: EnterDemoMode's other
  // effects are naturally wanted before the list that can read them exists.
  if (options_.enabled) {
    EnterDemoMode();
  }
  // BEFORE BuildConversationList, and that ordering is load-bearing. Under
  // --demo that function ends by calling OnConversationSelected(0) - the one
  // selection this constructor performs, since the agent may not synthesise a
  // click - and OnConversationSelected is where the wiring surface will drive
  // the rail from. A rail built after it would be a rail that call cannot
  // reach, which is this project's standing failure shape.
  BuildInspectRail();
  BuildConversationList();
  BuildThread();

  // The window reveal: bind now that the content tree exists, then arm BEFORE
  // Activate() so the first composed frame is already the start pose rather
  // than the settled one corrected a frame later. No tray icon yet, so there is
  // no anchor to spring from — nullopt gives the plain centred scale, which is
  // the third of WindowReveal's three documented fallbacks and never fails.
  //
  // ConversationList unconditionally, not a demo/non-demo ternary: the demo's
  // rows land there too now (BuildConversationList), so it is the one element
  // that is ever actually visible as the list ring in either mode. ListHost,
  // which this used to switch to under --demo, is never made visible any more.
  const FrameworkElement listRing = ConversationList().as<FrameworkElement>();
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

void MainWindow::StartReveal() {
  reveal_.Start();
  // After Activate(), beside the window reveal rather than inside it: the reveal
  // ramps ConversationList itself (the parent), this ramps its rows. Two
  // opacities on two elements multiply; they do not fight. A no-op on a
  // non-demo launch, where list_.rows is empty.
  urmsg::views::AnimateConversationListEntrance(list_);
}

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

  // --demo replaces the placeholder rows with the demo world. Without it, every
  // line below is byte-for-byte what it was: design doc 8, "without --demo the
  // app behaves exactly as it does today", which step 12 verifies in pixels.
  if (urmsg::demo::ParseDemoOptions().enabled) {
    auto const& world = urmsg::demo::GetWorld();
    list_ = urmsg::views::MakeConversationList(world, [weak = get_weak()](int index) {
      if (auto self = weak.get()) self->OnConversationSelected(index);
    });
    // `list` (ConversationList), now that EnterDemoMode no longer collapses
    // ListScaffold: the demo's rows land in the SAME scaffold -- header,
    // search row and all -- that the shipped placeholder rows below use. This
    // was routed through the separate ListHost for a while (see git history);
    // that detour existed only because ListScaffold was collapsed, and
    // restoring the scaffold restores L2's originally intended target too.
    list.Append(list_.root);
    ListPaneCount().Text(winrt::to_hstring(static_cast<int>(world.conversations.size())));
    urnw::LogInfo("window: demo conversation list built with {} rows",
                  world.conversations.size());
    // TextChanged, not KeyDown: it fires for paste, for undo and for a
    // programmatic Text() write, and the filter must be true of the box's
    // CONTENT rather than of the last key that touched it.
    search_.box.TextChanged([weak = get_weak()](winrt::Windows::Foundation::IInspectable const&,
                                                TextChangedEventArgs const&) {
      if (auto self = weak.get()) self->ApplyConversationFilter();
    });

    // Contract 5: Advanced Mode has ONE owner and no view reads the preference.
    // This is the initial application; the live subscription that re-calls this
    // on every toggle is registered by the wiring surface, which owns
    // OnAdvancedModeChanged.
    urmsg::views::SetConversationListAdvanced(list_, urmsg::AdvancedModeEnabled());

    // The agent may not synthesise input, so a selection that only ever happens
    // on a click is a state no capture can reach. Contract 2 already requires
    // --demo=inspect to pre-select conversation 0; --demo=chats does the same so
    // the three selection channels are visible at launch.
    OnConversationSelected(0);
    return;
  }

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

void MainWindow::BuildThread() {
  // Gated on --demo. Without it this window is the 480x760 shell it is today,
  // where ApplyBreakpoint collapses the thread column below 1000 dip anyway,
  // and mounting fabricated messages into a normal launch would change the one
  // thing design D7 says not to change.
  //
  // options_, NOT a second ParseDemoOptions() call. options_ is parsed once in
  // the constructor and it is what ApplyBreakpoint reads to decide WHICH of the
  // two thread surfaces is visible (ThreadPane vs ThreadHost). Re-parsing here
  // would give the mount and the visibility two sources of truth for the one
  // flag, and "mounted into the collapsed twin" is the failure this project has
  // shipped four times. One flag, one read.
  if (!options_.enabled) return;

  auto const& world = urmsg::demo::GetWorld();
  if (world.conversations.empty()) return;
  auto const& open = world.conversations.front();

  // Both callbacks now DO the selection half of their job: SetThreadSelectedMessage
  // owns the bubble edge (T5), so a click paints the accent outline and a click
  // in empty thread space takes it away. What is still a seam is the RAIL - it
  // does not exist yet, so nothing opens beside the selected bubble; the rail
  // task adds that here without touching this file's structure.
  //
  // get_weak(), not `this`: MakeThread stores these for the window's life, and a
  // raw capture would outlive a closed window.
  thread_ = urmsg::views::MakeThread(
      [weak = get_weak()](std::wstring id) {
        auto self = weak.get();
        if (!self) return;
        urmsg::views::SetThreadSelectedMessage(self->thread_, id);
        urnw::LogInfo("thread: bubble selected {}", winrt::to_string(id));
      },
      [weak = get_weak()]() {
        auto self = weak.get();
        if (!self) return;
        urmsg::views::SetThreadSelectedMessage(self->thread_, L"");
        urnw::LogInfo("thread: deselected");
      });

  // ThreadHost, NOT ThreadBody. ApplyBreakpoint gives exactly one of the two
  // thread surfaces to a run: under --demo it collapses ThreadPane outright
  // (EnterDemoMode does too) and shows ThreadHost, so anything appended to
  // ThreadBody here would compile, log and render ZERO pixels. ThreadHost is a
  // bare UrPaneStyle Grid with no header row, which is why nothing below writes
  // a pane title: ApplyStrings already set ThreadPaneTitle once to
  // Loc("pane_thread"), and UrPaneTitleStyle is the letterspaced CHROME voice -
  // a mixed-case personal name set in it reads wrong. The conversation's name
  // belongs to the wiring task that gives this host a header.
  ThreadHost().Children().Clear();
  ThreadHost().Children().Append(thread_.root);
  urmsg::views::SetThreadConversation(thread_, open);
  urnw::LogInfo("thread: mounted {} rows, {} bubbles", open.rows.size(),
                thread_.bubbles.size());
}

void MainWindow::BuildInspectRail() {
  // options_, NOT a second ParseDemoOptions() call - the same rule BuildThread
  // states: ApplyBreakpoint reads options_ to decide whether the rail column
  // exists at all, so a second read here would give the mount and the
  // visibility two sources of truth for one flag.
  if (!options_.enabled) return;

  rail_ = urmsg::views::MakeInspectRail();

  // RailHost, and NOT a host of the rail task's own. RailHost is already the
  // Grid.Column=4 occupant of ChatsPage (MainWindow.xaml), it is already what
  // ApplyBreakpoint shows and hides with RailColumn and RailRule, and a second
  // Grid in that cell would mean the shell displayed its empty one while this
  // filled the other - "mounted into the collapsed twin", the failure this
  // window has already shipped four times. Nothing in this task writes
  // RailColumn().Width(), RailRule().Visibility() or RailHost().Visibility():
  // ApplyBreakpoint is their one writer, from urmsg::demo::kRailWidthDip and
  // kRailBreakpointDip.
  RailHost().Children().Clear();
  RailHost().Children().Append(rail_.root);

  auto const& world = urmsg::demo::GetWorld();
  if (world.conversations.empty()) {
    // DemoWorld's --diagnose invariant 1 requires 8 conversations, so this is a
    // DemoWorld failure and --diagnose already names it. The rail stays empty
    // rather than inventing a subject.
    urnw::LogWarn("rail: not populated - the demo world has no conversations");
    return;
  }

  // Conversation 0 is what --demo opens on (fixed contract 2), and it is the
  // same conversation BuildConversationList selects a moment later.
  auto const& conv = world.conversations.front();

  // ONE initial mode, never both.
  //
  // Calling SetInspectRailConversation and then SetInspectRailMessage back to
  // back would run two crossfades over the SAME two elements in OPPOSITE
  // directions inside one synchronous block: RunCrossfade sets
  // incoming.Opacity(0) and begins a storyboard, so both scrollers end up driven
  // by two clocks at once and each Completed handler collapses its outgoing by
  // reading that contested Opacity. The end state would be a race - and it is
  // the exact frame every --demo=inspect capture depends on.
  //
  // --demo=inspect is a STATE, not a screen (design 8): the thread with a
  // message pre-selected and the rail already in message mode, which is the
  // state a screenshot needs and the state no click can reach for an agent.
  // PickInspectMessage is shared with the thread's selection outline, so both
  // land on the same bubble by construction.
  if (options_.screen == urmsg::demo::DemoScreen::Inspect) {
    if (auto const* picked = urmsg::views::PickInspectMessage(conv)) {
      urmsg::views::SetInspectRailMessage(rail_, conv, *picked);
      return;
    }
    urnw::LogWarn("rail: --demo=inspect but conversation 0 has no message row");
  }
  urmsg::views::SetInspectRailConversation(rail_, conv);
}

void MainWindow::OnConversationSelected(int index) {
  auto const& world = urmsg::demo::GetWorld();
  if (index < 0 || world.conversations.size() <= static_cast<std::size_t>(index)) return;
  selectedConversation_ = index;
  urmsg::views::SetConversationSelected(list_, index);

  // The pane TITLE stays "THREAD": UrPaneTitleStyle is the letterspaced chrome
  // voice (App.xaml:838-847) and a mixed-case name set in it reads wrong. The
  // conversation's name goes in the BODY as one muted centred line -- an
  // acknowledged interim state, replaced wholesale when the ThreadView surface
  // lands. A centred muted line is an empty state, not an affordance, so it does
  // not read as something that looks live and does nothing (design 9.1).
  ThreadBody().Children().Clear();
  ThreadBody().Children().Append(
      urnw::kit::MakePaneEmptyLine(winrt::hstring{world.conversations[index].name}));
  urnw::LogInfo("window: conversation {} selected ({})", index,
                winrt::to_string(winrt::hstring{world.conversations[index].name}));
}

void MainWindow::ApplyConversationFilter() {
  if (!search_.box || !list_.root) return;
  auto const& world = urmsg::demo::GetWorld();
  const std::wstring query{search_.box.Text()};
  const std::size_t visible =
      urmsg::views::ApplyConversationListFilter(list_, world, query);
  // The ONE count of "how many rows are on screen". The RECENT group header
  // deliberately carries no count (MakeConversationList): a second readout this
  // function did not update would be a readout that had stopped being true.
  ListPaneCount().Text(winrt::to_hstring(static_cast<int>(visible)));
}

void MainWindow::EnterDemoMode() {
  const auto link = urmsg::demo::DeepLinkFor(options_.screen);

  // Advanced Mode resolves FIRST: the Developer nav item and every view built
  // below depends on it. --demo-advanced and a --demo=developer deep link are
  // both session-only and write nothing (AdvancedMode.h).
  urmsg::InitAdvancedMode(options_.advanced || link.forceAdvanced);
  advanced_ = urmsg::AdvancedModeEnabled();

  // The shipped THREAD scaffold steps aside for the demo's ThreadHost; the
  // LIST scaffold does not, and ListHost is never made visible. Unlike the
  // thread (a genuinely different view the demo swaps in), the list pane's
  // shape does not change under --demo -- BuildConversationList fills the
  // same ConversationList/SearchHost/ListPaneCount either way -- so there is
  // no second list host to switch to, and collapsing ListScaffold here used
  // to take the search row and the pane header count down with it for no
  // reason tied to the demo/non-demo split itself (fixed: see
  // BuildConversationList's comment on the demo branch).
  ThreadPane().Visibility(Visibility::Collapsed);

  // Content only, here. The Visibility flip is deferred to DrainDeepLink (fix
  // round 1) and paired there with a PaneDisplayMode nudge - see that function
  // for why. Isolated by disabling each call above in turn: Content() alone
  // never triggered the regression, only Visibility() did.
  NetworkNavItem().Content(box_value(hstring{kDemoNavNetwork}));
  DeveloperNavItem().Content(box_value(hstring{kDemoNavDeveloper}));

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
  if (!pendingLinkArmed_ || !layoutApplied_) return;
  pendingLinkArmed_ = false;

  // The demo nav items become visible HERE, not in EnterDemoMode (fix round 1).
  // Flipping a NavigationViewItem from Collapsed to Visible at ANY point -
  // constructor or here, before or after the window reaches its final size -
  // corrupts NavigationView's own Auto pane-mode resolution: it renders
  // icon-only from that moment on, permanently, and does not recover on a
  // later resize either (checked against the harness's own live 1200x800
  // resize in the same run). Isolated with three throwaway builds: (1)
  // dropping both Visibility() calls kept the untouched items labelled, (2)
  // re-adding only NetworkNavItem's reproduced icon-only for every item
  // including ones never touched, (3) moving the calls here instead of
  // EnterDemoMode alone did not help. What DOES recover it is forcing
  // NavigationView to fully re-run its Auto adaptive logic immediately after:
  // stepping PaneDisplayMode away from Auto and back re-measures against the
  // CURRENT item set and CURRENT window width, rather than whatever it cached
  // before the Visibility flip.
  //
  // WORKAROUND, not a design choice - fix round 2. This is a platform defect in
  // Windows App SDK 2.2.0 (the version this build logs under "built against"
  // at startup): a NavigationViewItem's Collapsed -> Visible transition
  // corrupts NavigationView's Auto pane-mode resolution in that version,
  // independent of timing (test (1)/(2) above already ruled out both "set
  // Visibility in XAML from the start" and "build the items only after the
  // demo options are known" as fixes, since even a pre-layout, pre-any-window
  // flip in the constructor reproduced it). RE-TEST SIGNAL: if a future
  // Windows App SDK bump makes this cycle look removable, do not delete it
  // because it looks redundant - delete it, then rebuild and confirm the nav
  // still shows TEXT LABELS beside Chats/Contacts/Network/Developer/Settings
  // at 1560x900 under `--demo=network --demo-advanced`. If it does not, the
  // defect is still present and the cycle stays. UNTRIED CLEANER ROUTE, noted
  // for whoever picks this up next but NOT attempted and NOT proven: building
  // NetworkNavItem/DeveloperNavItem in code and Append/InsertAt-ing them into
  // HomeNav().MenuItems()/FooterMenuItems() only inside EnterDemoMode, instead
  // of declaring them Collapsed in XAML and toggling Visibility here, would
  // never perform the corrupting transition at all - a candidate, not a
  // verified fix.
  NetworkNavItem().Visibility(Visibility::Visible);
  DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
                                          : Visibility::Collapsed);
  HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact);
  HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);

  SelectNavTag(pendingLink_.navTag);

  // The MESSAGE half of the deep link, which used to be logged and not done.
  // DemoShellState.h defines --demo=inspect as "thread PLUS a message
  // pre-selected", and DemoWorld::kInspectTargetRowId names it: c0-r12, an
  // outgoing row in state Read.
  //
  // IT HAS TO HAPPEN HERE, not in the constructor. A property written during
  // construction is written against a template that has not been applied and a
  // tree that has not been laid out, so the border it sets never repaints. This
  // runs from the content root's first SizeChanged, after ApplyBreakpoint - i.e.
  // post-layout on a realized tree - which is the same reason the nav selection
  // was moved here.
  if (pendingLink_.selectMessage && thread_.root)
    urmsg::views::SetThreadSelectedMessage(thread_, urmsg::demo::kInspectTargetRowId);

  // The AMBIENT-APPEND seed (T6). AppendThreadRow is design §9.2's one entry
  // point for ambient activity and the loop that will drive it is not built
  // (design §2), so without a seed the append path is unreachable in a running
  // window: its one-reading-per-run rule could be asserted in --diagnose and
  // never SEEN, and every capture of this surface would photograph a column
  // nothing had ever been appended to.
  //
  // So --demo-autoplay sends TWO rows through the REAL function, from the place
  // the real loop will call it from — here, holding the ThreadView the
  // contract's setters take by reference. Two, because the two halves of what
  // AppendThreadRow decides need different rows to be visible at all:
  //
  //   the OUTGOING one lands on c0, whose last two rows are the 12:09 Failed
  //   one and the 12:11 Pending one. 12:11 stops being last of its run and must
  //   LOSE its reading; 12:09 keeps its own because it FAILED. That is the
  //   delivery-cluster half.
  //
  //   the INCOMING one lands under that outgoing row, so it STARTS a run in a
  //   group and must draw a sender NAME and an IDENTICON. That is the
  //   sender-header half, and it is the half that rendered blank until fix
  //   round 1 — AppendThreadRow hard-coded showSenderHeader to false. It also
  //   leaves the outgoing row's cluster alone, because an incoming row does not
  //   end an outgoing run.
  //
  // HERE and not in the constructor, for the same reason the message selection
  // above is here: this runs from the content root's first SizeChanged, i.e.
  // post-layout on a realized tree, which is the only place a bubble entrance
  // can actually play. DrainDeepLink is one-shot, so this fires exactly once.
  //
  // The demo WORLD is not mutated: MutableWorld() belongs to that ambient loop
  // (DemoWorld.h) and this is a capture seed, not the loop.
  if (options_.autoplay && options_.screen == urmsg::demo::DemoScreen::Thread &&
      thread_.root) {
    urmsg::demo::MessageRow out{};
    out.kind = urmsg::demo::RowKind::Message;
    out.id = L"c0-ambient-1";
    out.body = L"Measurements are in the branch now.";
    out.timeLabel = L"12:14";
    out.outgoing = true;
    out.state = urmsg::demo::DeliveryState::Sent;
    // Contract §1 guarantees this is populated on every row. On an OUTGOING
    // row nothing reads it — BubbleAutomationName says "You" — but a row that
    // left it empty would be the first in the world to do so.
    out.inspect.senderDisplayName = L"You";
    urmsg::views::AppendThreadRow(thread_, out);

    // The sender is LIFTED from the conversation rather than invented: the
    // identicon is a hash of senderKey, so a made-up key would draw a person
    // who appears nowhere else in the thread and the capture could not be
    // checked against anything. The last incoming row of c0 is Elena Vasquez at
    // 12:02, whose identicon is still on screen a few rows up — so "the same
    // face twice" is the concrete thing to look for.
    auto const& world = urmsg::demo::GetWorld();
    urmsg::demo::MessageRow const* speaker = nullptr;
    if (!world.conversations.empty())
      for (auto const& r : world.conversations.front().rows)
        if (r.kind == urmsg::demo::RowKind::Message && !r.outgoing) speaker = &r;
    if (speaker) {
      urmsg::demo::MessageRow in{};
      in.kind = urmsg::demo::RowKind::Message;
      in.id = L"c0-ambient-2";
      in.senderKey = speaker->senderKey;
      // senderName is what the bubble PRINTS; inspect.senderDisplayName is what
      // a screen reader gets on a continuation. Both, and the same words.
      in.senderName = speaker->inspect.senderDisplayName;
      in.inspect.senderDisplayName = speaker->inspect.senderDisplayName;
      in.body = L"Got them, thanks. Reviewing now.";
      in.timeLabel = L"12:16";
      in.outgoing = false;
      in.state = urmsg::demo::DeliveryState::Sent;
      urmsg::views::AppendThreadRow(thread_, in);
    }
    urnw::LogInfo("thread: ambient seed appended {} rows -> {} bubbles", speaker ? 2 : 1,
                  thread_.bubbles.size());
  }

  urnw::LogInfo("window: demo deep link -> tag={} conversation={} message={} (rail={})",
                urnw::Narrow(std::wstring{pendingLink_.navTag}),
                pendingLink_.selectConversation, pendingLink_.selectMessage,
                layout_.rail);
}

void MainWindow::ApplyBreakpoint() {
  auto root = Content().try_as<FrameworkElement>();
  if (!root) return;
  // CONTENT-root dips, not window dips: the frame costs about 14 of them (the
  // shipped log has 466 for a 480-dip window and 1186 for a 1200-dip one). The
  // thresholds in DemoShellState.h are content thresholds for this reason, and
  // the log line below says `content` so the two can be compared.
  const double width = root.ActualWidth();
  const double height = root.ActualHeight();
  if (width <= 0 || height <= 0) return;

  const auto next = urmsg::demo::LayoutFor(width, height);
  if (layoutApplied_ && next.wide == layout_.wide && next.rail == layout_.rail &&
      next.strip == layout_.strip)
    return;
  layout_ = next;
  layoutApplied_ = true;

  // Wide: a fixed 320dip list rail and the thread takes what is left. Narrow:
  // the list IS the window and the thread does not exist.
  ListColumn().Width(layout_.wide
                         ? GridLengthHelper::FromPixels(320)
                         : GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  ThreadColumn().Width(layout_.wide
                           ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                           : GridLengthHelper::FromPixels(0));
  const auto threadVisibility =
      layout_.wide ? Visibility::Visible : Visibility::Collapsed;
  PaneRule().Visibility(threadVisibility);
  // Exactly one of the two thread surfaces is ever live: the shipped scaffold,
  // or the demo's ThreadView host.
  ThreadPane().Visibility(options_.enabled ? Visibility::Collapsed : threadVisibility);
  ThreadHost().Visibility(options_.enabled ? threadVisibility : Visibility::Collapsed);

  // The rail is demo-only and exists only at or above kRailBreakpointDip. Below
  // it, message inspect is simply unavailable - no sheet, no fallback, no error
  // (design doc 6.5a).
  const bool rail = options_.enabled && layout_.rail;
  RailColumn().Width(rail ? GridLengthHelper::FromPixels(urmsg::demo::kRailWidthDip)
                          : GridLengthHelper::FromPixels(0));
  const auto railVisibility = rail ? Visibility::Visible : Visibility::Collapsed;
  RailRule().Visibility(railVisibility);
  RailHost().Visibility(railVisibility);

  // The strip is hidden below kStripMinHeightDip of content HEIGHT so it can
  // never eat a readable thread at Spec C 1.2's 480dip minimum.
  StatusStripHost().Visibility(options_.enabled && layout_.strip
                                   ? Visibility::Visible
                                   : Visibility::Collapsed);

  urnw::LogInfo("window: layout wide={} rail={} strip={} (content {:.0f}x{:.0f} dip)",
                layout_.wide, layout_.rail, layout_.strip, width, height);
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
