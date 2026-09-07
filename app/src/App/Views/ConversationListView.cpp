// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/ConversationListView.h"

#include <cstdint>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Text.h>

#include "Identicon.h"
#include "Localization.h"
#include "Log.h"
#include "UrColors.h"
#include "Views/ConversationRowModel.h"

using namespace winrt::Microsoft::UI::Xaml;

namespace urmsg::views {
namespace {

// Tags, so the state setters in later tasks can find the three elements this
// file builds without ConversationListView growing a field for each of them
// (contract 4 fixes the struct at root + rows).
constexpr wchar_t kTagSelectionBar[] = L"ur.selbar";
constexpr wchar_t kTagTrailing[] = L"ur.trailing";
constexpr wchar_t kTagCluster[] = L"ur.cluster";
constexpr wchar_t kTagGroupChip[] = L"ur.groupchip";

// Segoe Fluent Icons out of the app dictionary, so this file and App.xaml cannot
// name two different families. FontIcon otherwise defaults to the older Segoe
// MDL2 Assets, whose metrics differ -- UrComponents.cpp:25-29 records the same
// trap. This is NOT a copy of StyleByKey: it reads one resource, by one name.
Media::FontFamily IconFont() {
  if (auto app = Application::Current()) {
    auto key = winrt::box_value(winrt::hstring{L"UrIconFontFamily"});
    if (app.Resources().HasKey(key))
      if (auto family = app.Resources().Lookup(key).try_as<Media::FontFamily>()) return family;
  }
  return Media::FontFamily(L"Segoe Fluent Icons");
}

// A state mark on the row's SECOND line. 12, not UrRowIconStyle's 16
// (App.xaml:523-530): a 16px glyph beside 11px note text outweighs the name
// above it.
Controls::FontIcon MakeRowGlyph(winrt::hstring const& glyph) {
  Controls::FontIcon icon;
  icon.FontFamily(IconFont());
  icon.FontSize(12);
  icon.Glyph(glyph);
  icon.Foreground(urnw::colors::MutedBrush());
  icon.VerticalAlignment(VerticalAlignment::Center);
  // The row's automation name already says "muted" / "disappearing messages".
  Automation::AutomationProperties::SetAccessibilityView(
      icon, Automation::Peers::AccessibilityView::Raw);
  return icon;
}

// The unread pill: UrAccentBrush #EFF7BB with UrInverseTextBrush #101010 text,
// per design 6.1. kProGold appears nowhere, and this is the ONLY chromatic mark
// on a row -- Spec C 4.1: a list row never carries a delivery-state colour.
Controls::Border MakeUnreadPill(std::wstring const& text) {
  Controls::Border pill;
  pill.Background(urnw::colors::AccentBrush());
  // CornerRadius is a plain struct {TopLeft, TopRight, BottomRight, BottomLeft};
  // all four are the same here, so the declaration order cannot be got wrong.
  pill.CornerRadius(CornerRadius{9, 9, 9, 9});
  pill.Height(18);
  pill.MinWidth(18);
  pill.Padding(ThicknessHelper::FromLengths(6, 0, 6, 0));
  pill.VerticalAlignment(VerticalAlignment::Center);

  Controls::TextBlock count;
  count.Text(winrt::hstring{text});
  count.FontSize(11);
  count.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
  count.Foreground(urnw::colors::MakeBrush(urnw::colors::kInverseText));
  count.HorizontalAlignment(HorizontalAlignment::Center);
  count.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetAccessibilityView(
      count, Automation::Peers::AccessibilityView::Raw);
  pill.Child(count);
  return pill;
}

urnw::kit::PaneTwoLineRowButton MakeConversationRow(urmsg::demo::Conversation const& c,
                                                    int index,
                                                    std::function<void(int)> const& onSelect) {
  const ConversationRowModel model = MakeConversationRowModel(c);

  auto row = urnw::kit::MakePaneTwoLineRowButton(winrt::hstring{model.name},
                                                 winrt::hstring{model.preview},
                                                 kConversationRowHeight);
  auto grid = row.root ? row.root.Content().try_as<Controls::Grid>() : nullptr;
  if (!grid) return row;  // no kit grid: the row still draws its two lines

  // The kit's three children, read BEFORE anything of ours is appended. At this
  // point grid.Children() is exactly {StackPanel text, TextBlock value, FontIcon
  // chevron} (UrComponents.cpp:446-464), so TYPE identifies each one and no
  // child-ORDER assumption is made.
  Controls::StackPanel text{nullptr};
  Controls::FontIcon chevron{nullptr};
  for (auto const& child : grid.Children()) {
    if (auto panel = child.try_as<Controls::StackPanel>()) text = panel;
    if (auto icon = child.try_as<Controls::FontIcon>()) chevron = icon;
  }

  // 1. The chevron and its column go. Spec C 4.1's row anatomy has no disclosure
  //    mark, and an empty Auto column would still cost the grid's 10dip
  //    ColumnSpacing on each side of it.
  if (chevron) {
    uint32_t at = 0;
    if (grid.Children().IndexOf(chevron, at)) grid.Children().RemoveAt(at);
  }
  if (2 < grid.ColumnDefinitions().Size()) grid.ColumnDefinitions().RemoveAt(2);

  // 2. Selection channel 2, built on EVERY row and transparent until
  //    SetConversationSelected paints it, so a selected row and an unselected one
  //    measure identically -- the reason kit::BuildPaneListRowParts builds its
  //    marker for both row forms (UrComponents.cpp:206-211). Margin -10 is the
  //    kit's own number and lands the bar at the kit's own x: both grids sit
  //    inside UrPaneRowButtonStyle's Padding="12,0" (App.xaml:897), so -10 draws
  //    it 2 dip in from the row's left edge.
  Controls::Border bar;
  bar.Tag(winrt::box_value(winrt::hstring{kTagSelectionBar}));
  bar.Width(2);
  bar.Margin(ThicknessHelper::FromLengths(-10, 0, 0, 0));
  bar.HorizontalAlignment(HorizontalAlignment::Left);
  bar.VerticalAlignment(VerticalAlignment::Stretch);
  bar.Background(urnw::colors::AccentBrush());
  bar.Opacity(0.0);
  Automation::AutomationProperties::SetAccessibilityView(
      bar, Automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(bar);

  // 3. The identicon shares column 0 with the text and is pinned left; the text
  //    is pushed 52 dip clear of it (40 identicon + the pane's 12dip gutter).
  //    Two children in one star cell rather than a fourth ColumnDefinition,
  //    because inserting a column at 0 would renumber every Grid::SetColumn the
  //    kit already set on children this file does not own. MakeIdenticon applies
  //    its own CornerRadius(8) (contract 3) -- do NOT set one here.
  auto identicon = urmsg::MakeIdenticon(c.identityKey, 40);
  identicon.HorizontalAlignment(HorizontalAlignment::Left);
  identicon.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetAccessibilityView(
      identicon, Automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(identicon);
  if (text) text.Margin(ThicknessHelper::FromLengths(52, 0, 0, 0));

  // 4. The trailing column: the time on the title's line, the state marks under
  //    it. row.value is the kit's own trailing TextBlock (UrValueTextStyle,
  //    muted, already AccessibilityView=Raw, UrComponents.cpp:448-474) and is
  //    MOVED here rather than duplicated: a UIElement has exactly one parent, so
  //    it must leave grid.Children() before this Append or the append throws.
  Controls::StackPanel trailing;
  trailing.Tag(winrt::box_value(winrt::hstring{kTagTrailing}));
  trailing.Spacing(4);
  trailing.HorizontalAlignment(HorizontalAlignment::Right);
  trailing.VerticalAlignment(VerticalAlignment::Center);
  Controls::Grid::SetColumn(trailing, 1);
  if (row.value) {
    uint32_t at = 0;
    if (grid.Children().IndexOf(row.value, at)) grid.Children().RemoveAt(at);
    row.value.Text(winrt::hstring{model.timeLabel});
    row.value.HorizontalAlignment(HorizontalAlignment::Right);
    trailing.Children().Append(row.value);
  }

  // The second trailing line. A COLLAPSED child costs a StackPanel no Spacing --
  // it is skipped entirely, which is exactly why kit::SetTextOrCollapse sets
  // Visibility instead of clearing Text (UrComponents.h:99-107) -- so a quiet row
  // shows a clean one-line trailing column.
  Controls::StackPanel cluster;
  cluster.Tag(winrt::box_value(winrt::hstring{kTagCluster}));
  cluster.Orientation(Controls::Orientation::Horizontal);
  cluster.Spacing(6);
  cluster.HorizontalAlignment(HorizontalAlignment::Right);

  // Advanced Mode only, and never on a DM (model.groupIdChip is empty there).
  // Built collapsed on every row; L6's SetConversationListAdvanced is the only
  // thing that ever shows it, which is what keeps that call a DENSITY change and
  // not a rebuild.
  Controls::TextBlock chip;
  chip.Tag(winrt::box_value(winrt::hstring{kTagGroupChip}));
  chip.Text(winrt::hstring{model.groupIdChip});
  chip.FontSize(10);
  chip.Foreground(urnw::colors::FaintBrush());
  chip.VerticalAlignment(VerticalAlignment::Center);
  chip.Visibility(Visibility::Collapsed);
  Automation::AutomationProperties::SetAccessibilityView(
      chip, Automation::Peers::AccessibilityView::Raw);
  cluster.Children().Append(chip);

  auto muted = MakeRowGlyph(L"\uE74F");  // Mute
  muted.Visibility(model.showMuted ? Visibility::Visible : Visibility::Collapsed);
  cluster.Children().Append(muted);

  auto timer = MakeRowGlyph(L"\uE916");  // Stopwatch
  timer.Visibility(model.showTimer ? Visibility::Visible : Visibility::Collapsed);
  cluster.Children().Append(timer);

  auto pill = MakeUnreadPill(model.unread);
  pill.Visibility(model.unread.empty() ? Visibility::Collapsed : Visibility::Visible);
  cluster.Children().Append(pill);

  trailing.Children().Append(cluster);
  grid.Children().Append(trailing);

  // The row's WHOLE announcement. The kit set Name to the title and
  // FullDescription to the note (UrComponents.cpp:475-476); this name already
  // carries both, plus the unread count, the muted state and the time, so the
  // description is cleared rather than left to repeat the preview after it.
  // title/note/value are already AccessibilityView=Raw, so nothing inside the
  // row is announced twice.
  Automation::AutomationProperties::SetName(
      row.root, winrt::hstring{ConversationRowAutomationName(c, false)});
  Automation::AutomationProperties::SetFullDescription(row.root, L"");

  // The index by value, full stop: nothing captured here refers into `world`.
  row.root.Click([index, onSelect](winrt::Windows::Foundation::IInspectable const&,
                                   RoutedEventArgs const&) {
    if (onSelect) onSelect(index);
  });
  return row;
}

}  // namespace

ConversationListView MakeConversationList(urmsg::demo::World const& world,
                                          std::function<void(int)> onSelect) {
  ConversationListView view;

  Controls::StackPanel stack;
  // The 28px group rhythm the pane already uses, with NO count on it.
  // ListPaneCount in the pane header is the ONE "how many rows are on screen"
  // readout and L5's filter updates it; a second count that the filter did not
  // update would be a readout that had stopped being true. group_recent already
  // exists in the generated resw with the value "RECENT", so no key is added.
  auto group = urnw::kit::MakePaneGroupHeader(winrt::hstring{urnw::Localized("group_recent")});
  stack.Children().Append(group.root);

  view.rows.reserve(world.conversations.size());
  for (std::size_t i = 0; i < world.conversations.size(); ++i) {
    auto row = MakeConversationRow(world.conversations[i], static_cast<int>(i), onSelect);
    stack.Children().Append(row.root);
    view.rows.push_back(row);
  }

  view.root = stack;
  urnw::LogInfo("list: built {} conversation rows at {:.0f} dip", view.rows.size(),
                kConversationRowHeight);
  return view;
}

}  // namespace urmsg::views
