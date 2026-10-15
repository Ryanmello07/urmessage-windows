// SPDX-License-Identifier: MPL-2.0
//
// See RefusalCopy.h. NO PCH: this file is pure and is asked by --diagnose before the apartment.

#include "Live/RefusalCopy.h"

#include "Live/HeldGroupRules.h"

namespace urmsg::live {

namespace {
bool Has(std::string_view text, std::string_view sentence) {
  return text.find(sentence) != std::string_view::npos;
}

// A CLAUSE WITH A GROUP ID INSIDE IT: `head`, then exactly 64 lower-case hex digits (the library's
// %x of a 32 octet id), then `afterId`, anywhere in the text. The id itself is not asked for: on
// the restore road there is none to ask with, and on the join and leave roads the clause is the
// library's answer about the id it was handed.
bool HasAroundGroupId(std::string_view text, std::string_view head, std::string_view afterId) {
  constexpr size_t kHexDigits = 64;
  for (size_t at = text.find(head); at != std::string_view::npos; at = text.find(head, at + 1)) {
    const size_t id = at + head.size();
    if (text.size() < id + kHexDigits + afterId.size()) return false;
    bool hex = true;
    for (size_t i = id; i < id + kHexDigits && hex; ++i) {
      const char c = text[i];
      hex = ('0' <= c && c <= '9') || ('a' <= c && c <= 'f');
    }
    if (hex && text.substr(id + kHexDigits, afterId.size()) == afterId) return true;
  }
  return false;
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
  // ONE THIS BUILD CANNOT CLASSIFY (R29), on the restore road and inside the join gate's clause alike,
  // and before the transient row: a report that carries both names the longer-lasting.
  if (Has(libraryError, kLibGroupReadUnproven)) return JoinCopy::GroupReadUnproven;
  // A RECORD THAT COULD NOT BE READ JUST NOW (R25 Q5): nothing was changed, and nothing about the
  // group is known, so no row below that says what the group or the invitation IS may answer it.
  if (Has(libraryError, kLibGroupRecordNotReadNow) ||
      HasAroundGroupId(libraryError, kLibJoinNotReadHead, kLibJoinNotReadAfterId))
    return JoinCopy::GroupRecordNotReadNow;
  if (Has(libraryError, kLibGroupPredatesJoinSeal)) return JoinCopy::GroupPredatesJoinSeal;
  if (Has(libraryError, kLibInviteVersion)) return JoinCopy::InviteVersion;
  // A LEAVE MARK THAT NAMES ANOTHER GROUP (FIX 6): asked after every class row above, so a refusal
  // that carries a class's sentinel keeps its class's row
  if (IsLeaveMarkForeign(libraryError)) return JoinCopy::LeaveMarkForeign;
  // THE WINDOW'S REFUSAL OF A JOIN THE STORE WOULD NOT SERVE (R34 A1): asked after every not-read
  // class row above, so a refusal that carries a class's sentinel keeps its class's row.
  if (libraryError.starts_with(kLibJoinRefused) && Has(libraryError, kLibStateStoreState))
    return JoinCopy::JoinRefusedByStore;
  // THE WINDOW'S SENTENCE, BY ITS PREFIX: the window's one exit puts it first, and the same words
  // anywhere else are not that refusal.
  if (libraryError.starts_with(kLibJoinRefused)) return JoinCopy::JoinRefused;
  return JoinCopy::Other;
}

std::string_view JoinCopySentence(JoinCopy row) {
  switch (row) {
    case JoinCopy::JoinRefused: return kSayJoinRefused;
    case JoinCopy::JoinRefusedByStore: return kSayJoinRefusedByStore;
    case JoinCopy::NoJoinCodeForInvite: return kSayNoJoinCodeForInvite;
    case JoinCopy::JoinCodeSpent: return kSayJoinCodeSpent;
    case JoinCopy::InviteVersion: return kSayInviteVersion;
    case JoinCopy::GroupPredatesJoinSeal: return kSayGroupPredatesJoinSeal;
    case JoinCopy::GroupRecordNotReadNow: return kSayGroupRecordNotReadNow;
    case JoinCopy::GroupReadUnproven: return kSayGroupReadUnproven;
    case JoinCopy::LeaveMarkForeign: return kSayLeaveMarkForeign;
    case JoinCopy::Other: break;
  }
  return {};
}

std::string_view JoinCopyName(JoinCopy row) {
  switch (row) {
    case JoinCopy::JoinRefused: return "ErrJoinRefused";
    case JoinCopy::JoinRefusedByStore: return "ErrJoinRefused+ErrStateStoreState";
    case JoinCopy::NoJoinCodeForInvite: return "ErrNoJoinCodeForInvite";
    case JoinCopy::JoinCodeSpent: return "ErrJoinCodeSpent";
    case JoinCopy::InviteVersion: return "ErrInviteVersion";
    case JoinCopy::GroupPredatesJoinSeal: return "ErrGroupPredatesJoinSeal";
    case JoinCopy::GroupRecordNotReadNow: return "ErrGroupRecordNotReadNow";
    case JoinCopy::GroupReadUnproven: return "ErrGroupReadUnproven";
    case JoinCopy::LeaveMarkForeign: return "LeaveMarkNamesAnotherGroup";
    case JoinCopy::Other: break;
  }
  return "other";
}

bool OffersNewJoinCode(JoinCopy row) {
  return row == JoinCopy::NoJoinCodeForInvite || row == JoinCopy::JoinCodeSpent;
}

bool IsLeaveNotReadNow(std::string_view libraryError) {
  return HasAroundGroupId(libraryError, kLibLeaveNotReadHead, kLibLeaveNotReadAfterId);
}

bool IsGroupRecordNotReadNow(std::string_view restoreError) {
  return Has(restoreError, kLibGroupRecordNotReadNow);
}

bool IsGroupReadUnproven(std::string_view restoreError) {
  return Has(restoreError, kLibGroupReadUnproven);
}

bool IsNotConnected(std::string_view libraryError) {
  return Has(libraryError, kLibNotConnected) ||
         HasAroundGroupId(libraryError, kLibLeaveNotTriedHead, kLibLeaveNotTriedAfterId);
}

bool IsLeaveMarkForeign(std::string_view libraryError) {
  return HasAroundGroupId(libraryError, kLibLeaveMarkForeignHead, kLibLeaveMarkForeignAfterName);
}

ReadClass RestoreReadClass(std::string_view restoreError) {
  if (IsGroupReadUnproven(restoreError)) return ReadClass::Unproven;
  if (IsGroupRecordNotReadNow(restoreError)) return ReadClass::NotReadNow;
  return ReadClass::None;
}

ReadClass NextRestoreClass(ReadClass previous, std::string_view restoreError) {
  // a restore that was not tried read nothing, and says nothing about the files
  if (IsNotConnected(restoreError)) return previous;
  return RestoreReadClass(restoreError);
}

HeldLeaveStep StepAfterRestoreFirst(bool cameBack, std::string_view restoreError) {
  if (cameBack) return HeldLeaveStep::CameBack;
  // R32: a restore that could not be tried, because this device is not connected, decided nothing
  if (IsNotConnected(restoreError)) return HeldLeaveStep::NotConnected;
  // R29: nor did one that could not read a file for a reason this build cannot classify
  if (IsGroupReadUnproven(restoreError)) return HeldLeaveStep::Unproven;
  // R28: a restore that could not read a file of a group just now did not decide anything about
  // that group, so nothing is left over it.
  if (IsGroupRecordNotReadNow(restoreError)) return HeldLeaveStep::NotReadNow;
  return HeldLeaveStep::Forget;
}

LeaveRefusal ClassifyLeaveRefusal(std::string_view libraryError) {
  // R33 X2: the leave got as far as the owner's question, so the group's files read
  if (Has(libraryError, kLibOwnerMustTransfer)) return LeaveRefusal::OwnerMustTransfer;
  if (IsNotConnected(libraryError)) return LeaveRefusal::NotConnected;
  if (IsGroupReadUnproven(libraryError)) return LeaveRefusal::Unproven;
  // the transient class's sentinel, or e0d16ea's record clause, which carried no class
  if (IsGroupRecordNotReadNow(libraryError) || IsLeaveNotReadNow(libraryError)) return LeaveRefusal::NotReadNow;
  // FIX 6: a leave mark that names another group, after every class
  if (IsLeaveMarkForeign(libraryError)) return LeaveRefusal::MarkForeign;
  return LeaveRefusal::Other;
}

bool RestoresOnceMore(LeaveRefusal refusal) { return refusal == LeaveRefusal::OwnerMustTransfer; }

HeldCopy HeldNotLiveCopy(ReadClass lastRestore) {
  switch (lastRestore) {
    case ReadClass::NotReadNow: return HeldCopy{kSayGroupRecordNotReadNow, false};
    case ReadClass::Unproven: return HeldCopy{kSayGroupReadUnproven, false};
    case ReadClass::None: break;
  }
  return HeldCopy{kHeldNotLiveSentence, true};
}

HeldCopy LeaveStopCopy(HeldLeaveStep step) {
  switch (step) {
    case HeldLeaveStep::NotReadNow: return HeldNotLiveCopy(ReadClass::NotReadNow);
    case HeldLeaveStep::Unproven: return HeldNotLiveCopy(ReadClass::Unproven);
    case HeldLeaveStep::NotConnected: return HeldCopy{kSayNotConnected, true};
    case HeldLeaveStep::CameBack:
    case HeldLeaveStep::Forget: break;
  }
  return HeldNotLiveCopy(ReadClass::None);
}

HeldCopy LeaveRefusalCopy(LeaveRefusal refusal) {
  switch (refusal) {
    case LeaveRefusal::NotReadNow: return LeaveStopCopy(HeldLeaveStep::NotReadNow);
    case LeaveRefusal::Unproven: return LeaveStopCopy(HeldLeaveStep::Unproven);
    case LeaveRefusal::NotConnected: return LeaveStopCopy(HeldLeaveStep::NotConnected);
    case LeaveRefusal::MarkForeign: return HeldCopy{kSayLeaveMarkForeign, false};
    case LeaveRefusal::OwnerMustTransfer:
    case LeaveRefusal::Other: break;
  }
  return HeldCopy{};
}

std::string_view LeaveRefusalName(LeaveRefusal refusal) {
  switch (refusal) {
    case LeaveRefusal::NotReadNow: return "ErrGroupRecordNotReadNow";
    case LeaveRefusal::Unproven: return "ErrGroupReadUnproven";
    case LeaveRefusal::NotConnected: return "ErrNotConnected";
    case LeaveRefusal::OwnerMustTransfer: return "ErrOwnerMustTransfer";
    case LeaveRefusal::MarkForeign: return "LeaveMarkNamesAnotherGroup";
    case LeaveRefusal::Other: break;
  }
  return "other";
}

std::string_view RestoreNote(std::string_view restoreError, int restoredCount) {
  // A GROUP CAME BACK: this device is in it, and the join dialog is not opened at all.
  if (restoredCount > 0) return {};
  const JoinCopy row = ClassifyJoinCopy(restoreError);
  if (row == JoinCopy::GroupPredatesJoinSeal || row == JoinCopy::GroupRecordNotReadNow)
    return JoinCopySentence(row);
  if (row == JoinCopy::GroupReadUnproven) return JoinCopySentence(row);
  return {};
}

std::string WaitingText(std::string_view note, bool holdsGroup) {
  std::string out(note);
  if (!holdsGroup) {
    if (!out.empty()) out += ' ';
    out += kWaitingNotInAGroup;
  }
  if (!out.empty()) out += ' ';
  out += kWaitingSendJoinCode;
  return out;
}

std::wstring_view OnboardIntro(bool offerLeaveHeldGroup, bool founded, bool holdsGroup) {
  return offerLeaveHeldGroup || founded || holdsGroup ? kHeldIntro : kJoinIntro;
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

bool IsCreationUnrecorded(std::string_view libraryError) { return Has(libraryError, kLibCreationUnrecorded); }

bool IsOpenRecordNotPersisted(std::string_view libraryError) { return Has(libraryError, kLibOpenRecordNotPersisted); }

std::string_view OpenRefusalCopy(std::string_view libraryError) {
  if (IsCreationUnrecorded(libraryError)) return kSayCreationUnfinished;
  if (IsOpenRecordNotPersisted(libraryError)) return kSayOpenRecordNotSaved;
  return {};
}

}  // namespace urmsg::live
