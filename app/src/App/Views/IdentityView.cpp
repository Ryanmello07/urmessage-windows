// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Views/IdentityView.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "Live/LiveIdentity.h"
#include "Log.h"
#include "Strings.h"
#include "UrColors.h"
#include "UrComponents.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;
namespace input = winrt::Microsoft::UI::Xaml::Input;
namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace id = urmsg::demo::identity;
using urnw::kit::StyleByKey;

namespace urmsg::views {
namespace {

// One column for every screen, a narrower one for a dialog. The words grid wants the wide one: five
// columns of a 12-letter word in the mono face at 16 (measured: at 780 a Star column cut
// "thunderstorm" to "thunderstor", ci/i1-shots run 38899303355).
constexpr double kCardWidth = 860;
constexpr double kDialogWidth = 660;

media::FontFamily MonoFont() {
  if (auto app = Application::Current()) {
    auto key = box_value(hstring{L"UrMonoFontFamily"});
    if (app.Resources().HasKey(key))
      if (auto found = app.Resources().Lookup(key).try_as<media::FontFamily>()) return found;
  }
  return media::FontFamily(L"Cascadia Mono, Consolas, Courier New");
}

media::SolidColorBrush AmberBrush() { return urnw::colors::MakeBrush(urnw::colors::kUrAmber); }

TextBlock MakeLine(std::wstring const& text, id::TextStyle style) {
  TextBlock t;
  if (auto st = StyleByKey(L"UrBodyTextStyle")) t.Style(st);
  t.Text(hstring{text});
  t.TextWrapping(TextWrapping::Wrap);
  switch (style) {
    case id::TextStyle::Strong:
      t.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
      break;
    case id::TextStyle::Muted:
      t.Foreground(urnw::colors::MutedBrush());
      break;
    case id::TextStyle::Error:
      t.Foreground(urnw::colors::DangerBrush());
      break;
    case id::TextStyle::Body:
      break;
  }
  return t;
}

// The line over a block. A Fact label FRAMES the block under it (the words, the emoji, the code),
// so it is drawn in the demo banner's colour: the framing is read before the first word is.
TextBlock MakeLabel(std::wstring const& text, id::TextKind kind) {
  TextBlock t = MakeLine(text, id::TextStyle::Strong);
  if (kind != id::TextKind::Chrome) t.Foreground(AmberBrush());
  return t;
}

Border MakeBanner(std::wstring const& text) {
  Border b;
  b.Background(urnw::colors::MakeBrush(urnw::colors::WithAlpha(urnw::colors::kUrAmber, 0x24)));
  b.BorderBrush(urnw::colors::MakeBrush(urnw::colors::WithAlpha(urnw::colors::kUrAmber, 0x70)));
  b.BorderThickness(ThicknessHelper::FromUniformLength(1));
  b.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
  b.Padding(ThicknessHelper::FromLengths(12, 8, 12, 8));
  TextBlock t = MakeLine(text, id::TextStyle::Body);
  t.Foreground(AmberBrush());
  automation::AutomationProperties::SetAutomationId(t, L"IdentityBanner");
  b.Child(t);
  return b;
}

// AUTO columns, each as wide as its widest word: equal Star columns cut the long ones short.
Grid MakeFiveByFive(double columnSpacing) {
  Grid g;
  g.ColumnSpacing(columnSpacing);
  g.RowSpacing(10);
  for (int c = 0; c < 5; ++c) {
    ColumnDefinition col;
    col.Width(GridLengthHelper::Auto());
    g.ColumnDefinitions().Append(col);
  }
  for (int r = 0; r < 5; ++r) {
    RowDefinition row;
    row.Height(GridLengthHelper::Auto());
    g.RowDefinitions().Append(row);
  }
  return g;
}

TextBlock MakeNumber(size_t i) {
  TextBlock n;
  n.Text(hstring{std::to_wstring(i + 1)});
  n.Width(22);
  n.TextAlignment(TextAlignment::Right);
  n.FontFamily(MonoFont());
  n.FontSize(13);
  n.Foreground(urnw::colors::MutedBrush());
  n.VerticalAlignment(VerticalAlignment::Center);
  automation::AutomationProperties::SetAccessibilityView(
      n, automation::Peers::AccessibilityView::Raw);
  return n;
}

// The 25 words: numbered, column-major (F3's grid), in the mono face.
FrameworkElement MakeWordsGrid(std::vector<std::wstring> const& words) {
  Grid g = MakeFiveByFive(26);
  // each cell's name is written into this one reserved buffer and overwritten once it is set, so
  // no buffer that held a word is let go unwiped (R89, X2-3)
  std::wstring name;
  name.reserve(64);
  for (size_t i = 0; i < words.size() && i < 25; ++i) {
    StackPanel cell;
    cell.Orientation(Orientation::Horizontal);
    cell.Spacing(8);
    TextBlock w;
    w.Text(hstring{words[i]});
    w.FontFamily(MonoFont());
    w.FontSize(16);
    w.Foreground(urnw::colors::TextBrush());
    w.VerticalAlignment(VerticalAlignment::Center);
    urmsg::live::WriteWordCellName(i + 1, words[i], name);
    automation::AutomationProperties::SetName(w, hstring{name});
    urmsg::live::Wipe(name);
    cell.Children().Append(MakeNumber(i));
    cell.Children().Append(w);
    Grid::SetRow(cell, static_cast<int32_t>(i % 5));
    Grid::SetColumn(cell, static_cast<int32_t>(i / 5));
    g.Children().Append(cell);
  }
  return g;
}

// THE LIVE WORD FIELDS (PLAN Task 16; F3 section 2.11.3). A PasswordBox per word: it has no
// spelling and no prediction, takes a password's input scope, and lets nothing be copied or cut, so
// no word reaches the clipboard or its history. Its word shows while it is typed in and is hidden
// when focus leaves (PasswordRevealMode), and the screen's one switch shows them all. A word that
// is not on the list is marked as it is left, by the list's own check; the library names the
// wrong ones when Check is pressed.
void AppendMaskedWordFields(StackPanel const& column, id::Block const& b, IdentityScreenView& view,
                            CheckWordFn const& checkWord) {
  column.Children().Append(MakeLabel(b.label, b.labelKind));
  Grid g = MakeFiveByFive(12);
  auto revealAll = std::make_shared<bool>(false);
  auto fields = std::make_shared<std::vector<PasswordBox>>();
  for (size_t i = 0; i < b.words.size() && i < 25; ++i) {
    StackPanel cell;
    cell.Orientation(Orientation::Horizontal);
    cell.Spacing(6);
    PasswordBox box;
    box.Width(118);
    box.FontFamily(MonoFont());
    box.FontSize(14);
    box.MaxLength(32);
    box.PasswordRevealMode(PasswordRevealMode::Hidden);
    if (b.fields.passwordScope) {
      try {
        input::InputScope scope;
        input::InputScopeName name(input::InputScopeNameValue::Password);
        scope.Names().Append(name);
        box.InputScope(scope);
      } catch (winrt::hresult_error const& e) {
        urnw::LogWarn("identity: a live word field refused a password input scope: {}",
                      urnw::Narrow(std::wstring{e.message()}));
      }
    }
    if (i < b.prefill.size()) box.Password(hstring{b.prefill[i]});
    TextBlock number = MakeNumber(i);
    const bool named = std::find(b.wrongPositions.begin(), b.wrongPositions.end(),
                                 static_cast<int>(i + 1)) != b.wrongPositions.end();
    if (named) {
      box.BorderBrush(urnw::colors::DangerBrush());
      number.Foreground(urnw::colors::DangerBrush());
      number.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
      automation::AutomationProperties::SetHelpText(
          box, hstring{b.markNote.empty() ? std::wstring(L"Not the word you were shown.") : b.markNote});
    }
    box.GotFocus([](winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
      sender.as<PasswordBox>().PasswordRevealMode(PasswordRevealMode::Visible);
    });
    box.LostFocus([revealAll, number, checkWord, named](
                      winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
      PasswordBox self = sender.as<PasswordBox>();
      if (!*revealAll) self.PasswordRevealMode(PasswordRevealMode::Hidden);
      if (!checkWord || named) return;
      std::wstring word{self.Password()};
      const bool onList = word.empty() || checkWord(word);
      urmsg::live::Wipe(word);
      number.Foreground(onList ? urnw::colors::MutedBrush() : urnw::colors::DangerBrush());
      automation::AutomationProperties::SetHelpText(
          self, onList ? hstring{} : hstring{L"This is not a word of the list."});
    });
    automation::AutomationProperties::SetName(box, hstring{L"Word " + std::to_wstring(i + 1)});
    cell.Children().Append(number);
    cell.Children().Append(box);
    view.wordNumbers.push_back(number);
    Grid::SetRow(cell, static_cast<int32_t>(i % 5));
    Grid::SetColumn(cell, static_cast<int32_t>(i / 5));
    g.Children().Append(cell);
    view.maskedFields.push_back(box);
    fields->push_back(box);
  }
  column.Children().Append(g);
  if (b.fields.revealToggle) {
    CheckBox reveal;
    reveal.Content(box_value(hstring{L"Show the words I typed"}));
    automation::AutomationProperties::SetName(reveal, hstring{L"Show the words I typed"});
    reveal.Checked([revealAll, fields](auto const&, auto const&) {
      *revealAll = true;
      for (auto& f : *fields) f.PasswordRevealMode(PasswordRevealMode::Visible);
    });
    reveal.Unchecked([revealAll, fields](auto const&, auto const&) {
      *revealAll = false;
      for (auto& f : *fields)
        if (f.FocusState() == FocusState::Unfocused) f.PasswordRevealMode(PasswordRevealMode::Hidden);
    });
    column.Children().Append(reveal);
    view.revealToggle = reveal;
  }
  TextBlock status = MakeLine(b.text, id::TextStyle::Error);
  automation::AutomationProperties::SetAutomationId(status, L"IdentityWordStatus");
  status.Visibility(b.text.empty() ? Visibility::Collapsed : Visibility::Visible);
  column.Children().Append(status);
  view.wordStatus = status;
}

void AppendWordFields(StackPanel const& column, id::Block const& b, IdentityScreenView& view) {
  column.Children().Append(MakeLabel(b.label, b.labelKind));
  Grid g = MakeFiveByFive(12);
  for (size_t i = 0; i < b.words.size() && i < 25; ++i) {
    StackPanel cell;
    cell.Orientation(Orientation::Horizontal);
    cell.Spacing(6);
    TextBox box;
    box.Width(118);
    box.FontFamily(MonoFont());
    box.FontSize(14);
    box.MaxLength(32);
    // FROM THE BLOCK'S FieldPolicy (X-APP-4), and from nothing else. SCOPE H1, F3:894-900: no spell
    // check and no prediction on a field that takes the words, so neither the checker nor the
    // prediction store ever holds one.
    box.IsSpellCheckEnabled(b.fields.spellCheck);
    box.IsTextPredictionEnabled(b.fields.prediction);
    if (b.fields.passwordScope) {
      try {
        input::InputScope scope;
        input::InputScopeName name(input::InputScopeNameValue::Password);
        scope.Names().Append(name);
        box.InputScope(scope);
      } catch (winrt::hresult_error const& e) {
        urnw::LogWarn("identity: the word field refused a password input scope: {}",
                      urnw::Narrow(std::wstring{e.message()}));
      }
    }
    if (i < b.prefill.size()) box.Text(hstring{b.prefill[i]});
    TextBlock number = MakeNumber(i);
    // THE WRONG WORD IS MARKED ON ITS NUMBER as well as on the field: a focused TextBox's own visual
    // states repaint its border, and the number keeps the mark whatever has focus
    if (static_cast<int>(i) == b.wrongIndex) {
      box.BorderBrush(urnw::colors::DangerBrush());
      number.Foreground(urnw::colors::DangerBrush());
      number.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    }
    automation::AutomationProperties::SetName(box, hstring{L"Word " + std::to_wstring(i + 1)});
    cell.Children().Append(number);
    cell.Children().Append(box);
    view.wordNumbers.push_back(number);
    Grid::SetRow(cell, static_cast<int32_t>(i % 5));
    Grid::SetColumn(cell, static_cast<int32_t>(i / 5));
    g.Children().Append(cell);
    view.wordFields.push_back(box);
  }
  column.Children().Append(g);
  view.expectedWords = b.words;
  TextBlock status = MakeLine(b.text, id::TextStyle::Error);
  automation::AutomationProperties::SetAutomationId(status, L"IdentityWordStatus");
  status.Visibility(b.text.empty() ? Visibility::Collapsed : Visibility::Visible);
  column.Children().Append(status);
  view.wordStatus = status;
}

FrameworkElement MakeEmojiRow(std::vector<id::EmojiItem> const& emoji) {
  StackPanel row;
  row.Orientation(Orientation::Horizontal);
  row.Spacing(12);
  for (size_t i = 0; i < emoji.size(); ++i) {
    StackPanel item;
    item.Orientation(Orientation::Vertical);
    item.Spacing(4);
    item.Width(78);
    TextBlock glyph;
    glyph.Text(hstring{emoji[i].glyph});
    glyph.FontFamily(media::FontFamily(L"Segoe UI Emoji"));
    glyph.FontSize(40);
    glyph.HorizontalAlignment(HorizontalAlignment::Center);
    glyph.TextAlignment(TextAlignment::Center);
    automation::AutomationProperties::SetName(
        glyph, hstring{L"Emoji " + std::to_wstring(i + 1) + L": " + emoji[i].name});
    TextBlock name;
    if (auto st = StyleByKey(L"UrCaptionTextStyle")) name.Style(st);
    name.Text(hstring{emoji[i].name});
    name.HorizontalAlignment(HorizontalAlignment::Center);
    name.TextAlignment(TextAlignment::Center);
    item.Children().Append(glyph);
    item.Children().Append(name);
    row.Children().Append(item);
  }
  return row;
}

FrameworkElement MakeButtons(std::vector<id::Button> const& buttons,
                             IdentityActionHandler const& onAction) {
  StackPanel row;
  // more than three read as a list (the endings, the account's demo screens)
  row.Orientation(buttons.size() > 3 ? Orientation::Vertical : Orientation::Horizontal);
  row.Spacing(10);
  for (id::Button const& x : buttons) {
    Button btn;
    btn.Content(box_value(hstring{x.label}));
    if (x.primary) {
      if (auto st = StyleByKey(L"UrPrimaryButtonStyle")) btn.Style(st);
      btn.MinWidth(220);
    } else {
      btn.Padding(ThicknessHelper::FromLengths(16, 8, 16, 8));
      btn.MinHeight(40);
    }
    btn.HorizontalAlignment(HorizontalAlignment::Left);
    // a demo-only control wears the banner's colour as well as its words
    if (x.kind == id::TextKind::DemoOnly) btn.Foreground(AmberBrush());
    automation::AutomationProperties::SetName(btn, hstring{x.label});
    btn.Click([onAction, x](auto const&, auto const&) {
      if (onAction) onAction(x);
    });
    row.Children().Append(btn);
  }
  return row;
}

void AppendDevices(StackPanel const& column, id::Block const& b,
                   IdentityActionHandler const& onAction) {
  column.Children().Append(MakeLabel(b.label, b.labelKind));
  StackPanel list;
  list.Spacing(6);
  for (id::DeviceLine const& d : b.devices) {
    CheckBox box;
    StackPanel content;
    content.Children().Append(MakeLine(d.name, id::TextStyle::Strong));
    content.Children().Append(MakeLine(d.detail, id::TextStyle::Muted));
    box.Content(content);
    // seeded BEFORE the handlers are attached: IsChecked raises Checked, and a seed is not a tick
    box.IsChecked(d.selected);
    automation::AutomationProperties::SetName(box, hstring{d.name + L", " + d.detail});
    id::Button toggle;
    toggle.action = id::Action::ToggleDevice;
    toggle.arg = d.index;
    box.Checked([onAction, toggle](auto const&, auto const&) {
      if (onAction) onAction(toggle);
    });
    box.Unchecked([onAction, toggle](auto const&, auto const&) {
      if (onAction) onAction(toggle);
    });
    list.Children().Append(box);
  }
  column.Children().Append(list);
}

void AppendKeepOrRemove(StackPanel const& column, id::Block const& b) {
  column.Children().Append(MakeLabel(b.label, b.labelKind));
  for (id::DeviceLine const& d : b.devices) {
    Grid row;
    ColumnDefinition nameColumn, choiceColumn;
    nameColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    choiceColumn.Width(GridLengthHelper::Auto());
    row.ColumnDefinitions().Append(nameColumn);
    row.ColumnDefinitions().Append(choiceColumn);
    TextBlock name = MakeLine(d.name, id::TextStyle::Strong);
    name.VerticalAlignment(VerticalAlignment::Center);
    row.Children().Append(name);
    StackPanel choice;
    choice.Orientation(Orientation::Horizontal);
    choice.Spacing(12);
    const hstring group{L"identity-keep-" + std::to_wstring(d.index)};
    RadioButton keep;
    keep.Content(box_value(hstring{L"Keep"}));
    keep.GroupName(group);
    keep.IsChecked(true);
    automation::AutomationProperties::SetName(keep, hstring{L"Keep " + d.name});
    RadioButton remove;
    remove.Content(box_value(hstring{L"Remove"}));
    remove.GroupName(group);
    automation::AutomationProperties::SetName(remove, hstring{L"Remove " + d.name});
    choice.Children().Append(keep);
    choice.Children().Append(remove);
    Grid::SetColumn(choice, 1);
    row.Children().Append(choice);
    column.Children().Append(row);
  }
}

}  // namespace

IdentityScreenView MakeIdentityScreen(id::ScreenModel const& model,
                                      IdentityActionHandler const& onAction,
                                      CheckWordFn const& checkWord) {
  IdentityScreenView view;
  view.screen = model.screen;

  Grid root;
  // OPAQUE: the app under an identity screen is not part of it, and a capture of a screen that is
  // not excluded must show this screen and nothing behind it
  root.Background(urnw::colors::BackgroundBrush());

  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.HorizontalScrollMode(ScrollMode::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);

  Border card;
  card.Background(urnw::colors::CardBrush());
  card.BorderBrush(urnw::colors::BorderBrush());
  card.BorderThickness(ThicknessHelper::FromUniformLength(1));
  card.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
  card.Padding(ThicknessHelper::FromLengths(32, 28, 32, 28));
  // TOP, so the card is as tall as what it holds and no taller.
  card.VerticalAlignment(VerticalAlignment::Top);
  card.Margin(ThicknessHelper::FromLengths(16, 36, 16, 36));
  // the alarm wears the danger colour on its edge as well as in its words (SCOPE G6)
  if (model.alarm) {
    card.BorderBrush(urnw::colors::DangerBrush());
    card.BorderThickness(ThicknessHelper::FromUniformLength(2));
  }

  // ONE WIDTH FOR EVERY SCREEN, CENTRED, and narrower in a narrow window: the card stands in the
  // middle column of three, a star column capped at the card's width with a weight that takes all
  // the room below the cap, and the two outer columns share what is left. Measured twice: a
  // centred card sized to its content was a different width on every screen and cut the longest
  // words (ci/i1-shots, run 38899303355); a stretched card under a MaxWidth was not centred, sitting
  // 10 to 130 px right of the middle by screen (ci/i1-shots-3, run 38901507747).
  Grid frame;
  ColumnDefinition leftColumn, cardColumn, rightColumn;
  leftColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  cardColumn.Width(GridLengthHelper::FromValueAndType(1000, GridUnitType::Star));
  cardColumn.MaxWidth((model.dialog ? kDialogWidth : kCardWidth) + 32);
  rightColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  frame.ColumnDefinitions().Append(leftColumn);
  frame.ColumnDefinitions().Append(cardColumn);
  frame.ColumnDefinitions().Append(rightColumn);
  Grid::SetColumn(card, 1);
  frame.Children().Append(card);

  StackPanel column;
  column.Spacing(14);
  card.Child(column);
  scroller.Content(frame);
  root.Children().Append(scroller);

  for (id::Block const& b : model.blocks) {
    switch (b.type) {
      case id::BlockType::Banner:
        column.Children().Append(MakeBanner(b.text));
        break;
      case id::BlockType::Title: {
        TextBlock t;
        if (auto st = StyleByKey(L"UrSubtitleTextStyle")) t.Style(st);
        t.Text(hstring{b.text});
        t.TextWrapping(TextWrapping::Wrap);
        if (model.alarm) t.Foreground(urnw::colors::DangerBrush());
        automation::AutomationProperties::SetAutomationId(t, L"IdentityTitle");
        automation::AutomationProperties::SetHeadingLevel(
            t, automation::Peers::AutomationHeadingLevel::Level1);
        column.Children().Append(t);
        break;
      }
      case id::BlockType::Text:
        column.Children().Append(MakeLine(b.text, b.style));
        break;
      case id::BlockType::Code: {
        column.Children().Append(MakeLabel(b.label, b.labelKind));
        TextBlock code;
        code.Text(hstring{b.text});
        code.FontFamily(MonoFont());
        code.FontSize(34);
        code.CharacterSpacing(80);
        code.Foreground(urnw::colors::TextBrush());
        code.IsTextSelectionEnabled(true);
        automation::AutomationProperties::SetName(code, hstring{L"Code: " + b.text});
        automation::AutomationProperties::SetAutomationId(code, L"IdentityCode");
        column.Children().Append(code);
        break;
      }
      case id::BlockType::Words: {
        TextBlock label = MakeLabel(b.label, b.labelKind);
        automation::AutomationProperties::SetAutomationId(label, L"IdentityWordsLabel");
        column.Children().Append(label);
        column.Children().Append(MakeWordsGrid(b.words));
        break;
      }
      case id::BlockType::WordFields:
        // THE CONTROL IS THE POLICY (X-APP-4). A PasswordBox both masks a word when focus leaves and
        // lets nothing be copied or cut; it is drawn exactly when the policy asks for both. A policy
        // that allows copying is drawn as a TextBox, which can be copied from and masks nothing.
        if (b.fields.maskWhenFocusLeaves && !b.fields.copyAllowed)
          AppendMaskedWordFields(column, b, view, checkWord);
        else
          AppendWordFields(column, b, view);
        break;
      case id::BlockType::Emoji:
        column.Children().Append(MakeLabel(b.label, b.labelKind));
        column.Children().Append(MakeEmojiRow(b.emoji));
        break;
      case id::BlockType::Devices:
        AppendDevices(column, b, onAction);
        break;
      case id::BlockType::CodeField: {
        column.Children().Append(MakeLabel(b.label, b.labelKind));
        TextBox box;
        box.Width(320);
        box.HorizontalAlignment(HorizontalAlignment::Left);
        box.FontFamily(MonoFont());
        box.FontSize(20);
        box.MaxLength(14);
        box.CharacterCasing(CharacterCasing::Upper);
        box.IsSpellCheckEnabled(false);
        box.IsTextPredictionEnabled(false);
        box.PlaceholderText(L"XXXX XXXX XXXX");
        automation::AutomationProperties::SetName(box, hstring{L"Code"});
        column.Children().Append(box);
        view.codeField = box;
        break;
      }
      case id::BlockType::NameField: {
        column.Children().Append(MakeLabel(b.label, b.labelKind));
        TextBox box;
        box.Width(360);
        box.HorizontalAlignment(HorizontalAlignment::Left);
        box.MaxLength(64);
        box.Text(hstring{b.text});
        automation::AutomationProperties::SetName(box, hstring{L"Device name"});
        column.Children().Append(box);
        view.nameField = box;
        break;
      }
      case id::BlockType::KeepOrRemove:
        AppendKeepOrRemove(column, b);
        break;
      case id::BlockType::Buttons:
        column.Children().Append(MakeButtons(b.buttons, onAction));
        break;
      case id::BlockType::Divider: {
        Border rule = urnw::kit::MakeDivider();
        rule.Margin(ThicknessHelper::FromLengths(0, 10, 0, 6));
        column.Children().Append(rule);
        break;
      }
    }
  }

  view.root = root;
  return view;
}

std::vector<std::wstring> ReadIdentityFields(IdentityScreenView const& view) {
  std::vector<std::wstring> out;
  out.reserve(25);  // so no grown buffer that held a word is let go unwiped (X-APP-2)
  if (!view.maskedFields.empty()) {
    for (auto const& f : view.maskedFields) out.emplace_back(f.Password());
  } else {
    for (auto const& f : view.wordFields) out.emplace_back(f.Text());
  }
  return out;
}

void ClearIdentityFields(IdentityScreenView const& view) {
  for (auto const& f : view.maskedFields) f.Password(L"");
  for (auto const& f : view.wordFields) f.Text(L"");
}

bool CheckIdentityWords(IdentityScreenView const& view, urmsg::RunMode mode) {
  // NOTHING TO CHECK IS NOT A MATCH (id::WordsAccepted): a Check a click queued can land after a
  // Cancel or a Close has emptied this view, and FirstWrongWord over no fields answers -1.
  if (view.wordFields.empty()) {
    urnw::LogInfo("identity: the typed-back check found no word fields on screen; nothing opens");
    return id::WordsAccepted(0, -1);
  }
  std::vector<std::wstring> typed;
  for (auto const& field : view.wordFields) typed.emplace_back(field.Text());
  const int wrong = id::FirstWrongWord(typed, view.expectedWords);
  for (auto const& field : view.wordFields) field.ClearValue(Control::BorderBrushProperty());
  for (auto const& number : view.wordNumbers) {
    number.Foreground(urnw::colors::MutedBrush());
    number.FontWeight(winrt::Windows::UI::Text::FontWeights::Normal());
  }
  bool empty = wrong >= 0;
  if (wrong >= 0 && static_cast<size_t>(wrong) < typed.size()) {
    for (wchar_t c : typed[static_cast<size_t>(wrong)])
      if (c != L' ' && c != L'\t') empty = false;
  }
  if (wrong >= 0 && static_cast<size_t>(wrong) < view.wordFields.size())
    view.wordFields[static_cast<size_t>(wrong)].BorderBrush(urnw::colors::DangerBrush());
  if (wrong >= 0 && static_cast<size_t>(wrong) < view.wordNumbers.size()) {
    view.wordNumbers[static_cast<size_t>(wrong)].Foreground(urnw::colors::DangerBrush());
    view.wordNumbers[static_cast<size_t>(wrong)].FontWeight(
        winrt::Windows::UI::Text::FontWeights::SemiBold());
  }
  if (view.wordStatus) {
    view.wordStatus.Text(hstring{id::WordCheckLine(mode, wrong, empty)});
    view.wordStatus.Foreground(wrong >= 0 ? urnw::colors::DangerBrush()
                                          : urnw::colors::MutedBrush());
    view.wordStatus.Visibility(Visibility::Visible);
  }
  // The place of the first wrong word is logged and never a word: a log line outlives the moment
  // and has a different audience from a screen.
  urnw::LogInfo("identity: the typed-back check answered {}",
                wrong < 0 ? std::string("all match") : "word " + std::to_string(wrong + 1));
  return id::WordsAccepted(view.wordFields.size(), wrong);
}

void FillIdentityWords(IdentityScreenView const& view) {
  for (size_t i = 0; i < view.wordFields.size() && i < view.expectedWords.size(); ++i)
    view.wordFields[i].Text(hstring{view.expectedWords[i]});
  urnw::LogInfo("identity: the demo words were filled in ({} fields)", view.wordFields.size());
}

}  // namespace urmsg::views
