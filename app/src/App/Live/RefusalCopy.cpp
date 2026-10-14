// SPDX-License-Identifier: MPL-2.0
//
// See RefusalCopy.h. NO PCH: this file is pure and is asked by --diagnose before the apartment.

#include "Live/RefusalCopy.h"

namespace urmsg::live {

namespace {
bool Has(std::string_view text, std::string_view sentence) {
  return text.find(sentence) != std::string_view::npos;
}
}  // namespace

JoinCopy ClassifyJoinCopy(std::string_view libraryError) {
  if (libraryError.empty()) return JoinCopy::Other;
  // R13: an invitation no join code of this device can be read for. Asked FIRST, so that the row
  // that says "your join code is still good" is out of its reach whatever else the text carries.
  if (Has(libraryError, kLibNoJoinCodeForInvite)) return JoinCopy::NoJoinCodeForInvite;
  // What the join cost, ahead of what it was refused over: a spent join code is the fact a person
  // has to act on.
  if (Has(libraryError, kLibJoinCodeSpent)) return JoinCopy::JoinCodeSpent;
  if (Has(libraryError, kLibGroupPredatesJoinSeal)) return JoinCopy::GroupPredatesJoinSeal;
  if (Has(libraryError, kLibInviteVersion)) return JoinCopy::InviteVersion;
  // THE WINDOW'S SENTENCE, BY ITS PREFIX: the window's one exit puts it first, and the same words
  // anywhere else are not that refusal.
  if (libraryError.starts_with(kLibJoinRefused)) return JoinCopy::JoinRefused;
  return JoinCopy::Other;
}

std::string_view JoinCopySentence(JoinCopy row) {
  switch (row) {
    case JoinCopy::JoinRefused: return kSayJoinRefused;
    case JoinCopy::NoJoinCodeForInvite: return kSayNoJoinCodeForInvite;
    case JoinCopy::JoinCodeSpent: return kSayJoinCodeSpent;
    case JoinCopy::InviteVersion: return kSayInviteVersion;
    case JoinCopy::GroupPredatesJoinSeal: return kSayGroupPredatesJoinSeal;
    case JoinCopy::Other: break;
  }
  return {};
}

std::string_view JoinCopyName(JoinCopy row) {
  switch (row) {
    case JoinCopy::JoinRefused: return "ErrJoinRefused";
    case JoinCopy::NoJoinCodeForInvite: return "ErrNoJoinCodeForInvite";
    case JoinCopy::JoinCodeSpent: return "ErrJoinCodeSpent";
    case JoinCopy::InviteVersion: return "ErrInviteVersion";
    case JoinCopy::GroupPredatesJoinSeal: return "ErrGroupPredatesJoinSeal";
    case JoinCopy::Other: break;
  }
  return "other";
}

AddCopy ClassifyAddCopy(std::string_view libraryError) {
  if (libraryError.empty()) return AddCopy::Other;
  if (Has(libraryError, kLibJoinerAlreadyHasALeaf)) return AddCopy::JoinerAlreadyHasALeaf;
  if (Has(libraryError, kLibJoinerCannotOpenSealedInvite)) return AddCopy::JoinerCannotOpenSealedInvite;
  if (Has(libraryError, kLibKeyPackageInvalid)) return AddCopy::KeyPackageInvalid;
  return AddCopy::Other;
}

std::string_view AddCopySentence(AddCopy row) {
  switch (row) {
    case AddCopy::KeyPackageInvalid: return kSayKeyPackageInvalid;
    case AddCopy::JoinerCannotOpenSealedInvite: return kSayJoinerCannotOpenSealedInvite;
    case AddCopy::JoinerAlreadyHasALeaf: return kSayJoinerAlreadyHasALeaf;
    case AddCopy::Other: break;
  }
  return {};
}

std::string_view AddCopyName(AddCopy row) {
  switch (row) {
    case AddCopy::KeyPackageInvalid: return "ErrKeyPackageInvalid";
    case AddCopy::JoinerCannotOpenSealedInvite: return "ErrJoinerCannotOpenSealedInvite";
    case AddCopy::JoinerAlreadyHasALeaf: return "ErrJoinerAlreadyHasALeaf";
    case AddCopy::Other: break;
  }
  return "other";
}

bool IsNoDeviceWrapKey(std::string_view libraryError) { return Has(libraryError, kLibNoDeviceWrapKey); }

}  // namespace urmsg::live
