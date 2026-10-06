// SPDX-License-Identifier: MPL-2.0
//
// WHAT THIS EXE IMPORTS FROM URnetworkSdk.dll, READ OUT OF ITS OWN DELAY-IMPORT TABLE.
//
// The dll is delay-loaded (App.vcxproj), so each function in it is bound at its FIRST CALL, and a
// function the dll does not export fails right there: the delay-load helper raises a structured
// exception on the live worker thread, in the middle of a session, naming nothing. LoadLibraryW
// succeeding proves only that A dll of that name sits beside the exe. The import library is made
// from the vendored .def and the dll is staged by hand, so a dll from another sdk commit loads
// fine and fails later. So the worker asks GetProcAddress for every import right after
// LoadLibraryW and starts no session while any is missing (Live/LiveMesh.cpp, RunSession).
//
// THE LIST IS THE LINKER'S, NEVER A COPY. It is read from this exe's own delay-import name
// table, so a call added anywhere in the app is probed with no list to update. CI holds the same
// table, as dumpbin reads it, against the vendored .def (tools/verify-sdk-imports.ps1), and
// --diagnose prints what THIS reader finds (Startup.cpp) so that CI can check the two readings
// agree. Nothing here loads the dll.
//
// Pure Win32 and the standard library: no winrt/, no XAML and no pch (App.vcxproj), because the
// live worker calls it.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace urmsg::live {

// The dll's file name, spelled as App.vcxproj's DelayLoadDLLs spells it.
inline constexpr char kSdkDllName[] = "URnetworkSdk.dll";

struct DelayImports {
  // The image's headers were understood. When false, `problem` says why, and nothing else here
  // means anything: a table that could not be read is never reported as an empty one.
  bool readable = false;
  // The delay-import table has an entry for the dll that was asked about.
  bool found = false;
  // What that entry imports, in table order: by name, and by ordinal (the vendored .def declares
  // no ordinals, so --diagnose fails on any).
  std::vector<std::string> names;
  std::vector<uint16_t> ordinals;
  std::string problem;
};

// The functions `image` (a module mapped in this process) takes from `dll` through its
// delay-import table. The dll name compares case-insensitively, as the loader compares it.
DelayImports ReadDelayImports(HMODULE image, const char* dll);

// This exe's own delay imports from URnetworkSdk.dll.
DelayImports ReadOwnSdkImports();

// The imports `module` does not export, asked of GetProcAddress one at a time. Names come back as
// they are; an ordinal comes back as "#<n>".
std::vector<std::string> MissingExports(HMODULE module, DelayImports const& imports);

// Lower-case hex SHA-256 of the names, sorted bytewise and joined with '\n', with no newline at
// the end; empty if the hash could not be computed. tools/verify-sdk-imports.ps1 computes the
// same over dumpbin's reading of the exe.
std::string SortedNamesSha256(std::vector<std::string> names);

}  // namespace urmsg::live
