# Checks the URnetwork SDK's vendored ABI against itself, and against what
# URmessage.exe imports from URnetworkSdk.dll.
#
# THE ABI IS THREE FILES, copied together from one sdk/cgo build into
# app\third_party\vendor-include: urnetwork_sdk.h, urnetwork_message.h and
# urnetwork_sdk.def (README, The SDK). The import library is made from the .def
# (App.vcxproj, UrmSdkImportLib) and the dll is delay-loaded, so nothing in the
# build ever opens the dll, and three files copied from two different builds
# compile and link green. This holds them together:
#
#   1. every function the exe imports from URnetworkSdk.dll, read from its
#      delay-import table by dumpbin, is an export the .def names;
#   2. the .def's urnet_message_* exports and urnetwork_message.h's
#      urnet_message_* declarations are one set, checked in both directions;
#   3. with -DiagnoseOutput: the exe's own reading of that table, which is what
#      the live path asks the dll for before its first call (Live/SdkImports.h),
#      equals dumpbin's reading. --diagnose prints it as a count and a SHA-256
#      of the sorted names, and this computes the same over dumpbin's names.
#
# It reads the exe, the .def and the header. The dll is neither needed nor read:
# the run-time half is in the app, where the live worker asks the loaded dll for
# every import and starts no session while any is missing.
#
# -SelfTest runs the same parsers and checks over fixtures: a clean one, and one
# for each way a check can fail. It passes only if the clean ones pass and every
# other fails for exactly its own reason, so a check that has stopped being able
# to fail cannot pass. CI runs it first.
#
# Run it the way CI does, after the build and the --diagnose step:
#   powershell -NoProfile -ExecutionPolicy Bypass -File app\tools\verify-sdk-imports.ps1 -SelfTest
#   powershell -NoProfile -ExecutionPolicy Bypass -File app\tools\verify-sdk-imports.ps1 -ExePath app\build\x64\Release\URmessage.exe -DiagnoseOutput app\diagnose-x64.out
#
# dumpbin.exe is found through vswhere: the newest MSVC toolset of the newest
# Visual Studio that has the C++ tools. -Dumpbin overrides that.
#
# SPDX-License-Identifier: MPL-2.0
[CmdletBinding(DefaultParameterSetName = "Check")]
param(
  [Parameter(ParameterSetName = "Check", Mandatory = $true)]
  [string]$ExePath,

  [Parameter(ParameterSetName = "Check")]
  [string]$DiagnoseOutput,

  [Parameter(ParameterSetName = "Check")]
  [string]$DefPath,

  [Parameter(ParameterSetName = "Check")]
  [string]$HeaderPath,

  [Parameter(ParameterSetName = "Check")]
  [string]$Dumpbin,

  [Parameter(ParameterSetName = "SelfTest", Mandatory = $true)]
  [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

$DllName = "URnetworkSdk.dll"
$MessagePrefix = "urnet_message_"

# --- parsers -----------------------------------------------------------------

# The .def's exports, by name: every entry after EXPORTS. ";" starts a comment.
# An entry's first word is its name; "=internal", "@ordinal", NONAME, PRIVATE
# and DATA may follow it, and gen/gen.go writes none of them.
function Read-DefExports([string]$text) {
  $names = New-Object System.Collections.Generic.List[string]
  $inExports = $false
  foreach ($raw in ($text -split "\r?\n")) {
    $line = ($raw -split ";")[0].Trim()
    if ($line -eq "") { continue }
    if ($line -cmatch '^EXPORTS(\s+(.*))?$') {
      $inExports = $true
      $line = $Matches[2]
      if (-not $line) { continue }
    } elseif ($line -cmatch '^(LIBRARY|NAME|DESCRIPTION|HEAPSIZE|STACKSIZE|SECTIONS|STUB|VERSION)\b') {
      $inExports = $false
      continue
    }
    if (-not $inExports) { continue }
    $names.Add((($line -split '\s+')[0] -split '=')[0])
  }
  return ,$names.ToArray()
}

# The header's urnet_message_* functions: every urnet_message_ identifier that
# is followed by "(" once the comments are gone. A function-pointer typedef
# names its type as "(*urnet_message_x_cb)(", where the name is followed by ")",
# so a type is not taken for a function.
function Read-HeaderFunctions([string]$text) {
  $noBlock = [regex]::Replace($text, '/\*.*?\*/', ' ', [System.Text.RegularExpressions.RegexOptions]::Singleline)
  $noComments = [regex]::Replace($noBlock, '//[^\r\n]*', ' ')
  $names = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::Ordinal)
  foreach ($m in [regex]::Matches($noComments, '\b(urnet_message_[A-Za-z0-9_]+)\s*\(')) {
    [void]$names.Add($m.Groups[1].Value)
  }
  return ,([string[]]@($names))
}

# What dumpbin /imports says the exe takes from $dll through its delay-import
# table. The entry reads:
#
#   Section contains the following delay load imports:
#
#     URnetworkSdk.dll
#               00000001 Characteristics
#       0000000140226500 Address of HMODULE
#       (four more table addresses)
#                      0 time date stamp
#
#                                     00000001401583B7   343 urnet_message_group_react
#                                     (one line per function)
#
# Answers Found (the dll has an entry in that table), Names, and Problems: any
# line of the entry this cannot read, and any import by ordinal, which the .def
# never declares. An entry in the ORDINARY import table is not one of these.
function Read-DelayImports([string[]]$lines, [string]$dll) {
  $found = $false
  $names = New-Object System.Collections.Generic.List[string]
  $problems = New-Object System.Collections.Generic.List[string]
  $state = "section"
  foreach ($raw in $lines) {
    $line = $raw.TrimEnd()
    if ($state -eq "section") {
      if ($line -match '^\s*Section contains the following delay load imports:$') { $state = "dll" }
    } elseif ($state -eq "dll") {
      if ($line -match '^\s*(Summary$|Section contains the following)') { break }
      if ($line.Trim() -ieq $dll) {
        $found = $true
        $state = "header"
      }
    } elseif ($state -eq "header") {
      if ($line -eq "") {
        $state = "imports"
        continue
      }
      if ($line -notmatch '^\s+[0-9A-Fa-f]+ (Characteristics|Address of HMODULE|Import Address Table|Import Name Table|Bound Import Name Table|Unload Import Name Table|time date stamp)$') {
        $problems.Add("an unexpected line in the $dll entry's header: '$($line.Trim())'")
      }
    } elseif ($state -eq "imports") {
      if ($line -eq "") { break }
      if ($line -match '^\s+[0-9A-Fa-f]{8,16}\s+[0-9A-Fa-f]+\s+([A-Za-z_][A-Za-z0-9_]*)$') {
        $names.Add($Matches[1])
      } elseif ($line -match '\bOrdinal\s+(\d+)$') {
        $problems.Add("a $dll function imported by ordinal $($Matches[1]); the vendored .def declares no ordinals")
      } else {
        $problems.Add("an unreadable line among the $dll imports: '$($line.Trim())'")
      }
    }
  }
  return [pscustomobject]@{ Found = $found; Names = $names.ToArray(); Problems = $problems.ToArray() }
}

# The --diagnose "sdk imports" line (Startup.cpp, SdkImportsDiagnostic).
function Read-DiagnoseLine([string]$text) {
  foreach ($line in ($text -split "\r?\n")) {
    if ($line -cmatch '^\s*sdk imports\s*:\s*(.*)$') {
      $rest = $Matches[1].Trim()
      $m = [regex]::Match($rest, '^PASS (\d+) functions delay-loaded from URnetworkSdk\.dll, sha256 of their sorted names ([0-9a-f]{64});')
      if ($m.Success) {
        return [pscustomobject]@{ Present = $true; Pass = $true; Count = [int]$m.Groups[1].Value; Sha256 = $m.Groups[2].Value; Text = $rest }
      }
      return [pscustomobject]@{ Present = $true; Pass = $false; Count = -1; Sha256 = ""; Text = $rest }
    }
  }
  return [pscustomobject]@{ Present = $false; Pass = $false; Count = -1; Sha256 = ""; Text = "" }
}

# --- checks ------------------------------------------------------------------

function Get-OrdinalSorted([string[]]$names) {
  $sorted = New-Object string[] $names.Count
  for ($i = 0; $i -lt $names.Count; $i++) { $sorted[$i] = $names[$i] }
  [System.Array]::Sort($sorted, [System.StringComparer]::Ordinal)
  return ,$sorted
}

# SHA-256 of the names sorted bytewise, joined with "\n", with no newline at the
# end, as UTF-8: the bytes Live/SdkImports.cpp hashes.
function Get-SortedNamesSha256([string[]]$names) {
  $sorted = Get-OrdinalSorted $names
  $bytes = [System.Text.Encoding]::UTF8.GetBytes([string]::Join("`n", $sorted))
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $digest = $sha.ComputeHash($bytes)
  } finally {
    $sha.Dispose()
  }
  return -join ($digest | ForEach-Object { $_.ToString("x2") })
}

function New-NameSet([string[]]$names) {
  $set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
  foreach ($n in $names) { [void]$set.Add($n) }
  return ,$set
}

# Every check, over parsed inputs. Answers each failure as "CODE: text", so the
# self-test can tell that a fixture failed for its own reason and no other.
# $diagnose is $null when there is no --diagnose output to hold against dumpbin.
function Test-SdkAbi([string[]]$defExports, [string[]]$headerFunctions, $imports, $diagnose) {
  $failures = New-Object System.Collections.Generic.List[string]
  $def = New-NameSet $defExports
  $header = New-NameSet $headerFunctions
  $defMessage = Get-OrdinalSorted @($defExports | Where-Object { $_.StartsWith($MessagePrefix, [System.StringComparison]::Ordinal) })

  # Two empty sets agree in both directions, so emptiness is a failure of its own.
  if ($defMessage.Count -eq 0) { $failures.Add("DEF_EMPTY: the .def names no $MessagePrefix* export") }
  if ($headerFunctions.Count -eq 0) { $failures.Add("HEADER_EMPTY: the header declares no $MessagePrefix* function") }
  foreach ($n in $defMessage) {
    if (-not $header.Contains($n)) { $failures.Add("DEF_NOT_IN_HEADER: $n is exported by the .def and not declared in the header") }
  }
  foreach ($n in (Get-OrdinalSorted $headerFunctions)) {
    if (-not $def.Contains($n)) { $failures.Add("HEADER_NOT_IN_DEF: $n is declared in the header and not exported by the .def") }
  }

  if (-not $imports.Found) {
    $failures.Add("NO_DELAY_ENTRY: the exe's delay-import table has no entry for $DllName")
  } elseif ($imports.Names.Count -eq 0 -and $imports.Problems.Count -eq 0) {
    $failures.Add("NO_IMPORTS: the exe's delay-import entry for $DllName names no function")
  }
  foreach ($p in $imports.Problems) { $failures.Add("DUMPBIN_UNREADABLE: $p") }
  foreach ($n in (Get-OrdinalSorted $imports.Names)) {
    if (-not $def.Contains($n)) { $failures.Add("IMPORT_NOT_IN_DEF: $n is imported from $DllName and not exported by the .def") }
  }

  if ($null -ne $diagnose) {
    if (-not $diagnose.Present) {
      $failures.Add("DIAGNOSE_NO_LINE: the --diagnose output has no 'sdk imports' line")
    } elseif (-not $diagnose.Pass) {
      $failures.Add("DIAGNOSE_NOT_PASS: --diagnose says '$($diagnose.Text)'")
    } else {
      $sha = Get-SortedNamesSha256 $imports.Names
      if ($diagnose.Count -ne $imports.Names.Count -or $diagnose.Sha256 -ne $sha) {
        $failures.Add("DIAGNOSE_DIFFERS: --diagnose read $($diagnose.Count) functions, sha256 $($diagnose.Sha256); dumpbin read $($imports.Names.Count), sha256 $sha")
      }
    }
  }
  return ,$failures.ToArray()
}

function Get-FailureCodes([string[]]$failures) {
  $codes = @($failures | ForEach-Object { ($_ -split ":")[0] } | Select-Object -Unique)
  return ,(Get-OrdinalSorted $codes)
}

# --- self-test ---------------------------------------------------------------

function Invoke-SelfTest {
  $defLines = @(
    "; Code generated by gen/gen.go. DO NOT EDIT.",
    "LIBRARY URnetworkSdk",
    "EXPORTS",
    "`turnet_free_string",
    "`turnet_message_alpha",
    "`turnet_message_beta",
    "`turnet_version"
  )
  $headerLines = @(
    "/* urnet_message_ghost(void) is named only inside this comment. */",
    "#include <stdint.h>",
    "typedef void (*urnet_message_cb)(void* user_data, int32_t attempt);",
    "uint64_t urnet_message_alpha(void);",
    "// urnet_message_ghost(void) again, in a line comment",
    "char* urnet_message_beta(uint64_t self,",
    "                         char** out_error);"
  )
  $ordinaryImports = @(
    "Dump of file URmessage.exe",
    "",
    "File Type: EXECUTABLE IMAGE",
    "",
    "  Section contains the following imports:",
    "",
    "    KERNEL32.dll",
    "             140154000 Import Address Table",
    "             1401F9A20 Import Name Table",
    "                     0 time date stamp",
    "                     0 Index of first forwarder reference",
    "",
    "                          2B5 GetProcAddress",
    "",
    "    COMCTL32.dll",
    "             140154100 Import Address Table",
    "             1401F9B20 Import Name Table",
    "                     0 time date stamp",
    "                     0 Index of first forwarder reference",
    "",
    "                             Ordinal   200",
    ""
  )
  $sdkAsOrdinaryImport = @(
    "    URnetworkSdk.dll",
    "             140154200 Import Address Table",
    "             1401F9C20 Import Name Table",
    "                     0 time date stamp",
    "                     0 Index of first forwarder reference",
    "",
    "                          30A urnet_message_alpha",
    "                          40C urnet_version",
    ""
  )
  $summary = @("  Summary", "", "        2000 .reloc")
  $alpha = "                                    00000001401583B7   30A urnet_message_alpha"
  $version = "                                    00000001401583C3   40C urnet_version"
  $gamma = "                                    00000001401583CF   30B urnet_message_gamma"
  $byOrdinal = "                                    00000001401583DB       Ordinal    17"
  $delayEntry = {
    param([string[]]$importLines)
    return @(
      "  Section contains the following delay load imports:",
      "",
      "    URnetworkSdk.dll",
      "              00000001 Characteristics",
      "      0000000140226500 Address of HMODULE",
      "      0000000140226310 Import Address Table",
      "      0000000140201100 Import Name Table",
      "      0000000140201A78 Bound Import Name Table",
      "      0000000000000000 Unload Import Name Table",
      "                     0 time date stamp",
      ""
    ) + $importLines + @("")
  }
  $cleanDumpbin = $ordinaryImports + (& $delayEntry @($alpha, $version)) + $summary
  # sha256 of "urnet_message_alpha\nurnet_version", computed outside this script, so
  # the clean case pins the hashing convention instead of agreeing with itself.
  $cleanSha = "8a1f5a40b04ff419ed55942ab326362eaf0e2a2748aee587464548fa9d168253"
  # the same over urnet_message_alpha, urnet_message_gamma and urnet_version
  $threeSha = "b488db78a9c49e2253dd26ff9b11383f5b61344ae63dda931f52009177e7ca72"
  # ...and over urnet_Zeta, urnet_message_alpha and urnet_version, in BYTEWISE order,
  # which puts urnet_Zeta first; a dictionary sort puts it last and hashes to
  # 8c02dbf9..., so this case fails if the sort ever stops being bytewise.
  $zeta = "                                    00000001401583E7   40D urnet_Zeta"
  $zetaSha = "59d24e346dbcc0cae8e6f40baf637ea5e78f99686e3c04801b484146c9116707"
  $diagHead = "URmessage startup diagnostics`n  fonts            : present"
  $cleanDiag = "$diagHead`n  sdk imports      : PASS 2 functions delay-loaded from URnetworkSdk.dll, sha256 of their sorted names $cleanSha; the dll is not loaded here`n"

  $cases = @(
    @{ Name = "clean"; Expect = @() },
    @{ Name = "clean, with no --diagnose output to compare"; Diag = $null; Expect = @() },
    @{ Name = "clean, with a name whose bytewise and dictionary orders differ"; Def = ($defLines + "`turnet_Zeta"); Dumpbin = ($ordinaryImports + (& $delayEntry @($alpha, $version, $zeta)) + $summary); Diag = "$diagHead`n  sdk imports      : PASS 3 functions delay-loaded from URnetworkSdk.dll, sha256 of their sorted names $zetaSha; the dll is not loaded here`n"; Expect = @() },
    @{ Name = "an import the .def does not export"; Dumpbin = ($ordinaryImports + (& $delayEntry @($alpha, $version, $gamma)) + $summary); Diag = $null; Expect = @("IMPORT_NOT_IN_DEF"); Needle = "urnet_message_gamma" },
    @{ Name = "a .def message export the header does not declare"; Def = ($defLines + "`turnet_message_delta"); Expect = @("DEF_NOT_IN_HEADER"); Needle = "urnet_message_delta" },
    @{ Name = "a header declaration the .def does not export"; Header = ($headerLines + "int32_t urnet_message_epsilon(uint64_t self);"); Expect = @("HEADER_NOT_IN_DEF"); Needle = "urnet_message_epsilon" },
    @{ Name = "the name the clean header has only in comments, declared"; Header = ($headerLines + "void urnet_message_ghost(void);"); Expect = @("HEADER_NOT_IN_DEF"); Needle = "urnet_message_ghost" },
    @{ Name = "no delay-import table"; Dumpbin = ($ordinaryImports + $summary); Diag = $null; Expect = @("NO_DELAY_ENTRY") },
    @{ Name = "the dll in the ordinary import table only"; Dumpbin = ($ordinaryImports + $sdkAsOrdinaryImport + $summary); Diag = $null; Expect = @("NO_DELAY_ENTRY") },
    @{ Name = "a delay entry naming no function"; Dumpbin = ($ordinaryImports + (& $delayEntry @()) + $summary); Diag = $null; Expect = @("NO_IMPORTS") },
    @{ Name = "an import by ordinal"; Dumpbin = ($ordinaryImports + (& $delayEntry @($alpha, $version, $byOrdinal)) + $summary); Diag = $null; Expect = @("DUMPBIN_UNREADABLE"); Needle = "ordinal 17" },
    @{ Name = "a --diagnose output without the line"; Diag = "$diagHead`n"; Expect = @("DIAGNOSE_NO_LINE") },
    @{ Name = "a --diagnose line that is not PASS"; Diag = "$diagHead`n  sdk imports      : FAIL this exe's delay-import table has no entry for URnetworkSdk.dll`n"; Expect = @("DIAGNOSE_NOT_PASS") },
    @{ Name = "a --diagnose count that differs"; Diag = $cleanDiag.Replace("PASS 2 functions", "PASS 3 functions"); Expect = @("DIAGNOSE_DIFFERS") },
    @{ Name = "a --diagnose digest of another set"; Diag = $cleanDiag.Replace($cleanSha, $threeSha); Expect = @("DIAGNOSE_DIFFERS") },
    @{ Name = "a .def and a header with no message functions"; Def = @("LIBRARY URnetworkSdk", "EXPORTS", "`turnet_free_string", "`turnet_version"); Header = @("/* uint64_t urnet_message_alpha(void); */"); Dumpbin = ($ordinaryImports + (& $delayEntry @($version)) + $summary); Diag = $null; Expect = @("DEF_EMPTY", "HEADER_EMPTY") }
  )

  $bad = 0
  foreach ($case in $cases) {
    $def = $defLines
    if ($case.ContainsKey("Def")) { $def = $case.Def }
    $header = $headerLines
    if ($case.ContainsKey("Header")) { $header = $case.Header }
    $dump = $cleanDumpbin
    if ($case.ContainsKey("Dumpbin")) { $dump = $case.Dumpbin }
    $diagText = $cleanDiag
    if ($case.ContainsKey("Diag")) { $diagText = $case.Diag }
    $diag = $null
    if ($null -ne $diagText) { $diag = Read-DiagnoseLine $diagText }

    $failures = Test-SdkAbi (Read-DefExports ($def -join "`n")) (Read-HeaderFunctions ($header -join "`n")) (Read-DelayImports $dump $DllName) $diag
    $got = Get-FailureCodes $failures
    $want = Get-OrdinalSorted ([string[]]$case.Expect)
    $ok = (($got -join ",") -ceq ($want -join ","))
    if ($ok -and $case.ContainsKey("Needle")) {
      $ok = @($failures | Where-Object { $_.Contains($case.Needle) }).Count -gt 0
    }
    $wantText = "pass"
    if ($want.Count -gt 0) { $wantText = "fail " + ($want -join ",") }
    $gotText = "passed"
    if ($got.Count -gt 0) { $gotText = "failed " + ($got -join ",") }
    if ($ok) {
      Write-Host "  ok    $($case.Name): $gotText"
    } else {
      $bad++
      Write-Host "  WRONG $($case.Name): wanted $wantText, $gotText"
    }
    foreach ($f in $failures) { Write-Host "          $f" }
  }
  if ($bad -gt 0) {
    Write-Host "self-test: $bad of $($cases.Count) fixtures did not come out as their own reason says; the checks cannot be trusted"
    return 1
  }
  $clean = @($cases | Where-Object { $_.Expect.Count -eq 0 }).Count
  Write-Host "self-test: all $($cases.Count) fixtures came out as expected ($clean clean ones pass, $($cases.Count - $clean) fail for their own reason)"
  return 0
}

if ($SelfTest) {
  exit (Invoke-SelfTest)
}

# --- the real check ----------------------------------------------------------

function Find-Dumpbin {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe is not at $vswhere; pass -Dumpbin"
  }
  $found = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find "VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe")
  if ($found.Count -eq 0) {
    throw "vswhere found no dumpbin.exe under the newest Visual Studio with the C++ tools; pass -Dumpbin"
  }
  # ...\VC\Tools\MSVC\<version>\bin\Hostx64\x64\dumpbin.exe: the newest toolset.
  return ($found | Sort-Object { [version](($_ -split '\\')[-5]) } | Select-Object -Last 1)
}

if (-not $DefPath) { $DefPath = Join-Path $PSScriptRoot "..\third_party\vendor-include\urnetwork_sdk.def" }
if (-not $HeaderPath) { $HeaderPath = Join-Path $PSScriptRoot "..\third_party\vendor-include\urnetwork_message.h" }
foreach ($path in @($ExePath, $DefPath, $HeaderPath)) {
  if (-not (Test-Path -LiteralPath $path)) { throw "no file at $path" }
}
if ($DiagnoseOutput -and -not (Test-Path -LiteralPath $DiagnoseOutput)) {
  throw "no --diagnose output at $DiagnoseOutput"
}
if (-not $Dumpbin) { $Dumpbin = Find-Dumpbin }
if (-not (Test-Path -LiteralPath $Dumpbin)) { throw "no dumpbin.exe at $Dumpbin" }
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path

# Through a Process with both streams read, not `& dumpbin.exe 2>&1`: under
# Windows PowerShell 5.1 with ErrorActionPreference Stop, the first line on
# stderr would end the script.
$psi = New-Object System.Diagnostics.ProcessStartInfo $Dumpbin
$psi.Arguments = "/nologo /imports `"$ExePath`""
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$process = [System.Diagnostics.Process]::Start($psi)
$stderrRead = $process.StandardError.ReadToEndAsync()
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $stderrRead.Result
$process.WaitForExit()
if ($process.ExitCode -ne 0) {
  throw "dumpbin /imports exited $($process.ExitCode): $stderr"
}

$defExports = Read-DefExports ([System.IO.File]::ReadAllText($DefPath))
$headerFunctions = Read-HeaderFunctions ([System.IO.File]::ReadAllText($HeaderPath))
$imports = Read-DelayImports ($stdout -split "\r?\n") $DllName
$diagnose = $null
if ($DiagnoseOutput) { $diagnose = Read-DiagnoseLine ([System.IO.File]::ReadAllText($DiagnoseOutput)) }

$defMessage = @($defExports | Where-Object { $_.StartsWith($MessagePrefix, [System.StringComparison]::Ordinal) })
$importSet = New-NameSet $imports.Names
# Through a variable: a function's comma-wrapped array, piped straight on, would
# reach Where-Object as one object.
$sortedDefMessage = Get-OrdinalSorted $defMessage
$notImported = @($sortedDefMessage | Where-Object { -not $importSet.Contains($_) })
$importedMessage = @($imports.Names | Where-Object { $_.StartsWith($MessagePrefix, [System.StringComparison]::Ordinal) })

Write-Host "dumpbin     : $Dumpbin"
Write-Host "exe         : $ExePath"
Write-Host ".def        : $($defExports.Count) exports, $($defMessage.Count) of them $MessagePrefix* ($DefPath)"
Write-Host "header      : $($headerFunctions.Count) $MessagePrefix* functions declared ($HeaderPath)"
Write-Host "exe imports : $($imports.Names.Count) functions delay-loaded from $DllName, $($importedMessage.Count) of them $MessagePrefix*"
# The complements, printed so that what each check does not cover is on the record.
Write-Host "not imported: $($notImported.Count) of the .def's $MessagePrefix* exports: $($notImported -join ', ')"
Write-Host "not compared: the .def's $($defExports.Count - $defMessage.Count) other exports, which urnetwork_sdk.h declares; this holds only the $MessagePrefix* half against a header"
if ($null -ne $diagnose) {
  if ($diagnose.Pass) {
    Write-Host "--diagnose  : $($diagnose.Count) functions, sha256 $($diagnose.Sha256)"
  } else {
    Write-Host "--diagnose  : $($diagnose.Text)"
  }
  Write-Host "dumpbin     : $($imports.Names.Count) functions, sha256 $(Get-SortedNamesSha256 $imports.Names)"
} else {
  Write-Host "--diagnose  : not given, so the exe's own reading of its imports is not compared"
}

$failures = Test-SdkAbi $defExports $headerFunctions $imports $diagnose
if ($failures.Count -gt 0) {
  foreach ($f in $failures) { Write-Host "FAIL $f" }
  Write-Host "the SDK's vendored ABI and the exe's imports disagree in $($failures.Count) way(s); see above"
  exit 1
}
Write-Host "OK: every import is in the .def, and the .def's $MessagePrefix* exports are the header's, both ways"
exit 0
