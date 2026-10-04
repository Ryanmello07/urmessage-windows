// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Demo/AdvancedMode.h"

#include <vector>

#include "AppPrefs.h"
#include "Log.h"

namespace urmsg {
namespace {

constexpr const char* kPrefKey = "advanced_mode";

bool g_enabled = false;
bool g_sessionOverride = false;
bool g_stored = false;
std::vector<std::function<void(bool)>> g_subscribers;

}  // namespace

bool AdvancedModeEnabled() { return g_enabled; }

void InitAdvancedMode(bool sessionOverride) {
  g_sessionOverride = sessionOverride;
  const nlohmann::json prefs = urnw::LoadAppPrefs();
  // is_boolean() and not just contains(): a hand-edited or half-written
  // prefs file must not decide the UI density by throwing.
  g_stored = prefs.contains(kPrefKey) && prefs[kPrefKey].is_boolean() &&
             prefs[kPrefKey].get<bool>();
  g_enabled = g_stored || sessionOverride;
  // NOTHING IS WRITTEN HERE. That is the whole point of the override.
  urnw::LogInfo("advanced: init -> {} (stored {}, session override {})",
                g_enabled, g_stored, sessionOverride);
}

void SetAdvancedModeEnabled(bool on) {
  if (g_enabled == on) return;   // no change, so no write and no fan-out
  g_enabled = on;
  // A person who toggles this in Settings during a --demo-advanced run means
  // it, so this writes even then: the override is a LAUNCH state, not a lock.
  urnw::SaveAppPref(kPrefKey, on);
  g_stored = on;
  urnw::LogInfo("advanced: set -> {} (persisted; {} subscriber(s))", on,
                g_subscribers.size());
  for (auto const& fn : g_subscribers)
    if (fn) fn(on);
}

void OnAdvancedModeChanged(std::function<void(bool)> fn) {
  if (fn) g_subscribers.push_back(std::move(fn));
}

}  // namespace urmsg
