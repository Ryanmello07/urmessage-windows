// The --demo family, parsed in the shape of urnw::WantsDiagnose()
// (Startup.cpp:464): CommandLineToArgvW, and --x / -x / /x / bare x all
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

// --demo-composer=<session|observer>: WHAT THE COMPOSER IS TOLD, and nothing else (item 242 R4).
//
// WHY IT EXISTS. The composer has three states - no live session, a live session this device may
// not write to (OBSERVER), and a live session it may - and TWO of the three need a session, which
// a fabricated world does not have and must never claim to have. Without this switch the only way
// to see the observer composer is a real group on the real mesh with a peer willing to demote this
// device, which is not a thing a screenshot, a review or a --diagnose run can arrange.
//
// WHAT IT MAY BE READ AS CLAIMING: NOTHING. It sets the two booleans MainWindow::ArmComposer hands
// to views::SetThreadSendEnabled and it touches no other surface. The composer's own caption still
// says "Demo model - nothing is sent, and no message leaves this window", because that is still
// true: urmsg::live::QueueSend answers false with no worker behind it. So the `session` arm draws a
// LIVE-LOOKING Send button whose click is refused with the text left in the box and a line in the
// log. THAT IS THE ONE STATE THE FABRICATED WORLD CANNOT MAKE TRUE, it is the reason this switch is
// off by default, and no launch a person makes passes it. The `observer` arm has no such cost: its
// button and its box are DARK, which is exactly what that state is.
//
// The `observer` arm fabricates the ROLE as well, because no fabricated conversation makes the
// VIEWER an observer: the fixture's two groups carry the admin's and the owner's control sets the
// roster demo needs (Demo/DemoWorld.cpp), and turning one of them read-only would take a surface
// away to add one. A conversation whose myRole really IS "observer" reaches the same composer
// state through ArmComposer with no switch at all.
enum class DemoComposer {
  AsIs,      // no switch: the session is the worker's answer and the role is the conversation's
  Session,   // --demo-composer=session:  a session is fabricated; the ROLE is still the world's
  Observer,  // --demo-composer=observer: a session is fabricated AND the role is read-only
};

struct DemoOptions {
  bool enabled;           // --demo, or implied by any other demo switch
  DemoScreen screen;
  bool autoplay;          // --demo-autoplay
  bool advanced;          // --demo-advanced, session-only, never written to prefs
  bool watermark;         // false only when --demo-watermark=off
  int stressRows;         // --demo-stress=N: synthetic history rows for conversation 0; 0 = off
  DemoComposer composer;  // --demo-composer=session|observer; see above
};

// Inspect is a STATE, not a screen: it opens Chats, selects conversation 0,
// selects DemoWorld's kInspectTargetRowId, and puts the rail in message mode.
DemoOptions ParseDemoOptions();

}  // namespace urmsg::demo
