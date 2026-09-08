// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/InspectRailView.h"

#include <format>
#include <string>
#include <string_view>
#include <vector>

// BOTH automation headers, in this order, exactly as UrComponents.cpp:9-10 does
// it. AccessibilityView lives in the Peers projection; whether it reaches this
// file transitively through the other one is a cppwinrt layout detail and not
// something to bet a build on.
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include "Identicon.h"
#include "Log.h"
#include "UrColors.h"
#include "UrMotion.h"
#include "Views/InspectRailFields.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace kit = urnw::kit;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;

namespace urmsg::views {
namespace {

// std::wstring -> hstring is EXPLICIT, and the kit builders take
// `winrt::hstring const&`, so every std::wstring crossing into the kit goes
// through one named adapter rather than through forty hstring{...} at call
// sites. This is the same adapter MainWindow.xaml.cpp names Loc().
winrt::hstring H(std::wstring const& value) { return winrt::hstring{value}; }

// A style out of the app dictionary by key, or null if it is missing: applying
// styles by key is what keeps a pane built in code in step with App.xaml, and a
// missing key must not throw a layout away. UrComponents.cpp:78-86 has the same
// six lines in its own anonymous namespace and does not export them. A THIRD
// copy is the signal to promote it into UrComponents.h; this is the second.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

// ---- a blank value is one glyph, never a missing row -----------------------
//
// U+2014 EM DASH, written as an escape rather than as a literal character so the
// meaning of this file does not depend on its encoding surviving a checkout.
//
// WHY IT IS HERE AND NOT IN InspectRailFields.h. The demo fixture cannot fill
// every field: Demo/DemoWorld.cpp gives a DIRECT conversation an empty
// groupIdHex (:207) and copies it onto every message in it (:173), and it writes
// receivedAtLabel only once a device has said so (:177-188), which is correct -
// nothing has been received until one has. Swept over the fixture that is 28
// blank values across the world's message rows, and InspectRailFields.h's probe
// GATES on every blank being one of exactly those two shapes. Substituting in
// the field model would drive that count to zero and turn a working detector
// into a gate that cannot fail, so the substitution belongs to the VIEW: the
// model still reports "there is nothing here", and the view decides that
// "nothing here" is drawn as one em dash.
//
// AN EM DASH AND NOT A SKIPPED ROW. Skipping makes the row count
// data-dependent - the eight-then-four message shape is read BY POSITION
// downstream - and a "Received" row with a dash in it tells a reader more than
// an absent row does.
//
// THE LIMIT, STATED ACCURATELY: no render THIS task performs reaches the
// substituting branch. Conversation mode at the default density is
// BuildConversationFields' two rows, "Retention" and "Media", and DemoWorld
// fills both on all eight conversations. All 28 blanks are in message mode,
// which is defined by task R3. So the branch is covered here by the two
// static_asserts below - a locally built adversarial input, the same device
// InspectRailFields.cpp uses for ShortHex's unreachable truncating branch - and
// NOT by this task's screenshot, which shows no blank row because there is no
// blank value in it to show.
constexpr wchar_t const* kBlankValue = L"\u2014";

// The one funnel. Every key/value row the rail draws is built from the result of
// this function (AppendFieldRows below is the file's only MakePaneKeyValueRow
// call site), so a mode that forgets the placeholder cannot exist.
constexpr std::wstring_view RailValueOr(std::wstring_view value) {
  return value.empty() ? std::wstring_view{kBlankValue} : value;
}

// Both directions, because only one of them fails for each of the two ways this
// can be broken: deleting the substitution trips the first, and substituting
// unconditionally trips the second.
static_assert(RailValueOr(L"") == std::wstring_view{L"\u2014"},
              "a blank value must render as one em dash, not as an empty row");
static_assert(RailValueOr(L"Kept until deleted") == std::wstring_view{L"Kept until deleted"},
              "a value that IS present must reach the row unchanged");

// ---- the rail's state, parked on its own elements --------------------------
//
// FrameworkElement::Tag is an IInspectable slot the platform never touches, so
// it carries the rail's subject and density without a file-static that a second
// rail would silently share. IDS, never pointers - see the header.

void SetRailAdvanced(InspectRailView const& v, bool advanced) {
  if (v.root) v.root.Tag(winrt::box_value(advanced));
}

bool RailAdvanced(InspectRailView const& v) {
  return v.root && winrt::unbox_value_or<bool>(v.root.Tag(), false);
}

void SetRailSubject(InspectRailView const& v, std::wstring const& conversationId,
                    std::wstring const& messageId) {
  if (v.conversationScroll)
    v.conversationScroll.Tag(winrt::box_value(winrt::hstring{conversationId}));
  if (v.messageScroll) v.messageScroll.Tag(winrt::box_value(winrt::hstring{messageId}));
}

std::wstring TagId(FrameworkElement const& element) {
  if (!element) return {};
  auto tag = element.Tag().try_as<winrt::hstring>();
  return tag ? std::wstring{*tag} : std::wstring{};
}

// For the mode that re-resolves its subject at render time (task R3). Nothing in
// THIS task reads them back - conversation mode is handed its Conversation by
// the caller - so they are written and not yet read.
[[maybe_unused]] std::wstring RailConversationId(InspectRailView const& v) {
  return TagId(v.conversationScroll);
}
[[maybe_unused]] std::wstring RailMessageId(InspectRailView const& v) {
  return TagId(v.messageScroll);
}

// The StackPanel inside a body scroller. The fixed contract gives InspectRailView
// three fields and no body fields, so the panel is fetched rather than cached -
// which is also one fewer thing that can go stale.
StackPanel BodyOf(ScrollViewer const& scroller) {
  return scroller ? scroller.Content().try_as<StackPanel>() : nullptr;
}

// A crossfade ONLY when the mode actually changes.
//
// CrossfadePageSwap(outgoing, incoming) sets incoming.Opacity(0) and fades it
// back over kBaseMs (UrMotion.cpp:86-95). That is right for a real swap and
// wrong for anything else: called with an already-collapsed outgoing it blanks
// the VISIBLE body and re-fades it, and called twice in one synchronous block it
// drives two storyboards over the same two elements in opposite directions and
// the end state is a race. So the outgoing is passed only when it is actually on
// screen; otherwise `incoming` is passed as its own outgoing, which
// UrMotion.cpp:123-127 documents as the instant, no-storyboard path.
//
// A crossfade, never a slide: the column's width is fixed, and a slide inside a
// fixed-width column implies a navigation that has not happened (design 6.3).
// CrossfadePageSwap is already gated on motion::ShouldAnimate(), so "animations
// off" makes this an instant, correct swap rather than a slow one.
void PresentMode(InspectRailView const& v, bool messageMode) {
  auto incoming = messageMode ? v.messageScroll : v.conversationScroll;
  auto outgoing = messageMode ? v.conversationScroll : v.messageScroll;
  if (!incoming) return;
  const bool swapping = outgoing && outgoing.Visibility() == Visibility::Visible;
  urnw::motion::CrossfadePageSwap(swapping ? outgoing : incoming, incoming);
}

ScrollViewer MakeBodyScroller() {
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroller.Visibility(Visibility::Collapsed);
  StackPanel body;
  scroller.Content(body);
  return scroller;
}

// Conversation mode's subject: the identicon, the name, and one muted line under
// it. 56 tall so a 40x40 identicon sits in it on the pane's own rhythm.
// MakeIdenticon applies its OWN CornerRadius(8) - nothing here sets one.
FrameworkElement MakeSubjectRow(demo::Conversation const& conv) {
  auto root = kit::MakePaneRow(56);

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  auto identicon = urmsg::MakeIdenticon(conv.identityKey, 40);
  identicon.VerticalAlignment(VerticalAlignment::Center);
  automation::AutomationProperties::SetAccessibilityView(
      identicon, automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(identicon);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);
  TextBlock name;
  if (auto style = StyleByKey(L"UrRowTitleStyle")) name.Style(style);
  name.Text(H(conv.name));
  text.Children().Append(name);

  TextBlock note;
  if (auto style = StyleByKey(L"UrRowNoteStyle")) note.Style(style);
  // members.size(), NOT memberCount. DemoWorld's --diagnose invariant 2 requires
  // them equal, and members.size() is what decides how many rows the MEMBERS
  // list below actually draws - so this line and that list cannot disagree in a
  // screenshot, whatever memberCount happens to say.
  note.Text(H(conv.kind == demo::ConversationKind::Group
                  ? std::format(L"Group, {} members", conv.members.size())
                  : std::wstring{L"Direct message"}));
  text.Children().Append(note);
  Grid::SetColumn(text, 1);
  grid.Children().Append(text);

  root.Child(grid);
  return root;
}

// THE file's only MakePaneKeyValueRow call site. Both modes append their fields
// through here, which is what makes RailValueOr unskippable.
void AppendFieldRows(StackPanel const& panel, std::vector<InspectField> const& fields) {
  auto body = panel.Children();
  for (auto const& field : fields)
    body.Append(
        kit::MakePaneKeyValueRow(H(field.key), winrt::hstring{RailValueOr(field.value)}, 34)
            .root);
}

// POPULATE ONLY. No storyboard, so the density switch can call this on a rail
// that is already on screen without the rail flashing.
void PopulateConversation(InspectRailView const& v, demo::Conversation const& conv,
                          bool advanced) {
  auto panel = BodyOf(v.conversationScroll);
  if (!panel) return;
  auto body = panel.Children();
  body.Clear();
  body.Append(MakeSubjectRow(conv));

  body.Append(kit::MakePaneGroupHeader(L"MEMBERS", H(std::format(L"{}", conv.members.size())))
                  .root);
  if (conv.members.empty()) {
    body.Append(kit::MakePaneEmptyLine(L"No members"));
  } else {
    for (auto const& member : conv.members) body.Append(MakeMemberRow(member).root);
  }

  body.Append(kit::MakePaneGroupHeader(L"RETENTION").root);
  AppendFieldRows(panel, BuildConversationFields(conv, advanced));
}

}  // namespace

InspectRailView MakeInspectRail() {
  InspectRailView v;

  Grid root;
  // The view paints its own surface rather than depending on the host's style:
  // a view module that is transparent unless its container happens to carry
  // UrPaneStyle is a module with an invisible dependency.
  root.Background(urnw::colors::BackgroundBrush());
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  root.RowDefinitions().Append(headerRow);
  root.RowDefinitions().Append(bodyRow);

  // The 40 DIP strip every pane in this window opens with, by the same two keys
  // MainWindow.xaml uses in markup (lines 180-188), so a pane built in code and
  // a pane declared in XAML are the same pane.
  Border header;
  if (auto style = StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  TextBlock title;
  if (auto style = StyleByKey(L"UrPaneTitleStyle")) title.Style(style);
  title.Text(L"Details");
  header.Child(title);
  root.Children().Append(header);

  // Both bodies live in the same Grid cell, both collapsed. See the header.
  v.conversationScroll = MakeBodyScroller();
  Grid::SetRow(v.conversationScroll, 1);
  root.Children().Append(v.conversationScroll);

  v.messageScroll = MakeBodyScroller();
  Grid::SetRow(v.messageScroll, 1);
  root.Children().Append(v.messageScroll);

  v.root = root;
  SetRailAdvanced(v, false);
  SetRailSubject(v, L"", L"");
  return v;
}

kit::PaneListRow MakeMemberRow(demo::MemberRef const& member) {
  auto row = kit::MakePaneListRow(36);
  const size_t online = OnlineDeviceCount(member);
  row.dot.Fill(0 < online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                          : urnw::colors::FaintBrush());

  std::wstring title = member.displayName;
  if (member.admin) title += L" \u00B7 Admin";  // U+00B7 MIDDLE DOT
  row.title.Text(H(title));

  // The meta states the presence in WORDS. That is what entitles the dot beside
  // it to remain a restatement - BuildPaneListRowParts marks that dot Raw on
  // exactly that assumption (UrComponents.cpp:246-248) - and it is how this row
  // obeys "colour is never the only carrier of state".
  const std::wstring presence = std::format(L"{}/{} online", online, member.devices.size());
  row.meta.Text(H(presence));

  // MakePaneListRow does NOT mark title and meta Raw; only MakePaneListRowButton
  // does (UrComponents.cpp:295-298). Without these two lines the row's own name
  // is announced and then both children are announced again after it - the exact
  // triple announcement the row name exists to prevent. A THIRD caller needing
  // this is the signal to move the two calls down into BuildPaneListRowParts so
  // both row forms carry it; MakeDeviceRow below is the second.
  automation::AutomationProperties::SetAccessibilityView(
      row.title, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetAccessibilityView(
      row.meta, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetName(row.root, H(title + L", " + presence));
  return row;
}

kit::PaneListRow MakeDeviceRow(demo::DeviceRef const& device) {
  auto row = kit::MakePaneListRow(36);
  row.dot.Fill(device.online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                             : urnw::colors::FaintBrush());

  std::wstring title = device.name;
  const std::wstring owner =
      device.isThisComputer ? std::wstring{L"This computer"} : device.ownerName;
  if (!owner.empty()) title += L" \u00B7 " + owner;  // U+00B7 MIDDLE DOT
  row.title.Text(H(title));

  // Same rule as MakeMemberRow: the WORD carries the state and the dot restates
  // it. An online device reads "online"; an offline one reads when it was last
  // seen. Nothing here is legible only by hue.
  const std::wstring meta = device.online ? std::wstring{L"online"} : device.lastSeenLabel;
  row.meta.Text(H(meta));

  automation::AutomationProperties::SetAccessibilityView(
      row.title, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetAccessibilityView(
      row.meta, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetName(
      row.root, H(title + L", " + (device.online ? L"online" : L"offline") + L", " +
                  device.lastSeenLabel));
  return row;
}

void SetInspectRailConversation(InspectRailView& v, demo::Conversation const& c) {
  if (!v.root) return;
  PopulateConversation(v, c, RailAdvanced(v));
  SetRailSubject(v, c.id, L"");
  PresentMode(v, /*messageMode=*/false);
  urnw::LogInfo("rail: conversation mode -> {} ({} members)", winrt::to_string(c.id),
                c.members.size());
}

}  // namespace urmsg::views
