# THROWAWAY, NEVER MERGED (message-windows ci/names-mutants on the owner's fork).
#
# fix/live-row-names's --diagnose mutant. N1 undoes F1's pure decision, one exact edit: the names
# a list takes for its rows come from demo::GetWorld() again, not from the world it is given, and
# `list.row.names` must read FAIL. N0 is the control: no edit, `list.row.names` PASS, and no FAIL
# anywhere in --diagnose. F1's view half and F2 have no pure part (the defect was which world a view
# read); their mutants are the names shots' two controls.
# -Apply makes the edit, refusing unless the text it replaces occurs exactly once; -Judge reads the
# --diagnose output and passes only when the mutant is KILLED.
param(
  [Parameter(Mandatory = $true)][string]$Id,
  [switch]$Apply,
  [string]$Judge = ''
)
$ErrorActionPreference = 'Stop'
$model = 'app\src\App\Views\ConversationRowModel.cpp'
$mutants = @{
  'N1' = @{ Expect = 'list.row.names'
    Old = "  for (auto const& c : world.conversations)`n    out.push_back({ConversationRowAutomationName(c, false), ConversationRowAutomationName(c, true)});"
    New = "  for (auto const& c : demo::GetWorld().conversations)`n    out.push_back({ConversationRowAutomationName(c, false), ConversationRowAutomationName(c, true)});" }
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
  if ($Id -eq 'N0') { Write-Host 'N0: the control, no edit'; exit 0 }
  $m = $mutants[$Id]
  if ($null -eq $m) { Write-Host "::error::no mutant $Id"; exit 2 }
  Swap $model $m.Old $m.New
  Write-Host "$Id applied to $model"
  git --no-pager diff
  exit 0
}

if ($Judge -ne '') {
  $out = Get-Content -LiteralPath $Judge -Raw
  $lines = @($out -split "`n")
  $fails = @($lines | Where-Object { $_ -cmatch ':\s+FAIL\b' })
  Write-Host "FAIL lines: $($fails.Count)"
  $fails | ForEach-Object { Write-Host "  $_" }
  $ours = @($lines | Where-Object { $_ -match '^\s+list\.row\.names\s+:\s+(PASS|FAIL)\b' })
  $ours | ForEach-Object { Write-Host "  the line: $_" }
  if ($ours.Count -ne 1) { Write-Host "::error::$Id`: the output carries $($ours.Count) list.row.names line(s), not one"; exit 1 }
  if ($Id -eq 'N0') {
    if ($fails.Count -eq 0 -and $ours[0] -cmatch ':\s+PASS\b') { Write-Host 'N0: the control is green (no FAIL; list.row.names PASS)'; exit 0 }
    Write-Host '::error::N0: the unmutated tree has FAIL lines, or list.row.names does not PASS'; exit 1
  }
  $expect = $mutants[$Id].Expect
  $killed = @($fails | Where-Object { $_ -match ('^\s+' + [regex]::Escape($expect) + '\s+:\s+FAIL\b') })
  $others = @($fails | Where-Object { $_ -notmatch ('^\s+' + [regex]::Escape($expect) + '\s+:\s+FAIL\b') })
  if ($killed.Count -ge 1 -and $others.Count -eq 0) { Write-Host "$Id KILLED by '$expect', and by no other line"; exit 0 }
  Write-Host "::error::$Id not killed for its own reason: '$expect' FAIL lines $($killed.Count), other FAIL lines $($others.Count)"; exit 1
}
