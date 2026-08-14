// Stable identifiers for the URmessage Windows client.
//
// THIS FILE IS THE ONE THAT MUST NOT COLLIDE WITH THE URnetwork VPN CLIENT.
// Both products are unpackaged WinUI 3 desktop apps by the same publisher, both
// will be installed on the same machines, and every identity below is process-
// global or machine-global. The sharp one is kSingleInstanceKey:
// AppInstance::FindOrRegisterForKey keys a single-instance registration by
// STRING, per user session, so if URmessage registered "URnetwork.Desktop" then
// launching URmessage while the VPN client is running would not start URmessage
// at all — it would redirect this activation into the VPN client's window and
// exit. The symptom is "the messenger does nothing when I open it", and the
// only evidence is a line in the OTHER app's log.
//
// Every value here is deliberately different from the VPN app's, and each one
// carries the VPN value it must not be. Keep them constant across releases: the
// tray icon GUID is bound to installed state (exe path + Authenticode signer),
// and changing it orphans the registration.
//
// The namespace is urnw, matching the shared brand/shell layer this app copies
// from the VPN client (urnw = the URnetwork organisation, which owns both
// products). Keeping it identical is what lets UrColors.h, UrComponents.*,
// WindowShell.*, Log.*, Strings.* and friends be byte-for-byte copies, so a
// future diff against the VPN repo shows only what this product actually
// changed.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <guiddef.h>

namespace urnw::ids {

// AppInstance single-instance key. VPN app: L"URnetwork.Desktop" — see the
// header note; this must never be that string.
inline constexpr wchar_t kSingleInstanceKey[] = L"URmessage.Desktop";

// App user model id — toast notifications from an unpackaged app, and
// taskbar/tray grouping. VPN app: L"URnetwork.Desktop".
inline constexpr wchar_t kAppUserModelId[] = L"URmessage.Desktop";

// Tray icon identity for Shell_NotifyIcon with NIF_GUID. Freshly generated for
// this product; the VPN app's is {B7E9C2A1-4F3D-4C8E-9A1B-2D6E8F0A1C34}, and
// two apps sharing one GUID fight over a single registration.
// {BEAEFDA6-7B95-4987-BD95-0052295E4EF7}
inline constexpr GUID kTrayIconGuid = {
    0xbeaefda6, 0x7b95, 0x4987, {0xbd, 0x95, 0x00, 0x52, 0x29, 0x5e, 0x4e, 0xf7}};

// Hidden tray message window class, for when the tray lands. Harnesses find
// windows by class name, so it must not be the VPN app's
// L"URnetworkTrayWindow" or a screenshot harness will capture the wrong
// product and no one will notice.
inline constexpr wchar_t kTrayWindowClass[] = L"URmessageTrayWindow";

// Deep-link / callback scheme. VPN app: L"urnetwork". Registering the same
// scheme twice means the last installer wins and one of the two apps silently
// stops receiving its own callbacks.
inline constexpr wchar_t kUriScheme[] = L"urmessage";

// Per-user storage root leaf (Paths.cpp) and the environment variable that
// overrides it per worktree. Named here rather than buried in Paths.cpp so the
// "does this collide with the VPN app" question has exactly one file to read.
// VPN app: L"URnetwork" / L"URNETWORK_APP_ROOT".
inline constexpr wchar_t kStorageFolderName[] = L"URmessage";
inline constexpr wchar_t kAppRootEnvVar[] = L"URMESSAGE_APP_ROOT";

// HKCU subkey the window-placement blob lives under (WindowShell.cpp).
// VPN app: L"Software\\URnetwork\\Window". Left shared, the two apps would
// restore each other's window geometry.
inline constexpr wchar_t kWindowPlacementKey[] = L"Software\\URmessage\\Window";

// The log file name inside LogDir(). VPN app: urnetwork-app.log.
inline constexpr wchar_t kLogFileName[] = L"urmessage-app.log";

}  // namespace urnw::ids
