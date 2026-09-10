// The --demo-stress=N generator: N synthetic rows OLDER than the fixture's,
// prepended to conversation 0 so the owner can measure what a long thread
// costs to plan, build, launch and hold in memory. Demo tooling only — the
// default --demo path and the shipping path never call this (Startup.cpp's
// `demo stress` line is the one caller, gated on the parsed switch).
//
// PURE C++, like Demo/DemoWorld.h beside it and for the same reason:
// CollectDiagnostics() calls these from wWinMain BEFORE
// winrt::init_apartment(), so no winrt type may appear here and App.vcxproj
// compiles the .cpp with PrecompiledHeader=NotUsing.
//
// G4: every body the generator writes says what it is — "Stress load NNN"
// sample text, fabricated for the demo. No row claims delivery, encryption or
// verification beyond the same framed fields the fixture itself carries.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::demo {

// The synthetic half of conversation 0's history, OLDEST FIRST, every row
// older than the fixture's first row (the "Yesterday" separator). Reads the
// group's members, groupIdHex and the outgoing senderKey FROM the world
// rather than inventing them — the same lift-the-sender rule the W9 ambient
// seed follows (MainWindow.xaml.cpp), so a stress bubble draws a person who
// already exists in the thread.
//
// Deterministic: a pure function of `w` and `n`. No clock, no RNG — same N
// on the same fixture produces the same bytes on every run, which is what the
// `demo stress` --diagnose line asserts over two independent builds.
std::vector<MessageRow> BuildStressHistory(World const& w, int n);

// Inserts BuildStressHistory ahead of the front conversation's existing rows
// and returns how many rows were added. Returns 0 for n <= 0 or an empty
// world. Idempotent per world: a world whose first row already carries a
// synthetic id was extended by an earlier call in the same process and is
// left alone — CollectDiagnostics is the one caller and runs once, so the
// guard exists for the day that stops being true.
int PrependStressHistory(World& w, int n);

}  // namespace urmsg::demo
