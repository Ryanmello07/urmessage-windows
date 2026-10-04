// Advanced Mode: ONE owner for a state five surfaces change density on.
//
// Contract v2 section 5. Before this, every surface would have read the
// preference itself, which is five readers of one truth and five chances to
// disagree about it. MainWindow registers one subscriber and calls each
// view's Set*Advanced; no view touches the preference.
//
// The preference key is advanced_mode, and it DOES NOT EXIST in this repo
// yet - git grep returns one hit, a provenance comment in AppPrefs.h about
// the VPN client. The name is kept identical to that client's on purpose.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>

namespace urmsg {

bool AdvancedModeEnabled();

// Persists via urnw::SaveAppPref("advanced_mode", on). The ONLY write.
void SetAdvancedModeEnabled(bool on);

// Reads the stored preference and applies --demo-advanced over the top.
// The override is SESSION-ONLY and is never written: a launch switch is how
// someone asked to SEE a state, not a statement about what they want kept.
void InitAdvancedMode(bool sessionOverride);

// Subscribers are called on the UI thread, synchronously, in registration
// order.
void OnAdvancedModeChanged(std::function<void(bool)> fn);

}  // namespace urmsg
