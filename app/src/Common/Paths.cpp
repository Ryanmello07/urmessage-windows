// SPDX-License-Identifier: MPL-2.0
#include "Paths.h"

// GUARDED, for the reason Common/Strings.cpp gives: Directory.Build.props already defines this
// on the command line, so an unguarded #define is warning C4005 'macro redefinition'.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

// An environment variable's whole value, any length; empty when it is unset or empty.
std::wstring EnvValue(wchar_t const* name) {
  if (name == nullptr || *name == L'\0') return {};
  const DWORD need = ::GetEnvironmentVariableW(name, nullptr, 0);
  if (need == 0) return {};
  std::wstring value(need, L'\0');
  const DWORD got = ::GetEnvironmentVariableW(name, value.data(), need);
  if (got == 0 || got >= need) return {};
  value.resize(got);
  return value;
}

}  // namespace

std::wstring StorageRootOverride(std::wstring* variable) {
  const std::wstring own = EnvValue(ids::kAppRootEnvVar);
  if (!own.empty()) {
    if (variable) *variable = ids::kAppRootEnvVar;
    return own;
  }
  const std::wstring outer = EnvValue(ids::kAppRootNestedEnvVar);
  if (!outer.empty()) {
    if (variable) *variable = ids::kAppRootNestedEnvVar;
    return (std::filesystem::path(outer) / ids::kAppRootNestedFolder).wstring();
  }
  return {};
}

std::filesystem::path StorageRoot() {
  // %URMESSAGE_APP_ROOT% overrides the per-user root (the identity preview's is
  // %URMESSAGE_IDENTITY_PREVIEW_APP_ROOT%: Ids.h). Several agents build and
  // run this repo concurrently from separate worktrees, and every one of them
  // otherwise shares a single %LOCALAPPDATA%\URmessage\app: one prefs file and
  // one log, with two unsynchronised writers. That is a state-corruption risk,
  // not just noisy logs — and it silently makes one agent's run appear in
  // another agent's evidence.
  //
  //   $env:URMESSAGE_APP_ROOT = 'C:\...\wt-p1\.localstate'
  const std::wstring root = StorageRootOverride();
  if (!root.empty()) return EnsureDir(std::filesystem::path(root));

  // FOLDERID_LocalAppData -> C:\Users\<u>\AppData\Local
  return EnsureDir(KnownFolder(FOLDERID_LocalAppData) / ids::kStorageFolderName / L"app");
}

std::filesystem::path LogDir() { return EnsureDir(StorageRoot() / L"logs"); }

std::filesystem::path AppPrefsFile() { return StorageRoot() / L"app_prefs.json"; }

bool CommonBuiltAsIdentityPreview() {
#if defined(URM_IDENTITY_PREVIEW) && URM_IDENTITY_PREVIEW
  return true;
#else
  return false;
#endif
}

}  // namespace urnw
