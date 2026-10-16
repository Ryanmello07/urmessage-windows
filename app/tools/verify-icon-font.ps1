# SPDX-License-Identifier: MPL-2.0
#
# EVERY ICON NAMES THE ICON FONT WITH ITS FALLBACK, AND --diagnose NAMES EVERY GLYPH (urnetwork/windows#12,
# ported to this app).
#
# Windows 11 ships Segoe Fluent Icons and Windows 10 does not, nor does Windows Server 2022 (the Windows 10
# code base), so an icon drawn from "Segoe Fluent Icons" alone is an empty box there: every icon of this app
# drew that way on a windows-2022 runner. Segoe MDL2 Assets, which both ship, maps every glyph the app draws.
# So every source that names an icon font names exactly the list below, the one WinUI itself gives FontIcon
# by default: App.xaml's UrIconFontFamily and UrComponents.h's kIconFontFamily, which everything else goes
# through. One family alone fails, and so does the list the other way round, which would draw Windows 11
# from the older font.
#
# AND EVERY GLYPH IS LISTED. UrComponents.h's kIconGlyphs is the table --diagnose's `icon glyphs` line asks
# the machine about (which family of the list draws each code point). This check holds that table to the
# sources both ways: a private-use code point (U+E000..U+F8FF) a source draws that is not in the table fails,
# and so does a table entry no source draws.
#
# WHAT IT READS, AND HOW. Every file under app\src. A .cpp, .h or .idl file is read as the compiler sees its
# literals: comments are taken out, and a string or character literal is a NAMING when it mentions an icon
# family; a mention left in the code outside every literal is a naming of its own, so a macro cannot pass as
# the list. A glyph is a private-use character in a literal, written as itself or as a \u, \U or \x escape.
# A .xaml file is read as XML: every attribute value and every run of element text, so a comment is neither
# a naming nor a glyph, and &#xE8BD; is the glyph it decodes to. A .md or .txt file is prose and is listed,
# not read; any other kind of file that mentions an icon family fails, because this check cannot read it.
#
# IT PRINTS WHAT IT LEFT OUT: the mentions in comments and in prose, by file, beside every naming and every
# glyph it counted, so a reader sees what the narrowing removed.
#
# -SelfTest reads fixtures first, each of which must be found, or not found, as it says.
param(
  [switch]$SelfTest,
  [string]$Root = ''
)
$ErrorActionPreference = 'Stop'
$fallback = 'Segoe Fluent Icons,Segoe MDL2 Assets'
$families = @('Segoe Fluent Icons', 'Segoe MDL2 Assets')

if ($Root -eq '') { $Root = Join-Path (Join-Path $PSScriptRoot '..') 'src' }

# ONE PASS OVER A C++ SOURCE (verify-copy.ps1's lexer): raw strings, string literals, character literals,
# line comments and block comments, whichever comes first. A character literal is never read straight
# after a digit, which is how a digit separator (1'000) stays a number.
$cppToken = [regex]('(?<raw>(?:L|u8|u|U)?R"(?<d>[^ ()\\\t\r\n]{0,16})\((?<rawbody>[\s\S]*?)\)\k<d>")' +
  '|(?<str>(?:L|u8|u|U)?"(?<body>(?:\\.|[^"\\\r\n])*)")' +
  '|(?<chr>(?<![0-9])(?:L|u8|u|U)?''(?<cbody>(?:\\.|[^''\\\r\n])+)'')' +
  '|(?<line>//[^\r\n]*)' +
  '|(?<block>/\*[\s\S]*?\*/)')

function Get-LineOf([string]$text, [int]$index) {
  return ([regex]::Matches($text.Substring(0, $index), "`n")).Count + 1
}

function Get-Mentions([string]$text) {
  $n = 0
  foreach ($family in $families) {
    $at = 0
    while (($at = $text.IndexOf($family, $at, [StringComparison]::OrdinalIgnoreCase)) -ge 0) { $n++; $at += $family.Length }
  }
  return $n
}

# Whether a value is the list: its families, trimmed, in that order, case ignored as DirectWrite ignores it.
function Test-Fallback([string]$value) {
  $parts = @($value -split ',' | ForEach-Object { $_.Trim() })
  return [string]::Equals(($parts -join ','), $fallback, [StringComparison]::OrdinalIgnoreCase)
}

# The private-use code points a C++ literal body draws: written as themselves, or as \u, \U or \x escapes.
# A raw string's body has no escapes.
function Get-CppGlyphs([string]$body, [bool]$raw) {
  $out = New-Object System.Collections.Generic.List[int]
  $i = 0
  while ($i -lt $body.Length) {
    $c = [int][char]$body[$i]
    if (-not $raw -and $body[$i] -eq [char]92 -and $i + 1 -lt $body.Length) {
      $k = $body[$i + 1]
      $m = $null
      if ($k -ceq 'u') { $m = [regex]::Match($body.Substring($i), '^\\u([0-9A-Fa-f]{4})') }
      elseif ($k -ceq 'U') { $m = [regex]::Match($body.Substring($i), '^\\U([0-9A-Fa-f]{8})') }
      elseif ($k -ceq 'x') { $m = [regex]::Match($body.Substring($i), '^\\x([0-9A-Fa-f]+)') }
      if ($null -ne $m -and $m.Success) {
        $cp = [Convert]::ToInt32($m.Groups[1].Value, 16)
        if ($cp -ge 0xE000 -and $cp -le 0xF8FF) { $out.Add($cp) }
        $i += $m.Length
        continue
      }
      $i += 2
      continue
    }
    if ($c -ge 0xE000 -and $c -le 0xF8FF) { $out.Add($c) }
    $i++
  }
  return , $out
}

# What one C++ source holds: its namings (literals that mention a family, and mentions outside every
# literal), its glyphs, and how many mentions its comments hold.
function Read-Cpp([string]$file, [string]$text) {
  $text = $text -replace "`r`n", "`n"
  $namings = New-Object System.Collections.Generic.List[object]
  $glyphs = New-Object System.Collections.Generic.List[object]
  $commentMentions = 0
  $rest = $text.ToCharArray()
  foreach ($m in $cppToken.Matches($text)) {
    for ($k = $m.Index; $k -lt $m.Index + $m.Length; $k++) { if ($rest[$k] -ne "`n") { $rest[$k] = ' ' } }
    if ($m.Groups['line'].Success -or $m.Groups['block'].Success) { $commentMentions += Get-Mentions $m.Value; continue }
    $raw = $m.Groups['raw'].Success
    $body = if ($raw) { $m.Groups['rawbody'].Value } elseif ($m.Groups['chr'].Success) { $m.Groups['cbody'].Value } else { $m.Groups['body'].Value }
    $line = Get-LineOf $text $m.Index
    if ((Get-Mentions $body) -gt 0) { $namings.Add(@{ File = $file; Line = $line; Value = $body }) }
    foreach ($cp in (Get-CppGlyphs $body $raw)) { $glyphs.Add(@{ File = $file; Line = $line; CodePoint = $cp }) }
  }
  $restText = -join $rest
  foreach ($family in $families) {
    $at = 0
    while (($at = $restText.IndexOf($family, $at, [StringComparison]::OrdinalIgnoreCase)) -ge 0) {
      $namings.Add(@{ File = $file; Line = (Get-LineOf $restText $at); Value = "outside every string literal: $family" })
      $at += $family.Length
    }
  }
  return @{ Namings = $namings; Glyphs = $glyphs; CommentMentions = $commentMentions }
}

# What one XAML source holds, read as XML: attribute values and element text; a comment is neither.
function Read-Xaml([string]$file, [string]$text) {
  $namings = New-Object System.Collections.Generic.List[object]
  $glyphs = New-Object System.Collections.Generic.List[object]
  $settings = New-Object System.Xml.XmlReaderSettings
  $settings.DtdProcessing = [System.Xml.DtdProcessing]::Prohibit
  $reader = [System.Xml.XmlReader]::Create((New-Object System.IO.StringReader($text)), $settings)
  $commentMentions = 0
  try {
    while ($reader.Read()) {
      $values = @()
      if ($reader.NodeType -eq [System.Xml.XmlNodeType]::Element) {
        if ($reader.MoveToFirstAttribute()) {
          do { $values += , @($reader.Value, ([System.Xml.IXmlLineInfo]$reader).LineNumber) } while ($reader.MoveToNextAttribute())
          [void]$reader.MoveToElement()
        }
      } elseif ($reader.NodeType -eq [System.Xml.XmlNodeType]::Text -or $reader.NodeType -eq [System.Xml.XmlNodeType]::CDATA) {
        $values += , @($reader.Value.Trim(), ([System.Xml.IXmlLineInfo]$reader).LineNumber)
      } elseif ($reader.NodeType -eq [System.Xml.XmlNodeType]::Comment) {
        $commentMentions += Get-Mentions $reader.Value
      }
      foreach ($v in $values) {
        if ((Get-Mentions $v[0]) -gt 0) { $namings.Add(@{ File = $file; Line = $v[1]; Value = $v[0] }) }
        foreach ($ch in $v[0].ToCharArray()) {
          $cp = [int]$ch
          if ($cp -ge 0xE000 -and $cp -le 0xF8FF) { $glyphs.Add(@{ File = $file; Line = $v[1]; CodePoint = $cp }) }
        }
      }
    }
  } finally { $reader.Close() }
  return @{ Namings = $namings; Glyphs = $glyphs; CommentMentions = $commentMentions }
}

# The table --diagnose asks about: UrComponents.h's kIconGlyphs, by its entries' code points.
function Read-Table([string]$text) {
  $m = [regex]::Match($text, 'kIconGlyphs\[\]\s*=\s*\{(?<body>[\s\S]*?)\n\};')
  if (-not $m.Success) { return $null }
  $set = @{}
  foreach ($e in [regex]::Matches($m.Groups['body'].Value, '\{\s*0x(?<cp>[0-9A-Fa-f]{4,5})\s*,')) {
    $set[[Convert]::ToInt32($e.Groups['cp'].Value, 16)] = $true
  }
  return $set
}

# THE CHECK, over a tree of sources (relative path -> text) and the table's text.
function Invoke-Check($files, [string]$tableText) {
  $report = @{ Namings = @(); Violations = @(); LeftOut = @(); Glyphs = @(); Table = $null }
  $table = Read-Table $tableText
  if ($null -eq $table) { $report.Violations += 'UrComponents.h holds no kIconGlyphs table, so no glyph can be checked' ; $table = @{} }
  $report.Table = $table
  foreach ($path in ($files.Keys | Sort-Object)) {
    $text = $files[$path]
    $ext = [IO.Path]::GetExtension($path).ToLowerInvariant()
    $mentions = Get-Mentions $text
    $read = $null
    if (@('.cpp', '.h', '.hpp', '.idl', '.inl') -contains $ext) { $read = Read-Cpp $path $text }
    elseif ($ext -eq '.xaml') {
      try { $read = Read-Xaml $path $text } catch { $report.Violations += "$path does not read as XML: $($_.Exception.Message)"; continue }
    } elseif (@('.md', '.txt') -contains $ext) {
      if ($mentions -gt 0) { $report.LeftOut += "${path}: $mentions mention(s) in prose" }
      continue
    } else {
      if ($mentions -gt 0) { $report.Violations += "$path names an icon family in a kind of file this check cannot read ($ext); name the font from a source it reads" }
      continue
    }
    foreach ($n in $read.Namings) {
      $report.Namings += $n
      if (-not (Test-Fallback $n.Value)) {
        $report.Violations += ("{0}:{1} names an icon font without the fallback: `"{2}`"; name `"{3}`" (urnw::kit::kIconFontFamily in code, {{StaticResource UrIconFontFamily}} in markup)" -f $n.File, $n.Line, $n.Value, $fallback)
      }
    }
    if ($read.CommentMentions -gt 0) { $report.LeftOut += "${path}: $($read.CommentMentions) mention(s) in comments" }
    foreach ($g in $read.Glyphs) { $report.Glyphs += $g }
  }
  # THE TABLE AGAINST THE SOURCES, both ways
  $used = @{}
  foreach ($g in $report.Glyphs) { $used[$g.CodePoint] = $true }
  foreach ($cp in ($used.Keys | Sort-Object)) {
    if (-not $table.ContainsKey($cp)) {
      $where = @($report.Glyphs | Where-Object { $_.CodePoint -eq $cp } | ForEach-Object { "$($_.File):$($_.Line)" })
      $report.Violations += ("U+{0:X4} is drawn ({1}) and is not in kIconGlyphs, so --diagnose does not ask about it" -f $cp, ($where -join ', '))
    }
  }
  foreach ($cp in ($table.Keys | Sort-Object)) {
    if (-not $used.ContainsKey($cp)) { $report.Violations += ("U+{0:X4} is in kIconGlyphs and no source draws it" -f $cp) }
  }
  return $report
}

if ($SelfTest) {
  $tableOk = "inline constexpr IconGlyph kIconGlyphs[] = {`n    {0xE8BD, L`"Chats`"},`n};"
  # EACH CASE FAILS FOR ITS OWN REASON: the violations it must carry, and the ones it must not, by their
  # words. A case that wants none wants no violation at all.
  $cases = @(
    @{ Name = 'the list in markup'; Want = @()
       Files = @{ 'App.xaml' = '<R><FontFamily x:Key="UrIconFontFamily" xmlns:x="x">Segoe Fluent Icons,Segoe MDL2 Assets</FontFamily><FontIcon Glyph="&#xE8BD;"/></R>' } },
    @{ Name = 'Fluent alone in markup'; Want = @('without the fallback: "Segoe Fluent Icons"')
       Files = @{ 'App.xaml' = '<R><FontFamily>Segoe Fluent Icons</FontFamily><FontIcon Glyph="&#xE8BD;"/></R>' } },
    @{ Name = 'Fluent alone in an attribute'; Want = @('a.xaml:1 names an icon font without the fallback')
       Files = @{ 'a.xaml' = '<R><FontIcon FontFamily="Segoe Fluent Icons" Glyph="&#xE8BD;"/></R>' } },
    @{ Name = 'the list the other way round'; Want = @('without the fallback: "Segoe MDL2 Assets,Segoe Fluent Icons"')
       Files = @{ 'a.cpp' = 'auto f = Media::FontFamily(L"Segoe MDL2 Assets,Segoe Fluent Icons"); auto g = L"\uE8BD";' } },
    @{ Name = 'Fluent alone in code'; Want = @('a.cpp:1 names an icon font without the fallback: "Segoe Fluent Icons"')
       Files = @{ 'a.cpp' = 'auto f = Media::FontFamily(L"Segoe Fluent Icons"); auto g = L"\uE8BD";' } },
    @{ Name = 'the list in code, spaces around the comma'; Want = @()
       Files = @{ 'a.h' = 'inline constexpr wchar_t k[] = L"Segoe Fluent Icons , Segoe MDL2 Assets"; auto g = L"\uE8BD";' } },
    @{ Name = 'a family in a comment, a XAML comment and prose'; Want = @()
       Files = @{ 'a.cpp' = "// Segoe Fluent Icons alone was an empty box`n/* Segoe MDL2 Assets */ auto g = L`"\uE8BD`";"
                  'b.xaml' = '<R><!-- Segoe Fluent Icons --></R>'; 'c.md' = 'Segoe Fluent Icons' } },
    @{ Name = 'a family outside every literal'; Want = @('outside every string literal: Segoe Fluent Icons')
       Files = @{ 'a.cpp' = "#define FONT Segoe Fluent Icons`nauto g = L`"\uE8BD`";" } },
    @{ Name = 'a family in a kind of file this cannot read'; Want = @('cannot read (.resw)')
       Files = @{ 'a.resw' = '<data>Segoe Fluent Icons</data>'; 'b.cpp' = 'auto g = L"\uE8BD";' } },
    @{ Name = 'a glyph drawn that is not in the table'; Want = @('U+E711 is drawn (a.cpp:1)')
       Files = @{ 'a.cpp' = 'auto g = L"\uE8BD"; auto h = L"\uE711";' } },
    @{ Name = 'a table entry no source draws'; Want = @('U+E8BD is in kIconGlyphs and no source draws it')
       Files = @{ 'a.cpp' = 'auto g = L"nothing";' } },
    @{ Name = 'a glyph written as itself, and one as \x'; Want = @('U+E711 is drawn'); NotWant = @('U+E8BD is in kIconGlyphs and no source draws it')
       Files = @{ 'a.cpp' = ('auto g = L"' + [char]0xE8BD + '"; auto h = L"\xE711";') } },
    @{ Name = 'a glyph in a comment is not drawn'; Want = @('U+E8BD is in kIconGlyphs and no source draws it')
       Files = @{ 'a.cpp' = '// L"\uE8BD" is drawn elsewhere' } },
    @{ Name = 'a glyph in a character literal, beside a digit separator'; Want = @()
       Files = @{ 'a.cpp' = "auto n = 1'000; wchar_t g = L'\uE8BD';" } },
    @{ Name = 'a glyph written as itself in a raw string'; Want = @()
       Files = @{ 'a.cpp' = ('auto g = LR"(' + [char]0xE8BD + ')";') } },
    @{ Name = 'a \u in a raw string is not an escape'; Want = @('U+E8BD is in kIconGlyphs and no source draws it')
       Files = @{ 'a.cpp' = 'auto g = LR"(\uE8BD)";' } }
  )
  $bad = 0
  foreach ($c in $cases) {
    $r = Invoke-Check $c.Files $tableOk
    $v = @($r.Violations)
    $want = if ($c.ContainsKey('Want')) { @($c.Want) } else { @() }
    $notWant = if ($c.ContainsKey('NotWant')) { @($c.NotWant) } else { @() }
    $missing = @($want | Where-Object { $w = $_; -not ($v | Where-Object { $_.Contains($w) }) })
    $unwanted = @($notWant | Where-Object { $w = $_; $v | Where-Object { $_.Contains($w) } })
    $ok = if ($want.Count -eq 0) { $v.Count -eq 0 } else { $missing.Count -eq 0 -and $unwanted.Count -eq 0 }
    if (-not $ok) { $bad++ }
    Write-Host ("SELFTEST {0}: {1} ({2} violation(s))" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $c.Name, $v.Count)
    if (-not $ok) {
      $missing | ForEach-Object { Write-Host "    wanted, not found: $_" }
      $unwanted | ForEach-Object { Write-Host "    found, not wanted: $_" }
      $v | ForEach-Object { Write-Host "    violation: $_" }
    }
  }
  if ($bad -gt 0) { Write-Host "SELFTEST FAILED: $bad of $($cases.Count) cases"; exit 1 }
  Write-Host "SELFTEST PASSED: $($cases.Count) of $($cases.Count) cases"
  exit 0
}

$rootPath = (Resolve-Path $Root).Path
$buildOutput = @('Generated Files', 'x64', 'ARM64', 'bin', 'obj')
$files = @{}
foreach ($f in (Get-ChildItem -LiteralPath $rootPath -Recurse -File | Sort-Object FullName)) {
  $rel = $f.FullName.Substring($rootPath.Length).TrimStart('\', '/')
  $skip = $false
  foreach ($dir in $buildOutput) { if (($rel -split '[\\/]') -contains $dir) { $skip = $true } }
  if ($skip) { continue }
  $ext = $f.Extension.ToLowerInvariant()
  if (@('.ttf', '.otf', '.ico', '.png', '.jpg', '.gif', '.pri') -contains $ext) { continue }
  $files[$rel] = [IO.File]::ReadAllText($f.FullName)
}
if ($files.Count -eq 0) { Write-Host "FAIL: no source under $rootPath, so nothing was read"; exit 1 }
$tableFile = Join-Path $rootPath 'App\UrComponents.h'
$tableText = if (Test-Path -LiteralPath $tableFile) { [IO.File]::ReadAllText($tableFile) } else { '' }
$r = Invoke-Check $files $tableText

foreach ($n in $r.Namings) { Write-Host ("naming {0}:{1}: `"{2}`"" -f $n.File, $n.Line, $n.Value) }
# THE TWO NAMINGS THE APP MUST HAVE: a scan that found neither has shown nothing
$markup = @($r.Namings | Where-Object { $_.File -like '*App.xaml' -and (Test-Fallback $_.Value) }).Count
$code = @($r.Namings | Where-Object { $_.File -like '*UrComponents.h' -and (Test-Fallback $_.Value) }).Count
if ($markup -lt 1) { $r.Violations += 'App.xaml does not name the list (UrIconFontFamily), so the markup draws from no checked family' }
if ($code -lt 1) { $r.Violations += 'UrComponents.h does not name the list (kIconFontFamily), so the code draws from no checked family' }
foreach ($l in $r.LeftOut) { Write-Host "left out $l" }
$byCp = @{}
foreach ($g in $r.Glyphs) { if (-not $byCp.ContainsKey($g.CodePoint)) { $byCp[$g.CodePoint] = @() }; $byCp[$g.CodePoint] += "$($g.File):$($g.Line)" }
foreach ($cp in ($byCp.Keys | Sort-Object)) { Write-Host ("glyph U+{0:X4}: {1}" -f $cp, ($byCp[$cp] -join ', ')) }
Write-Host ("read {0} files under {1}: {2} naming(s), {3} glyph(s) drawn, {4} in kIconGlyphs" -f $files.Count, $rootPath, @($r.Namings).Count, $byCp.Count, $r.Table.Count)
if (@($r.Violations).Count -gt 0) {
  foreach ($v in $r.Violations) { Write-Host "FAIL $v" }
  exit 1
}
Write-Host "PASS: every icon names `"$fallback`", and kIconGlyphs is every glyph the sources draw"
exit 0
