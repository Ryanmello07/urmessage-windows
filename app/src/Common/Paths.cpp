// SPDX-License-Identifier: MPL-2.0
#include "Paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <system_error>

#include "Ids.h"

#pragma comment(lib, "shell32.lib")

namespace urnw {
namespace {

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  std::filesystem::path result;
  if (SUCCEEDED(::SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw))) {
    result = raw;
  }
  if (raw) ::CoTaskMemFree(raw);
  return result;
}

std::filesystem::path EnsureDir(std::filesystem::path p) {
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p;
}

}  // namespace

std::filesystem::path StorageRoot() {
  // %URMESSAGE_APP_ROOT% overrides the per-user root. Several agents build and
  // run this repo concurrently from separate worktrees, and every one of them
  // otherwise shares a single %LOCALAPPDATA%\URmessage\app: one prefs file and
  // one log, with two unsynchronised writers. That is a state-corruption risk,
  // not just noisy logs — and it silently makes one agent's run appear in
  // another agent's evidence.
  //
  //   $env:URMESSAGE_APP_ROOT = 'C:\...\wt-p1\.localstate'
  wchar_t buf[MAX_PATH];
  const DWORD n = ::GetEnvironmentVariableW(ids::kAppRootEnvVar, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return EnsureDir(std::filesystem::path(buf, buf + n));

  // FOLDERID_LocalAppData -> C:\Users\<u>\AppData\Local
  return EnsureDir(KnownFolder(FOLDERID_LocalAppData) / ids::kStorageFolderName / L"app");
}

std::filesystem::path LogDir() { return EnsureDir(StorageRoot() / L"logs"); }

std::filesystem::path AppPrefsFile() { return StorageRoot() / L"app_prefs.json"; }

}  // namespace urnw
