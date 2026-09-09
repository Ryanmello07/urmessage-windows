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
#include "Identicon.h"
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

// NavigationView Auto's overlay threshold (the platform default,
// ExpandedModeThresholdWidth = 640 effective pixels): below it the pane is an
// overlay flyout floating OVER the content, so there is no docked boundary to
// draw a rule on (d3 3.1). ApplyBreakpoint compares content-root dips against
// it - the nav fills the window's full width, so the two measures are the
// same quantity here.
constexpr double kNavOverlayMaxDip = 640.0;

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
  // click - and the selection handler is where the wiring surface will drive the
  // rail from. (The wiring task replaces this file's OnConversationSelected with
  // its own SelectConversation and drives the rail from there.) A rail built
  // after that call would be a rail it cannot reach, which is this project's
  // standing failure shape.
  //
  // *** READ THIS BEFORE GIVING THE RAIL A SECOND WRITER (R3). ***
  //
  // The wiring task installs its own --demo=inspect deep link, in DrainDeepLink.
  // Its ordering is already safe - conversation first, message second - so the
  // hazard is not the ordering. It is this:
  //
  //   DELETE THE --demo=inspect BRANCH IN BuildInspectRail() BELOW WHEN THAT
  //   LANDS. Do not keep both. Two deep-link writers for one state is one too
  //   many: whichever runs last wins, which makes the rail's subject an ordering
  //   accident, and if the two designate different rows it silently parts the
  //   rail from the bubble the thread outlines.
  //
  // WHY NOTHING WOULD CATCH IT. The rail has --diagnose probes for its FIELDS
  // and its DEVICES, both computed from pure functions in InspectRailFields.
  // The MODE decision used to be unreachable the same way - an inline `if` in
  // this winrt translation unit - which is why R4 extracted it:
  // InitialRailMode(screen, conv) is pure now, and InspectRailDeviceProbe
  // asserts Inspect->Message, Chats->Conversation, and
  // Inspect-with-no-message->Conversation (that last clause being the LogWarn
  // arm below, at BuildInspectRail - NOT InspectRailView.cpp, which contains
  // no LogWarn). The probe asserts what the mode SHOULD be, not which write
  // lands last, so it becomes a real gate only alongside the single-writer
  // rule stated above, which is the wiring task's half.
  //
  // And do NOT solve any of this by moving BuildInspectRail() after
  // BuildConversationList(): that reintroduces the unreachable-rail failure this
  // comment was originally written about.
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

  // Compact-rail mode (641-1007epx, icons only) names its destinations on
  // hover (d3 3.2); zero visual change in expanded mode. The tooltip IS the
  // label's string, so the two can never disagree. The demo destinations get
  // theirs in EnterDemoMode beside the Content() writes there.
  Controls::ToolTipService::SetToolTip(ChatsNavItem(), box_value(Loc("nav_chats")));
  Controls::ToolTipService::SetToolTip(ContactsNavItem(), box_value(Loc("nav_contacts")));
  Controls::ToolTipService::SetToolTip(SettingsNavItem(), box_value(Loc("nav_settings")));

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

  // The search row's interactive engineering (d3 2.5), registered for BOTH
  // launches: harmless on the placeholder list, and the two launches stay
  // identical. Focus wakes the glyph muted -> off-white - the same channel
  // nav items use on hover - and deliberately NOT an accent underline:
  // accent is reserved for the send button and selection outlines, and a
  // focused field is neither (G3). Esc clears the box; under --demo the
  // TextChanged handler below does the rest, and on the placeholder list
  // clearing a box nobody filters on is still the least surprising thing
  // Esc can do.
  search_.box.GotFocus([glyph = search_.glyph](auto const&, auto const&) {
    if (glyph) glyph.Foreground(urnw::colors::TextBrush());
  });
  search_.box.LostFocus([glyph = search_.glyph](auto const&, auto const&) {
    if (glyph) glyph.Foreground(urnw::colors::MutedBrush());
  });
  search_.box.KeyDown([](winrt::Windows::Foundation::IInspectable const& sender,
                         Input::KeyRoutedEventArgs const& e) {
    if (e.Key() == winrt::Windows::System::VirtualKey::Escape)
      sender.as<Controls::TextBox>().Text(L"");
  });

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

    // The unread InfoBadge on the Chats nav item (d3 3.2, the wave's
    // optional item): the summed unread, set ONCE here - the world's unread
    // counts never change at runtime, so a live binding would have nothing
    // to do. The badge is neutral by the App.xaml theme overrides
    // (InfoBadgeBackground/Foreground): not accent (reserved), not green
    // (presence), not red (danger). A property set at build, never a
    // Visibility flip - the WASDK 2.2.0 Auto-mode defect DrainDeepLink
    // documents is about Collapsed -> Visible transitions, which this does
    // not perform; the nav is still screenshot-verified after the change.
    int unreadTotal = 0;
    for (auto const& c : world.conversations) unreadTotal += c.unread;
    if (0 < unreadTotal) {
      Controls::InfoBadge badge;
      badge.Value(unreadTotal);
      ChatsNavItem().InfoBadge(badge);
    }
    // TextChanged, not KeyDown: it fires for paste, for undo and for a
    // programmatic Text() write, and the filter must be true of the box's
    // CONTENT rather than of the last key that touched it.
    search_.box.TextChanged([weak = get_weak()](winrt::Windows::Foundation::IInspectable const&,
                                                TextChangedEventArgs const&) {
      if (auto self = weak.get()) self->ApplyConversationFilter();
    });

    // The search empty state (d3 2.5): when the filter returns nothing, the
    // pane says WHY rather than going blank - the identicon lattice (an
    // empty frame where a person-mark would go, the honest inverse of an
    // avatar: no seed, no hue) and one line naming what the search reads.
    // G4: the second line describes the search's BEHAVIOUR and is strictly
    // true of ConversationRowMatches (name always; preview only when the row
    // draws it) - it claims no crypto, and it teaches the privacy property
    // demo.list.search asserts at the moment that property is operating.
    // Both strings are English literals from code (DemoChip precedent;
    // Resources.resw is generated). Mounted as ListScaffold's row-2 child
    // AFTER the ScrollViewer so it paints above the emptied row area - into
    // ListScaffold itself, never a new container: ListHost (MainWindow.xaml)
    // is the standing warning about writing into a collapsed twin.
    {
      Controls::StackPanel column;
      column.Spacing(8);
      column.HorizontalAlignment(HorizontalAlignment::Center);
      column.VerticalAlignment(VerticalAlignment::Center);
      auto lattice = urmsg::MakeIdenticonLattice(64);
      lattice.HorizontalAlignment(HorizontalAlignment::Center);
      // Decorative: the two lines below carry all of the meaning.
      Automation::AutomationProperties::SetAccessibilityView(
          lattice, Automation::Peers::AccessibilityView::Raw);
      column.Children().Append(lattice);
      Controls::TextBlock headline;
      headline.Text(L"No conversations match");
      headline.FontSize(12);
      headline.Foreground(urnw::colors::MutedBrush());
      headline.TextAlignment(TextAlignment::Center);
      column.Children().Append(headline);
      Controls::TextBlock detail;
      detail.Text(L"Search checks names and visible previews only.");
      detail.FontSize(11);
      detail.Foreground(urnw::colors::FaintBrush());
      detail.TextAlignment(TextAlignment::Center);
      column.Children().Append(detail);
      searchEmpty_ = Controls::Grid();
      searchEmpty_.Children().Append(column);
      Controls::Grid::SetRow(searchEmpty_, 2);
      searchEmpty_.Visibility(Visibility::Collapsed);
      ListScaffold().Children().Append(searchEmpty_);
    }

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

  // The density seed (R4): ONE reader of the Advanced Mode truth, matching
  // the conversation list's established call above (contract 5 allows one
  // reader, not two). NOT demo_.advanced (does not exist) and NOT
  // options_.advanced (the wrong source - the switch is session-only and
  // InitAdvancedMode has already folded it in). This runs before the initial
  // populate below, so the first render reads the right density from the
  // tag; SetInspectRailAdvanced itself re-populates nothing yet because
  // neither body is visible. The LIVE toggle subscription is the wiring
  // task's (wiring.md:578), not this task's.
  //
  // W5 CARRY-OVER (the d7 distillation's sequencing note): when the wiring
  // task deletes BuildInspectRail, this seeding line must move into
  // BuildDemoViews beside the rail mount, or the rail opens at normal
  // density under --demo-advanced.
  urmsg::views::SetInspectRailAdvanced(rail_, urmsg::AdvancedModeEnabled());

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
  // The rail takes its subject from PickInspectMessage; the thread's selection
  // outline takes its own from kInspectTargetRowId. Those are TWO designations,
  // not one shared function, so they agree by GATE and not by construction:
  // InspectRailDeviceProbe asserts the pick equals the constant, and that
  // assertion is the only thing keeping the rail and the outline on the same
  // bubble. If it ever fails, this deep link and the thread have parted.
  //
  // THE DECISION ITSELF IS PURE NOW (R4): InitialRailMode lives in
  // InspectRailFields, so InspectRailDeviceProbe asserts what the mode SHOULD
  // be - the deep link, the default, and the no-message arm below. What the
  // probe cannot assert is which write lands LAST; that half stays the wiring
  // task's single-writer rule.
  //
  // *** THE WIRING TASK MUST DELETE THIS BRANCH, NOT KEEP IT. *** That task
  // installs its own --demo=inspect deep link in DrainDeepLink. Two writers of
  // one state is one too many: whichever runs last wins, which makes the rail's
  // subject an ordering accident, and if the two do not designate the same row
  // it can put the rail on a different bubble from the one the thread outlines.
  // The constructor comment above BuildInspectRail() states the hazard.
  if (urmsg::views::InitialRailMode(options_.screen, conv) ==
      urmsg::views::RailMode::Message) {
    // InitialRailMode returns Message only when the pick is non-null, so the
    // second pick here cannot miss; it is the same pure, deterministic call
    // the function itself made.
    urmsg::views::SetInspectRailMessage(rail_, conv, *urmsg::views::PickInspectMessage(conv));
    return;
  }
  if (options_.screen == urmsg::demo::DemoScreen::Inspect) {
    // The arm InitialRailMode's third probe clause covers: Inspect asked, but
    // this conversation has no message row to be about.
    urnw::LogWarn("rail: --demo=inspect but conversation 0 has no message row");
  }
  urmsg::views::SetInspectRailConversation(rail_, conv);
}

// DRIVING THE RAIL FROM A SELECTION HANDLER? Read the block above
// BuildInspectRail() in the constructor first (R3). The wiring task replaces
// this function with SelectConversation and drives the rail from there; the rule
// that matters is that --demo=inspect must end up with exactly ONE writer, which
// means deleting BuildInspectRail()'s deep-link branch rather than keeping both.
// Nothing in --diagnose checks the rail's MODE, so getting this wrong is silent.
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
  // While filtering it reads "3 of 8" - what the filter DID - returning to the
  // bare total unfiltered so the one readout is never ambiguous (d3 2.5). The
  // fragment is an English literal (DemoChip precedent; resw is generated).
  const std::size_t total = world.conversations.size();
  ListPaneCount().Text(visible == total
                           ? winrt::to_hstring(static_cast<int>(total))
                           : winrt::to_hstring(static_cast<int>(visible)) + L" of " +
                                 winrt::to_hstring(static_cast<int>(total)));
  // The empty state answers a zero-result SEARCH. The query guard keeps it
  // one: an empty world with no query would also be visible == 0, and that is
  // a different, already-gated failure (I1 requires 8 conversations) - not a
  // search result this module should explain.
  SetSearchEmptyVisible(visible == 0 && !query.empty());
}

void MainWindow::SetSearchEmptyVisible(bool show) {
  if (!searchEmpty_) return;  // a non-demo launch never built it
  searchEmptyShown_ = show;
  const bool drawn = searchEmpty_.Visibility() == Visibility::Visible;
  // Instant swap with motion off (MOT): the gate means GONE, not reduced.
  // This branch is code-inspection only - reduce-motion has never executed
  // on this machine.
  if (!urnw::motion::ShouldAnimate()) {
    searchEmpty_.Opacity(1.0);
    searchEmpty_.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
    return;
  }
  namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
  if (show) {
    if (drawn && searchEmpty_.Opacity() == 1.0) return;  // already settled
    // Entrance: kBaseMs on the standard curve (d3 2.5). A re-show mid-exit
    // resumes from the current opacity instead of popping back to 0.
    const double from = drawn ? searchEmpty_.Opacity() : 0.0;
    searchEmpty_.Visibility(Visibility::Visible);
    auto fade = urnw::motion::MakeSplineDouble(from, 1.0, urnw::motion::kBaseMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(fade, searchEmpty_);
    anim::Storyboard::SetTargetProperty(fade, L"Opacity");
    anim::Storyboard board;
    board.Children().Append(fade);
    board.Begin();
    return;
  }
  if (!drawn) return;  // already gone: nothing to fade
  // Exit one step faster than the entrance (MOT) on the exit curve.
  auto fade = urnw::motion::MakeSplineDouble(searchEmpty_.Opacity(), 0.0,
                                             urnw::motion::kFastMs, 0,
                                             urnw::motion::kExitP1,
                                             urnw::motion::kExitP2);
  anim::Storyboard::SetTarget(fade, searchEmpty_);
  anim::Storyboard::SetTargetProperty(fade, L"Opacity");
  anim::Storyboard board;
  board.Children().Append(fade);
  board.Completed([weak = get_weak()](auto const&, auto const&) {
    auto self = weak.get();
    // A re-show during the fade owns the module now (searchEmptyShown_ is the
    // TARGET state); collapsing here would strand it invisible over an
    // emptied list.
    if (!self || self->searchEmptyShown_) return;
    self->searchEmpty_.Visibility(Visibility::Collapsed);
    self->searchEmpty_.Opacity(1.0);
  });
  board.Begin();
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
  // The compact-rail tooltip, same string as the label - ApplyStrings does
  // the three permanent destinations for the same reason (d3 3.2).
  Controls::ToolTipService::SetToolTip(NetworkNavItem(), box_value(hstring{kDemoNavNetwork}));
  Controls::ToolTipService::SetToolTip(DeveloperNavItem(), box_value(hstring{kDemoNavDeveloper}));

  DemoChipText().Text(kDemoWatermark);
  // d3 3.4: the identicon lattice as the chip's prefix - decorative, links
  // the chip to the identicon language. It adds no WORDS, so nothing
  // honesty-bearing depends on a chip --demo-watermark=off removes (G4).
  // 10dip, achromatic; MakeIdenticonLattice owns its corner radius.
  DemoChipLatticeHost().Children().Clear();
  DemoChipLatticeHost().Children().Append(urmsg::MakeIdenticonLattice(10));
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
  // The list column's fixed width (320, stepping to 360 at
  // kListWideBreakpointDip - d3 section 4) and whether the nav pane is
  // docked: LayoutFor deliberately carries neither (the step stays out of
  // the gate-asserted Layout struct; the overlay threshold is the
  // platform's), so both are tracked beside layout_ and joined into the
  // early-out - a resize that crosses only the 1200 or the 640 line must
  // not be skipped.
  const double listWidth = urmsg::demo::ListWidthFor(width);
  const bool navDocked = kNavOverlayMaxDip < width;
  if (layoutApplied_ && next.wide == layout_.wide && next.rail == layout_.rail &&
      next.strip == layout_.strip && listWidth == listWidth_ && navDocked == navDocked_)
    return;
  layout_ = next;
  listWidth_ = listWidth;
  navDocked_ = navDocked;
  layoutApplied_ = true;

  // Wide: a fixed list rail and the thread takes what is left. Narrow:
  // the list IS the window and the thread does not exist.
  ListColumn().Width(layout_.wide
                         ? GridLengthHelper::FromPixels(listWidth_)
                         : GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  ThreadColumn().Width(layout_.wide
                           ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                           : GridLengthHelper::FromPixels(0));
  const auto threadVisibility =
      layout_.wide ? Visibility::Visible : Visibility::Collapsed;
  PaneRule().Visibility(threadVisibility);
  // The nav pane's right-edge rule (d3 3.1): every other pane boundary has
  // its 1px rule; the nav pane is a pane. Only while the pane is DOCKED -
  // below kNavOverlayMaxDip it is an overlay flyout and there is no boundary
  // to draw on. ApplyBreakpoint is this element's one writer, as it is for
  // PaneRule and RailRule.
  NavPaneRule().Visibility(navDocked_ ? Visibility::Visible : Visibility::Collapsed);
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

  urnw::LogInfo("window: layout wide={} rail={} strip={} listW={:.0f} navRule={} (content {:.0f}x{:.0f} dip)",
                layout_.wide, layout_.rail, layout_.strip, listWidth_, navDocked_, width, height);
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
    // The N/S/A groups have not landed: NetworkHost/SettingsHost/DeveloperHost
    // are empty Grids that paint a blank pane. Route to the stub (which carries
    // the "not built" line) until the real surface mounts into its host.
    incoming = StubPage();
    header = hstring{kDemoNavNetwork};
  } else if (tag == L"developer" && options_.enabled) {
    incoming = StubPage();
    header = hstring{kDemoNavDeveloper};
  } else if (tag == L"settings" && options_.enabled) {
    incoming = StubPage();
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
