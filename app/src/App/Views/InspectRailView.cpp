// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/InspectRailView.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// BOTH automation headers, in this order, exactly as UrComponents.cpp:9-10 does
// it. AccessibilityView lives in the Peers projection; whether it reaches this
// file transitively through the other one is a cppwinrt layout detail and not
// something to bet a build on.
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include "Identicon.h"
#include "Log.h"
#include "UrColors.h"
#include "UrMotion.h"
#include "Views/InspectRailFields.h"

// ---- ONE key/value call site in this file, enforced by the build (R3) ------
//
// THE RULE. AppendFieldRows below is the only function in this file that may
// call kit::MakePaneKeyValueRow, and the poison enforces it: the identifier is
// defined to a name that does not exist, so a call written outside the funnel
// fails to compile and the error names the funnel.
//
// WHY IT IS WORTH ENFORCING. AppendFieldRows routes every rail value through
// RailValueOr, which draws a blank as one em dash. Appending a row with
// `kit::MakePaneKeyValueRow(H(field.key), H(field.value), 34)` instead skips
// that. This is not hypothetical and it is not a style point: it is the line
// task R3 was briefed with, and message mode is the surface that holds the
// world's blank values - InspectRailFieldsProbe counts them and gates their
// shape - so that one line renders empty cells while the static_asserts below
// stay true, the build stays clean and --diagnose stays green. Task R2 measured
// the bypass and reported that nothing it could write would catch it, because
// the second mode did not exist yet.
//
// THE LIMIT. The poison is lifted for one window around the funnel, and that
// window is wider than the call: a bypassing call added inside AppendFieldRows,
// or a helper appended immediately after that function, lands un-poisoned. The
// second case is the likely one - it is where a person adding a sibling
// naturally types - so the end of the window is marked in place, and new
// functions belong after the marker.
//
// It is also per-FILE and PREPROCESSOR-level: it sees an identifier, not a call
// graph. A rail row built by a helper that calls the kit from ANOTHER file would
// pass this. That is the failure mode to watch, and nothing mechanical here
// catches it - only reading a diff does.
//
// AND THE TRADEOFF, SO A READER CAN ACTUALLY JUDGE IT. Where a builder has few
// callers, cheaper enforcement is available: a reviewer reading the diff catches
// the bypass as reliably as the macro does, and a comment would have cost
// nothing. The macro was chosen because the bypass is not hypothetical, and
// because its value is on the day someone edits this file without the brief in
// front of them - when the plan's other pane surfaces have callers of their own
// and this file is no longer obviously the only one. It is not free: a
// preprocessor device is a surprising thing to meet in this codebase, and
// surprise is a real cost.
//
// This comment carries no census on purpose. A comment is not re-run, and the
// command below would count these lines among its own hits. For the current
// picture, run it:
//
//     git grep -n "MakePaneKeyValueRow" -- app/
#define MakePaneKeyValueRow MakePaneKeyValueRow_bypasses_RailValueOr_use_AppendFieldRows

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace kit = urnw::kit;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;

namespace urmsg::views {
namespace {

// std::wstring -> hstring is EXPLICIT, and the kit builders take
// `winrt::hstring const&`, so every std::wstring crossing into the kit goes
// through one named adapter rather than through forty hstring{...} at call
// sites. This is the same adapter MainWindow.xaml.cpp names Loc().
winrt::hstring H(std::wstring const& value) { return winrt::hstring{value}; }

// ---- a blank value is one glyph, never a missing row -----------------------
//
// U+2014 EM DASH, written as an escape rather than as a literal character so the
// meaning of this file does not depend on its encoding surviving a checkout.
//
// WHY IT IS HERE AND NOT IN InspectRailFields.h. The demo fixture cannot fill
// every field: Demo/DemoWorld.cpp gives a DIRECT conversation an empty
// groupIdHex (:207) and copies it onto every message in it (:173), and it writes
// receivedAtLabel only once a device has said so (:177-188), which is correct -
// nothing has been received until one has. Swept over the fixture that is 28
// blank values across the world's message rows, and InspectRailFields.h's probe
// GATES on every blank being one of exactly those two shapes. Substituting in
// the field model would drive that count to zero and turn a working detector
// into a gate that cannot fail, so the substitution belongs to the VIEW: the
// model still reports "there is nothing here", and the view decides that
// "nothing here" is drawn as one em dash.
//
// AN EM DASH AND NOT A SKIPPED ROW. Skipping makes the row count
// data-dependent - the eight-then-four message shape is read BY POSITION
// downstream - and a "Received" row with a dash in it tells a reader more than
// an absent row does.
//
// THE LIMIT: NOTHING RENDERED SO FAR REACHES THE SUBSTITUTING BRANCH. The
// blanks live in message mode - InspectRailFieldsProbe is what counts them and
// gates their shape - but neither kind is reachable from what can be opened
// today, for a structural reason rather than a lucky one:
//
//   * conversation mode at the default density draws only the retention rows,
//     and DemoWorld fills those on every conversation.
//   * the "Group id" blanks are ADVANCED-density rows on a DIRECT conversation.
//     --demo=inspect opens conversation 0, which is a GROUP with a populated
//     groupIdHex, and the rail's density stays false until R4 defines
//     SetInspectRailAdvanced - so neither half of that pair is reachable yet.
//   * the "Received" blanks are outgoing rows nothing has received. The only row
//     --demo=inspect can land on is PickInspectMessage's, which is by definition
//     an outgoing row in state Read, and a Read row has a receivedAtLabel.
//     Reaching a Pending or Failed row means CLICKING a bubble, and the thread's
//     onSelectMessage seam is not wired yet.
//
// So the branch is covered by the static_asserts below - a locally built
// adversarial input, the same device InspectRailFields.cpp uses for ShortHex's
// unreachable truncating branch - and NOT by any screenshot yet taken, which
// shows no blank row because there is no blank value in it to show. What the
// poison adds is the guarantee that when a render finally does reach a blank, it
// comes through here: see the call-site block under this file's includes.
constexpr wchar_t const* kBlankValue = L"\u2014";

// The one funnel. Every key/value row the rail draws is built from the result of
// this function (AppendFieldRows below is the file's only MakePaneKeyValueRow
// call site), so a mode that forgets the placeholder cannot exist.
constexpr std::wstring_view RailValueOr(std::wstring_view value) {
  return value.empty() ? std::wstring_view{kBlankValue} : value;
}

// Both directions, because only one of them fails for each of the two ways this
// can be broken: deleting the substitution trips the first, and substituting
// unconditionally trips the second.
static_assert(RailValueOr(L"") == std::wstring_view{L"\u2014"},
              "a blank value must render as one em dash, not as an empty row");
static_assert(RailValueOr(L"Kept until deleted") == std::wstring_view{L"Kept until deleted"},
              "a value that IS present must reach the row unchanged");

// ---- and the same blank, SPOKEN -------------------------------------------
//
// An em dash is the right mark for the eye and the wrong one for the ear: a
// screen reader on the row above reads "Received, em dash", which is the glyph
// and not the fact. So the row is told the spoken value separately, through
// MakePaneKeyValueRow's accessibleValue parameter.
//
// AT THE KIT, NOT AT THE CALL SITE, and that is the point of the parameter
// existing at all. The row composes its own automation name from key and value;
// overwriting that name here would make this file a SECOND writer of one
// property, which is the exact defect an earlier fix round in this project had
// to undo. One writer, told what to say.
//
// "not set" and not "none", "empty" or "unknown": the model reported that the
// fixture has nothing to put here, which is neither a zero nor a mystery. It
// also stays true of both shapes the blank actually takes - a direct
// conversation that has no group id, and an outgoing row no device has received.
constexpr wchar_t const* kBlankSpoken = L"not set";

constexpr std::wstring_view RailSpokenValueOr(std::wstring_view value) {
  return value.empty() ? std::wstring_view{kBlankSpoken} : value;
}

// Both directions again, and for the same two reasons: dropping the
// substitution reads the glyph aloud, substituting unconditionally announces
// every populated field as "not set".
static_assert(RailSpokenValueOr(L"") == std::wstring_view{L"not set"},
              "a blank value must be SPOKEN as words, never as the em dash glyph");
static_assert(RailSpokenValueOr(L"Kept until deleted") == std::wstring_view{L"Kept until deleted"},
              "a value that IS present must be spoken unchanged");

// AND THE TWO MUST DISAGREE, or the parameter buys nothing: the pair above is
// satisfied whole by making RailSpokenValueOr a second name for RailValueOr.
//
// THIS CLAUSE IS IMPLIED BY THE TWO BELOW, AND IS KEPT ANYWAY - said plainly,
// because a clause kept for a reason that does not check out is the dead-gate
// shape this project has shipped over and over. IsSpeakable(spoken) and
// !IsSpeakable(drawn) use ONE predicate, so a true result and a false result
// cannot come from equal strings: they already entail this inequality. There is
// NO mutation this clause catches alone.
//
// It stays because it says the point DIRECTLY, in one line, at the definition,
// where the two-step inference through IsSpeakable does not read as obviously.
// It is documentation the compiler checks, NOT an independent gate. Do not count
// it as one.
static_assert(RailValueOr(L"") != RailSpokenValueOr(L""),
              "the spoken blank must differ from the drawn one, or the parameter buys nothing");

// AND DIFFERENT IS NOT ENOUGH. Set kBlankSpoken to L"\u2013" (EN dash) and edit
// the first assert's expectation to match: every clause above passes, and the
// row ships "Received, en dash" - the exact defect, one code point along.
// "Differs from the em dash" was never the property; "is words" was. So that is
// what is asserted.
constexpr bool IsSpeakable(std::wstring_view s) {
  if (s.empty()) return false;
  for (wchar_t c : s)
    if (!((L'a' <= c && c <= L'z') || (L'A' <= c && c <= L'Z') || c == L' ')) return false;
  return true;
}

// THE LIMIT OF THIS ONE, STATED BEFORE IT IS BELIEVED. It enforces "letters and
// spaces only", which is a PROXY for speakable, and it rejects every punctuation
// mark - every dash, bullet, ellipsis and arrow anyone might reach for. It does
// NOT reject a pronounceable non-word: "xyzzy" passes. Nothing mechanical here
// catches that, and a predicate that pretended to would be another gate that
// cannot fail - the same limit, and the same reason, as ReadsAsVerified in
// InspectRailFields.cpp.
//
// THESE TWO ARE THE GATE. Between them they entail the inequality asserted
// above, and they catch what it cannot: the en dash trips the first, and both
// funnels collapsing onto one speakable string trips the second. Of these three
// clauses, these are the two that can fail on their own.
static_assert(IsSpeakable(RailSpokenValueOr(L"")),
              "the spoken blank must be WORDS - a punctuation mark read aloud is the defect");
static_assert(!IsSpeakable(RailValueOr(L"")),
              "and the DRAWN blank must still be the glyph, or these two are one string");

// ---- the rail's state, parked on its own elements --------------------------
//
// FrameworkElement::Tag is an IInspectable slot the platform never touches, so
// it carries the rail's subject and density without a file-static that a second
// rail would silently share. IDS, never pointers - see the header.

void SetRailAdvanced(InspectRailView const& v, bool advanced) {
  if (v.root) v.root.Tag(winrt::box_value(advanced));
}

bool RailAdvanced(InspectRailView const& v) {
  return v.root && winrt::unbox_value_or<bool>(v.root.Tag(), false);
}

void SetRailSubject(InspectRailView const& v, std::wstring const& conversationId,
                    std::wstring const& messageId) {
  if (v.conversationScroll)
    v.conversationScroll.Tag(winrt::box_value(winrt::hstring{conversationId}));
  if (v.messageScroll) v.messageScroll.Tag(winrt::box_value(winrt::hstring{messageId}));
}

std::wstring TagId(FrameworkElement const& element) {
  if (!element) return {};
  auto tag = element.Tag().try_as<winrt::hstring>();
  return tag ? std::wstring{*tag} : std::wstring{};
}

// For the caller that re-resolves the rail's subject at render time.
//
// CORRECTED BY R3: R2 wrote "message mode is the one that needs it". Message
// mode exists now and does NOT - SetInspectRailMessage is handed both the
// Conversation and the MessageRow, and stores their IDS precisely so it never
// has to hold those references. The reader these were waiting for is
// SetInspectRailAdvanced (R4, now landed), which re-populates whichever mode
// is already showing and is handed no subject at all: it has only these two
// tags and FindConversation / FindMessageRow.
std::wstring RailConversationId(InspectRailView const& v) {
  return TagId(v.conversationScroll);
}
std::wstring RailMessageId(InspectRailView const& v) {
  return TagId(v.messageScroll);
}

// The StackPanel inside a body scroller. The fixed contract gives the view
// struct its fields and no body fields, so the panel is fetched rather than
// cached - which is also one fewer thing that can go stale.
StackPanel BodyOf(ScrollViewer const& scroller) {
  return scroller ? scroller.Content().try_as<StackPanel>() : nullptr;
}

// The section cascade (design d4 §12.2): on a REAL mode swap, the incoming
// body's top-level children (the subject row, the captions, the cards) fade
// 0->1 at kBaseMs, BeginTime i * kStaggerMs capped at kMaxStaggerSteps - the
// conversation list already staggers on entrance, so this is a second use of
// an established rhythm, not a new one. It COMPOSES with the scroller-level
// fade rather than replacing it: the swap owns the visibility/collapse
// bookkeeping.
//
// Density re-population never reaches here (it does not call PresentMode at
// all): re-rendering an on-screen surface must stay silent.
//
// UNVERIFIED BRANCH, STATED: ShouldAnimate() has never returned false on this
// machine (SPI_GETCLIENTAREAANIMATION = 1), so the early return is
// code-inspection only; with motion off the children are simply left at
// Opacity 1.
void CascadeSections(ScrollViewer const& scroller) {
  if (!urnw::motion::ShouldAnimate()) return;
  auto panel = BodyOf(scroller);
  if (!panel) return;
  auto children = panel.Children();
  const uint32_t count = children.Size();
  namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
  anim::Storyboard sb;
  for (uint32_t i = 0; i < count; ++i) {
    auto element = children.GetAt(i).try_as<FrameworkElement>();
    if (!element) continue;
    const int64_t step = std::min<int64_t>(i, urnw::motion::kMaxStaggerSteps);
    auto fade = urnw::motion::MakeSplineDouble(0.0, 1.0, urnw::motion::kBaseMs,
                                               step * urnw::motion::kStaggerMs,
                                               urnw::motion::kStandardP1,
                                               urnw::motion::kStandardP2);
    anim::Storyboard::SetTarget(fade, element);
    anim::Storyboard::SetTargetProperty(fade, L"Opacity");
    sb.Children().Append(fade);
  }
  sb.Begin();
}

// A crossfade ONLY when the mode actually changes.
//
// CrossfadePageSwap(outgoing, incoming) sets incoming.Opacity(0) and fades it
// back over kBaseMs (UrMotion.cpp:86-95). That is right for a real swap and
// wrong for anything else: called with an already-collapsed outgoing it blanks
// the VISIBLE body and re-fades it, and called twice in one synchronous block it
// drives two storyboards over the same two elements in opposite directions and
// the end state is a race. So the outgoing is passed only when it is actually on
// screen; otherwise `incoming` is passed as its own outgoing, which
// UrMotion.cpp:123-127 documents as the instant, no-storyboard path.
//
// A crossfade, never a slide: the column's width is fixed, and a slide inside a
// fixed-width column implies a navigation that has not happened (design 6.3).
// The swap is already gated on motion::ShouldAnimate(), so "animations
// off" makes this an instant, correct swap rather than a slow one.
//
// The settle and the cascade ride only a REAL swap (design d4 §12): the first
// present has no outgoing and the WindowReveal already covers that entrance -
// a rail that also faded itself in would be a second animation over the same
// tree.
void PresentMode(InspectRailView const& v, bool messageMode) {
  auto incoming = messageMode ? v.messageScroll : v.conversationScroll;
  auto outgoing = messageMode ? v.conversationScroll : v.messageScroll;
  if (!incoming) return;
  const bool swapping = outgoing && outgoing.Visibility() == Visibility::Visible;
  urnw::motion::CrossfadePageSwap(swapping ? outgoing : incoming, incoming);
  if (!swapping) return;
  urnw::motion::SettleIn(incoming);
  CascadeSections(incoming);
}

ScrollViewer MakeBodyScroller() {
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  // Bottom clearance (polish B2): ScrollViewer bottom padding is a VIEWPORT
  // inset on this platform (measured: 40 moved the rest-state cut up 50px,
  // leaving a dead band), so it does two jobs here. At rest it lands the
  // 900dip window's cut mid-row on the DELIVERED TO card - a half-shown
  // device row is the "there is more" affordance the flush cut did not have.
  // Scrolled to the end, the last READ BY row clears the status strip by the
  // same 12 (the B1 Developer/Settings precedent at a smaller value: 12 is
  // the pane's own gutter, so the rest-state band reads as the card's bottom
  // margin, not as dead space). THE LIMIT, MEASURED: a partial SIXTH row at
  // rest is geometrically unreachable without cramping the lock card and
  // captions - the cut can only move UP from here - so the partial row it
  // lands on is the fifth.
  scroller.Padding(ThicknessHelper::FromLengths(0, 0, 0, 12));
  scroller.Visibility(Visibility::Collapsed);
  StackPanel body;
  scroller.Content(body);
  return scroller;
}

// Conversation mode's subject: the identicon, the name, and one muted line under
// it. Design d4 §2 grows it into the pane's identity header: 64 tall with a 48px
// identicon and the name in UrBodyStrongTextStyle (14 SemiBold) - the subject IS
// this pane's title. MakeIdenticon applies its OWN CornerRadius - nothing here
// sets one.
FrameworkElement MakeSubjectRow(demo::Conversation const& conv) {
  auto root = kit::MakePaneRow(64);
  // MakePaneRow's bottom hairline is a LIST rule, and this row heads no list:
  // full-bleed under the subject it doubled the edge of the MEMBERS card
  // below it (polish B2). The Details header divider stays the rail's single
  // full-bleed rule.
  root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  auto identicon = urmsg::MakeIdenticon(conv.identityKey, 48);
  identicon.VerticalAlignment(VerticalAlignment::Center);
  automation::AutomationProperties::SetAccessibilityView(
      identicon, automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(identicon);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);
  TextBlock name;
  if (auto style = kit::StyleByKey(L"UrBodyStrongTextStyle")) name.Style(style);
  name.Text(H(conv.name));
  text.Children().Append(name);

  TextBlock note;
  if (auto style = kit::StyleByKey(L"UrRowNoteStyle")) note.Style(style);
  // members.size(), NOT memberCount. DemoWorld's --diagnose invariant 2 requires
  // them equal, and members.size() is what decides how many rows the MEMBERS
  // list below actually draws - so this line and that list cannot disagree in a
  // screenshot, whatever memberCount happens to say.
  note.Text(H(conv.kind == demo::ConversationKind::Group
                  ? std::format(L"Group, {} members", conv.members.size())
                  : std::wstring{L"Direct message"}));
  text.Children().Append(note);
  Grid::SetColumn(text, 1);
  grid.Children().Append(text);

  root.Child(grid);
  return root;
}

// Message mode's subject: a padlock and what the product WILL say about a
// message, FRAMED as a statement about the demo model rather than about this
// message.
//
// THE RULING, AND WHY THE BRIEF'S VERSION IS NOT WHAT SHIPPED. Design 2 makes it
// a hard constraint that "no copy in the demo may state that a message WAS
// encrypted as a fact about a real operation". The plan proposed the bare
// "End-to-end encrypted" plus MessageInspect::cipher underneath, on the grounds
// that the DEMO chip in the title bar (D2) is the mitigation the design itself
// names. That mitigation does not hold: the chip is removed by
// --demo-watermark=off, and the watermark is patched out for presentations - so
// chip-dependent honesty disappears at exactly the moment the screen is being
// shown to people. AttestationLabel (InspectRailFields.cpp) already refused the
// same bargain for the same reason and carries its own framing; this row is the
// LOUDER claim of the two and cannot carry less.
//
// PREFIX-FIRST, not "End-to-end encrypted (demo)". A trailing qualifier leads
// with the claim and is the half a screenshot crop, a narrow column or a
// trimmed TextBlock throws away first. The framing has to survive being read
// alone.
//
// AND THE CIPHER NAME IS NOT RENDERED AT ALL. MessageInspect::cipher holds
// "XChaCha20-Poly1305" (DemoWorld.cpp:172). A specific algorithm is a claim
// about what encrypted this message whatever label sits above it, this binary
// has no crypto to have used one, and InspectRailFields.h already records that
// the field must stay unrendered. The note under the title describes the MODEL
// instead, which is the question a reader of this row actually has.
//
// If the owner wants the plain product wording back, the change is these two
// strings and nothing else - no other row, count or check in the rail reads
// them. The padlock and the green stay either way: the picture is fine, it was
// the words that made the claim.
//
// THE HEADER IS A CARD NOW (design d4 §6), and the reason is the reviewer's
// note in the handoff: the green padlock was the only UNFRAMED positive claim
// on the row, and the half a cropped screenshot keeps. So the header is built
// as the top card of message mode, visibly containing its own framing - the
// crop now keeps both lines because they are vertically adjacent inside a
// bounded box. Framing is ADDED with layout and typography, which cannot be
// misquoted; nothing is removed and both strings stay byte-exact.
FrameworkElement MakeLockHeader() {
  auto card = kit::MakePaneCard();
  // The mode's hero, so it leaves the (12,0,12,10) section-card margin: it
  // opens the body, and the note below needs every DIP the column has.
  card.root.Margin(ThicknessHelper::FromLengths(12, 12, 12, 10));

  Border pad;
  pad.Padding(ThicknessHelper::FromUniformLength(12));

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  // The chip: a 36x36 rounded square in kCardHover with a 1px border edge -
  // a NEUTRAL tonal container that frames the glyph the way a mount frames a
  // photo. It adds no colour claim and spends no reserved token; the padlock
  // stays the only green pixel-group in the card (no green rule, no tint).
  Border chip;
  chip.Width(36);
  chip.Height(36);
  chip.Background(urnw::colors::MakeBrush(urnw::colors::kCardHover));
  chip.BorderBrush(urnw::colors::BorderBrush());
  chip.BorderThickness(ThicknessHelper::FromUniformLength(1));
  chip.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));

  FontIcon lock;
  // UrRowIconStyle still carries the family (Segoe Fluent Icons, named
  // explicitly so FontIcon does not fall back to the older Segoe MDL2) and
  // AccessibilityView Raw. The SIZE is overridden 16 -> 18 here, deliberately
  // and only here: the glyph has to centre optically inside a 36px chip, and
  // the style's 16 was tuned for a bare list row (design d4 §6). Same glyph,
  // same kUrGreen - the reservation is "presence, padlock", and this is still
  // the padlock. The words beside it say the same thing, so the colour is a
  // restatement.
  if (auto style = kit::StyleByKey(L"UrRowIconStyle")) lock.Style(style);
  lock.FontSize(18);
  lock.Glyph(L"\uE72E");  // Segoe Fluent E72E, Lock
  lock.Foreground(urnw::colors::MakeBrush(urnw::colors::kUrGreen));
  lock.HorizontalAlignment(HorizontalAlignment::Center);
  lock.VerticalAlignment(VerticalAlignment::Center);
  chip.Child(lock);
  grid.Children().Append(chip);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);
  TextBlock title;
  if (auto style = kit::StyleByKey(L"UrBodyStrongTextStyle")) title.Style(style);
  title.Text(L"Demo model: end-to-end encrypted");
  text.Children().Append(title);
  TextBlock note;
  if (auto style = kit::StyleByKey(L"UrRowNoteStyle")) note.Style(style);
  // The note WRAPS instead of trimming - the one behavioural change here, and
  // it has teeth. The card's text column is 360 - 24 (margins) - 24 (padding)
  // - 36 (chip) - 10 (gap) = 266 DIP and the 47-char note needs ~285, so the
  // old NoWrap row would trim away "in this build" - i.e. crop the framing,
  // which is the exact failure prefix-first framing exists to prevent. A hero
  // card is not a fixed-height list row, so wrapping costs nothing
  // structurally. Acceptance: the full note must be measurable on screen in
  // every capture.
  note.TextWrapping(TextWrapping::Wrap);
  note.TextTrimming(TextTrimming::None);
  // The MODEL, not an algorithm. This is the line that would have named the
  // cipher; it answers "what am I looking at" instead. This header
  // deliberately leaves the 34-44 DIP list rhythm: it is the mode's hero, not
  // a list row.
  //
  // "Demo model:" a THIRD time, and deliberately. This note is the only one of
  // the three G4 strings that says WHY, so it changes register - but it used to
  // change vocabulary too ("Fabricated demo data ... this build"), naming in two
  // ways the same object the title and AttestationLabel both call the demo
  // model. Same prefix, and the note now DEFINES the term the other two use.
  note.Text(L"Demo model: fabricated data, no crypto in this build");
  text.Children().Append(note);
  Grid::SetColumn(text, 1);
  grid.Children().Append(text);

  pad.Child(grid);
  card.body.Children().Append(pad);
  return card.root;
}

// The reason AND the affordance, because design 9.1 asks for both - and the
// button is EXPLICITLY DISABLED, because the same section forbids anything that
// looks live and does nothing and there is no send path in the demo to retry
// into (design 2). UrButtonBaseStyle's Disabled visual state dims the button, so
// "present but not available" is VISIBLE rather than something a user discovers
// by clicking.
//
// UrPaneActionSecondaryStyle, not UrSecondaryButtonStyle: the latter is the
// 48-tall, 24pt NeueBit hero button, which in a 360 DIP rail would be a slab.
// This one is 40 tall, radius 4, outlined, with its own 12 DIP inset.
void AppendFailureBlock(UIElementCollection const& body, std::wstring const& reason) {
  auto row = kit::MakePaneRow(40);
  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition ruleColumn, textColumn;
  ruleColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(ruleColumn);
  grid.ColumnDefinitions().Append(textColumn);

  // The 2px danger bar (design d4 §11): the thread's key-change record idiom -
  // red rule + content - so the danger signal is a SHAPE reinforcement of the
  // red text and never colour-alone. kDanger is the reserved token for exactly
  // this. The reason string and the automation shape are untouched.
  Border rule;
  rule.Width(2);
  rule.Background(urnw::colors::DangerBrush());
  rule.VerticalAlignment(VerticalAlignment::Stretch);
  automation::AutomationProperties::SetAccessibilityView(
      rule, automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(rule);

  TextBlock line;
  if (auto style = kit::StyleByKey(L"UrRowTitleStyle")) line.Style(style);
  line.Foreground(urnw::colors::DangerBrush());
  line.Text(H(reason));
  Grid::SetColumn(line, 1);
  grid.Children().Append(line);

  row.Child(grid);
  body.Append(row);

  Button retry;
  if (auto style = kit::StyleByKey(L"UrPaneActionSecondaryStyle")) retry.Style(style);
  retry.Content(winrt::box_value(winrt::hstring{L"Try again"}));
  retry.IsEnabled(false);
  // A Button whose Content is text still gets a name from that text, but the
  // reason it cannot be pressed is not in it. This project has paid twice for
  // controls that reach a screen reader as "button" and nothing else.
  automation::AutomationProperties::SetName(retry, L"Try again, not available in the demo");
  body.Append(retry);
}

// A floating caption (design d4 §4): the group header's letterspaced chrome
// voice with the ruled sheet strip removed - transparent, borderless, 32 tall -
// so a section name floats above its card instead of ruling the column. The
// first caption in a body sits 4 from its predecessor, later ones 12. Returned
// so a caller can drop a glyph in the trailing slot (the RETENTION caption's
// disappearing timer).
kit::PaneGroupHeader AppendCaption(StackPanel const& panel, std::wstring_view title,
                                   std::wstring const& meta, bool first) {
  auto header = kit::MakePaneGroupHeader(winrt::hstring{title},
                                         meta.empty() ? winrt::hstring{} : H(meta));
  header.root.Background(urnw::colors::MakeBrush({0, 0, 0, 0}));
  header.root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));
  header.root.Height(32);
  header.root.Margin(ThicknessHelper::FromLengths(0, first ? 4 : 12, 0, 0));
  // The kit grid's 8dip ColumnSpacing costs the trailing META its flush edge:
  // the header's third (action) column is empty here, and Grid spends the
  // spacing on it anyway, so the meta stopped 8dip short of the card's right
  // border while the caption's padding already mirrors the card's 12dip
  // gutters (measured on the polish-A captures; polish B2). Zeroing the
  // spacing right-aligns the meta flush. The title column is Star and every
  // caption title is a short fixed constant, so the lost title-to-meta gap
  // cannot be reached; the one caption with a real trailing child (RETENTION's
  // timer) has no meta for the glyph to crowd.
  if (auto grid = header.root.Child().try_as<Grid>()) grid.ColumnSpacing(0);
  panel.Children().Append(header.root);
  return header;
}

// THE FUNNEL. This is the one function permitted to call the kit's key/value
// builder; the identifier is poisoned elsewhere in this translation unit (see
// the block under the includes) and un-poisoned across this function.
//
// FUNCTION SCOPE, NOT STATEMENT SCOPE. Wrapping the directives around the call
// itself is tighter, and reads - wrongly - as if the preprocessor ran per
// iteration. Directives are processed once, at translation, but a reader should
// not have to know that to read a loop.
//
// WHAT THE WINDOW COSTS. Anything written between the two directives is
// un-poisoned: a second call inside this function, and - the case that actually
// matters - a helper appended after it, which is where someone adding a sibling
// naturally types. That is the bigger hole of the two, so the closing directive
// carries a marker; a new function belongs after it.
//
// The DRAWN blank and the SPOKEN blank are different strings and go in through
// different parameters, so one row cannot acquire two writers of its name.
#undef MakePaneKeyValueRow
void AppendFieldRows(StackPanel const& panel, demo::Conversation const& conv,
                     std::vector<InspectField> const& fields, bool messageMode,
                     bool captionsAlreadyOpened = false) {
  // The group walk (design d4 §9): the fields arrive positionally pinned, and
  // the walk opens a floating caption + card whenever the PURE group function
  // (InspectRailFields, probe-asserted) says the section changes. Grouping can
  // therefore never permute a row - the probe pins the order AND the
  // partition.
  std::optional<InspectGroup> open;
  kit::PaneCard card{nullptr, nullptr};
  bool firstCaption = !captionsAlreadyOpened;
  for (size_t i = 0; i < fields.size(); ++i) {
    auto const& field = fields[i];
    const InspectGroup group =
        messageMode ? MessageFieldGroup(i) : ConversationFieldGroup(i);
    if (!open || group != *open) {
      if (card.root) kit::FinalizePaneCard(card);
      auto caption =
          AppendCaption(panel, InspectGroupCaption(group), L"", firstCaption);
      firstCaption = false;
      // The disappearing timer rides the RETENTION caption's trailing slot
      // (design d4 §2) - the same Stopwatch glyph, at the same 12px faint ink,
      // the conversation list row draws for a disappearing conversation.
      if (!messageMode && group == InspectGroup::Retention && conv.disappearing) {
        FontIcon timer;
        if (auto style = kit::StyleByKey(L"UrRowIconStyle")) timer.Style(style);
        timer.FontSize(12);
        timer.Foreground(urnw::colors::FaintBrush());
        timer.Glyph(L"\uE916");  // Segoe Fluent E916, Stopwatch
        caption.trailing.Children().Append(timer);
      }
      card = kit::MakePaneCard();
      panel.Children().Append(card.root);
      open = group;
    }
    auto row = kit::MakePaneKeyValueRow(H(field.key),
                                        winrt::hstring{RailValueOr(field.value)}, 34,
                                        winrt::hstring{RailSpokenValueOr(field.value)});
    // The rail-local hierarchy (design d4 §5): keys quiet to 12sp through
    // UrCaptionTextStyle - an EXISTING key; the kit styles are shared with
    // future pane surfaces and are deliberately NOT edited. Values keep 13 in
    // UrValueTextStyle, so the value column's geometry - and with it the
    // 28-character budget - is unchanged.
    if (auto style = kit::StyleByKey(L"UrCaptionTextStyle")) row.key.Style(style);
    // ...and a blank's em dash goes FAINT: "nothing here" must not borrow the
    // weight of a real value. The drawn substitution (the dash) and the spoken
    // one ("not set") stay exactly as the static_asserts pin them; only the
    // ink quiets. THE LIMIT, restated from the kBlankValue block: no openable
    // surface reaches a blank today, so this branch is code-inspection only
    // until one does.
    if (field.value.empty()) row.value.Foreground(urnw::colors::FaintBrush());
    card.body.Children().Append(row.root);
  }
  if (card.root) kit::FinalizePaneCard(card);
}
// <-- THE WINDOW ENDS HERE. New functions go BELOW this line, not above it.
#define MakePaneKeyValueRow MakePaneKeyValueRow_bypasses_RailValueOr_use_AppendFieldRows

// ---- member presence rows and their expandable devices (design d4 §7) -------

bool IsExpanded(InspectRailView const& v, std::wstring const& memberId) {
  return std::find(v.expandedMemberIds.begin(), v.expandedMemberIds.end(), memberId) !=
         v.expandedMemberIds.end();
}

// The row's whole announcement (a Button with panel content gets no automatic
// name - the kit rule): identity, the admin note, the presence WORDS, and the
// disclosure state, rewritten on every toggle so the state is spoken, not only
// drawn.
std::wstring MemberPresenceName(demo::MemberRef const& member, bool expanded) {
  std::wstring name = member.displayName;
  if (member.admin) name += L", Admin";
  name += std::format(L", {}/{} online", OnlineDeviceCount(member), member.devices.size());
  name += expanded ? L", expandable device list, expanded"
                   : L", expandable device list, collapsed";
  return name;
}

// A sub-row's Tag names the member it belongs to, so a collapse finds exactly
// that member's rows with no position bookkeeping.
constexpr wchar_t kDeviceSubRowTag[] = L"devsub:";

std::vector<Border> BuildDeviceSubRows(demo::MemberRef const& member) {
  std::vector<Border> rows;
  for (auto const& device : member.devices) {
    // showOwner=false: under the member's own row the owner suffix would
    // repeat the row above. The default keeps the message-mode usage. The
    // seed is the member's own identityKey - the member's devices resolve to
    // the member by construction, so the sub-row and the avatar above it are
    // one face (polish B2).
    auto row = MakeDeviceRow(device, /*showOwner=*/false, member.identityKey);
    // Indent 26 (design d4 §7.2): the member title column starts at 12
    // (padding) + 28 (avatar) + 10 (gap) = 50; this row's leading column
    // starts at 12 + 2 (marker) + 10 (spacing) + 26 (margin) = 50 - the
    // device rows form one vertical line directly under the member names,
    // which is what reads as "belonging" without a tree glyph.
    row.root.Margin(ThicknessHelper::FromLengths(26, 0, 0, 0));
    row.root.Tag(winrt::box_value(winrt::hstring{std::wstring{kDeviceSubRowTag} + member.id}));
    rows.push_back(row.root);
  }
  return rows;
}

void InsertDeviceSubRows(StackPanel const& cardBody, UIElement const& afterRow,
                         demo::MemberRef const& member, bool animate) {
  auto children = cardBody.Children();
  uint32_t index = 0;
  if (!children.IndexOf(afterRow, index)) return;
  auto rows = BuildDeviceSubRows(member);
  uint32_t at = index + 1;
  for (auto const& row : rows) children.InsertAt(at++, row);

  // On expand the sub-rows fade 0->1 at kFastMs with the Soft ease - "a
  // gentle disclosure" (UrMotion.h) - staggered kStaggerMs, at most 2 steps
  // so the 6-step cap is never near. Collapse is instant removal: exits run
  // one step faster than entrances. The chevron swaps instantly in both
  // cases.
  //
  // UNVERIFIED BRANCH, STATED TWICE OVER: ShouldAnimate() has never returned
  // false on this machine, and `animate` is only ever true from a CLICK,
  // which an agent may not synthesise - so this whole block is
  // code-inspection only in this environment.
  if (!animate || !urnw::motion::ShouldAnimate()) return;
  namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
  anim::Storyboard sb;
  for (size_t i = 0; i < rows.size(); ++i) {
    auto fade = urnw::motion::MakeSplineDouble(
        0.0, 1.0, urnw::motion::kFastMs,
        static_cast<int64_t>(std::min<size_t>(i, 2)) * urnw::motion::kStaggerMs,
        urnw::motion::kSoftP1, urnw::motion::kSoftP2);
    anim::Storyboard::SetTarget(fade, rows[i]);
    anim::Storyboard::SetTargetProperty(fade, L"Opacity");
    sb.Children().Append(fade);
  }
  sb.Begin();
}

void RemoveDeviceSubRows(StackPanel const& cardBody, std::wstring const& memberId) {
  auto children = cardBody.Children();
  const std::wstring tag = std::wstring{kDeviceSubRowTag} + memberId;
  // Highest index first, so the indexes below stay valid as they shift.
  for (uint32_t i = children.Size(); i-- > 0;) {
    auto element = children.GetAt(i).try_as<FrameworkElement>();
    if (!element) continue;
    auto value = element.Tag().try_as<winrt::hstring>();
    if (value && std::wstring{*value} == tag) children.RemoveAt(i);
  }
}

void ToggleMemberExpansion(InspectRailView& v, kit::PanePresenceRow const& row,
                           demo::MemberRef const& member) {
  auto& ids = v.expandedMemberIds;
  auto it = std::find(ids.begin(), ids.end(), member.id);
  const bool expanding = it == ids.end();
  if (expanding) {
    ids.push_back(member.id);
  } else {
    ids.erase(it);
  }
  kit::SetPanePresenceExpanded(row, expanding);
  automation::AutomationProperties::SetName(row.root, H(MemberPresenceName(member, expanding)));

  // The surgical path, not a re-populate: the sub-rows fade in under the
  // clicked member (or vanish instantly) and the rest of the card never
  // moves. The card body is the row's own Parent - one population generation
  // holds both, so nothing here goes stale within it.
  auto cardBody = row.root.Parent().try_as<StackPanel>();
  if (!cardBody) return;
  if (expanding) {
    InsertDeviceSubRows(cardBody, row.root, member, /*animate=*/true);
  } else {
    RemoveDeviceSubRows(cardBody, member.id);
  }
  // The card's last row may have changed hands; the edge rule is idempotent,
  // so re-running it on the body keeps the card honest.
  kit::FinalizePaneCard(kit::PaneCard{nullptr, cardBody});
}

kit::PanePresenceRow MakeMemberPresenceRow(InspectRailView& v, demo::MemberRef const& member) {
  auto row = kit::MakePanePresenceRow();
  // The member's own mark, 28px, under the badge the kit already seated - the
  // key whose change would change the picture, which is exactly what an
  // identicon is FOR.
  row.avatarHost.Children().InsertAt(0, urmsg::MakeIdenticon(member.identityKey, 28));
  kit::SetPanePresenceOnline(row, 0 < OnlineDeviceCount(member));

  std::wstring title = member.displayName;
  if (member.admin) title += L" \u00B7 Admin";  // U+00B7 MIDDLE DOT
  row.title.Text(H(title));

  // The meta states the presence in WORDS. That is the primary channel and
  // what entitles the corner badge to remain a Raw restatement - "colour is
  // never the only carrier of state".
  const std::wstring presence =
      std::format(L"{}/{} online", OnlineDeviceCount(member), member.devices.size());
  row.meta.Text(H(presence));

  const bool expanded = IsExpanded(v, member.id);
  kit::SetPanePresenceExpanded(row, expanded);
  automation::AutomationProperties::SetName(row.root, H(MemberPresenceName(member, expanded)));

  // Captures, deliberately: the view struct BY REFERENCE (it is a MainWindow
  // member and outlives every population generation of its own rows - a click
  // can only fire while the row exists), the member BY VALUE (ambient
  // activity can append to the world vector it came from), the row BY VALUE
  // (a struct of winrt handles). No `this`, no raw pointer into the world.
  row.root.Click([&v, member, row](auto const&, auto const&) {
    ToggleMemberExpansion(v, row, member);
  });
  return row;
}

// Message mode's subject (design d4 §10): WHICH message this inspector is
// about, in the same shape as conversation mode's identity block - sender,
// time, one excerpt line - so the two modes read as the same shape of page
// (subject, then sections). Honest by construction: every string here is
// already on screen in the thread; the row adds no claim.
FrameworkElement MakeMessageSubjectRow(demo::MessageRow const& row) {
  auto root = kit::MakePaneRow(44);  // UrPaneRowTallHeight
  // Same ruling as conversation mode's subject (MakeSubjectRow): the list
  // hairline MakePaneRow carries doubled the edge of the first caption card
  // below it (polish B2).
  root.BorderThickness(ThicknessHelper::FromLengths(0, 0, 0, 0));

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  // The sender's face, 20px: outgoing rows carry the deterministic "me" seed
  // (DemoWorld.cpp), so "You" has a stable picture.
  auto identicon = urmsg::MakeIdenticon(row.senderKey, 20);
  identicon.VerticalAlignment(VerticalAlignment::Center);
  automation::AutomationProperties::SetAccessibilityView(
      identicon, automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(identicon);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);

  Grid line1;
  ColumnDefinition senderColumn, timeColumn;
  senderColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  timeColumn.Width(GridLengthHelper::Auto());
  line1.ColumnDefinitions().Append(senderColumn);
  line1.ColumnDefinitions().Append(timeColumn);
  TextBlock sender;
  if (auto style = kit::StyleByKey(L"UrRowTitleStyle")) sender.Style(style);
  sender.Text(H(SenderLabel(row)));
  line1.Children().Append(sender);
  TextBlock time;
  if (auto style = kit::StyleByKey(L"UrPaneMetaStyle")) time.Style(style);
  time.Text(H(row.timeLabel));
  Grid::SetColumn(time, 1);
  line1.Children().Append(time);
  text.Children().Append(line1);

  TextBlock excerpt;
  if (auto style = kit::StyleByKey(L"UrRowNoteStyle")) excerpt.Style(style);
  // One line, trimmed (the style already trims): the rail is an inspector,
  // not a second bubble.
  excerpt.Text(H(row.body));
  text.Children().Append(excerpt);
  Grid::SetColumn(text, 1);
  grid.Children().Append(text);

  root.Child(grid);
  return root;
}

// R4's delivered-by / read-by lists, carded in their final form (design d4 §8,
// so they were built once): a caption with the count meta, then a card of
// device rows - the owner suffix KEPT here, because in message mode the
// devices belong to DIFFERENT people and "Pixel 9 · Bo Nakamura" is the
// load-bearing "statement by a device" content. The empty case is the pure
// PlanDeviceList's: one honest line in place of the card, never an empty
// bordered box. The `This computer` substitution and the online/last-seen
// words stay exactly as MakeDeviceRow writes them.
void AppendDeviceList(StackPanel const& panel, demo::Conversation const& conv,
                      std::vector<demo::DeviceRef> const& devices, bool deliveredList) {
  const DeviceListPlan plan = PlanDeviceList(devices, deliveredList);
  AppendCaption(panel,
                deliveredList ? std::wstring_view{L"DELIVERED TO"}
                              : std::wstring_view{L"READ BY"},
                std::format(L"{}", devices.size()), /*first=*/false);
  if (!plan.emptyNote.empty()) {
    panel.Children().Append(kit::MakePaneEmptyLine(winrt::hstring{plan.emptyNote}));
    return;
  }
  auto card = kit::MakePaneCard();
  // The identicon seed resolves against THIS conversation: a member's devices
  // draw the member's face (polish B2); the fixture's myDevices name no
  // member and keep their own ownerKey (DeviceIdenticonSeed's documented
  // fallback).
  for (auto const& device : devices)
    card.body.Children().Append(
        MakeDeviceRow(device, /*showOwner=*/true, DeviceIdenticonSeed(conv, device)).root);
  kit::FinalizePaneCard(card);
  panel.Children().Append(card.root);
}

// POPULATE ONLY. No storyboard, so the density switch can call this on a rail
// that is already on screen without the rail flashing.
void PopulateConversation(InspectRailView& v, demo::Conversation const& conv,
                          bool advanced) {
  auto panel = BodyOf(v.conversationScroll);
  if (!panel) return;
  auto body = panel.Children();
  body.Clear();
  body.Append(MakeSubjectRow(conv));

  // The caption meta is WORDS, not a bare count (design d4 §2), summed with
  // the same OnlineDeviceCount the rows report - so the caption and the rows
  // cannot disagree in a screenshot.
  size_t online = 0;
  size_t total = 0;
  for (auto const& member : conv.members) {
    online += OnlineDeviceCount(member);
    total += member.devices.size();
  }
  AppendCaption(panel, L"MEMBERS",
                std::format(L"{} members \u00B7 {}/{} online",  // U+00B7 MIDDLE DOT
                            conv.members.size(), online, total),
                /*first=*/true);
  if (conv.members.empty()) {
    body.Append(kit::MakePaneEmptyLine(L"No members"));
  } else {
    auto card = kit::MakePaneCard();
    for (auto const& member : conv.members) {
      card.body.Children().Append(MakeMemberPresenceRow(v, member).root);
      // An expanded member's sub-rows are rebuilt inline and SILENTLY: a
      // populate never animates - the fade belongs to the click, and a
      // density toggle must not announce itself.
      if (IsExpanded(v, member.id))
        for (auto const& sub : BuildDeviceSubRows(member)) card.body.Children().Append(sub);
    }
    kit::FinalizePaneCard(card);
    body.Append(card.root);
  }

  AppendFieldRows(panel, conv, BuildConversationFields(conv, advanced),
                  /*messageMode=*/false, /*captionsAlreadyOpened=*/true);
}

// POPULATE ONLY, for the same reason as the conversation half above.
void PopulateMessage(InspectRailView const& v, demo::Conversation const& conv,
                     demo::MessageRow const& row, bool advanced) {
  auto panel = BodyOf(v.messageScroll);
  if (!panel) return;
  auto body = panel.Children();
  body.Clear();
  body.Append(MakeLockHeader());

  // failureReason is documented non-empty ONLY when state == Failed, and an
  // empty reason must produce NO block rather than an empty one: a StackPanel
  // gives every child its space whether or not the child drew anything, which is
  // the measured defect SetTextOrCollapse exists for.
  if (!row.failureReason.empty()) AppendFailureBlock(body, row.failureReason);

  // Which message this is, directly under the lock card (and under the failure
  // block when present): message mode opens warm now, not cold on a table.
  body.Append(MakeMessageSubjectRow(row));

  // AppendFieldRows, NOT the kit builder - the brief's line here was
  // `kit::MakePaneKeyValueRow(H(field.key), H(field.value), 34)`, which is the
  // one call in the module that would skip RailValueOr, on the one mode that
  // holds every blank value in the world. It no longer compiles; see the block
  // under this file's includes.
  AppendFieldRows(panel, conv, BuildMessageFields(conv, row, advanced),
                  /*messageMode=*/true);

  // The device lists are always LAST (R4's step-8 invariant: Advanced Mode
  // adds rows; it does not reorder the surface).
  AppendDeviceList(panel, conv, row.inspect.deliveredTo, /*deliveredList=*/true);
  AppendDeviceList(panel, conv, row.inspect.readBy, /*deliveredList=*/false);
}

// The 20px mini-identicon (design d1 §7) on the DEVICE row - the row the
// delivered-by / read-by lists and a member's expanded sub-rows share. The
// presence dot STAYS: it badges the chip's corner, and removing it would
// strip the row's second presence channel (the meta words are the first).
// The MEMBER row outgrew this helper in wave 4: members are
// kit::MakePanePresenceRow buttons now (design d4 §7), whose badge the kit
// seats itself.
//
// The kit grid is not rebuilt for this: the dot is re-parented into a
// chip-sized host that takes the dot's old column, so "one row species per
// pane layout" survives and MakePaneListRowButton stays in step.
void SeatIdenticonBadge(kit::PaneListRow const& row, Border const& chip) {
  auto grid = row.root.Child().try_as<Grid>();
  if (!grid) return;
  uint32_t index = 0;
  if (!grid.Children().IndexOf(row.dot, index)) return;
  grid.Children().RemoveAt(index);

  Grid host;
  host.Width(20);
  host.Height(20);
  host.VerticalAlignment(VerticalAlignment::Center);
  // Decorative only: the row's automation name already carries the identity
  // and the presence words, so the chip announces nothing of its own.
  automation::AutomationProperties::SetAccessibilityView(
      chip, automation::Peers::AccessibilityView::Raw);
  host.Children().Append(chip);
  // Badged on the chip's corner and poked just past it, so the dot straddles
  // the edge instead of covering two pattern cells outright.
  row.dot.HorizontalAlignment(HorizontalAlignment::Right);
  row.dot.VerticalAlignment(VerticalAlignment::Bottom);
  row.dot.Margin(ThicknessHelper::FromLengths(0, 0, -2, -2));
  host.Children().Append(row.dot);
  Grid::SetColumn(host, 1);
  grid.Children().Append(host);
}

}  // namespace

InspectRailView MakeInspectRail() {
  InspectRailView v;

  Grid root;
  // The view paints its own surface rather than depending on the host's style:
  // a view module that is transparent unless its container happens to carry
  // UrPaneStyle is a module with an invisible dependency.
  //
  // The surface is SHEET, not page (design d1 §1.2): the rail is chrome ABOUT
  // content - a key/value inspector - and chrome sits one tonal step above the
  // ground it flanks, so the frame reads nav | list | thread | rail as
  // chrome | content | content | chrome. One existing token, no new key.
  root.Background(urnw::colors::SheetBrush());
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  root.RowDefinitions().Append(headerRow);
  root.RowDefinitions().Append(bodyRow);

  // The 40 DIP strip every pane in this window opens with, by the same two keys
  // MainWindow.xaml uses in markup (lines 180-188), so a pane built in code and
  // a pane declared in XAML are the same pane. The header is a two-column grid
  // now: the title, and the density word at the right edge (design d4 §13).
  Border header;
  if (auto style = kit::StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  Grid headerGrid;
  ColumnDefinition titleColumn, metaColumn;
  titleColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  metaColumn.Width(GridLengthHelper::Auto());
  headerGrid.ColumnDefinitions().Append(titleColumn);
  headerGrid.ColumnDefinitions().Append(metaColumn);
  TextBlock title;
  if (auto style = kit::StyleByKey(L"UrPaneTitleStyle")) title.Style(style);
  // UrPaneTitleStyle IS the chrome voice (12sp SemiBold, letterspaced) but it
  // does not uppercase: every other pane title is WRITTEN uppercase
  // ("CONVERSATIONS"), and this one read as body copy beside them (polish B2).
  // No gate and no automation name reads the old casing (grep: "Details"
  // existed only here); a TextBlock is announced as its text either way.
  title.Text(L"DETAILS");
  headerGrid.Children().Append(title);
  // The ADVANCED meta answers "why are there more rows than a minute ago"
  // without a toast or a colour. Collapsed at normal density; flipped
  // INSTANTLY by SetInspectRailAdvanced, never with a storyboard.
  v.headerMeta = TextBlock();
  if (auto style = kit::StyleByKey(L"UrPaneMetaStyle")) v.headerMeta.Style(style);
  v.headerMeta.Text(L"ADVANCED");
  v.headerMeta.Visibility(Visibility::Collapsed);
  Grid::SetColumn(v.headerMeta, 1);
  headerGrid.Children().Append(v.headerMeta);
  header.Child(headerGrid);
  root.Children().Append(header);

  // Both bodies live in the same Grid cell, both collapsed. See the header.
  v.conversationScroll = MakeBodyScroller();
  Grid::SetRow(v.conversationScroll, 1);
  root.Children().Append(v.conversationScroll);

  v.messageScroll = MakeBodyScroller();
  Grid::SetRow(v.messageScroll, 1);
  root.Children().Append(v.messageScroll);

  v.root = root;
  SetRailAdvanced(v, false);
  SetRailSubject(v, L"", L"");
  return v;
}

kit::PaneListRow MakeDeviceRow(demo::DeviceRef const& device, bool showOwner,
                               demo::Seed const& identiconSeed) {
  auto row = kit::MakePaneListRow(36);
  row.dot.Fill(device.online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                             : urnw::colors::FaintBrush());

  // The OWNER's mark, not the device's: the device list has no per-device key,
  // and the owner is the identity this row is actually about (DemoWorld.h).
  // The seed ARRIVES resolved: the owning member's identityKey when the device
  // belongs to a member of the conversation (DeviceIdenticonSeed), so a person
  // and their device rows draw ONE face - before polish B2 the row seeded from
  // ownerKey, a second input (DemoWorld.cpp:63), and one person drew two.
  SeatIdenticonBadge(row, urmsg::MakeIdenticon(identiconSeed, 20));

  std::wstring title = device.name;
  // The owner suffix is the "statement by a device" content when the list
  // mixes owners (message mode's delivered-by / read-by); under a member's
  // own expanded row it would repeat the row above, so the caller drops it.
  if (showOwner) {
    const std::wstring owner =
        device.isThisComputer ? std::wstring{L"This computer"} : device.ownerName;
    if (!owner.empty()) title += L" \u00B7 " + owner;  // U+00B7 MIDDLE DOT
  }
  row.title.Text(H(title));

  // Same rule as the member rows: the WORD carries the state and the dot
  // restates it. An online device reads "online"; an offline one reads when it
  // was last seen. Nothing here is legible only by hue.
  const std::wstring meta = device.online ? std::wstring{L"online"} : device.lastSeenLabel;
  row.meta.Text(H(meta));

  automation::AutomationProperties::SetAccessibilityView(
      row.title, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetAccessibilityView(
      row.meta, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetName(
      row.root, H(title + L", " + (device.online ? L"online" : L"offline") + L", " +
                  device.lastSeenLabel));
  return row;
}

void SetInspectRailConversation(InspectRailView& v, demo::Conversation const& c) {
  if (!v.root) return;
  // A new subject gets fresh expansion state (design d4 §7.2): parked ids are
  // per-subject. SetInspectRailAdvanced is the path that PRESERVES them across
  // a density re-population.
  v.expandedMemberIds.clear();
  PopulateConversation(v, c, RailAdvanced(v));
  SetRailSubject(v, c.id, L"");
  PresentMode(v, /*messageMode=*/false);
  urnw::LogInfo("rail: conversation mode -> {} ({} members)", winrt::to_string(c.id),
                c.members.size());
}

void SetInspectRailMessage(InspectRailView& v, demo::Conversation const& c,
                           demo::MessageRow const& m) {
  if (!v.root) return;
  PopulateMessage(v, c, m, RailAdvanced(v));
  // IDS, not the references we were just handed: `m` is a reference into
  // Conversation::rows, and design 9.2's ambient activity appends to that vector.
  SetRailSubject(v, c.id, m.id);
  PresentMode(v, /*messageMode=*/true);
  urnw::LogInfo("rail: message mode -> {} in {} (state {}, failure \"{}\")",
                winrt::to_string(m.id), winrt::to_string(c.id),
                winrt::to_string(DeliveryLabel(m.state)), winrt::to_string(m.failureReason));
}

void SetInspectRailAdvanced(InspectRailView& v, bool advanced) {
  if (!v.root) return;
  SetRailAdvanced(v, advanced);
  // The header meta flips INSTANTLY (design d4 §13) - and nothing here starts
  // a storyboard. This function deliberately makes no call into the mode-swap
  // machinery (the regex gate on this file requires zero such references in
  // this body): animating a density change reads as a phantom mode swap.
  if (v.headerMeta)
    v.headerMeta.Visibility(advanced ? Visibility::Visible : Visibility::Collapsed);

  // Re-populate whichever mode is SHOWING, in place, from the parked subject
  // ids (ids, never pointers - the world vector can be appended to). At
  // startup neither body is visible yet - BuildInspectRail's seeding line
  // runs before the first present - so there is nothing to re-render and the
  // first populate simply reads the density tag.
  auto const& world = demo::GetWorld();
  if (v.conversationScroll && v.conversationScroll.Visibility() == Visibility::Visible) {
    if (auto const* conv = FindConversation(world, RailConversationId(v)))
      PopulateConversation(v, *conv, advanced);
    return;
  }
  if (v.messageScroll && v.messageScroll.Visibility() == Visibility::Visible) {
    auto const* conv = FindConversation(world, RailConversationId(v));
    if (!conv) return;
    if (auto const* row = FindMessageRow(*conv, RailMessageId(v)))
      PopulateMessage(v, *conv, *row, advanced);
  }
}

}  // namespace urmsg::views
