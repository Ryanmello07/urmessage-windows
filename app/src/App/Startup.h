// Startup diagnostics for URmessage — the code that runs before anything else,
// so that a launch which produces no window still produces evidence.
//
// Adapted from the URnetwork VPN client's App/Startup.h. Three things are gone
// with the tunnel: ServicePipeProbe (there is no urnetworkd and no control
// pipe), the URnetworkSdk.dll probe, and PreviewUiDestination (its destination
// tags are the VPN NavigationView's). Everything else is kept verbatim,
// including the reasoning, because it is about the WinUI/App SDK startup path
// and not about VPNs.
//
// "I opened URmessage.exe and nothing happened" has at least three causes that
// look identical from outside: the Windows App Runtime is missing so the App
// SDK bootstrapper kills the process before wWinMain; AppInstance threw; XAML /
// resources.pri failed to load. Every failure here writes to BOTH the log file
// and a message box, and --diagnose prints the same facts to a console for
// pasting.
//
// The strings in this file are deliberately NOT localized: the resource stack
// (resources.pri + MRT) is one of the things that may have failed, and
// Localized() falls back to the raw key id when it has, which would turn a
// diagnostic into a puzzle.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace urnw {

// Open the app log (Common LogInit) and write the first lines. Called as the
// FIRST statement of wWinMain — before init_apartment, before any App SDK call
// — so that the ABSENCE of a "startup: wWinMain" line means the process died
// before reaching its own first instruction. That is the signature of a missing
// Windows App Runtime, because the App SDK's auto-initializer runs from a CRT
// initializer, ahead of every line we own.
void StartupLogInit();

// One line per environment fact the look-alike startup failures are told apart
// by: App Runtime presence + path (the path carries the version), the
// bootstrap dll, resources.pri, the log file. Logged at startup and printed by
// --diagnose. Pure Win32 — safe to call before COM/WinRT is up.
//
// It also carries the app's PURE surface assertions (one PASS/FAIL line each),
// because this repo has no test project and --diagnose is the test runner. The
// bar for adding one here is the bar this function already sets: no COM, no
// WinRT activation, no Localized(), and nothing that fabricates data into the
// log of a normal launch. Anything needing the demo world is gated on the demo
// switch by its own caller instead.
std::vector<std::wstring> CollectDiagnostics();

// Log every line at info, under the "startup:" prefix the rest of the path uses.
void LogDiagnostics(const std::vector<std::wstring>& lines);

// Whether the app's localized resources actually RESOLVE — not merely whether
// resources.pri exists, which is all a file check can tell. Localized() falls
// back to the key id, so a present-but-unindexed pri renders every string in
// the UI as "app_name" / "conversations": a file size cannot see that.
//
// CALL ORDER MATTERS. Localization.cpp caches its ResourceLoader in a
// function-local static on the FIRST call, keeping the failure too — so probing
// before the app is up would move that first call earlier than the UI's, and a
// probe that failed for a reason of its own (no apartment yet, MRT not ready)
// would then make every string in the UI render as its key id for the rest of
// the process. A diagnostic that causes the fault it looks for is worse than no
// diagnostic. So: --diagnose calls this (that process prints and exits, there is
// no UI to poison), and the normal path calls it from OnLaunched.
std::wstring ResourceProbe();

// --diagnose: print the lines to the console this process was launched from (a
// /SUBSYSTEM:WINDOWS process has none of its own, so it attaches to the
// parent's), and to a message box when there is no console at all — a
// double-clicked diagnostic that shows nothing would be its own bug. Returns
// the process exit code.
int WriteDiagnosticsToConsole(const std::vector<std::wstring>& lines);

// True when this process was launched to run diagnostics rather than the app:
// --diagnose, -diagnose, /diagnose or a bare diagnose.
bool WantsDiagnose();

// Set by App::OnLaunched, read by wWinMain after Application::Start returns. A
// message loop that ends without OnLaunched ever having run means XAML gave up
// without throwing — the look-alike nothing else can see.
void MarkLaunched();
bool WasLaunched();

// A startup failure the user can see: logged as an error, then a message box
// naming the cause, the mechanical detail (hresult / GetLastError / path) and
// the log file. Blocks until dismissed; callers exit afterwards.
void FailVisible(std::wstring_view cause, std::wstring_view detail);

// True once FailVisible has shown anything. wWinMain returns non-zero then,
// even when the message loop went on to exit normally: a process that told the
// user it failed must not also tell the shell (and any script wrapping it) that
// it succeeded.
bool HadVisibleFailure();

}  // namespace urnw
