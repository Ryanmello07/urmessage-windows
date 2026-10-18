// Per-user storage and log locations for URmessage.
//
//   %LOCALAPPDATA%\URmessage\app
//
// Adapted from the URnetwork VPN client's Common/Paths.h. Two deliberate
// changes, both about not sharing state with that app:
//   * the folder leaf is URmessage, not URnetwork. A shared root would put two
//     unsynchronised writers on one app_prefs.json and one log file.
//   * the per-worktree override is URMESSAGE_APP_ROOT, not URNETWORK_APP_ROOT,
//     so pointing one product's worktree somewhere does not silently move the
//     other's.
// Both names live in Ids.h with the rest of the identity. The service/SYSTEM
// half of the original is gone: URmessage has no service, so there is no
// isService flag on any of these.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <filesystem>

namespace urnw {

// Root storage dir for this user, created if missing. Honours
// %URMESSAGE_APP_ROOT% so concurrent worktrees can be isolated.
std::filesystem::path StorageRoot();

// Log directory (a subdir of StorageRoot).
std::filesystem::path LogDir();

// The app's own preferences: one small JSON document (AppPrefs.h).
std::filesystem::path AppPrefsFile();

// True when COMMON was compiled as the identity preview (URM_IDENTITY_PREVIEW, identity phase 1,
// Task 6). Not inline, on purpose: Ids.h's folder name and override variable are inline constants,
// so a Common compiled with one arm and an App with the other would be an ODR violation the linker
// may settle on either value, and a check reading the header from both sides would compare one
// value with itself. This answer is compiled into Common's own object; --diagnose's build kind
// line holds it to App's.
bool CommonBuiltAsIdentityPreview();

}  // namespace urnw
