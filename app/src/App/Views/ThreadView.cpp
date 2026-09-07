// SPDX-License-Identifier: MPL-2.0
#include "pch.h"  // /Yu"pch.h": every normal TU includes it FIRST

#include "Views/ThreadView.h"

#include <map>
#include <memory>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "Demo/ThreadLayout.h"
#include "Identicon.h"
#include "UrColors.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace urmsg::views {
namespace {

namespace demo = urmsg::demo;

// UrComponents.cpp's StyleByKey/MetricByKey are file-local to that unit, so
// this one needs its own pair. Applying styles by KEY rather than by hand is
// what keeps the bubble in step with App.xaml; a missing key must not throw a
// layout away.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

Media::Brush BrushByKey(wchar_t const* key, winrt::Windows::UI::Color fallback) {
  auto app = Application::Current();
  if (app) {
    auto boxed = winrt::box_value(winrt::hstring{key});
    if (app.Resources().HasKey(boxed))
      if (auto b = app.Resources().Lookup(boxed).try_as<Media::Brush>()) return b;
  }
  return urnw::colors::MakeBrush(fallback);
}

void MarkRaw(UIElement const& e) {
  Automation::AutomationProperties::SetAccessibilityView(
      e, Automation::Peers::AccessibilityView::Raw);
}

// The delivery reading under the last outgoing bubble of a run. Right-aligned,
// 12px, and for the one state that must never rest on a 12px glyph the word
// is beside it in the danger brush — colour is a second channel here, never
// the only one.
FrameworkElement MakeDeliveryLine(demo::MessageRow const& row) {
  StackPanel line;
  line.Orientation(Orientation::Horizontal);
  line.Spacing(4);
  line.HorizontalAlignment(HorizontalAlignment::Right);
  line.Margin(ThicknessHelper::FromLengths(0, 2, 2, 0));

  const bool failed = (row.state == demo::DeliveryState::Failed);

  FontIcon icon;
  icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
  icon.FontSize(12);
  icon.Glyph(winrt::hstring{DeliveryGlyph(row.state)});
  icon.Foreground(failed ? urnw::colors::DangerBrush()
                         : (row.state == demo::DeliveryState::Read
                                ? urnw::colors::TextBrush()
                                : urnw::colors::MutedBrush()));
  icon.VerticalAlignment(VerticalAlignment::Center);
  // the bubble's automation name already says the delivery WORD; announcing
  // the glyph again would put a second item beside the thing it describes
  MarkRaw(icon);
  line.Children().Append(icon);

  if (failed) {
    TextBlock word;
    word.Text(winrt::hstring{DeliveryWord(row.state)});
    if (auto s = StyleByKey(L"UrCaptionTextStyle")) word.Style(s);
    word.FontSize(11);
    word.Foreground(urnw::colors::DangerBrush());
    word.VerticalAlignment(VerticalAlignment::Center);
    MarkRaw(word);
    line.Children().Append(word);
  }
  return line;
}

}  // namespace

BubbleRow MakeBubbleRow(demo::MessageRow const& row, bool group, bool showSenderHeader,
                        bool carriesDeliveryGlyph) {
  BubbleRow out;
  out.bubble.id = row.id;

  // ---- the bubble ---------------------------------------------------------
  Button bubble;
  if (auto style = StyleByKey(L"UrBubbleButtonStyle")) bubble.Style(style);

  // Design §6.2: incoming UrCardBrush #1C1C1C left, outgoing UrCardHoverBrush
  // #242424 with a 1px UrBorderBrush right. UrAccentBrush is NEVER a bubble
  // fill — it is the send button and the selection outline only.
  bubble.Background(row.outgoing ? BrushByKey(L"UrCardHoverBrush", urnw::colors::kCardHover)
                                 : BrushByKey(L"UrCardBrush", urnw::colors::kCard));
  bubble.BorderThickness(ThicknessHelper::FromUniformLength(1));
  bubble.BorderBrush(row.outgoing
                         ? BrushByKey(L"UrBorderBrush", urnw::colors::kBorder)
                         : Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
  bubble.HorizontalAlignment(row.outgoing ? HorizontalAlignment::Right
                                          : HorizontalAlignment::Left);
  // The cap now; the thread column narrows it on SizeChanged (the column task
  // owns that walk). Never 0 here — a bubble built before the column has been
  // measured must still be a bubble.
  bubble.MaxWidth(BubbleMaxWidthDip(0.0));

  StackPanel column;
  column.Spacing(2);

  if (showSenderHeader && !row.senderName.empty()) {
    TextBlock name;
    name.Text(winrt::hstring{row.senderName});
    if (auto s = StyleByKey(L"UrCaptionTextStyle")) name.Style(s);
    name.Foreground(urnw::colors::MutedBrush());
    name.TextTrimming(TextTrimming::CharacterEllipsis);
    MarkRaw(name);
    column.Children().Append(name);
  }

  // THE BODY FACE. UrBodyTextStyle is UrBodyFontFamily at 14/20 — never
  // UrHeadingFontFamily, which is the display face for titles and the hero.
  TextBlock body;
  body.Text(winrt::hstring{row.body});
  if (auto s = StyleByKey(L"UrBodyTextStyle")) body.Style(s);
  body.TextWrapping(TextWrapping::Wrap);
  MarkRaw(body);
  column.Children().Append(body);

  TextBlock time;
  time.Text(winrt::hstring{row.timeLabel});
  if (auto s = StyleByKey(L"UrCaptionTextStyle")) time.Style(s);
  time.FontSize(11);
  time.Foreground(urnw::colors::FaintBrush());
  time.HorizontalAlignment(HorizontalAlignment::Right);
  MarkRaw(time);
  column.Children().Append(time);

  bubble.Content(column);
  // A Button whose Content is a Panel gets NO automatic name.
  Automation::AutomationProperties::SetName(
      bubble, winrt::hstring{BubbleAutomationName(row, group)});
  out.bubble.root = bubble;

  // ---- the gutter + bubble row -------------------------------------------
  Grid gutterRow;
  ColumnDefinition gutter, content;
  const bool wantsGutter = group && !row.outgoing;
  gutter.Width(GridLengthHelper::FromPixels(wantsGutter ? kThreadGutterDip : 0.0));
  content.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  gutterRow.ColumnDefinitions().Append(gutter);
  gutterRow.ColumnDefinitions().Append(content);

  if (wantsGutter && showSenderHeader) {
    // MakeIdenticon applies its OWN CornerRadius(8) — do not set one here.
    auto ident = urmsg::MakeIdenticon(row.senderKey, kThreadIdenticonDip);
    ident.VerticalAlignment(VerticalAlignment::Top);
    ident.HorizontalAlignment(HorizontalAlignment::Left);
    MarkRaw(ident);
    Grid::SetColumn(ident, 0);
    gutterRow.Children().Append(ident);
  }
  Grid::SetColumn(bubble, 1);
  gutterRow.Children().Append(bubble);

  StackPanel rowRoot;
  rowRoot.Spacing(0);
  rowRoot.Children().Append(gutterRow);
  if (carriesDeliveryGlyph) rowRoot.Children().Append(MakeDeliveryLine(row));
  out.root = rowRoot;
  return out;
}


namespace {

// The thread pane's inset. The bubble cap is 68% of the CONTENT width, so the
// padding comes off before BubbleMaxWidthDip sees it.
constexpr double kThreadPadDip = 16.0;

// Live state for one built thread. MakeThread returns ThreadView BY VALUE, so
// a SizeChanged lambda capturing &v would dangle on the first copy; the parts
// live here in a shared_ptr instead, and every setter finds them from v.root.
struct ThreadParts {
  ScrollViewer scroller{nullptr};
  StackPanel stack{nullptr};
  std::function<void(std::wstring)> onSelect;
  std::function<void()> onDeselect;
  std::vector<Button> bubbles;   // for the width walk; ThreadView owns the public list
  double columnWidth = 0.0;
};

std::map<void const*, std::shared_ptr<ThreadParts>>& Registry() {
  static std::map<void const*, std::shared_ptr<ThreadParts>> map;
  return map;
}

std::shared_ptr<ThreadParts> Find(FrameworkElement const& root) {
  if (!root) return nullptr;
  auto it = Registry().find(winrt::get_abi(root));
  return it == Registry().end() ? nullptr : it->second;
}

void ApplyColumnWidth(std::shared_ptr<ThreadParts> const& parts) {
  if (!parts) return;
  const double cap = BubbleMaxWidthDip(parts->columnWidth - kThreadPadDip * 2.0);
  for (auto const& b : parts->bubbles)
    if (b) b.MaxWidth(cap);
}

// The day separator: a centred pill, not a rule with text on it. 11px
// letterspaced muted is UrGroupHeaderTextStyle - the same voice every group
// header in the app already speaks, so the thread does not grow a caption
// species of its own.
FrameworkElement MakeDaySeparator(std::wstring const& label) {
  Border pill;
  pill.Background(BrushByKey(L"UrCardBrush", urnw::colors::kCard));
  pill.BorderBrush(BrushByKey(L"UrBorderBrush", urnw::colors::kBorder));
  pill.BorderThickness(ThicknessHelper::FromUniformLength(1));
  pill.CornerRadius(CornerRadiusHelper::FromUniformRadius(10));
  pill.Padding(ThicknessHelper::FromLengths(10, 2, 10, 3));
  pill.HorizontalAlignment(HorizontalAlignment::Center);
  pill.Margin(ThicknessHelper::FromLengths(0, 14, 0, 6));

  TextBlock text;
  text.Text(winrt::hstring{label});
  if (auto s = StyleByKey(L"UrGroupHeaderTextStyle")) text.Style(s);
  text.Foreground(urnw::colors::MutedBrush());
  pill.Child(text);
  Automation::AutomationProperties::SetName(pill, winrt::hstring{label});
  return pill;
}

// A system row: centred, muted, and NOT a bubble - it did not come from a
// person. A permanent record (the key-change line, Spec C 7.4) additionally
// carries a lock glyph and an edge, so "this one cannot be dismissed" is a
// shape and not a shade.
FrameworkElement MakeSystemRow(demo::MessageRow const& row) {
  StackPanel line;
  line.Orientation(Orientation::Horizontal);
  line.Spacing(6);
  line.HorizontalAlignment(HorizontalAlignment::Center);

  if (row.permanentRecord) {
    FontIcon lock;
    lock.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
    lock.Glyph(L"\uE72E");  // Lock
    lock.FontSize(12);
    lock.Foreground(urnw::colors::MutedBrush());
    lock.VerticalAlignment(VerticalAlignment::Center);
    MarkRaw(lock);
    line.Children().Append(lock);
  }

  TextBlock text;
  text.Text(winrt::hstring{row.systemText});
  if (auto s = StyleByKey(L"UrCaptionTextStyle")) text.Style(s);
  text.Foreground(urnw::colors::MutedBrush());
  text.TextWrapping(TextWrapping::Wrap);
  text.TextAlignment(TextAlignment::Center);
  text.MaxWidth(420);
  MarkRaw(text);
  line.Children().Append(text);

  Border box;
  box.Child(line);
  box.HorizontalAlignment(HorizontalAlignment::Center);
  box.Margin(ThicknessHelper::FromLengths(0, 8, 0, 8));
  if (row.permanentRecord) {
    box.BorderBrush(BrushByKey(L"UrBorderBrush", urnw::colors::kBorder));
    box.BorderThickness(ThicknessHelper::FromUniformLength(1));
    box.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
    box.Padding(ThicknessHelper::FromLengths(10, 4, 10, 4));
  }
  Automation::AutomationProperties::SetName(box, winrt::hstring{row.systemText});
  return box;
}

}  // namespace

ThreadView MakeThread(std::function<void(std::wstring)> onSelectMessage,
                      std::function<void()> onDeselect) {
  ThreadView v;
  auto parts = std::make_shared<ThreadParts>();
  parts->onSelect = std::move(onSelectMessage);
  parts->onDeselect = std::move(onDeselect);

  Grid root;
  root.Background(BrushByKey(L"UrBackgroundBrush", urnw::colors::kBackground));

  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroller.Padding(ThicknessHelper::FromLengths(kThreadPadDip, 8, kThreadPadDip, 12));

  StackPanel stack;
  stack.Spacing(6);
  scroller.Content(stack);
  root.Children().Append(scroller);

  parts->scroller = scroller;
  parts->stack = stack;
  v.root = root;
  v.scroller = scroller;
  v.stack = stack;
  Registry()[winrt::get_abi(v.root)] = parts;

  // The bubble cap tracks the column. One walk over <= ~45 buttons on a
  // resize is cheaper than a per-bubble binding and is the only way the 68%
  // rule can be true at more than one window width.
  scroller.SizeChanged([parts](auto const&, SizeChangedEventArgs const& e) {
    parts->columnWidth = e.NewSize().Width;
    ApplyColumnWidth(parts);
  });

  // A thread opens at its NEWEST row, and SetThreadConversation alone cannot
  // put it there: BuildThread runs from the window constructor, before any
  // layout pass and while ThreadHost is still collapsed, so the ChangeView down
  // there sees ScrollableHeight 0 and lands on nothing. VERIFIED, not assumed -
  // the first capture of this surface opened on "Yesterday" at the top with the
  // failed row an entire viewport below the fold. The stack's own SizeChanged
  // is the first moment a real extent exists, and it fires again whenever the
  // column resizes, which is also when a thread should stay at its foot rather
  // than drift up the backlog. Nothing here loops: ChangeView moves the OFFSET,
  // which is not a size.
  //
  // The task that lets the reader scroll away (or appends a row) owns the guard
  // that stops this re-pinning a thread the reader has deliberately left.
  stack.SizeChanged([parts](auto const&, auto const&) {
    parts->scroller.ChangeView(nullptr, parts->scroller.ScrollableHeight(), nullptr, true);
  });

  // Design 9.1: a click in empty thread space deselects. A Button handles its
  // own pointer events, so a bubble click does not reach this.
  root.Tapped([parts](auto const&, auto const&) {
    if (parts->onDeselect) parts->onDeselect();
  });
  return v;
}

void SetThreadConversation(ThreadView& v, demo::Conversation const& c) {
  auto parts = Find(v.root);
  if (!parts) return;

  parts->stack.Children().Clear();
  parts->bubbles.clear();
  v.bubbles.clear();

  const bool group = (c.kind == demo::ConversationKind::Group);
  for (std::size_t i = 0; i < c.rows.size(); ++i) {
    auto const& row = c.rows[i];
    demo::MessageRow const* prev = (i > 0) ? &c.rows[i - 1] : nullptr;
    demo::MessageRow const* next = (i + 1 < c.rows.size()) ? &c.rows[i + 1] : nullptr;

    switch (row.kind) {
      case demo::RowKind::DaySeparator: {
        const std::wstring label = DaySeparatorLabel(row);
        // An unlabelled separator draws NOTHING rather than an empty pill.
        if (!label.empty()) parts->stack.Children().Append(MakeDaySeparator(label));
        break;
      }
      case demo::RowKind::System:
        parts->stack.Children().Append(MakeSystemRow(row));
        break;
      case demo::RowKind::Message: {
        auto built = MakeBubbleRow(row, group, ShowsSenderHeader(prev, row, group),
                                   CarriesDeliveryGlyph(row, next));
        built.bubble.root.Click([parts, id = row.id](auto const&, auto const&) {
          if (parts->onSelect) parts->onSelect(id);
        });
        parts->bubbles.push_back(built.bubble.root);
        v.bubbles.push_back(built.bubble);
        parts->stack.Children().Append(built.root);
        break;
      }
    }
  }

  ApplyColumnWidth(parts);
  // A thread opens at its newest row. ScrollableHeight is 0 until the stack
  // has been measured, so lay out first; disableAnimation is true because
  // this is a jump to a position, not a motion the user asked for.
  parts->scroller.UpdateLayout();
  parts->scroller.ChangeView(nullptr, parts->scroller.ScrollableHeight(), nullptr, true);
}

}  // namespace urmsg::views
