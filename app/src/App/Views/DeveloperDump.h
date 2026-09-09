// The seeded demo world as TEXT (design §6.6's fourth Developer item): every
// field contract §1 declares, with the two 32-byte Seed values as an 8-byte
// hex prefix so the lines stay readable, and the relay glyphs as their
// codepoints rather than as characters. When a row on Chats looks wrong, the
// dump says whether the ROW is wrong or the WORLD is.
//
// PURE C++ AND IT HAS TO BE, for the reason Demo/DemoWorld.h states:
// WorldDumpDiagnostics() runs from CollectDiagnostics(), which wWinMain runs
// at main.cpp:176 — BEFORE winrt::init_apartment() at main.cpp:191. The
// dump lives in its own module rather than in DeveloperView.cpp (the StatusStrip
// split is the precedent: pure half in StatusStripRules.cpp, XAML half in
// StatusStripView.cpp), and App.vcxproj compiles the .cpp with
// PrecompiledHeader=NotUsing so "pure" is a property of the build, not a
// comment.
//
// G4 — the dump describes FIELDS, never claims. It opens with the same
// framing line the rail's lock header ships (InspectRailView.cpp:516), the
// two claim-carrying tokens are framed in token form
// (key=demo-model-verified, att=demo-model-verified), and the message-row
// cipher field is deliberately omitted: a specific algorithm name is a claim
// about what encrypted the message, whatever label sits above it, and the
// standing no-render ruling for that field (InspectRailFields.h:26-27) does
// not except the dump (the d7 audit's A6 overrides).
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

namespace urmsg::views {

// The whole seeded world, rendered field by field. Deterministic: two calls
// in one process are byte-identical, which WorldDumpDiagnostics asserts.
std::wstring DumpDemoWorld();

// The --diagnose assertions for the dump. Touches no global state and writes
// nothing. What is asserted is a PROPERTY — every conversation and every row
// appears exactly once, two calls are byte-identical, and the framing first
// line is present — not a count that could silently mean "complete".
std::vector<std::wstring> WorldDumpDiagnostics();

}  // namespace urmsg::views
