// The inspector rail's CONTENT, with no XAML in it.
//
// The rail (design §6.3, D3) renders exactly what these functions return: a list
// of key/value pairs, in order, at one of two densities. Keeping that here is
// what makes the rail's content assertable in --diagnose - this repo has no
// test project - and leaves the view file below it holding nothing but layout.
//
// DEMO COPY IS LITERAL, NOT LOCALIZED, AND THAT IS A DECISION.
// Strings/en/Resources.resw is GENERATED from the separate urnetwork/
// localizations repo (Localization.h:3-4), so a demo-only label cannot be added
// to it. Design §2 scopes localization out and design §9.4 records the ruling.
// The demo surfaces are therefore the one place in this window that does not go
// through Localization.h - MainWindow.xaml's "no user-facing text lives in this
// file" rule is deliberately narrowed here, not broken by accident - and they go
// back through it the day the demo becomes product.
//
// NOTHING HERE CLAIMS A MESSAGE WAS ENCRYPTED, VERIFIED OR ATTESTED, and on this
// file that is a correctness constraint rather than a footnote. There is no
// protocol and no crypto behind this window: every value these functions return
// is fabricated by Demo/DemoWorld.cpp. So each label names a FIELD the rail
// would display, and no value is phrased as the result of a check this binary
// performed - see AttestationLabel, the one field where the difference is not
// academic.
//
// AND A FIELD THAT MUST STAY UNRENDERED, for whoever extends this list next.
// MessageInspect::cipher carries "XChaCha20-Poly1305" (DemoWorld.cpp:172) and is
// deliberately emitted by NOTHING here. A named cipher is a claim about what
// encrypted a message - the most specific such claim this fixture can make - and
// this binary has no crypto to have used it. It is not in design 6.3's eight
// fields nor in 6.6's four Advanced additions, so nothing is missing; if a later
// task adds a "Cipher" row it is a G4 violation on its own, whatever the label
// says. The same test applies to any new field: does the value describe the
// MODEL, or assert something about the message?
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Demo/DemoSwitches.h"  // DemoScreen, for InitialRailMode - pure, no winrt
#include "Demo/DemoWorld.h"

namespace urmsg::views {

struct InspectField {
  std::wstring key;
  std::wstring value;
};

// The widest a value may be before kit::MakePaneKeyValueRow stops behaving. Its
// value column is Auto and its key column is Star (UrComponents.cpp:172-176), so
// a long value takes the width it wants and the KEY is what gets squeezed - the
// row does not ellipsize, it overflows the 360 DIP column. 28 characters of Neue
// Montreal at 13 is ~196 DIP, which leaves 110 for the widest key ("Sender leaf
// index"), 8 for the grid's ColumnSpacing and the row's own 24 of padding:
// 338 of 360. Asserted in InspectRailFieldsProbe rather than hoped for.
//
// NO VALUE IN THE DEMO WORLD COMES NEAR IT. "widest <= 28" over the fixture is
// therefore a rule nothing can trip, and a rule nothing can trip is
// indistinguishable from a width scan that cannot see. So the probe calls ONE
// scan function twice - once over the world's row, where the result is gated,
// and once over a locally over-long row, where it must trip. Literally the same
// function, not a second copy of the same loop: a demonstration that validates
// its own private duplicate proves nothing about the code under test.
inline constexpr size_t kInspectValueMaxChars = 28;

// The rail's retention vocabulary for a MESSAGE. Deliberately NOT the same row
// as conversation mode's: a message has a retention CLASS (a two-valued enum),
// a conversation has a retention POLICY that DemoWorld carries as a display
// string. Two questions, two keys - "Retention class" and "Retention" - so one
// 360 DIP column never shows two vocabularies under one word.
std::wstring RetentionClassLabel(demo::RetentionClass retention);

// The one field on this surface where the wording is a correctness question and
// not a copy preference. MessageInspect::attestationVerified is a boolean
// DemoWorld derives from the row's delivery state (DemoWorld.cpp:171); nothing
// in this binary verifies anything. So the value names what the RAIL is showing
// - a demo model's flag, and the reading that flag would carry - instead of
// asserting that a check ran and passed. The KEY stays "Attestation" (task R3
// checks the eight message keys by name); only the VALUE carries the framing,
// and InspectRailFieldsProbe asserts that it still does.
std::wstring AttestationLabel(bool verified);

// Named here rather than in the thread because the rail's --diagnose probe has
// to PRINT it (a verification step cannot check a delivery state it has no way
// to read), and because the thread's delivery glyph needs the same word for its
// accessible name - an unnamed glyph is a defect UrComponents.cpp has paid for
// twice.
std::wstring DeliveryLabel(demo::DeliveryState state);

// Who the rail names as the sender. MessageInspect::senderDisplayName is ALWAYS
// populated (DemoWorld.h), including on run continuations, so there is no
// fallback chain here and an empty one is a DemoWorld defect that the probe
// reports by key name.
std::wstring SenderLabel(demo::MessageRow const& row);

// "a1b2c3d4...9f0e": the first `keep` characters, one horizontal ellipsis, the
// last four. Returns `hex` unchanged when it already fits. Shortening happens
// HERE, where the result is a string a probe can measure, rather than in the
// view where it would depend on a trimming this row shape does not do.
//
// Every hex id DemoWorld actually BUILDS is exactly 16 characters -
// HexOf(seed, 8), DemoWorld.cpp:207 - which is `keep + 8` exactly, so every one
// of them comes back WHOLE. (A direct conversation is given no id at all rather
// than a short one; that emptiness is a separate problem, and the probe counts
// it.) The truncating branch is therefore unreachable from the fixture and is
// covered instead by cases the probe builds locally, on both sides of that
// boundary.
std::wstring ShortHex(std::wstring const& hex, size_t keep = 8);

// Message mode. Normal is design §6.3's eight fields, in its order. Advanced adds
// design §6.6's four: the hex group id, the wire size and the two raw ids. The
// leaf index is NOT one of them - §6.3 lists it among the normal fields and §6.6's
// summary table lists it again; §6.3 is the field list and wins, so the counts
// are 8 and 12 and the probe asserts both.
//
// Extending this list is a G4 decision, not a layout one - MessageInspect::cipher
// is the specific trap, and the file header says why.
std::vector<InspectField> BuildMessageFields(demo::Conversation const& conv,
                                             demo::MessageRow const& row, bool advanced);

// Conversation mode's retention block. The member list is not here: it is a list
// of person rows, not key/value pairs. Advanced adds the conversation id always,
// and the hex group id only for a GROUP - Conversation::groupIdHex is documented
// non-empty for groups, so emitting it for a direct message is a blank row.
std::vector<InspectField> BuildConversationFields(demo::Conversation const& conv,
                                                  bool advanced);

// ---- the section grouping the view draws around those fields (design d4 §9) --
//
// The field order is probe-pinned (kExpectedKeys in the .cpp) and normal must be
// a positional prefix of advanced, so grouping must be CONTIGUOUS and
// ORDER-PRESERVING: the view inserts captions and card frames BETWEEN rows at
// fixed positions and never permutes them. The mapping lives here - the pure TU
// the probes live in - so InspectRailFieldsProbe asserts the partition against
// the pinned key table instead of trusting a screenshot to read it.
//
// Retention is in the enum for the CONVERSATION mapping only (0-1 Retention,
// 2+ Advanced); MessageFieldGroup never returns it. The caption names are
// honest labels of the fields the rail already shows - the same register as the
// existing MEMBERS/RETENTION headers. PROTOCOL names the CATEGORY of two
// fields that already ship; it asserts nothing about what this binary did. No
// caption, row or tooltip may ever name a cipher (the file header's standing
// ruling on MessageInspect::cipher).
enum class InspectGroup { Delivery, Protocol, Message, Advanced, Retention };

// 0-2 Delivery (who and when), 3-4 Protocol (the model's metadata), 5-7 Message
// (this message's policy and shape), 8+ Advanced (the density-only additions).
InspectGroup MessageFieldGroup(size_t fieldIndex);
// 0-1 Retention, 2+ Advanced (conversation mode's retention block, then the
// density-only conversation id / group id).
InspectGroup ConversationFieldGroup(size_t fieldIndex);

// The caption strings live here too, so the rail's copy passes through the same
// review funnel as its field copy.
std::wstring_view InspectGroupCaption(InspectGroup group);

// ---- which MODE the rail opens in (design d4 §0, d7 R4) ----------------------
//
// This decision used to be an inline `if` in MainWindow::BuildInspectRail - a
// winrt translation unit --diagnose cannot reach, so the rail's fields and
// devices were probed but its MODE never was. Extracted pure so
// InspectRailDeviceProbe can assert what the mode SHOULD be. It still cannot
// assert which write lands last; that half is the wiring task's single-writer
// rule, and BuildInspectRail's comment block says so.
enum class RailMode { Conversation, Message };

// Message exactly when --demo=inspect was asked for AND the conversation has a
// message row to inspect; Conversation otherwise (the no-pick arm is the
// LogWarn at MainWindow.xaml.cpp's BuildInspectRail, NOT InspectRailView.cpp,
// which contains no LogWarn).
RailMode InitialRailMode(demo::DemoScreen screen, demo::Conversation const& conv);

// The message --demo=inspect opens on: the last OUTGOING row whose state is
// Read, else the last message row, else nullptr.
//
// The fixed contract (2) says --demo=inspect opens "the message named by
// DemoWorld's designated inspect target", but DemoWorld.h's World and
// Conversation carry NO such field - there is nothing to read. So the
// designation is THIS function: one pure, deterministic choice the thread
// surface calls for its selection outline and the rail calls for its message
// mode, which is what makes the two agree by construction rather than by luck.
demo::MessageRow const* PickInspectMessage(demo::Conversation const& conv);

// Re-resolve a subject by id. The rail holds IDS, never pointers into
// GetWorld()'s storage: design §9.2's ambient activity APPENDS a MessageRow to
// the open conversation's `rows`, and a vector append invalidates every
// MessageRow* into it. The pointers these return are for IMMEDIATE use inside
// one render call and must not be stored across one.
demo::Conversation const* FindConversation(demo::World const& world, std::wstring const& id);
demo::MessageRow const* FindMessageRow(demo::Conversation const& conv, std::wstring const& id);

// Delivered is a statement by a DEVICE, so a device that read a message must
// have received it first. Compared by DeviceRef::id, which DemoWorld.h documents
// as stable ("dev-pixel9"); comparing by name would pass on two different
// devices that happen to share one. If this is ever false the rail is rendering
// an impossible claim, so it is asserted rather than assumed.
bool ReadByIsSubsetOfDeliveredTo(demo::MessageInspect const& inspect);

// How many of a person's devices are online. The member row's right-hand meta
// says "2/3 online" in WORDS, which is the only condition under which
// BuildPaneListRowParts is entitled to mark that row's presence dot Raw
// (UrComponents.cpp:246-248), and the only way the row obeys "colour is never
// the only carrier of state".
size_t OnlineDeviceCount(demo::MemberRef const& member);

// ---- the delivered-by / read-by lists' empty decision (R4) -------------------
//
// The honest line rendered IN PLACE OF the card when a list has no devices -
// a bordered empty box would frame nothing. G4-safe copy: it reports an
// absence (no device has said anything), it never claims a check ran.
inline constexpr wchar_t kDeliveredEmptyNote[] = L"No device has acknowledged this message";
inline constexpr wchar_t kReadEmptyNote[] = L"No device has read this message";

// What one device list draws: one row per device, or - when there are none -
// exactly ONE empty line carrying the note above. The view's AppendDeviceList
// is built FROM this plan, so the --diagnose clause covering the empty branch
// (unreachable from what --demo=inspect can open: the pick is c0-r12 with
// seven in each list, and the fixture is immutable) asserts the same decision
// the view renders from rather than a private copy of it.
struct DeviceListPlan {
  size_t rowCount;              // devices.size(), or 1 when empty (the note line)
  std::wstring_view emptyNote;  // EMPTY when there are devices
};
DeviceListPlan PlanDeviceList(std::vector<demo::DeviceRef> const& devices, bool deliveredList);

// The two --diagnose lines, already in CollectDiagnostics()'s column format
// (two-space indent, 17-character label, " : "). They live here so Startup.cpp's
// edit is one guard and two push_backs - that file is a collision point for
// every surface task.
std::wstring InspectRailFieldsProbe();
std::wstring InspectRailDeviceProbe();

}  // namespace urmsg::views
