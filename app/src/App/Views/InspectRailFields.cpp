// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader>, the same as Demo/DemoWorld.cpp
// and Views/ThreadLayout.cpp. Startup.cpp's CollectDiagnostics() calls the two
// probes at the foot of this file from wWinMain BEFORE winrt::init_apartment()
// (main.cpp:168 vs :183), so "pure C++, no winrt" has to be a property the
// compiler enforces rather than a comment at the top of a file.
#include "Views/InspectRailFields.h"

#include <format>

namespace urmsg::views {
namespace {

// U+2026 HORIZONTAL ELLIPSIS. Written as an escape and never as a pasted
// character: this project compiles /utf-8 and an editing pass that silently
// re-encodes a pasted glyph leaves no build error behind, only a wrong byte in
// a string. The probe prints it back through AsciiOnly() as "<U+2026>", so the
// code point is checkable from --diagnose's own output.
constexpr wchar_t kEllipsis = L'\u2026';

// Renders a string so a --diagnose line stays ASCII. --diagnose writes UTF-8
// bytes through a redirected handle (Startup.cpp:156-171) and Windows
// PowerShell 5.1's Get-Content decodes ANSI by default, so a non-ascii
// character comes back as mojibake in the one place these lines are read.
// Anything outside printable ASCII is printed as <U+XXXX> instead.
std::wstring AsciiOnly(std::wstring const& s) {
  std::wstring out;
  for (wchar_t c : s) {
    if (c < 0x20 || 0x7E < c)
      out += std::format(L"<U+{:04X}>", static_cast<unsigned int>(c));
    else
      out.push_back(c);
  }
  return out;
}

}  // namespace

std::wstring RetentionClassLabel(demo::RetentionClass retention) {
  return retention == demo::RetentionClass::Eph ? L"Disappearing" : L"Permanent";
}

std::wstring AttestationLabel(bool verified) {
  // G4, and this is the whole of it in two strings. "Verified" / "Not verified"
  // states that a check RAN and returned a result; this binary runs no check,
  // and MessageInspect::attestationVerified is a boolean DemoWorld derives from
  // the row's delivery state (DemoWorld.cpp:171). Naming the model keeps the
  // boolean visible - the rail still distinguishes the two states, which is the
  // point of the field - while making the value unquotable as URmessage
  // asserting a verification it never performed. "DEMO" is already this app's
  // word for the fabricated world (MainWindow.xaml.cpp:47), and the watermark
  // that carries it can be switched off (--demo-watermark=off), so the value
  // has to carry its own framing rather than lean on the chip.
  return verified ? L"Demo model: verified" : L"Demo model: not verified";
}

std::wstring DeliveryLabel(demo::DeliveryState state) {
  switch (state) {
    case demo::DeliveryState::Pending:   return L"Pending";
    case demo::DeliveryState::Sent:      return L"Sent";
    case demo::DeliveryState::Delivered: return L"Delivered";
    case demo::DeliveryState::Read:      return L"Read";
    case demo::DeliveryState::Failed:    return L"Failed";
    case demo::DeliveryState::Expired:   return L"Expired";
  }
  return L"Unknown";
}

std::wstring SenderLabel(demo::MessageRow const& row) {
  // No fallback chain: MessageInspect::senderDisplayName is documented ALWAYS
  // populated, run continuations included, so an empty one is a DemoWorld defect
  // and InspectRailFieldsProbe reports it as a blank "Sender" value.
  if (row.outgoing) return L"You";
  return row.inspect.senderDisplayName;
}

std::wstring ShortHex(std::wstring const& hex, size_t keep) {
  if (hex.size() <= keep + 8) return hex;
  return hex.substr(0, keep) + kEllipsis + hex.substr(hex.size() - 4);
}

size_t OnlineDeviceCount(demo::MemberRef const& member) {
  size_t online = 0;
  for (auto const& device : member.devices)
    if (device.online) ++online;
  return online;
}

std::vector<InspectField> BuildMessageFields(demo::Conversation const& conv,
                                             demo::MessageRow const& row, bool advanced) {
  std::vector<InspectField> out;
  out.push_back({L"Sender", SenderLabel(row)});
  out.push_back({L"Sent", row.inspect.sentAtLabel});
  out.push_back({L"Received", row.inspect.receivedAtLabel});
  out.push_back({L"Epoch", std::format(L"{}", row.inspect.epoch)});
  out.push_back({L"Sender leaf index", std::format(L"{}", row.inspect.senderLeafIndex)});
  out.push_back({L"Retention class", RetentionClassLabel(row.inspect.retention)});
  out.push_back({L"Size", row.inspect.sizeBucket});
  out.push_back({L"Attestation", AttestationLabel(row.inspect.attestationVerified)});
  if (!advanced) return out;
  // Design §6.6's four Advanced additions for this surface - "raw ids, hex group
  // id, leaf index, wire size" - minus the leaf index, which §6.3 already put in
  // the normal list above. Emitted UNCONDITIONALLY, because the eight-then-four
  // shape is read BY POSITION downstream (tasks R3 and R4 check the eighth row
  // and the twelfth), so a row cannot be dropped without moving every row after
  // it.
  //
  // MEASURED, AND IT DOES NOT HOLD: DemoWorld gives a DIRECT conversation an
  // EMPTY groupIdHex (DemoWorld.cpp:207) and copies it onto every message in it
  // (:173), so a message in one of the six DMs has nothing to put here. That is
  // 24 of the world's 60 message rows. InspectRailFieldsProbe sweeps every
  // message row and reports the blank by key and by conversation kind rather
  // than letting a screenshot find it; the fix belongs to whoever renders these
  // rows (skip an empty value) or to DemoWorld, not to the field order.
  out.push_back({L"Group id", ShortHex(row.inspect.groupIdHex)});
  out.push_back({L"Wire size", std::format(L"{} bytes", row.inspect.wireSizeBytes)});
  out.push_back({L"Message id", row.id});
  out.push_back({L"Conversation id", conv.id});
  return out;
}

std::vector<InspectField> BuildConversationFields(demo::Conversation const& conv,
                                                  bool advanced) {
  std::vector<InspectField> out;
  out.push_back({L"Retention", conv.retentionLabel});
  out.push_back({L"Media", conv.mediaRetentionLabel});
  if (!advanced) return out;
  out.push_back({L"Conversation id", conv.id});
  // GROUPS ONLY, and this is the one place the two modes differ on purpose:
  // Conversation::groupIdHex is documented non-empty for a group, which means
  // empty for a direct message, so a DM would get a 34 DIP row of nothing.
  // Message mode has no such freedom - its row count is read by position - which
  // is why the same emptiness is reported there instead of hidden.
  if (conv.kind == demo::ConversationKind::Group)
    out.push_back({L"Group id", ShortHex(conv.groupIdHex)});
  return out;
}

demo::MessageRow const* PickInspectMessage(demo::Conversation const& conv) {
  demo::MessageRow const* lastMessage = nullptr;
  demo::MessageRow const* lastRead = nullptr;
  for (auto const& row : conv.rows) {
    if (row.kind != demo::RowKind::Message) continue;
    lastMessage = &row;
    if (row.outgoing && row.state == demo::DeliveryState::Read) lastRead = &row;
  }
  return lastRead ? lastRead : lastMessage;
}

demo::Conversation const* FindConversation(demo::World const& world, std::wstring const& id) {
  for (auto const& conv : world.conversations)
    if (conv.id == id) return &conv;
  return nullptr;
}

demo::MessageRow const* FindMessageRow(demo::Conversation const& conv, std::wstring const& id) {
  for (auto const& row : conv.rows)
    if (row.id == id) return &row;
  return nullptr;
}

bool ReadByIsSubsetOfDeliveredTo(demo::MessageInspect const& inspect) {
  for (auto const& reader : inspect.readBy) {
    bool found = false;
    for (auto const& target : inspect.deliveredTo) {
      // By id, not by (name, owner): DeviceRef::id is documented stable, and two
      // devices that happen to share a name would otherwise pass a check whose
      // whole point is device identity.
      if (target.id == reader.id) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

std::wstring InspectRailFieldsProbe() {
  auto const& world = demo::GetWorld();
  if (world.conversations.empty())
    return L"  inspect rail     : FAIL - the demo world has no conversations";
  auto const& conv = world.conversations.front();
  demo::MessageRow const* row = PickInspectMessage(conv);
  if (row == nullptr)
    return L"  inspect rail     : FAIL - conversation 0 has no message row";

  const auto normal = BuildMessageFields(conv, *row, false);
  const auto advanced = BuildMessageFields(conv, *row, true);

  // PROPERTIES, not spot checks. "normal[0].value is non-empty" cannot fail by
  // construction and would print PASS over a column of blanks; these two can
  // fail, and both name the key that tripped them so the report is actionable.
  // A blank value is a 34 DIP row of nothing; an over-long one overflows the
  // column (see kInspectValueMaxChars).
  size_t blanks = 0;
  size_t widest = 0;
  std::wstring widestKey = L"(none)";
  for (auto const& f : advanced) {
    if (f.value.empty()) ++blanks;
    if (widest < f.value.size()) {
      widest = f.value.size();
      widestKey = f.key;
    }
  }

  // ORDER, PER ROW. A count is blind to permutation: swapping the "Epoch" and
  // "Sender leaf index" push_backs leaves 8 and 12 intact, leaves no blanks and
  // leaves the widest value where it was, while every screenshot check that
  // reads these rows by position (R3 step 5, R4 step 1) reads the wrong one.
  // All twelve keys are distinct, so any swap moves at least two of them.
  constexpr size_t kKeyCount = 12;
  static constexpr wchar_t const* kExpectedKeys[kKeyCount] = {
      L"Sender",          L"Sent",      L"Received",   L"Epoch",
      L"Sender leaf index", L"Retention class", L"Size", L"Attestation",
      L"Group id",        L"Wire size", L"Message id", L"Conversation id"};
  size_t keysWrong = 0;
  for (size_t i = 0; i < advanced.size(); ++i)
    if (kKeyCount <= i || advanced[i].key != kExpectedKeys[i]) ++keysWrong;

  // R4 step 1 requires the twelve to be "the same eight, in the same order, then
  // Group id, ...". That is a relationship between the two calls, not a property
  // of either, so it is checked as one: key AND value, position by position.
  size_t prefixWrong = 0;
  for (size_t i = 0; i < normal.size(); ++i)
    if (advanced.size() <= i || advanced[i].key != normal[i].key ||
        advanced[i].value != normal[i].value)
      ++prefixWrong;

  // Conversation mode is a RULE over all the conversations, not one magic
  // number: 2 rows at normal density, +1 for the conversation id, +1 more for a
  // group's hex group id.
  //
  // And then the rows themselves, because the counts alone are blind in exactly
  // the way a census is: "Retention" and "Media" carry two DIFFERENT DemoWorld
  // strings ("Kept until deleted" / "Media kept 30 days"), and swapping them
  // leaves both sizes correct and both values non-blank.
  size_t convOk = 0;
  size_t convRowsWrong = 0;
  for (auto const& c : world.conversations) {
    const size_t expected = (c.kind == demo::ConversationKind::Group) ? 4u : 3u;
    const auto cn = BuildConversationFields(c, false);
    const auto ca = BuildConversationFields(c, true);
    if (cn.size() == 2 && ca.size() == expected) ++convOk;
    if (cn.size() != 2 || cn[0].key != L"Retention" || cn[0].value != c.retentionLabel ||
        cn[1].key != L"Media" || cn[1].value != c.mediaRetentionLabel)
      ++convRowsWrong;
    else if (ca.size() < 3 || ca[2].key != L"Conversation id" || ca[2].value != c.id)
      ++convRowsWrong;
    else if (c.kind == demo::ConversationKind::Group &&
             (ca.size() != 4 || ca[3].key != L"Group id" || ca[3].value.empty() ||
              ca[3].value.compare(0, 8, c.groupIdHex, 0, 8) != 0))
      ++convRowsWrong;
  }

  // SWEEP THE SIBLINGS. The blank check above looks at ONE row of ONE
  // conversation - conversation 0, which is a group - and the rail renders these
  // fields for whichever message is selected in whichever conversation is open.
  // Swept over all of them, the fixture has two kinds of blank and they are
  // named here rather than left for a screenshot:
  //
  //   * "Group id" on a DIRECT conversation. DemoWorld leaves
  //     Conversation::groupIdHex empty for a DM (DemoWorld.cpp:207) and copies
  //     it onto every message (:173). 24 of the 60 message rows.
  //   * "Received" on an OUTGOING row that is not Delivered or Read. DemoWorld
  //     only writes receivedAtLabel once a device has said so (:177-185), which
  //     is right - nothing has been received until one has - but the rail still
  //     draws the row. 4 rows.
  //
  // THE LIMIT, STATED: this gate does NOT require zero blanks, because the
  // fixture cannot satisfy that and DemoWorld is immutable. It requires that
  // every blank be one of the two above, so a blank of any OTHER shape - a
  // missing size bucket, an empty sender, a lost id - fails it and is named.
  size_t sweepRows = 0;
  size_t sweepValues = 0;
  size_t sweepBlanks = 0;
  size_t blankGroupIdOnDirect = 0;
  size_t blankReceivedNotYet = 0;
  size_t sweepUnexpected = 0;
  std::wstring unexpectedKey = L"(none)";
  for (auto const& c : world.conversations) {
    const bool direct = (c.kind == demo::ConversationKind::Direct);
    for (auto const& r : c.rows) {
      if (r.kind != demo::RowKind::Message) continue;
      ++sweepRows;
      const bool notYetReceived = r.outgoing &&
                                  r.state != demo::DeliveryState::Delivered &&
                                  r.state != demo::DeliveryState::Read;
      for (auto const& f : BuildMessageFields(c, r, true)) {
        ++sweepValues;
        if (!f.value.empty()) continue;
        ++sweepBlanks;
        if (f.key == L"Group id" && direct)
          ++blankGroupIdOnDirect;
        else if (f.key == L"Received" && notYetReceived)
          ++blankReceivedNotYet;
        else {
          ++sweepUnexpected;
          unexpectedKey = f.key;
        }
      }
    }
  }

  // THE 28-CHARACTER BUDGET, PROVED LIVE. Nothing in the demo world comes near
  // 28, so "widest <= 28" above passes whether the scan works or not - an
  // inverted comparison, or one measuring f.key, would print the same PASS. The
  // same scan is run here over a LOCAL COPY of the picked row carrying one
  // deliberately over-long value (DemoWorld itself is immutable and is not
  // touched), and is required to trip on it and to name the right key.
  demo::MessageRow overlong = *row;
  overlong.inspect.sizeBucket.assign(kInspectValueMaxChars + 1, L'x');
  size_t overWidest = 0;
  std::wstring overKey = L"(none)";
  for (auto const& f : BuildMessageFields(conv, overlong, true))
    if (overWidest < f.value.size()) {
      overWidest = f.value.size();
      overKey = f.key;
    }
  const bool budgetGateLive = kInspectValueMaxChars < overWidest &&
                              overWidest == kInspectValueMaxChars + 1 && overKey == L"Size";

  // SHORTHEX'S TRUNCATING BRANCH IS UNREACHABLE FROM THE FIXTURE, so asserting
  // it over the world would certify a rule no value in the world can trip: every
  // groupIdHex DemoWorld builds is HexOf(seed, 8) = 16 characters, which is
  // `keep + 8` exactly and comes back whole - and the ones it does not build are
  // empty, which is shorter still. Three local cases instead - the boundary, one
  // past it, and one well past it - plus the assertion that conversation 0's id
  // really is at the boundary, so the paragraph above cannot quietly go stale.
  const std::wstring kLongHex = L"0123456789abcdef0123456789abcdef";  // 32
  const std::wstring boundary = kLongHex.substr(0, 16);               // keep + 8
  const std::wstring justOver = kLongHex.substr(0, 17);               // one more
  std::wstring expectedShort = kLongHex.substr(0, 8);
  expectedShort += kEllipsis;
  expectedShort += kLongHex.substr(kLongHex.size() - 4);
  const std::wstring shortened = ShortHex(kLongHex);
  const bool shortHexOk = shortened == expectedShort && shortened.size() == 13 &&
                          shortened[8] == kEllipsis && ShortHex(boundary) == boundary &&
                          ShortHex(justOver) != justOver && ShortHex(justOver).size() == 13 &&
                          conv.groupIdHex.size() == 16 &&
                          ShortHex(conv.groupIdHex) == conv.groupIdHex;

  // THE LABEL HELPERS, each against a DemoWorld field the helper never reads, so
  // that none of these is the helper compared to its own body.
  size_t retentionWrong = 0;
  size_t ephRows = 0;
  size_t senderWrong = 0;
  size_t senderBlank = 0;
  size_t namedRows = 0;
  size_t attestationWrong = 0;
  for (auto const& c : world.conversations)
    for (auto const& r : c.rows) {
      if (r.kind != demo::RowKind::Message) continue;
      // Retention: the label must track Conversation::disappearing, which
      // RetentionClassLabel cannot see. An INVERTED ternary there leaves every
      // count on this line identical and every row wrong - the exact mutation a
      // census gate earlier in this plan did not catch. Both sides are populated
      // (conv-marta is the disappearing one), so neither arm is vacuous.
      const bool saysEph = (RetentionClassLabel(r.inspect.retention) == L"Disappearing");
      if (saysEph != c.disappearing) ++retentionWrong;
      if (c.disappearing) ++ephRows;
      // Sender: MessageRow::senderName is the BUBBLE's own name line, a separate
      // field SenderLabel does not read. Where DemoWorld populated it the two
      // must agree, which is what catches an inverted `row.outgoing` (outgoing
      // rows would still read "You", incoming ones would not).
      const std::wstring who = SenderLabel(r);
      if (who.empty()) ++senderBlank;
      if (!r.senderName.empty()) {
        ++namedRows;
        if (who != r.senderName) ++senderWrong;
      } else if (r.outgoing && who != L"You") {
        ++senderWrong;
      }
      // Attestation: DemoWorld sets attestationVerified from the delivery state
      // (DemoWorld.cpp:171). A constant-returning AttestationLabel fails here on
      // c0-r22, the world's one Failed row - without that row this would be
      // vacuous, and it is the only one, so it is worth saying that it exists.
      const bool saysVerified =
          (AttestationLabel(r.inspect.attestationVerified) == AttestationLabel(true));
      if (saysVerified != (r.state != demo::DeliveryState::Failed)) ++attestationWrong;
    }

  // G4, made mechanical. The two attestation values must stay distinguishable
  // from each other AND must not be the bare words a viewer reads as a completed
  // cryptographic check. This is a regression guard on a project constraint, not
  // a fact about the world: if a later pass "simplifies" the copy back to
  // "Verified", this line is what says so.
  const bool attestationFramed = AttestationLabel(true) != AttestationLabel(false) &&
                                 !AttestationLabel(true).empty() &&
                                 !AttestationLabel(false).empty() &&
                                 AttestationLabel(true) != L"Verified" &&
                                 AttestationLabel(false) != L"Not verified";

  // DeliveryLabel over the ENUM rather than the world: the world never reaches
  // Expired, so a world sweep would leave that case uncovered. A dropped switch
  // case falls through to "Unknown"; two cases returning one word would make two
  // states indistinguishable in the rail and in the glyph's accessible name.
  constexpr size_t kStateCount = 6;
  constexpr demo::DeliveryState kStates[kStateCount] = {
      demo::DeliveryState::Pending, demo::DeliveryState::Sent,
      demo::DeliveryState::Delivered, demo::DeliveryState::Read,
      demo::DeliveryState::Failed, demo::DeliveryState::Expired};
  size_t deliveryOk = 0;
  for (size_t i = 0; i < kStateCount; ++i) {
    const std::wstring d = DeliveryLabel(kStates[i]);
    bool good = !d.empty() && d != L"Unknown";
    for (size_t j = 0; j < i; ++j)
      if (DeliveryLabel(kStates[j]) == d) good = false;
    if (good) ++deliveryOk;
  }

  const bool ok = normal.size() == 8 && advanced.size() == 12 &&
                  normal[0].key == L"Sender" && advanced[8].key == L"Group id" &&
                  keysWrong == 0 && prefixWrong == 0 && blanks == 0 &&
                  widest <= kInspectValueMaxChars && budgetGateLive && shortHexOk &&
                  convOk == world.conversations.size() && convRowsWrong == 0 &&
                  0 < sweepRows && sweepUnexpected == 0 && retentionWrong == 0 &&
                  0 < ephRows && ephRows < sweepRows && senderWrong == 0 &&
                  senderBlank == 0 && 0 < namedRows && attestationWrong == 0 &&
                  attestationFramed && deliveryOk == kStateCount;

  return std::format(
      L"  inspect rail     : {} - message fields {}/8 normal {}/12 advanced, "
      L"conversation fields correct in {}/{}, blank values {}, widest value {} "
      L"(\"{}\") of {} allowed, sender \"{}\"; keys out of order {}, normal not a "
      L"prefix of advanced in {} row(s), conversation rows wrong {}; world sweep "
      L"{} values over {} message rows, {} blank = {} \"Group id\" on a direct "
      L"conversation + {} \"Received\" on an outgoing row not yet delivered + {} "
      L"unexpected (\"{}\"); the width scan trips at {} on a locally over-long "
      L"\"{}\"; ShortHex(32) = \"{}\", boundary and world-16 whole, ok {}; "
      L"labels: retention {} wrong over {} message rows ({} disappearing), "
      L"sender {} wrong and {} blank over {} named rows, attestation {} wrong, "
      L"G4 framing {}, delivery {}/{} distinct",
      ok ? L"PASS" : L"FAIL", normal.size(), advanced.size(), convOk,
      world.conversations.size(), blanks, widest, widestKey, kInspectValueMaxChars,
      normal.empty() ? std::wstring{L"(none)"} : normal[0].value, keysWrong, prefixWrong,
      convRowsWrong, sweepValues, sweepRows, sweepBlanks, blankGroupIdOnDirect,
      blankReceivedNotYet, sweepUnexpected, unexpectedKey, overWidest, overKey,
      AsciiOnly(shortened), shortHexOk ? L"yes" : L"no", retentionWrong, sweepRows, ephRows,
      senderWrong, senderBlank, namedRows, attestationWrong, attestationFramed ? L"yes" : L"no",
      deliveryOk, kStateCount);
}

std::wstring InspectRailDeviceProbe() {
  auto const& world = demo::GetWorld();
  if (world.conversations.empty())
    return L"  inspect devices  : FAIL - the demo world has no conversations";

  size_t messages = 0;
  size_t withReaders = 0;
  size_t consistent = 0;
  for (auto const& conv : world.conversations) {
    for (auto const& row : conv.rows) {
      if (row.kind != demo::RowKind::Message) continue;
      ++messages;
      // A row with no readers satisfies the subset property VACUOUSLY, so it is
      // not evidence and is not counted. withReaders is the denominator that
      // makes the ratio mean something: without it a world with zero read
      // receipts anywhere reports a perfect score.
      if (row.inspect.readBy.empty()) continue;
      ++withReaders;
      if (ReadByIsSubsetOfDeliveredTo(row.inspect)) ++consistent;
    }
  }

  auto const& conv = world.conversations.front();
  demo::MessageRow const* pick = PickInspectMessage(conv);
  const size_t delivered = pick ? pick->inspect.deliveredTo.size() : 0;
  const size_t read = pick ? pick->inspect.readBy.size() : 0;

  // AND THE COUNTER-EXAMPLES, because `consistent == withReaders` above is
  // satisfied WHOLE by `return true;`. DemoWorld builds readBy by copying
  // deliveredTo (DemoWorld.cpp:184, :189), so no row anywhere in the fixture can
  // make ReadByIsSubsetOfDeliveredTo return false, and a 47-of-47 score would be
  // printed by a function that never looked. Two local readers, built on a copy
  // of the pick's own inspect:
  //
  //   * a GHOST - a device id, name and owner the delivered list has never seen.
  //   * a NAME TWIN - a second device of an existing name and owner, differing
  //     only in id. This is the one that separates the implementation from a
  //     name comparison: comparing by name accepts the twin and would still
  //     reject the ghost, so requiring BOTH rejections is what pins it to id.
  bool acceptsThePick = false;
  size_t rejected = 0;
  if (pick != nullptr && !pick->inspect.deliveredTo.empty()) {
    acceptsThePick = ReadByIsSubsetOfDeliveredTo(pick->inspect);

    demo::MessageInspect ghosted = pick->inspect;
    demo::DeviceRef ghost = ghosted.deliveredTo.front();
    ghost.id = L"dev-never-delivered";
    ghost.name = L"Unknown handset";
    ghost.ownerName = L"Nobody";
    ghosted.readBy.push_back(ghost);
    if (!ReadByIsSubsetOfDeliveredTo(ghosted)) ++rejected;

    demo::MessageInspect twinned = pick->inspect;
    demo::DeviceRef twin = twinned.deliveredTo.front();
    twin.id = L"dev-second-of-that-name";  // ONLY the id differs
    twinned.readBy.push_back(twin);
    if (!ReadByIsSubsetOfDeliveredTo(twinned)) ++rejected;
  }

  // PickInspectMessage's other two arms are unreachable from the fixture: all
  // eight conversations carry an outgoing row in state Read, so the first arm
  // always wins and neither "else the last message row" nor "else nullptr" is
  // ever taken. Built locally instead, from copies of conversation 0's rows:
  // one conversation with messages but no read outgoing row (and a trailing
  // SYSTEM row the kind filter has to skip), one with only that system row, and
  // one with no rows at all.
  demo::Conversation noRead{};
  noRead.kind = demo::ConversationKind::Direct;
  for (auto const& r : conv.rows)
    if (r.kind == demo::RowKind::Message &&
        !(r.outgoing && r.state == demo::DeliveryState::Read))
      noRead.rows.push_back(r);
  const std::wstring lastMessageId =
      noRead.rows.empty() ? std::wstring{L"(none)"} : noRead.rows.back().id;
  demo::MessageRow trailingSystem{};
  trailingSystem.kind = demo::RowKind::System;
  trailingSystem.id = L"local-system-row";
  trailingSystem.systemText = L"a locally built system row";
  noRead.rows.push_back(trailingSystem);
  demo::MessageRow const* fallback = PickInspectMessage(noRead);

  demo::Conversation systemOnly{};
  systemOnly.rows.push_back(trailingSystem);
  demo::Conversation nothing{};

  // The pick and DemoWorld's kInspectTargetRowId are TWO LIVE DESIGNATIONS of
  // one message: MainWindow.xaml.cpp draws the thread's selection outline from
  // the constant, and the rail takes its message-mode subject from this
  // function. They agree today (c0-r12) and the whole reason PickInspectMessage
  // is a shared pure function is that they must keep agreeing. If a later task
  // repoints the constant, this is the line that says the outline and the rail
  // have parted - it is not a check to delete, it is the decision to re-take.
  const bool pickOk = pick != nullptr && pick->id == demo::kInspectTargetRowId &&
                      fallback != nullptr && fallback->id == lastMessageId &&
                      PickInspectMessage(systemOnly) == nullptr &&
                      PickInspectMessage(nothing) == nullptr;

  // The lookups, by POINTER IDENTITY and over every row, not a spot check: "the
  // loop returns the first entry regardless of id" succeeds at index 0 and fails
  // at every other, which one lookup cannot tell apart from a correct one.
  size_t convFound = 0;
  size_t rowFound = 0;
  for (auto const& c : world.conversations)
    if (FindConversation(world, c.id) == &c) ++convFound;
  for (auto const& r : conv.rows)
    if (FindMessageRow(conv, r.id) == &r) ++rowFound;
  const bool missesAreNull = FindConversation(world, L"no-such-conversation") == nullptr &&
                             FindMessageRow(conv, L"no-such-row") == nullptr;
  const bool lookupsOk = convFound == world.conversations.size() &&
                         rowFound == conv.rows.size() && missesAreNull;

  // OnlineDeviceCount against DemoWorld's own structure rather than against
  // itself: every member gets a phone that is online, an admin gets a second
  // device that is not (DemoWorld.cpp:78-86). So the online total must equal the
  // member count, the device total must equal members + admins, and exactly the
  // admins may be short of full. `return devices.size()` breaks the first,
  // `return 0` breaks it the other way, and counting !online breaks it too.
  // Per member, the count is also compared against how many of that member's
  // devices report lastSeenLabel "now" - a different field, though a correlated
  // one; the mutation it cannot see is a count clamped to 1, which the totals
  // above also do not see, and that is stated rather than papered over.
  size_t members = 0;
  size_t memberDevices = 0;
  size_t onlineDevices = 0;
  size_t admins = 0;
  size_t onlineWrong = 0;
  size_t shortOfFull = 0;
  size_t shortAndAdmin = 0;
  for (auto const& c : world.conversations)
    for (auto const& m : c.members) {
      ++members;
      memberDevices += m.devices.size();
      if (m.admin) ++admins;
      const size_t up = OnlineDeviceCount(m);
      onlineDevices += up;
      size_t byLastSeen = 0;
      for (auto const& d : m.devices)
        if (d.lastSeenLabel == L"now") ++byLastSeen;
      if (up != byLastSeen || m.devices.size() < up) ++onlineWrong;
      if (up < m.devices.size()) {
        ++shortOfFull;
        if (m.admin) ++shortAndAdmin;
      }
    }
  const bool devicesOk = 0 < members && 0 < admins && onlineWrong == 0 &&
                         onlineDevices == members && memberDevices == members + admins &&
                         shortOfFull == admins && shortAndAdmin == admins;

  // The pick's own receipt counts are printed as FACTS, not gated on: the
  // fallback pick is legitimately allowed to be an incoming row with none.
  const bool ok = pick != nullptr && 0 < withReaders && consistent == withReaders &&
                  acceptsThePick && rejected == 2 && pickOk && lookupsOk && devicesOk;

  return std::format(
      L"  inspect devices  : {} - pick \"{}\" state {} delivered-by {} read-by {}; "
      L"read-by is a subset of delivered-by in {}/{} rows that have readers, of "
      L"{} message rows; the check accepts the pick ({}) and rejects {}/2 local "
      L"counter-examples (an unknown device id, and a second device of an "
      L"existing name); pick == kInspectTargetRowId \"{}\", falls back to \"{}\" "
      L"with no read row and to null with none, ok {}; lookups resolve {}/{} "
      L"conversations and {}/{} rows of conversation 0 by id, misses null {}; "
      L"devices {} online of {} across {} members ({} admin), {} short of full, "
      L"{} wrong",
      ok ? L"PASS" : L"FAIL", pick ? pick->id : std::wstring{L"(none)"},
      pick ? DeliveryLabel(pick->state) : std::wstring{L"-"}, delivered, read, consistent,
      withReaders, messages, acceptsThePick ? L"yes" : L"no", rejected,
      demo::kInspectTargetRowId, lastMessageId, pickOk ? L"yes" : L"no", convFound,
      world.conversations.size(), rowFound, conv.rows.size(), missesAreNull ? L"yes" : L"no",
      onlineDevices, memberDevices, members, admins, shortOfFull, onlineWrong);
}

}  // namespace urmsg::views
