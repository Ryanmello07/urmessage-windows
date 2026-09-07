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
#include "Views/ThreadLayout.h"

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

// The icon face, named EVERY time. FontIcon defaults to the older
// "Segoe MDL2 Assets", whose metrics differ and whose coverage is not the same
// set of codepoints, so a glyph picked from the Fluent set can land on a
// different drawing - or on nothing - if the family is left to the default.
Media::FontFamily IconFont() { return Media::FontFamily(L"Segoe Fluent Icons"); }

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
  icon.FontFamily(IconFont());
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

// design §6.2: centred, muted, NON-bubble. No fill, no border, no 68% cap.
// Nobody SAID a system line, so giving it a bubble would be the mock claiming a
// group changed its own timer. It is 12px muted on the page fill and nothing
// else - the whole point is that it does not look like anyone spoke.
FrameworkElement MakeSystemLine(winrt::hstring const& text) {
  TextBlock line;
  line.Text(text);
  // UrCaptionTextStyle, not a bare TextBlock: it is body-face/12/16 muted, and
  // it is what pins the BODY face to this line from the app's own resources.
  // Without it the face is only whatever ContentControlThemeFontFamily happens
  // to be - correct today, but a framework default is not a guarantee. FontSize
  // and Foreground are still set explicitly after it, the same belt-and-braces
  // MakeDeliveryLine uses, so a missing resource key cannot silently resize or
  // recolour the line.
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) line.Style(st);
  line.FontSize(12);
  line.Foreground(urnw::colors::MutedBrush());
  line.TextWrapping(TextWrapping::Wrap);
  line.TextAlignment(TextAlignment::Center);
  line.HorizontalAlignment(HorizontalAlignment::Center);
  line.MaxWidth(420);
  line.Margin(ThicknessHelper::FromLengths(0, 10, 0, 10));
  return line;
}

// Spec C §7.4: the permanent key-change record. A 2px UrDangerBrush rule on
// the LEADING edge, a key glyph, the record's own copy, and [ Review ].
//
// Non-dismissible is rendered by CONSTRUCTION: there is no close, no X and no
// collapse on this element, and none may be added. A demo that offered one
// would be advertising a control §7.5 forbids the product to ship.
//
// The rule is a SHAPE, so the record still reads as "this one is different"
// with colour taken away.
//
// The copy is the WORLD's - it says what this client OBSERVED (a key changed,
// verify before sending). Nothing here claims a message was encrypted or that
// anything was cryptographically checked: there is no crypto in this demo.
FrameworkElement MakeKeyChangeRecord(winrt::hstring const& text) {
  Grid root;
  root.HorizontalAlignment(HorizontalAlignment::Center);
  root.MaxWidth(520);
  root.Margin(ThicknessHelper::FromLengths(0, 12, 0, 12));
  {
    ColumnDefinition ruleCol;
    ruleCol.Width(GridLengthHelper::FromPixels(2));
    ColumnDefinition bodyCol;
    bodyCol.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    root.ColumnDefinitions().Append(ruleCol);
    root.ColumnDefinitions().Append(bodyCol);
  }

  Border rule;
  rule.Width(2);
  rule.Background(urnw::colors::DangerBrush());
  rule.VerticalAlignment(VerticalAlignment::Stretch);
  root.Children().Append(rule);

  StackPanel column;
  column.Spacing(6);
  column.Margin(ThicknessHelper::FromLengths(12, 2, 0, 2));
  Grid::SetColumn(column, 1);

  StackPanel head;
  head.Orientation(Orientation::Horizontal);
  head.Spacing(8);

  FontIcon key;
  key.FontFamily(IconFont());
  key.Glyph(L"\uE192");  // Segoe Fluent "Permissions" - the key glyph
  key.FontSize(14);
  key.Foreground(urnw::colors::DangerBrush());
  key.VerticalAlignment(VerticalAlignment::Top);
  // decoration beside a line that already carries the words
  MarkRaw(key);
  head.Children().Append(key);

  TextBlock body;
  body.Text(text);
  // Same reason as MakeSystemLine: the style is what names the BODY face. The
  // caption style's muted foreground is then overridden - this copy is the
  // record itself, not a caption about it, so it reads at full contrast.
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) body.Style(st);
  body.FontSize(12);
  body.TextWrapping(TextWrapping::Wrap);
  body.Foreground(urnw::colors::TextBrush());
  // The ROOT below carries this exact text in its automation name. Leaving the
  // TextBlock in the tree as well would announce the record twice - once as the
  // Grid's name and again as its child. Same treatment as the bubble's own body
  // and time lines above.
  MarkRaw(body);
  head.Children().Append(body);
  column.Children().Append(head);

  // design §9.1: the modal behind [ Review ] is not built (design §2), so the
  // button is DISABLED rather than live-but-dead.
  Button review;
  review.Content(winrt::box_value(winrt::hstring{L"Review"}));
  review.HorizontalAlignment(HorizontalAlignment::Left);
  review.FontSize(12);
  review.Padding(ThicknessHelper::FromLengths(10, 3, 10, 3));
  review.MinHeight(26);
  review.IsEnabled(false);
  Automation::AutomationProperties::SetName(
      review,
      // "identity key changed" is what the record actually says on screen; a
      // name that said "safety number" would announce different words from the
      // ones beside it.
      winrt::hstring{L"Review the identity-key change (not available in the demo)"});
  column.Children().Append(review);

  root.Children().Append(column);
  Automation::AutomationProperties::SetName(
      root, winrt::hstring{L"Permanent record, cannot be dismissed. "} + text);
  return root;
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
  // WHAT IS MISSING, stated accurately. An earlier version of this comment
  // implied nothing scrolls yet. That was false: the scroller is created with
  // VerticalScrollBarVisibility::Auto just above, and a measured run of this
  // thread reports 892.8 dip of scrollable extent, so the reader CAN wheel away
  // from the foot today. What they cannot do is STAY away across a window
  // resize - this handler fires on width-only changes too and yanks them back.
  //
  // The obvious two-line guard ("return if we are far from the bottom") is
  // WRONG here and was deliberately not added: on the construction path the
  // first size change that carries a real extent has offset 0 and a large
  // scrollable height, which is indistinguishable from "the reader scrolled to
  // the top" - so the guard would skip the very first pin and put the thread
  // straight back into the bug this handler exists to fix. A correct guard
  // needs an ARMED flag (pin unconditionally until the first pin lands, guard
  // after that), and proving it needs a scrolled-away state, which needs input
  // this task may not synthesize. The task that owns scroll behaviour owns it.
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

  // The builder renders the PLAN and chooses no SHAPE of its own. PlanThreadRows
  // lives in Views/ThreadLayout.h, which is pure C++, so every branch of this
  // switch is reachable from --diagnose - a builder that classified rows inline
  // could only ever be checked by looking at a screenshot.
  //
  // NOT literally one element per plan entry, and the exception is deliberate:
  // a DaySeparator whose label is empty draws NOTHING rather than an empty pill
  // (see the case below). That is the only place the count of children can be
  // less than the count of plan entries; no branch here ever draws MORE than
  // one, and no branch reorders or invents a row.
  //
  // The plan carries the SHAPE ONLY. The bubble's own trim still comes from the
  // per-row rules in Demo/ThreadLayout.h. The two now AGREE about what a run
  // is, and that agreement is enforced rather than assumed:
  //
  //   ShowsSenderHeader   - a run is the same SENDER (it compares senderKey),
  //                         so Mira then Tobias is two runs and Tobias gets
  //                         his name. Spec C §5.2: in a group the reader has
  //                         to know WHO is speaking.
  //   showSenderHeader    - delegates to exactly that function (fix round 1).
  //                         It used to be computed by DIRECTION, which named
  //                         only the first speaker of an incoming stretch and
  //                         disagreed on 5 of the shipped world's 60 message
  //                         rows.
  //
  // So p.showSenderHeader is now SAFE for T5 to use, and `T4 sender headers`
  // in --diagnose is the gate that keeps it that way: it compares the plan
  // against the rule over every message row and FAILS at "rule 20 vs plan 15,
  // 5 disagree" the moment anyone re-inlines a direction rule.
  //
  // endsOutgoingRun is the field that is still direction-only, and it is NOT a
  // substitute for CarriesDeliveryGlyph - see Views/ThreadLayout.h.
  for (auto const& p : PlanThreadRows(c)) {
    demo::MessageRow const& row = c.rows[p.rowIndex];
    demo::MessageRow const* prev = (p.rowIndex > 0) ? &c.rows[p.rowIndex - 1] : nullptr;
    demo::MessageRow const* next =
        (p.rowIndex + 1 < c.rows.size()) ? &c.rows[p.rowIndex + 1] : nullptr;

    switch (p.shape) {
      case ThreadRowShape::DaySeparator: {
        const std::wstring label = DaySeparatorLabel(row);
        // An unlabelled separator draws NOTHING rather than an empty pill.
        if (!label.empty()) parts->stack.Children().Append(MakeDaySeparator(label));
        break;
      }
      case ThreadRowShape::SystemLine:
        parts->stack.Children().Append(MakeSystemLine(winrt::hstring{row.systemText}));
        break;
      case ThreadRowShape::SystemPermanentRecord:
        parts->stack.Children().Append(MakeKeyChangeRecord(winrt::hstring{row.systemText}));
        break;
      case ThreadRowShape::IncomingBubble:
      case ThreadRowShape::OutgoingBubble: {
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
  // THIS PAIR IS INERT ON THE ONLY PATH THAT RUNS TODAY, and it is NOT the
  // thing that opens a thread at its newest row. BuildThread calls us from the
  // window constructor, before ApplyBreakpoint has made ThreadHost visible and
  // before any layout pass; UpdateLayout() on a collapsed host measures
  // nothing, ScrollableHeight() is 0, and this ChangeView lands on offset 0.
  // The bottom pin in MakeThread (stack.SizeChanged) is what actually puts the
  // thread at its foot - deleting that pin because "SetThreadConversation
  // already does this" regresses the surface straight back to opening on
  // "Yesterday" with the newest rows a viewport below the fold, which is a bug
  // that was already shipped once and caught in a capture.
  //
  // Kept because it IS the right path for a later caller that re-points an
  // already-measured column at a different conversation: there ScrollableHeight
  // is real and this is the jump. disableAnimation is true because it is a jump
  // to a position, not a motion the user asked for.
  parts->scroller.UpdateLayout();
  parts->scroller.ChangeView(nullptr, parts->scroller.ScrollableHeight(), nullptr, true);
}

}  // namespace urmsg::views
