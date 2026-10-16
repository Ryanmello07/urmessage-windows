# THROWAWAY, NEVER MERGED (message-windows ci/s3-mutants on the owner's fork).
#
# fix/s3-relay-drawer-trim's mutants. Each undoes ONE decision of the fix in its pure rules
# (Views/StatusStripRules.cpp), one exact edit, and the --diagnose line that holds that decision
# must read FAIL:
#   relay row fit  S1 the label is cut again, S2 the value is not cut, S3 the label takes the rest
#                  of the row again, S4 the tooltip and help text are not the whole value, S5 the
#                  row's automation name loses the value
#   strip fit      S6 no fit at all (the strip as it was), S7 a Fixed item is dropped instead of
#                  the caption, S8 the Advanced extras drop leftmost first, S9 the tooltip and help
#                  text keep only the first fact
# S0 is the control: no edit, both S3 lines PASS, and no FAIL anywhere in --diagnose.
# -Apply makes the edit, refusing unless the text it replaces occurs exactly once; -Judge reads the
# --diagnose output and passes only when the mutant is KILLED.
param(
  [Parameter(Mandatory = $true)][string]$Id,
  [switch]$Apply,
  [string]$Judge = ''
)
$ErrorActionPreference = 'Stop'
$rules = 'app\src\App\Views\StatusStripRules.cpp'
$mutants = @{
  'S1' = @{ Expect = 'relay row fit'
    Old = '  fit.label = TextCut::None;'
    New = '  fit.label = TextCut::Ellipsis;' }
  'S2' = @{ Expect = 'relay row fit'
    Old = '  fit.value = TextCut::Ellipsis;'
    New = '  fit.value = TextCut::None;' }
  'S3' = @{ Expect = 'relay row fit'
    Old = '  fit.labelSizesToContent = true;'
    New = '  fit.labelSizesToContent = false;' }
  'S4' = @{ Expect = 'relay row fit'
    Old = '  std::wstring value{subLabel};'
    New = '  std::wstring value{subLabel.substr(0, 24)};' }
  'S5' = @{ Expect = 'relay row fit'
    Old = "  name += L`", `";`n  name += subLabel;"
    New = "  name += L`", `";" }
  'S6' = @{ Expect = 'strip fit'
    Old = '  const auto over = [&out, room] { return out.total > room + 0.01; };'
    New = '  const auto over = [&out, room] { return false && out.total > room + 0.01; };' }
  'S7' = @{ Expect = 'strip fit'
    Old = '    if (items[i].part != StripPart::Caption) continue;'
    New = '    if (items[i].part != StripPart::Fixed) continue;' }
  'S8' = @{ Expect = 'strip fit'
    Old = "  for (size_t i = items.size(); i-- > 0 && over();) {`n    if (items[i].part != StripPart::Optional) continue;"
    New = "  for (size_t i = 0; i < items.size() && over(); ++i) {`n    if (items[i].part != StripPart::Optional) continue;" }
  'S9' = @{ Expect = 'strip fit'
    Old = '    if (fact.empty()) continue;'
    New = '    if (fact.empty() || !out.empty()) continue;' }
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
  if ($Id -eq 'S0') { Write-Host 'S0: the control, no edit'; exit 0 }
  $m = $mutants[$Id]
  if ($null -eq $m) { Write-Host "::error::no mutant $Id"; exit 2 }
  Swap $rules $m.Old $m.New
  Write-Host "$Id applied to $rules"
  git --no-pager diff
  exit 0
}

if ($Judge -ne '') {
  $out = Get-Content -LiteralPath $Judge -Raw
  $lines = @($out -split "`n")
  $fails = @($lines | Where-Object { $_ -cmatch ':\s+FAIL\b' })
  Write-Host "FAIL lines: $($fails.Count)"
  $fails | ForEach-Object { Write-Host "  $($_.Substring(0, [Math]::Min(220, $_.Length)))" }
  $ours = @($lines | Where-Object { $_ -match '^\s+(relay row fit|strip fit)\s+:\s+(PASS|FAIL)\b' })
  $ours | ForEach-Object { Write-Host "  S3 line: $($_.Substring(0, [Math]::Min(220, $_.Length)))" }
  if ($ours.Count -ne 2) { Write-Host "::error::$Id`: the output carries $($ours.Count) of the two S3 lines"; exit 1 }
  if ($Id -eq 'S0') {
    $oursPass = @($ours | Where-Object { $_ -cmatch ':\s+PASS\b' })
    if ($fails.Count -eq 0 -and $oursPass.Count -eq 2) { Write-Host "S0: the control is green (no FAIL; both S3 lines PASS)"; exit 0 }
    Write-Host '::error::S0: the unmutated tree has FAIL lines, or an S3 line that does not PASS'; exit 1
  }
  $expect = $mutants[$Id].Expect
  $killed = @($fails | Where-Object { $_ -match ('^\s+' + [regex]::Escape($expect) + '\s+:\s+FAIL\b') })
  if ($killed.Count -ge 1) { Write-Host "$Id KILLED by '$expect'"; exit 0 }
  Write-Host "::error::$Id SURVIVED: '$expect' is not FAIL"; exit 1
}
