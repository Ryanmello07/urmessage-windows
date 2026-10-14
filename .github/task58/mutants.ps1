# THROWAWAY, NEVER MERGED (message-windows ci/task58-mutants on the owner's fork).
#
# Hardening plan Task 58's mutants: one per rule of Live/RefusalCopy, each a single exact edit of
# the PR tree. ci/task58-mutants-v3 added R25's follow-up (M13 to M17) to the first thirteen;
# ci/task58-mutants-v4 adds R27's (M18 to M18k) and R28's (M19 to M19d). M16, M16b and M16d are
# re-cut to R27's renamed constant (kLibJoinNotReadTail is kLibJoinNotReadAfterId) with the same
# meaning; every other earlier mutant is unchanged. M19c and M19d change the worker, which
# --diagnose cannot see: their Expect is SHOTS, and ci/task58-mshots-v4 judges them. -Apply makes the edit (refusing unless the text it replaces occurs exactly once);
# -Judge reads the --diagnose output and passes only when the mutant is KILLED: the line it must
# turn red is FAIL. M0 is the control: no edit, and no FAIL anywhere.
param(
  [Parameter(Mandatory = $true)][string]$Id,
  [switch]$Apply,
  [string]$Judge = ''
)
$ErrorActionPreference = 'Stop'
$copy = 'app\src\App\Live\RefusalCopy.cpp'
$head = 'app\src\App\Live\RefusalCopy.h'
$held = 'app\src\App\Live\HeldGroupRules.h'
$mesh = 'app\src\App\Live\LiveMesh.cpp'
$mutants = @{
  # THE MANDATORY ONE (R13): ErrNoJoinCodeForInvite asked AFTER the window's prefix, so a text that
  # carries both reaches the row that says the join code is still good.
  'M1' = @{ File = $copy; Expect = 'join copy R13'
    Old = '  if (Has(libraryError, kLibNoJoinCodeForInvite)) return JoinCopy::NoJoinCodeForInvite;' + "`n"
    New = '' ; Then = @{ Old = '  if (libraryError.starts_with(kLibJoinRefused)) return JoinCopy::JoinRefused;' + "`n"
                         New = '  if (libraryError.starts_with(kLibJoinRefused)) return JoinCopy::JoinRefused;' + "`n" +
                               '  if (Has(libraryError, kLibNoJoinCodeForInvite)) return JoinCopy::NoJoinCodeForInvite;' + "`n" } }
  # R13 again, the other way: the right row, and the window's sentence shown for it.
  'M1b' = @{ File = $copy; Expect = 'join copy R13'
    Old = '    case JoinCopy::NoJoinCodeForInvite: return kSayNoJoinCodeForInvite;'
    New = '    case JoinCopy::NoJoinCodeForInvite: return kSayJoinRefused;' }
  # the window's sentence anywhere in the text, not as its first words
  'M2' = @{ File = $copy; Expect = 'join copy'
    Old = '  if (libraryError.starts_with(kLibJoinRefused)) return JoinCopy::JoinRefused;'
    New = '  if (Has(libraryError, kLibJoinRefused)) return JoinCopy::JoinRefused;' }
  # a spent join code not told apart
  'M3' = @{ File = $copy; Expect = 'join copy'
    Old = '  if (Has(libraryError, kLibJoinCodeSpent)) return JoinCopy::JoinCodeSpent;' + "`n"; New = '' }
  # the group that predates the seal asked after the window's prefix
  'M4' = @{ File = $copy; Expect = 'join copy'
    Old = '  if (Has(libraryError, kLibGroupPredatesJoinSeal)) return JoinCopy::GroupPredatesJoinSeal;' + "`n"
    New = '' ; Then = @{ Old = '  if (libraryError.starts_with(kLibJoinRefused)) return JoinCopy::JoinRefused;' + "`n"
                         New = '  if (libraryError.starts_with(kLibJoinRefused)) return JoinCopy::JoinRefused;' + "`n" +
                               '  if (Has(libraryError, kLibGroupPredatesJoinSeal)) return JoinCopy::GroupPredatesJoinSeal;' + "`n" } }
  # an older URmessage's invitation not told apart
  'M5' = @{ File = $copy; Expect = 'join copy'
    Old = '  if (Has(libraryError, kLibInviteVersion)) return JoinCopy::InviteVersion;' + "`n"; New = '' }
  # the identity whose entry stands not told apart
  'M6' = @{ File = $copy; Expect = 'add copy'
    Old = '  if (Has(libraryError, kLibJoinerAlreadyHasALeaf)) return AddCopy::JoinerAlreadyHasALeaf;' + "`n"; New = '' }
  # a join code from an older build told it is invalid
  'M7' = @{ File = $copy; Expect = 'add copy'
    Old = '  if (Has(libraryError, kLibJoinerCannotOpenSealedInvite)) return AddCopy::JoinerCannotOpenSealedInvite;'
    New = '  if (Has(libraryError, kLibJoinerCannotOpenSealedInvite)) return AddCopy::KeyPackageInvalid;' }
  # an invalid join code not told apart
  'M8' = @{ File = $copy; Expect = 'add copy'
    Old = '  if (Has(libraryError, kLibKeyPackageInvalid)) return AddCopy::KeyPackageInvalid;' + "`n"; New = '' }
  # the device with no seed not told apart
  'M9' = @{ File = $copy; Expect = 'no wrap key'
    Old = 'bool IsNoDeviceWrapKey(std::string_view libraryError) { return Has(libraryError, kLibNoDeviceWrapKey); }'
    New = 'bool IsNoDeviceWrapKey(std::string_view libraryError) { return Has(libraryError, kLibKeyPackageInvalid); }' }
  # R20 undone: the held state's intro says "not in a group yet" again
  'M10' = @{ File = $head; Expect = 'dialog copy'
    Old = 'inline constexpr std::wstring_view kHeldIntro = L"This device is connected.";'
    New = 'inline constexpr std::wstring_view kHeldIntro = L"This device is connected but is not in a group yet.";' }
  # the adder's copy reworded
  'M11' = @{ File = $head; Expect = 'dialog copy'
    Old = '    L"Only their device can open it. It works once. Send it privately.";'
    New = '    L"Only their device can open it. Send it privately.";' }
  # a row of the table reworded
  'M12' = @{ File = $head; Expect = 'refusal copy'
    Old = '    "This join did not complete. Make a new join code and ask to be added again.";'
    New = '    "This join did not complete. Ask to be added again.";' }
  # ---- R25's follow-up (ci/task58-mutants-v3): one or more per item ----------------------------
  # Q1: the F5 sentence back to the table's first wording, which pointed at a mark T1 does not ship
  'M13' = @{ File = $head; Expect = 'refusal copy'
    Old = '    "This join code claims to be someone already in this group. Remove their entry only if they "' + "`n" +
          '    "tell you they never opened their invitation, then add this join code.";'
    New = '    "They already have an entry in this group, perhaps from an invitation that was never opened. "' + "`n" +
          '    "Remove that entry, then add their new join code.";' }
  # Q2: the spent row offers no new join code
  'M14' = @{ File = $copy; Expect = 'new join code'
    Old = '  return row == JoinCopy::NoJoinCodeForInvite || row == JoinCopy::JoinCodeSpent;'
    New = '  return row == JoinCopy::NoJoinCodeForInvite;' }
  # Q2: every recognised row offers it, the window's "still good" row included
  'M14b' = @{ File = $copy; Expect = 'new join code'
    Old = '  return row == JoinCopy::NoJoinCodeForInvite || row == JoinCopy::JoinCodeSpent;'
    New = '  return row != JoinCopy::Other;' }
  # Q4: the Waiting text says "not in a group yet" under the predates and not-read rows again
  'M15' = @{ File = $copy; Expect = 'waiting copy'
    Old = '  if (!holdsGroup) {'
    New = '  if (true) {' }
  # Q4: the intro of a device whose group did not open is the join road's again
  'M15b' = @{ File = $copy; Expect = 'waiting copy'
    Old = '  return offerLeaveHeldGroup || founded || holdsGroup ? kHeldIntro : kJoinIntro;'
    New = '  return offerLeaveHeldGroup || founded ? kHeldIntro : kJoinIntro;' }
  # Q5: no not-read row on the restore and join roads (each falls to the fallback, or the window's row)
  'M16' = @{ File = $copy; Expect = 'not read now'
    Old = '  if (Has(libraryError, kLibGroupRecordNotReadNow) ||' + "`n" +
          '      HasAroundGroupId(libraryError, kLibJoinNotReadHead, kLibJoinNotReadAfterId))' + "`n" +
          '    return JoinCopy::GroupRecordNotReadNow;' + "`n"
    New = '' }
  # Q5: the leave's rule asks for the join gate's clause, so ForgetGroup's refusal is not caught
  'M16b' = @{ File = $copy; Expect = 'not read now'
    Old = '  return HasAroundGroupId(libraryError, kLibLeaveNotReadHead, kLibLeaveNotReadAfterId);'
    New = '  return HasAroundGroupId(libraryError, kLibJoinNotReadHead, kLibJoinNotReadAfterId);' }
  # Q5: a restore whose record could not be read opens the dialog with no sentence
  'M16c' = @{ File = $copy; Expect = 'restore note'
    Old = '  if (row == JoinCopy::GroupPredatesJoinSeal || row == JoinCopy::GroupRecordNotReadNow)'
    New = '  if (row == JoinCopy::GroupPredatesJoinSeal)' }
  # Q5, THE REJECTED DESIGN: "could not be read" anywhere is the not-read row, so F4's join code the
  # store failed to read (whose sentence says the code is still good) and the leave's clause are too
  'M16d' = @{ File = $copy; Expect = 'not read now'
    Old = '      HasAroundGroupId(libraryError, kLibJoinNotReadHead, kLibJoinNotReadAfterId))'
    New = '      Has(libraryError, "could not be read"))' }
  # Q5: its row reworded
  'M16e' = @{ File = $head; Expect = 'refusal copy'
    Old = '    "This conversation could not be read just now. Close anything else that may be using "'
    New = '    "This conversation could not be read. Close anything else that may be using "' }
  # F4, THE REJECTED DESIGN: the engine's own words for a refused key package taken for
  # ErrNoJoinCodeForInvite, so a join code the store failed to read is told to make a new one
  'M17' = @{ File = $copy; Expect = 'join copy'
    Old = '  if (Has(libraryError, kLibNoJoinCodeForInvite)) return JoinCopy::NoJoinCodeForInvite;'
    New = '  if (Has(libraryError, "no key package this store holds is addressed by this welcome")) return JoinCopy::NoJoinCodeForInvite;' + "`n" +
          '  if (Has(libraryError, kLibNoJoinCodeForInvite)) return JoinCopy::NoJoinCodeForInvite;' }
  # ---- R27 (ci/task58-mutants-v4): the full-sentence match back, one constant at a time. Each
  # is killed by its reworded-tail case in 'stable parts' (Task 39's sentence by 'held refusal').
  'M18' = @{ File = $head; Expect = 'stable parts'
    Old = '    "on this device''s disk could not be read just now";'
    New = '    "urmessage: a file of a group on this device''s disk could not be read just now, so that group was not restored this time; nothing of it was changed, and the next Restore tries it again";' }
  'M18b' = @{ File = $head; Expect = 'stable parts'
    Old = '    "urmessage: this invite was refused before anything was written";'
    New = '    "urmessage: this invite was refused before anything was written, and the join code is not spent";' }
  'M18c' = @{ File = $head; Expect = 'stable parts'
    Old = '    "urmessage: this device holds no join code this invitation was made for";'
    New = '    "urmessage: this device holds no join code this invitation was made for; make a new join code";' }
  'M18d' = @{ File = $head; Expect = 'stable parts'
    Old = '    "urmessage: this join did not complete and its join code is spent";'
    New = '    "urmessage: this join did not complete and its join code is spent; make a new join code and ask to be added again";' }
  'M18e' = @{ File = $head; Expect = 'stable parts'
    Old = '    "urmessage: this group was founded before invitations were sealed";'
    New = '    "urmessage: this group was founded before invitations were sealed, so this build does not open it; leave it and start the group again";' }
  'M18f' = @{ File = $head; Expect = 'stable parts'
    Old = '    "urmessage: this join code was made by a build that cannot open a sealed invite";'
    New = '    "urmessage: this join code was made by a build that cannot open a sealed invite; ask for a join code from an updated URmessage";' }
  'M18g' = @{ File = $head; Expect = 'stable parts'
    Old = '    "urmessage: this identity already has an entry in this group";'
    New = '    "urmessage: this identity already has an entry in this group, perhaps from an invitation that was never opened; remove that entry, then add the new join code";' }
  'M18h' = @{ File = $head; Expect = 'stable parts'
    Old = '    "urmessage: this device holds no x-wing seed for the leaf it publishes";'
    New = '    "urmessage: this device holds no x-wing seed for the leaf it publishes, so it cannot open an encapsulation addressed to that leaf";' }
  'M18i' = @{ File = $head; Expect = 'stable parts'
    Old = 'inline constexpr std::string_view kLibJoinNotReadAfterId = " could not be read";'
    New = 'inline constexpr std::string_view kLibJoinNotReadAfterId = " could not be read, so the invite into it is refused";' }
  'M18j' = @{ File = $head; Expect = 'stable parts'
    Old = 'inline constexpr std::string_view kLibLeaveNotReadAfterId = " on its disk could not be read";'
    New = 'inline constexpr std::string_view kLibLeaveNotReadAfterId = " on its disk could not be read, so nothing was left";' }
  'M18k' = @{ File = $held; Expect = 'held refusal'
    Old = 'inline constexpr std::string_view kHeldRefusalSentence = "urmessage: this device already holds this group";'
    New = 'inline constexpr std::string_view kHeldRefusalSentence = "urmessage: this device already holds this group, so an invite into it is refused";' }
  # ---- R28: a transient restore-first failure decides nothing ----------------------------------
  # forget_group after it: the rule says Forget for the sentinel
  'M19' = @{ File = $copy; Expect = 'held leave'
    Old = '  if (IsGroupRecordNotReadNow(restoreError)) return HeldLeaveStep::NotReadNow;' + "`n"
    New = '' }
  # the held sentence and the leave after it
  'M19b' = @{ File = $copy; Expect = 'held leave'
    Old = '  if (lastRestoreNotReadNow) return HeldCopy{kSayGroupRecordNotReadNow, false};'
    New = '  if (lastRestoreNotReadNow) return HeldCopy{kHeldNotLiveSentence, true};' }
  # THE REJECTED DESIGN, IN THE WORKER: the leave decides off "did the group come back" alone, as it
  # did before R28, so forget_group is called after a transient restore-first failure. --diagnose
  # cannot see the worker; the epochheld screenshot leg must (ci/task58-mshots-v4).
  'M19c' = @{ File = $mesh; Expect = 'SHOTS'
    Old = '  switch (urmsg::live::StepAfterRestoreFirst(restore.back, restore.error)) {'
    New = '  switch (restore.back ? urmsg::live::HeldLeaveStep::CameBack : urmsg::live::HeldLeaveStep::Forget) {' }
  # the startup restore never sets the flag, so a held refusal after it offers the leave again (the
  # notreadnow screenshot leg must see it)
  'M19d' = @{ File = $mesh; Expect = 'SHOTS'
    Old = '    g_lastRestoreNotReadNow = urmsg::live::IsGroupRecordNotReadNow(restoreError);' + "`n" + '  }' + "`n" + "`n" + '  // ONE PASS PER GROUP'
    New = '    g_lastRestoreNotReadNow = false;' + "`n" + '  }' + "`n" + "`n" + '  // ONE PASS PER GROUP' }
}

function Swap([string]$file, [string]$old, [string]$new) {
  $text = [IO.File]::ReadAllText($file)
  $n = 0; $at = 0
  while (($at = $text.IndexOf($old, $at, [StringComparison]::Ordinal)) -ge 0) { $n++; $at += [Math]::Max(1, $old.Length) }
  if ($n -ne 1) { Write-Host "::error::$Id`: the text to replace occurs $n times in $file"; exit 2 }
  $text = $text.Replace($old, $new)
  [IO.File]::WriteAllText($file, $text, (New-Object Text.UTF8Encoding($false)))
}

if ($Apply) {
  if ($Id -eq 'M0') { Write-Host 'M0: the control, no edit'; exit 0 }
  $m = $mutants[$Id]
  if ($null -eq $m) { Write-Host "::error::no mutant $Id"; exit 2 }
  Swap $m.File $m.Old $m.New
  if ($m.ContainsKey('Then')) { Swap $m.File $m.Then.Old $m.Then.New }
  Write-Host "$Id applied to $($m.File)"
  git --no-pager diff
  exit 0
}

if ($Judge -ne '') {
  $out = Get-Content -LiteralPath $Judge -Raw
  $fails = @($out -split "`n" | Where-Object { $_ -cmatch ':\s+FAIL\b' })
  Write-Host "FAIL lines: $($fails.Count)"
  $fails | ForEach-Object { Write-Host "  $($_.Substring(0, [Math]::Min(200, $_.Length)))" }
  # THE CONTROL'S OWN CONTROL: an empty or cut-short output has no FAIL line either, so the PASS
  # lines are counted, and the thirteen Task 58 lines (six, R25's four, and R27's and R28's three)
  # must each be there.
  $passes = @($out -split "`n" | Where-Object { $_ -cmatch ':\s+PASS\b' })
  $ours = @($out -split "`n" | Where-Object { $_ -match '^\s+(join copy|join copy R13|add copy|no wrap key|refusal copy|dialog copy|new join code|not read now|restore note|waiting copy|stable parts|e0d16ea roads|held leave)\s+:\s+(PASS|FAIL)\b' })
  Write-Host "PASS lines: $($passes.Count); Task 58 lines present: $($ours.Count) of 13"
  if ($ours.Count -ne 13) { Write-Host "::error::$Id`: the output carries $($ours.Count) of the thirteen Task 58 lines"; exit 1 }
  if ($Id -eq 'M0') {
    # (the first cut asked for 100 PASS lines, a number guessed before it was measured; the measured
    # control printed 92 by this count, so the rule is the property: no FAIL, and the six Task 58
    # lines each PASS)
    $oursPass = @($ours | Where-Object { $_ -cmatch ':\s+PASS' })
    if ($fails.Count -eq 0 -and $oursPass.Count -eq 13) { Write-Host "M0: the control is green ($($passes.Count) PASS lines, the thirteen Task 58 lines PASS)"; exit 0 }
    Write-Host '::error::M0: the unmutated tree has FAIL lines, or a Task 58 line that does not PASS'; exit 1
  }
  $expect = $mutants[$Id].Expect
  $killed = @($fails | Where-Object { $_ -match ('^\s+' + [regex]::Escape($expect) + '\s+:\s+FAIL\b') })
  if ($killed.Count -ge 1) { Write-Host "$Id KILLED by '$expect'"; exit 0 }
  Write-Host "::error::$Id SURVIVED: '$expect' is not FAIL"; exit 1
}
