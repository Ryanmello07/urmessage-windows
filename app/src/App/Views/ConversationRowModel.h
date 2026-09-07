// The conversation row's DECISIONS, separated from its pixels.
//
// main.cpp:168 calls CollectDiagnostics() BEFORE winrt::init_apartment (:180),
// so every invariant --diagnose asserts has to be reachable without a WinRT
// object. This header names no XAML type. That is a CONVENTION enforced by
// review, not by the compiler: the .cpp includes pch.h, which makes the whole
// projection reachable regardless.
//
// It is also the honest split. "The disappearing row shows a timer INSTEAD of
// its preview" is a claim about content, and a claim about content is testable
// without a window.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

struct ConversationRowModel {
  std::wstring name;
  // EMPTY when showTimer is true. Spec C 4.2 / design 6.1: a disappearing
  // conversation shows the timer glyph INSTEAD of a preview. The world's
  // preview is NON-empty on that conversation (contract 1.1 rule 3), so this is
  // a refusal to carry it, not an absence of anything to carry -- which is what
  // makes the invariant falsifiable and stops it leaking back through a filter,
  // an automation name or a tooltip later.
  std::wstring preview;
  bool showTimer = false;
  bool showMuted = false;
  // "" when nothing is unread; "99+" above 99, because three digits do not fit
  // a pill in a 320dip list column.
  std::wstring unread;
  std::wstring timeLabel;
  // Advanced Mode only, and only on a group: at most the first 6 chars of
  // Conversation::groupIdHex. Empty on every DM. Design 6.6's "+ group id chip".
  std::wstring groupIdChip;
};

ConversationRowModel MakeConversationRowModel(demo::Conversation const& c);

// What a screen reader is told. This is selection's THIRD channel: the fill step
// and the 2px accent bar are the other two, and colour alone is never the
// carrier of a state -- kit::SetPaneListRowSelected carries the same three for
// the same reason (UrComponents.h:333-340).
std::wstring ConversationRowAutomationName(demo::Conversation const& c, bool selected);

// The row's entrance delay: kStaggerMs per step, capped at kMaxStaggerSteps.
// int64_t because motion::MakeSplineDouble takes its beginMs as one
// (UrMotion.h:100-101).
int64_t ConversationRowDelayMs(std::size_t index);

// Does the row survive `query`? Case-insensitive substring over the name and,
// for a conversation that is NOT disappearing, its preview. A disappearing
// conversation is searchable BY NAME ONLY: matching on a preview the row refuses
// to draw would put the hidden text back on screen by way of the result set.
bool ConversationRowMatches(demo::Conversation const& c, std::wstring const& query);

// This surface's --diagnose assertions. One PASS/FAIL line per invariant in the
// "  key              : value" shape CollectDiagnostics already uses. Pure C++:
// safe to call before init_apartment.
std::vector<std::wstring> CollectConversationListDiagnostics();

}  // namespace urmsg::views
