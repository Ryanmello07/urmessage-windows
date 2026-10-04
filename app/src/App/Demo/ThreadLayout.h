// Thread layout rules: what the thread surface decides about ROWS, with no
// XAML in it.
//
// PURE C++, like Demo/DemoWorld.h beside it and for the same reason. Every
// function here is called from CollectDiagnostics() (Startup.cpp), which
// wWinMain runs at main.cpp:168 - BEFORE winrt::init_apartment() at
// main.cpp:181. A winrt type in this header would be constructed with no
// apartment, and pch.h (which pulls in every winrt/ header) must not appear.
//
// Namespace is urmsg::views, not urmsg::demo: these are PRESENTATION rules
// about demo data - the pure half of Views/ThreadView.*, the same split as
// Views/StatusStripRules.h. Only the PATH is in Demo/, beside the data.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// ---- bubble width (design S6.2, Spec C S5.2) -----------------------------
// 68% of the column, capped at 640 DIP.
inline constexpr double kBubbleColumnFraction = 0.68;
inline constexpr double kBubbleMaxWidthDip = 640.0;

// A column that has not been measured yet - ActualWidth is 0 on the first
// layout pass - must NOT collapse every bubble to zero width. It gets the
// cap, so the first composed frame is a normal thread that the next pass
// narrows, rather than a column of slivers.
//
// No std::min: pch.h defines WIN32_LEAN_AND_MEAN but not NOMINMAX, so
// windows.h's min/max macros are live in every TU that includes this.
inline double BubbleMaxWidthDip(double columnWidthDip) {
  if (columnWidthDip <= 0.0) return kBubbleMaxWidthDip;
  const double w = columnWidthDip * kBubbleColumnFraction;
  return w > kBubbleMaxWidthDip ? kBubbleMaxWidthDip : w;
}

// ---- sender runs ---------------------------------------------------------
// A run is consecutive Message rows from the same sender. The FIRST bubble of
// a run carries the sender's name and identicon; continuations carry neither.
//
// `prev` is the row immediately above `cur` in Conversation::rows, or nullptr
// for the first row. A day separator, a system row and a direction change all
// end a run - which is exactly what DemoWorld does when it clears its
// previousSender, so ShowsSenderHeader() below is checkable against
// MessageRow::senderName on real data instead of only against itself.
inline bool StartsRun(demo::MessageRow const* prev, demo::MessageRow const& cur) {
  if (cur.kind != demo::RowKind::Message) return false;
  if (prev == nullptr) return true;
  if (prev->kind != demo::RowKind::Message) return true;  // a separator/system row breaks it
  if (prev->outgoing) return true;   // DemoWorld clears previousSender on an outgoing row
  if (cur.outgoing) return true;
  return prev->senderKey != cur.senderKey;
}

// Does this bubble draw a sender name AND an identicon? Groups only -
// never in a DM (there is exactly one other person and the pane header
// already names them), never on an outgoing row.
inline bool ShowsSenderHeader(demo::MessageRow const* prev, demo::MessageRow const& cur,
                              bool group) {
  return group && !cur.outgoing && StartsRun(prev, cur);
}

// ---- the delivery glyph --------------------------------------------------
// Design S6.2: right-aligned under the LAST outgoing bubble of a run - one
// reading per run, not a column of ticks.
//
// Plus one addition, stated rather than smuggled: a Failed row ALWAYS carries
// it, wherever it sits. "Last of run" alone hides a failure the moment
// someone sends again after it, and a silent failure is the one delivery
// state the demo must never lose.
inline bool CarriesDeliveryGlyph(demo::MessageRow const& cur, demo::MessageRow const* next) {
  if (cur.kind != demo::RowKind::Message || !cur.outgoing) return false;
  if (cur.state == demo::DeliveryState::Failed) return true;
  const bool nextIsOutgoingMessage =
      next != nullptr && next->kind == demo::RowKind::Message && next->outgoing;
  return !nextIsOutgoingMessage;
}

// ---- day separators ------------------------------------------------------
// DemoWorld puts the day label in MessageRow::body - F2's AppendRows sets
// `r.body = s.body` for every row and clears it only for a System row, so a
// DaySeparator keeps its label there. systemText is System-only (contract S1).
//
// Returns empty for anything that is not a labelled separator, so a malformed
// row draws NOTHING rather than an empty pill.
inline std::wstring DaySeparatorLabel(demo::MessageRow const& row) {
  if (row.kind != demo::RowKind::DaySeparator) return {};
  return row.body;
}

// Well-formedness of one conversation's separator sequence. Counted, not
// boolean, so the --diagnose line prints what it actually looked at.
struct DaySeparatorAudit {
  int separators = 0;   // rows with kind == DaySeparator
  int unlabelled = 0;   // ...whose DaySeparatorLabel() is empty
  int adjacent = 0;     // ...immediately preceded by another separator
  int trailing = 0;     // ...that are the last row, so they separate nothing
};

inline DaySeparatorAudit AuditDaySeparators(std::vector<demo::MessageRow> const& rows) {
  DaySeparatorAudit a;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].kind != demo::RowKind::DaySeparator) continue;
    ++a.separators;
    if (DaySeparatorLabel(rows[i]).empty()) ++a.unlabelled;
    if (i > 0 && rows[i - 1].kind == demo::RowKind::DaySeparator) ++a.adjacent;
    if (i + 1 == rows.size()) ++a.trailing;
  }
  return a;
}

// ---- delivery vocabulary -------------------------------------------------
// The six states as WORDS. This is the channel that keeps delivery state out of
// colour-alone: it is what a screen reader hears, via BubbleAutomationName
// below.
//
// THERE IS NO GLYPH TABLE HERE ANY MORE. DeliveryGlyph() lived here until T5
// and returned six DISTINCT codepoints. The rendered cluster — BadgeFor() in
// Views/ThreadLayout.h — deliberately gives Sent and Delivered the SAME glyph
// (E930) and tells them apart by COUNT, so a six-distinct-glyph table beside it
// was a second and contradictory answer to "what does this state draw", and its
// --diagnose line asserted a property the render deliberately violates. Deleted
// rather than left orphaned: one table, and it is the one that draws.
//
// "Not sent" for Failed, matching the cluster's own visible word exactly. A
// screen reader and a sighted reader must not be given two different words for
// one state.
inline std::wstring DeliveryWord(demo::DeliveryState s) {
  switch (s) {
    case demo::DeliveryState::Pending:   return L"Sending";
    case demo::DeliveryState::Sent:      return L"Sent";
    case demo::DeliveryState::Delivered: return L"Delivered";
    case demo::DeliveryState::Read:      return L"Read";
    case demo::DeliveryState::Failed:    return L"Not sent";
    case demo::DeliveryState::Expired:   return L"Expired";
  }
  return L"Sent";
}

// What a screen reader hears when it reaches a bubble.
//
// A Button whose Content is a Panel gets NO automatic automation name —
// UrComponents.h records that this project has paid for the lesson twice — so
// without this the whole thread reads as a column of "button".
//
// On a run CONTINUATION senderName is empty by design (the bubble draws no
// name), so the sender comes from inspect.senderDisplayName, which contract §1
// guarantees is always populated. The visual omits it; the announcement
// must not.
//
//   incoming, group : "Bo Nakamura, 14:22. Slide 4 is the one."
//   incoming, DM    : "14:22. Slide 4 is the one."
//   outgoing        : "You, 14:22, Read. On it."
//   outgoing failed : "You, 14:22, Not sent: no route to recipient. On it."
inline std::wstring BubbleAutomationName(demo::MessageRow const& row, bool group) {
  std::wstring name;
  if (row.outgoing) {
    name = L"You, ";
  } else if (group) {
    const std::wstring& who =
        row.senderName.empty() ? row.inspect.senderDisplayName : row.senderName;
    if (!who.empty()) name = who + L", ";
  }
  name += row.timeLabel;
  if (row.outgoing) {
    name += L", " + DeliveryWord(row.state);
    if (row.state == demo::DeliveryState::Failed && !row.failureReason.empty())
      name += L": " + row.failureReason;
  }
  name += L". " + row.body;
  return name;
}

}  // namespace urmsg::views
