// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Views/DeveloperView.h"

#include <string>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

#include "Demo/AdvancedMode.h"
#include "Demo/DemoSwitches.h"
#include "Demo/DeveloperSwitches.h"
#include "UrColors.h"
#include "UrComponents.h"
#include "UrMotion.h"
#include "Views/DeveloperDump.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace kit = urnw::kit;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;
namespace documents = winrt::Microsoft::UI::Xaml::Documents;

// The promoted one lookup of an App.xaml style (UrComponents.h:106-122): a
// fourth file-local copy is explicitly forbidden by that block, and a
// using-declaration keeps the call sites reading as they do in ThreadView.cpp.
using urnw::kit::StyleByKey;

// There is deliberately no #include "Localization.h" and no Loc() adapter in
// this file, for the reason SettingsView.cpp states: Resources.resw is
// generated and this work adds no key to it (design §9.4), so every string
// here is an English literal or a field of World.

namespace urmsg::views {
namespace {

// The kit builders take `winrt::hstring const&` and a wide literal does not
// convert (two user-defined conversions). Same adapter as SettingsView.cpp.
winrt::hstring S(wchar_t const* text) { return winrt::hstring{text}; }

// The FontFamily twin of kit::StyleByKey, and it exists for the same reason:
// ResourceDictionary::Lookup THROWS hresult_out_of_bounds on a missing key. An
// unguarded Lookup here would throw out of MakeDeveloper, out of
// BuildDeveloper, out of the MainWindow constructor, and kill the app at
// launch. Guarded, a missing key costs the mono face and nothing else — which
// is what keeps "the dump renders in a proportional face" a true diagnosis of
// a misspelled UrMonoFontFamily key rather than a crash.
Media::FontFamily FontFamilyByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Media::FontFamily>();
}

// MakePaneKeyValueRow bakes the accessible name at CONSTRUCTION —
// UrComponents.cpp sets Name(value) = "key, value" — so a row built with one
// value and rewritten later would keep announcing the FIRST value forever.
// Every deferred write the two switches make goes through here.
//
// A figure like "off (no --demo-autoplay)" is one fact with an aside: the
// parenthetical half renders in the muted brush via a second Run, so the
// value keeps the row's ink and the qualifier steps back (polish B1). The
// accessible name still takes the WHOLE string — Runs must never change what
// is announced.
void SetFigure(kit::PaneKeyValueRow const& row, wchar_t const* key,
               winrt::hstring const& value) {
  if (!row.value) return;
  const std::wstring text{value};
  const size_t paren = text.find(L'(');
  if (paren == std::wstring::npos) {
    row.value.Text(value);
  } else {
    row.value.Inlines().Clear();
    documents::Run head;
    head.Text(winrt::hstring{text.substr(0, paren)});
    documents::Run aside;
    aside.Text(winrt::hstring{text.substr(paren)});
    aside.Foreground(urnw::colors::MutedBrush());
    row.value.Inlines().Append(head);
    row.value.Inlines().Append(aside);
  }
  automation::AutomationProperties::SetName(
      row.value, winrt::hstring{std::wstring{key} + L", " + std::wstring{value}});
}

// The FIGURES height (MakePaneKeyValueRow's own default) and the
// EXPLAINED-ROW height. Two groups, two jobs — not one list that drifted.
constexpr double kFigureHeight = 34;
constexpr double kSwitchRowHeight = 44;

// The rail's floating caption (design d4 §4): the letterspaced chrome voice
// stays, but the sheet background, the hairline and the 28px strip go, so a
// section name floats above its card instead of ruling the column. The first
// caption in the body sits 4 from the header, later ones 12. Same three
// overrides SettingsView.cpp's AppendCaption applies to the same builder.
kit::PaneGroupHeader AppendCaption(StackPanel const& column, winrt::hstring const& title,
                                   bool first) {
  auto header = kit::MakePaneGroupHeader(title, {});
  header.root.Background(urnw::colors::MakeBrush({0, 0, 0, 0}));
  header.root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
  header.root.Height(32);
  header.root.Margin(ThicknessHelper::FromLengths(0, first ? 4 : 12, 0, 0));
  column.Children().Append(header.root);
  return header;
}

winrt::hstring ScreenName(demo::DemoScreen screen) {
  switch (screen) {
    case demo::DemoScreen::Chats: return S(L"chats");
    case demo::DemoScreen::Thread: return S(L"thread");
    case demo::DemoScreen::Inspect: return S(L"inspect");
    case demo::DemoScreen::Network: return S(L"network");
    case demo::DemoScreen::Settings: return S(L"settings");
    case demo::DemoScreen::Developer: return S(L"developer");
    case demo::DemoScreen::None: break;
  }
  return S(L"none");
}

// "Ambient activity" is a THREE-state fact, and collapsing it to on/off would
// hide the difference between "never armed" and "armed and held".
winrt::hstring AmbientLabel(bool autoplay) {
  if (!autoplay) return S(L"off (no --demo-autoplay)");
  return demo::AmbientActivityPaused() ? S(L"paused") : S(L"running");
}

winrt::hstring MotionLabel() {
  const bool animates = urnw::motion::ShouldAnimate();
  if (urnw::motion::HasMotionOverride())
    return animates ? S(L"on (override)") : S(L"off (override)");
  return animates ? S(L"on (Windows)") : S(L"off (Windows)");
}

}  // namespace

DeveloperView MakeDeveloper(urmsg::demo::World const& world) {
  DeveloperView view;

  // DeveloperHost (MainWindow.xaml:286) is a bare UrPaneStyle Grid — no pane
  // header strip and no scroller, the same shape SettingsHost is — so the page
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
  TextBlock paneTitle;
  if (auto style = StyleByKey(L"UrPaneTitleStyle")) paneTitle.Style(style);
  // Upper case, like pane_conversations and the Settings page's SETTINGS. The
  // nav item's label is title case ("Developer"); a pane title and a nav label
  // are two voices. No Normal/Advanced meta here, and that is deliberate:
  // this page is only reachable under Advanced Mode, so the meta could only
  // ever read "Advanced" — a figure that cannot change is decoration.
  paneTitle.Text(L"DEVELOPER");
  header.Child(paneTitle);
  Grid::SetRow(header, 0);
  pane.Children().Append(header);

  StackPanel column;
  column.Orientation(Orientation::Vertical);
  // Two caps, left-anchored (polish B1): the column itself caps at 1100 —
  // the DEMOWORLD DUMP card rides it, because a mono table wants width —
  // while the DEMO STATE and SWITCHES groups cap tighter at 840, the same
  // column the Settings page reads as.
  column.MaxWidth(1100);
  column.HorizontalAlignment(HorizontalAlignment::Left);
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.HorizontalScrollMode(ScrollMode::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  // Bottom clearance (polish B1): without it the last dump line can scroll
  // only until its own edge meets the viewport's, guillotined against the
  // status strip; 40 DIP lets it scroll fully clear.
  scroller.Padding(ThicknessHelper::FromLengths(0, 0, 0, 40));
  scroller.Content(column);
  Grid::SetRow(scroller, 1);
  pane.Children().Append(scroller);

  // The launch switches, parsed here and not passed in: ParseDemoOptions is a
  // pure function of the command line with exactly one writer, so the view and
  // the window cannot disagree about what was asked for.
  const auto options = demo::ParseDemoOptions();

  // ---- DEMO STATE: the figures list (key left, value hard right, at 34) ----
  // The 840 cap for the two figure groups is a GRID COLUMN's MaxWidth, never
  // a MaxWidth on the groups themselves: in a StackPanel a stretch child
  // capped by MaxWidth CENTRES in the slot, and a left-aligned one shrinks to
  // its content (both measured on polish-B1 captures). The star column caps
  // at 840 and shrinks with the window below it — capped, left-anchored, and
  // no fixed width to overflow a narrow pane.
  Grid cap840;
  ColumnDefinition capColumn;
  capColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  capColumn.MaxWidth(840);
  cap840.ColumnDefinitions().Append(capColumn);
  StackPanel narrow;
  narrow.Orientation(Orientation::Vertical);
  cap840.Children().Append(narrow);
  column.Children().Append(cap840);

  AppendCaption(narrow, S(L"DEMO STATE"), /*first=*/true);
  {
    auto card = kit::MakePaneCard();
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(S(L"Demo world"),
                                 options.enabled ? S(L"on") : S(L"off"),
                                 kFigureHeight).root);
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(S(L"Deep link (--demo=)"),
                                 ScreenName(options.screen), kFigureHeight).root);
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(S(L"Watermark"),
                                 options.watermark ? S(L"on") : S(L"off"),
                                 kFigureHeight).root);
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(
            S(L"Advanced Mode"),
            urmsg::AdvancedModeEnabled() ? S(L"on") : S(L"off"), kFigureHeight)
            .root);

    // The two live figures. SetFigure runs at BUILD time too, not only from
    // the switches below: it is what renders a parenthetical qualifier in the
    // muted brush, and the first paint deserves the same two voices as a
    // rewrite.
    auto ambientRow = kit::MakePaneKeyValueRow(S(L"Ambient activity"), {},
                                               kFigureHeight);
    SetFigure(ambientRow, L"Ambient activity", AmbientLabel(options.autoplay));
    card.body.Children().Append(ambientRow.root);
    auto motionRow = kit::MakePaneKeyValueRow(S(L"Motion"), {}, kFigureHeight);
    SetFigure(motionRow, L"Motion", MotionLabel());
    card.body.Children().Append(motionRow.root);

    size_t messageRows = 0;
    for (auto const& conversation : world.conversations)
      messageRows += conversation.rows.size();
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(
            S(L"Conversations"),
            winrt::to_hstring(static_cast<int>(world.conversations.size())),
            kFigureHeight).root);
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(
            S(L"Message rows"),
            winrt::to_hstring(static_cast<int>(messageRows)), kFigureHeight).root);
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(
            S(L"Linked devices"),
            winrt::to_hstring(static_cast<int>(world.myDevices.size())),
            kFigureHeight).root);
    card.body.Children().Append(
        kit::MakePaneKeyValueRow(
            S(L"Current epoch"),
            winrt::to_hstring(world.currentEpoch), kFigureHeight).root);
    kit::FinalizePaneCard(card);
    narrow.Children().Append(card.root);

    // ---- SWITCHES (session only; nothing here writes a preference) ---------
    AppendCaption(narrow, S(L"SWITCHES"), /*first=*/false);
    auto switchCard = kit::MakePaneCard();
    {
      auto row = kit::MakePaneTwoLineRow(
          S(L"Pause ambient activity"),
          S(L"Stops the typing indicator and incoming messages."), kSwitchRowHeight);
      ToggleSwitch toggle;
      if (auto style = StyleByKey(L"UrSwitchToggleStyle")) toggle.Style(style);
      // UrSwitchToggleStyle leaves the switch NAMELESS to a screen reader;
      // App.xaml's own note on that style says to pair every instance with
      // LabeledBy on the row title.
      automation::AutomationProperties::SetLabeledBy(toggle, row.title);
      // Seed BEFORE wiring, for the reason SettingsView's Advanced Mode switch
      // states: ToggleSwitch raises Toggled from IsOn(), so wiring first and
      // seeding second would fire the handler during construction.
      toggle.IsOn(demo::AmbientActivityPaused());
      const bool autoplay = options.autoplay;
      toggle.Toggled([figure = ambientRow, autoplay](
                         winrt::Windows::Foundation::IInspectable const& sender,
                         auto const&) {
        demo::SetAmbientActivityPaused(sender.as<ToggleSwitch>().IsOn());
        SetFigure(figure, L"Ambient activity", AmbientLabel(autoplay));
      });
      row.trailing.Children().Append(toggle);
      switchCard.body.Children().Append(row.root);
    }
    {
      auto row = kit::MakePaneTwoLineRow(
          S(L"Animations"),
          S(L"Overrides Windows' own Show animations setting, for this run only."),
          kSwitchRowHeight);
      ToggleSwitch toggle;
      if (auto style = StyleByKey(L"UrSwitchToggleStyle")) toggle.Style(style);
      automation::AutomationProperties::SetLabeledBy(toggle, row.title);
      // Seeded from what the app WOULD do right now, so the switch starts
      // telling the truth even on a machine with animations off.
      toggle.IsOn(urnw::motion::ShouldAnimate());
      toggle.Toggled([figure = motionRow](
                         winrt::Windows::Foundation::IInspectable const& sender,
                         auto const&) {
        // One choke point: the override lives in UrMotion.cpp and every
        // animation in the app already asks ShouldAnimate(). Session-only —
        // no duration, no curve, no preference.
        urnw::motion::SetMotionOverride(sender.as<ToggleSwitch>().IsOn());
        SetFigure(figure, L"Motion", MotionLabel());
      });
      row.trailing.Children().Append(toggle);
      switchCard.body.Children().Append(row.root);
    }
    kit::FinalizePaneCard(switchCard);
    narrow.Children().Append(switchCard.root);
  }

  // ---- DEMOWORLD DUMP ------------------------------------------------------
  {
    auto group = AppendCaption(column, S(L"DEMOWORLD DUMP"), /*first=*/false);
    // The group header's `trailing` slot is the documented home for an
    // icon-only command (UrComponents.h:468-471), so the copy button costs no
    // row. UrPaneActionButtonStyle's own note: every instance MUST carry
    // AutomationProperties.Name — a glyph is not a name.
    Button copy;
    if (auto style = StyleByKey(L"UrPaneActionButtonStyle")) copy.Style(style);
    FontIcon glyph;
    if (auto style = StyleByKey(L"UrRowIconStyle")) glyph.Style(style);
    glyph.Glyph(L"\uE8C8");  // Copy (E8C8)
    glyph.FontSize(14);
    copy.Content(glyph);
    automation::AutomationProperties::SetName(copy, S(L"Copy the DemoWorld dump"));
    // The copied text IS DumpDemoWorld(), framing first line and all: the
    // honesty framing rides the clipboard rather than staying behind on the
    // page (G4; the d7 audit's A6 framing override).
    copy.Click([](auto const&, auto const&) {
      winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
      package.SetText(winrt::hstring{DumpDemoWorld()});
      winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
    });
    group.trailing.Children().Append(copy);

    TextBlock dump;
    if (auto family = FontFamilyByKey(L"UrMonoFontFamily")) dump.FontFamily(family);
    // 12px in the body text brush, not 11 muted (polish B1): a dump too dim
    // to read at arm's length is decoration. The punch named
    // "UrTextSecondaryBrush" — no such key exists in App.xaml and G3 forbids
    // inventing colour tokens, so the one step up the ladder that EXISTS
    // (UrTextBrush, #F8F8F8) carries it.
    dump.FontSize(12);
    dump.Foreground(urnw::colors::TextBrush());
    dump.IsTextSelectionEnabled(true);
    dump.TextWrapping(TextWrapping::NoWrap);
    dump.Text(winrt::hstring{DumpDemoWorld()});

    // The dump is WIDE and the pane must not be. Its own horizontal scroller
    // keeps the long lines readable without widening the destination.
    ScrollViewer dumpScroller;
    dumpScroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    dumpScroller.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
    dumpScroller.HorizontalScrollMode(ScrollMode::Enabled);
    dumpScroller.VerticalScrollMode(ScrollMode::Disabled);
    dumpScroller.Padding(ThicknessHelper::FromLengths(12, 8, 12, 12));
    dumpScroller.Content(dump);

    auto card = kit::MakePaneCard();
    card.body.Children().Append(dumpScroller);
    // NO FinalizePaneCard here: its one job is clearing the last MakePaneRow's
    // bottom hairline, and this card's only child is a ScrollViewer, not a
    // row — there is no hairline to clear.
    column.Children().Append(card.root);
  }

  view.root = pane;
  return view;
}

}  // namespace urmsg::views
