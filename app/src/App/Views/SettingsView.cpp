// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Views/SettingsView.h"

#include <string>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

#include "Demo/DemoWorld.h"
#include "UrColors.h"
#include "UrComponents.h"
#include "Views/NetworkPageView.h"  // FormatKeyState — the ONE owner of the key-state string

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace kit = urnw::kit;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;

// The promoted one lookup of an App.xaml style (UrComponents.h:106-122): a
// fourth file-local copy is explicitly forbidden by that block, and a
// using-declaration keeps the call sites reading as they do in ThreadView.cpp.
using urnw::kit::StyleByKey;

// There is deliberately no #include "Localization.h" and no Loc() adapter in
// this file: Resources.resw is generated (Localization.h:3-4) and this work
// adds no keys, so every string here is either an English literal or a field
// of World. (The pane title's upper case lives in the literal for the same
// reason the Network page's NETWORK does — XAML has no text-transform, and
// nav_settings is title case because it is a NavigationViewItem label.)

namespace urmsg::views {
namespace {

// ONE row height for the whole list — UrPaneRowTallHeight. A settings list is
// a list of EXPLAINED rows, so the rows with no note simply centre their title
// in the same 44.
constexpr double kRowHeight = 44;

// The rail's floating caption (design d4 §4): the letterspaced chrome voice
// stays, but the sheet background, the hairline and the 28px strip go, so a
// section name floats above its card instead of ruling the column. The first
// caption in the body sits 4 from the header, later ones 12. Same three
// overrides InspectRailView.cpp's AppendCaption applies to the same builder.
kit::PaneGroupHeader AppendCaption(StackPanel const& column, winrt::hstring const& title,
                                   winrt::hstring const& meta, bool first) {
  auto header = kit::MakePaneGroupHeader(title, meta);
  header.root.Background(urnw::colors::MakeBrush({0, 0, 0, 0}));
  header.root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
  header.root.Height(32);
  header.root.Margin(ThicknessHelper::FromLengths(0, first ? 4 : 12, 0, 0));
  column.Children().Append(header.root);
  return header;
}

// A populated row: title, one-line note, value hard right. A Border, not a
// Button: there is nothing to open, and a row that lights up under the pointer
// and then does nothing is exactly the affordance §9.1 forbids.
kit::PaneTwoLineRow MakeValueRow(winrt::hstring const& title, winrt::hstring const& note,
                                 winrt::hstring const& value) {
  auto row = kit::MakePaneTwoLineRow(title, note, kRowHeight);
  TextBlock text;
  if (auto style = StyleByKey(L"UrValueTextStyle")) text.Style(style);
  text.Text(value);
  // MakePaneKeyValueRow names its value "key, value" so a screen reader hears
  // one fact rather than two fragments (UrComponents.cpp:190-193). Same shape.
  automation::AutomationProperties::SetName(
      text, winrt::hstring{std::wstring{title} + L", " + std::wstring{value}});
  row.trailing.Children().Append(text);
  return row;
}

// A group that is present but has nothing in it: ONE disclosure, the faint
// in-card row — the caption carries no "not in this demo" meta (polish B1:
// the same disclosure twice, once per caption and once per card, read as an
// apology; the row alone says it). The row is a Border, per §9.1.
void AppendInertGroup(StackPanel const& column, winrt::hstring const& title) {
  AppendCaption(column, title, {}, /*first=*/false);
  auto card = kit::MakePaneCard();
  auto row = kit::MakePaneTwoLineRow(L"Not part of this demo.", {}, kRowHeight);
  row.title.Foreground(urnw::colors::FaintBrush());
  card.body.Children().Append(row.root);
  kit::FinalizePaneCard(card);
  column.Children().Append(card.root);
}

}  // namespace

SettingsView MakeSettings(std::function<void(bool)> onAdvancedChanged, bool advanced) {
  SettingsView view;

  // SettingsHost (MainWindow.xaml:284) is a bare UrPaneStyle Grid — no pane
  // header strip and no scroller, the same shape NetworkHost is — so the page
  // builds both: row 0 the 40 DIP header every pane opens with, row 1 the
  // scroller whose horizontal Disable constrains the content to the viewport
  // width.
  Grid pane;
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  pane.RowDefinitions().Append(headerRow);
  pane.RowDefinitions().Append(bodyRow);

  Border header;
  if (auto style = StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  Grid headerGrid;
  TextBlock paneTitle;
  if (auto style = StyleByKey(L"UrPaneTitleStyle")) paneTitle.Style(style);
  paneTitle.Text(L"SETTINGS");
  headerGrid.Children().Append(paneTitle);

  TextBlock modeMeta;
  if (auto style = StyleByKey(L"UrPaneMetaStyle")) modeMeta.Style(style);
  modeMeta.HorizontalAlignment(HorizontalAlignment::Right);
  modeMeta.Text(advanced ? L"Advanced" : L"Normal");
  headerGrid.Children().Append(modeMeta);
  header.Child(headerGrid);
  Grid::SetRow(header, 0);
  pane.Children().Append(header);

  StackPanel column;
  column.Orientation(Orientation::Vertical);
  // The card groups read as one 840 DIP column, left-anchored (polish B1):
  // full-bleed at 1560 spread a one-word value across half a metre of glass.
  column.MaxWidth(840);
  column.HorizontalAlignment(HorizontalAlignment::Left);
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.HorizontalScrollMode(ScrollMode::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  // Bottom clearance (polish B1): without it the last row can scroll only
  // until its own edge meets the viewport's, which guillotines it mid-row
  // against the status strip; 40 DIP lets it scroll fully clear.
  scroller.Padding(ThicknessHelper::FromLengths(0, 0, 0, 40));
  scroller.Content(column);
  Grid::SetRow(scroller, 1);
  pane.Children().Append(scroller);

  auto const& world = demo::GetWorld();

  // ---- Appearance -----------------------------------------------------------
  AppendCaption(column, L"APPEARANCE", {}, /*first=*/true);
  {
    auto card = kit::MakePaneCard();
    card.body.Children().Append(
        MakeValueRow(L"Theme", L"URmessage is dark only.", L"Dark").root);
    {
      // The accent as the colour ITSELF beside its hex: "pale yellow" is not
      // a value a settings list can honestly render as words. The hex is the
      // text half, so the swatch is never the only carrier (contract rule 6).
      // Point-sized: the accent is never a fill and never a large area (G3).
      auto row = kit::MakePaneTwoLineRow(L"Accent", L"The URnetwork pale yellow.",
                                         kRowHeight);
      StackPanel trailing;
      trailing.Orientation(Orientation::Horizontal);
      trailing.Spacing(8);
      trailing.VerticalAlignment(VerticalAlignment::Center);
      Border swatch;
      swatch.Width(16);
      swatch.Height(16);
      swatch.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
      swatch.Background(urnw::colors::AccentBrush());
      automation::AutomationProperties::SetAccessibilityView(
          swatch, automation::Peers::AccessibilityView::Raw);
      trailing.Children().Append(swatch);
      TextBlock value;
      if (auto style = StyleByKey(L"UrValueTextStyle")) value.Style(style);
      value.Text(L"#EFF7BB");
      automation::AutomationProperties::SetName(value, L"Accent, #EFF7BB");
      trailing.Children().Append(value);
      row.trailing.Children().Append(trailing);
      card.body.Children().Append(row.root);
    }
    // Measured against App.xaml rather than remembered: UrHeadingFontFamily is
    // "ABC Gravity Extended", UrBodyFontFamily is "PP Neue Montreal",
    // UrWordmarkFontFamily is "PP NeueBit". There is deliberately NO "message
    // text size" row: UrBodyTextStyle is 14 and a size row here would be the
    // one screen in the demo that says something false about the app.
    card.body.Children().Append(
        MakeValueRow(L"Typefaces", L"Display, body and wordmark.",
                     L"ABC Gravity Extended · PP Neue Montreal · PP NeueBit")
            .root);
    kit::FinalizePaneCard(card);
    column.Children().Append(card.root);
  }

  // ---- Privacy and security -------------------------------------------------
  AppendCaption(column, L"PRIVACY AND SECURITY", {}, /*first=*/false);
  {
    auto card = kit::MakePaneCard();
    // The contract's invariant, asserted in --diagnose: myDevices[0]
    // .isThisComputer is true. front() IS this computer; no ordering guess.
    card.body.Children().Append(
        MakeValueRow(L"This device", L"The device you are reading this on.",
                     world.myDevices.empty()
                         ? winrt::hstring{L"unknown"}
                         : winrt::hstring{world.myDevices.front().name})
            .root);
    card.body.Children().Append(
        MakeValueRow(L"Linked devices",
                     L"Devices that would decrypt a message addressed to you.",
                     winrt::to_hstring(static_cast<int>(world.myDevices.size())))
            .root);
    card.body.Children().Append(
        MakeValueRow(L"Message server", L"Host and jurisdiction.",
                     winrt::hstring{world.server.host + L"  ·  " +
                                    world.server.jurisdiction})
            .root);
    {
      // FormatKeyState is the ONE owner of this string (NetworkPageView.h:48)
      // and it is prefix-first — "Demo model: verified" — because the bare
      // words state that a check RAN and returned a result, and this binary
      // runs no check (G4; the d7 audit's A3 override). The colour only
      // restates the words, so the meaning is never carried on colour alone.
      auto row = MakeValueRow(
          L"Server key", L"Whether this client has pinned the server's key.",
          winrt::hstring{FormatKeyState(world.server.keyVerified)});
      if (auto value = row.trailing.Children().GetAt(0).try_as<TextBlock>()) {
        value.Foreground(world.server.keyVerified
                             ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                             : urnw::colors::MutedBrush());
      }
      card.body.Children().Append(row.root);
    }
    kit::FinalizePaneCard(card);
    column.Children().Append(card.root);
  }

  // ---- Advanced -------------------------------------------------------------
  // Design §6.6: a MODE, not a destination — one persisted toggle that changes
  // what existing surfaces show.
  AppendCaption(column, L"ADVANCED", {}, /*first=*/false);
  {
    auto card = kit::MakePaneCard();
    auto row = kit::MakePaneTwoLineRow(
        L"Advanced Mode",
        L"Adds epoch, raw ids, per-hop timings and the Developer page.", kRowHeight);
    ToggleSwitch toggle;
    if (auto style = StyleByKey(L"UrSwitchToggleStyle")) toggle.Style(style);
    // UrSwitchToggleStyle sets OnContent/OffContent empty, which leaves the
    // switch NAMELESS to a screen reader; App.xaml's own note on that style
    // says to pair every instance with LabeledBy on the row title.
    automation::AutomationProperties::SetLabeledBy(toggle, row.title);

    // IsOn is written BEFORE the Toggled handler is attached, and that order
    // is load-bearing. ToggleSwitch raises Toggled from IsOn(), so wiring
    // first and seeding second would fire the callback during construction —
    // which under --demo-advanced would call SetAdvancedModeEnabled(true) and
    // WRITE the preference design §8 forbids it to write. The equality
    // early-out inside SetAdvancedModeEnabled is only the second line of
    // defence; this ordering is the first.
    toggle.IsOn(advanced);
    toggle.Toggled([onAdvancedChanged, meta = modeMeta](
                       winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
      const bool on = sender.as<ToggleSwitch>().IsOn();
      // The header meta is this view's own business: Settings owns the
      // switch, so it follows itself and needs no Set*Advanced from outside.
      meta.Text(on ? L"Advanced" : L"Normal");
      if (onAdvancedChanged) onAdvancedChanged(on);
    });
    row.trailing.Children().Append(toggle);
    card.body.Children().Append(row.root);
    kit::FinalizePaneCard(card);
    column.Children().Append(card.root);
  }

  // ---- This demo ------------------------------------------------------------
  // Design §2's hard constraint, IN the product — cheaper than hoping a
  // presenter says it. A click that opens a panel THIS FILE owns, so it cannot
  // become a dead affordance if another surface slips.
  AppendCaption(column, L"THIS DEMO", {}, /*first=*/false);
  {
    auto card = kit::MakePaneCard();
    auto row = kit::MakePaneTwoLineRowButton(
        L"What this demo does not do",
        L"Read this before drawing a conclusion from any screen.", kRowHeight);
    // The ON-CARD button variant: UrPaneRowButtonStyle's hover fill IS the
    // card token, so on a card it would paint nothing (the same reason
    // MakePanePresenceRow styles its root UrPaneRowButtonOnCardStyle,
    // UrComponents.cpp:360-363).
    if (auto style = StyleByKey(L"UrPaneRowButtonOnCardStyle")) row.root.Style(style);
    row.value.Text(L"Show");

    Border panel;
    panel.Padding(ThicknessHelper::FromLengths(12, 8, 12, 14));
    panel.Visibility(Visibility::Collapsed);
    TextBlock body;
    if (auto style = StyleByKey(L"UrCaptionTextStyle")) body.Style(style);
    body.TextWrapping(TextWrapping::Wrap);
    body.Text(L"No protocol, no store, no network and no cryptography are running. "
              L"Every value on these screens is fabricated in one module. Nothing has "
              L"been sent, received, stored, encrypted or decrypted. The inspector "
              L"shows what URmessage will one day say about a real message; it is not "
              L"a statement about one.");
    panel.Child(body);

    // The row's bottom hairline follows the panel's visibility: open, it
    // separates the row from the revealed text; closed, the panel takes no
    // space and the line would sit flush against the card's own edge — the
    // double rule FinalizePaneCard exists to prevent, except that it can only
    // see child ORDER and the collapsed panel is still the last child.
    auto rowRoot = row.root;
    // DELIBERATELY INSTANT. Design §7's table is the complete inventory of
    // this work's motion and a settings disclosure is not in it; UrMotion.h's
    // kSoftP1/kSoftP2 ("a gentle disclosure (Advanced Mode reveal)") name the
    // DENSITY change, not this panel. Read as a decision, not an oversight.
    row.root.Click([panel, label = row.value, rowRoot](auto const&, auto const&) {
      const bool open = panel.Visibility() == Visibility::Collapsed;
      panel.Visibility(open ? Visibility::Visible : Visibility::Collapsed);
      rowRoot.BorderThickness(open ? ThicknessHelper::FromLengths(0, 0, 0, 1)
                                   : ThicknessHelper::FromLengths(0, 0, 0, 0));
      label.Text(open ? L"Hide" : L"Show");
    });

    card.body.Children().Append(row.root);
    card.body.Children().Append(panel);
    kit::FinalizePaneCard(card);
    column.Children().Append(card.root);
  }

  // ---- present, and visibly inert (design §6.6, §9.1) -----------------------
  AppendInertGroup(column, L"NOTIFICATIONS");
  AppendInertGroup(column, L"STORAGE AND DATA");
  AppendInertGroup(column, L"ACCOUNT");

  view.root = pane;
  return view;
}

}  // namespace urmsg::views
