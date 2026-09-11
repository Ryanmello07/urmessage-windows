// SPDX-License-Identifier: MPL-2.0
#include "pch.h"  // /Yu"pch.h": every normal TU includes it FIRST

#include "Views/ThreadView.h"

#include <chrono>
#include <iterator>  // make_move_iterator, for the slide's born-row splice
#include <map>
#include <memory>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Dispatching.h>  // DispatcherQueueTimer, the T8 load beat

#include "Demo/DemoSwitches.h"
#include "Demo/ThreadLayout.h"
#include "Identicon.h"
#include "Log.h"
#include "Strings.h"  // Narrow, for the window instrumentation lines
#include "UrColors.h"
#include "UrComponents.h"  // urnw::kit::StyleByKey
#include "UrMotion.h"
#include "Views/ThreadLayout.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace urmsg::views {
namespace {

namespace demo = urmsg::demo;

// StyleByKey is urnw::kit::StyleByKey (UrComponents.h) and is no longer copied
// here. It used to be: this unit, UrComponents.cpp and Views/InspectRailView.cpp
// each held the same six lines, each with a comment explaining that the others
// were file-local, which is one lookup that could drift three ways. A
// using-declaration rather than a `kit::` prefix at each of the twelve call
// sites below, so the promotion changed the DEFINITION and not the call graph.
//
// Applying styles by KEY rather than by hand is what keeps the bubble in step
// with App.xaml; a missing key must not throw a layout away.
//
// MetricByKey is a different story: it is still file-local to UrComponents.cpp
// and this file does not use it, so nothing here needs it.
using urnw::kit::StyleByKey;

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

// CornerRadiusHelper::FromCorners does not exist in the WinUI 3 projection
// (only FromUniformRadius survived the port), so d2 §1's four corner values
// are written onto the struct directly — TL,TR,BR,BL, the same order the
// corner table in Views/ThreadLayout.cpp speaks.
CornerRadius CornerRadiusFromCorners(double const c[4]) {
  CornerRadius cr;
  cr.TopLeft = c[0];
  cr.TopRight = c[1];
  cr.BottomRight = c[2];
  cr.BottomLeft = c[3];
  return cr;
}

// The icon face, named EVERY time. FontIcon defaults to the older
// "Segoe MDL2 Assets", whose metrics differ and whose coverage is not the same
// set of codepoints, so a glyph picked from the Fluent set can land on a
// different drawing - or on nothing - if the family is left to the default.
Media::FontFamily IconFont() { return Media::FontFamily(L"Segoe Fluent Icons"); }

namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;

// Whether a storyboard begun on `node` right now would PLAY. XAML drops a
// board whose target sits under a Collapsed ancestor: it never ticks and never
// raises Completed (measured on this surface: rows faded in under a still-
// Collapsed scroller never left Opacity 0). Under the from-pose-local rule
// below, writing the from-pose and then beginning a board that cannot play
// would strand the element INVISIBLE, so callers gate both on this. The one
// reachable case in this app: a conversation selected (or autoplay-refreshed,
// or appended to) while the window is below kWideBreakpointDip, where
// ApplyBreakpoint has ThreadHost Collapsed and the list IS the window.
bool ChainVisible(DependencyObject const& node) {
  for (auto cur = node; cur;) {
    auto fe = cur.try_as<FrameworkElement>();
    if (!fe) break;
    if (fe.Visibility() == Visibility::Collapsed) return false;
    cur = fe.Parent();
  }
  return true;
}

// design §7: fade + 10 DIP rise + 0.96 -> 1.0 scale, kBaseMs, standard curve.
// The rationale for putting them in ONE Storyboard is at the Storyboard itself,
// below; it is not repeated here.
//
// EntranceTimelineCount() (Views/ThreadLayout.h) is `.size()` of the very table
// this function iterates, so the count and the timelines CANNOT drift — there is
// no longer a hand-written list to keep in step with a literal. --diagnose
// asserts it INCLUDING the zero: "motion off" means the timelines are never
// created at all, not that they run short.
//
// design d2 §8.1: the 0.96->1.0 scale blooms from the SPEAKER's side —
// origin (0,1) incoming / (1,1) outgoing — rather than the old hard-coded
// (0.5,1). `staggerMs` is design d2 §8.2's open stagger (0 on the append
// path). Neither is in the pure table, stated as a gate gap: the "T6 bubble
// entrance" gate asserts the four timelines' endpoints and the staggered
// beginMs, but it cannot see RenderTransformOrigin — a pure function of one
// bool did not earn a table row. The origin is correct-by-inspection and the
// captures show which side the bloom comes from.
void RunBubbleEntrance(FrameworkElement const& el, bool outgoing, int64_t staggerMs) {
  if (!el) return;

  // ShouldAnimate() is consulted ONCE and its answer goes straight into the
  // pure table, so "is motion on" and "which timelines exist" cannot disagree,
  // and an empty table IS the reduce-motion path.
  //
  // What that does NOT buy, stated because an earlier version of this comment
  // overclaimed it: the `plan.empty()` branch below is still deletable, and
  // deleting it leaves --diagnose printing PASS over a render nobody checked.
  // No pure gate can reach that; only looking at the reduce-motion render can,
  // and this machine reports SPI_GETCLIENTAREAANIMATION = 1, so that path has
  // never executed here. A2's motion override is the first task that can.
  const auto plan = EntranceTimelines(urnw::motion::ShouldAnimate(), staggerMs);
  if (plan.empty()) {
    // Motion GONE, not reduced: the final pose, immediately, and no transform
    // left on the element for a later layout pass to trip over.
    el.Opacity(1.0);
    el.RenderTransform(nullptr);
    return;
  }

  // THE RULE, and it is the opposite of what wave 2 shipped here: the
  // FROM-pose is the LOCAL value, written synchronously in the same turn the
  // row is appended (so no frame can render first), and the storyboard carries
  // from -> to. A timeline does not apply its from-value until it STARTS, so
  // during its BeginTime — the d2 §8.2 open stagger is exactly that — and
  // during the frame a begun storyboard takes to attach, the property renders
  // at its LOCAL value. With the final pose local that window renders the
  // bubble fully-formed and then SNAPS it to the from-pose: the
  // render-first-then-animate bump. EntranceStartPose (ConversationListView.cpp:137)
  // is the same rule, as are UrMotion.cpp's RunCrossfade and SettleIn.
  //
  // The stranded-at-0 hazard wave 2's inversion was defending against is
  // retired by WHERE the Begin now happens rather than by inverting the pose:
  // the initial open no longer runs from the window constructor (it moved to
  // DrainDeepLink, MainWindow.xaml.cpp, which fires from the content root's
  // first SizeChanged — post-layout, on a realized tree), and the append path
  // is a live tree by definition. "Realized" is not quite "can play", though:
  // below kWideBreakpointDip ThreadHost is Collapsed, and a board begun there
  // never ticks (see ChainVisible) — that one case lands at the final pose
  // with no board, the same landing Completed writes.
  if (!ChainVisible(el)) {
    el.Opacity(1.0);
    el.RenderTransform(nullptr);
    return;
  }
  Media::CompositeTransform t;
  t.TranslateY(kBubbleRiseDip);
  t.ScaleX(kBubbleFromScale);
  t.ScaleY(kBubbleFromScale);
  el.RenderTransform(t);
  // The bubble grows from where it will end up, and from its speaker's side.
  el.RenderTransformOrigin(
      winrt::Windows::Foundation::Point{outgoing ? 1.0f : 0.0f, 1.0f});
  el.Opacity(0.0);

  // ONE Storyboard for all four so they finish on the same frame — two
  // independent storyboards can land a frame apart, which reads as a hitch
  // exactly when the bubble settles (the same reason CrossfadePageSwap shares
  // one storyboard, UrMotion.cpp).
  anim::Storyboard sb;
  for (auto const& spec : plan) {
    auto a = urnw::motion::MakeSplineDouble(spec.from, spec.to, urnw::motion::kBaseMs,
                                            spec.beginMs, urnw::motion::kStandardP1,
                                            urnw::motion::kStandardP2);
    if (spec.autoReverse) a.AutoReverse(true);
    if (spec.forever) a.RepeatBehavior(anim::RepeatBehaviorHelper::Forever());
    anim::Storyboard::SetTarget(a, el);
    anim::Storyboard::SetTargetProperty(a, spec.path);
    sb.Children().Append(a);
  }
  sb.Completed([el, weakSb = winrt::make_weak(sb)](auto const&, auto const&) {
    // The landing: FINAL pose as local values, then Stop, so a completed
    // board's HoldEnd keeps owning none of the four properties — the bubble
    // rests exactly where the reduce-motion branch puts it. The board is
    // captured WEAK: a strong capture would cycle board -> delegate -> board.
    el.Opacity(1.0);
    el.RenderTransform(nullptr);
    if (auto board = weakSb.get()) board.Stop();
  });
  sb.Begin();
}

// The selection outline's home inside UrBubbleButtonStyle's template
// (App.xaml): a 2px UrAccentBrush layer at Opacity 0. FindName from the Button
// would not reach the template's own namescope from code, so the walk compares
// names directly; the tree here is three levels deep, so the recursion is
// bounded by the template, not by the page.
FrameworkElement FindTemplateChild(DependencyObject const& parent, wchar_t const* name) {
  const int count = Media::VisualTreeHelper::GetChildrenCount(parent);
  for (int i = 0; i < count; ++i) {
    auto child = Media::VisualTreeHelper::GetChild(parent, i);
    if (auto fe = child.try_as<FrameworkElement>())
      if (fe.Name() == name) return fe;
    if (auto found = FindTemplateChild(child, name)) return found;
  }
  return nullptr;
}

// THE ONE WRITER of a bubble's edge — resting AND selected, because they are
// the same two properties and a property with two writers is a property that
// can half-change. MakeBubbleRow calls it with selected=false when it builds a
// bubble; SetThreadSelectedMessage calls it for every bubble on every selection
// change. There is no third caller and there must not be one.
//
// The RESTING edge (Spec C §5.2): a 1px UrBorderBrush edge on an outgoing
// bubble, none on an incoming one. "None" is still thickness 1 with a
// Transparent brush — the style's own rule (App.xaml:750-752) — so the two
// directions have identical inner metrics (design d2 §2.2).
//
// SELECTION no longer touches that edge at all. It draws the template's
// SelectEdge layer — the same 2px UrAccentBrush outline as before, on its own
// layer (design d1 §2.3ii, motion M1) — on over kMicroMs, and removes it
// instantly on deselect (exits one step faster; kMicroMs is the floor, so
// removal is immediate). Because the resting metrics never change, selecting a
// bubble cannot shift its content in either direction.
void SetBubbleEdge(Button const& bubble, bool outgoing, bool selected) {
  if (!bubble) return;
  // Rest first, on every call, so a later branch can never leave a stale
  // selection treatment behind.
  bubble.BorderThickness(ThicknessHelper::FromUniformLength(1.0));
  // A local rather than a nested ternary: two of the arms would be a
  // SolidColorBrush and a nullptr, and letting the compiler pick a common type
  // for those is how a null edge quietly becomes a transparent one.
  Media::Brush edge{nullptr};
  if (outgoing) {
    edge = BrushByKey(L"UrBorderBrush", urnw::colors::kBorder);
  } else {
    edge = urnw::colors::MakeBrush({0, 0, 0, 0});  // Transparent
  }
  bubble.BorderBrush(edge);

  // The template is applied on first measure; a property-write selection (the
  // --demo=inspect deep link) can land before that, so force it. A no-op once
  // applied. UrBubbleButtonStyle template-binds BorderBrush and BorderThickness
  // onto its template root, which is what makes the rest edge above paint.
  if (selected) bubble.ApplyTemplate();
  const FrameworkElement layer = FindTemplateChild(bubble, L"SelectEdge");

  if (!selected) {
    // Deselect removes instantly. A draw-on still in flight is stopped FIRST:
    // a running storyboard's value beats the local write, and without the stop
    // the outline would linger for the remainder of its kMicroMs.
    if (layer) {
      if (auto sb = layer.Tag().try_as<anim::Storyboard>()) sb.Stop();
      layer.Tag(nullptr);
      layer.Opacity(0.0);
    }
    return;
  }
  if (!layer) {
    // If the template ever stops carrying the layer (a style drift), selection
    // must still draw SOMETHING: the pre-layer rendering, a 2px accent write on
    // the rest edge itself.
    bubble.BorderThickness(ThicknessHelper::FromUniformLength(2.0));
    bubble.BorderBrush(urnw::colors::AccentBrush());
    return;
  }
  // Already drawn (a held animation or the instant write below both leave
  // Opacity at 1): re-writing the SAME selection is not a new selection and
  // must not restart the draw-on.
  if (layer.Opacity() >= 1.0) return;
  if (auto sb = layer.Tag().try_as<anim::Storyboard>()) sb.Stop();
  layer.Tag(nullptr);
  if (!urnw::motion::ShouldAnimate()) {
    // Motion GONE, not shortened: the edge appears instantly, exactly as it did
    // before it had a layer. This branch has never executed on this machine
    // (SPI_GETCLIENTAREAANIMATION = 1) — unverified beyond code inspection.
    layer.Opacity(1.0);
    return;
  }
  auto a = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kMicroMs, 0,
                                          urnw::motion::kStandardP1,
                                          urnw::motion::kStandardP2);
  anim::Storyboard::SetTarget(a, layer);
  anim::Storyboard::SetTargetProperty(a, L"Opacity");
  anim::Storyboard sb;
  sb.Children().Append(a);
  // Remembered on the layer itself so the next call can stop a draw-on still
  // in flight; dropped by the deselect/re-select branches above.
  layer.Tag(sb);
  sb.Begin();
}

// THE delivery cluster. design §6.2: right-aligned, under the last bubble that
// CarriesDeliveryGlyph() marks — which is the last of an outgoing run, PLUS any
// Failed row wherever it sits.
//
// THIS IS THE ONLY ELEMENT ON A ROW THAT DRAWS A DELIVERY INDICATION. It
// replaces MakeDeliveryLine (T2), which drew a 12px glyph and printed a word
// only for Failed. It is a SIBLING of the bubble Button inside BubbleRow::root,
// never a child of the Button: a delivery reading is about the message rather
// than part of it, and putting it inside would make it part of the bubble's
// fill and of its click target. If a second drawer is ever added anywhere, that
// is the bug — two readings of one state is worse than none.
//
// Nothing here is carried by colour alone. Sent -> Delivered is a COUNT change
// (one ring, two rings); Delivered -> Read is a SHAPE change (rings to discs);
// and every state prints its own word, which is what survives a greyscale
// screenshot. BadgeFor() (Views/ThreadLayout.h) holds the table, so --diagnose
// asserts the three channels instead of a screenshot having to be believed.
FrameworkElement MakeDeliveryCluster(demo::MessageRow const& row) {
  const DeliveryBadge badge = BadgeFor(row.state);
  Media::Brush brush = badge.danger ? urnw::colors::DangerBrush()
                                    : (badge.solid ? urnw::colors::TextBrush()
                                                   : urnw::colors::MutedBrush());

  StackPanel cluster;
  cluster.Orientation(Orientation::Horizontal);
  cluster.Spacing(4);
  cluster.HorizontalAlignment(HorizontalAlignment::Right);
  // design d2 §6: (0,1,2,4) binds the cluster to ITS bubble — 1 dip above,
  // the wider gap below left to §1's row margins — so ownership is
  // unambiguous (it used to float near-equidistant between its bubble and the
  // next run at (0,2,2,6)).
  cluster.Margin(ThicknessHelper::FromLengths(0, 1, 2, 4));

  StackPanel glyphs;
  glyphs.Orientation(Orientation::Horizontal);
  // Spacing 0: a 13px Segoe Fluent icon's own advance already puts the two
  // rings ~13 DIP apart, which is where the pair reads as two. Overlapping them
  // turns it into a blot — measured at 8 DIP before this was settled.
  glyphs.Spacing(0);
  glyphs.VerticalAlignment(VerticalAlignment::Center);
  for (int i = 0; i < badge.repeat; ++i) {
    FontIcon g;
    g.FontFamily(IconFont());
    g.Glyph(winrt::hstring{badge.glyph});
    g.FontSize(13);
    g.Foreground(brush);
    // The bubble's automation name already carries the delivery WORD
    // (BubbleAutomationName), so announcing the glyph as well would put a second
    // item beside the thing it describes — and a repeat of 2 would say it twice.
    MarkRaw(g);
    glyphs.Children().Append(g);
  }
  cluster.Children().Append(glyphs);

  TextBlock word;
  word.Text(winrt::hstring{badge.word});
  // UrCaptionTextStyle first, for the reason MakeSystemLine gives: the style is
  // what pins the BODY face to this line from the app's own resources. Size and
  // colour are then set explicitly, so a missing resource key cannot silently
  // resize or recolour it.
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) word.Style(st);
  word.FontSize(11);
  word.Foreground(brush);
  word.VerticalAlignment(VerticalAlignment::Center);
  // Raw for the same reason as the glyph: the bubble's own name already says
  // "Read" / "Not sent". This word is the SIGHTED reader's non-colour channel.
  MarkRaw(word);
  cluster.Children().Append(word);

  if (row.state != demo::DeliveryState::Failed) return cluster;

  // Spec C §5.3: failed carries a Reason and a retry. Both render INLINE rather
  // than behind a tap — the sheet the product would open is not built (design
  // §2), and a tap target that opens nothing is exactly what design §9.1 bans.
  StackPanel column;
  column.HorizontalAlignment(HorizontalAlignment::Right);
  column.Spacing(3);
  // The §6 margin change applied to the Failed cluster's outer element, the
  // same (0,1,2,4) the plain cluster carries; the reason line and the
  // disabled [ Try again ] below are otherwise exactly as they were (d2 §6.3
  // — this row is the surface's most important honesty artifact).
  column.Margin(ThicknessHelper::FromLengths(0, 1, 2, 4));
  cluster.Margin(ThicknessHelper::FromUniformLength(0));
  column.Children().Append(cluster);

  TextBlock reason;
  reason.Text(winrt::hstring{row.failureReason});
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) reason.Style(st);
  reason.FontSize(11);
  reason.Foreground(urnw::colors::DangerBrush());
  reason.TextWrapping(TextWrapping::Wrap);
  reason.TextAlignment(TextAlignment::Right);
  reason.MaxWidth(320);
  // BubbleAutomationName already appends ": <failureReason>" to the bubble's own
  // name, so leaving this line in the tree would announce the reason twice.
  MarkRaw(reason);
  column.Children().Append(reason);

  // Retry would have to mutate the world, and CONTRACT-V2 §1 gives ambient
  // activity the only key to MutableWorld(). So it cannot act, and per design
  // §9.1 it is disabled rather than live-but-dead. The composer's one honest
  // line (T6) is where the demo says why, once, instead of five times.
  Button retry;
  retry.Content(winrt::box_value(winrt::hstring{L"Try again"}));
  retry.HorizontalAlignment(HorizontalAlignment::Right);
  retry.FontSize(11);
  retry.Padding(ThicknessHelper::FromLengths(10, 2, 10, 2));
  retry.MinHeight(24);
  retry.IsEnabled(false);
  Automation::AutomationProperties::SetName(
      retry, winrt::hstring{L"Try again: resend this message (not available in the demo)"});
  column.Children().Append(retry);
  return column;
}

// ---- the cluster as a thing that can be TAKEN AWAY AGAIN (T6) ------------
// SetThreadConversation builds a whole column at once and every row's `next` is
// known, so until T6 a cluster was only ever created. AppendThreadRow changes
// that: the row that WAS newest stops being last-of-run the moment another
// outgoing row lands under it, and design §6.2's one-reading-per-run then
// requires its cluster to GO. So the element needs an identity the remover can
// find.
//
// A TAG, not an index. MakeBubbleRow happens to append the cluster as
// BubbleRow::root's second child today; a later task that adds a third child
// would make an index-based remove delete the wrong element, silently and only
// on the append path, which is the path nobody screenshots.
constexpr wchar_t kDeliveryClusterTag[] = L"ur-delivery-cluster";

FrameworkElement MakeTaggedDeliveryCluster(demo::MessageRow const& row) {
  auto el = MakeDeliveryCluster(row);
  if (el) el.Tag(winrt::box_value(winrt::hstring{kDeliveryClusterTag}));
  return el;
}

// -1 when this row draws no cluster.
int DeliveryClusterIndex(Controls::Panel const& rowRoot) {
  if (!rowRoot) return -1;
  auto kids = rowRoot.Children();
  for (uint32_t i = 0; i < kids.Size(); ++i) {
    auto fe = kids.GetAt(i).try_as<FrameworkElement>();
    if (!fe) continue;
    if (winrt::unbox_value_or<winrt::hstring>(fe.Tag(), winrt::hstring{}) ==
        winrt::hstring{kDeliveryClusterTag})
      return static_cast<int>(i);
  }
  return -1;
}

// Make this row's cluster match `carries`, whatever it was before. Idempotent
// in both directions, so the caller may hand it the answer unconditionally.
//
// The ADD branch is unreachable from the append path today and is kept on
// purpose — AppendClusterPlan::prevMustGain carries the one-line proof of why
// (Views/ThreadLayout.h), and `T6 append cluster` asserts the zero rather than
// printing it. This stays a total "make it match" rather than a one-way clear
// so it is still correct the day CarriesDeliveryGlyph grows a clause that can
// turn a reading ON.
//
// design d2 §6.2: an APPEARING cluster fades in over kFastMs on the standard
// curve, gated by ShouldAnimate — the honest subset of design §7's "delivery
// morph" (a true Sent->Delivered morph never occurs in this build: ambient
// activity only appends, and nothing mutates a rendered row's state).
// Removal stays instant: the only removal event is a newer outgoing row
// arriving below, whose own bubble entrance is where the eye already is, and
// an async fade-out would make this gate-treated-synchronous function
// stateful for no visible gain. The FROM-pose is the local value (Opacity 0,
// written before Begin) and the Completed handler lands 1.0 — the same
// from-pose-local rule RunBubbleEntrance follows, so the frame between the
// append and the timeline's start shows the cluster invisible, never
// fully-drawn-then-vanishing.
void SetRowCluster(FrameworkElement const& rowRoot, demo::MessageRow const& row,
                   bool carries) {
  if (!rowRoot) return;
  auto panel = rowRoot.try_as<Controls::Panel>();
  if (!panel) return;
  const int at = DeliveryClusterIndex(panel);
  if (carries == (0 <= at)) return;  // already right
  if (carries) {
    auto cluster = MakeTaggedDeliveryCluster(row);
    panel.Children().Append(cluster);
    if (urnw::motion::ShouldAnimate() && cluster && ChainVisible(cluster)) {
      // ChainVisible: a board begun under a Collapsed ancestor (a narrow
      // window's ThreadHost) never plays, so that case keeps the cluster at
      // its built opacity 1 — final pose, no board, same as the landing.
      cluster.Opacity(0.0);
      anim::Storyboard sb;
      auto a = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kFastMs, 0,
                                              urnw::motion::kStandardP1,
                                              urnw::motion::kStandardP2);
      anim::Storyboard::SetTarget(a, cluster);
      anim::Storyboard::SetTargetProperty(a, L"Opacity");
      sb.Children().Append(a);
      sb.Completed([cluster, weakSb = winrt::make_weak(sb)](auto const&, auto const&) {
        cluster.Opacity(1.0);
        if (auto board = weakSb.get()) board.Stop();
      });
      sb.Begin();
    }
  } else {
    panel.Children().RemoveAt(static_cast<uint32_t>(at));
  }
}

}  // namespace

BubbleRow MakeBubbleRow(demo::MessageRow const& row, bool group, bool showSenderHeader,
                        bool carriesDeliveryGlyph, BubbleRunPos runPos) {
  BubbleRow out;
  out.bubble.id = row.id;

  // ---- the bubble ---------------------------------------------------------
  Button bubble;
  if (auto style = StyleByKey(L"UrBubbleButtonStyle")) bubble.Style(style);

  // The run-shape corners (design d2 §1): a local CornerRadius write, which
  // beats the style's uniform-12 setter and flows through the template's
  // {TemplateBinding CornerRadius} to Root, EdgeLayer AND SelectEdge — so the
  // hover edge and the selection outline track the asymmetric corners with no
  // template change. This, and the append-time re-plan in AppendThreadRow,
  // are the only two writers of a bubble's corners.
  double corners[4];
  BubbleCornerDip(runPos, row.outgoing, corners);
  bubble.CornerRadius(CornerRadiusFromCorners(corners));

  // Design §6.2: incoming UrCardBrush #1C1C1C left, outgoing UrCardHoverBrush
  // #242424 with a 1px UrBorderBrush right. UrAccentBrush is NEVER a bubble
  // fill — it is the send button and the selection outline only.
  bubble.Background(row.outgoing ? BrushByKey(L"UrCardHoverBrush", urnw::colors::kCardHover)
                                 : BrushByKey(L"UrCardBrush", urnw::colors::kCard));
  // The edge goes through the ONE writer, at rest: a bubble that has just been
  // built is not selected. SetThreadSelectedMessage rewrites it from there.
  SetBubbleEdge(bubble, row.outgoing, /*selected=*/false);
  bubble.HorizontalAlignment(row.outgoing ? HorizontalAlignment::Right
                                          : HorizontalAlignment::Left);
  // The cap now; the thread column narrows it on SizeChanged (the column task
  // owns that walk). Never 0 here — a bubble built before the column has been
  // measured must still be a bubble.
  bubble.MaxWidth(BubbleMaxWidthDip(0.0));

  StackPanel column;
  column.Spacing(2);

  // The sender name is NOT here: it moved out of the bubble to a line above
  // the run-start bubble (design d2 §1, below at rowRoot), so every bubble
  // interior is uniformly body + time.

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
  // Belt and braces: ButtonBase already marks the pointer events handled, so a
  // click on a bubble does not generate a Tapped for the column behind it.
  // Saying so explicitly means the deselect handler in MakeThread cannot start
  // firing on bubble clicks because of a style change three months from now -
  // which would select and immediately deselect, and read as "nothing happened".
  bubble.Tapped([](auto const&, auto const& args) { args.Handled(true); });
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
    // MakeIdenticon applies its OWN corner radius — do not set one here.
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
  // The §1 rhythm, replacing MakeThread's old uniform stack.Spacing(6): 10 dip
  // above a run start/single, 2 above a continuation, so runs read as blocks.
  rowRoot.Margin(ThicknessHelper::FromLengths(0, GapAboveDip(runPos), 0, 0));

  if (showSenderHeader && !row.senderName.empty()) {
    // The sender name, OUT of the bubble and above it (design d2 §1), aligned
    // with the bubble's TEXT: the 36 dip gutter plus the bubble's own 12 dip
    // padding (UrBubbleButtonStyle's Padding 12,8 — if that ever changes, this
    // alignment goes with it). Group-only by way of showSenderHeader, which
    // delegates to ShowsSenderHeader — DMs never draw it. 11px muted caption,
    // the same voice the cluster's word speaks. MarkRaw as it was inside the
    // bubble: BubbleAutomationName reads no visual tree, so automation is
    // unchanged by the move.
    TextBlock name;
    name.Text(winrt::hstring{row.senderName});
    if (auto s = StyleByKey(L"UrCaptionTextStyle")) name.Style(s);
    name.FontSize(11);
    name.Foreground(urnw::colors::MutedBrush());
    name.TextTrimming(TextTrimming::CharacterEllipsis);
    name.Margin(ThicknessHelper::FromLengths(kThreadGutterDip + 12.0, 0, 0, 2));
    MarkRaw(name);
    rowRoot.Children().Append(name);
  }
  rowRoot.Children().Append(gutterRow);
  // ONE cluster, or none. The CALLER decides, with CarriesDeliveryGlyph()
  // (Demo/ThreadLayout.h) - never with ThreadRowPlan::endsOutgoingRun, which is
  // direction-only and is FALSE on the shipped world's mid-run Failed row.
  //
  // TAGGED, because "or none" is not only decided once: AppendThreadRow re-asks
  // the same question of the row above the one it is adding, and a row that has
  // stopped being last-of-run has to LOSE this element again. SetRowCluster is
  // the remover and it finds the cluster by that tag.
  if (carriesDeliveryGlyph) rowRoot.Children().Append(MakeTaggedDeliveryCluster(row));
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
  std::vector<ThreadBubble> bubbles;  // the canonical list; ThreadView::bubbles mirrors it
  double columnWidth = 0.0;

  // ---- T6: what AppendThreadRow needs and ThreadBubble cannot carry -------
  // ThreadBubble is fixed by contract §4 (an id and a Button) and ThreadView
  // holds no MessageRows, but appending a row is a decision ABOUT THE ROW
  // ABOVE IT: CarriesDeliveryGlyph() takes a row and its successor, so the
  // previous row's DATA has to still be here to be re-asked the question, and
  // the element that owns its cluster has to be reachable to answer it.
  //
  // EVERY RENDERED row is recorded, not only the message rows — a day
  // separator or a system row is a legitimate `prev`, and
  // CarriesDeliveryGlyph returns false for one, which is exactly the answer
  // that KEEPS an outgoing row's cluster when a system line lands under it.
  // clusterHost is null for those. With the T8 window live, "rendered" means
  // rows[0] is world row window.start — the window's slice, not the whole
  // conversation.
  struct RenderedRow {
    demo::MessageRow row;
    FrameworkElement clusterHost{nullptr};  // BubbleRow::root; null off a message row
    // Whether the row occupies a stack child at all — the unlabelled day
    // separator draws NOTHING, and the T8 trims map rows to children by it.
    bool drawn = true;
  };
  std::vector<RenderedRow> rows;
  bool group = false;

  // The typing indicator (design §7). typingStory is stopped and dropped on
  // every state change so two waves can never run over one another.
  // typingRowStory is the row's own one-shot entrance (design d1 motion M3) —
  // a different storyboard on different properties, so the two cannot fight.
  FrameworkElement typingRow{nullptr};
  std::vector<winrt::Microsoft::UI::Xaml::Shapes::Ellipse> typingDots;
  anim::Storyboard typingStory{nullptr};
  anim::Storyboard typingRowStory{nullptr};

  // The thread pane's 40dip L1 header (design d1 §1.4): the conversation name
  // in the pane-title voice plus a right-aligned muted meta, both re-pointed
  // by SetThreadConversation (the one writer).
  TextBlock headerTitle{nullptr};
  TextBlock headerMeta{nullptr};
  // The no-selection empty state (one centred muted line under the identicon
  // lattice), built Visible and collapsed by the first SetThreadConversation.
  FrameworkElement emptyState{nullptr};

  // False until the first bottom pin has actually landed. The stack.SizeChanged
  // pin below asks ShouldPinToBottom, which pins unconditionally while unarmed
  // (the first real extent is indistinguishable from "scrolled to the top")
  // and guards by distance to the foot after that (design 9.2's do-not-yank).
  bool pinArmed = false;
  // The scrollable extent the LAST pin decision was made against. The handler
  // measures the reader's offset against THIS, not against
  // ScrollableHeight() at fire time: an arriving row, a rewrap or a font
  // metric swap GROWS the extent under a stationary offset, and "at the foot
  // when the last decision was made" must not read as "scrolled away" —
  // measuring against the live extent opened the thread ~50dip above the foot
  // at launch, with the newest row half-cut (caught in a capture). Updated
  // when a pin lands, so a scrolled-away reader's slack can only grow — and by
  // the T8 window slides, which change WHICH rows the extent covers: after a
  // slide, "within 48 dip of the foot" must be asked of the loaded extent that
  // exists NOW, or a reader who travelled home stops being pinned on the next
  // ambient append (a partial top chunk shrinks the extent ~76 rows below the
  // stale pinExtent, and scrollable - offset never again fits in 48). A slide
  // update cannot misread a scrolled-away reader: their offset sits
  // mid-extent, so scrollable - offset stays far past 48 either way.
  double pinExtent = 0.0;

  // ---- T8: the sliding window over the backlog ------------------------------
  // The window is a VIEW concern over the full world plan (the pure half lives
  // in Views/ThreadLayout.h). `conv` re-points at the world's conversation on
  // every SetThreadConversation: the World is a function-static that outlives
  // every view, and MutableWorld() only ever appends ROWS, so the conversation
  // object itself never moves and slides re-read conv->rows FRESH instead of
  // caching row pointers across appends (a rows.push_back can reallocate).
  demo::Conversation const* conv = nullptr;
  std::wstring convId;  // for the same-conversation refresh placement rule
  ThreadWindow window{};
  bool windowBusy = false;  // a load beat (marker + chunk fade) is in flight

  // The "loading earlier" marker: a slim overlay pill at the scroller's top
  // edge (NOT a stack row — it must not touch the extent, or its arrival would
  // be the very jump the prepend rule forbids). Shown only while a chunk
  // materializes, kFastMs in/out; never exists with motion off.
  FrameworkElement earlierMarker{nullptr};
  anim::Storyboard earlierMarkerStory{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer earlierTimer{nullptr};

  // The one-unit chunk fade. `fadingRows` are mid-fade rows with Opacity 0 as
  // their LOCAL from-pose: if a rebuild cancels the beat, CancelEarlierLoad
  // writes their final pose by hand before stopping the board (a Stop leaves
  // locals alone, which is the stranded-at-0 hazard the from-pose rule exists
  // to avoid).
  anim::Storyboard chunkFadeStory{nullptr};
  std::vector<FrameworkElement> fadingRows;

  // ---- T9: progressive hydration of the open window -------------------------
  // SetThreadConversation renders only the viewport-covering INITIAL SET
  // synchronously; the rest of the initial window materializes in background
  // beats at Low dispatcher priority. hydrateTarget is the window.start the
  // fill walks down to (the initial window's start — a row INDEX, so it names
  // the same row for the fill's whole life: the world grows at the foot
  // only). window.start is the fill's cursor — the ONE residency truth the
  // beats share with the scroll-triggered slides (Views/ThreadLayout.h).
  std::size_t hydrateTarget = 0;
  // Bumped per SetThreadConversation; every queued beat captures it and a
  // stale capture dies unrescheduled (PlanHydrateBeat) — the switch-away
  // guard, so an old conversation's fill can never run under the new one.
  std::uint64_t hydrateGeneration = 0;
  bool hydrateQueued = false;  // one beat queued on the dispatcher, at most
  // Instrumentation for the "hydrate complete" line: when the fill started
  // (== the sync phase's end) and how many beats it took.
  std::chrono::steady_clock::time_point hydrateStart{};
  int hydrateBeats = 0;

  // The selection MainWindow last wrote, so a row the window re-covers can be
  // re-outlined on the way back (SetThreadSelectedMessage is the one writer).
  std::wstring selectedId;

  // Every public setter takes ThreadView& and re-points this at the CALLER's
  // instance. The ViewChanged/slide lambdas outlive MakeThread's local v
  // (returned by value and moved into MainWindow's member), so they cannot
  // capture it — but they must keep ThreadView::bubbles (fixed contract §4) in
  // sync with the window for SetThreadSelectedMessage to keep finding rows.
  // MainWindow assigns thread_ once and never copies it afterwards, so the
  // pointer is stable from the first SetThreadConversation; nothing
  // dereferences it before then (no conversation, no scroller events).
  ThreadView* owner = nullptr;
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

// The T8 trigger (defined with the rest of the window machinery below
// MakeThread; the scroller's ViewChanged in MakeThread already needs the
// name).
void MaybeSlideWindow(std::shared_ptr<ThreadParts> const& parts);

}  // namespace

// Pure by construction: no winrt, no element tree, three numbers in and one
// decision out - which is what lets Startup.cpp's `demo scroll pin` line walk
// all four cases from CollectDiagnostics() before winrt::init_apartment,
// even though the scroll event itself cannot be synthesised. ThreadView.h
// carries the rule and the 48-dip reason.
bool ShouldPinToBottom(bool armed, double offset, double scrollableHeight) {
  return !armed || scrollableHeight - offset <= 48.0;
}

namespace {

void ApplyColumnWidth(std::shared_ptr<ThreadParts> const& parts) {
  if (!parts) return;
  const double cap = BubbleMaxWidthDip(parts->columnWidth - kThreadPadDip * 2.0);
  for (auto const& b : parts->bubbles)
    if (b.root) b.root.MaxWidth(cap);
}

// parts->bubbles is the canonical bubble list; ThreadView::bubbles (fixed
// contract §4) is its mirror, refreshed here after every mutation — the T8
// slides included, which is the whole reason the canonical list lives in
// parts: a ViewChanged lambda cannot reach MainWindow's ThreadView. A full
// assign, not a diff: <= 500 entries of id+Button is noise next to the row
// build that just ran, and two lists walked in parallel are how a trim drops
// from one and not the other.
void SyncPublicBubbles(std::shared_ptr<ThreadParts> const& parts) {
  if (parts->owner) parts->owner->bubbles = parts->bubbles;
}

// The day separator: a centred pill, not a rule with text on it. 11px
// letterspaced muted is UrGroupHeaderTextStyle - the same voice every group
// header in the app already speaks, so the thread does not grow a caption
// species of its own.
//
// design d2 §4: NO border (a bordered pill competes with bubble edges; the
// card fill alone lifts it off the #101010 page), radius 8 rather than 10,
// and the §1 rhythm owns its margins ((0,16,0,8)) now that stack.Spacing is 0.
FrameworkElement MakeDaySeparator(std::wstring const& label) {
  Border pill;
  pill.Background(BrushByKey(L"UrCardBrush", urnw::colors::kCard));
  pill.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
  pill.Padding(ThicknessHelper::FromLengths(12, 3, 12, 4));
  pill.HorizontalAlignment(HorizontalAlignment::Center);
  pill.Margin(ThicknessHelper::FromLengths(0, 16, 0, 8));

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
  // MakeDeliveryCluster uses, so a missing resource key cannot silently resize
  // or recolour the line.
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) line.Style(st);
  line.FontSize(12);
  line.Foreground(urnw::colors::MutedBrush());
  line.TextWrapping(TextWrapping::Wrap);
  line.TextAlignment(TextAlignment::Center);
  line.HorizontalAlignment(HorizontalAlignment::Center);
  line.MaxWidth(420);
  // (0,8,0,8) — the §1 row-margin rhythm owns the gaps now that stack.Spacing
  // is 0 (design d2 §4; nothing else about the line changes — restraint is
  // the design there, and 12px muted stays for legibility).
  line.Margin(ThicknessHelper::FromLengths(0, 8, 0, 8));
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
//
// design d2 §7 adds the chrome header: PERMANENT RECORD in
// UrGroupHeaderTextStyle (the app's chrome voice, DangerBrush). It labels
// PERSISTENCE — the record's actual property, mirrored from the automation
// name below — and makes no claim about crypto.
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

  TextBlock mark;
  mark.Text(L"PERMANENT RECORD");
  if (auto st = StyleByKey(L"UrGroupHeaderTextStyle")) mark.Style(st);
  mark.Foreground(urnw::colors::DangerBrush());
  // The ROOT below already announces "Permanent record, cannot be dismissed.";
  // leaving this line in the tree would say it twice.
  MarkRaw(mark);
  column.Children().Append(mark);

  StackPanel head;
  head.Orientation(Orientation::Horizontal);
  head.Spacing(8);

  // The key glyph seated in its own chip (design d2 §7's flourish): a 24x24,
  // radius-6 tile of danger at 10% alpha — WithAlpha over the existing
  // kDanger, no new colour token — so the record's glyph reads as an emblem
  // rather than as text. A full danger border was rejected there: one red
  // rule is a record; a red box is an alert banner.
  Border keyChip;
  keyChip.Width(24);
  keyChip.Height(24);
  keyChip.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
  keyChip.Background(
      urnw::colors::MakeBrush(urnw::colors::WithAlpha(urnw::colors::kDanger, 0x1A)));
  keyChip.VerticalAlignment(VerticalAlignment::Top);

  FontIcon key;
  key.FontFamily(IconFont());
  key.Glyph(L"\uE192");  // Segoe Fluent "Permissions" - the key glyph
  key.FontSize(14);
  key.Foreground(urnw::colors::DangerBrush());
  key.HorizontalAlignment(HorizontalAlignment::Center);
  key.VerticalAlignment(VerticalAlignment::Center);
  // decoration beside a line that already carries the words
  MarkRaw(key);
  keyChip.Child(key);
  head.Children().Append(keyChip);

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

// ---- the typing indicator (T6, design §7) --------------------------------
// Three pulsing dots AND a word. The word is not decoration: with "Show
// animations in Windows" off the dots do not move at all, and three motionless
// grey dots say nothing — this line is what carries the state in that case, and
// it is also the only thing a screen reader gets (the dots are Raw).
//
// It says "Typing", which is the one thing this demo can honestly claim about a
// fabricated participant. Nothing here says a message was encrypted, sent or
// received: there is no protocol and no crypto behind this window.
FrameworkElement MakeTypingIndicator(std::shared_ptr<ThreadParts> const& parts) {
  StackPanel row;
  row.Orientation(Orientation::Horizontal);
  row.VerticalAlignment(VerticalAlignment::Center);
  // The left edge is the incoming bubble column: the thread pad, plus the
  // identicon gutter in a group (design d2 §5). This is only the value before
  // any conversation is known — SetThreadConversation owns the real one,
  // because `group` is first known there.
  row.Margin(ThicknessHelper::FromLengths(kThreadPadDip, 0, 0, 6));
  row.Visibility(Visibility::Collapsed);

  // design d2 §5: the indicator reads as a message MATERIALIZING, so the dots
  // and the word sit in an incoming-shaped shell — the card fill and the
  // incoming run-start corner (12,12,12,4). The corner comes from the same
  // BubbleCornerDip table the bubbles draw with (never a hand-copied
  // {12,12,12,4}), so the shell and a real incoming bubble cannot drift apart.
  // The shell gets NO identicon: the fixture has no typing-sender field and is
  // immutable, so the indicator stays anonymous by honesty, not by omission.
  Border shell;
  shell.Background(BrushByKey(L"UrCardBrush", urnw::colors::kCard));
  double corners[4];
  BubbleCornerDip(BubbleRunPos::First, /*outgoing=*/false, corners);
  shell.CornerRadius(CornerRadiusFromCorners(corners));
  shell.Padding(ThicknessHelper::FromLengths(10, 7, 10, 7));

  StackPanel content;
  content.Orientation(Orientation::Horizontal);
  content.Spacing(6);
  content.VerticalAlignment(VerticalAlignment::Center);

  StackPanel dots;
  dots.Orientation(Orientation::Horizontal);
  dots.Spacing(5);
  dots.VerticalAlignment(VerticalAlignment::Center);
  parts->typingDots.clear();
  for (int i = 0; i < kTypingDots; ++i) {
    winrt::Microsoft::UI::Xaml::Shapes::Ellipse dot;
    dot.Width(6);
    dot.Height(6);
    dot.Fill(urnw::colors::MutedBrush());
    dot.Opacity(0.30);
    MarkRaw(dot);
    parts->typingDots.push_back(dot);
    dots.Children().Append(dot);
  }
  content.Children().Append(dots);

  TextBlock says;
  says.Text(L"Typing…");  // U+2026 HORIZONTAL ELLIPSIS
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) says.Style(st);
  says.FontSize(11);
  says.Foreground(urnw::colors::MutedBrush());
  says.VerticalAlignment(VerticalAlignment::Center);
  MarkRaw(says);
  content.Children().Append(says);

  shell.Child(content);
  row.Children().Append(shell);

  // The row carries the announcement; the dots and the word inside it are Raw,
  // so a screen reader says this ONCE rather than four times.
  Automation::AutomationProperties::SetName(row, L"Typing");
  parts->typingRow = row;
  return row;
}

// design d1 §1.4: the thread was the one pane with no 40dip L1 header strip, so
// its top edge was a bare cut. This is the same strip every other pane opens
// with (UrPaneHeaderStyle: sheet fill, the bottom hairline, 12dip side
// padding); SetThreadConversation re-points the two TextBlocks it hands back.
FrameworkElement MakeThreadHeader(std::shared_ptr<ThreadParts> const& parts) {
  Border bar;
  if (auto st = StyleByKey(L"UrPaneHeaderStyle")) bar.Style(st);

  Grid grid;
  ColumnDefinition title, meta;
  title.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  meta.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
  grid.ColumnDefinitions().Append(title);
  grid.ColumnDefinitions().Append(meta);

  TextBlock name;
  if (auto st = StyleByKey(L"UrPaneTitleStyle")) name.Style(st);
  Grid::SetColumn(name, 0);
  grid.Children().Append(name);

  TextBlock metaText;
  if (auto st = StyleByKey(L"UrPaneMetaStyle")) metaText.Style(st);
  metaText.HorizontalAlignment(HorizontalAlignment::Right);
  Grid::SetColumn(metaText, 1);
  grid.Children().Append(metaText);

  bar.Child(grid);
  parts->headerTitle = name;
  parts->headerMeta = metaText;
  return bar;
}

// The no-selection empty state (the --demo=chats void): ONE centred muted line
// under the identicon lattice at low alpha, echoing the search empty state
// (MainWindow.xaml.cpp). Built Visible; SetThreadConversation is the one writer
// that collapses it, so it shows exactly when no conversation is open and never
// alongside one. The lattice is decorative (Raw), achromatic by construction —
// no seed, no hue — the honest inverse of an avatar, never a stand-in person.
FrameworkElement MakeThreadEmptyState(std::shared_ptr<ThreadParts> const& parts) {
  StackPanel column;
  column.Spacing(10);
  column.HorizontalAlignment(HorizontalAlignment::Center);
  column.VerticalAlignment(VerticalAlignment::Center);

  auto lattice = urmsg::MakeIdenticonLattice(64);
  lattice.HorizontalAlignment(HorizontalAlignment::Center);
  lattice.Opacity(0.6);
  MarkRaw(lattice);
  column.Children().Append(lattice);

  // Mirrors thread_none_selected (Resources.resw). A code literal, like this
  // file's other demo strings (the search empty state precedent), rather than
  // a Loc dependency in the view.
  TextBlock line;
  line.Text(L"Select a conversation to read it.");
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) line.Style(st);
  line.FontSize(12);
  line.Foreground(urnw::colors::MutedBrush());
  line.TextAlignment(TextAlignment::Center);
  line.TextWrapping(TextWrapping::Wrap);
  line.MaxWidth(320);
  column.Children().Append(line);

  parts->emptyState = column;
  return column;
}

// The "loading earlier" marker (T8): a slim pill OVERLAY at the scroller's
// top edge, shown only while a chunk materializes. It is deliberately NOT a
// stack row — a row would grow the extent, and growing the extent is the very
// jump the prepend rule forbids. ~31 dip tall (the decision's 24-36 dip row),
// kFastMs in and out, and it never exists with motion off. The copy is the
// honest one: earlier history is being materialized, and this build claims
// nothing more than that (G4).
FrameworkElement MakeEarlierMarker(std::shared_ptr<ThreadParts> const& parts) {
  Border pill;
  pill.Background(BrushByKey(L"UrCardBrush", urnw::colors::kCard));
  pill.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
  pill.Padding(ThicknessHelper::FromLengths(14, 5, 14, 5));
  pill.HorizontalAlignment(HorizontalAlignment::Center);
  pill.VerticalAlignment(VerticalAlignment::Top);
  pill.Margin(ThicknessHelper::FromLengths(0, 6, 0, 0));
  // A marker must never eat the scroll it announces.
  pill.IsHitTestVisible(false);

  TextBlock text;
  text.Text(L"Loading earlier messages");
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) text.Style(st);
  text.FontSize(11);
  text.Foreground(urnw::colors::MutedBrush());
  pill.Child(text);

  pill.Visibility(Visibility::Collapsed);
  pill.Opacity(0.0);
  Automation::AutomationProperties::SetName(pill, L"Loading earlier messages");
  parts->earlierMarker = pill;
  return pill;
}

// ---- the composer (T6, design §9.1) --------------------------------------
// = App.xaml's UrBorderStrongBrush (#38FFFFFF). Written as a literal rather
// than derived from kText, which would give #38F8F8F8 — near enough to look
// right and wrong enough to be a second edge token.
constexpr winrt::Windows::UI::Color kBorderStrong{0x38, 0xFF, 0xFF, 0xFF};

// Entrance kFastMs on the standard curve, exit one step faster on the exit
// curve — UrMotion's own rule ("exits run one step faster than entrances"), no
// new token. Instant in both directions when motion is off.
void FadeFocusRule(FrameworkElement const& rule, bool on) {
  if (!rule) return;
  if (!urnw::motion::ShouldAnimate()) {
    rule.Opacity(on ? 1.0 : 0.0);
    return;
  }
  anim::Storyboard sb;
  auto a = on ? urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kFastMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2)
              : urnw::motion::MakeSplineDouble(1.0, 0.0, urnw::motion::kMicroMs, 0,
                                               urnw::motion::kExitP1, urnw::motion::kExitP2);
  anim::Storyboard::SetTarget(a, rule);
  anim::Storyboard::SetTargetProperty(a, L"Opacity");
  sb.Children().Append(a);
  sb.Begin();
}

// design §9.1: a control that cannot act is DISABLED, so the platform draws it
// at 0.38 opacity and it never takes focus. An enabled-looking button that eats
// a click is the demo bug that rule exists to stop.
Button MakeInertIconButton(wchar_t const* glyph, wchar_t const* name) {
  Button b;
  FontIcon g;
  g.FontFamily(IconFont());
  g.Glyph(glyph);
  g.FontSize(16);
  b.Content(g);
  b.Background(nullptr);
  b.BorderThickness(ThicknessHelper::FromUniformLength(0));
  b.Padding(ThicknessHelper::FromUniformLength(6));
  b.MinWidth(0);
  b.IsEnabled(false);
  Automation::AutomationProperties::SetName(b, winrt::hstring{name});
  return b;
}

FrameworkElement MakeComposer() {
  Border bar;
  // design d2 §3: the bar is SHEET (#151515, one step above the page) and the
  // input sits in a CARD well on it — the palette's own page -> sheet -> card
  // layering (UrColors.h) instead of one tonal step with the controls
  // floating in it. It also future-proofs the thread header: UrPaneHeaderStyle
  // is the same sheet fill, so header and composer become matching bookends.
  bar.Background(BrushByKey(L"UrSheetBrush", urnw::colors::kSheet));
  // The top hairline is NOT the bar's BorderBrush: it is its own pair of
  // elements below (`seam`/`seamFocus`), because focus has to LIFT it from
  // kBorder to kBorderStrong (design d1 §1.3) and FadeFocusRule animates
  // opacity — a BorderBrush colour cannot be faded.
  bar.BorderThickness(ThicknessHelper::FromUniformLength(0));
  bar.Padding(ThicknessHelper::FromLengths(12, 8, 12, 10));

  StackPanel column;
  column.Spacing(6);

  // The WELL (design d2 §3): one card surface holding the controls, with the
  // focus channel as a 1px overlay on its edge — the overlay pattern borrowed
  // from UrBubbleButtonStyle's EdgeLayer, so focus draws a ring around the
  // well instead of the old 1px rule under the whole row.
  Grid well;

  Border inputWell;
  inputWell.Background(BrushByKey(L"UrCardBrush", urnw::colors::kCard));
  inputWell.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
  // NO border (design d1 §1's one-seam rule): the card-over-sheet tonal step
  // carries the well's edge, and the composer's ONE horizontal seam is the
  // tray's own top hairline below — the well's old 1px border drew a second
  // edge ~10px under it. focusEdge (further down) is a SEPARATE overlay, not
  // this border, so the focus ring is untouched by removing it.
  inputWell.BorderThickness(ThicknessHelper::FromUniformLength(0));
  inputWell.Padding(ThicknessHelper::FromLengths(6, 2, 6, 2));

  Grid row;
  row.ColumnSpacing(4);
  for (int i = 0; i < 5; ++i) {
    ColumnDefinition c;
    c.Width(i == 3 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                   : GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
    row.ColumnDefinitions().Append(c);
  }

  auto attach = MakeInertIconButton(L"\uE723",  // Segoe Fluent "Attach" - paperclip
                                    L"Attach a file (not available in the demo)");
  auto emoji = MakeInertIconButton(L"\uE76E",   // Segoe Fluent "Emoji2" - outline smiley
                                   L"Insert an emoji (not available in the demo)");
  Grid::SetColumn(attach, 0);
  Grid::SetColumn(emoji, 1);
  row.Children().Append(attach);
  row.Children().Append(emoji);

  // The disappearing-timer chip: a glyph AND its value, because a clock face
  // alone cannot say "24 hours" and this is the one control whose whole point is
  // the number on it.
  Button timer;
  {
    StackPanel chip;
    chip.Orientation(Orientation::Horizontal);
    chip.Spacing(5);
    FontIcon g;
    g.FontFamily(IconFont());
    g.Glyph(L"\uE916");  // Segoe Fluent "Timer"
    g.FontSize(14);
    chip.Children().Append(g);
    TextBlock t;
    t.Text(L"24h");
    t.FontSize(12);
    chip.Children().Append(t);
    timer.Content(chip);
  }
  // design d2 §3 (its optional seating): cardHover fill, so the chip reads as
  // a control seated ON the card well rather than floating in it. Still
  // disabled — design §9.1 — with the border and the automation name as they
  // were.
  timer.Background(BrushByKey(L"UrCardHoverBrush", urnw::colors::kCardHover));
  timer.BorderBrush(urnw::colors::BorderBrush());
  timer.BorderThickness(ThicknessHelper::FromUniformLength(1));
  timer.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
  timer.Padding(ThicknessHelper::FromLengths(8, 3, 8, 3));
  timer.MinWidth(0);
  timer.IsEnabled(false);
  Automation::AutomationProperties::SetName(
      timer, L"Disappearing messages: 24 hours (not available in the demo)");
  Grid::SetColumn(timer, 2);
  row.Children().Append(timer);

  // The ONE live control. Focus and typing are things this demo can honestly
  // do, so they are not taken away. The placeholder is the word "Message" and
  // nothing more — a placeholder that promised anything about what happens to
  // what you type would be the claim G4 forbids.
  TextBox box;
  box.PlaceholderText(L"Message");
  box.AcceptsReturn(false);
  box.TextWrapping(TextWrapping::Wrap);
  box.MaxHeight(96);
  box.BorderThickness(ThicknessHelper::FromUniformLength(0));
  box.Background(nullptr);
  // The caret is accent (design d1 §4, an approved micro-extension of the
  // reservation): a 1.5px blinking line in the demo's one live input, which is
  // the same class as every sanctioned use — a point-sized mark singling out
  // where you can act. Never a fill, and off whenever the box is unfocused.
  //
  // WinUI 3's TextBox has NO CaretBrush property (the UWP one was never
  // ported; the WindowsAppSDK 2.2 projection proves it) — the caret's colour
  // is the theme resource its template binds, so the per-control override of
  // that key below is the only way to spend it. If the template's key ever
  // changes this degrades silently to the default caret, which is the design's
  // sanctioned fallback.
  box.Resources().Insert(winrt::box_value(winrt::hstring{L"TextControlCaretBrush"}),
                         urnw::colors::AccentBrush());
  box.VerticalAlignment(VerticalAlignment::Center);
  Automation::AutomationProperties::SetName(box, L"Message (the demo does not send)");
  Grid::SetColumn(box, 3);
  row.Children().Append(box);

  // UrAccentBrush's one legitimate home on this surface (the selection outline
  // is the other). AccentButtonStyle is what spends App.xaml's
  // AccentButtonBackground* keys, which are already the pale yellow — and
  // DISABLED it resolves to AccentButtonBackgroundDisabled #33EFF7BB, so the
  // pill reads as faint rather than as a live call to action.
  Button send;
  FontIcon plane;
  plane.FontFamily(IconFont());
  plane.Glyph(L"\uE724");  // Segoe Fluent "Send" - outline paper plane
  plane.FontSize(16);
  send.Content(plane);
  if (auto st = StyleByKey(L"AccentButtonStyle")) {
    send.Style(st);
  } else {
    send.Background(urnw::colors::AccentBrush());
    send.Foreground(urnw::colors::MakeBrush(urnw::colors::kInverseText));
  }
  send.MinWidth(40);
  send.Padding(ThicknessHelper::FromLengths(10, 6, 10, 6));
  // design d2 §3: a real pill (16) against the 12 dip well and chips.
  send.CornerRadius(CornerRadiusHelper::FromUniformRadius(16));
  send.IsEnabled(false);  // design §9.1 — inert in BOTH text states below
  Automation::AutomationProperties::SetName(send, L"Send (not available in the demo)");
  Grid::SetColumn(send, 4);
  row.Children().Append(send);

  // design d2 §3's enable-motion bullet, taken (the honesty note at the bottom
  // of MakeComposer covers why this is safe): the pill now tells the text->send
  // relationship without EVER becoming clickable. EMPTY box: glyph-only in
  // MutedBrush, NO wash — the bare #33EFF7BB disc read as muddy olive on an
  // empty composer, an affordance pointing at nothing. NON-EMPTY: today's
  // disabled wash, unchanged. IsEnabled(false) in both, so the platform never
  // gives it focus or a click.
  //
  // Only BRUSHES change, so the pill's geometry never moves (no layout
  // animation). The wash is ONE SolidColorBrush whose Color is mutated, never a
  // resource swap: it is inserted as the per-button
  // AccentButtonBackgroundDisabled the (always-applied) Disabled visual state
  // resolves, so re-colouring that one object repaints the pill with no
  // VisualStateManager re-evaluation to rely on. It starts transparent — every
  // capture opens on an empty box.
  auto sendWash = urnw::colors::MakeBrush(winrt::Windows::UI::Color{0x00, 0x00, 0x00, 0x00});
  send.Resources().Insert(winrt::box_value(winrt::hstring{L"AccentButtonBackgroundDisabled"}),
                          sendWash);
  plane.Foreground(urnw::colors::MutedBrush());  // the empty-box glyph
  box.TextChanged([sendWash, plane](winrt::Windows::Foundation::IInspectable const& sender,
                                    TextChangedEventArgs const&) {
    const bool empty = sender.as<Controls::TextBox>().Text().empty();
    // transparent <-> today's disabled wash #33EFF7BB; muted glyph <-> the
    // inherited (dark) disabled glyph. Both states keep IsEnabled(false).
    sendWash.Color(empty ? winrt::Windows::UI::Color{0x00, 0x00, 0x00, 0x00}
                         : winrt::Windows::UI::Color{0x33, 0xEF, 0xF7, 0xBB});
    if (empty)
      plane.Foreground(urnw::colors::MutedBrush());
    else
      plane.ClearValue(Controls::IconElement::ForegroundProperty());  // inherit the disabled glyph
  });
  inputWell.Child(row);
  well.Children().Append(inputWell);

  // The focus channel. Not the accent — that is the send button and the
  // selection outline only — so focus lifts UrBorderStrongBrush, the same
  // edge step UrCardButtonStyle's hover state already spends. It now draws a
  // 1px ring on the WELL's edge (design d2 §3): same radius 12 and same
  // thickness 1 as the well beneath it, so it lands on exactly the well's
  // pixels — the overlay pattern borrowed from UrBubbleButtonStyle's
  // EdgeLayer. Retargeted from wave 1's 1px rule under the whole row: same
  // FadeFocusRule helper, same durations (kFastMs standard in / kMicroMs exit
  // out), same ShouldAnimate gate, zero new motion code.
  Border focusEdge;
  focusEdge.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
  focusEdge.BorderThickness(ThicknessHelper::FromUniformLength(1));
  focusEdge.BorderBrush(urnw::colors::MakeBrush(kBorderStrong));
  focusEdge.Opacity(0.0);
  well.Children().Append(focusEdge);
  column.Children().Append(well);

  // The bar's TOP hairline (design d1 §1.3): `seam` is the resting kBorder
  // line, `seamFocus` the kBorderStrong lift stacked exactly over it, faded in
  // by the same helper and on the same tokens as the well's focus ring above —
  // kFastMs in on the standard curve, kMicroMs out on the exit curve, instant
  // both ways when ShouldAnimate() is false. The composer is the one working
  // object pinned to the stage; focus waking its surface boundary is what makes
  // the bar read as resting ON the thread rather than floating under it.
  Border seam;
  seam.Height(1);
  seam.Background(urnw::colors::BorderBrush());
  Border seamFocus;
  seamFocus.Height(1);
  seamFocus.Background(urnw::colors::MakeBrush(kBorderStrong));
  seamFocus.Opacity(0.0);

  box.GotFocus([focusEdge, seamFocus](auto const&, auto const&) {
    FadeFocusRule(focusEdge, true);
    FadeFocusRule(seamFocus, true);
  });
  box.LostFocus([focusEdge, seamFocus](auto const&, auto const&) {
    FadeFocusRule(focusEdge, false);
    FadeFocusRule(seamFocus, false);
  });

  // Said ONCE, here, instead of on every inert control in the window: the
  // failed message's [ Try again ], the [ Review ] on the key-change record and
  // these four all point at the same fact. It says what is NOT happening; it
  // claims nothing about encryption, because there is none.
  TextBlock note;
  note.Text(L"Demo — nothing is sent, and no message leaves this window.");  // U+2014 EM DASH
  if (auto st = StyleByKey(L"UrCaptionTextStyle")) note.Style(st);
  note.FontSize(11);
  note.Foreground(urnw::colors::FaintBrush());
  column.Children().Append(note);

  // seam and seamFocus share row 0 so the focus lift lands on the SAME pixels
  // as the resting hairline; the column sits below them.
  Grid surface;
  RowDefinition seamRow, bodyRow;
  seamRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  surface.RowDefinitions().Append(seamRow);
  surface.RowDefinitions().Append(bodyRow);
  Grid::SetRow(column, 1);
  surface.Children().Append(seam);
  surface.Children().Append(seamFocus);
  surface.Children().Append(column);
  bar.Child(surface);
  // A click in the composer is NOT a click in empty thread space. Without this
  // it bubbles to MakeThread's root.Tapped and deselects the message the rail
  // is showing, the moment you go to type — the same reason a bubble marks its
  // own Tapped handled.
  bar.Tapped([](auto const&, auto const& args) { args.Handled(true); });
  return bar;
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
  // FOUR rows: the 40dip pane header, the scrolling backlog, the typing
  // indicator, the composer. Auto / Star / Auto / Auto — the header, typing row
  // and composer keep their measured height and the backlog takes what is left,
  // so the composer cannot be scrolled off and a typing indicator appearing
  // SHORTENS the backlog rather than covering its last bubble.
  for (int i = 0; i < 4; ++i) {
    RowDefinition rd;
    rd.Height(i == 1 ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                     : GridLengthHelper::FromValueAndType(1, GridUnitType::Auto));
    root.RowDefinitions().Append(rd);
  }

  // d1 §1.4's missing thread header. Re-pointed by SetThreadConversation.
  auto header = MakeThreadHeader(parts);
  Grid::SetRow(header, 0);
  root.Children().Append(header);

  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroller.Padding(ThicknessHelper::FromLengths(kThreadPadDip, 8, kThreadPadDip, 12));

  StackPanel stack;
  // Spacing 0: the §1 run rhythm is carried by each bubble row's own top
  // margin (GapAboveDip — 2 continuation / 10 run-start, set in MakeBubbleRow)
  // and by the separators'/system rows'/record's own margins, so runs read as
  // blocks. A uniform inter-row gap here would flatten exactly that.
  stack.Spacing(0);
  scroller.Content(stack);
  Grid::SetRow(scroller, 1);
  root.Children().Append(scroller);

  // The no-selection empty state, in the SAME backlog row ON TOP of the
  // (transparent, empty) scroller, declared after it so it paints above.
  // Visible until the first SetThreadConversation collapses it.
  auto emptyState = MakeThreadEmptyState(parts);
  Grid::SetRow(emptyState, 1);
  root.Children().Append(emptyState);

  // The T8 "loading earlier" marker, same row, above the scroller — an
  // overlay, never a stack row (its own comment carries why).
  auto earlierMarker = MakeEarlierMarker(parts);
  Grid::SetRow(earlierMarker, 1);
  root.Children().Append(earlierMarker);

  auto typingRow = MakeTypingIndicator(parts);
  Grid::SetRow(typingRow, 2);
  root.Children().Append(typingRow);

  auto composer = MakeComposer();
  Grid::SetRow(composer, 3);
  root.Children().Append(composer);

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
  // put it there: BuildDemoViews runs from the window constructor, before any
  // layout pass and while ThreadHost is still collapsed, so the ChangeView down
  // there sees ScrollableHeight 0 and lands on nothing. VERIFIED, not assumed -
  // the first capture of this surface opened on "Yesterday" at the top with the
  // failed row an entire viewport below the fold. The stack's own SizeChanged
  // is the first moment a real extent exists, and it fires again whenever the
  // column resizes. Nothing here loops: ChangeView moves the OFFSET, which is
  // not a size.
  //
  // T6 GAVE IT A SECOND JOB, and W9 gave the job its guard. Appending a row
  // adds a child to this stack, so this handler fires and re-pins - which is
  // what keeps an arriving message visible and is why AppendThreadRow writes
  // no scroll offset of its own (two writers of one property, and the later
  // one wins by accident of ordering). The guard is design 9.2's do-not-yank
  // rule, decided by the pure ShouldPinToBottom (ThreadView.h): pin
  // unconditionally until the FIRST pin has landed (pinArmed - the first real
  // extent has offset 0 and a large scrollable height, indistinguishable from
  // "the reader scrolled to the top", so an unarmed guard would skip the very
  // pin this handler exists to make), then pin only while the reader is within
  // 48 dip of the foot. A reader who scrolled up to read the backlog is not
  // yanked down when an ambient row lands; a reader at the bottom stays
  // pinned. The four cases are asserted in --diagnose; the scrolled-away
  // branch of a live window is code-inspection only, because input may not be
  // synthesised.
  stack.SizeChanged([parts](auto const&, auto const&) {
    // The offset is measured against pinExtent (the extent the LAST decision
    // was made at), not ScrollableHeight() at fire time: this SizeChanged is
    // the extent CHANGING - an arriving row or a rewrap grows it under a
    // stationary offset - and "at the foot when the last decision was made"
    // must not read as "scrolled away". ShouldPinToBottom stays the one pure
    // decision; the handler only chooses what to feed it.
    if (!ShouldPinToBottom(parts->pinArmed, parts->scroller.VerticalOffset(),
                           parts->pinExtent))
      return;
    parts->pinArmed = true;
    parts->pinExtent = parts->scroller.ScrollableHeight();
    parts->scroller.ChangeView(nullptr, parts->scroller.ScrollableHeight(), nullptr, true);
  });

  // T8: the window follows the scroller. ViewChanged covers user scrolls AND
  // the slides' own ChangeViews — the latter are dropped by windowBusy inside
  // MaybeSlideWindow, so a slide's offset correction cannot retrigger itself.
  // Under the cap this is a no-op (WindowActive false), so a small
  // conversation carries one event subscription and nothing else.
  scroller.ViewChanged([parts](auto const&, auto const&) { MaybeSlideWindow(parts); });

  // Design 9.1: a click in empty thread space deselects. A Button handles its
  // own pointer events, so a bubble click does not reach this — and since T6
  // this root also holds the COMPOSER, which is not a Button and is not empty
  // thread space, so MakeComposer marks its own Tapped handled rather than
  // letting a click into the message box throw away the selection the rail is
  // showing.
  root.Tapped([parts](auto const&, auto const&) {
    if (parts->onDeselect) parts->onDeselect();
  });

  // --demo is the ONLY way an agent can reach a state without synthesising
  // input (design §9.3), and the typing indicator is otherwise unreachable
  // until an ambient loop that does not exist yet decides to raise it. So
  // --demo-autoplay SEEDS it on: a presenter sees the indicator immediately
  // instead of waiting out a minute of silence, and the capture that proves it
  // renders has something to photograph. Without --demo-autoplay the row stays
  // Collapsed and costs nothing.
  //
  // The WAVE starts here too, from the window constructor and before this root
  // is mounted into ThreadHost — and that works, which is not what a lifecycle
  // rule of thumb would predict. It was not taken on trust: a Loaded-armed
  // variant was built and swept across a whole 1500 ms cycle at 150 ms steps,
  // and this version was then sampled at the steps where that sweep showed the
  // dots apart. Both drew three DIFFERENT opacities, so the extra hook bought
  // nothing and came out rather than being kept "to be safe".
  //
  // HOW NOT TO CHECK IT, because a pair of stills said the opposite and was
  // believed for a while. The standard curve (0.10,0.90)->(0.20,1.00) is a hard
  // ease-out: a dot is past 0.93 opacity a third of the way through its 750 ms
  // leg and sits near 1.0 for the rest, so the three are only visibly apart for
  // part of the cycle. MEASURED on that 10-frame sweep: 3 frames showed three
  // different opacities, 7 showed three identical ones. Two stills, however far
  // apart they are spaced, are therefore NOT a test of whether the wave runs —
  // they will usually land on the plateau, which looks exactly like a
  // storyboard that never started.
  {
    const auto opts = demo::ParseDemoOptions();
    if (opts.autoplay && opts.screen == demo::DemoScreen::Thread) SetThreadTyping(v, true);
  }
  return v;
}

namespace {

// ---- T8: the sliding window, applied to the tree ---------------------------
// The pure arithmetic lives in Views/ThreadLayout.h (and its --diagnose walk);
// everything below is the view half: apply ThreadWindow ranges to the stack,
// diffing minimally — insert the chunk, remove the trimmed, never rebuild the
// column on a slide.

// One built row of the plan, as data. root is null ONLY for the unlabelled
// day separator — the SetThreadConversation exception, shared here so a slide
// cannot draw what an open would not.
struct BuiltRow {
  FrameworkElement root{nullptr};
  FrameworkElement clusterHost{nullptr};
  ThreadBubble bubble;  // bubble.root null off a message row
};

// THE ONE BUILDER of a planned row. SetThreadConversation's loop and the
// window slides both call it: a row the window re-covers is built by the same
// function a fresh open would use — the incremental==rebuild discipline of T6
// stated as code structure rather than as a gate comment.
//
// The plan walks the FULL history, so a row at the window seam gets the
// header, the corners and the cluster (CarriesDeliveryGlyph against the row's
// real world successor) that a full rebuild would give it.
BuiltRow BuildPlanRow(std::shared_ptr<ThreadParts> const& parts,
                      demo::Conversation const& c, ThreadRowPlan const& p) {
  BuiltRow out;
  demo::MessageRow const& row = c.rows[p.rowIndex];
  demo::MessageRow const* next =
      (p.rowIndex + 1 < c.rows.size()) ? &c.rows[p.rowIndex + 1] : nullptr;
  switch (p.shape) {
    case ThreadRowShape::DaySeparator: {
      const std::wstring label = DaySeparatorLabel(row);
      // An unlabelled separator draws NOTHING rather than an empty pill.
      if (!label.empty()) out.root = MakeDaySeparator(label);
      break;
    }
    case ThreadRowShape::SystemLine:
      out.root = MakeSystemLine(winrt::hstring{row.systemText});
      break;
    case ThreadRowShape::SystemPermanentRecord:
      out.root = MakeKeyChangeRecord(winrt::hstring{row.systemText});
      break;
    case ThreadRowShape::IncomingBubble:
    case ThreadRowShape::OutgoingBubble: {
      // CarriesDeliveryGlyph(row, next), never endsOutgoingRun: the plan field
      // is direction-only and is FALSE on the shipped world's mid-run Failed
      // row (Views/ThreadLayout.h carries the case).
      auto built = MakeBubbleRow(row, parts->group, p.showSenderHeader,
                                 CarriesDeliveryGlyph(row, next), p.runPos);
      built.bubble.root.Click([parts, id = row.id](auto const&, auto const&) {
        if (parts->onSelect) parts->onSelect(id);
      });
      out.bubble = built.bubble;
      out.root = built.root;
      out.clusterHost = built.root;
      break;
    }
  }
  return out;
}

// Remove the FIRST `count` rendered rows from the tree and the bookkeeping.
// Does NOT measure or touch the scroll offset or parts->window: the caller owns
// all three (the offset correction is an EXTENT delta, measured across the
// trim by the caller — UseLayoutRounding makes per-row summation lose the
// accumulated pixel snapping; MaterializeEarlierChunk carries the measurement).
// `drawn` maps rows to stack children (an unlabelled separator occupies none).
void TrimWindowHead(std::shared_ptr<ThreadParts> const& parts, std::size_t count) {
  if (count == 0 || parts->rows.size() < count) return;
  std::size_t elems = 0, bubblesGone = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (parts->rows[i].drawn) ++elems;
    if (parts->rows[i].clusterHost) ++bubblesGone;
  }
  auto kids = parts->stack.Children();
  for (std::size_t i = 0; i < elems; ++i) kids.RemoveAt(0);
  parts->rows.erase(parts->rows.begin(),
                    parts->rows.begin() + static_cast<std::ptrdiff_t>(count));
  parts->bubbles.erase(parts->bubbles.begin(),
                       parts->bubbles.begin() + static_cast<std::ptrdiff_t>(bubblesGone));
  SyncPublicBubbles(parts);
}

// The FOOT mirror: remove the LAST `count` rendered rows. No offset correction
// is needed here — rows leaving below the viewport do not move content above
// them — so nothing is returned.
void TrimWindowFoot(std::shared_ptr<ThreadParts> const& parts, std::size_t count) {
  if (count == 0 || parts->rows.size() < count) return;
  const std::size_t base = parts->rows.size() - count;
  std::size_t elems = 0, bubblesGone = 0;
  for (std::size_t i = base; i < parts->rows.size(); ++i) {
    if (parts->rows[i].drawn) ++elems;
    if (parts->rows[i].clusterHost) ++bubblesGone;
  }
  auto kids = parts->stack.Children();
  for (std::size_t i = 0; i < elems; ++i) kids.RemoveAt(kids.Size() - 1);
  parts->rows.erase(parts->rows.begin() + static_cast<std::ptrdiff_t>(base),
                    parts->rows.end());
  parts->bubbles.erase(parts->bubbles.end() - static_cast<std::ptrdiff_t>(bubblesGone),
                       parts->bubbles.end());
  SyncPublicBubbles(parts);
}

// A row that was selected when it left the tree comes back selected. The id is
// the one SetThreadSelectedMessage recorded; SetBubbleEdge is the one writer
// of what that looks like. No match is the common case and costs a walk over
// at most one chunk.
void ReapplySelection(std::shared_ptr<ThreadParts> const& parts,
                      std::vector<ThreadBubble> const& born) {
  if (parts->selectedId.empty()) return;
  for (auto const& b : born) {
    if (b.id != parts->selectedId || !b.root) continue;
    // Direction from the ALIGNMENT, the same read SetThreadSelectedMessage
    // makes (ThreadBubble carries no direction field — contract §4).
    SetBubbleEdge(b.root, b.root.HorizontalAlignment() == HorizontalAlignment::Right,
                  /*selected=*/true);
  }
}

// The chunk's one motion: the WHOLE chunk fades 0 -> 1 at kFastMs as ONE unit
// — no per-bubble stagger, no rise; history is not arriving, and a bubble
// entrance would say it was. From-pose local (Opacity 0 written in the same
// turn the rows were inserted), one storyboard, Completed lands the final pose
// and Stops — the canonical rule; RunBubbleEntrance carries the why. Completed
// also ends the load beat (CompleteWindowBeat), so a reader who kept scrolling
// during the kFastMs gets the next chunk chained without another event.
void FadeChunkIn(std::shared_ptr<ThreadParts> const& parts,
                 std::vector<FrameworkElement> const& rows);
void CompleteWindowBeat(std::shared_ptr<ThreadParts> const& parts);
void MaybeSlideWindow(std::shared_ptr<ThreadParts> const& parts);
void StartEarlierLoad(std::shared_ptr<ThreadParts> const& parts);

void FadeChunkIn(std::shared_ptr<ThreadParts> const& parts,
                 std::vector<FrameworkElement> const& rows) {
  parts->fadingRows = rows;
  if (rows.empty() || !urnw::motion::ShouldAnimate() || !ChainVisible(rows.front())) {
    // Motion GONE, not shortened (or nothing to fade, or a tree that cannot
    // play a board): the final pose, immediately. The caller ends the beat
    // itself — there is no Completed coming.
    for (auto const& el : rows)
      if (el) el.Opacity(1.0);
    parts->fadingRows.clear();
    return;
  }
  for (auto const& el : rows) el.Opacity(0.0);
  anim::Storyboard sb;
  for (auto const& el : rows) {
    auto a = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kFastMs, 0,
                                            urnw::motion::kStandardP1,
                                            urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(a, el);
    anim::Storyboard::SetTargetProperty(a, L"Opacity");
    sb.Children().Append(a);
  }
  sb.Completed([parts, weakSb = winrt::make_weak(sb)](auto const&, auto const&) {
    for (auto const& el : parts->fadingRows)
      if (el) el.Opacity(1.0);
    parts->fadingRows.clear();
    if (auto board = weakSb.get()) board.Stop();
    parts->chunkFadeStory = nullptr;
    CompleteWindowBeat(parts);
  });
  parts->chunkFadeStory = sb;
  sb.Begin();
}

// The marker's show/hide. kFastMs BOTH ways — the decision fixes one beat for
// in and out; the marker is not a dismissal, so the exits-run-faster default
// does not apply to it. Instant both ways with motion off.
void SetEarlierMarkerVisible(std::shared_ptr<ThreadParts> const& parts, bool on) {
  if (!parts->earlierMarker) return;
  if (parts->earlierMarkerStory) {
    parts->earlierMarkerStory.Stop();
    parts->earlierMarkerStory = nullptr;
  }
  if (!urnw::motion::ShouldAnimate()) {
    parts->earlierMarker.Opacity(on ? 1.0 : 0.0);
    parts->earlierMarker.Visibility(on ? Visibility::Visible : Visibility::Collapsed);
    return;
  }
  anim::Storyboard sb;
  auto a = on ? urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kFastMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2)
              : urnw::motion::MakeSplineDouble(1.0, 0.0, urnw::motion::kFastMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
  anim::Storyboard::SetTarget(a, parts->earlierMarker);
  anim::Storyboard::SetTargetProperty(a, L"Opacity");
  sb.Children().Append(a);
  sb.Completed([parts, on, weakSb = winrt::make_weak(sb)](auto const&, auto const&) {
    if (!on) parts->earlierMarker.Visibility(Visibility::Collapsed);
    parts->earlierMarker.Opacity(on ? 1.0 : 0.0);
    if (auto board = weakSb.get()) board.Stop();
    if (parts->earlierMarkerStory) parts->earlierMarkerStory = nullptr;
  });
  // From-pose local, written BEFORE the Visibility flip on the way in — the
  // SetSearchEmptyVisible precedent, so no frame renders the marker fully
  // opaque first.
  parts->earlierMarker.Opacity(on ? 0.0 : 1.0);
  if (on) parts->earlierMarker.Visibility(Visibility::Visible);
  parts->earlierMarkerStory = sb;
  sb.Begin();
}

// A rebuild retires any load beat mid-flight: the timer, the marker, and a
// chunk fade whose rows are about to leave the tree.
void CancelEarlierLoad(std::shared_ptr<ThreadParts> const& parts) {
  if (parts->earlierTimer) {
    parts->earlierTimer.Stop();
    parts->earlierTimer = nullptr;
  }
  if (parts->earlierMarkerStory) {
    parts->earlierMarkerStory.Stop();
    parts->earlierMarkerStory = nullptr;
  }
  if (parts->earlierMarker) {
    parts->earlierMarker.Visibility(Visibility::Collapsed);
    parts->earlierMarker.Opacity(0.0);
  }
  // Mid-fade rows sit at local Opacity 0 (the from-pose): land them by hand
  // BEFORE stopping the board — a Stop leaves locals alone, which is the
  // stranded-at-0 hazard the from-pose rule exists to avoid.
  for (auto const& el : parts->fadingRows)
    if (el) el.Opacity(1.0);
  parts->fadingRows.clear();
  if (parts->chunkFadeStory) {
    parts->chunkFadeStory.Stop();
    parts->chunkFadeStory = nullptr;
  }
  parts->windowBusy = false;
}

// The prepend itself: one chunk OLDER inserted above, the foot trimmed back to
// the cap, the viewport preserved exactly — the reader's content does not move.
// The correction measures the EXTENT before/after the insert, never per-row
// heights: UseLayoutRounding snaps each arranged row to the physical pixel
// grid, so a sum of DesiredSize/ActualHeight loses the accumulated rounding
// (measured on this surface: 39.2 dip lost over a 100-row chunk at 125% DPI —
// caught by the A/B capture as a one-row drift). The extent delta includes it.
void MaterializeEarlierChunk(std::shared_ptr<ThreadParts> const& parts) {
  parts->earlierTimer = nullptr;  // one-shot; it already fired
  // A rebuild may have landed during the beat: re-check before touching
  // anything (the timer was one-shot and the beat is still marked busy).
  if (!parts->conv || parts->window.start == 0) {
    SetEarlierMarkerVisible(parts, false);
    CompleteWindowBeat(parts);
    return;
  }
  const auto buildStart = std::chrono::steady_clock::now();
  const std::vector<ThreadRowPlan> plan = PlanThreadRows(*parts->conv);
  const double planMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
          .count();

  const ThreadWindow before = parts->window;
  const ThreadWindow after = SlideWindowUp(before);
  if (after.start == before.start && after.end == before.end) {
    SetEarlierMarkerVisible(parts, false);
    CompleteWindowBeat(parts);
    return;
  }
  const std::size_t added = before.start - after.start;
  const std::size_t trimmed = before.end - after.end;
  const double offsetBefore = parts->scroller.VerticalOffset();
  // Clean layout at entry, so this is the true pre-insert extent.
  const double extentBefore = parts->scroller.ExtentHeight();

  // Insert [after.start, before.start) ABOVE, oldest first. insertPos counts
  // DRAWN rows only — an unlabelled separator occupies no stack child.
  std::vector<FrameworkElement> born;
  born.reserve(added);
  std::vector<ThreadBubble> bornBubbles;
  std::vector<ThreadParts::RenderedRow> bornRows;
  bornRows.reserve(added);
  uint32_t insertPos = 0;
  for (std::size_t i = after.start; i < before.start; ++i) {
    BuiltRow b = BuildPlanRow(parts, *parts->conv, plan[i]);
    if (b.root) {
      parts->stack.Children().InsertAt(insertPos, b.root);
      ++insertPos;
      born.push_back(b.root);
    }
    if (b.bubble.root) bornBubbles.push_back(b.bubble);
    bornRows.push_back(
        ThreadParts::RenderedRow{parts->conv->rows[i], b.clusterHost, b.root != nullptr});
  }
  parts->rows.insert(parts->rows.begin(),
                     std::make_move_iterator(bornRows.begin()),
                     std::make_move_iterator(bornRows.end()));
  parts->bubbles.insert(parts->bubbles.begin(), bornBubbles.begin(), bornBubbles.end());
  parts->window = after;
  SyncPublicBubbles(parts);

  // The 68% cap BEFORE the measure: MakeBubbleRow builds with the unmeasured
  // 640 DIP fallback, and a long body wraps differently at the real cap than
  // at 640 — measured at the fallback and capped after, the inserted extent
  // would come out short by exactly the re-wrap. (Caught in the A/B capture.)
  ApplyColumnWidth(parts);
  parts->scroller.UpdateLayout();
  // A prepend moves every existing row down by exactly the inserted extent, so
  // OffsetAfterSlide gets the anchor pair 0 -> addedExtent — the same pure
  // arithmetic the gate walks.
  const double addedExtent = parts->scroller.ExtentHeight() - extentBefore;
  if (trimmed) TrimWindowFoot(parts, trimmed);
  parts->scroller.UpdateLayout();
  parts->pinExtent = parts->scroller.ScrollableHeight();  // the T8 second-writer rule
  parts->scroller.ChangeView(
      nullptr,
      ClampScrollOffset(OffsetAfterSlide(offsetBefore, 0.0, addedExtent), parts->pinExtent),
      nullptr, true);
  ReapplySelection(parts, bornBubbles);
  FadeChunkIn(parts, born);
  SetEarlierMarkerVisible(parts, false);
  urnw::LogInfo(
      "thread: window slide [{},{}) -> [{},{}) of {}: plan {:.2f} ms, {} rows parented in {:.2f} ms",
      before.start, before.end, after.start, after.end, parts->conv->rows.size(), planMs,
      added,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
          .count());
  if (!parts->chunkFadeStory) CompleteWindowBeat(parts);  // the instant path
}

// The foot mirror: one chunk NEWER appended at the loaded foot, the head
// trimmed back to the cap, same one-unit fade, and NO marker — nothing is
// being loaded; the world already held these rows and the window is simply
// re-covering them on the way home.
void SlideWindowDownInView(std::shared_ptr<ThreadParts> const& parts) {
  parts->windowBusy = true;
  const auto buildStart = std::chrono::steady_clock::now();
  const std::vector<ThreadRowPlan> plan = PlanThreadRows(*parts->conv);
  const double planMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
          .count();

  const ThreadWindow before = parts->window;
  const ThreadWindow after = SlideWindowDown(before, parts->conv->rows.size());
  if (after.end == before.end) {  // defensive: the trigger guards this
    CompleteWindowBeat(parts);
    return;
  }
  const std::size_t added = after.end - before.end;
  const std::size_t trimmed = after.start - before.start;
  const double offsetBefore = parts->scroller.VerticalOffset();
  // The W9 question, asked BEFORE the mutation: at the foot of the loaded
  // range when the slide began. The stack.SizeChanged pin fires during the
  // UpdateLayout below and the ChangeView here OVERWRITES it — same turn, both
  // silent, last writer wins — so the slide asks the pure decision itself
  // rather than racing the handler.
  const bool pinToFoot =
      ShouldPinToBottom(parts->pinArmed, offsetBefore, parts->pinExtent);

  // Head trim FIRST, and measured by EXTENT delta (layout rounding — see
  // MaterializeEarlierChunk): the trim pulls every surviving row UP by exactly
  // what the extent shrinks.
  const double extentBefore = parts->scroller.ExtentHeight();  // clean at entry
  if (trimmed) TrimWindowHead(parts, trimmed);
  parts->scroller.UpdateLayout();
  const double removedExtent = extentBefore - parts->scroller.ExtentHeight();

  std::vector<FrameworkElement> born;
  born.reserve(added);
  std::vector<ThreadBubble> bornBubbles;
  for (std::size_t i = before.end; i < after.end; ++i) {
    BuiltRow b = BuildPlanRow(parts, *parts->conv, plan[i]);
    if (b.root) {
      parts->stack.Children().Append(b.root);
      born.push_back(b.root);
    }
    if (b.bubble.root) bornBubbles.push_back(b.bubble);
    parts->rows.push_back(
        ThreadParts::RenderedRow{parts->conv->rows[i], b.clusterHost, b.root != nullptr});
  }
  parts->bubbles.insert(parts->bubbles.end(), bornBubbles.begin(), bornBubbles.end());
  parts->window = after;
  SyncPublicBubbles(parts);
  ApplyColumnWidth(parts);
  parts->scroller.UpdateLayout();
  parts->pinExtent = parts->scroller.ScrollableHeight();  // the T8 second-writer rule
  // A reader within 48 dip of the foot is AT the foot — the pin carries them
  // to the new foot, exactly as an ambient append would. Anyone else keeps
  // their content: the offset follows the head trim's delta.
  parts->scroller.ChangeView(
      nullptr,
      pinToFoot ? parts->pinExtent
                : ClampScrollOffset(OffsetAfterSlide(offsetBefore, removedExtent, 0.0),
                                    parts->pinExtent),
      nullptr, true);
  ReapplySelection(parts, bornBubbles);
  FadeChunkIn(parts, born);
  urnw::LogInfo(
      "thread: window slide [{},{}) -> [{},{}) of {}: plan {:.2f} ms, {} rows parented in {:.2f} ms",
      before.start, before.end, after.start, after.end, parts->conv->rows.size(), planMs,
      added,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
          .count());
  if (!parts->chunkFadeStory) CompleteWindowBeat(parts);  // the instant path
}

void CompleteWindowBeat(std::shared_ptr<ThreadParts> const& parts) {
  parts->windowBusy = false;
  // Scroll events during the beat were dropped on windowBusy; re-evaluate once
  // so a reader still parked at an edge gets the next chunk without having to
  // nudge the scroller again.
  MaybeSlideWindow(parts);
}

// The trigger. Fires on the scroller's ViewChanged (user scrolls AND the
// slides' own ChangeViews — the latter are dropped by windowBusy), and once
// from each beat's completion. Under the cap WindowActive is false and the
// whole window is a no-op: small conversations carry one event subscription
// and nothing else.
void MaybeSlideWindow(std::shared_ptr<ThreadParts> const& parts) {
  if (parts->windowBusy || !parts->conv || !parts->scroller) return;
  const std::size_t total = parts->conv->rows.size();
  if (!WindowActive(total)) return;
  const double offset = parts->scroller.VerticalOffset();
  const double viewport = parts->scroller.ViewportHeight();
  if (0 < parts->window.start && NearTopOfLoaded(offset, viewport)) {
    StartEarlierLoad(parts);
  } else if (parts->window.end < total &&
             NearFootOfLoaded(offset, parts->scroller.ScrollableHeight(), viewport)) {
    SlideWindowDownInView(parts);
  }
}

// The load beat: the marker fades in (kFastMs), holds one beat so the load is
// SEEN to have happened — the demo's fetch is same-turn, so without the hold
// the marker would never render — then the chunk materializes. With motion
// off there is no marker and no fade: the chunk is simply there, this turn.
void StartEarlierLoad(std::shared_ptr<ThreadParts> const& parts) {
  parts->windowBusy = true;
  if (!urnw::motion::ShouldAnimate()) {
    MaterializeEarlierChunk(parts);
    return;
  }
  SetEarlierMarkerVisible(parts, true);
  // 2 x kFastMs: one to finish fading in, one to be read. IsRepeating(false),
  // the DemoAutoplayLoop rule: a DispatcherQueueTimer REPEATS by default, and
  // a repeating beat would keep sliding the window up on its own.
  parts->earlierTimer =
      winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
  parts->earlierTimer.IsRepeating(false);
  parts->earlierTimer.Interval(urnw::motion::Ms(2 * urnw::motion::kFastMs));
  parts->earlierTimer.Tick(
      [parts](auto const&, auto const&) { MaterializeEarlierChunk(parts); });
  parts->earlierTimer.Start();
}

// ---- T9: the hydration fill, applied to the tree -----------------------------
// The background half of the open: after SetThreadConversation has rendered
// the viewport-covering initial set, the rest of the initial window
// materializes ABOVE it in kHydrateFillBeatRows-sized beats, one per
// dispatcher turn at LOW priority so input and rendering always go first.
//
// SILENT by rule: no fade, no marker, no entrance. The rows a beat inserts
// sit above the loaded content — off the viewport by construction (the
// initial set already covers the viewport plus headroom) — and the
// extent-delta correction below holds the reader's content exactly still, so
// there is nothing to animate. With motion off the beats are the same
// instant inserts (there was never any animation to cut), and the initial
// set's entrance is the only motion hydration has — RunBubbleEntrance's
// ShouldAnimate() gate already makes it instant.
//
// windowBusy is deliberately NOT set: a reader scrolling toward history
// mid-fill must get the scroll-triggered load IMMEDIATELY (StartEarlierLoad
// recomputes its range from the live window.start, so the two never
// double-materialize — the coalescing rule the "T9 hydrate coalesce" gate
// walks).

void RunHydrateBeat(std::shared_ptr<ThreadParts> const& parts, std::uint64_t gen);

void QueueHydrateBeat(std::shared_ptr<ThreadParts> const& parts) {
  // hydrateQueued makes the queue a CHAIN: each beat queues at most one
  // successor, so two chains can never race each other into the same rows.
  if (parts->hydrateQueued || !parts->stack) return;
  auto queue = parts->stack.DispatcherQueue();
  if (!queue) return;
  const std::uint64_t gen = parts->hydrateGeneration;
  // TryEnqueue returns false once the queue is shutting down — the flag is
  // set only on a real enqueue, so a teardown-time failure cannot wedge it.
  parts->hydrateQueued = queue.TryEnqueue(
      winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
      [parts, gen] { RunHydrateBeat(parts, gen); });
}

void RunHydrateBeat(std::shared_ptr<ThreadParts> const& parts, std::uint64_t gen) {
  parts->hydrateQueued = false;
  if (!parts->conv) return;
  // The beat's whole decision is pure (Views/ThreadLayout.h): the stale
  // generation dies unrescheduled HERE, and the done/overshot cursor stops
  // the chain the same way — the view spends only what the gate walked.
  const HydrateBeatPlan beat =
      PlanHydrateBeat(parts->window.start, parts->hydrateTarget, gen, parts->hydrateGeneration);
  if (!beat.run) return;

  const auto buildStart = std::chrono::steady_clock::now();
  const std::vector<ThreadRowPlan> plan = PlanThreadRows(*parts->conv);
  const double planMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
          .count();

  // The insert half of MaterializeEarlierChunk, mirrored — build the range,
  // insert above in order, splice the bookkeeping, correct the offset by the
  // measured EXTENT delta. The differences ARE the beat's definition: the
  // range comes from PlanHydrateBeat rather than SlideWindowUp, there is no
  // foot trim (hydrateTarget is the initial window's start, so the resident
  // count reaches 500 exactly — an ambient append racing the fill can push it
  // TRANSIENTLY past the cap, and PlanAmbientAppend's head trim takes it back
  // on the next arrival; a foot trim HERE would be the yank the fill exists
  // to avoid), and there is no marker and no fade. Keep the two in step —
  // they are two writers of one pattern (TrimWindowHead/TrimWindowFoot carry
  // the same warning).
  const std::size_t oldStart = parts->window.start;
  const std::size_t newStart = beat.newStart;
  const double offsetBefore = parts->scroller.VerticalOffset();
  const double extentBefore = parts->scroller.ExtentHeight();  // clean at entry

  std::vector<ThreadBubble> bornBubbles;
  std::vector<ThreadParts::RenderedRow> bornRows;
  bornRows.reserve(oldStart - newStart);
  uint32_t insertPos = 0;
  for (std::size_t i = newStart; i < oldStart; ++i) {
    BuiltRow b = BuildPlanRow(parts, *parts->conv, plan[i]);
    if (b.root) {
      parts->stack.Children().InsertAt(insertPos, b.root);
      ++insertPos;
    }
    if (b.bubble.root) bornBubbles.push_back(b.bubble);
    bornRows.push_back(
        ThreadParts::RenderedRow{parts->conv->rows[i], b.clusterHost, b.root != nullptr});
  }
  parts->rows.insert(parts->rows.begin(), std::make_move_iterator(bornRows.begin()),
                     std::make_move_iterator(bornRows.end()));
  parts->bubbles.insert(parts->bubbles.begin(), bornBubbles.begin(), bornBubbles.end());
  parts->window.start = newStart;
  SyncPublicBubbles(parts);

  // The 68% cap BEFORE the measure, for the same re-wrap reason
  // MaterializeEarlierChunk carries.
  ApplyColumnWidth(parts);
  parts->scroller.UpdateLayout();
  const double addedExtent = parts->scroller.ExtentHeight() - extentBefore;
  // The T8 second-writer rule: the pin decision after this beat must be
  // measured against the extent that exists NOW. For a reader pinned at the
  // foot the correction below lands exactly on the new foot (offset + added
  // == new scrollable), so the pin survives the fill without the
  // stack.SizeChanged handler ever seeing a stale extent.
  parts->pinExtent = parts->scroller.ScrollableHeight();
  parts->scroller.ChangeView(
      nullptr, ClampScrollOffset(OffsetAfterSlide(offsetBefore, 0.0, addedExtent), parts->pinExtent),
      nullptr, true);
  ReapplySelection(parts, bornBubbles);

  ++parts->hydrateBeats;
  if (beat.reschedule) {
    QueueHydrateBeat(parts);
    urnw::LogInfo(
        "thread: hydrate beat [{},{}) — {} rows, plan {:.2f} ms, beat {:.2f} ms",
        newStart, oldStart, oldStart - newStart, planMs,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
            .count());
  } else {
    // The window is fully resident. The total is measured from the END of the
    // synchronous phase (hydrateStart), so "sync + total" is the honest
    // before/after pair against the pre-hydration single-turn number.
    urnw::LogInfo(
        "thread: hydrate complete — window [{},{}) of {} filled in {:.2f} ms over {} beats "
        "(last beat {:.2f} ms)",
        parts->window.start, parts->window.end, parts->conv->rows.size(),
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  parts->hydrateStart)
            .count(),
        parts->hydrateBeats,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
            .count());
  }
}

}  // namespace

void SetThreadConversation(ThreadView& v, demo::Conversation const& c) {
  auto parts = Find(v.root);
  if (!parts) return;
  parts->owner = &v;

  // A rebuild retires any load beat mid-flight: the timer, the marker, and a
  // chunk fade whose rows are about to leave the tree.
  CancelEarlierLoad(parts);

  // The two timings the stress harness (--demo-stress=N) exists to read:
  // what the pure PLAN costs and what parenting the backlog costs, per row
  // count. Log-only, on every open/switch/refresh. The windowing wave (T8)
  // reads the same lines: with the window live, "backlog parented" is the
  // window's size, not the world's.
  const auto buildStart = std::chrono::steady_clock::now();

  // T8: where this set lands the window. A fresh open or a conversation switch
  // lands at the foot. Re-setting the SAME conversation is autoplay's
  // delivery-advance path (RefreshOpenThread), and PlanRefreshWindow decides:
  // a reader at the foot re-bases and pins — the pre-window behaviour, kept —
  // and a reader deep in history keeps their window, so the refresh cannot
  // yank them (design 9.2, carried across a rebuild).
  const std::size_t total = c.rows.size();
  RefreshWindowPlan placement{InitialWindow(total), true};
  double keepOffset = -1.0;  // >= 0: preserve the reader's exact position
  if (parts->convId == c.id && WindowActive(total) && parts->window.end != 0) {
    placement = PlanRefreshWindow(
        parts->window, total,
        ShouldPinToBottom(parts->pinArmed, parts->scroller.VerticalOffset(),
                          parts->pinExtent));
    if (!placement.pinToFoot) keepOffset = parts->scroller.VerticalOffset();
  }

  parts->stack.Children().Clear();
  parts->bubbles.clear();
  parts->rows.clear();
  v.bubbles.clear();
  parts->conv = &c;
  parts->convId = c.id;

  // T9: progressive hydration. On an open/switch (keepOffset < 0) of a
  // conversation OVER the window cap, only the viewport-covering INITIAL SET
  // renders synchronously; the rest of the initial window fills in background
  // beats (RunHydrateBeat). parts->window narrows to the initial set — it IS
  // the residency truth, so the render loop below, the beats and the
  // scroll-triggered slides all read the same [start, end) — and
  // hydrateTarget remembers the initial window's start for the fill. The
  // position-preserving refresh (keepOffset >= 0) renders the whole slice
  // synchronously as before: the reader's rows must ALL be there, that path
  // has no first-frame problem (the tree is already warm), and hydrating
  // around a mid-history anchor is a different rule than anchoring at the
  // foot.
  //
  // The generation bump is also the fill's CANCELLATION: any beat the
  // previous conversation queued arrives stale and dies unrescheduled in
  // PlanHydrateBeat. hydrateQueued is reset with it — a queued stale beat
  // must not suppress the NEW conversation's first beat (the flag gates
  // queueing only; the stale beat still fires and clears it again).
  ++parts->hydrateGeneration;
  parts->hydrateQueued = false;
  parts->hydrateBeats = 0;

  const bool group = (c.kind == demo::ConversationKind::Group);
  // Remembered because AppendThreadRow has no Conversation to ask later, and a
  // continuation bubble in a GROUP still reserves the identicon gutter.
  parts->group = group;

  // d1 §1.4: re-point the pane header at this conversation. Title in the
  // pane-title voice; the right-aligned muted meta follows the rail's subject
  // row (InspectRailView.cpp's MakeSubjectRow) — "Group · N members" (middle
  // dot) for a group, "Direct message" for a DM, so the two panes never
  // disagree. Fixture metadata only (G4): kind, name and members.size() are the
  // world's own, and members.size() is what the MEMBERS list draws.
  if (parts->headerTitle) parts->headerTitle.Text(winrt::hstring{c.name});
  if (parts->headerMeta)
    parts->headerMeta.Text(winrt::hstring{
        group ? (L"Group \u00B7 " + std::to_wstring(c.members.size()) +
                 L" members")                             // U+00B7 MIDDLE DOT
              : std::wstring{L"Direct message"}});

  // A conversation is open: the no-selection empty state collapses. It is built
  // Visible in MakeThread and this is its one writer, so it shows exactly when
  // no conversation is open and never alongside one.
  if (parts->emptyState) parts->emptyState.Visibility(Visibility::Collapsed);

  // The typing row aligns with the incoming bubble column (design d2 §5):
  // thread pad + the 36dip identicon gutter in a group, the pad alone
  // otherwise. Set here because this is where `group` is first known.
  if (parts->typingRow)
    parts->typingRow.Margin(ThicknessHelper::FromLengths(
        kThreadPadDip + (group ? kThreadGutterDip : 0.0), 0, 0, 6));

  // The builder renders the PLAN and chooses no SHAPE of its own. PlanThreadRows
  // lives in Views/ThreadLayout.h, which is pure C++, so every branch of the
  // build is reachable from --diagnose - a builder that classified rows inline
  // could only ever be checked by looking at a screenshot.
  //
  // Hoisted above the build so the plan's own cost is measurable apart from
  // the element build that consumes it (the stress-harness log lines at the
  // foot of this function) — and so the T9 sizing below can walk the plan's
  // row SHAPES, which is what the initial-set formula is computed from.
  const auto planStart = std::chrono::steady_clock::now();
  const std::vector<ThreadRowPlan> rowPlan = PlanThreadRows(c);
  const double planMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - planStart)
          .count();

  // T9: how much of the window renders SYNCHRONOUSLY. The viewport is real on
  // every switch/refresh path and 0 only on an unrealized tree, which
  // InitialViewportRows maps to its fallback — either way the decision is the
  // pure one the "T9 hydrate sizing" gate walks. Under the cap `hydrate` is
  // false and initialRows IS the window: the open is pixel-identical to the
  // pre-hydration path and no beat is ever queued.
  const double viewportDip = parts->scroller.ViewportHeight();
  const bool hydrate = keepOffset < 0.0 && WindowActive(total);
  const std::size_t windowRows = WindowRowCount(placement.window);
  const std::size_t initialRows =
      hydrate ? InitialViewportRows(viewportDip, rowPlan, placement.window) : windowRows;
  parts->window = placement.window;
  parts->window.start = parts->window.end - initialRows;
  parts->hydrateTarget = placement.window.start;

  // The loop renders the WINDOW'S SLICE of the full plan, [window.start,
  // window.end) — with T9, the INITIAL SET slice; the background beats widen
  // the window upward from there. At or under the cap the window IS the whole
  // conversation and this builds exactly what it did before T8; beyond it, the
  // plan still walks the full history so the seam row gets the sender header,
  // corners and cluster a full rebuild would give it (`T8 window seam` asserts
  // that agreement). BuildPlanRow is the one builder — the slides AND the fill
  // beats share it, so a row materialized later cannot drift from a fresh open.
  //
  // bubbleCount is the count the OPEN STAGGER sees (design d2 §8.2): the
  // rendered slice's bubble rows — only the last min(kMaxStaggerSteps, count)
  // animate, which is the visible foot either way.
  std::size_t bubbleCount = 0;
  for (std::size_t i = parts->window.start; i < parts->window.end; ++i)
    if (c.rows[i].kind == demo::RowKind::Message) ++bubbleCount;
  std::size_t bubbleIndex = 0;
  for (std::size_t i = parts->window.start; i < parts->window.end; ++i) {
    auto const& p = rowPlan[i];
    const BuiltRow built = BuildPlanRow(parts, c, p);
    if (built.root) parts->stack.Children().Append(built.root);
    if (built.bubble.root) {
      parts->bubbles.push_back(built.bubble);
      // design d2 §8.2: on OPEN only the visible foot animates — the last
      // min(kMaxStaggerSteps, count) bubble rows, kStaggerMs apart, newest
      // last. Begun synchronously HERE, not on a Loaded hook: every path that
      // reaches this build is already post-layout on a realized tree, so the
      // board always plays, and RunBubbleEntrance's Completed landing writes
      // the final pose back. SKIPPED on a position-preserving refresh
      // (keepOffset >= 0): the reader is deep in history and an entrance they
      // cannot see is noise — the refresh's only change is a delivery reading
      // at the foot.
      if (keepOffset < 0.0) {
        const int64_t stagger = OpenStaggerBeginMs(bubbleIndex, bubbleCount);
        if (0 <= stagger)
          RunBubbleEntrance(built.root, c.rows[p.rowIndex].outgoing, stagger);
      }
      ++bubbleIndex;
    }

    // EVERY rendered row, including the unlabelled separator that drew
    // nothing: the append path asks CarriesDeliveryGlyph() about the row
    // IMMEDIATELY above the one it is adding, and the T8 trims map rows to
    // stack children by `drawn`. Skipping a row here would hand both the
    // wrong one.
    parts->rows.push_back(
        ThreadParts::RenderedRow{c.rows[i], built.clusterHost, built.root != nullptr});
  }
  SyncPublicBubbles(parts);

  urnw::LogInfo(
      "thread: plan {} rows in {:.2f} ms; backlog parented ({} bubbles, window [{},{})) in {:.2f} ms",
      c.rows.size(), planMs, bubbleIndex, parts->window.start, parts->window.end,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
          .count());

  ApplyColumnWidth(parts);
  // THIS PAIR IS INERT ON THE CONSTRUCTOR PATH, and it is not what opens a
  // thread at its newest row. BuildDemoViews builds the view from the window
  // constructor, before ApplyBreakpoint has made ThreadHost visible and
  // before any layout pass; UpdateLayout() on a collapsed host measures
  // nothing, ScrollableHeight() is 0, and this ChangeView lands on offset 0.
  // The bottom pin in MakeThread (stack.SizeChanged) is what actually puts
  // the thread at its foot there - deleting that pin because
  // "SetThreadConversation already does this" regresses the surface straight
  // back to opening on "Yesterday" with the newest rows a viewport below the
  // fold, which is a bug that was already shipped once and caught in a
  // capture.
  //
  // And it is NOT inert on the paths W5/W9 added: SelectConversation and
  // RefreshOpenThread re-point an already-measured column at a conversation,
  // where ScrollableHeight is real and this is the jump to the foot.
  // disableAnimation is true because it is a jump to a position, not a motion
  // the user asked for.
  // Nothing is selected in a freshly built thread, so this settles every bubble
  // onto its RESTING edge. It reaches that edge through SetBubbleEdge, the same
  // single writer MakeBubbleRow used when it built each bubble — one function
  // with two callers, not two functions writing the same two properties, which
  // is how an edge goes stale. Spec C §5.2: a 1px UrBorderBrush edge on an
  // outgoing bubble, none on an incoming one.
  SetThreadSelectedMessage(v, L"");

  parts->scroller.UpdateLayout();
  // The jump to the foot IS a pin landing, so it is recorded like one: the
  // next stack.SizeChanged measures the reader against THIS extent
  // (pinExtent's rule, stated on the member), not against a stale one from
  // before the rebuild - which on a conversation switch would be a different
  // thread's extent entirely. The position-preserving refresh (T8) also lands
  // here: it is not a jump, but it re-based the loaded extent, so pinExtent
  // follows it for the same reason. On the constructor path ScrollableHeight
  // is 0 and both writes are as inert as the ChangeView below them.
  parts->pinArmed = true;
  parts->pinExtent = parts->scroller.ScrollableHeight();
  if (keepOffset < 0.0) {
    parts->scroller.ChangeView(nullptr, parts->scroller.ScrollableHeight(), nullptr, true);
  } else {
    // The position-preserving refresh (T8): the same window slice re-rendered
    // with the same rows, so the extent is the same and the reader's offset is
    // still valid — restore it, clamped, instead of jumping to the foot. This
    // is the difference between autoplay's delivery advance and a yank.
    parts->scroller.ChangeView(
        nullptr, ClampScrollOffset(keepOffset, parts->scroller.ScrollableHeight()), nullptr,
        true);
  }

  // T9: the synchronous phase ends HERE — everything the first presented
  // frame of this thread needs has been issued (plan, initial-set parent,
  // measure/arrange, foot pin). That is the number the owner feels on a
  // switch, and the honest before/after against the pre-hydration single-turn
  // open: the fill below is background by construction.
  const double syncMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart)
          .count();
  if (HydrateFillActive(parts->window.start, parts->hydrateTarget)) {
    parts->hydrateStart = std::chrono::steady_clock::now();
    QueueHydrateBeat(parts);
    urnw::LogInfo(
        "thread: sync phase {:.2f} ms (plan {:.2f}); initial {} of {} window rows at {:.0f} "
        "dip viewport — fill of {} rows queued (target {})",
        syncMs, planMs, initialRows, windowRows, viewportDip,
        parts->window.start - parts->hydrateTarget, parts->hydrateTarget);
  } else {
    // The default path states itself in the log too: at/under the cap, on a
    // position-preserving refresh, or when the viewport covers the window,
    // the initial set IS the window and no beat is ever queued.
    urnw::LogInfo(
        "thread: sync phase {:.2f} ms (plan {:.2f}); initial {} of {} window rows at {:.0f} "
        "dip viewport — no fill",
        syncMs, planMs, initialRows, windowRows, viewportDip);
  }
}

// Contract §4. Selection is a PROPERTY WRITE, never a synthesized click: the
// --demo=inspect deep link writes it after the first layout pass, MakeThread's
// onSelectMessage callback writes it on a real click, and the rail task will
// write it too. One function owns the edge, so there is no second opinion about
// what a bubble looks like at rest.
//
// v.bubbles is the contract's own list, so this reads no private state and needs
// no registry lookup — which is also why it works on a ThreadView copy.
void SetThreadSelectedMessage(ThreadView& v, std::wstring const& id) {
  // The one writer of parts->selectedId: the T8 slides re-apply it to a row
  // the window re-covers, so the outline survives a trim-and-return.
  if (auto parts = Find(v.root)) parts->selectedId = id;
  std::vector<std::wstring> ids;
  ids.reserve(v.bubbles.size());
  for (auto const& b : v.bubbles) ids.push_back(b.id);
  const int selected = SelectedBubbleIndex(ids, id);

  for (std::size_t i = 0; i < v.bubbles.size(); ++i) {
    auto const& b = v.bubbles[i];
    if (!b.root) continue;
    const bool on = (static_cast<int>(i) == selected);
    // Direction comes from the ALIGNMENT, which Spec C §5.2 fixes and which
    // MakeBubbleRow sets as a LOCAL value (so no Style setter can win it back).
    // ThreadBubble is fixed by contract §4 and cannot grow a field to carry it.
    const bool outgoing = (b.root.HorizontalAlignment() == HorizontalAlignment::Right);

    // Three channels, the same rule SetPaneListRowSelected already follows
    // (UrComponents.h): the accent OUTLINE (the SelectEdge layer, drawn on over
    // kMicroMs — a SHAPE appearing, not a colour swap), and the automation
    // name. The outline is SetBubbleEdge's — the same writer MakeBubbleRow
    // used to build this bubble's resting edge, so there is exactly one place
    // that decides what a bubble's border is.
    SetBubbleEdge(b.root, outgoing, on);

    // The name is a channel too, and it is idempotent: the suffix is stripped
    // before it is re-applied, so calling this twice cannot leave
    // "..., selected, selected" behind.
    auto name = Automation::AutomationProperties::GetName(b.root);
    std::wstring base{name};
    const std::wstring mark = L", selected";
    if (base.size() >= mark.size() &&
        base.compare(base.size() - mark.size(), mark.size(), mark) == 0)
      base.erase(base.size() - mark.size());
    Automation::AutomationProperties::SetName(b.root, winrt::hstring{on ? base + mark : base});
  }
}

// Contract §4. design §7's typing indicator, as a state rather than as an
// animation: the ROW is what says someone is typing, and the wave is how it
// says it when the machine is allowed to move.
//
// The reduce-motion path is not a shorter wave, it is NO wave: the dots are
// left at full opacity (not the 0.30 they rest at, which would read as three
// dots frozen mid-fade) and the word beside them carries the state on its own.
// TypingTimelineCount() is the pure statement of that — kTypingDots timelines
// with motion on, ZERO with it off — and --diagnose asserts both halves.
void SetThreadTyping(ThreadView& v, bool typing) {
  auto parts = Find(v.root);
  if (!parts || !parts->typingRow) return;
  parts->typingRow.Visibility(typing ? Visibility::Visible : Visibility::Collapsed);

  // Stopped and dropped FIRST, on every call: Begin() on a second storyboard
  // targeting the same three Opacity properties leaves two waves fighting over
  // them, and the loser never stops because RepeatBehavior is Forever.
  if (parts->typingStory) {
    parts->typingStory.Stop();
    parts->typingStory = nullptr;
  }

  // The ROW's own entrance (design d1 motion M3): fade + a 4dip rise at
  // kFastMs on the standard curve, as ONE one-shot storyboard targeting the
  // row's Opacity and TranslateY — never the dots' properties, so it cannot
  // fight their wave. Exit is the instant collapse the Visibility write above
  // already performs (one step faster than the kMicroMs floor).
  if (parts->typingRowStory) {
    parts->typingRowStory.Stop();
    parts->typingRowStory = nullptr;
  }
  if (typing && urnw::motion::ShouldAnimate()) {
    Media::CompositeTransform rise;
    rise.TranslateY(urnw::motion::kDist4);
    parts->typingRow.RenderTransform(rise);
    parts->typingRow.Opacity(0.0);
    anim::Storyboard sb;
    auto fade = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kFastMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(fade, parts->typingRow);
    anim::Storyboard::SetTargetProperty(fade, L"Opacity");
    sb.Children().Append(fade);
    auto lift = urnw::motion::MakeSplineDouble(urnw::motion::kDist4, 0.0,
                                               urnw::motion::kFastMs, 0,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(lift, parts->typingRow);
    anim::Storyboard::SetTargetProperty(
        lift, L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)");
    sb.Children().Append(lift);
    parts->typingRowStory = sb;
    sb.Begin();
  } else {
    // Motion GONE, not reduced — or the row is going away: the final pose,
    // immediately, and no residual transform for a later layout pass to trip
    // over (the same rule RunBubbleEntrance's empty-plan branch follows). The
    // reduce-motion half of this branch has never executed on this machine
    // (SPI_GETCLIENTAREAANIMATION = 1) — unverified beyond code inspection.
    parts->typingRow.Opacity(1.0);
    parts->typingRow.RenderTransform(nullptr);
  }

  // The same table --diagnose asserts, and one call to the reduce-motion gate.
  const auto plan = typing ? TypingTimelines(urnw::motion::ShouldAnimate())
                           : std::vector<TimelineSpec>{};

  // THE RESTING VALUE, and it is not one number. With motion off the dots do
  // not move at all, so they must read as three SOLID dots beside the word —
  // 0.30 there would be three dots frozen mid-fade. With motion on they must
  // rest at 0.30, which is where every timeline STARTS: dots 2 and 3 sit out a
  // 140 / 280 ms BeginTime before their own timeline takes over, and leaving
  // them at 1.0 for that made them POP 1.0 -> 0.30 as their turn arrived, which
  // is the opposite of the wave the offset exists to draw.
  const bool animating = !plan.empty();
  for (auto const& d : parts->typingDots) d.Opacity((typing && !animating) ? 1.0 : 0.30);
  if (!animating) return;

  anim::Storyboard sb;
  for (std::size_t i = 0; i < plan.size() && i < parts->typingDots.size(); ++i) {
    auto const& spec = plan[i];
    // Half a cycle out, AutoReverse back = kPulseMs per dot, offset by design
    // §7's 140 ms so the three read as a wave rather than as one blink.
    auto a = urnw::motion::MakeSplineDouble(spec.from, spec.to, urnw::motion::kPulseMs / 2,
                                            spec.beginMs, urnw::motion::kStandardP1,
                                            urnw::motion::kStandardP2);
    if (spec.autoReverse) a.AutoReverse(true);
    if (spec.forever) a.RepeatBehavior(anim::RepeatBehaviorHelper::Forever());
    anim::Storyboard::SetTarget(a, parts->typingDots[i]);
    anim::Storyboard::SetTargetProperty(a, spec.path);
    sb.Children().Append(a);
  }
  parts->typingStory = sb;
  sb.Begin();
}

// Contract §4. Ambient activity's one entry point (design §9.2): ONE row,
// arriving at the foot of a column that is already built.
//
// TWO ROWS CHANGE, NOT ONE, and the second is the whole difficulty. The row
// being appended is the newest, so its `next` is nullptr and
// CarriesDeliveryGlyph() gives it a cluster whenever it is outgoing. But the
// row that WAS newest was judged under the same rule with the same nullptr, and
// an outgoing one got a cluster for being last of its run — which it now is
// not. Leave it alone and the run shows TWO readings, which design §6.2 gives
// exactly one of, and which every earlier draft of this function did.
//
// Not "clear the previous cluster" either: CarriesDeliveryGlyph fires on any
// Failed row wherever it sits, so a Failed row KEEPS its cluster when something
// lands under it. The rule is re-EVALUATED, and PlanAppendCluster
// (Views/ThreadLayout.h) is that re-evaluation as pure data, so --diagnose can
// assert it without an apartment. `T6 append cluster` is the gate.
void AppendThreadRow(ThreadView& v, demo::MessageRow const& row) {
  auto parts = Find(v.root);
  if (!parts || !parts->stack) return;
  parts->owner = &v;

  // T8: whether the arrival RENDERS is a window question first. The caller
  // pushes the row into the world BEFORE calling (MainWindow.xaml.cpp — "any
  // later rebuild still has it"), so its index is rows.size() - 1, and
  // PlanAmbientAppend renders it exactly when the window covers the foot.
  // Deep in history: NO tree change — no yank, nothing materialized below the
  // fold. The row is a world row; the window re-covers it on the slide home.
  AmbientAppendPlan ap{true, parts->window};
  if (parts->conv) {
    ap = PlanAmbientAppend(parts->window, parts->conv->rows.size() - 1);
    if (!ap.render) {
      urnw::LogInfo(
          "thread: ambient row {} deferred — window [{},{}) of {} does not cover it",
          urnw::Narrow(row.id), parts->window.start, parts->window.end,
          parts->conv->rows.size());
      return;
    }
  }

  // ---- 1. the row above, re-decided --------------------------------------
  demo::MessageRow const* prev = parts->rows.empty() ? nullptr : &parts->rows.back().row;
  const AppendClusterPlan plan = PlanAppendCluster(prev, row);
  if (prev) SetRowCluster(parts->rows.back().clusterHost, *prev, plan.prevCarriesNow);

  // The row above's CORNERS are re-decided too. Its runPos was planned with
  // next == nullptr (Single/Last), and a message landing under it can make it
  // First/Middle — the spine corner facing the new bubble tightens 12 -> 4.
  // Same "re-ask the row above" discipline as the cluster, for the same
  // reason: incremental must equal rebuild, and a rebuild would draw the
  // tightened corner. The --demo-autoplay seed exercises this live — its
  // outgoing row lands under c0's 12:11 Pending bubble, a Last that becomes
  // a Middle. bubbles.back() IS prev's bubble exactly when prev is a Message
  // row (bubbles records message rows only, in order).
  if (prev && prev->kind == demo::RowKind::Message && !parts->bubbles.empty()) {
    demo::MessageRow const* prevPrev =
        (2 <= parts->rows.size()) ? &parts->rows[parts->rows.size() - 2].row : nullptr;
    double corners[4];
    BubbleCornerDip(RunPosFor(prevPrev, *prev, &row), prev->outgoing, corners);
    parts->bubbles.back().root.CornerRadius(CornerRadiusFromCorners(corners));
  }

  // ---- 2. the new row ----------------------------------------------------
  FrameworkElement added{nullptr};
  FrameworkElement clusterHost{nullptr};
  switch (row.kind) {
    case demo::RowKind::DaySeparator: {
      // Same exception SetThreadConversation makes: an unlabelled separator
      // draws NOTHING rather than an empty pill.
      const std::wstring label = DaySeparatorLabel(row);
      if (!label.empty()) added = MakeDaySeparator(label);
      break;
    }
    case demo::RowKind::System:
      added = row.permanentRecord ? MakeKeyChangeRecord(winrt::hstring{row.systemText})
                                  : MakeSystemLine(winrt::hstring{row.systemText});
      break;
    case demo::RowKind::Message: {
      // THE RULE, not a guess. An earlier version hard-coded false here on the
      // grounds that "an arriving message has no conversation around it" — but
      // it does: `prev` is the row this function just re-decided the cluster
      // for, and parts->group was recorded by SetThreadConversation for exactly
      // this call. ShowsSenderHeader() is the app's ONE definition of "starts a
      // run" for the sender name, the same one PlanThreadRows delegates to.
      //
      // It matters because showSenderHeader gates BOTH the sender name and the
      // identicon: an ambient INCOMING message that starts a run in a group
      // rendered with neither, while a full rebuild of the same conversation
      // showed both. That is the incremental-equals-rebuild property this
      // surface is gated on, broken in an attribute the gate was not looking
      // at — so `T6 append cluster` now compares the header per row too.
      const bool showSenderHeader = ShowsSenderHeader(prev, row, parts->group);
      // The newest row's runPos is judged with next == nullptr: it can only
      // CONTINUE the run above (Last) or stand alone (Single). If it
      // continues, the re-corner above has already tightened the row over it.
      auto built = MakeBubbleRow(row, parts->group, showSenderHeader,
                                 plan.appendedCarries, RunPosFor(prev, row, nullptr));
      built.bubble.root.Click([parts, id = row.id](auto const&, auto const&) {
        if (parts->onSelect) parts->onSelect(id);
      });
      parts->bubbles.push_back(built.bubble);
      added = built.root;
      clusterHost = built.root;
      break;
    }
  }

  // Recorded even when it drew nothing — see SetThreadConversation.
  parts->rows.push_back(ThreadParts::RenderedRow{row, clusterHost, added != nullptr});

  // T8: the cap. The window grew by one at the foot, so a row leaves at the
  // HEAD — silent and offset-corrected, exactly like a slide's head trim.
  const std::size_t trim = ap.after.start - parts->window.start;
  if (0 < trim) {
    // The pin question is asked BEFORE the trim: the head trim's layout pass
    // clamps an at-foot reader's offset down by itself, so asking afterwards
    // would double-count the correction.
    const bool pinned = ShouldPinToBottom(parts->pinArmed,
                                          parts->scroller.VerticalOffset(), parts->pinExtent);
    const double extentBefore = parts->scroller.ExtentHeight();  // clean layout
    TrimWindowHead(parts, trim);
    parts->scroller.UpdateLayout();
    const double removedExtent = extentBefore - parts->scroller.ExtentHeight();
    if (!pinned) {
      // Not at the foot: hold the reader's content exactly still across the
      // trim. At the foot the stack.SizeChanged pin is the right landing and
      // fires on the natural layout pass after this function — two writers of
      // one offset, and the pin's is the correct one there.
      parts->scroller.ChangeView(
          nullptr,
          ClampScrollOffset(
              OffsetAfterSlide(parts->scroller.VerticalOffset(), removedExtent, 0.0),
              parts->scroller.ScrollableHeight()),
          nullptr, true);
    }
  }
  parts->window = ap.after;
  SyncPublicBubbles(parts);
  if (!added) return;

  parts->stack.Children().Append(added);
  // The 68% cap is a function of the CURRENT column width, and the walk in
  // ApplyColumnWidth only ever runs on a resize; a bubble appended between two
  // resizes would otherwise keep MakeBubbleRow's unmeasured 640 DIP fallback.
  ApplyColumnWidth(parts);
  // No stagger on append (design d2 §8.2 staggers the OPEN only): a single
  // arriving row plays its entrance at once, from its own side.
  RunBubbleEntrance(added, row.outgoing, 0);

  // NO SCROLL WRITE HERE, deliberately. MakeThread's stack.SizeChanged pin
  // already fires on this append — a new child changes the stack's height — and
  // it re-pins when ShouldPinToBottom says the reader is at the foot, which is
  // what keeps the arriving message visible. A second ChangeView from here
  // would be a second writer of one property, and the one that ran last would
  // win by accident of ordering. Design 9.2's do-not-yank rule lives on that
  // pin (W9): a reader who scrolled away is not followed down, and the four
  // cases of the pure decision are asserted in --diagnose.
}

}  // namespace urmsg::views
