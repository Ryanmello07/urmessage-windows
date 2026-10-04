// Ambient activity (design doc 9.2): the slow background loop that makes the
// demo feel alive while the owner is talking over it.
//
// This header is the PURE half — what decides, as opposed to what ticks. It
// has no winrt in it for the same reason DemoWorld.h has none: Startup.cpp's
// `demo autoplay` line asserts these functions from CollectDiagnostics(),
// which runs before winrt::init_apartment() (main.cpp:168 vs :183), and
// App.vcxproj compiles the .cpp beside it with PrecompiledHeader=NotUsing.
// The timer half — the loop the window starts — is DemoAutoplayLoop.h.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>

#include "Demo/DemoWorld.h"

namespace urmsg::demo {

// One step up Spec C 5.3's delivery ladder, and no further than Read. Failed and
// Expired are TERMINAL: ambient activity must never quietly "fix" a failed send,
// and the demo's whole claim to honesty is that nothing changes state without a
// cause.
DeliveryState AdvanceDelivery(DeliveryState state);

// The loop's cadence, deterministic by round index. ~40s between rounds, 3-6s of
// typing, both from a fixed table so a second run of the demo behaves like the
// first.
int64_t IdleMsForRound(int round);
int64_t TypingMsForRound(int round);

// The next ambient incoming message for `conversation`. Fabricated HERE, in the
// Demo directory, so the diff that deletes the demo is still one directory. Its
// clock is DERIVED from the conversation's own last message, never the wall
// clock: the world is seeded and two runs of the same demo must behave the same
// way (the d7 audit's W8-class-4 override).
MessageRow IncomingForRound(Conversation const& conversation, int round);

}  // namespace urmsg::demo
