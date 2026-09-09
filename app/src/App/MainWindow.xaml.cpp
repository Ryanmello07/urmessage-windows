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
#include "Views/NetworkPageView.h"
#include "Views/StatusStripView.h"
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
  // The search row (and, on a normal launch, the placeholder rows) FIRST:
  // RebuildConversationList, reached through EnterDemoMode -> BuildDemoViews,
  // wires the box this builds. EnterDemoMode second: it resolves Advanced Mode
  // (InitAdvancedMode) before any Set*Advanced seed reads it, arms the DEMO
  // chip and the deep link, and builds the demo's three content views. Under
  // --demo BuildConversationList early-returns without touching the list
  // children, so the two never write the same container.
  BuildConversationList();
  if (options_.enabled) {
    EnterDemoMode();
  }
  BuildNetworkPage();
  BuildSettings();
  BuildDeveloper();
  BuildStatusStrip();

  // The window reveal: bind now that the content tree exists, then arm BEFORE
  // Activate() so the first composed frame is already the start pose rather
  // than the settled one corrected a frame later. No tray icon yet, so there is
  // no anchor to spring from — nullopt gives the plain centred scale, which is
  // the third of WindowReveal's three documented fallbacks and never fails.
  //
  // ConversationList unconditionally, not a demo/non-demo ternary: the demo's
  // rows land there too (RebuildConversationList), so it is the one element
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
  // the reading the pane model exists to delete. The line is DECLARED in
  // markup (ThreadEmptyLine, inside ThreadBody) and only its text is set
  // here: a .Children() write into ThreadBody is a write into the pane the
  // demo collapses, which is the stale-mount failure mount_check exists to
  // name — it flagged this line until it became markup.
  ThreadEmptyLine().Text(Loc("thread_none_selected"));

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

  // --demo: the demo world's rows are RebuildConversationList's job (W5, the
  // d7 audit's Step-0 override - BuildDemoViews is the SOLE builder of list_,
  // thread_ and rail_). This function runs first only because it builds the
  // search row that builder wires, and it returns here so that nothing on the
  // placeholder path - the Clear() below especially - can touch the container
  // the demo's rows are already sitting in. Without --demo every line below
  // is byte-for-byte what it was: design doc 8, "without --demo the app
  // behaves exactly as it does today".
  if (urmsg::demo::ParseDemoOptions().enabled) return;

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

int MainWindow::OpenConversationIndex() const {
  if (openConversationId_.empty()) return -1;
  auto const& conversations = urmsg::demo::GetWorld().conversations;
  for (size_t i = 0; i < conversations.size(); ++i)
    if (conversations[i].id == openConversationId_) return static_cast<int>(i);
  return -1;
}

void MainWindow::RebuildConversationList() {
  // The view wires its own rows and calls back with an INDEX into
  // World::conversations (contract v2 section 4), so there is one place that
  // knows how a row maps to a conversation and it is not here.
  auto const& world = urmsg::demo::GetWorld();
  list_ = urmsg::views::MakeConversationList(world, [weak = get_weak()](int index) {
    if (auto self = weak.get()) self->SelectConversation(index);
  });
  // ConversationList, NOT ListHost (the d7 audit's W5-class-1 override):
  // ListHost is Visibility="Collapsed" by XAML attribute at MainWindow.xaml:213,
  // its own XAML comment says "never made Visible, nothing ever appended to it
  // again", and no line of code ever flips it - mount_check reports it "ok"
  // because it reads only .Visibility(Collapsed) WRITES in this file, a
  // documented false pass for a XAML-declared Collapsed.
  auto rows = ConversationList().Children();
  rows.Clear();
  if (!list_.root) {
    urnw::LogError("window: MakeConversationList returned no root - the demo list pane is empty");
    return;
  }
  rows.Append(list_.root);
  ListPaneCount().Text(winrt::to_hstring(static_cast<int>(world.conversations.size())));
  urnw::LogInfo("window: demo conversation list built with {} rows",
                world.conversations.size());

  // The unread InfoBadge on the Chats nav item (d3 3.2): the summed unread,
  // set ONCE here - the world's unread counts never change at runtime, so a
  // live binding would have nothing to do. The badge is neutral by the
  // App.xaml theme overrides (InfoBadgeBackground/Foreground): not accent
  // (reserved), not green (presence), not red (danger). A property set at
  // build, never a Visibility flip - the WASDK 2.2.0 Auto-mode defect
  // DrainDeepLink documents is about Collapsed -> Visible transitions, which
  // this does not perform; the nav is still screenshot-verified after the
  // change.
  int unreadTotal = 0;
  for (auto const& c : world.conversations) unreadTotal += c.unread;
  if (0 < unreadTotal) {
    Controls::InfoBadge badge;
    badge.Value(unreadTotal);
    ChatsNavItem().InfoBadge(badge);
  }
  // TextChanged, not KeyDown: it fires for paste, for undo and for a
  // programmatic Text() write, and the filter must be true of the box's
  // CONTENT rather than of the last key that touched it. The box is built by
  // BuildConversationList, which is why the constructor runs that function
  // before EnterDemoMode reaches this one.
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
  // is the standing warning about writing into a collapsed twin. Built once
  // (searchEmpty_ is the guard): this builder runs once today, but its name
  // invites a second call and a second Append would throw.
  if (!searchEmpty_) {
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
  // This is the launch-time read of the same truth; the live subscription
  // that re-applies it on every toggle is the ONE OnAdvancedModeChanged
  // registration in EnterDemoMode (W7).
  urmsg::views::SetConversationListAdvanced(list_, urmsg::AdvancedModeEnabled());

  const int open = OpenConversationIndex();
  if (0 <= open) urmsg::views::SetConversationSelected(list_, open);
}

void MainWindow::BuildDemoViews() {
  RebuildConversationList();

  // Built ONCE. SetThreadConversation re-points it; rebuilding it per
  // selection would drop the two callbacks it was constructed with, and would
  // re-run the bubble entrance animation for a conversation the viewer is
  // already in. get_weak(), not `this`: MakeThread stores these for the
  // window's life, and a raw capture would outlive a closed window.
  thread_ = urmsg::views::MakeThread(
      [weak = get_weak()](std::wstring id) {
        if (auto self = weak.get()) self->SelectMessage(std::move(id));
      },
      [weak = get_weak()]() {
        if (auto self = weak.get()) self->ClearMessageSelection();
      });
  // ThreadHost, NOT ThreadBody. ApplyBreakpoint gives exactly one of the two
  // thread surfaces to a run: under --demo it collapses ThreadPane outright
  // and shows ThreadHost, so anything appended to ThreadBody here would
  // compile, log and render ZERO pixels - the failure this window has already
  // shipped, and the one mount_check reads this file to catch.
  ThreadHost().Children().Clear();
  if (thread_.root)
    ThreadHost().Children().Append(thread_.root);
  else
    urnw::LogError("window: MakeThread returned no root - the demo thread pane is empty");

  rail_ = urmsg::views::MakeInspectRail();
  // RailHost, and NOT a host of the rail task's own. RailHost is already the
  // Grid.Column=4 occupant of ChatsPage (MainWindow.xaml), it is already what
  // ApplyBreakpoint shows and hides with RailColumn and RailRule, and a second
  // Grid in that cell would mean the shell displayed its empty one while this
  // filled the other - "mounted into the collapsed twin" again.
  RailHost().Children().Clear();
  if (rail_.root)
    RailHost().Children().Append(rail_.root);
  else
    urnw::LogError("window: MakeInspectRail returned no root - the inspector rail is empty");

  // The density seed (R4, carried here by the d7 audit's W5 sequencing note:
  // this line lived in BuildInspectRail, which W5's Step 0 deleted, and it
  // moves beside the rail mount or the rail opens at normal density under
  // --demo-advanced). ONE reader of the Advanced Mode truth, matching the
  // conversation list's established call above (contract 5 allows one reader,
  // not two). NOT options_.advanced: the switch is session-only and
  // InitAdvancedMode has already folded it in. The LIVE toggle subscription
  // is the ONE OnAdvancedModeChanged registration in EnterDemoMode (W7).
  urmsg::views::SetInspectRailAdvanced(rail_, urmsg::AdvancedModeEnabled());
}

void MainWindow::SelectConversation(int index) {
  auto const& conversations = urmsg::demo::GetWorld().conversations;
  if (index < 0 || conversations.size() <= static_cast<size_t>(index)) return;
  auto const& conversation = conversations[static_cast<size_t>(index)];
  // Re-clicking the conversation that is already open is a NO-OP, not a
  // rebuild. A rebuild would re-run the thread's entrance animation and drop
  // the message selection for something the viewer did not ask to change.
  if (conversation.id == openConversationId_) return;

  openConversationId_ = conversation.id;
  selectedMessageId_.clear();
  urmsg::views::SetConversationSelected(list_, index);
  urmsg::views::SetThreadConversation(thread_, conversation);
  urmsg::views::SetInspectRailConversation(rail_, conversation);
  urnw::LogInfo("window: conversation -> {} (index {})",
                urnw::Narrow(openConversationId_), index);
}

void MainWindow::SelectMessage(std::wstring id) {
  // Below kRailBreakpointDip there IS no rail, so selecting a message does
  // nothing visible and message inspect is unavailable until the window is
  // widened again (design doc 6.5a). No sheet, no fallback, no error - a
  // narrowed demo window is a smaller demo, not a broken one.
  if (!layout_.rail) {
    urnw::LogInfo("window: message inspect unavailable below {:.0f} dip of content width",
                  urmsg::demo::kRailBreakpointDip);
    return;
  }
  const int index = OpenConversationIndex();
  if (index < 0) return;
  auto const& conversation =
      urmsg::demo::GetWorld().conversations[static_cast<size_t>(index)];
  for (auto const& row : conversation.rows) {
    if (row.id != id) continue;
    selectedMessageId_ = id;
    urmsg::views::SetThreadSelectedMessage(thread_, selectedMessageId_);
    urmsg::views::SetInspectRailMessage(rail_, conversation, row);
    urnw::LogInfo("window: message inspect -> {}", urnw::Narrow(selectedMessageId_));
    return;
  }
  urnw::LogWarn("window: no row {} in conversation {}", urnw::Narrow(id),
                urnw::Narrow(conversation.id));
}

void MainWindow::ClearMessageSelection() {
  if (selectedMessageId_.empty()) return;
  selectedMessageId_.clear();
  urmsg::views::SetThreadSelectedMessage(thread_, std::wstring{});
  const int index = OpenConversationIndex();
  if (0 <= index)
    urmsg::views::SetInspectRailConversation(
        rail_, urmsg::demo::GetWorld().conversations[static_cast<size_t>(index)]);
  urnw::LogInfo("window: message inspect cleared");
}

void MainWindow::ApplyAdvanced(bool on) {
  advanced_ = on;

  // Developer is a destination that exists only under Advanced Mode. Leaving
  // it selected while it disappears would strand the window on a hidden
  // destination with no way back. The item is INSERTED and REMOVED, never
  // Visibility-flipped: the WASDK 2.2.0 Auto-mode defect DrainDeepLink
  // documents is triggered by a Collapsed -> Visible transition, and at
  // runtime no PaneDisplayMode cycle recovers from it - measured, not
  // theorized: flip + cycle and flip + cycle + UpdateLayout both left the
  // whole pane icon-only permanently (the w7-live captures). A collection
  // mutation never performs the corrupting transition, so this function
  // carries NO PaneDisplayMode cycle; DrainDeepLink keeps its own, which
  // protects NetworkNavItem's startup flip. EnterDemoMode builds the item;
  // this is the ONE writer of its membership.
  if (developerNavItem_) {
    auto footer = HomeNav().FooterMenuItems();
    uint32_t devIndex = 0;
    const bool present = footer.IndexOf(developerNavItem_, devIndex);
    if (on && !present) {
      // Developer above Settings (design 6.6's nav table), below the
      // separator: at Settings' own index when it is found.
      uint32_t settingsIndex = 0;
      if (footer.IndexOf(SettingsNavItem(), settingsIndex))
        footer.InsertAt(settingsIndex, developerNavItem_);
      else
        footer.Append(developerNavItem_);
    } else if (!on && present) {
      footer.RemoveAt(devIndex);
    }
  }
  if (!on && currentTag_ == L"developer") SelectNavTag(L"chats");

  urmsg::views::SetConversationListAdvanced(list_, on);
  urmsg::views::SetStatusStripAdvanced(statusStrip_, on);
  urmsg::views::SetNetworkPageAdvanced(network_, on);
  // DENSITY only (contract v2 section 4). NOT SetInspectRailMessage: that
  // runs the rail's conversation<->message crossfade, which would fade the
  // pane the viewer is looking at out over itself for a change that is not a
  // mode change. The one SetInspectRailMessage call site in this file sits
  // inside SelectMessage; this function must never grow one.
  urmsg::views::SetInspectRailAdvanced(rail_, on);

  urnw::LogInfo("window: advanced mode {}", on);
}

void MainWindow::RefreshOpenThread() {
  const int index = OpenConversationIndex();
  if (index < 0) return;
  auto const& conversation =
      urmsg::demo::GetWorld().conversations[static_cast<size_t>(index)];
  urmsg::views::SetThreadConversation(thread_, conversation);
  // SetThreadConversation rebuilds the bubbles, so the selection outline has
  // to be put back. Restoring a selection is the opposite of moving one:
  // after this returns, the same message id is selected that was selected
  // before.
  if (!selectedMessageId_.empty())
    urmsg::views::SetThreadSelectedMessage(thread_, selectedMessageId_);
}

void MainWindow::StartAmbientActivity() {
  if (!options_.enabled || !options_.autoplay) return;

  urmsg::demo::AutoplayCallbacks callbacks;
  callbacks.openConversationIndex = [weak = get_weak()]() -> int {
    auto self = weak.get();
    return self ? self->OpenConversationIndex() : -1;
  };
  callbacks.setTyping = [weak = get_weak()](bool on) {
    if (auto self = weak.get()) urmsg::views::SetThreadTyping(self->thread_, on);
  };
  callbacks.onDeliveryAdvanced = [weak = get_weak()]() {
    if (auto self = weak.get()) self->RefreshOpenThread();
  };
  callbacks.onIncoming = [weak = get_weak()](urmsg::demo::MessageRow const& row) {
    // AppendThreadRow, not SetThreadConversation: the row springs in (design
    // doc 7's bubble entrance) instead of the whole thread being redrawn
    // under it. The row is already in the world, so any later rebuild still
    // has it.
    if (auto self = weak.get()) urmsg::views::AppendThreadRow(self->thread_, row);
  };

  // The static GetForCurrentThread(), not Window::DispatcherQueue(): both are
  // real, but the static is the one this repo's projection definitely carries
  // (Microsoft.UI.Dispatching.h:554), and EnterDemoMode runs on the UI thread,
  // which is the thread that owns the queue.
  autoplay_ = urmsg::demo::MakeAutoplay(
      winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread(),
      std::move(callbacks));
  urmsg::demo::StartAutoplay(*autoplay_);
}

void MainWindow::BuildNetworkPage() {
  // options_, NOT a second ParseDemoOptions() call: one flag, parsed once in
  // the constructor. ShowDestination's network arm reads options_ to decide
  // whether the host is routable at all, so a second read here would give the
  // host is routable at all, so a second read here would give the mount and
  // the route two sources of truth for one flag. It also keeps GetWorld() off
  // a normal launch (design §8: the app behaves exactly as it does today).
  if (!options_.enabled) return;

  network_ = urmsg::views::MakeNetworkPage(urmsg::demo::GetWorld());
  // NetworkHost, NOT a host of the network task's own: MainWindow.xaml:282
  // already declares it and ShowDestination's network arm routes to it, and a
  // second Grid would be "mounted into the collapsed twin", the failure this
  // window has already shipped four times (the d7 audit's N3 override - there
  // is no NetworkBody).
  NetworkHost().Children().Clear();
  if (network_.root) {
    NetworkHost().Children().Append(network_.root);
  } else {
    urnw::LogWarn("window: MakeNetworkPage returned no root; the Network "
                  "destination is empty");
  }

  // N6 SEEDS ONLY (the d7 distillation's §1.2 tightening): the live
  // subscription is the ONE OnAdvancedModeChanged registration in
  // EnterDemoMode (W7), which fans out to SetNetworkPageAdvanced from
  // ApplyAdvanced. This is the launch-time read of the same truth, matching
  // the conversation list's and the rail's established calls.
  urmsg::views::SetNetworkPageAdvanced(network_, urmsg::AdvancedModeEnabled());
  urnw::LogInfo("window: network page built");
}

void MainWindow::BuildSettings() {
  // options_, NOT a second ParseDemoOptions() call — one flag, parsed once
  // in the constructor (the rule BuildNetworkPage states). It also keeps GetWorld() off a normal
  // launch (design §8: the app behaves exactly as it does today).
  if (!options_.enabled) return;

  // The callback IS the preference's one writer (Demo/AdvancedMode.h): the
  // view owns the switch and follows itself, so there is no Set*Advanced seed
  // here and no second subscription — the live fan-out to the OTHER surfaces
  // is the ONE OnAdvancedModeChanged registration in EnterDemoMode (W7).
  // AdvancedModeEnabled() is already resolved:
  // EnterDemoMode ran InitAdvancedMode before any Build* call.
  settings_ = urmsg::views::MakeSettings(
      [](bool on) { urmsg::SetAdvancedModeEnabled(on); },
      urmsg::AdvancedModeEnabled());
  // SettingsHost, NOT a Grid of this task's own: MainWindow.xaml:284 already
  // declares it and ShowDestination's settings arm routes to it (the d7
  // audit's A3 override — do not add a SettingsPage Grid; that is "mounted
  // into the collapsed twin", the failure this window has already shipped).
  SettingsHost().Children().Clear();
  if (settings_.root) {
    SettingsHost().Children().Append(settings_.root);
  } else {
    urnw::LogWarn("window: MakeSettings returned no root; the Settings "
                  "destination is empty");
  }
  urnw::LogInfo("window: settings page built (advanced={})",
                urmsg::AdvancedModeEnabled() ? "on" : "off");
}

void MainWindow::BuildDeveloper() {
  // options_, NOT a second ParseDemoOptions() call — one flag, parsed once
  // in the constructor (the rule BuildNetworkPage states). It also keeps
  // GetWorld() off a normal launch (design §8: the app behaves exactly as it
  // does today).
  if (!options_.enabled) return;

  developer_ = urmsg::views::MakeDeveloper(urmsg::demo::GetWorld());
  // DeveloperHost, NOT a Grid of this task's own: MainWindow.xaml:286 already
  // declares it and ShowDestination's developer arm routes to it (the d7
  // audit's A5 override — do not add a DeveloperPage Grid; that is "mounted
  // into the collapsed twin", the failure this window has already shipped).
  DeveloperHost().Children().Clear();
  if (developer_.root) {
    DeveloperHost().Children().Append(developer_.root);
  } else {
    urnw::LogWarn("window: MakeDeveloper returned no root; the Developer "
                  "destination is empty");
  }
  urnw::LogInfo("window: developer page built");
}

void MainWindow::BuildStatusStrip() {
  // The whole surface is demo-only. Design 8: without --demo "the app behaves
  // exactly as it does today", and design 2's hard constraint is that nothing
  // may overstate what exists — a strip reading "Connected | server
  // urmsg-01.ur.io" on a plain double-click of a build with no protocol would
  // do exactly that. This early return is also why demo::GetWorld() is never
  // constructed on a normal launch. options_, NOT a second ParseDemoOptions()
  // call — one flag, parsed once in the constructor (the rule
  // BuildNetworkPage states).
  if (!options_.enabled) {
    urnw::LogInfo("window: status strip not built (demo off)");
    return;
  }

  statusStrip_ = urmsg::views::MakeStatusStrip(urmsg::demo::GetWorld());
  // StatusStripHost (MainWindow.xaml:309) already exists with its Collapsed
  // markup default, and ApplyBreakpoint is the one writer of its Visibility
  // (options_.enabled && layout_.strip, at the foot of ApplyBreakpoint) — the
  // d7 audit's S2 override skips the brief's host-row surgery entirely, so
  // this mount changes nothing about WHEN the strip shows.
  StatusStripHost().Children().Clear();
  StatusStripHost().Children().Append(statusStrip_.root);
  // The drawer mounts into StatusDrawerHost in Grid.Row 1 — ABOVE the
  // destination, never into the strip's own Auto row, which a raised drawer
  // would grow (d5 4.4).
  StatusDrawerHost().Children().Clear();
  if (statusStrip_.drawer) {
    StatusDrawerHost().Children().Append(statusStrip_.drawer);
  } else {
    urnw::LogWarn("window: demo status strip has no drawer; activation will "
                  "paint nothing");
  }
  // S3 owns the toggle and this wiring (the d7 audit's resolution alpha):
  // the drawer's own Visibility is the only state — there is no drawerOpen_
  // bool to disagree with the tree.
  statusStrip_.strip.Click([weak = get_weak()](auto const&, auto const&) {
    if (auto self = weak.get()) self->ToggleStatusDrawer();
  });

  // S4 SEEDS ONLY (the d7 distillation's 1.2 ruling): the live subscription
  // is the ONE OnAdvancedModeChanged registration in EnterDemoMode (W7),
  // which fans out to SetStatusStripAdvanced from ApplyAdvanced. This is the
  // launch-time read of the same truth, matching the network page's
  // established call above.
  const bool advanced = urmsg::AdvancedModeEnabled();
  urmsg::views::SetStatusStripAdvanced(statusStrip_, advanced);
  urnw::LogInfo("window: status strip built");
  urnw::LogInfo("window: status strip advanced -> {}", advanced ? "on" : "off");
}

void MainWindow::ToggleStatusDrawer() {
  if (!statusStrip_.drawer) return;
  // Visibility IS the state; there is no second bool to disagree with the
  // tree. During the 150 ms dismiss the drawer is still Visible, so a second
  // activation inside that window re-reads "open" and dismisses again —
  // which is the same thing the user asked for, and cheaper than a state
  // machine.
  const bool open = statusStrip_.drawer.Visibility() != Visibility::Visible;
  urmsg::views::SetStatusStripDrawerOpen(statusStrip_, open);
  urnw::LogInfo("window: status drawer -> {}", open ? "open" : "closed");
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
  // shape does not change under --demo -- RebuildConversationList fills the
  // same ConversationList/SearchHost/ListPaneCount the placeholder rows use --
  // so there is no second list host to switch to, and collapsing ListScaffold
  // here used to take the search row and the pane header count down with it
  // for no reason tied to the demo/non-demo split itself.
  ThreadPane().Visibility(Visibility::Collapsed);

  // Content only, here. The Visibility flip is deferred to DrainDeepLink (fix
  // round 1) and paired there with a PaneDisplayMode nudge - see that function
  // for why. Isolated by disabling each call above in turn: Content() alone
  // never triggered the regression, only Visibility() did.
  NetworkNavItem().Content(box_value(hstring{kDemoNavNetwork}));
  // The compact-rail tooltip, same string as the label - ApplyStrings does
  // the three permanent destinations for the same reason (d3 3.2).
  Controls::ToolTipService::SetToolTip(NetworkNavItem(), box_value(hstring{kDemoNavNetwork}));

  // The Developer item is BUILT in code, never declared in markup and never
  // Visibility-flipped: the WASDK 2.2.0 Auto-mode defect DrainDeepLink
  // documents is triggered by a NavigationViewItem's Collapsed -> Visible
  // transition, and at runtime no PaneDisplayMode cycle recovers from it -
  // measured, not theorized (a live toggle left the whole pane icon-only
  // permanently, with UpdateLayout between the cycle's writes; the w7-live
  // captures). ApplyAdvanced INSERTS and REMOVES this item instead: a
  // collection mutation never performs the corrupting transition - the route
  // DrainDeepLink's comment names as the untried cleaner candidate, now
  // tried and verified. Built here, empty-handed: the one writer of the
  // item's collection MEMBERSHIP is ApplyAdvanced, which runs below.
  developerNavItem_ = Controls::NavigationViewItem();
  developerNavItem_.Content(box_value(hstring{kDemoNavDeveloper}));
  developerNavItem_.Tag(box_value(hstring{L"developer"}));
  {
    // Segoe Fluent Icons out of the app dictionary, with the same fallback
    // ConversationListView.cpp's IconFont() records: FontIcon otherwise
    // defaults to the older Segoe MDL2 Assets, whose metrics differ.
    Media::FontFamily family{L"Segoe Fluent Icons"};
    if (auto app = Application::Current()) {
      auto key = box_value(hstring{L"UrIconFontFamily"});
      if (app.Resources().HasKey(key))
        if (auto found = app.Resources().Lookup(key).try_as<Media::FontFamily>())
          family = found;
    }
    Controls::FontIcon icon;
    icon.FontFamily(family);
    icon.FontSize(20);
    icon.Glyph(L"\uE943");  // Code
    developerNavItem_.Icon(icon);
  }
  Controls::ToolTipService::SetToolTip(developerNavItem_, box_value(hstring{kDemoNavDeveloper}));

  DemoChipText().Text(kDemoWatermark);
  // d3 3.4: the identicon lattice as the chip's prefix - decorative, links
  // the chip to the identicon language. It adds no WORDS, so nothing
  // honesty-bearing depends on a chip --demo-watermark=off removes (G4).
  // 10dip, achromatic; MakeIdenticonLattice owns its corner radius.
  DemoChipLatticeHost().Children().Clear();
  DemoChipLatticeHost().Children().Append(urmsg::MakeIdenticonLattice(10));
  DemoChip().Visibility(options_.watermark ? Visibility::Visible
                                           : Visibility::Collapsed);

  // The demo's three content views, built ONCE here (W5: BuildDemoViews is
  // their sole builder - the placeholder-era BuildThread/BuildInspectRail/
  // OnConversationSelected are gone, so nothing after this line reassigns
  // list_, thread_ or rail_). After InitAdvancedMode, so the Set*Advanced
  // seeds inside read the resolved truth; after BuildConversationList, so the
  // search box RebuildConversationList wires exists. Nothing is SELECTED yet:
  // the deep link below owns the one pre-selection an agent may reach.
  BuildDemoViews();

  // The ONE subscription (contract v2 section 5; W7). Registered after the
  // views exist, so the first notification cannot reach a half-built window.
  // InitAdvancedMode deliberately notifies nobody, which is why the initial
  // apply is an explicit call here: ONE path into Advanced Mode whatever
  // turned it on - the launch switch, a --demo=developer deep link, the
  // persisted preference, or a click in Settings.
  urmsg::OnAdvancedModeChanged([weak = get_weak()](bool on) {
    if (auto self = weak.get()) self->ApplyAdvanced(on);
  });
  ApplyAdvanced(urmsg::AdvancedModeEnabled());

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

  // NetworkNavItem becomes visible HERE, not in EnterDemoMode (fix round 1).
  // The Developer item is NOT here: it is built in code by EnterDemoMode and
  // inserted/removed by ApplyAdvanced, because at runtime no PaneDisplayMode
  // cycle recovers from a Visibility flip - the "untried cleaner route" this
  // comment used to name, now tried on the Developer item and verified (the
  // w7-live captures). NetworkNavItem keeps the markup declaration and the
  // cycle below: flipping it at runtime never happens (it is always visible
  // under --demo), so the startup-only cycle remains sufficient for it.
  //
  // Flipping a NavigationViewItem from Collapsed to Visible at ANY point -
  // constructor or here, before or after the window reaches its final size -
  // corrupts NavigationView's own Auto pane-mode resolution: it renders
  // icon-only from that moment on, permanently, and does not recover on a
  // later resize either (checked against the harness's own live 1200x800
  // resize in the same run). Isolated with three throwaway builds: (1)
  // dropping both Visibility() calls kept the untouched items labelled, (2)
  // re-adding only NetworkNavItem's reproduced icon-only for every item
  // including ones never touched, (3) moving the calls here instead of
  // EnterDemoMode alone did not help. What DOES recover it at startup is
  // forcing NavigationView to fully re-run its Auto adaptive logic
  // immediately after: stepping PaneDisplayMode away from Auto and back
  // re-measures against the CURRENT item set and CURRENT window width,
  // rather than whatever it cached before the Visibility flip. This cycle
  // still runs after the LAST Visibility flip of a launch (the
  // NetworkNavItem line above).
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
  // defect is still present and the cycle stays.
  NetworkNavItem().Visibility(Visibility::Visible);
  HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact);
  HomeNav().PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);

  SelectNavTag(pendingLink_.navTag);

  // The CONVERSATION half of the deep link. Conversation 0 is named explicitly
  // rather than taken from the view's first row: World is seeded and
  // deterministic (design doc 5), so it is a stable target, and it stays the
  // right target even if something later reorders the list's children.
  //
  // IT HAS TO HAPPEN HERE, not in the constructor. A property written during
  // construction is written against a template that has not been applied and a
  // tree that has not been laid out, so the border it sets never repaints. This
  // runs from the content root's first SizeChanged, after ApplyBreakpoint - i.e.
  // post-layout on a realized tree - which is the same reason the nav selection
  // was moved here. Conversation first, message second: SelectMessage reads
  // OpenConversationIndex(), so the order is load-bearing.
  if (pendingLink_.selectConversation) SelectConversation(0);
  // The MESSAGE half, and the ONE `pendingLink_.selectMessage` branch in this
  // file (the d7 audit's three-writers override: BuildInspectRail's deep-link
  // branch and DrainDeepLink's old SetThreadSelectedMessage line are both
  // deleted, so the rail's subject can never be an ordering accident). The
  // pick is the shared one - PickInspectMessage, c0-r12 (Read, delivered-by 7,
  // read-by 7) - NOT the last RowKind::Message: that is c0-r23, Pending, with
  // no received-at and no device lists, and it would open the showcase capture
  // on a blank Received row (the d7 distillation's §2.2 ruling).
  // InspectRailDeviceProbe asserts the pick equals kInspectTargetRowId, so the
  // rail and the outline cannot part.
  if (pendingLink_.selectMessage) {
    auto const& conv = urmsg::demo::GetWorld().conversations.front();
    if (auto const* picked = urmsg::views::PickInspectMessage(conv))
      SelectMessage(picked->id);
    else
      urnw::LogWarn("window: --demo=inspect but conversation 0 has no message row");
  }

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
  // The rows go INTO THE WORLD FIRST (the d7 audit's W9-class-8 override):
  // the ambient loop's first RefreshOpenThread re-sets the thread from
  // World::conversations, and a row that lived only in the ThreadView would
  // vanish ~40s into a presentation - two messages the viewer already read
  // disappearing with no cause, the one thing design 9.2 says ambient
  // activity must never do. This is a runtime append through the world's
  // existing mutation seam, the same path the loop's own rows take (contract
  // v2 section 1); the fixture FILE stays byte-frozen, because I10
  // fingerprints the seeded world at --diagnose time, before any window
  // exists.
  if (options_.autoplay && options_.screen == urmsg::demo::DemoScreen::Thread &&
      thread_.root && !urmsg::demo::GetWorld().conversations.empty()) {
    // front() is the open conversation: DeepLinkFor(Thread).selectConversation
    // is true, so SelectConversation(0) ran above, and c0-ambient-*'s ids name
    // that conversation.
    auto& front = urmsg::demo::MutableWorld().conversations.front();
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
    front.rows.push_back(out);
    urmsg::views::AppendThreadRow(thread_, out);

    // The sender is LIFTED from the conversation rather than invented: the
    // identicon is a hash of senderKey, so a made-up key would draw a person
    // who appears nowhere else in the thread and the capture could not be
    // checked against anything. The last incoming row of c0 is Elena Vasquez at
    // 12:02, whose identicon is still on screen a few rows up — so "the same
    // face twice" is the concrete thing to look for. (`out` is outgoing, so
    // pushing it above does not change this scan's answer.)
    urmsg::demo::MessageRow const* speaker = nullptr;
    for (auto const& r : front.rows)
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
      front.rows.push_back(in);
      urmsg::views::AppendThreadRow(thread_, in);
    }
    urnw::LogInfo("thread: ambient seed appended {} rows -> {} bubbles", speaker ? 2 : 1,
                  thread_.bubbles.size());
  }

  urnw::LogInfo("window: demo deep link -> tag={} conversation={} message={} (rail={})",
                urnw::Narrow(std::wstring{pendingLink_.navTag}),
                pendingLink_.selectConversation, pendingLink_.selectMessage,
                layout_.rail);

  // Started here rather than in EnterDemoMode: the loop's first act is to ask
  // which conversation is open, and the deep link is what opens one. Starting
  // it in the constructor would have every round before the first click skip
  // whole. Gated inside on options_.enabled && options_.autoplay.
  StartAmbientActivity();
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
  // An outlined bubble with no rail beside it is an affordance pointing at
  // nothing. Dropping below the breakpoint drops the selection with the rail.
  if (!rail && !selectedMessageId_.empty()) ClearMessageSelection();

  // The strip is hidden below kStripMinHeightDip of content HEIGHT so it can
  // never eat a readable thread at Spec C 1.2's 480dip minimum.
  StatusStripHost().Visibility(options_.enabled && layout_.strip
                                   ? Visibility::Visible
                                   : Visibility::Collapsed);
  // A drawer standing on a strip that has just collapsed would be left on the
  // window's bottom edge with its deliberately-missing fourth hairline and
  // nothing beneath it — and its toggle would be unreachable. The strip going
  // away takes its drawer with it (S3). On a non-demo launch statusStrip_ is
  // empty and SetStatusStripDrawerOpen no-ops on the null drawer.
  if (!(options_.enabled && layout_.strip))
    urmsg::views::SetStatusStripDrawerOpen(statusStrip_, false);

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

  // The network page's first-show entrance (d5 §3.7). AFTER the swap, inside
  // this same synchronous handler, so the start pose is written before the
  // first frame the page is visible in — and composed so the two never
  // animate one property on one element: the crossfade owns the page root's
  // opacity, the stagger owns the sections' rise and fade. Run-once inside
  // the view, so later visits are simple swaps.
  if (incoming == NetworkHost())
    urmsg::views::AnimateNetworkPageEntrance(network_);
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
