// The two session-only switches the Developer surface owns (design §6.6).
//
// They live here rather than in DeveloperView because the code that OBEYS them
// is elsewhere: the autoplay timer reads AmbientActivityPaused(), and every
// animation reads urnw::motion::ShouldAnimate(). Developer -> flag -> consumer;
// the consumer never reaches back into the view.
//
// CONTRACT GAP: contract v2 has no entry for either switch. This header is the
// resolution, in the same shape Demo/AdvancedMode.h (shipped from F6) fixed for
// Advanced Mode — a free-function subscription with no token. The ambient flag
// must exist ONCE in the build: the autoplay surface READS it rather than
// exporting a second one.
//
// Session-only on purpose: nothing here writes a preference. Advanced Mode is
// the one persisted toggle (Demo/AdvancedMode.h) and these are not.
//
// UI THREAD ONLY, for the reason given in Demo/AdvancedMode.h.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace urmsg::demo {

// False at startup. --demo-autoplay decides whether ambient activity is ARMED
// at all; this decides whether an armed loop is currently suspended.
bool AmbientActivityPaused();
void SetAmbientActivityPaused(bool paused);

// Same contract as urmsg::OnAdvancedModeChanged: called with the NEW value,
// synchronously, on the UI thread, in registration order. No unsubscribe.
void OnAmbientPausedChanged(std::function<void(bool)> fn);

// The --diagnose assertions. Restores everything it touches; the half that
// registers a subscriber runs only under urnw::WantsDiagnose(), because this
// API has no unsubscribe and a probe registered on a normal launch would sit
// in the list for the life of the process.
std::vector<std::wstring> DeveloperSwitchDiagnostics();

}  // namespace urmsg::demo
