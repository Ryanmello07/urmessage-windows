// SPDX-License-Identifier: MPL-2.0
#include "pch.h"  // /Yu"pch.h": every normal TU includes it FIRST

#include "Views/ThreadView.h"

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

}  // namespace urmsg::views
