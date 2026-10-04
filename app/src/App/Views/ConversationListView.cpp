// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/ConversationListView.h"

#include <cstdint>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Text.h>

#include "Identicon.h"
#include "Localization.h"
#include "Log.h"
#include "UrColors.h"
#include "UrMotion.h"
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

// Channel-exact brush comparison (A,R,G,B all compared): the hover protocol in
// MakeConversationRow recognises its own repaint by the bar's brush being the
// brand hairline, and winrt::Windows::UI::Color has no operator== in the
// projection.
bool IsBrush(Media::Brush const& brush, winrt::Windows::UI::Color const& color) {
  auto solid = brush.try_as<Media::SolidColorBrush>();
  if (!solid) return false;
  auto const c = solid.Color();
  return c.A == color.A && c.R == color.R && c.G == color.G && c.B == color.B;
}

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

// The first DIRECT child of `panel` carrying `tag`. Shallow by design: every
// element this file tags is a direct child of the panel it is looked up from, so
// a recursive walk would only hide a mistake. Null-safe at every hop -- try_as on
// an empty handle dereferences null.
FrameworkElement TaggedChild(Controls::Panel const& panel, wchar_t const* tag) {
  if (!panel) return nullptr;
  for (auto const& child : panel.Children()) {
    auto element = child.try_as<FrameworkElement>();
    if (!element) continue;
    auto value = element.Tag().try_as<winrt::hstring>();
    if (value && *value == tag) return element;
  }
  return nullptr;
}

// The same lookup, narrowed to a Panel, so a chain of them reads as one
// expression without a null check between every hop.
Controls::Panel TaggedPanel(Controls::Panel const& parent, wchar_t const* tag) {
  auto child = TaggedChild(parent, tag);
  if (!child) return nullptr;
  return child.try_as<Controls::Panel>();
}

// The same narrowing, to a Border: SetConversationSelected writes the
// selection bar's BRUSH as well as its opacity (see it for why), which a
// FrameworkElement cannot do.
Controls::Border TaggedBorder(Controls::Panel const& parent, wchar_t const* tag) {
  auto child = TaggedChild(parent, tag);
  if (!child) return nullptr;
  return child.try_as<Controls::Border>();
}

// The entrance start pose (d3 2.6): invisible AND 8dip low. Only ever written
// under ShouldAnimate() - the two-pose-skip MakeConversationList documents -
// so the reduce-motion path has no pose to clear and no residual transform for
// a later layout pass to trip on.
void EntranceStartPose(FrameworkElement const& element) {
  element.Opacity(0.0);
  Media::CompositeTransform pose;
  pose.TranslateY(urnw::motion::kDist8);
  element.RenderTransform(pose);
}

// One element's entrance as two timelines on `board` sharing kBaseMs, the
// standard bezier and `beginMs` (d3 2.6): the fade this file already ran,
// extended with the rise rather than restructured. No new duration, no new
// curve, no new stagger constant anywhere in the wave - the stagger is
// ConversationRowDelayMs, whose cap L1's demo.list.stagger asserts
// exhaustively at index 0/5/6/50.
void AppendEntrance(winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard const& board,
                    FrameworkElement const& target, int64_t beginMs) {
  namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
  auto fade = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kBaseMs, beginMs,
                                             urnw::motion::kStandardP1,
                                             urnw::motion::kStandardP2);
  anim::Storyboard::SetTarget(fade, target);
  anim::Storyboard::SetTargetProperty(fade, L"Opacity");
  board.Children().Append(fade);
  auto rise = urnw::motion::MakeSplineDouble(urnw::motion::kDist8, 0.0,
                                             urnw::motion::kBaseMs, beginMs,
                                             urnw::motion::kStandardP1,
                                             urnw::motion::kStandardP2);
  anim::Storyboard::SetTarget(rise, target);
  anim::Storyboard::SetTargetProperty(
      rise, L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)");
  board.Children().Append(rise);
}

// The kit row's content Grid. MakePaneTwoLineRowButton ends with
// out.root.Content(grid) (UrComponents.cpp:478), so this is the documented
// shape rather than a guess.
Controls::Grid RowGrid(urnw::kit::PaneTwoLineRowButton const& row) {
  if (!row.root) return nullptr;
  return row.root.Content().try_as<Controls::Grid>();
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

  // A timer row's second line names the STATE instead of showing a preview
  // (d3 2.4): the model REFUSES the preview for a disappearing conversation
  // (ConversationRowModel.cpp, the property demo.list.timer asserts), which
  // leaves the kit's note collapsed and the 64px row reading half-empty.
  // What lands instead is the timer glyph plus "Disappearing messages", both
  // textFaint at UrRowNoteStyle's 11px. G4: the string describes the FIELD,
  // never a claim - and a screen reader hears no change, because the row's
  // automation name already says exactly these words (which is also why both
  // elements are AccessibilityView=Raw). An English literal from code:
  // Resources.resw is generated and no task here may add a key (DemoChip
  // precedent). The search privacy gate is untouched: ConversationRowMatches
  // reads the name and the hidden preview, never this label.
  if (model.showTimer && text) {
    Controls::StackPanel note;
    note.Orientation(Controls::Orientation::Horizontal);
    note.Spacing(4);
    Controls::FontIcon glyph;
    glyph.FontFamily(IconFont());
    glyph.FontSize(12);
    glyph.Glyph(L"\uE916");  // Stopwatch
    glyph.Foreground(urnw::colors::FaintBrush());
    glyph.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetAccessibilityView(
        glyph, Automation::Peers::AccessibilityView::Raw);
    Controls::TextBlock label;
    label.Text(L"Disappearing messages");
    label.FontSize(11);
    label.Foreground(urnw::colors::FaintBrush());
    label.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetAccessibilityView(
        label, Automation::Peers::AccessibilityView::Raw);
    note.Children().Append(glyph);
    note.Children().Append(label);
    text.Children().Append(note);
  }

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
    // 13 -> 11 (d3 2.1): at 13 the time outweighed the preview line and tied
    // with the name; at 11 it aligns with the cluster beneath it and the 13px
    // name becomes the clear primary. A one-off override on a code-built row,
    // not a new text species - UrValueTextStyle and its muted brush stay (G3:
    // no new colour, no new key).
    row.value.FontSize(11);
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

  // NO timer glyph here (polish B2): when model.showTimer is set the row's
  // second line IS the timer glyph plus "Disappearing messages" (above), so
  // the cluster's copy drew the same mark twice on one row. The cluster keeps
  // the mute glyph and the unread pill. demo.list.timer still passes: it
  // gates the MODEL (showTimer == c.disappearing and the preview refused),
  // which this does not touch - it never read the cluster.
  auto pill = MakeUnreadPill(model.unread);
  pill.Visibility(model.unread.empty() ? Visibility::Collapsed : Visibility::Visible);
  cluster.Children().Append(pill);

  trailing.Children().Append(cluster);
  grid.Children().Append(trailing);

  // 5. Hover and press (d3 2.2/2.3). Hover rehearses selection channel 2's
  //    GEOMETRY without spending its colour: the bar - accent-coloured,
  //    transparent and in place on every row - is painted with the brand
  //    hairline (UrBorderBrush, white at 12%) exactly where selection would
  //    land, and selection later COMMITS that slot to accent. A shape change,
  //    not colour alone, and border-white cannot be mistaken for #EFF7BB.
  //    The protocol with SetConversationSelected: selection writes the accent
  //    brush AND the opacity, hover writes the border brush and the opacity,
  //    so a bar in the border brush is recognisably hover's own repaint and
  //    PointerExited only ever undoes that - an accent brush at exit means
  //    selection wrote the bar while the pointer was over the row, and the
  //    slot belongs to selection. Hover on the selected row is a no-op: the
  //    accent bar already owns the slot. One accepted wrinkle: any selection
  //    write restores every unselected bar to accent+transparent, so a hover
  //    tick does not survive an unrelated selection change until re-entry.
  //    In the same pair the identicon plate's alpha lifts one step
  //    (0x33 -> 0x4D, 20% -> 30% of the row's own hue): the avatar visibly
  //    wakes under the pointer with no new colour entering the app - the
  //    plate is already WithAlpha(hue, ...), so this is a parameter change
  //    on an existing brush (G3: the hue is the compile-time-proven palette;
  //    only alpha moves).
  row.root.PointerEntered([bar, identicon](auto const&, auto const&) {
    if (bar.Opacity() != 1.0) {  // a selected row: the accent bar owns the slot
      bar.Background(urnw::colors::BorderBrush());
      bar.Opacity(1.0);
    }
    urmsg::SetIdenticonPlateAlpha(identicon, 0x4D);
  });
  row.root.PointerExited([bar, identicon](auto const&, auto const&) {
    if (IsBrush(bar.Background(), urnw::colors::kBorder)) {
      bar.Background(urnw::colors::AccentBrush());
      bar.Opacity(0.0);
    }
    urmsg::SetIdenticonPlateAlpha(identicon, 0x33);
  });

  // 6. Press feedback is the identicon's, not the row's (d3 2.2): a
  //    full-width pane row shrinking reads as a bug, so the avatar takes the
  //    press - kPressScale over kMicroMs and back. Installed ONLY when
  //    ShouldAnimate(): with motion off there is no animation to run and
  //    never installing the handlers IS the instant fallback (that branch is
  //    code-inspection only: reduce-motion has never executed on this
  //    machine). AddHandler with handledEventsToo because ButtonBase marks
  //    PointerPressed/Released handled for its own click logic, which would
  //    otherwise eat the event before these handlers see it.
  if (urnw::motion::ShouldAnimate()) {
    Media::ScaleTransform scale;
    scale.CenterX(20);  // half the 40dip identicon: shrink in place
    scale.CenterY(20);
    identicon.RenderTransform(scale);
    auto runScale = [identicon](double from, double to) {
      namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
      anim::Storyboard sb;
      for (wchar_t const* property :
           {L"(UIElement.RenderTransform).(ScaleTransform.ScaleX)",
            L"(UIElement.RenderTransform).(ScaleTransform.ScaleY)"}) {
        auto step = urnw::motion::MakeSplineDouble(from, to, urnw::motion::kMicroMs, 0,
                                                   urnw::motion::kStandardP1,
                                                   urnw::motion::kStandardP2);
        anim::Storyboard::SetTarget(step, identicon);
        anim::Storyboard::SetTargetProperty(step, property);
        sb.Children().Append(step);
      }
      sb.Begin();
    };
    row.root.AddHandler(
        UIElement::PointerPressedEvent(),
        winrt::box_value(Input::PointerEventHandler(
            [runScale](auto const&, auto const&) { runScale(1.0, urnw::motion::kPressScale); })),
        true);
    auto restore = Input::PointerEventHandler(
        [runScale](auto const&, auto const&) { runScale(urnw::motion::kPressScale, 1.0); });
    row.root.AddHandler(UIElement::PointerReleasedEvent(), winrt::box_value(restore), true);
    // A press that loses capture mid-hold (drag off, window change) still
    // restores, or the avatar would stay shrunk.
    row.root.AddHandler(UIElement::PointerCaptureLostEvent(), winrt::box_value(restore), true);
  }

  // Unread rows bold the NAME, never anything chromatic (d3 2.1): Signal's
  // unread-bold as a WEIGHT channel, so the pill stays the only chromatic
  // mark a row may carry (Spec C 4.1). Weight is not colour (G3), and rows
  // with nothing unread change by zero pixels.
  if (!model.unread.empty() && row.title)
    row.title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

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
  // UrGroupHeaderStyle's sheet fill swept the band into the header+search
  // chrome slab above it (d3 2.5 fuses exactly THOSE two); the band titles
  // the ROWS, so it drops to the page tone they sit on and rejoins them
  // (polish B2). The style's hairlines stay - only the fill goes.
  group.root.Background(urnw::colors::MakeBrush({0, 0, 0, 0}));
  stack.Children().Append(group.root);
  view.header = group.root;

  view.rows.reserve(world.conversations.size());
  for (std::size_t i = 0; i < world.conversations.size(); ++i) {
    auto row = MakeConversationRow(world.conversations[i], static_cast<int>(i), onSelect);
    stack.Children().Append(row.root);
    view.rows.push_back(row);
  }

  // The START pose, written here because MakeConversationList runs from the
  // MainWindow CONSTRUCTOR, before Activate(). WindowReveal.h:50-54 is the rule:
  // write the start pose synchronously ahead of the first composed frame and
  // START after Activate -- which AnimateConversationListEntrance does from
  // MainWindow::StartReveal. Gated, so that with animations off in Windows no
  // element is ever written to 0 and none can be left there. d3 2.6 extended
  // the SAME two-pose-skip rather than restructuring it: the rise's start
  // pose (a CompositeTransform at TranslateY kDist8) lives inside the same
  // `if` as the opacity's, so the reduce-motion path still writes nothing at
  // all (code-inspection only: that branch has never executed on this
  // machine).
  if (urnw::motion::ShouldAnimate()) {
    for (auto const& row : view.rows)
      if (row.root) EntranceStartPose(row.root);
    if (view.header) EntranceStartPose(view.header);
  }

  view.root = stack;
  urnw::LogInfo("list: built {} conversation rows at {:.0f} dip", view.rows.size(),
                kConversationRowHeight);
  return view;
}

void SetConversationSelected(ConversationListView& v, int index) {
  auto const& world = urmsg::demo::GetWorld();
  for (std::size_t i = 0; i < v.rows.size(); ++i) {
    auto const& row = v.rows[i];
    if (!row.root) continue;
    const bool selected = (static_cast<int>(i) == index);

    // Channel 1: the fill step. Deliberately the same step the kit uses, and
    // deliberately the same value UrPaneRowButtonStyle paints on PointerOver
    // (App.xaml:931-935) -- a hovered row is indistinguishable from a selected
    // one on this channel alone, which is why the next two exist.
    row.root.Background(selected ? urnw::colors::CardBrush()
                                 : urnw::colors::MakeBrush({0, 0, 0, 0}));

    // Channel 2: the 2px leading accent bar. A SHAPE change, not colour alone.
    // The brush write is load-bearing, not hygiene: hover rehearses this same
    // bar in the border hairline (MakeConversationRow section 5), and writing
    // the accent brush here means selection always reclaims the bar's colour
    // outright - a hover repaint can never survive INTO selection and turn
    // the selected bar white.
    if (auto bar = TaggedBorder(RowGrid(row), kTagSelectionBar)) {
      bar.Background(urnw::colors::AccentBrush());
      bar.Opacity(selected ? 1.0 : 0.0);
    }

    // Channel 3: the announcement. A fill step and a bar say nothing to a screen
    // reader, so the row's own Name carries the state.
    if (i < world.conversations.size())
      Automation::AutomationProperties::SetName(
          row.root,
          winrt::hstring{ConversationRowAutomationName(world.conversations[i], selected)});
  }
  urnw::LogInfo("list: selection -> row {} of {}", index, v.rows.size());
}

void AnimateConversationListEntrance(ConversationListView& v) {
  namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
  if (!urnw::motion::ShouldAnimate()) {
    urnw::LogInfo("list: entrance skipped ({} rows, motion off)", v.rows.size());
    return;
  }
  // A LOCAL Storyboard, begun and left: the shape every hand-built animation in
  // this app already uses (UrMotion.cpp:90-115), because a running Storyboard is
  // held by the timing manager.
  anim::Storyboard board;
  // The RECENT header at delay 0 (d3 2.6), so the chrome doesn't pop a frame
  // ahead of the rows it titles. Same kBaseMs, same standard bezier - the
  // header is one more target, not a new timeline shape.
  if (v.header) AppendEntrance(board, v.header, 0);
  for (std::size_t i = 0; i < v.rows.size(); ++i) {
    auto const& row = v.rows[i];
    if (!row.root) continue;
    AppendEntrance(board, row.root, ConversationRowDelayMs(i));
  }
  board.Begin();
  urnw::LogInfo("list: entrance armed ({} rows, last begins at {} ms)", v.rows.size(),
                ConversationRowDelayMs(v.rows.empty() ? 0 : v.rows.size() - 1));
}

std::size_t ApplyConversationListFilter(ConversationListView& v,
                                        urmsg::demo::World const& world,
                                        std::wstring const& query) {
  std::size_t visible = 0;
  for (std::size_t i = 0; i < v.rows.size(); ++i) {
    auto const& row = v.rows[i];
    if (!row.root) continue;
    // A row with no conversation behind it (a world that shrank under a built
    // list) is hidden rather than left showing stale text.
    const bool keep = (i < world.conversations.size()) &&
                      ConversationRowMatches(world.conversations[i], query);
    row.root.Visibility(keep ? Visibility::Visible : Visibility::Collapsed);
    if (keep) ++visible;
  }
  urnw::LogInfo("list: filter \"{}\" -> {} of {}", winrt::to_string(winrt::hstring{query}),
                visible, v.rows.size());
  return visible;
}

void SetConversationListAdvanced(ConversationListView& v, bool advanced) {
  int shown = 0;
  int chips = 0;
  for (auto const& row : v.rows) {
    auto cluster = TaggedPanel(TaggedPanel(RowGrid(row), kTagTrailing), kTagCluster);
    auto chip = TaggedChild(cluster, kTagGroupChip);
    if (!chip) continue;
    auto text = chip.try_as<Controls::TextBlock>();
    if (!text) continue;
    ++chips;
    // Empty on every DM (ConversationRowModel::groupIdChip), so Advanced Mode
    // never puts an empty chip and its 6dip of Spacing on a direct message.
    const bool show = advanced && !text.Text().empty();
    text.Visibility(show ? Visibility::Visible : Visibility::Collapsed);
    if (show) ++shown;
  }
  urnw::LogInfo("list: advanced {} -> {} of {} group-id chips visible",
                advanced ? "on" : "off", shown, chips);
}

}  // namespace urmsg::views
