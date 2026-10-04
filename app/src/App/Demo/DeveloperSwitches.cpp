// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Demo/DeveloperSwitches.h"

#include <format>

#include "Startup.h"
#include "UrMotion.h"

namespace urmsg::demo {
namespace {

bool g_paused = false;

std::vector<std::function<void(bool)>>& Subscribers() {
  static std::vector<std::function<void(bool)>> subscribers;
  return subscribers;
}

// File-static for the same reason Demo/AdvancedMode.h's subscriber list is:
// this API has no unsubscribe, so the probe's state must not either.
int g_probeCalls = 0;
bool g_probeSaw = false;

}  // namespace

bool AmbientActivityPaused() { return g_paused; }

void SetAmbientActivityPaused(bool paused) {
  if (paused == g_paused) return;
  g_paused = paused;
  // Fan out over a copy — same reasoning as urmsg::SetAdvancedModeEnabled: a
  // subscriber that re-enters the setter must not invalidate the iteration.
  const auto snapshot = Subscribers();
  for (auto const& fn : snapshot) fn(paused);
}

void OnAmbientPausedChanged(std::function<void(bool)> fn) {
  if (!fn) return;
  Subscribers().push_back(std::move(fn));
}

std::vector<std::wstring> DeveloperSwitchDiagnostics() {
  std::vector<std::wstring> lines;

  // ---- always on: the motion override AT the choke point ------------------
  // ShouldAnimate() is never called here without an override already engaged,
  // so this never touches WinRT UISettings and is safe before init_apartment
  // (main.cpp calls CollectDiagnostics at :176, the apartment opens at :191).
  // The no-override fall-through to UISettings is checked instead by the
  // normal-launch screenshot: the window still reveals.
  const bool hadOverride = urnw::motion::HasMotionOverride();
  urnw::motion::SetMotionOverride(false);
  const bool offAnimates = urnw::motion::ShouldAnimate();
  urnw::motion::SetMotionOverride(true);
  const bool onAnimates = urnw::motion::ShouldAnimate();
  urnw::motion::SetMotionOverride(std::nullopt);
  const bool cleared = !urnw::motion::HasMotionOverride();

  const bool motionOk = !hadOverride && !offAnimates && onAnimates && cleared;
  lines.push_back(std::format(
      L"  dev.motion       : {} no override at entry: {}; override(0)->ShouldAnimate "
      L"{}; override(1)->{}; cleared->HasMotionOverride {}; expected no 0 1 0",
      motionOk ? L"PASS" : L"FAIL", hadOverride ? L"NO" : L"no", int(offAnimates),
      int(onAnimates), int(!cleared)));

  // ---- --diagnose only: registers a subscriber that cannot be removed -----
  if (!urnw::WantsDiagnose()) {
    lines.push_back(
        L"  dev.ambient      : SKIPPED — registers a subscriber this API cannot "
        L"remove; runs only under --diagnose");
    return lines;
  }

  const bool original = AmbientActivityPaused();
  g_probeCalls = 0;
  g_probeSaw = original;
  OnAmbientPausedChanged([](bool v) { ++g_probeCalls; g_probeSaw = v; });

  SetAmbientActivityPaused(!original);
  const bool followed = (AmbientActivityPaused() == !original) && (g_probeSaw == !original);
  SetAmbientActivityPaused(!original);      // same value: must be a total no-op
  const int afterNoop = g_probeCalls;
  SetAmbientActivityPaused(original);

  const bool ambientOk = (g_probeCalls == 2) && (afterNoop == 1) && followed &&
                         (AmbientActivityPaused() == original);
  lines.push_back(std::format(
      L"  dev.ambient      : {} subscriber saw {} calls (expected 2); the "
      L"equal-value call fired nothing ({} after it, expected 1); the flag "
      L"followed the setter: {}; restored to {}: {}",
      ambientOk ? L"PASS" : L"FAIL", g_probeCalls, afterNoop,
      followed ? L"yes" : L"NO", int(original),
      (AmbientActivityPaused() == original) ? L"yes" : L"NO"));
  return lines;
}

}  // namespace urmsg::demo
