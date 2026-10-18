// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit NotUsing, as it does LiveWorld.cpp and
// RunMode.cpp. CollectDiagnostics runs LiveIdentityDiagnostics before winrt::init_apartment.
//
// NOT BUILT YET: PLAN Task 16's diagnose cases come first, and each must fail against these stubs
// for its own reason. Every answer below is "nothing", or what the build had before.
#include "Live/LiveIdentity.h"

namespace urmsg::live {

std::filesystem::path SliceStoreFolder(std::filesystem::path const& liveRoot) { return liveRoot; }

bool IsAccountIdShape(std::string_view) { return false; }
bool IsDeviceIdShape(std::string_view) { return false; }

IdentityInfo ParseIdentityJson(std::string_view) {
  IdentityInfo info;
  info.problem = "not built";
  return info;
}

std::vector<std::wstring> SplitOfferedWords(std::string_view) { return {}; }
std::string JoinTypedWords(std::vector<std::wstring> const&) { return {}; }
bool ConfirmAccepted(int32_t, size_t) { return false; }
std::vector<int> NamedPositions(std::vector<int32_t> const&, int32_t) { return {}; }
std::wstring WrongWordsLine(std::vector<int> const&) { return {}; }

void Wipe(std::string& s) { s.clear(); }
void Wipe(std::wstring& s) { s.clear(); }
void Wipe(std::vector<std::wstring>& words) { words.clear(); }

}  // namespace urmsg::live
