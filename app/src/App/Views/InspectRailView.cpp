// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/InspectRailView.h"

#include <format>
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
// WHAT IT CATCHES, precisely. AppendFieldRows below routes every rail value
// through RailValueOr, which draws a blank as one em dash. Appending a row with
// `kit::MakePaneKeyValueRow(H(field.key), H(field.value), 34)` instead skips it.
// That is not hypothetical and it is not a style point: it is the line task R3
// was briefed with, and message mode holds ALL 28 of the world's blank values
// (24 "Group id" on direct conversations, 4 "Received" on outgoing rows nothing
// has received yet), so that one line renders 28 empty cells while both
// static_asserts below stay true, the build stays clean and --diagnose stays
// 42 PASS / 0 FAIL. Task R2 measured the bypass and reported that nothing it
// could write would catch it, because the second mode did not exist yet.
//
// So the identifier is POISONED for the whole translation unit and un-poisoned
// for exactly the one line inside AppendFieldRows that is allowed to use it. A
// second call site anywhere in this file - above the funnel or below it - is a
// compile error naming the funnel, rather than 28 empty cells in a screenshot
// nobody looked at closely enough.
//
// THE LIMIT, STATED ACCURATELY, AND CHECK IT RATHER THAN BELIEVING IT:
//
//     git grep -n "MakePaneKeyValueRow" -- app/
//
// Today every hit is a declaration, a definition or a comment except ONE
// invocation - AppendFieldRows below. This file is the builder's only caller in
// the tree. So the guard is PROSPECTIVE: it protects against the callers the
// plan will add, in docs/superpowers/plans/tasks/advanced.md and network.md, and
// on the day those land this file stops being the obvious only user and the
// funnel stops being self-evident. Those surfaces will call the kit builder
// directly and correctly - they have no blank-value model to funnel through.
//
// AND THE TRADEOFF, SO A READER CAN ACTUALLY JUDGE IT. With one call site in the
// tree, cheaper enforcement was available: a reviewer reading this diff catches
// the bypass as reliably as the macro does, and a comment would have cost
// nothing. The macro was chosen because the bypass is not hypothetical - it is
// the line THIS task was briefed with, in a group where the same class of defect
// has now shipped thirteen times - and because its whole value is on the day
// someone edits this file without the brief in front of them. It is not free: it
// is the only preprocessor device of its kind in this codebase.
//
// It is also per-FILE and PREPROCESSOR-level, so it sees an identifier and not a
// call graph: a rail row built by some new helper that calls the kit from
// ANOTHER file would pass this. That is the failure mode to watch, and nothing
// mechanical in this file catches it - only reading a diff does.
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
// THE LIMIT, STATED ACCURATELY - AND RE-STATED BY R3, BECAUSE MESSAGE MODE NOW
// EXISTS. R2 wrote that all 28 blanks were "in message mode, whose body is not
// defined yet". That body is defined now, and the substituting branch is STILL
// not reached by anything either task renders - but for a narrower reason, which
// is the part worth writing down:
//
//   * conversation mode at the default density is BuildConversationFields' two
//     rows, "Retention" and "Media", and DemoWorld fills both on all eight.
//   * the 24 "Group id" blanks are ADVANCED-density rows on a DIRECT
//     conversation. --demo=inspect opens conversation 0, which is a GROUP with a
//     populated groupIdHex, and the rail's density stays false until R4 defines
//     SetInspectRailAdvanced - so neither half of that pair is reachable yet.
//   * the 4 "Received" blanks are outgoing rows nothing has received. The only
//     row --demo=inspect can land on is PickInspectMessage's, which is by
//     definition the last outgoing row in state Read (c0-r12), and a Read row
//     has a receivedAtLabel. Reaching a Pending or Failed row means CLICKING a
//     bubble, and the thread's onSelectMessage seam is not wired yet.
//
// So the branch is covered by the static_asserts below - a locally built
// adversarial input, the same device InspectRailFields.cpp uses for ShortHex's
// unreachable truncating branch - and NOT by any screenshot yet taken, which
// shows no blank row because there is no blank value in it to show. What R3 adds
// is the guarantee that when a render finally does reach a blank, it comes
// through here: see the call-site block under this file's includes.
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
// because a clause kept for a reason that does not check out is the exact shape
// this file's own header cites thirteen times. IsSpeakable(spoken) and
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
// funnels collapsing onto one speakable string trips the second. If you are
// counting clauses that can independently fail, the answer here is two.
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
// has to hold those references. The reader these are waiting for is
// SetInspectRailAdvanced (R4), which re-populates whichever mode is already
// showing and is handed no subject at all: it has only these two tags and
// FindConversation / FindMessageRow. Still written and not yet read.
[[maybe_unused]] std::wstring RailConversationId(InspectRailView const& v) {
  return TagId(v.conversationScroll);
}
[[maybe_unused]] std::wstring RailMessageId(InspectRailView const& v) {
  return TagId(v.messageScroll);
}

// The StackPanel inside a body scroller. The fixed contract gives InspectRailView
// three fields and no body fields, so the panel is fetched rather than cached -
// which is also one fewer thing that can go stale.
StackPanel BodyOf(ScrollViewer const& scroller) {
  return scroller ? scroller.Content().try_as<StackPanel>() : nullptr;
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
// CrossfadePageSwap is already gated on motion::ShouldAnimate(), so "animations
// off" makes this an instant, correct swap rather than a slow one.
void PresentMode(InspectRailView const& v, bool messageMode) {
  auto incoming = messageMode ? v.messageScroll : v.conversationScroll;
  auto outgoing = messageMode ? v.conversationScroll : v.messageScroll;
  if (!incoming) return;
  const bool swapping = outgoing && outgoing.Visibility() == Visibility::Visible;
  urnw::motion::CrossfadePageSwap(swapping ? outgoing : incoming, incoming);
}

ScrollViewer MakeBodyScroller() {
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroller.Visibility(Visibility::Collapsed);
  StackPanel body;
  scroller.Content(body);
  return scroller;
}

// Conversation mode's subject: the identicon, the name, and one muted line under
// it. 56 tall so a 40x40 identicon sits in it on the pane's own rhythm.
// MakeIdenticon applies its OWN CornerRadius(8) - nothing here sets one.
FrameworkElement MakeSubjectRow(demo::Conversation const& conv) {
  auto root = kit::MakePaneRow(56);

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  auto identicon = urmsg::MakeIdenticon(conv.identityKey, 40);
  identicon.VerticalAlignment(VerticalAlignment::Center);
  automation::AutomationProperties::SetAccessibilityView(
      identicon, automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(identicon);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);
  TextBlock name;
  if (auto style = kit::StyleByKey(L"UrRowTitleStyle")) name.Style(style);
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
FrameworkElement MakeLockHeader() {
  auto root = kit::MakePaneRow(56);

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  FontIcon lock;
  // UrRowIconStyle carries the family (Segoe Fluent Icons, named explicitly so
  // FontIcon does not fall back to the older Segoe MDL2), the size (16) and
  // AccessibilityView Raw. The size is NOT overridden: styles-by-key is what
  // stops a screen acquiring a second icon weight, and the rail's one icon has
  // no claim to be the exception. Only the colour is set, and the words beside
  // it say the same thing, so the colour is a restatement.
  if (auto style = kit::StyleByKey(L"UrRowIconStyle")) lock.Style(style);
  lock.Glyph(L"\uE72E");  // Segoe Fluent E72E, Lock
  lock.Foreground(urnw::colors::MakeBrush(urnw::colors::kUrGreen));
  grid.Children().Append(lock);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);
  TextBlock title;
  if (auto style = kit::StyleByKey(L"UrBodyStrongTextStyle")) title.Style(style);
  title.Text(L"Demo model: end-to-end encrypted");
  text.Children().Append(title);
  TextBlock note;
  if (auto style = kit::StyleByKey(L"UrRowNoteStyle")) note.Style(style);
  // The MODEL, not an algorithm. This is the line that would have named the
  // cipher; it answers "what am I looking at" instead, and it is what keeps the
  // 56 DIP row on conversation mode's two-line rhythm.
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

  root.Child(grid);
  return root;
}

// The reason AND the affordance, because design 9.1 asks for both - and the
// button is EXPLICITLY DISABLED, because the same section forbids anything that
// looks live and does nothing and there is no send path in the demo to retry
// into (design 2). UrButtonBaseStyle's Disabled visual state draws the button at
// 38% opacity (App.xaml:371-375), so "present but not available" is VISIBLE
// rather than something a user discovers by clicking.
//
// UrPaneActionSecondaryStyle, not UrSecondaryButtonStyle: the latter is the
// 48-tall, 24pt NeueBit hero button, which in a 360 DIP rail would be a slab.
// This one is 40 tall, radius 4, outlined, with its own 12 DIP inset.
void AppendFailureBlock(UIElementCollection const& body, std::wstring const& reason) {
  auto row = kit::MakePaneRow(40);
  TextBlock line;
  if (auto style = kit::StyleByKey(L"UrRowTitleStyle")) line.Style(style);
  line.Foreground(urnw::colors::DangerBrush());
  line.Text(H(reason));
  row.Child(line);
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

// THE file's only MakePaneKeyValueRow call site, and now that is enforced rather
// than asserted in a comment: the identifier is poisoned everywhere else in this
// translation unit (see the block under the includes), and this function is the
// window where it is spelled out.
//
// FUNCTION SCOPE, NOT STATEMENT SCOPE. Putting the directives inside the `for`
// body is one line tighter and reads - wrongly - as if the preprocessor ran per
// iteration. Directives are processed once, at translation, but a reader should
// not have to know that to read a loop.
//
// WHAT THE WINDOW COSTS, COUNTED HONESTLY. Everything between the #undef and the
// #define is un-poisoned: this eight-line function, AND - the part that actually
// matters - the position immediately after its closing brace, which is exactly
// where someone adding a sibling helper "just after AppendFieldRows" would type.
// That is a bigger hole than a second call inside the funnel, and it is the one
// to watch. The #define below sits flush against the brace with no blank line so
// the boundary is visible, and it is marked; a new function belongs BELOW it.
//
// The DRAWN blank and the SPOKEN blank are different strings and go in through
// different parameters, so one row cannot acquire two writers of its name.
#undef MakePaneKeyValueRow
void AppendFieldRows(StackPanel const& panel, std::vector<InspectField> const& fields) {
  auto body = panel.Children();
  for (auto const& field : fields)
    body.Append(kit::MakePaneKeyValueRow(H(field.key),
                                         winrt::hstring{RailValueOr(field.value)}, 34,
                                         winrt::hstring{RailSpokenValueOr(field.value)})
                    .root);
}
// <-- THE WINDOW ENDS HERE. New functions go BELOW this line, not above it.
#define MakePaneKeyValueRow MakePaneKeyValueRow_bypasses_RailValueOr_use_AppendFieldRows

// POPULATE ONLY. No storyboard, so the density switch can call this on a rail
// that is already on screen without the rail flashing.
void PopulateConversation(InspectRailView const& v, demo::Conversation const& conv,
                          bool advanced) {
  auto panel = BodyOf(v.conversationScroll);
  if (!panel) return;
  auto body = panel.Children();
  body.Clear();
  body.Append(MakeSubjectRow(conv));

  body.Append(kit::MakePaneGroupHeader(L"MEMBERS", H(std::format(L"{}", conv.members.size())))
                  .root);
  if (conv.members.empty()) {
    body.Append(kit::MakePaneEmptyLine(L"No members"));
  } else {
    for (auto const& member : conv.members) body.Append(MakeMemberRow(member).root);
  }

  body.Append(kit::MakePaneGroupHeader(L"RETENTION").root);
  AppendFieldRows(panel, BuildConversationFields(conv, advanced));
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

  body.Append(kit::MakePaneGroupHeader(L"MESSAGE").root);
  // AppendFieldRows, NOT the kit builder - the brief's line here was
  // `kit::MakePaneKeyValueRow(H(field.key), H(field.value), 34)`, which is the
  // one call in the module that would skip RailValueOr, on the one mode that
  // holds every blank value in the world. It no longer compiles; see the block
  // under this file's includes.
  AppendFieldRows(panel, BuildMessageFields(conv, row, advanced));
}

}  // namespace

InspectRailView MakeInspectRail() {
  InspectRailView v;

  Grid root;
  // The view paints its own surface rather than depending on the host's style:
  // a view module that is transparent unless its container happens to carry
  // UrPaneStyle is a module with an invisible dependency.
  root.Background(urnw::colors::BackgroundBrush());
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  root.RowDefinitions().Append(headerRow);
  root.RowDefinitions().Append(bodyRow);

  // The 40 DIP strip every pane in this window opens with, by the same two keys
  // MainWindow.xaml uses in markup (lines 180-188), so a pane built in code and
  // a pane declared in XAML are the same pane.
  Border header;
  if (auto style = kit::StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  TextBlock title;
  if (auto style = kit::StyleByKey(L"UrPaneTitleStyle")) title.Style(style);
  title.Text(L"Details");
  header.Child(title);
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

kit::PaneListRow MakeMemberRow(demo::MemberRef const& member) {
  auto row = kit::MakePaneListRow(36);
  const size_t online = OnlineDeviceCount(member);
  row.dot.Fill(0 < online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                          : urnw::colors::FaintBrush());

  std::wstring title = member.displayName;
  if (member.admin) title += L" \u00B7 Admin";  // U+00B7 MIDDLE DOT
  row.title.Text(H(title));

  // The meta states the presence in WORDS. That is what entitles the dot beside
  // it to remain a restatement - BuildPaneListRowParts marks that dot Raw on
  // exactly that assumption (UrComponents.cpp:246-248) - and it is how this row
  // obeys "colour is never the only carrier of state".
  const std::wstring presence = std::format(L"{}/{} online", online, member.devices.size());
  row.meta.Text(H(presence));

  // MakePaneListRow does NOT mark title and meta Raw; only MakePaneListRowButton
  // does (UrComponents.cpp:295-298). Without these two lines the row's own name
  // is announced and then both children are announced again after it - the exact
  // triple announcement the row name exists to prevent. A THIRD caller needing
  // this is the signal to move the two calls down into BuildPaneListRowParts so
  // both row forms carry it; MakeDeviceRow below is the second.
  automation::AutomationProperties::SetAccessibilityView(
      row.title, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetAccessibilityView(
      row.meta, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetName(row.root, H(title + L", " + presence));
  return row;
}

kit::PaneListRow MakeDeviceRow(demo::DeviceRef const& device) {
  auto row = kit::MakePaneListRow(36);
  row.dot.Fill(device.online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                             : urnw::colors::FaintBrush());

  std::wstring title = device.name;
  const std::wstring owner =
      device.isThisComputer ? std::wstring{L"This computer"} : device.ownerName;
  if (!owner.empty()) title += L" \u00B7 " + owner;  // U+00B7 MIDDLE DOT
  row.title.Text(H(title));

  // Same rule as MakeMemberRow: the WORD carries the state and the dot restates
  // it. An online device reads "online"; an offline one reads when it was last
  // seen. Nothing here is legible only by hue.
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

}  // namespace urmsg::views
