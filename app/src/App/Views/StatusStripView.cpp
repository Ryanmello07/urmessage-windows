// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/StatusStripView.h"

#include <memory>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include "UrColors.h"
#include "UrComponents.h"
#include "UrMotion.h"
#include "Views/NetworkPageView.h"
#include "Views/StatusStripRules.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace urmsg::views {
namespace {

namespace kit = urnw::kit;
namespace shapes = winrt::Microsoft::UI::Xaml::Shapes;

// Demo copy is English string literals, NOT localization keys.
// Strings/en/Resources.resw is GENERATED from urnetwork/localizations
// (Localization.h) and this work adds no key to it; localization is out of
// scope (design §2, §9.4).
constexpr wchar_t kServerCaption[] = L"server";
constexpr wchar_t kStateName[] = L"Connection";
// The strip's accessible name. A Button whose Content is a Panel gets NO
// automatic name — this project has paid for that twice (UrComponents.h on
// PaneListRowButton) — and the name has to say what activating it DOES,
// because the drawer it raises is not visible until it does.
constexpr wchar_t kStripName[] = L"Connection status. Activate to show the relay path.";
// The padlock is content — the strip's only statement of key verification —
// and it is NAMED, prefix-first, per the d7 audit's S2 override: the same
// framing AttestationLabel ships at InspectRailFields.cpp:113, for the reason
// recorded there (the bare words state that a check RAN and returned a
// result; this binary runs no check, and --demo-watermark=off removes the
// chip a suffix would lean on).
constexpr wchar_t kKeyVerifiedName[] = L"Demo model: server key verified";
constexpr wchar_t kKeyUnverifiedName[] = L"Demo model: server key not verified";
// The marker SetStatusStripAdvanced looks for. A Tag rather than a list of six
// named members: a separator has no identity beyond "the rule in front of that
// field", and a list of names is how one of them gets forgotten and a hairline
// is left floating at the end of the Normal strip. Namespaced so it cannot
// collide with a Tag anything else in the app sets.
constexpr wchar_t kAdvancedTag[] = L"ur.status.advanced";

// ThreadView.cpp:43's idiom, for the one brush this view reads by key
// (UrBorderStrongBrush — the drawer's d1 edge and the mini-path's rules).
// NetworkPageView.cpp carries the same copy; promoting it beside
// kit::StyleByKey is a follow-up, not this wave's change.
Media::Brush BrushByKey(wchar_t const* key, winrt::Windows::UI::Color fallback) {
  auto app = Application::Current();
  if (app) {
    auto boxed = winrt::box_value(winrt::hstring{key});
    if (app.Resources().HasKey(boxed))
      if (auto b = app.Resources().Lookup(boxed).try_as<Media::Brush>()) return b;
  }
  return urnw::colors::MakeBrush(fallback);
}

// Named, for the reason UrComponents.cpp:25-29 states: FontIcon defaults to
// the older Segoe MDL2 Assets, whose metrics differ.
Media::FontFamily IconFont() { return Media::FontFamily(L"Segoe Fluent Icons"); }

// The dot's fill. Spends the connect-status ramp UrColors.h already carries
// for exactly this dot. NOT kProGold (the Pro entitlement's, reachable from
// nowhere on this surface) and NOT kAccent: UrColors.h records that the
// connecting dot is Yellow400 #E6EA23 from android's circle_indicator_yellow,
// and that the pale kAccent is a different ramp step.
//
// The colour is never the only carrier here — StatusStateWord sits 6 DIP to
// its right and says the same thing in words.
winrt::Windows::UI::Color StatusStateColor(demo::ConnectState state) {
  switch (state) {
    case demo::ConnectState::Connected:
      return urnw::colors::kUrGreen;
    case demo::ConnectState::Connecting:
      return urnw::colors::kStatusConnecting;
    case demo::ConnectState::Offline:
      break;
  }
  return urnw::colors::kTextFaint;
}

// The mini-path's two rules, module-private. The fixed contract gives
// StatusStripView exactly three members, so the elements
// SetStatusStripDrawerOpen has to reach live here, in a registry keyed by the
// drawer — NetworkPageView.cpp's PageParts pattern, for the same reason.
// shared_ptr, not a plain value, so a lookup never copies a XAML reference.
struct DrawerParts {
  FrameworkElement drawer{nullptr};
  shapes::Rectangle ruleA{nullptr};
  shapes::Rectangle ruleB{nullptr};
};

std::vector<std::shared_ptr<DrawerParts>>& DrawerRegistry() {
  // One strip per window and the demo has one window; the vector exists so a
  // second window would still be correct, not as a growth plan. Entries are
  // never removed — a strip lives as long as the process that built it.
  static std::vector<std::shared_ptr<DrawerParts>> registry;
  return registry;
}

std::shared_ptr<DrawerParts> FindDrawerParts(FrameworkElement const& drawer) {
  if (!drawer) return nullptr;
  for (auto const& parts : DrawerRegistry())
    if (parts->drawer == drawer) return parts;
  return nullptr;
}

// A 20x20 host holding an 8x8 dot with a same-size ring BEHIND it (a Grid, so
// the ring is drawn first and the dot sits on top).
//
// The arithmetic, stated so the next reader can check it rather than trust it:
// the ring is 8 DIP and scales to 2.0, i.e. 16 DIP at its peak, inside a 20
// DIP host — 2 DIP of clear space on every side. It must not reach the state
// word, which begins at the host's right edge.
//
// The host is 20 rather than 8 for a second reason: the 8 DIP dot is centred
// in it, which leaves exactly 6 DIP to its right, and 6 is the dot-to-value
// gap kit::MakeStatusField uses inside every other field of the strip
// (row.Spacing(6), UrComponents.cpp). So the state field takes NO left
// margin; the host's own slack already supplies the house spacing.
//
// The ring starts invisible, so a strip that never pulses (offline, or motion
// off) shows exactly the 8 DIP dot and nothing else.
Grid MakeConnectDot(shapes::Ellipse& dot, shapes::Ellipse& ring) {
  Grid host;
  host.Width(20);
  host.Height(20);
  host.VerticalAlignment(VerticalAlignment::Center);

  ring = shapes::Ellipse();
  ring.Width(8);
  ring.Height(8);
  ring.HorizontalAlignment(HorizontalAlignment::Center);
  ring.VerticalAlignment(VerticalAlignment::Center);
  ring.Opacity(0.0);
  // CenterX/Y = half of 8, or the ring grows to the right and down instead of
  // out of the dot.
  Media::ScaleTransform scale;
  scale.CenterX(4);
  scale.CenterY(4);
  ring.RenderTransform(scale);
  host.Children().Append(ring);

  dot = shapes::Ellipse();
  dot.Width(8);
  dot.Height(8);
  dot.HorizontalAlignment(HorizontalAlignment::Center);
  dot.VerticalAlignment(VerticalAlignment::Center);
  host.Children().Append(dot);

  // The colour IS the information and the word beside it says the same thing,
  // so both shapes are decoration to a screen reader — the same treatment,
  // for the same reason, kit::MakeStatusField gives its own dot.
  Automation::AutomationProperties::SetAccessibilityView(
      host, Automation::Peers::AccessibilityView::Raw);
  return host;
}

// The connect dot's motion, per d5 §4.2's states table — all of it gated on
// ShouldAnimate(), and all of it GONE (not reduced) when the OS says so: the
// strip then renders a plain 8 DIP dot and the word, instantly.
//
// Started once and never stopped, and therefore no Storyboard is held. The
// demo's connect state is seeded data that nothing mutates (ambient activity,
// §9.2, only appends message rows), so there is no state change to restart on.
void StartStatusDotMotion(shapes::Ellipse const& dot, shapes::Ellipse const& ring,
                          demo::ConnectState state) {
  namespace anim = Media::Animation;
  // Offline pulses NOTHING: a pulsing offline dot says the app is doing
  // something, which is the one thing it is not (d5 §4.2).
  if (state == demo::ConnectState::Offline) return;
  if (!urnw::motion::ShouldAnimate()) return;

  anim::Storyboard sb;
  if (state == demo::ConnectState::Connected) {
    if (!ring) return;
    // The halo: the 8 DIP ring scales 1.0 -> 2.0 while fading 0.55 -> 0,
    // kPulseMs, standard bezier, forever (design §7's connect-dot row). No new
    // duration and no new curve. 0.55, not 1.0: the ring is a halo, not a
    // second dot, and it must never be bright enough to read as its own state.
    auto grow = [&sb, &ring](wchar_t const* path) {
      auto track = urnw::motion::MakeSplineDouble(1.0, 2.0, urnw::motion::kPulseMs, 0,
                                                  urnw::motion::kStandardP1,
                                                  urnw::motion::kStandardP2);
      anim::Storyboard::SetTarget(track, ring);
      anim::Storyboard::SetTargetProperty(track, path);
      sb.Children().Append(track);
    };
    grow(L"(UIElement.RenderTransform).(ScaleTransform.ScaleX)");
    grow(L"(UIElement.RenderTransform).(ScaleTransform.ScaleY)");

    auto fade = urnw::motion::MakeSplineDouble(0.55, 0.0, urnw::motion::kPulseMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(fade, ring);
    anim::Storyboard::SetTargetProperty(fade, L"Opacity");
    sb.Children().Append(fade);
  } else {
    // Connecting: the DOT itself beats opacity 0.35 <-> 1.0 on kPulseMs,
    // AutoReverse — a "working" beat, visibly distinct from the connected
    // halo (d5 §4.2). The fixture seeds only Connected, so this branch is
    // covered by the state table and never renders in-session.
    if (!dot) return;
    auto pulse = urnw::motion::MakeSplineDouble(0.35, 1.0, urnw::motion::kPulseMs, 0,
                                                urnw::motion::kStandardP1,
                                                urnw::motion::kStandardP2);
    pulse.AutoReverse(true);
    anim::Storyboard::SetTarget(pulse, dot);
    anim::Storyboard::SetTargetProperty(pulse, L"Opacity");
    sb.Children().Append(pulse);
  }

  sb.RepeatBehavior(anim::RepeatBehaviorHelper::Forever());
  sb.Begin();
}

// The mini-path (d5 §4.4 — the drawer's one flourish): a 40 DIP strip holding
// a centred horizontal miniature of the Network page's hero — the three
// RelayNode glyphs joined by two 20x1 rules in UrBorderStrongBrush. Same
// data, no new strings, and the mesh metaphor is visible from every screen
// the strip is on, not just the Network page.
//
// It is HEADER FURNITURE, not a row, so "one row species per pane"
// (UrComponents.h) is untouched, and it is marked Raw: the glyphs restate the
// rows below, which carry the same facts in words. The glyphs are always
// muted — colouring them by health would make colour the only carrier on the
// furniture, and health already rides the rows' dots AND words.
Border MakeMiniPath(std::vector<demo::RelayNode> const& path,
                    shapes::Rectangle& ruleA, shapes::Rectangle& ruleB) {
  Border strip;
  strip.Height(40);
  // The rule under the furniture group, INSIDE the 40 (a Border's Height is
  // its total layout height), so the node list below reads as a separate unit
  // and the total stays d5 §4.4's 1 + 28 + 40 + 3x36 = 177 DIP.
  strip.BorderBrush(urnw::colors::BorderBrush());
  strip.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));

  StackPanel mini;
  mini.Orientation(Orientation::Horizontal);
  // 8: the same clear space the hero diagram leaves each side of its wires
  // (d5 §3.2), scaled down with the miniature.
  mini.Spacing(8);
  mini.HorizontalAlignment(HorizontalAlignment::Center);
  mini.VerticalAlignment(VerticalAlignment::Center);

  shapes::Rectangle* rules[] = {&ruleA, &ruleB};
  int ruleCount = 0;
  for (size_t i = 0; i < path.size(); ++i) {
    FontIcon glyph;
    glyph.FontFamily(IconFont());
    glyph.FontSize(12);
    glyph.Glyph(winrt::hstring{path[i].glyph});
    glyph.Foreground(urnw::colors::MutedBrush());
    mini.Children().Append(glyph);

    if (i + 1 < path.size() && ruleCount < 2) {
      shapes::Rectangle rule;
      rule.Width(20);
      rule.Height(1);
      rule.VerticalAlignment(VerticalAlignment::Center);
      // UrBorderStrongBrush (#38FFFFFF, App.xaml:271): the app's "hairline you
      // are meant to see", the same brush the hero's wires spend.
      rule.Fill(BrushByKey(L"UrBorderStrongBrush", {0x38, 0xFF, 0xFF, 0xFF}));
      // ScaleX 1 at REST, with the origin at the rule's left edge. The open
      // animation follows the from-pose-local rule RunBubbleEntrance settled:
      // SetStatusStripDrawerOpen writes ScaleX 0 as the local value in the
      // same turn it Begins the 0 -> 1 draw-in, and the board's Completed
      // lands 1.0 back — so the rule never renders fully-drawn and then snaps
      // to 0, and no finished board's HoldEnd owns the property.
      Media::ScaleTransform scale;
      scale.CenterX(0);
      scale.CenterY(0);
      rule.RenderTransform(scale);
      *rules[ruleCount] = rule;
      ++ruleCount;
      mini.Children().Append(rule);
    }
  }

  Automation::AutomationProperties::SetAccessibilityView(
      strip, Automation::Peers::AccessibilityView::Raw);
  strip.Child(mini);
  return strip;
}

// The preview drawer the strip raises (design §6.5, d5 §4.4): the relay node
// LIST under the framed header, with the mini-path as its furniture. The full
// map — per-hop timings, the travelling packet, the device list — is the
// Network page's (§6.4).
//
// A plain Border in the window's own tree, not a Popup and not a Flyout: a
// Popup is a second element tree and verify-render.ps1's PrintWindow capture
// does not composite one. This surface exists to be screenshotted.
Border MakeStatusDrawer(std::vector<demo::RelayNode> const& path) {
  Border drawer;
  // 320 — the app's established column width (the conversation list gets
  // exactly 320 at or above kWideBreakpointDip), so the drawer is one of the
  // window's two established column widths rather than a third measurement.
  drawer.Width(320);
  // d1's no-shadow ruling: the drawer is the ONE element that opens ABOVE
  // chrome, so it gets sheet + a border-strong edge + an 8 DIP rise instead
  // of a shadow (no ThemeShadow anywhere in the pane shell). Sheets sit ABOVE
  // the page, never flush with it (UrColors.h): #151515 over the #101010
  // destination. No radius, no margin but the left inset — this window is the
  // PANE vocabulary (MainWindow.xaml's header block).
  drawer.Background(urnw::colors::SheetBrush());
  drawer.BorderBrush(BrushByKey(L"UrBorderStrongBrush", {0x38, 0xFF, 0xFF, 0xFF}));
  // Three edges. The fourth is the strip's own top hairline, immediately below.
  drawer.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 0));
  drawer.HorizontalAlignment(HorizontalAlignment::Left);
  drawer.VerticalAlignment(VerticalAlignment::Bottom);
  // 16: the strip's own content inset, so the drawer's left edge aligns with
  // the left edge of the connect dot's 20 DIP HOST — NOT with the dot's ink,
  // which sits at 22 (the d7 audit's S3 override strikes the brief's "flush
  // with the connect dot": the 8 DIP dot is centred in its 20 DIP host).
  drawer.Margin(ThicknessHelper::FromLengths(16, 0, 0, 0));
  drawer.Visibility(Visibility::Collapsed);
  // MakePaneGroupHeader announces its title as a level-3 heading
  // (UrComponents.cpp's SetHeadingLevel), so the group is reachable by heading
  // navigation. The drawer's own container still needs a name: a heading
  // INSIDE a container does not name the container. The name is the framed
  // kStatusDrawerName (the d7 audit's S3 override) — the header's words frame
  // the EYES, this frames the screen reader, and neither leans on the chip
  // --demo-watermark=off removes.
  Automation::AutomationProperties::SetName(drawer, winrt::hstring{kStatusDrawerName});
  // The rise's transform, attached at rest so SetStatusStripDrawerOpen never
  // grows a transform-attachment block of its own (SettleIn's precedent).
  drawer.RenderTransform(Media::CompositeTransform());

  StackPanel nodes;

  // The framing header: kRelayPathGroupTitle ITSELF, the constant the Network
  // page's hero wears — one owner, so the drawer and the page speak one
  // sentence (d5 §4.4) and can never drift into two spellings of it. The
  // letterspaced chrome voice carries the framing; no new colour, no new key.
  auto header = kit::MakePaneGroupHeader(
      winrt::hstring{kRelayPathGroupTitle},
      winrt::to_hstring(static_cast<int>(path.size())));
  // Its top hairline would land one DIP under the drawer's own top edge and
  // read as a 2 px rule — the same double-rule fix the LAST row gets at the
  // drawer's foot, mirrored at its head. The row species is unchanged; only
  // this instance's leading edge goes.
  header.root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));
  nodes.Children().Append(header.root);

  auto parts = std::make_shared<DrawerParts>();
  parts->drawer = drawer;
  Border furniture = MakeMiniPath(path, parts->ruleA, parts->ruleB);
  nodes.Children().Append(furniture);

  // Every node is one MakePaneListRow, which is what stops the list acquiring
  // a 52px row the day someone gives one of them a second line.
  for (size_t i = 0; i < path.size(); ++i) {
    auto const& node = path[i];
    auto row = kit::MakePaneListRow();
    row.dot.Fill(urnw::colors::MakeBrush(node.healthy ? urnw::colors::kUrGreen
                                                      : urnw::colors::kTextFaint));
    row.title.Text(winrt::hstring{node.label});
    // The state is in WORDS as well as in the dot's colour. MakePaneListRow
    // marks its dot Raw on the explicit premise that "the colour is a
    // restatement of what the row's text already says" (UrComponents.cpp) —
    // here that premise holds only because this line makes it hold.
    row.meta.Text(winrt::hstring{node.healthy ? node.subLabel
                                              : node.subLabel + L"  offline"});
    // ...and the row is named, so the whole fact reaches a screen reader as
    // one item rather than as three fragments with a colour nobody is told
    // about.
    Automation::AutomationProperties::SetName(
        row.root, winrt::hstring{node.label + L", " + node.subLabel + L", " +
                                 (node.healthy ? L"online" : L"offline")});
    // The last row's bottom hairline would land one DIP above the strip's own
    // top hairline and read as a 2 px rule. The row species is unchanged;
    // only this instance's trailing edge goes.
    if (i + 1 == path.size())
      row.root.BorderThickness(ThicknessHelper::FromUniformLength(0));
    nodes.Children().Append(row.root);
  }

  DrawerRegistry().push_back(parts);
  drawer.Child(nodes);
  return drawer;
}

}  // namespace

StatusStripView MakeStatusStrip(urmsg::demo::World const& world) {
  StatusStripView v;

  // The surface. UrStatusStripStyle (App.xaml:590) already IS this chrome —
  // #151515 with one hairline along its TOP edge — so it is applied, not
  // re-spelled. Two local overrides on top of it, both deliberate:
  //   Height 26   design §6.5's figure. The style carries no height.
  //   Padding 0   the style's "16,7" needs ~30 DIP for a 12sp line and would
  //               clip at 26. The 16 DIP inset moves onto the Button below, so
  //               the hover fill spans the WHOLE strip instead of stopping at
  //               the text.
  Border root;
  if (auto style = kit::StyleByKey(L"UrStatusStripStyle")) root.Style(style);
  root.Height(kStatusStripHeightDip);
  root.Padding(ThicknessHelper::FromUniformLength(0));
  root.VerticalAlignment(VerticalAlignment::Bottom);

  // The whole strip is the activator (design §6.5). UrPaneRowButtonStyle
  // brings the hover / pressed fill and the focus visual; its pane-row
  // MinHeight, its bottom hairline and its 12 padding are a pane row's and
  // are overridden here.
  //
  // No Height and no MinHeight of its own. A Border's Height is its TOTAL
  // layout height, so this Border's 26 includes its own 1 DIP top hairline
  // and its content box is 25; a Button pinned to 26 would overflow its
  // parent by a pixel. Stretch fills whatever the content box turns out to
  // be.
  //
  // Background is NOT set — the style's Transparent is what lets the Border's
  // #151515 show through, and the template's PointerOver setter paints
  // UrCardBrush over it.
  v.strip = Button();
  if (auto style = kit::StyleByKey(L"UrPaneRowButtonStyle")) v.strip.Style(style);
  v.strip.BorderThickness(ThicknessHelper::FromUniformLength(0));
  v.strip.MinHeight(0);
  v.strip.VerticalAlignment(VerticalAlignment::Stretch);
  v.strip.Padding(ThicknessHelper::FromLengths(16, 0, 16, 0));
  v.strip.HorizontalContentAlignment(HorizontalAlignment::Left);
  Automation::AutomationProperties::SetName(v.strip, winrt::hstring{kStripName});

  StackPanel fields;
  fields.Orientation(Orientation::Horizontal);
  // 0, not a Spacing: MakeStatusSeparator already carries its own 14/14
  // margins (UrComponents.cpp), and a panel Spacing would add to it at every
  // rule.
  fields.Spacing(0);
  fields.VerticalAlignment(VerticalAlignment::Center);

  shapes::Ellipse dot{nullptr};
  shapes::Ellipse ring{nullptr};
  auto dotHost = MakeConnectDot(dot, ring);
  const auto stateBrush = urnw::colors::MakeBrush(StatusStateColor(world.connectState));
  dot.Fill(stateBrush);
  ring.Fill(stateBrush);
  fields.Children().Append(dotHost);

  // withDot=false: the dot above IS this field's, drawn one element to the
  // left so it can carry the ring. Empty label + accessibleName is the shape
  // MakeStatusField documents for a field whose value speaks for itself — a
  // coloured dot and the word "Connected" need no caption saying "Status" —
  // and SetStatusFieldValue then announces it as "Connection, Connected".
  //
  // No Margin: see MakeConnectDot's note. The 20 DIP host already leaves the
  // kit's 6 DIP gap to the right of the dot.
  auto state = kit::MakeStatusField(winrt::hstring{L""}, /*withDot=*/false,
                                    winrt::hstring{kStateName});
  kit::SetStatusFieldValue(state, winrt::hstring{StatusStateWord(world.connectState)});
  fields.Children().Append(state.root);

  fields.Children().Append(kit::MakeStatusSeparator());

  auto host = kit::MakeStatusField(winrt::hstring{kServerCaption});
  kit::SetStatusFieldValue(host, winrt::hstring{world.server.host});
  fields.Children().Append(host.root);
  // Deliberately NOT world.server.jurisdiction or latencyMs. The owner ruled
  // this a connect indicator, not a network readout (design D4); those two
  // are the Network page's (§6.4) and the drawer's.

  fields.Children().Append(kit::MakeStatusSeparator());

  // 14, one step under UrRowIconStyle's 16: the strip must never compete with
  // the page, which is the same reason its label/value styles are smaller
  // than UrStatLabel/UrStatValue.
  FontIcon lock;
  if (auto style = kit::StyleByKey(L"UrRowIconStyle")) lock.Style(style);
  lock.FontSize(14);
  lock.Glyph(winrt::hstring{StatusLockGlyph(world.server.keyVerified)});
  // Muted when verified, danger when not — deliberately NOT green: the
  // handoff's reviewer note names the green padlock as the one unframed
  // positive claim in the app, and the strip is the worst place to add a
  // second one (d5 §4.1). The framed automation NAME carries the meaning;
  // the colour is the redundant channel.
  lock.Foreground(world.server.keyVerified ? urnw::colors::MutedBrush()
                                           : urnw::colors::DangerBrush());
  // UrRowIconStyle marks its glyph Raw, because a leading mark normally sits
  // beside a label that already says the same word. This one does not: it is
  // the ONLY statement of key verification anywhere on the strip, so it is
  // content, and it is named (prefix-first, the d7 audit's S2 override). The
  // glyph also changes SHAPE between the two states (e72e closed padlock /
  // e785 open padlock), so the fact never rides on colour.
  Automation::AutomationProperties::SetAccessibilityView(
      lock, Automation::Peers::AccessibilityView::Content);
  Automation::AutomationProperties::SetName(
      lock, winrt::hstring{world.server.keyVerified ? kKeyVerifiedName
                                                    : kKeyUnverifiedName});
  fields.Children().Append(lock);

  // Advanced Mode's three, in §6.6's order ("+ epoch, session mode,
  // records/s"). Each is a separator then a field, appended to the SAME
  // horizontal panel — "four more fields must cost four more calls to this
  // function and no layout change" (UrComponents.h on StatusField). They come
  // AFTER the padlock, so the Normal marks never change position when the
  // mode changes.
  //
  // Built ONCE, here, and shown or hidden later — never built and torn down
  // on a toggle. The mode changes live (§9.1) and a strip that re-lays out
  // from scratch visibly jumps; building the values here is also what lets
  // SetStatusStripAdvanced be a Visibility change with nothing to re-read.
  //
  // Collapsed HERE rather than by the caller: a strip that flashes six fields
  // for one frame before the mode is applied is a strip that lies about what
  // Normal mode shows.
  auto addAdvanced = [&fields](winrt::hstring const& caption,
                               winrt::hstring const& value) {
    auto rule = kit::MakeStatusSeparator();
    rule.Tag(winrt::box_value(winrt::hstring{kAdvancedTag}));
    rule.Visibility(Visibility::Collapsed);
    fields.Children().Append(rule);

    auto field = kit::MakeStatusField(caption);
    kit::SetStatusFieldValue(field, value);
    field.root.Tag(winrt::box_value(winrt::hstring{kAdvancedTag}));
    field.root.Visibility(Visibility::Collapsed);
    fields.Children().Append(field.root);
  };
  // Lower case, like "server": strip captions are 11sp chrome that name a
  // value, not headings. "rec/s" is abbreviated because the strip is one row
  // and this is its sixth field.
  addAdvanced(winrt::hstring{L"epoch"},
              winrt::hstring{StatusEpochValue(world.currentEpoch)});
  addAdvanced(winrt::hstring{L"session"}, winrt::hstring{world.sessionMode});
  addAdvanced(winrt::hstring{L"rec/s"},
              winrt::hstring{StatusRecordsValue(world.recordsPerSecond)});

  v.strip.Content(fields);
  root.Child(v.strip);
  v.root = root;

  // Built with the strip and hosted separately by MainWindow: the drawer must
  // draw OVER the destination, and a child of the strip's own Auto-height row
  // would grow that row instead (d5 §4.4).
  v.drawer = MakeStatusDrawer(world.relayPath);

  // The pulse starts from Loaded, never from here: BuildStatusStrip runs from
  // the MainWindow constructor — before Activate(), and while StatusStripHost
  // is still Collapsed — so Begin() here would start a clock against a
  // subtree that has not been loaded. WindowReveal sets this pattern: arm in
  // the constructor, start after the tree is up (the Network page's relay
  // motion follows it too).
  root.Loaded([dot, ring, connectState = world.connectState](auto const&, auto const&) {
    StartStatusDotMotion(dot, ring, connectState);
  });
  return v;
}

void SetStatusStripAdvanced(StatusStripView& v, bool advanced) {
  // DENSITY ONLY. No rebuild and no crossfade: the strip on screen is the
  // strip that stays on screen, with three more fields showing. Cheap and
  // idempotent, so it is safe to call on every toggle and once at startup.
  if (!v.strip) return;
  auto fields = v.strip.Content().try_as<StackPanel>();
  if (!fields) return;
  const auto visibility = advanced ? Visibility::Visible : Visibility::Collapsed;
  const auto marker = winrt::hstring{kAdvancedTag};
  for (auto const& child : fields.Children()) {
    auto element = child.try_as<FrameworkElement>();
    if (!element) continue;
    auto tag = element.Tag().try_as<winrt::hstring>();
    if (!tag || *tag != marker) continue;
    element.Visibility(visibility);
  }
}

void SetStatusStripDrawerOpen(StatusStripView& v, bool open) {
  namespace anim = Media::Animation;
  if (!v.drawer) return;

  if (open) {
    // The drawer's entrance IS a page transition with no outgoing page — the
    // case CrossfadePageSwap documents and already handles ("that is what the
    // three former ConnectPage::AnimateDrawerIn call sites become",
    // UrMotion.h, and UrMotion.cpp really does branch on a null outgoing). It
    // is that call, not a fourth way to fade an element in, and it carries
    // the ShouldAnimate() gate with it.
    urnw::motion::CrossfadePageSwap(nullptr, v.drawer);

    auto const parts = FindDrawerParts(v.drawer);
    auto transform = v.drawer.RenderTransform().try_as<Media::CompositeTransform>();
    if (!urnw::motion::ShouldAnimate()) {
      // Instant swap: the crossfade has already shown it. The rest pose is
      // written back explicitly so the static path never depends on what an
      // interrupted animation left behind.
      if (transform) transform.TranslateY(0.0);
      if (parts) {
        if (parts->ruleA)
          parts->ruleA.RenderTransform().as<Media::ScaleTransform>().ScaleX(1.0);
        if (parts->ruleB)
          parts->ruleB.RenderTransform().as<Media::ScaleTransform>().ScaleX(1.0);
      }
      return;
    }

    anim::Storyboard sb;
    // d1's no-shadow alternative: the drawer opens ABOVE chrome, so it rises
    // kDist8 off the strip over kBaseMs on the standard bezier instead of
    // casting a shadow. FROM-pose local, the rule RunBubbleEntrance settled:
    // a timeline applies its from-value only when it STARTS, so with the rest
    // pose local the frame a begun board takes to attach renders the drawer
    // already settled and then SNAPS it 8 DIP down. The Completed handler
    // lands TranslateY 0 as the local value and Stops the board.
    if (transform) {
      transform.TranslateY(urnw::motion::kDist8);
      auto rise = urnw::motion::MakeSplineDouble(
          urnw::motion::kDist8, 0.0, urnw::motion::kBaseMs, 0,
          urnw::motion::kStandardP1, urnw::motion::kStandardP2);
      anim::Storyboard::SetTarget(rise, v.drawer);
      anim::Storyboard::SetTargetProperty(
          rise, L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)");
      sb.Children().Append(rise);
    }
    // ...and the mini-path ASSEMBLES as the drawer appears: each rule draws
    // in left -> right at kFastMs, kStaggerMs apart (d5 §4.4). Same
    // from-pose-local rule: ScaleX 0 written locally before Begin, 1.0 landed
    // by Completed — a rule left at its ScaleX 1 rest would render fully
    // drawn and then flash to 0 as the draw-in reached it.
    int64_t beginMs = 0;
    for (auto const& rule : {parts ? parts->ruleA : shapes::Rectangle{nullptr},
                             parts ? parts->ruleB : shapes::Rectangle{nullptr}}) {
      if (!rule) continue;
      rule.RenderTransform().as<Media::ScaleTransform>().ScaleX(0.0);
      auto draw = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kFastMs,
                                                 beginMs, urnw::motion::kStandardP1,
                                                 urnw::motion::kStandardP2);
      anim::Storyboard::SetTarget(draw, rule);
      anim::Storyboard::SetTargetProperty(
          draw, L"(UIElement.RenderTransform).(ScaleTransform.ScaleX)");
      sb.Children().Append(draw);
      beginMs = urnw::motion::kStaggerMs;
    }
    sb.Completed([drawer = v.drawer, transform, parts,
                  weakSb = winrt::make_weak(sb)](auto const&, auto const&) {
      // The landing: the open state's WHOLE final pose becomes local values
      // and the board Stops, so the resting drawer is owned by its locals —
      // not by a finished board's HoldEnd — and the toggle (which reads
      // Visibility) and the dismiss fade never meet a held animation value.
      // A board dropped BEFORE completing would still strand the pose; that
      // is why Begin stays inside the click handler, on a chain that is
      // always visible. Runs after any concurrent dismiss began only into
      // properties that are either held by that fade or moot on a Collapsed
      // drawer, and the next open rewrites the from-pose anyway. The board
      // is captured WEAK: a strong capture would cycle
      // board -> delegate -> board.
      drawer.Opacity(1.0);
      if (transform) transform.TranslateY(0.0);
      if (parts) {
        if (parts->ruleA)
          parts->ruleA.RenderTransform().as<Media::ScaleTransform>().ScaleX(1.0);
        if (parts->ruleB)
          parts->ruleB.RenderTransform().as<Media::ScaleTransform>().ScaleX(1.0);
      }
      if (auto board = weakSb.get()) board.Stop();
    });
    sb.Begin();
    return;
  }

  if (!urnw::motion::ShouldAnimate()) {
    v.drawer.Visibility(Visibility::Collapsed);
    v.drawer.Opacity(1.0);
    return;
  }

  // Exits run one step faster than entrances, on the exit curve — UrMotion.h's
  // second rule, and the same pair RunCrossfade spends. No new token.
  anim::Storyboard sb;
  auto fadeOut = urnw::motion::MakeSplineDouble(1.0, 0.0, urnw::motion::kFastMs, 0,
                                                urnw::motion::kExitP1,
                                                urnw::motion::kExitP2);
  anim::Storyboard::SetTarget(fadeOut, v.drawer);
  anim::Storyboard::SetTargetProperty(fadeOut, L"Opacity");
  sb.Children().Append(fadeOut);
  auto drawer = v.drawer;
  sb.Completed([drawer](auto const&, auto const&) {
    // Guard against a re-open landing before this fires: only collapse it if
    // it is still the one that faded out. Same guard, same reason, as
    // UrMotion.cpp's RunCrossfade.
    if (drawer.Opacity() <= 0.01) {
      drawer.Visibility(Visibility::Collapsed);
      drawer.Opacity(1.0);  // restored for its NEXT entrance
    }
  });
  sb.Begin();
}

}  // namespace urmsg::views
