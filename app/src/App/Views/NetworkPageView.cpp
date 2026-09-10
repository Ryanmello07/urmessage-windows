// SPDX-License-Identifier: MPL-2.0
// the project compiles with /Yu"pch.h" (App.vcxproj), so every translation unit
// must include it first
#include "pch.h"

#include "Views/NetworkPageView.h"

#include <cwctype>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include "Log.h"
#include "UrColors.h"
#include "UrComponents.h"
#include "UrMotion.h"
#include "Views/StatusStripRules.h"  // StatusStateWord, the pane header meta's one owner
#include "Views/StatusStripView.h"   // kStatusDrawerName, for the framing gate

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace shapes = winrt::Microsoft::UI::Xaml::Shapes;
namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;

// The promoted one lookup of an App.xaml style (UrComponents.h:106-122): a
// fourth file-local copy is explicitly forbidden by that block, and a
// using-declaration keeps the call sites reading as they do in ThreadView.cpp.
using urnw::kit::StyleByKey;

// There is deliberately no #include "Localization.h" and no Loc() adapter in
// this file: Resources.resw is generated (Localization.h:3-4) and this work
// adds no keys, so every string here is either an English literal or a field
// of World.

namespace urmsg::views {
namespace {

// PASS / FAIL with the QUERY printed beside the value it compared. A line that
// says PASS without showing what it asked is not evidence — it is a claim.
const wchar_t* Check(bool ok) { return ok ? L"PASS" : L"FAIL"; }

// Case-insensitive ASCII prefix test for the "demo model:" framing. The chrome
// voice is uppercase (DEMO MODEL: RELAY PATH) and the automation-name voice is
// mixed-case ("Demo model: relay path preview"); the gated property is that
// the PREFIX is present, not which casing carries it.
bool StartsWithDemoModel(std::wstring const& s) {
  const std::wstring prefix = L"demo model:";
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i)
    if (std::towlower(s[i]) != prefix[i]) return false;
  return true;
}

// The page's elements, module-private.
//
// The fixed contract gives NetworkPageView exactly one member (`root`), so the
// elements SetNetworkPageAdvanced has to reach live here instead, in a registry
// keyed by the page root. shared_ptr, not a plain value, because the device
// rows' Click handlers (N5) capture it.
struct PageParts {
  FrameworkElement root{nullptr};

  // the relay path
  FrameworkElement pathRoot{nullptr};
  // One per wire whose target hopMs > 0; Advanced Mode only. The hop figure
  // rides the WIRE it measures (d5 §3.3), not a third line inside the node
  // card — a latency figure belongs on the link it measures, and node 0's
  // 0 ms is "no hop to yourself", which renders nothing (the d7 audit's N2
  // override).
  std::vector<TextBlock> wireLabels;
  TextBlock roundTrip{nullptr};  // Advanced Mode only
  shapes::Ellipse packetA{nullptr};
  shapes::Ellipse packetB{nullptr};
  // Built by MakeRelayPath and left STOPPED (design D6 quarantines the moving
  // wire behind Advanced Mode). SetRelayPathAnimated is the only Begin caller.
  anim::Storyboard motion{nullptr};
  bool animate = false;

  // The three top-level sections — path panel, server group, devices group —
  // for the first-show entrance (d5 §3.7). entranceDone makes it run ONCE:
  // re-navigation must not become a light show.
  std::vector<FrameworkElement> sections;
  bool entranceDone = false;

  // your devices
  StackPanel deviceList{nullptr};
  FrameworkElement deviceEmpty{nullptr};
  TextBlock deviceCount{nullptr};
  // An EXPLICIT counter. Deriving the count from deviceList.Children().Size()
  // would make it depend on what else anyone ever appends to that panel, with
  // no compile error the day someone does.
  int deviceRemaining = 0;
};

// One page per window, and the demo has one window; the vector exists so a
// second window would still be correct, not as a growth plan. Entries are never
// removed — a page lives as long as the process that built it.
std::vector<std::shared_ptr<PageParts>>& Registry() {
  static std::vector<std::shared_ptr<PageParts>> registry;
  return registry;
}

std::shared_ptr<PageParts> Find(FrameworkElement const& root) {
  if (!root) return nullptr;
  for (auto const& parts : Registry())
    if (parts->root == root) return parts;
  return nullptr;
}

// ThreadView.cpp:43's idiom, for the one brush this page reads by key.
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

// The rail's floating caption (design d4 §4): the letterspaced chrome voice
// stays, but the sheet background, the hairline and the 28px strip go, so a
// section name floats above its card instead of ruling the column. The first
// caption in the body sits 4 from the header, later ones 12. Same three
// overrides InspectRailView.cpp's AppendCaption applies to the same builder.
urnw::kit::PaneGroupHeader AppendCaption(StackPanel const& column,
                                         winrt::hstring const& title,
                                         winrt::hstring const& meta, bool first) {
  auto header = urnw::kit::MakePaneGroupHeader(title, meta);
  header.root.Background(urnw::colors::MakeBrush({0, 0, 0, 0}));
  header.root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
  header.root.Height(32);
  header.root.Margin(ThicknessHelper::FromLengths(0, first ? 4 : 12, 0, 0));
  column.Children().Append(header.root);
  return header;
}

// Named, for the reason UrComponents.cpp:25-29 states: FontIcon defaults to the
// older Segoe MDL2 Assets, whose metrics differ.
Media::FontFamily IconFont() { return Media::FontFamily(L"Segoe Fluent Icons"); }

// One node of the relay path: a 28epx glyph over a title and one muted
// sub-line, every string from the RelayNode — nothing on this diagram is a
// literal invented in this file (N3's own rule, kept).
//
// A PLAIN Border, deliberately NOT UrStatTileStyle. That style is BasedOn
// UrCardStyle and adds CornerRadius 8 (App.xaml:566) — three 8px-radius tiles
// sitting directly above square pane rows is exactly the mixed vocabulary
// MainWindow.xaml's header (lines 10-16) says this window does not use. The
// node keeps the card FILL (#1C1C1C) and the 1px border and drops the radius.
//
// NO in-card third line: the hop figure moved onto the wire (d5 §3.3) — a
// latency figure belongs on the link it measures, and a line that is absent
// on exactly one of three cards (node 0, no hop to yourself) has no good home.
FrameworkElement MakeNode(urmsg::demo::RelayNode const& node) {
  Border card;
  card.Background(urnw::colors::CardBrush());
  card.BorderBrush(urnw::colors::BorderBrush());
  card.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
  // Polish B1: the row was a hero's bones at a legend's size — 168-wide cards
  // and a 20px glyph left ~325 DIP of dead space either side at 1560. The
  // scale moves up one step (208 min, 20,16 padding, 28px glyph); the measure
  // stays FIXED — no fluid layout invented.
  card.Padding(ThicknessHelper::FromLengths(20, 16, 20, 16));
  card.MinWidth(208);
  card.VerticalAlignment(VerticalAlignment::Center);

  StackPanel column;
  column.Orientation(Orientation::Vertical);
  column.Spacing(6);
  column.HorizontalAlignment(HorizontalAlignment::Center);

  FontIcon icon;
  icon.FontFamily(IconFont());
  // From the world. The contract fixes these as E977 (Devices), E774 (Globe)
  // and E968 (Server) and --diagnose asserts none of them is empty (I7).
  icon.Glyph(winrt::hstring{node.glyph});
  icon.FontSize(28);
  // Colour is not the only carrier of health: an unhealthy node also says so
  // in its automation name below.
  icon.Foreground(node.healthy ? urnw::colors::MutedBrush()
                               : urnw::colors::DangerBrush());
  icon.HorizontalAlignment(HorizontalAlignment::Center);
  MarkRaw(icon);
  column.Children().Append(icon);

  TextBlock title;
  if (auto style = StyleByKey(L"UrRowTitleStyle")) title.Style(style);
  title.Text(winrt::hstring{node.label});
  title.HorizontalAlignment(HorizontalAlignment::Center);
  column.Children().Append(title);

  TextBlock sub;
  if (auto style = StyleByKey(L"UrRowNoteStyle")) sub.Style(style);
  sub.Text(winrt::hstring{node.subLabel});
  sub.HorizontalAlignment(HorizontalAlignment::Center);
  column.Children().Append(sub);

  card.Child(column);
  Automation::AutomationProperties::SetName(
      card, winrt::hstring{node.label + L", " + node.subLabel +
                           (node.healthy ? L"" : L", not healthy")});
  return card;
}

struct WireParts {
  FrameworkElement root{nullptr};
  TextBlock label{nullptr};      // null when the hop renders no figure (hopMs <= 0)
  shapes::Ellipse packet{nullptr};
};

// One leg of the path, as one 112 DIP cell holding four things: the wire, its
// direction cue, the Advanced hop label above the midpoint, and the packet
// that rides it. `hopTarget` is the node this wire delivers INTO:
// RelayNode::hopMs is the timing of the hop TERMINATING at that node, which
// is what makes the label honest about which link it measures.
WireParts MakeWireCell(urmsg::demo::RelayNode const& hopTarget) {
  WireParts out;
  Grid cell;
  // 96 DIP of wire plus 8 of clear space each side — a FIXED span, not a star
  // column, so the diagram has ONE measurable width (3x208 + 2x112 = 848 DIP)
  // at every window size and cannot be squeezed to nothing by a long label.
  cell.Width(112);
  cell.VerticalAlignment(VerticalAlignment::Center);

  shapes::Rectangle wire;
  wire.Width(96);
  wire.Height(2);
  wire.RadiusX(1);
  wire.RadiusY(1);
  wire.HorizontalAlignment(HorizontalAlignment::Center);
  wire.VerticalAlignment(VerticalAlignment::Center);
  // UrBorderStrongBrush (#38FFFFFF, App.xaml:271) is already this app's "a
  // hairline you are MEANT to see"; a wire is that, 2px thick.
  wire.Fill(BrushByKey(L"UrBorderStrongBrush", {0x38, 0xFF, 0xFF, 0xFF}));
  MarkRaw(wire);
  cell.Children().Append(wire);

  // The direction cue (d5 §3.2): without it the row reads as three boxes and
  // two rules; with it the row reads as a PATH with a direction, which is the
  // whole story the fixture tells (this device -> URnetwork -> server). A
  // chevron describes the path's shape, and the path's shape is fixture data
  // (the vector order is the path order) — it asserts no delivery and no
  // crypto, so it is G4 clean. Static in both modes. 10px in textMuted, not
  // d5's 8px textFaint (polish B1): at 8px the cue read as a nick in the wire
  // rather than a mark ON it, and the brush describes shape, not state, so no
  // colour semantics move with it.
  FontIcon cue;
  cue.FontFamily(IconFont());
  cue.Glyph(L"\uE76C");  // ChevronRight, Segoe Fluent Icons
  cue.FontSize(10);
  cue.Foreground(urnw::colors::MutedBrush());
  cue.HorizontalAlignment(HorizontalAlignment::Right);
  cue.VerticalAlignment(VerticalAlignment::Center);
  cue.Margin(ThicknessHelper::FromLengths(0, 0, 1, 0));
  MarkRaw(cue);
  cell.Children().Append(cue);

  // The Advanced per-hop label, above the wire's midpoint (d5 §3.3). Rendered
  // ONLY when hopMs > 0 — node 0's 0 is "no hop to yourself" and shows
  // nothing (the d7 audit's N2 override). Built Collapsed;
  // SetNetworkPageAdvanced only ever flips Visibility — density, never a
  // rebuild, never a crossfade.
  if (hopTarget.hopMs > 0) {
    TextBlock label;
    if (auto style = StyleByKey(L"UrRowNoteStyle")) label.Style(style);
    label.Text(winrt::hstring{FormatLatency(hopTarget.hopMs)});
    label.Foreground(urnw::colors::FaintBrush());
    label.HorizontalAlignment(HorizontalAlignment::Center);
    label.VerticalAlignment(VerticalAlignment::Center);
    // 10 DIP of lift, not 18 (polish B1): the label names the wire below it,
    // so it hugs the wire rather than floating into the node cards' row.
    label.Margin(ThicknessHelper::FromLengths(0, 0, 0, 10));
    label.Visibility(Visibility::Collapsed);
    out.label = label;
    cell.Children().Append(label);
  }

  // The packet (d5 §3.3): one 4x4 disc in kUrGreen — the chart semantics'
  // "bytes" colour (UrColors.h:69-70), NOT accent and NOT gold, both
  // forbidden here by G3 — riding 1 DIP above the wire. Opacity 0 at rest:
  // the marker exists only while the travel storyboard runs.
  shapes::Ellipse packet;
  packet.Width(4);
  packet.Height(4);
  packet.Fill(urnw::colors::MakeBrush(urnw::colors::kUrGreen));
  packet.Opacity(0.0);
  packet.HorizontalAlignment(HorizontalAlignment::Left);
  packet.VerticalAlignment(VerticalAlignment::Center);
  packet.Margin(ThicknessHelper::FromLengths(0, 0, 0, 8));
  packet.RenderTransform(Media::TranslateTransform());
  MarkRaw(packet);
  out.packet = packet;
  cell.Children().Append(packet);

  out.root = cell;
  return out;
}

// ONE opacity track per packet, not a separate fade-in and fade-out: a
// Storyboard cannot hold two animations targeting the same property on one
// element — XAML throws "Multiple animations in the same containing
// Storyboard cannot target the same property on a single element", measured
// this wave on the first --demo-advanced launch. The shape is d5 §3.3's,
// merged into one track: materialise over the first kFastMs, hold, dissolve
// over the last kFastMs — same UrMotion tokens, same standard bezier, no new
// duration and no new curve.
anim::DoubleAnimationUsingKeyFrames MakePacketOpacity(shapes::Ellipse const& packet,
                                                      int64_t beginMs) {
  // A FRESH KeySpline per keyframe: one KeySpline instance cannot be the
  // property value of two keyframes at once (XAML throws E_INVALIDARG,
  // measured this wave), so the curve is rebuilt rather than shared.
  const auto spline = [] {
    anim::KeySpline s;
    s.ControlPoint1(urnw::motion::kStandardP1);
    s.ControlPoint2(urnw::motion::kStandardP2);
    return s;
  };
  const auto at = [](int64_t ms) { return anim::KeyTime{urnw::motion::Ms(ms)}; };
  anim::SplineDoubleKeyFrame f0;
  f0.KeyTime(at(0));
  f0.Value(0.0);
  anim::SplineDoubleKeyFrame f1;
  f1.KeyTime(at(urnw::motion::kFastMs));
  f1.Value(1.0);
  f1.KeySpline(spline());
  anim::SplineDoubleKeyFrame f2;  // the hold: equal values, so no curve needed
  f2.KeyTime(at(urnw::motion::kEpicMs - urnw::motion::kFastMs));
  f2.Value(1.0);
  anim::SplineDoubleKeyFrame f3;
  f3.KeyTime(at(urnw::motion::kEpicMs));
  f3.Value(0.0);
  f3.KeySpline(spline());
  anim::DoubleAnimationUsingKeyFrames opacity;
  opacity.KeyFrames().Append(f0);
  opacity.KeyFrames().Append(f1);
  opacity.KeyFrames().Append(f2);
  opacity.KeyFrames().Append(f3);
  if (0 < beginMs) opacity.BeginTime(urnw::motion::Ms(beginMs));
  anim::Storyboard::SetTarget(opacity, packet);
  anim::Storyboard::SetTargetProperty(opacity, L"Opacity");
  return opacity;
}

// Three nodes and two wire cells on one 5-column grid: node, wire, node,
// wire, node. Fills the path fields of `parts` and returns the element to
// place.
FrameworkElement MakeRelayPath(urmsg::demo::World const& world,
                               std::shared_ptr<PageParts> const& parts) {
  Grid grid;
  // Left-anchored inside the page's capped content column (polish B1): the
  // diagram is a legend row, not a hero, so it hangs off the column's left
  // edge with the cards below instead of floating mid-pane.
  grid.HorizontalAlignment(HorizontalAlignment::Left);
  for (int i = 0; i < 5; ++i) {
    ColumnDefinition column;
    column.Width(GridLengthHelper::Auto());
    grid.ColumnDefinitions().Append(column);
  }
  // Two rows: the path itself, and the Advanced round trip under it. The
  // round trip is a GRID row, not a sibling under the scroller, for one
  // reason: "centred under the whole diagram" is only exact if the line's
  // layout slot IS the diagram's 848 DIP — a sibling under the panel centres
  // on the panel instead, and with the diagram left-anchored the two
  // midpoints no longer agree.
  RowDefinition pathRow, rttRow;
  pathRow.Height(GridLengthHelper::Auto());
  rttRow.Height(GridLengthHelper::Auto());
  grid.RowDefinitions().Append(pathRow);
  grid.RowDefinitions().Append(rttRow);

  // The contract fixes relayPath at exactly three and --diagnose asserts it
  // (I7, and `net relay hops`). Guarded anyway: a short world must not index
  // past the end of a vector at 1560x900 in front of an audience.
  if (world.relayPath.size() != 3)
    urnw::LogWarn("network: relayPath has {} nodes, expected 3", world.relayPath.size());

  const size_t nodes = world.relayPath.size() < 3 ? world.relayPath.size() : 3;
  for (size_t i = 0; i < nodes; ++i) {
    auto node = MakeNode(world.relayPath[i]);
    Grid::SetColumn(node, static_cast<int32_t>(i * 2));
    grid.Children().Append(node);
  }

  for (size_t i = 0; i + 1 < nodes; ++i) {
    // Wire i delivers into node i+1, so the hop it labels and the packet it
    // carries belong to relayPath[i+1]'s hopMs.
    auto cell = MakeWireCell(world.relayPath[i + 1]);
    Grid::SetColumn(cell.root, static_cast<int32_t>(i * 2 + 1));
    grid.Children().Append(cell.root);
    if (cell.label) parts->wireLabels.push_back(cell.label);
    (i == 0 ? parts->packetA : parts->packetB) = cell.packet;
  }

  // ADVANCED ONLY. ServerInfo carries ONE end-to-end latency and no per-hop
  // split, so this says "round trip" and is centred under the whole diagram
  // (a full-span row of the diagram's own grid, per the block above); the
  // per-hop figures ride the wires and are the only thing on this page
  // entitled to name a hop. Built Collapsed; SetNetworkPageAdvanced flips
  // Visibility — density, never a rebuild.
  parts->roundTrip = TextBlock();
  if (auto style = StyleByKey(L"UrRowNoteStyle")) parts->roundTrip.Style(style);
  parts->roundTrip.Text(winrt::hstring{FormatRoundTrip(world.server)});
  parts->roundTrip.HorizontalAlignment(HorizontalAlignment::Center);
  parts->roundTrip.Margin(ThicknessHelper::FromLengths(0, 16, 0, 0));
  parts->roundTrip.Visibility(Visibility::Collapsed);
  Grid::SetRow(parts->roundTrip, 1);
  Grid::SetColumnSpan(parts->roundTrip, 5);
  grid.Children().Append(parts->roundTrip);

  // The travelling packet (d5 §3.3), REPLACING the brief's wire-opacity pulse:
  // a whole wire breathing reads as blinking chrome and two wires out of
  // phase read as a fault; the same motion budget spent as a travelling
  // marker says the actual thing — bytes move device -> server. One
  // storyboard, built STOPPED: kEpicMs of travel on the standard bezier, wire
  // B offset by kSlowMs so the path reads as flowing rather than flashing;
  // the packet materialises over the first kFastMs and dissolves over the
  // last (MakePacketOpacity — one merged track, for the reason it states).
  anim::RepeatBehavior forever{};
  forever.Type = anim::RepeatBehaviorType::Forever;
  anim::Storyboard board;
  board.RepeatBehavior(forever);
  int64_t wireIndex = 0;
  for (auto const& packet : {parts->packetA, parts->packetB}) {
    if (!packet) continue;
    const int64_t begin = wireIndex * urnw::motion::kSlowMs;
    // 108 = the 112 DIP cell minus the 4 DIP packet — the marker rides its
    // own wire edge to edge and no further. Cell-relative, so the page's
    // column cap and left anchor move the whole diagram without touching
    // this travel.
    auto travel = urnw::motion::MakeSplineDouble(0.0, 108.0, urnw::motion::kEpicMs,
                                                 begin, urnw::motion::kStandardP1,
                                                 urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(travel, packet);
    anim::Storyboard::SetTargetProperty(
        travel, L"(UIElement.RenderTransform).(TranslateTransform.X)");
    board.Children().Append(travel);
    board.Children().Append(MakePacketOpacity(packet, begin));
    ++wireIndex;
  }
  parts->motion = board;

  // Started from Loaded, never from the constructor. BuildNetworkPage runs
  // from the MainWindow constructor — before Activate(), and while
  // NetworkHost is still Collapsed — so Begin() there would start a clock
  // against a subtree that has not been loaded. WindowReveal sets this
  // pattern: arm in the constructor, start after the tree is up.
  grid.Loaded([parts](auto const&, auto const&) {
    if (!parts->animate || !parts->motion) return;
    parts->motion.Begin();
    urnw::LogInfo("network: relay motion begun on Loaded");
  });

  parts->pathRoot = grid;
  return grid;
}

// Start or stop the packet travel. File-local on purpose: the fixed
// contract's public API for this surface is MakeNetworkPage +
// SetNetworkPageAdvanced, and nothing outside this file needs to reach a
// wire.
void SetRelayPathAnimated(std::shared_ptr<PageParts> const& parts, bool animated) {
  if (!parts || !parts->motion) return;
  // "Show animations in Windows = off" means motion is GONE, not reduced. An
  // Advanced-Mode user with that setting gets the static diagram, correct and
  // instant — which is also the only honest reading of D6 for them. The hop
  // labels and the round trip still appear there: they are density, not
  // motion (d5 §3.8).
  parts->animate = animated && urnw::motion::ShouldAnimate();
  const bool loaded = parts->pathRoot && parts->pathRoot.IsLoaded();
  if (parts->animate) {
    // If the tree is not up yet, the Loaded handler in MakeRelayPath starts it.
    if (loaded) parts->motion.Begin();
  } else {
    parts->motion.Stop();
    // Written back explicitly rather than trusting Stop() to restore it: at
    // rest the packets are invisible markers, not whatever value the timeline
    // happened to be holding when it was stopped.
    if (parts->packetA) parts->packetA.Opacity(0.0);
    if (parts->packetB) parts->packetB.Opacity(0.0);
  }
  // The motion line logs FIRST, inside the function that reads the values it
  // prints; "network: advanced mode ->" follows in SetNetworkPageAdvanced and
  // "begun on Loaded" lands when the tree comes up — the d7 audit's N6 order.
  urnw::LogInfo("network: relay motion -> {} (advanced={} shouldAnimate={})",
                parts->animate ? "started" : "stopped", animated,
                urnw::motion::ShouldAnimate());
}

// The remove button's reveal state (d5 §3.6): it appears on row hover OR
// keyboard focus, so both channels are tracked and either one keeps it shown.
struct RemoveReveal {
  bool hover = false;
  bool focus = false;
};

void SetRemoveRevealed(Button const& remove, bool shown) {
  const double target = shown ? 1.0 : 0.0;
  if (!urnw::motion::ShouldAnimate()) {
    remove.Opacity(target);
    return;
  }
  // In over kFastMs on the standard curve; out one step faster on the exit
  // curve — UrMotion's exits-faster rule. Resumes from the current opacity,
  // so a pointer flicking across the row edge never pops the button.
  const bool entrance = shown;
  auto fade = urnw::motion::MakeSplineDouble(
      remove.Opacity(), target, entrance ? urnw::motion::kFastMs : urnw::motion::kMicroMs,
      0, entrance ? urnw::motion::kStandardP1 : urnw::motion::kExitP1,
      entrance ? urnw::motion::kStandardP2 : urnw::motion::kExitP2);
  anim::Storyboard::SetTarget(fade, remove);
  anim::Storyboard::SetTargetProperty(fade, L"Opacity");
  anim::Storyboard board;
  board.Children().Append(fade);
  board.Begin();
}

}  // namespace

std::wstring FormatLatency(int latencyMs) {
  return std::to_wstring(latencyMs) + L" ms";
}

std::wstring FormatKeyState(bool keyVerified) {
  return keyVerified ? std::wstring(L"Demo model: verified")
                     : std::wstring(L"Demo model: not verified");
}

std::wstring FormatDeviceMeta(urmsg::demo::DeviceRef const& device) {
  // U+00B7 MIDDLE DOT, the separator the pane rows already use between two
  // facts on one line.
  const std::wstring state =
      device.online ? std::wstring(L"Online") : (L"Last seen " + device.lastSeenLabel);
  return device.isThisComputer ? (L"This computer · " + state) : state;
}

std::wstring FormatRoundTrip(urmsg::demo::ServerInfo const& server) {
  return FormatLatency(server.latencyMs) + L" round trip";
}

std::vector<std::wstring> CollectNetworkDiagnostics() {
  const urmsg::demo::World& world = urmsg::demo::GetWorld();
  std::vector<std::wstring> lines;

  lines.push_back(std::format(
      L"  net fmt latency  : {}  FormatLatency(18) == \"18 ms\" -> \"{}\"",
      Check(FormatLatency(18) == L"18 ms"), FormatLatency(18)));

  // The prefix-first pair, and nothing else: the bare words "Verified" /
  // "Not verified" state that a check RAN (G4), so a regression to them fails
  // this line (the d7 audit's N2 override).
  lines.push_back(std::format(
      L"  net fmt keystate : {}  FormatKeyState(true|false) == \"Demo model: "
      L"verified\" | \"Demo model: not verified\" -> \"{}\" | \"{}\"",
      Check(FormatKeyState(true) == L"Demo model: verified" &&
            FormatKeyState(false) == L"Demo model: not verified"),
      FormatKeyState(true), FormatKeyState(false)));

  // A PROPERTY over the whole device list, not one hand-built probe: exactly
  // the device DemoWorld marks isThisComputer names itself, and no other device
  // claims to be this computer. A stub returning "" fails it, and so does a
  // world with two local machines.
  bool deviceOk = !world.myDevices.empty();
  std::wstring thisComputerLine;
  for (auto const& device : world.myDevices) {
    const std::wstring meta = FormatDeviceMeta(device);
    if (device.isThisComputer) thisComputerLine = meta;
    if (meta.starts_with(L"This computer") != device.isThisComputer) deviceOk = false;
  }
  lines.push_back(std::format(
      L"  net fmt device   : {}  exactly the isThisComputer device says so, over "
      L"{} devices -> \"{}\"",
      Check(deviceOk), world.myDevices.size(), thisComputerLine));

  // Against a LOCALLY BUILT adversarial value, never the implementation
  // restated and never a new DemoWorld row (the world is I10-fingerprinted):
  // the "7 ms round trip" literal on the left is what makes this able to fail
  // (the d7 audit's N2 override).
  urmsg::demo::ServerInfo probe{};
  probe.latencyMs = 7;
  lines.push_back(std::format(
      L"  net fmt rtt      : {}  FormatRoundTrip(probe latencyMs=7) == \"7 ms "
      L"round trip\" -> \"{}\"; world -> \"{}\"",
      Check(FormatRoundTrip(probe) == L"7 ms round trip" &&
            FormatRoundTrip(world.server) ==
                FormatLatency(world.server.latencyMs) + L" round trip"),
      FormatRoundTrip(probe), FormatRoundTrip(world.server)));

  // A PRECONDITION on the world, not on this file's code. hopMs < 0 is the
  // failure, NOT hopMs <= 0: node 0's 0 ms is "no hop to yourself" and renders
  // no figure, so a zero is correct data (the d7 audit's N2 override — the
  // brief's own <= 0 would have failed the shipped world). I7
  // (Startup.cpp:317-322) already asserts size==3 and non-empty glyphs, so the
  // only new thing this line buys is the label/hop check.
  bool hopsOk = (world.relayPath.size() == 3);
  int hopSum = 0;
  for (auto const& node : world.relayPath) {
    hopSum += node.hopMs;
    if (node.hopMs < 0 || node.glyph.empty() || node.label.empty()) hopsOk = false;
  }
  lines.push_back(std::format(
      L"  net relay hops   : {}  relayPath.size()==3, every hopMs >= 0 (node 0's "
      L"0 is no hop to yourself) and label non-empty — buys the label/hop check "
      L"only; I7 owns size and glyphs (nodes {}, hops total {} ms)",
      Check(hopsOk), world.relayPath.size(), hopSum));

  // The framing header is the page's single always-on honesty frame (d5 §3.4),
  // so its prefix cannot be dropped silently (d5 §6): the page renders
  // kRelayPathGroupTitle and this gate reads the SAME constant. The strip's
  // preview drawer renders that same constant for its header (one owner, one
  // sentence — d5 §4.4), so the drawer's own always-present string is what
  // gets checked beside it: the automation name kStatusDrawerName.
  const std::wstring relayTitle{kRelayPathGroupTitle};
  const std::wstring drawerName{kStatusDrawerName};
  lines.push_back(std::format(
      L"  net framing      : {}  relay group title + strip drawer name open "
      L"with \"demo model:\" (either casing) -> \"{}\" | \"{}\"",
      Check(StartsWithDemoModel(relayTitle) && StartsWithDemoModel(drawerName)),
      relayTitle, drawerName));

  return lines;
}

NetworkPageView MakeNetworkPage(urmsg::demo::World const& world) {
  auto parts = std::make_shared<PageParts>();

  // NetworkHost (MainWindow.xaml:282) is a bare UrPaneStyle Grid — no pane
  // header strip and no scroller, deliberately (the d7 audit's N3 override) —
  // so the page builds both: row 0 is the 40 DIP header every pane opens
  // with, row 1 the scroller whose horizontal Disable is what constrains the
  // content to the viewport width and makes the group headers and rows
  // stretch to the pane instead of shrinking to their text.
  Grid root;
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  root.RowDefinitions().Append(headerRow);
  root.RowDefinitions().Append(bodyRow);

  Border header;
  if (auto style = StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  Grid headerGrid;
  TextBlock title;
  if (auto style = StyleByKey(L"UrPaneTitleStyle")) title.Style(style);
  title.Text(L"NETWORK");
  headerGrid.Children().Append(title);
  // The connect state word, right-aligned in the muted meta voice (d5 §3.1):
  // the page and the status strip then agree about state without the page
  // re-explaining it. StatusStateWord (StatusStripRules.h) is the ONE owner
  // of the string — the strip renders the same call, so this page is a second
  // READER, never a second writer. A state word, not a claim (S1's ruling).
  TextBlock stateMeta;
  if (auto style = StyleByKey(L"UrPaneMetaStyle")) stateMeta.Style(style);
  stateMeta.HorizontalAlignment(HorizontalAlignment::Right);
  stateMeta.Text(winrt::hstring{StatusStateWord(world.connectState)});
  headerGrid.Children().Append(stateMeta);
  header.Child(headerGrid);
  Grid::SetRow(header, 0);
  root.Children().Append(header);

  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.HorizontalScrollMode(ScrollMode::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  Grid::SetRow(scroller, 1);

  StackPanel column;
  column.Orientation(Orientation::Vertical);
  // The content column is capped and left-anchored (polish B1): at 1560 DIP a
  // full-bleed column spreads one fact per row across half a metre of glass.
  // 960 fits the 848 DIP diagram plus the panel's insets; the measure stays
  // fixed and the window's extra width simply stays page — no fluid layout
  // invented.
  column.MaxWidth(960);
  column.HorizontalAlignment(HorizontalAlignment::Left);
  scroller.Content(column);
  root.Children().Append(scroller);

  // ---- section: the relay path hero (d5 §3.2) ------------------------------
  StackPanel pathSection;
  pathSection.Orientation(Orientation::Vertical);

  // The framing header (G4, d5 §3.4): prefix-first honesty, always visible in
  // both modes — it cannot be cropped away from the diagram it frames because
  // it sits directly on top of it. It FLOATS now, the same caption the two
  // groups below use: a ruled sheet strip across the capped column would
  // re-bleed exactly what this wave capped. The STRING is untouched —
  // kRelayPathGroupTitle is still what the `net framing` gate reads.
  AppendCaption(pathSection, winrt::hstring{kRelayPathGroupTitle},
                winrt::to_hstring(static_cast<int>(world.relayPath.size())) + L" nodes",
                /*first=*/true);

  // The diagram sits on the SHEET step (#151515) with a hairline along its
  // bottom edge — the "this is chrome above the page" reading. Square corners
  // (pane vocabulary, not cards), but the same 12 DIP side margin the cards
  // below carry, so every left and right edge in the column lands on one
  // inset. Padding 24,48 gives the legend row its height.
  Border pathPanel;
  pathPanel.Background(urnw::colors::SheetBrush());
  pathPanel.BorderBrush(urnw::colors::BorderBrush());
  pathPanel.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 1));
  pathPanel.Padding(ThicknessHelper::FromLengths(24, 48, 24, 48));
  pathPanel.Margin(ThicknessHelper::FromLengths(12, 0, 12, 10));

  // The diagram is 848 DIP wide and a person can drag the demo window
  // narrower than that (design §6.5a). It gets its OWN horizontal scroller so
  // the PANE's scroller can stay horizontally Disabled — a narrowed demo
  // window is a smaller demo, never a clipped one.
  ScrollViewer pathScroll;
  pathScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
  pathScroll.HorizontalScrollMode(ScrollMode::Enabled);
  pathScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
  pathScroll.VerticalScrollMode(ScrollMode::Disabled);
  pathScroll.Content(MakeRelayPath(world, parts));

  pathPanel.Child(pathScroll);
  pathSection.Children().Append(pathPanel);
  column.Children().Append(pathSection);
  parts->sections.push_back(pathSection);

  // ---- section: the message server (design §6.4) ---------------------------
  // The carded idiom (polish B1): a floating caption over one card that
  // encloses the rows — internal hairlines only, the last row's suppressed by
  // FinalizePaneCard — replacing the full-bleed ruled strip and page-wide
  // hairline rows. Same builders the rail and the Settings page use.
  StackPanel serverSection;
  serverSection.Orientation(Orientation::Vertical);
  AppendCaption(serverSection, L"MESSAGE SERVER", winrt::hstring{world.server.host},
                /*first=*/false);
  auto serverCard = urnw::kit::MakePaneCard();

  // MakePaneKeyValueRow is a FIXED 34 DIP row with a bottom hairline and the
  // pane's 12 DIP inset. Four calls, so the four rows cannot drift from one
  // another.
  serverCard.body.Children().Append(
      urnw::kit::MakePaneKeyValueRow(L"Host", winrt::hstring{world.server.host}).root);
  serverCard.body.Children().Append(
      urnw::kit::MakePaneKeyValueRow(L"Jurisdiction",
                                     winrt::hstring{world.server.jurisdiction}).root);
  // The VALUE comes from FormatLatency, the same function `net fmt latency`
  // asserts, so what the page shows and what the gate checks cannot diverge.
  serverCard.body.Children().Append(
      urnw::kit::MakePaneKeyValueRow(
          L"Latency", winrt::hstring{FormatLatency(world.server.latencyMs)}).root);
  // The key row is the only one whose VALUE is a state rather than a fact, so
  // it is the only one that gets a colour — and the colour rule survives the
  // honesty fix (the d7 audit's N4 override): the prefix-first WORDS carry
  // the framing, so green/danger is the redundant channel, never the message.
  // The bare "Verified" and MessageInspect::cipher render nowhere on this
  // page, under any label.
  auto keyRow = urnw::kit::MakePaneKeyValueRow(
      L"Server key", winrt::hstring{FormatKeyState(world.server.keyVerified)});
  keyRow.value.Foreground(world.server.keyVerified
                              ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                              : urnw::colors::DangerBrush());
  serverCard.body.Children().Append(keyRow.root);
  urnw::kit::FinalizePaneCard(serverCard);
  serverSection.Children().Append(serverCard.root);
  column.Children().Append(serverSection);
  parts->sections.push_back(serverSection);

  // ---- section: your devices (design §6.4) ---------------------------------
  // This IS the connected-clients view the fixture supports (d5 §1): the
  // account's own devices, presence, last-seen, and a remove that removes
  // (design §9.1: nothing looks live and does nothing).
  StackPanel devicesSection;
  devicesSection.Orientation(Orientation::Vertical);
  parts->deviceRemaining = static_cast<int>(world.myDevices.size());
  auto devicesGroup = AppendCaption(devicesSection, L"YOUR DEVICES",
                                    winrt::to_hstring(parts->deviceRemaining),
                                    /*first=*/false);
  parts->deviceCount = devicesGroup.meta;

  auto deviceCard = urnw::kit::MakePaneCard();
  // The rows go DIRECTLY into the card body — parts->deviceList IS that body,
  // not an inner panel — so FinalizePaneCard reaches every row: its last-row
  // hairline clear only sees the card's own children, and the remove handler
  // re-runs it as rows leave.
  parts->deviceList = deviceCard.body;

  // The empty line is the FIRST child, never the last: FinalizePaneCard reads
  // child ORDER, and the last child must always be a real row — a collapsed
  // TextBlock sitting last would keep the last row's hairline alive flush
  // against the card's own edge. A TextBlock is neither Border nor Control,
  // so FinalizePaneCard leaves it alone wherever it sits.
  parts->deviceEmpty = urnw::kit::MakePaneEmptyLine(
      L"No devices are linked to this account.");
  parts->deviceEmpty.Visibility(world.myDevices.empty() ? Visibility::Visible
                                                        : Visibility::Collapsed);
  deviceCard.body.Children().Append(parts->deviceEmpty);

  for (auto const& device : world.myDevices) {
    // The two-line row species: name on top, one TRIMMED line of state under
    // it, so the height is 44 whatever the string is.
    auto row = urnw::kit::MakePaneTwoLineRow(
        winrt::hstring{device.name}, winrt::hstring{FormatDeviceMeta(device)});

    StackPanel trailing;
    trailing.Orientation(Orientation::Horizontal);
    trailing.Spacing(10);
    trailing.VerticalAlignment(VerticalAlignment::Center);

    shapes::Ellipse dot;
    dot.Width(8);
    dot.Height(8);
    dot.VerticalAlignment(VerticalAlignment::Center);
    dot.Fill(device.online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                           : urnw::colors::FaintBrush());
    // Not colour alone: the row's second line already says "Online" or
    // "Last seen ..." in words (fixed contract §6 rule 6).
    MarkRaw(dot);
    trailing.Children().Append(dot);

    Button remove;
    if (auto style = StyleByKey(L"UrPaneActionButtonStyle")) remove.Style(style);
    FontIcon removeGlyph;
    removeGlyph.FontFamily(IconFont());
    removeGlyph.Glyph(L"\uE74D");  // Delete (wastebasket), Segoe Fluent Icons
    removeGlyph.FontSize(14);
    removeGlyph.Foreground(urnw::colors::MutedBrush());
    remove.Content(removeGlyph);
    // A Button whose Content is an element gets NO automatic name — this
    // project has paid for that twice (UrComponents.h:315-318) — so an
    // unnamed row reaches a screen reader as "button" and nothing else.
    Automation::AutomationProperties::SetName(
        remove, winrt::hstring{L"Remove " + device.name});
    // Hover-reveal (d5 §3.6): the wastebasket rests at Opacity 0 and appears
    // on row hover or keyboard focus. OPACITY, never Visibility: a hover-only
    // control must never vanish from a screen reader — it stays in the tab
    // order and the UIA tree at all times, and its automation name never
    // changes.
    remove.Opacity(0.0);
    auto reveal = std::make_shared<RemoveReveal>();
    // Hover is tracked on the ROW (the button is inside it, so entering the
    // button has already entered the row); focus is tracked on the button
    // itself, which is the row's one tab stop.
    row.root.PointerEntered([reveal, remove](auto const&, auto const&) {
      reveal->hover = true;
      SetRemoveRevealed(remove, reveal->hover || reveal->focus);
    });
    row.root.PointerExited([reveal, remove](auto const&, auto const&) {
      reveal->hover = false;
      SetRemoveRevealed(remove, reveal->hover || reveal->focus);
    });
    remove.GotFocus([reveal, remove](auto const&, auto const&) {
      reveal->focus = true;
      SetRemoveRevealed(remove, reveal->hover || reveal->focus);
    });
    remove.LostFocus([reveal, remove](auto const&, auto const&) {
      reveal->focus = false;
      SetRemoveRevealed(remove, reveal->hover || reveal->focus);
    });
    trailing.Children().Append(remove);

    row.trailing.Children().Append(trailing);
    parts->deviceList.Children().Append(row.root);

    // View-local: nothing here mutates the world, so a relaunch restores all
    // three devices. That is right for a demo the owner re-opens — and it is
    // a REAL click, which §9.1 requires of anything that looks live.
    //
    // The handler captures `parts`, and `parts` reaches this button through
    // the panel, so this is a reference cycle. Deliberate and bounded:
    // Registry() already holds every page for the life of the process (one
    // page per window, and the demo has one window).
    auto rowRoot = row.root;
    remove.Click([parts, deviceCard, rowRoot](auto const&, auto const&) {
      uint32_t index = 0;
      if (!parts->deviceList.Children().IndexOf(rowRoot, index)) return;
      parts->deviceList.Children().RemoveAt(index);
      if (0 < parts->deviceRemaining) --parts->deviceRemaining;
      // SetTextOrCollapse, not .Text(): MakePaneGroupHeader set this
      // TextBlock through it, so its Visibility is part of how the field
      // works, and .Text() alone would leave a collapsed "0" invisible.
      urnw::kit::SetTextOrCollapse(parts->deviceCount,
                                   winrt::to_hstring(parts->deviceRemaining));
      // No list Visibility toggle: the rows are REMOVED, so at zero the card
      // holds only the empty line. Toggling deviceList (the card body itself)
      // would hide that line with the rows.
      parts->deviceEmpty.Visibility(parts->deviceRemaining <= 0
                                        ? Visibility::Visible
                                        : Visibility::Collapsed);
      // The removed row may have taken the cleared hairline with it, leaving
      // the NEW last row's line flush against the card's edge — re-run the
      // edge rule (FinalizePaneCard is idempotent by design).
      urnw::kit::FinalizePaneCard(deviceCard);
      urnw::LogInfo("network: device removed, {} left", parts->deviceRemaining);
    });
  }
  urnw::kit::FinalizePaneCard(deviceCard);
  devicesSection.Children().Append(deviceCard.root);
  column.Children().Append(devicesSection);
  parts->sections.push_back(devicesSection);

  parts->root = root;
  Registry().push_back(parts);

  NetworkPageView out;
  out.root = root;
  return out;
}

void SetNetworkPageAdvanced(NetworkPageView& v, bool advanced) {
  auto parts = Find(v.root);
  if (!parts) return;
  for (auto const& label : parts->wireLabels)
    label.Visibility(advanced ? Visibility::Visible : Visibility::Collapsed);
  if (parts->roundTrip)
    parts->roundTrip.Visibility(advanced ? Visibility::Visible : Visibility::Collapsed);
  SetRelayPathAnimated(parts, advanced);
  urnw::LogInfo("network: advanced mode -> {}", advanced);
}

void AnimateNetworkPageEntrance(NetworkPageView& v) {
  auto parts = Find(v.root);
  if (!parts || parts->entranceDone) return;
  // Run ONCE per page (d5 §3.7): the page is built once and cached, and
  // re-navigation must not re-run the stagger or every nav click becomes a
  // light show.
  parts->entranceDone = true;
  if (!urnw::motion::ShouldAnimate()) {
    // With motion off the page is simply there, fully formed (d5 §3.7): no
    // start pose is ever written, so nothing can be left stranded at 0.
    urnw::LogInfo("network: entrance skipped (motion off)");
    return;
  }
  anim::Storyboard board;
  int64_t step = 0;
  for (auto const& section : parts->sections) {
    if (!section) continue;
    // Three sections, kStaggerMs apart — 3 steps is within kMaxStaggerSteps.
    const int64_t begin = step * urnw::motion::kStaggerMs;
    // The start pose is written directly and the animation only carries it
    // home: if the storyboard were dropped, the section still lands at
    // TranslateY 0 / opacity 1 rather than staying offset (UrMotion.cpp's
    // SettleIn states the same rule).
    Media::CompositeTransform transform;
    transform.TranslateY(urnw::motion::kDist8);
    section.RenderTransform(transform);
    section.Opacity(0.0);
    auto rise = urnw::motion::MakeSplineDouble(
        urnw::motion::kDist8, 0.0, urnw::motion::kBaseMs, begin,
        urnw::motion::kStandardP1, urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(rise, section);
    anim::Storyboard::SetTargetProperty(
        rise, L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)");
    board.Children().Append(rise);
    auto fade = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kBaseMs, begin,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(fade, section);
    anim::Storyboard::SetTargetProperty(fade, L"Opacity");
    board.Children().Append(fade);
    ++step;
  }
  board.Begin();
  urnw::LogInfo("network: entrance armed ({} sections, stagger {} ms)", step,
                urnw::motion::kStaggerMs);
}

}  // namespace urmsg::views
