// The --demo family, parsed in the shape of urnw::WantsDiagnose()
// (Startup.cpp:280): CommandLineToArgvW, and --x / -x / /x / bare x all
// accepted, because a switch that works one way and not another is a bug
// report waiting to be filed.
//
// No winrt here either: ParseDemoOptions is called from CollectDiagnostics,
// which runs before winrt::init_apartment.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace urmsg::demo {

enum class DemoScreen { None, Chats, Thread, Inspect, Network, Settings, Developer };

struct DemoOptions {
  bool enabled;        // --demo, or implied by any other demo switch
  DemoScreen screen;
  bool autoplay;       // --demo-autoplay
  bool advanced;       // --demo-advanced, session-only, never written to prefs
  bool watermark;      // false only when --demo-watermark=off
};

// Inspect is a STATE, not a screen: it opens Chats, selects conversation 0,
// selects DemoWorld's kInspectTargetRowId, and puts the rail in message mode.
DemoOptions ParseDemoOptions();

}  // namespace urmsg::demo
