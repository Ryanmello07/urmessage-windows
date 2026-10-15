# SPDX-License-Identifier: MPL-2.0
#
# THE APP'S COPY SAYS NOTHING FALSE ABOUT AN INVITATION (hardening plan Task 58; the lead's ruling
# R40 A11).
#
# Since invite version 3 an invitation is sealed to the join code it was made for, so only the device
# that holds that join code can open it, and "whoever has it is in the group" is false. The joiner's
# paste label said it until R40. This check fails on any app string that says it again.
#
# WHAT IT READS, AND HOW. Every .cpp, .h and .idl file under app\src, as the compiler sees its strings:
# comments are taken out, and adjacent string literals are joined, so a phrase split across two
# literals, or across a line, is still found. Raw strings, wide and UTF-8 literals, character
# literals and digit separators are read as such. Every .xaml file is read with its comments taken
# out. The phrase is matched without regard to case.
#
# IT PRINTS WHAT IT LEFT OUT: every comment that says the phrase, by file and line, because a comment
# is not a string a person is shown, and a reader should see what that narrowing removed.
#
# -SelfTest reads fixtures first: a literal that says it, the phrase split across two literals and
# across a line, a wide literal, a raw string, a XAML attribute, and the phrase in a line comment, a
# block comment and a XAML comment, beside a character literal holding a quote, a "//" inside a
# string and a number with digit separators. Each must be found, or not found, as it says.
param(
  [switch]$SelfTest,
  [string]$Root = ''
)
$ErrorActionPreference = 'Stop'
$phrase = 'whoever has it is in the group'

if ($Root -eq '') { $Root = Join-Path (Join-Path $PSScriptRoot '..') 'src' }

# ONE PASS OVER A C++ SOURCE: raw strings, string literals, character literals, line comments and
# block comments, whichever comes first. A character literal is never read straight after a digit,
# which is how a digit separator (1'000) stays a number.
$cppToken = [regex]('(?<raw>(?:L|u8|u|U)?R"(?<d>[^ ()\\\t\r\n]{0,16})\((?<rawbody>[\s\S]*?)\)\k<d>")' +
  '|(?<str>(?:L|u8|u|U)?"(?<body>(?:\\.|[^"\\\r\n])*)")' +
  '|(?<chr>(?<![0-9])(?:L|u8|u|U)?''(?:\\.|[^''\\\r\n])+'')' +
  '|(?<line>//[^\r\n]*)' +
  '|(?<block>/\*[\s\S]*?\*/)')

function Get-LineOf([string]$text, [int]$index) {
  return ([regex]::Matches($text.Substring(0, $index), "`n")).Count + 1
}

# Every string a C++ source makes, adjacent literals joined, each with the line it starts on; and
# every comment, with the line it starts on.
function Read-Cpp([string]$text) {
  $strings = New-Object System.Collections.Generic.List[object]
  $comments = New-Object System.Collections.Generic.List[object]
  $current = $null
  $currentLine = 0
  $prevEnd = 0
  $onlyTrivia = $false
  foreach ($m in $cppToken.Matches($text)) {
    $gap = $text.Substring($prevEnd, $m.Index - $prevEnd)
    if ($gap -match '\S') { $onlyTrivia = $false }
    $prevEnd = $m.Index + $m.Length
    if ($m.Groups['line'].Success -or $m.Groups['block'].Success) {
      $comments.Add(@{ Text = $m.Value; Line = (Get-LineOf $text $m.Index) })
      continue
    }
    if ($m.Groups['chr'].Success) { $onlyTrivia = $false; continue }
    $body = if ($m.Groups['raw'].Success) { $m.Groups['rawbody'].Value } else { $m.Groups['body'].Value }
    if ($null -ne $current -and $onlyTrivia) {
      $current += $body
    } else {
      if ($null -ne $current) { $strings.Add(@{ Text = $current; Line = $currentLine }) }
      $current = $body
      $currentLine = Get-LineOf $text $m.Index
    }
    $onlyTrivia = $true
  }
  if ($null -ne $current) { $strings.Add(@{ Text = $current; Line = $currentLine }) }
  return @{ Strings = $strings; Comments = $comments }
}

# A XAML file: its comments, and the rest, read whole.
function Read-Xaml([string]$text) {
  $comments = New-Object System.Collections.Generic.List[object]
  foreach ($m in [regex]::Matches($text, '<!--[\s\S]*?-->')) {
    $comments.Add(@{ Text = $m.Value; Line = (Get-LineOf $text $m.Index) })
  }
  $body = [regex]::Replace($text, '<!--[\s\S]*?-->', ' ')
  $strings = New-Object System.Collections.Generic.List[object]
  $lineNo = 0
  foreach ($line in ($body -split "`n")) {
    $lineNo++
    $strings.Add(@{ Text = $line; Line = $lineNo })
  }
  return @{ Strings = $strings; Comments = $comments }
}

function Test-Says([string]$text) {
  return $text.IndexOf($phrase, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

# What one source says: its strings that say the phrase, and its comments that do.
function Get-Findings([string]$text, [string]$kind) {
  $read = if ($kind -eq 'xaml') { Read-Xaml $text } else { Read-Cpp $text }
  return @{
    Says = @($read.Strings | Where-Object { Test-Says $_.Text })
    Comments = @($read.Comments | Where-Object { Test-Says $_.Text })
    Strings = $read.Strings.Count
  }
}

if ($SelfTest) {
  $cases = @(
    @{ Name = 'a literal that says it'; Kind = 'cpp'; Want = $true
       Text = 'auto s = "The invitation they send back. Whoever has it is in the group, and it works once.";' },
    @{ Name = 'a wide literal that says it'; Kind = 'cpp'; Want = $true
       Text = 'auto s = L"whoever has it is in the group";' },
    @{ Name = 'the phrase across two literals'; Kind = 'cpp'; Want = $true
       Text = 'auto s = L"The invitation. Whoever has it is in " L"the group.";' },
    @{ Name = 'the phrase across a line'; Kind = 'cpp'; Want = $true
       Text = "auto s = L`"The invitation. Whoever has it `"`n    L`"is in the group.`";" },
    @{ Name = 'the phrase across a comment between literals'; Kind = 'cpp'; Want = $true
       Text = "auto s = `"whoever has it is `" /* joined */ `"in the group`";" },
    @{ Name = 'a raw string that says it'; Kind = 'cpp'; Want = $true
       Text = 'auto s = R"x(say "whoever has it is in the group" here)x";' },
    @{ Name = 'a XAML attribute that says it'; Kind = 'xaml'; Want = $true
       Text = '<TextBlock Text="Whoever has it is in the group"/>' },
    @{ Name = 'the phrase in a line comment'; Kind = 'cpp'; Want = $false
       Text = "// whoever has it is in the group`nauto s = `"fine`";" },
    @{ Name = 'the phrase in a block comment'; Kind = 'cpp'; Want = $false
       Text = "/* whoever has it`n   is in the group */ auto s = `"fine`";" },
    @{ Name = 'a comment after a string, beside a quote character and a // in a string'; Kind = 'cpp'; Want = $false
       Text = "char q = '`"'; auto u = `"http://x`"; // whoever has it is in the group" },
    @{ Name = 'two literals that are not adjacent'; Kind = 'cpp'; Want = $false
       Text = 'f("whoever has it is in ", "the group");' },
    @{ Name = 'a digit separator before a string'; Kind = 'cpp'; Want = $false
       Text = "auto n = 1'000; auto s = `"whoever has it`"; // is in the group" },
    @{ Name = 'the phrase in a XAML comment'; Kind = 'xaml'; Want = $false
       Text = '<!-- whoever has it is in the group --><TextBlock Text="fine"/>' }
  )
  $bad = 0
  foreach ($c in $cases) {
    $found = (Get-Findings $c.Text $c.Kind).Says.Count -gt 0
    $ok = $found -eq $c.Want
    if (-not $ok) { $bad++ }
    Write-Host ("SELFTEST {0}: {1} (found {2}, want {3})" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $c.Name, $found, $c.Want)
  }
  if ($bad -gt 0) { Write-Host "SELFTEST FAILED: $bad of $($cases.Count) cases"; exit 1 }
  Write-Host "SELFTEST PASSED: $($cases.Count) of $($cases.Count) cases"
  exit 0
}

$files = @(Get-ChildItem -LiteralPath $Root -Recurse -File |
  Where-Object { @('.cpp', '.h', '.idl', '.xaml') -contains $_.Extension.ToLowerInvariant() } |
  Sort-Object FullName)
if ($files.Count -eq 0) { Write-Host "FAIL: no source under $Root, so nothing was read"; exit 1 }
$hits = 0
$stringsRead = 0
foreach ($f in $files) {
  $text = [IO.File]::ReadAllText($f.FullName)
  $kind = if ($f.Extension -ieq '.xaml') { 'xaml' } else { 'cpp' }
  $findings = Get-Findings $text $kind
  $stringsRead += $findings.Strings
  $rel = $f.FullName.Substring((Resolve-Path $Root).Path.Length).TrimStart('\', '/')
  foreach ($s in $findings.Says) {
    $hits++
    Write-Host ("FAIL {0}:{1}: a string says `"{2}`"" -f $rel, $s.Line, $phrase)
  }
  foreach ($c in $findings.Comments) {
    Write-Host ("left out {0}:{1}: a comment says it, and a comment is not a string anybody is shown" -f $rel, $c.Line)
  }
}
Write-Host ("read {0} files under {1}, {2} strings" -f $files.Count, $Root, $stringsRead)
if ($hits -gt 0) { Write-Host "FAIL: $hits app string(s) say `"$phrase`", which is false since invite version 3"; exit 1 }
Write-Host "PASS: no app string says `"$phrase`""
exit 0
