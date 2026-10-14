# THROWAWAY, NEVER MERGED (message-windows ci/task58-mutants on the owner's fork).
#
# Hardening plan Task 58's mutants: one per rule of Live/RefusalCopy, each a single exact edit of
# the PR tree. -Apply makes the edit (refusing unless the text it replaces occurs exactly once);
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
  $summary = @($out -split "`n" | Where-Object { $_ -match '^diagnostic lines:' })
  Write-Host "summary: $summary"
  if ($Id -eq 'M0') {
    if ($fails.Count -eq 0) { Write-Host 'M0: the control is green'; exit 0 }
    Write-Host '::error::M0: the unmutated tree has FAIL lines'; exit 1
  }
  $expect = $mutants[$Id].Expect
  $killed = @($fails | Where-Object { $_ -match ('^\s+' + [regex]::Escape($expect) + '\s+:\s+FAIL\b') })
  if ($killed.Count -ge 1) { Write-Host "$Id KILLED by '$expect'"; exit 0 }
  Write-Host "::error::$Id SURVIVED: '$expect' is not FAIL"; exit 1
}
