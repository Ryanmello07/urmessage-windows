// SPDX-License-Identifier: MPL-2.0
//
// See Live/SdkImports.h. NO PCH, ON PURPOSE, for the reason LiveMesh.cpp gives: the live worker
// calls this, and it must never acquire a winrt/ or XAML dependency.

#include <windows.h>

#include <bcrypt.h>  // the SHA-256 that --diagnose prints
#pragma comment(lib, "bcrypt.lib")

#include "Live/SdkImports.h"

#include <string.h>  // _stricmp

#include <algorithm>
#include <cstddef>
#include <format>

// The base of the module this code is linked into, which is the exe. The linker defines it.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace urmsg::live {

DelayImports ReadDelayImports(HMODULE image, const char* dll) {
  DelayImports out;
  if (image == nullptr || dll == nullptr) {
    out.problem = "no image to read";
    return out;
  }
  const auto* base = reinterpret_cast<const BYTE*>(image);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    out.problem = "the image has no DOS header";
    return out;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) {
    out.problem = "the image is not a PE image of this process's word size";
    return out;
  }
  // An image with no delay-import directory at all is a readable answer, and `found` stays false.
  if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT) {
    out.readable = true;
    return out;
  }
  const IMAGE_DATA_DIRECTORY& dir =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
  if (dir.VirtualAddress == 0 || dir.Size == 0) {
    out.readable = true;
    return out;
  }
  const auto* descriptors =
      reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>(base + dir.VirtualAddress);
  // The array ends at an all-zero entry. The directory's size bounds it too, so a table without
  // its terminator is not read past its end.
  const size_t capacity = dir.Size / sizeof(IMAGE_DELAYLOAD_DESCRIPTOR);
  for (size_t i = 0; i < capacity && descriptors[i].DllNameRVA != 0; ++i) {
    const IMAGE_DELAYLOAD_DESCRIPTOR& d = descriptors[i];
    // Every linker since Visual C++ 7 writes RVAs here. The older form holds virtual addresses,
    // and reading one as an RVA walks into nothing, so it is refused rather than guessed at.
    if (!d.Attributes.RvaBased) {
      out = DelayImports{};
      out.problem = "a delay-import entry uses the pre-RVA form, which this reader does not read";
      return out;
    }
    if (_stricmp(reinterpret_cast<const char*>(base + d.DllNameRVA), dll) != 0) continue;
    if (d.ImportNameTableRVA == 0) {
      out = DelayImports{};
      out.problem = "the entry for the dll has no import name table";
      return out;
    }
    out.found = true;
    for (const auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + d.ImportNameTableRVA);
         thunk->u1.AddressOfData != 0; ++thunk) {
      if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) {
        out.ordinals.push_back(static_cast<uint16_t>(IMAGE_ORDINAL(thunk->u1.Ordinal)));
      } else {
        const auto* byName =
            reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
        out.names.emplace_back(reinterpret_cast<const char*>(byName->Name));
      }
    }
  }
  out.readable = true;
  return out;
}

DelayImports ReadOwnSdkImports() {
  return ReadDelayImports(reinterpret_cast<HMODULE>(&__ImageBase), kSdkDllName);
}

std::vector<std::string> MissingExports(HMODULE module, DelayImports const& imports) {
  std::vector<std::string> missing;
  for (const std::string& name : imports.names) {
    if (::GetProcAddress(module, name.c_str()) == nullptr) missing.push_back(name);
  }
  for (const uint16_t ordinal : imports.ordinals) {
    if (::GetProcAddress(module, MAKEINTRESOURCEA(ordinal)) == nullptr) {
      missing.push_back(std::format("#{}", ordinal));
    }
  }
  return missing;
}

std::string SortedNamesSha256(std::vector<std::string> names) {
  std::sort(names.begin(), names.end());
  std::string joined;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i != 0) joined.push_back('\n');
    joined += names[i];
  }
  UCHAR digest[32] = {};
  bool ok = false;
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
      ok = BCRYPT_SUCCESS(::BCryptHashData(hash, reinterpret_cast<PUCHAR>(joined.data()),
                                           static_cast<ULONG>(joined.size()), 0)) &&
           BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest, sizeof(digest), 0));
      ::BCryptDestroyHash(hash);
    }
    ::BCryptCloseAlgorithmProvider(alg, 0);
  }
  if (!ok) return {};
  constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (const UCHAR byte : digest) {
    hex.push_back(kHex[byte >> 4]);
    hex.push_back(kHex[byte & 0x0f]);
  }
  return hex;
}

}  // namespace urmsg::live
