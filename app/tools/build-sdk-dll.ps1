# Builds URnetworkSdk.dll from urnetwork/message's native composition, at the commits
# app\third_party\urnetwork-sdk\composition.txt pins, and holds the ABI this repository vendors
# (app\third_party\vendor-include: urnetwork_sdk.def, urnetwork_sdk.h, urnetwork_message.h) to it.
#
#   powershell -ExecutionPolicy Bypass -File app\tools\build-sdk-dll.ps1 -Workspace C:\urm-sdk
#       check out the pinned commits under the workspace, compose, build and check. The dll is
#       left at <workspace>\message\sdk\cgo\build\windows\amd64\URnetworkSdk.dll.
#   ... -Stage          and, when every check passed, copy the dll to
#                       app\third_party\urnetwork-sdk\bin\x64\, where the app build copies it
#                       next to URmessage.exe.
#   ... -Vendor         after moving a pin: copy the composition's three ABI files into
#                       vendor-include first, then check as usual. Commit them with the pin.
#   ... -CheckoutOnly   check the pins and check out the commits, and stop (CI reads the Go
#                       version from the checked-out go.mod before it builds).
#   ... -CheckDll <path>  no build: hold a dll you already have (a staged one, say) to the
#                       vendored .def, with checks 8 to 10 below.
#   ... -SelfTest       no build: run each check's mechanism over fixtures it must pass and
#                       fixtures it must refuse, so a check that can no longer fail cannot pass.
#
# -MingwBin <dir> puts a mingw-w64 gcc first on PATH (cgo needs one). -Version <text> is the
# string urnet_version() answers (default 0.0.0-composition.<message>.<sdk>, both commits cut to
# 12). The workspace wants a short path: some of gvisor's paths are long.
#
# WHAT IT CHECKS. Every check runs, each failure is named, and the exit code is 1 if any failed.
#   1. composition.txt: each line is a name, a url under https://github.com/urnetwork/ and a full
#      commit SHA; no name twice; message is pinned.
#   2. each checkout is at its pin, made with core.autocrlf=false, with no local change.
#   3. the pinned names other than message are exactly the sibling checkouts message's
#      sdk/cgo/go.mod replaces by path (../../../<name>), both ways.
#   4. each of those commits is the one message's own scripts/siblings.txt pins at the message
#      commit, so this builds the combination message's test.sh builds. A placeholder there is a
#      refusal: no composition at that message commit has been tested with any core.
#   5. message's sdk/cgo/compose.sh composes, go mod verify passes, and gen regenerates the
#      committed .def byte for byte.
#   6. the composed module's vet and tests pass (package main's C ABI tests, and gen's).
#   7. the c-shared build, and message's scripts/native-exports.sh on it: the exports
#      cgo declares are exactly the .def's names, none is the loopback harness's, and none is a
#      generated messaging export the split retired.
#   8. the dll's own export table (dumpbin /exports) is exactly the vendored .def's names, both
#      ways. The import library is made from that .def, so this is what the app links against.
#   9. the dll imports only what Windows itself provides (API sets, and dlls in System32 that are
#      not a toolchain's lib*.dll), so it loads on a machine with no mingw.
#  10. a fresh 64-bit process loads the dll, GetProcAddress finds every name the vendored .def
#      lists, and urnet_abi_version() and urnet_version() answer. The first call waits for the
#      Go runtime and every package init, which is where a second registration of message.proto
#      panics: the export table and GetProcAddress cannot see that, only a call can.
#  11. the three vendored files are the composition's, byte for byte.
#
# Never launches URmessage.exe.
#
# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
  [string]$Workspace = '',
  [switch]$Stage,
  [switch]$Vendor,
  [switch]$CheckoutOnly,
  [string]$CheckDll = '',
  [switch]$SelfTest,
  [string]$Pins = '',
  [string]$MingwBin = '',
  [string]$Version = ''
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vendorDir = Join-Path $repo 'app\third_party\vendor-include'
$stageDir = Join-Path $repo 'app\third_party\urnetwork-sdk\bin\x64'
if (-not $Pins) { $Pins = Join-Path $repo 'app\third_party\urnetwork-sdk\composition.txt' }

# The three files, by their name in vendor-include and their path in the composed message tree.
$abiFiles = @(
  @{ Name = 'urnetwork_sdk.def';   Composed = 'sdk\cgo\include\urnetwork_sdk.def' },
  @{ Name = 'urnetwork_sdk.h';     Composed = 'sdk\cgo\include\urnetwork_sdk.h' },
  @{ Name = 'urnetwork_message.h'; Composed = 'sdk\cgo\include\urnetwork_message.h' }
)

# ---- results -----------------------------------------------------------------------------------

$script:failures = New-Object System.Collections.Generic.List[string]
function Pass([string]$check, [string]$text) { Write-Host "PASS $check $text" }
function Fail([string]$check, [string]$text) {
  Write-Host "FAIL $check $text"
  $script:failures.Add("$check $text")
}

# ---- native commands ---------------------------------------------------------------------------
# Windows PowerShell 5.1 turns a native command's stderr into error records when it is captured,
# and with ErrorActionPreference Stop the first one throws. Every native call goes through here.

function Invoke-Native {
  param([string]$File, [string[]]$Arguments, [string]$Directory = '', [switch]$Capture)
  $saved = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  if ($Directory) { Push-Location -LiteralPath $Directory }
  try {
    # An error record's own text, not its type: an empty stderr line would print as
    # System.Management.Automation.RemoteException.
    if ($Capture) {
      $lines = @(& $File @Arguments 2>&1 | ForEach-Object { if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { "$_" } })
      return [pscustomobject]@{ Code = $LASTEXITCODE; Lines = $lines }
    }
    & $File @Arguments 2>&1 | ForEach-Object { if ($_ -is [System.Management.Automation.ErrorRecord]) { Write-Host "  $($_.Exception.Message)" } else { Write-Host "  $_" } }
    return [pscustomobject]@{ Code = $LASTEXITCODE; Lines = @() }
  } finally {
    if ($Directory) { Pop-Location }
    $ErrorActionPreference = $saved
  }
}

function Find-Git {
  $git = Get-Command git -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $git) { throw "git is not on PATH" }
  return $git.Source
}

# Git for Windows' bash. NOT whatever "bash" resolves to: on a machine with WSL that is
# System32\bash.exe, which runs the script in Linux. Found from git's own install root.
function Find-GitBash([string]$git) {
  $dir = Split-Path -Parent $git
  foreach ($root in @((Split-Path -Parent $dir), (Split-Path -Parent (Split-Path -Parent $dir)))) {
    $bash = Join-Path $root 'bin\bash.exe'
    if (Test-Path -LiteralPath $bash) { return $bash }
  }
  throw "Git for Windows' bin\bash.exe was not found beside $git"
}

# dumpbin from the newest MSVC toolset of the newest Visual Studio, found the way build-local.ps1
# finds Visual Studio.
function Find-Dumpbin {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere was not found -- is Visual Studio (or Build Tools) installed?" }
  $vs = @(& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath) | Select-Object -First 1
  if (-not $vs) { throw "no Visual Studio with MSBuild was found" }
  $found = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  if (-not $found) { throw "no dumpbin.exe under $vs\VC\Tools\MSVC\*\bin\Hostx64\x64" }
  return $found
}

function Get-Sha256([string]$path) { return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }
function To-Posix([string]$path) { return ($path -replace '\\', '/') }
function Write-Utf8([string]$path, [string]$text) {
  [IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# ---- parsers (pure: text in, data out; -SelfTest drives each of them) ---------------------------

# composition.txt and message's scripts/siblings.txt share one format: name url commit. Every row
# is returned, with Problem set when it is not a usable pin, so a caller can say which.
function Read-PinRows([string]$text) {
  $rows = New-Object System.Collections.Generic.List[object]
  $n = 0
  foreach ($raw in ($text -split "`n")) {
    $n++
    $line = $raw.TrimEnd("`r")
    if ($line -match '^\s*(#|$)') { continue }
    $fields = @($line.Trim() -split '\s+')
    $row = [pscustomobject]@{ Line = $n; Name = $fields[0]; Url = ''; Commit = ''; Problem = '' }
    if ($fields.Count -ne 3) {
      $row.Problem = "line $n has $($fields.Count) fields, want 3 (name url commit)"
    } else {
      $row.Url = $fields[1]
      $row.Commit = $fields[2]
      if ($row.Name -cnotmatch '^[a-z][a-z0-9-]*$') {
        $row.Problem = "line ${n}: '$($row.Name)' is not a sibling name"
      } elseif ($row.Url -cnotmatch '^https://github\.com/urnetwork/[A-Za-z0-9_-][A-Za-z0-9_.-]*\.git$' -or $row.Url.Contains('..')) {
        $row.Problem = "line ${n}: '$($row.Name)' fetches from $($row.Url), outside https://github.com/urnetwork/"
      } elseif ($row.Commit -cnotmatch '^[0-9a-f]{40}$') {
        $row.Problem = "line ${n}: '$($row.Name)' is pinned to '$($row.Commit)', not a full commit SHA"
      }
    }
    $rows.Add($row)
  }
  return ,$rows
}

# The usable pin set of composition.txt, or the reasons there is none.
function Test-PinSet($rows) {
  $problems = New-Object System.Collections.Generic.List[string]
  foreach ($row in $rows) { if ($row.Problem) { $problems.Add($row.Problem) } }
  $seen = @{}
  foreach ($row in $rows) {
    if ($seen.ContainsKey($row.Name)) { $problems.Add("'$($row.Name)' is pinned twice (lines $($seen[$row.Name]) and $($row.Line))") }
    else { $seen[$row.Name] = $row.Line }
  }
  if (-not $seen.ContainsKey('message')) { $problems.Add("message is not pinned") }
  return ,$problems
}

# The sibling checkouts a go.mod replaces by path: the first element after ../../../ of every
# replace target that starts there. Targets inside the message repository (.. and ../..) are not
# siblings; any other relative target is reported, never guessed at.
function Get-ReplaceSiblings([string]$goMod) {
  $siblings = New-Object System.Collections.Generic.SortedSet[string]
  $odd = New-Object System.Collections.Generic.List[string]
  $inBlock = $false
  foreach ($raw in ($goMod -split "`n")) {
    $line = ($raw.TrimEnd("`r") -replace '//.*$', '').Trim()
    if (-not $line) { continue }
    $spec = $null
    if ($inBlock) {
      if ($line -eq ')') { $inBlock = $false; continue }
      $spec = $line
    } elseif ($line -match '^replace\s*\($') {
      $inBlock = $true; continue
    } elseif ($line -match '^replace\s+(.*)$') {
      $spec = $Matches[1]
    }
    if (-not $spec) { continue }
    $parts = $spec -split '=>'
    if ($parts.Count -ne 2) { $odd.Add("unreadable replace: $spec"); continue }
    $target = @($parts[1].Trim() -split '\s+')[0]
    if ($target -match '^\.\./\.\./\.\./([A-Za-z0-9_][A-Za-z0-9_-]*)(/.*)?$' -and -not $target.Substring(9).Contains('..')) { [void]$siblings.Add($Matches[1]); continue }
    if ($target -eq '..' -or $target -eq '../..' -or $target -eq './' -or $target -eq '.') { continue }
    if ($target.StartsWith('.')) { $odd.Add("replace target $target is neither inside message nor a sibling") }
  }
  return [pscustomobject]@{ Siblings = @($siblings); Odd = @($odd) }
}

# Both directions of a set difference, by ordinal (case-sensitive) comparison.
function Compare-Both([string[]]$left, [string[]]$right) {
  $l = New-Object System.Collections.Generic.HashSet[string] ([StringComparer]::Ordinal)
  $r = New-Object System.Collections.Generic.HashSet[string] ([StringComparer]::Ordinal)
  foreach ($x in $left) { [void]$l.Add($x) }
  foreach ($x in $right) { [void]$r.Add($x) }
  $onlyLeft = @($left | Where-Object { -not $r.Contains($_) } | Sort-Object -Unique -CaseSensitive)
  $onlyRight = @($right | Where-Object { -not $l.Contains($_) } | Sort-Object -Unique -CaseSensitive)
  return [pscustomobject]@{ OnlyLeft = $onlyLeft; OnlyRight = $onlyRight }
}

# A .def's export names: the first token of every line after EXPORTS, without @ordinals or
# =internal names. A duplicate is reported.
function Read-DefNames([string]$def) {
  $names = New-Object System.Collections.Generic.List[string]
  $problems = New-Object System.Collections.Generic.List[string]
  $seen = New-Object System.Collections.Generic.HashSet[string] ([StringComparer]::Ordinal)
  $inExports = $false
  foreach ($raw in ($def -split "`n")) {
    $line = ($raw.TrimEnd("`r") -replace ';.*$', '').Trim()
    if (-not $line) { continue }
    if ($line -ceq 'EXPORTS') { $inExports = $true; continue }
    if (-not $inExports) { continue }
    $name = ((@($line -split '\s+')[0]) -split '[=@]')[0]
    if ($name -cnotmatch '^[A-Za-z_][A-Za-z0-9_]*$') { $problems.Add("unreadable .def line: $line"); continue }
    if (-not $seen.Add($name)) { $problems.Add("$name is listed twice"); continue }
    $names.Add($name)
  }
  if (-not $inExports) { $problems.Add("no EXPORTS section") }
  return [pscustomobject]@{ Names = @($names); Problems = @($problems) }
}

# dumpbin /exports: the name column of the export table, and its own count of names, which must
# agree, or the parse missed rows.
function Read-DumpbinExports([string[]]$lines) {
  $names = New-Object System.Collections.Generic.List[string]
  $declared = -1
  $inTable = $false
  foreach ($line in $lines) {
    if ($line -match '^\s+(\d+)\s+number of names\s*$') { $declared = [int]$Matches[1]; continue }
    if ($line -match '^\s+ordinal\s+hint\s+RVA\s+name\s*$') { $inTable = $true; continue }
    if (-not $inTable) { continue }
    if ($line -match '^\s*Summary\s*$') { break }
    if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+(?:[0-9A-Fa-f]{8}\s+)?([A-Za-z_][A-Za-z0-9_@?$]*)(\s|$)') { $names.Add($Matches[1]) }
  }
  $problem = ''
  if ($declared -lt 0) { $problem = "dumpbin printed no 'number of names'" }
  elseif ($declared -ne $names.Count) { $problem = "dumpbin declares $declared names and $($names.Count) were read" }
  return [pscustomobject]@{ Names = @($names); Declared = $declared; Problem = $problem }
}

# dumpbin /dependents: the dlls listed under "Image has the following [delay load] dependencies".
function Read-DumpbinDependents([string[]]$lines) {
  $dlls = New-Object System.Collections.Generic.List[string]
  $inList = $false
  foreach ($line in $lines) {
    if ($line -match 'Image has the following (delay load )?dependencies:') { $inList = $true; continue }
    if (-not $inList) { continue }
    if ($line -match '^\s*Summary\s*$') { break }
    if ($line -match '^\s+(\S+\.(dll|DLL|exe|EXE|sys|SYS|drv|DRV))\s*$') { $dlls.Add($Matches[1]) }
  }
  return @($dlls)
}

# What Windows provides itself: an API set, or a dll in System32 that is not a toolchain runtime
# (mingw's libwinpthread-1, libgcc_s_seh-1 and libstdc++-6 are all lib*.dll).
function Test-SystemDll([string]$name, [string]$system32) {
  if ($name -match '^(api|ext)-ms-win-') { return $true }
  if ($name -match '^lib') { return $false }
  return (Test-Path -LiteralPath (Join-Path $system32 $name))
}

# ---- the load check: a fresh 64-bit Windows PowerShell loads the dll -----------------------------

$loadCheckSource = @'
param([string]$Dll, [string]$NamesFile)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class UrmSdkLoadCheck {
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode, ExactSpelling = true)]
  public static extern IntPtr LoadLibraryW(string path);
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi, ExactSpelling = true)]
  public static extern IntPtr GetProcAddress(IntPtr module, string name);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate int Int32Fn();
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate IntPtr StringFn();
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void FreeFn(IntPtr p);
}
"@
if (-not [Environment]::Is64BitProcess) { "LOADCHECK not a 64-bit process"; exit 5 }
$module = [UrmSdkLoadCheck]::LoadLibraryW($Dll)
if ($module -eq [IntPtr]::Zero) { "LOADCHECK LoadLibraryW failed, error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"; exit 3 }
$wanted = @(Get-Content -LiteralPath $NamesFile | Where-Object { $_ -ne '' })
$missing = @($wanted | Where-Object { [UrmSdkLoadCheck]::GetProcAddress($module, $_) -eq [IntPtr]::Zero })
"LOADCHECK resolved $($wanted.Count - $missing.Count) of $($wanted.Count)"
foreach ($name in $missing) { "LOADCHECK missing $name" }
$abi = [UrmSdkLoadCheck]::GetProcAddress($module, 'urnet_abi_version')
if ($abi -ne [IntPtr]::Zero) {
  $call = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($abi, [UrmSdkLoadCheck+Int32Fn])
  "LOADCHECK urnet_abi_version $($call.Invoke())"
}
$ver = [UrmSdkLoadCheck]::GetProcAddress($module, 'urnet_version')
$free = [UrmSdkLoadCheck]::GetProcAddress($module, 'urnet_free_string')
if ($ver -ne [IntPtr]::Zero -and $free -ne [IntPtr]::Zero) {
  $call = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($ver, [UrmSdkLoadCheck+StringFn])
  $release = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($free, [UrmSdkLoadCheck+FreeFn])
  $text = $call.Invoke()
  "LOADCHECK urnet_version [$([Runtime.InteropServices.Marshal]::PtrToStringAnsi($text))]"
  $release.Invoke($text)
}
# A Go dll is never unloaded: the process exits with it loaded.
if ($missing.Count -gt 0) { exit 4 }
exit 0
'@

function Invoke-LoadCheck([string]$dll, [string[]]$names, [string]$scratch) {
  $script = Join-Path $scratch 'urm-sdk-loadcheck.ps1'
  $list = Join-Path $scratch 'urm-sdk-loadcheck-names.txt'
  Write-Utf8 $script $loadCheckSource
  Write-Utf8 $list (($names -join "`n") + "`n")
  $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $run = Invoke-Native -File $ps -Arguments @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $script, '-Dll', $dll, '-NamesFile', $list) -Capture
  $resolved = -1; $missing = @(); $abi = ''; $ver = $null
  foreach ($line in $run.Lines) {
    if ($line -match '^LOADCHECK resolved (\d+) of (\d+)$') { $resolved = [int]$Matches[1] }
    elseif ($line -match '^LOADCHECK missing (\S+)$') { $missing += $Matches[1] }
    elseif ($line -match '^LOADCHECK urnet_abi_version (-?\d+)$') { $abi = $Matches[1] }
    elseif ($line -match '^LOADCHECK urnet_version \[(.*)\]$') { $ver = $Matches[1] }
  }
  # Version is $null when urnet_version did not answer, and '' when it answered with nothing (a dll
  # built without -X ...sdk.Version): only the first is a failure.
  return [pscustomobject]@{ Code = $run.Code; Lines = $run.Lines; Resolved = $resolved; Missing = $missing; Abi = $abi; Version = $ver }
}

# ---- checks 8 to 10: a dll against the vendored .def -------------------------------------------

function Test-Dll([string]$dll, [string]$scratch) {
  if (-not (Test-Path -LiteralPath $dll)) { Fail 'dll' "$dll does not exist"; return }
  $size = (Get-Item -LiteralPath $dll).Length
  Write-Host "dll: $dll ($size bytes, sha256 $(Get-Sha256 $dll))"
  $def = Read-DefNames ([IO.File]::ReadAllText((Join-Path $vendorDir 'urnetwork_sdk.def')))
  if ($def.Problems.Count -gt 0) { Fail '8' "vendor-include\urnetwork_sdk.def: $($def.Problems -join '; ')"; return }
  $dumpbin = Find-Dumpbin

  $exports = Read-DumpbinExports ((Invoke-Native -File $dumpbin -Arguments @('/nologo', '/exports', $dll) -Capture).Lines)
  if ($exports.Problem) {
    Fail '8' $exports.Problem
  } else {
    $diff = Compare-Both $exports.Names $def.Names
    if ($diff.OnlyLeft.Count -eq 0 -and $diff.OnlyRight.Count -eq 0) {
      Pass '8' "the dll exports exactly the vendored .def's $($def.Names.Count) names ($(@($def.Names | Where-Object { $_ -clike 'urnet_message_*' }).Count) urnet_message_*)"
    } else {
      foreach ($x in $diff.OnlyLeft) { Write-Host "  exported, not in the .def: $x" }
      foreach ($x in $diff.OnlyRight) { Write-Host "  in the .def, not exported: $x" }
      Fail '8' "the dll's exports and the vendored .def differ: $($diff.OnlyLeft.Count) exported and not listed, $($diff.OnlyRight.Count) listed and not exported"
    }
  }

  $system32 = Join-Path $env:SystemRoot 'System32'
  $deps = Read-DumpbinDependents ((Invoke-Native -File $dumpbin -Arguments @('/nologo', '/dependents', $dll) -Capture).Lines)
  $foreign = @($deps | Where-Object { -not (Test-SystemDll $_ $system32) })
  if ($deps.Count -eq 0) {
    Fail '9' "dumpbin listed no dependency at all, so nothing was asked"
  } elseif ($foreign.Count -gt 0) {
    Fail '9' "the dll imports $($foreign -join ', '), which Windows does not provide; it would not load on a machine without them"
  } else {
    Pass '9' "the dll imports only what Windows provides: $($deps -join ', ')"
  }

  $load = Invoke-LoadCheck $dll $def.Names $scratch
  foreach ($line in $load.Lines) { Write-Host "  $line" }
  $answered = ($null -ne $load.Version)
  if ($load.Code -ne 0 -or $load.Resolved -ne $def.Names.Count -or $load.Missing.Count -gt 0 -or -not $load.Abi -or -not $answered) {
    Fail '10' "loading the dll in a fresh process: exit $($load.Code), resolved $($load.Resolved) of $($def.Names.Count), $($load.Missing.Count) missing, urnet_abi_version '$($load.Abi)', urnet_version answered: $answered"
  } else {
    Pass '10' "a fresh process loads the dll, finds all $($def.Names.Count) names, urnet_abi_version() is $($load.Abi), urnet_version() is '$($load.Version)'"
  }
}

# ---- -SelfTest ---------------------------------------------------------------------------------

function Invoke-SelfTest {
  $script:selfFailures = 0
  function Expect([string]$what, [bool]$ok) {
    if ($ok) { Write-Host "ok   $what" } else { Write-Host "BAD  $what"; $script:selfFailures++ }
  }
  $sha = 'f3f8f2bdd95cf6440935d1c29986c4510c64a534'
  $good = "# c`nmessage https://github.com/urnetwork/message.git $sha`nglog https://github.com/urnetwork/glog.git $sha`n"
  Expect 'pins: a good file is accepted' ((Test-PinSet (Read-PinRows $good)).Count -eq 0)
  Expect 'pins: CRLF line endings are read the same' ((Test-PinSet (Read-PinRows ($good -replace "`n", "`r`n"))).Count -eq 0)
  $bad = @(
    @("message https://github.com/urnetwork/message.git f3f8f2bd", 'not a full commit SHA'),
    @("message https://github.com/urnetwork/message.git FILL_IN_THE_PIN", 'not a full commit SHA'),
    @("message https://github.com/Ryanmello07/urmessage.git $sha", 'outside https://github.com/urnetwork/'),
    @("message https://github.com/urnetwork/../x.git $sha", 'outside https://github.com/urnetwork/'),
    @("message https://github.com/urnetwork/message.git $sha extra", 'want 3'),
    @("message https://github.com/urnetwork/message.git $sha`nmessage https://github.com/urnetwork/message.git $sha", 'pinned twice'),
    @("glog https://github.com/urnetwork/glog.git $sha", 'message is not pinned')
  )
  foreach ($case in $bad) {
    $problems = Test-PinSet (Read-PinRows $case[0])
    Expect "pins: refused, '$($case[1])'" (@($problems | Where-Object { $_ -like "*$($case[1])*" }).Count -gt 0)
  }

  $goMod = "module m`nreplace github.com/urnetwork/message => ../..`nreplace github.com/urnetwork/connect => ../../../connect`nreplace github.com/pion/sctp => ../../../connect/sctp // a comment`nreplace (`n  gvisor.dev/gvisor v0.0.1 => ../../../gvisor`n)`n"
  $sib = Get-ReplaceSiblings $goMod
  Expect 'siblings: line and block replaces, a nested target, an internal target' ((($sib.Siblings -join ',') -eq 'connect,gvisor') -and $sib.Odd.Count -eq 0)
  Expect 'siblings: a relative target that is neither is reported' ((Get-ReplaceSiblings "replace x => ../../../../y`n").Odd.Count -eq 1)
  Expect 'siblings: a sibling path that climbs back out is reported, not read as its first element' ((Get-ReplaceSiblings "replace x => ../../../connect/../sdk`n").Odd.Count -eq 1)
  $both = Compare-Both @('connect', 'glog', 'sdk') @('connect', 'gvisor', 'sdk')
  Expect 'sets: both directions are named' ((($both.OnlyLeft -join ',') -eq 'glog') -and (($both.OnlyRight -join ',') -eq 'gvisor'))
  Expect 'sets: equal sets differ in nothing' ((Compare-Both @('a', 'b') @('b', 'a')).OnlyLeft.Count -eq 0)
  Expect 'sets: the comparison is case-sensitive' ((Compare-Both @('urnet_x') @('URNET_X')).OnlyLeft.Count -eq 1)

  $def = Read-DefNames "; c`nLIBRARY URnetworkSdk`r`nEXPORTS`r`n`turnet_a`n`turnet_b @2`n`turnet_c=internal_c`n"
  Expect '.def: names after EXPORTS, without ordinals or internal names, CRLF or not' ((($def.Names -join ',') -eq 'urnet_a,urnet_b,urnet_c') -and $def.Problems.Count -eq 0)
  Expect '.def: a duplicate is refused' ((Read-DefNames "EXPORTS`n`turnet_a`n`turnet_a`n").Problems.Count -eq 1)
  Expect '.def: no EXPORTS section is refused' ((Read-DefNames "LIBRARY x`n").Problems.Count -eq 1)

  $dumpExports = @('Dump of file x.dll', '', '        1219 number of functions', '           2 number of names', '',
    '    ordinal hint RVA      name', '', '          1    0 013CB2E0 urnet_abi_version', '          2    1 013CB300 urnet_free_string', '', '  Summary')
  $read = Read-DumpbinExports $dumpExports
  Expect 'exports: the name column is read' ((($read.Names -join ',') -eq 'urnet_abi_version,urnet_free_string') -and -not $read.Problem)
  $short = @($dumpExports | Where-Object { $_ -notmatch 'urnet_free_string' })
  Expect 'exports: a row the parse missed is refused by the declared count' ([bool](Read-DumpbinExports $short).Problem)
  $deps = Read-DumpbinDependents @('  Image has the following dependencies:', '', '    KERNEL32.dll', '    api-ms-win-crt-heap-l1-1-0.dll', '', '  Image has the following delay load dependencies:', '', '    libwinpthread-1.dll', '', '  Summary')
  Expect 'dependents: both lists are read' (($deps -join ',') -eq 'KERNEL32.dll,api-ms-win-crt-heap-l1-1-0.dll,libwinpthread-1.dll')
  $system32 = Join-Path $env:SystemRoot 'System32'
  Expect 'system dlls: KERNEL32.dll is Windows' (Test-SystemDll 'KERNEL32.dll' $system32)
  Expect 'system dlls: an API set is Windows' (Test-SystemDll 'api-ms-win-crt-runtime-l1-1-0.dll' $system32)
  Expect "system dlls: mingw's libwinpthread-1.dll is not" (-not (Test-SystemDll 'libwinpthread-1.dll' $system32))
  Expect 'system dlls: a dll that is nowhere is not' (-not (Test-SystemDll 'urm-no-such.dll' $system32))

  # The same mechanisms on real binaries, which need nothing but Windows and dumpbin.
  $dumpbin = Find-Dumpbin
  $kernel32 = Join-Path $system32 'kernel32.dll'
  $real = Read-DumpbinExports ((Invoke-Native -File $dumpbin -Arguments @('/nologo', '/exports', $kernel32) -Capture).Lines)
  Expect "exports: kernel32.dll's own table is read whole ($($real.Names.Count) names, LoadLibraryW among them)" ((-not $real.Problem) -and ($real.Names -ccontains 'LoadLibraryW'))
  $realDeps = Read-DumpbinDependents ((Invoke-Native -File $dumpbin -Arguments @('/nologo', '/dependents', (Join-Path $system32 'ws2_32.dll')) -Capture).Lines)
  Expect "dependents: ws2_32.dll's are read, and all are Windows ($($realDeps.Count))" (($realDeps.Count -gt 0) -and @($realDeps | Where-Object { -not (Test-SystemDll $_ $system32) }).Count -eq 0)
  $scratch = Join-Path ([IO.Path]::GetTempPath()) ("urm-sdk-selftest-" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $scratch | Out-Null
  try {
    $load = Invoke-LoadCheck $kernel32 @('LoadLibraryW', 'GetProcAddress', 'urnet_no_such_export') $scratch
    Expect 'load check: a missing name is named, and only it' (($load.Code -eq 4) -and ($load.Resolved -eq 2) -and (($load.Missing -join ',') -eq 'urnet_no_such_export'))
    $load = Invoke-LoadCheck (Join-Path $scratch 'no-such.dll') @('x') $scratch
    Expect 'load check: a dll that does not load is refused' ($load.Code -eq 3)
  } finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
  }
  if ($script:selfFailures -gt 0) { Write-Host "self-test: $script:selfFailures fixture(s) did not come out as expected"; exit 1 }
  Write-Host 'self-test: every fixture came out as expected'
  exit 0
}

# ---- main --------------------------------------------------------------------------------------

if (-not [Environment]::Is64BitProcess) { throw "run this from a 64-bit PowerShell: the dll is x64, and so is the process that loads it" }
if ($SelfTest) { Invoke-SelfTest }

if ($CheckDll) {
  $scratch = Join-Path ([IO.Path]::GetTempPath()) ("urm-sdk-check-" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $scratch | Out-Null
  try { Test-Dll (Resolve-Path -LiteralPath $CheckDll).Path $scratch } finally { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue }
  if ($script:failures.Count -gt 0) { Write-Host "$($script:failures.Count) check(s) failed"; exit 1 }
  Write-Host 'the dll matches the vendored .def'
  exit 0
}

if (-not $Workspace) { throw "-Workspace <dir> is required: the pinned repositories are checked out there, side by side" }
$git = Find-Git
$bash = Find-GitBash $git

# 1. the pins
$rows = Read-PinRows ([IO.File]::ReadAllText($Pins))
$problems = Test-PinSet $rows
if ($problems.Count -gt 0) {
  foreach ($p in $problems) { Fail '1' $p }
  exit 1
}
$pin = @{}
foreach ($row in $rows) { $pin[$row.Name] = $row }
Pass '1' "composition.txt pins $(@($rows | ForEach-Object { "$($_.Name)@$($_.Commit.Substring(0, 12))" }) -join ', ')"

# 2. the checkouts
New-Item -ItemType Directory -Force -Path $Workspace | Out-Null
$Workspace = (Resolve-Path -LiteralPath $Workspace).Path
$messageDir = Join-Path $Workspace 'message'
$cgoDir = Join-Path $messageDir 'sdk\cgo'
$compose = To-Posix (Join-Path $cgoDir 'compose.sh')
foreach ($row in $rows) {
  $dir = Join-Path $Workspace $row.Name
  if (Test-Path -LiteralPath $dir) {
    $rev = Invoke-Native -File $git -Arguments @('-C', $dir, 'rev-parse', '--verify', '-q', 'HEAD') -Capture
    $head = $rev.Lines -join ''
    if ($rev.Code -ne 0) { Fail '2' "$dir holds no commit (an earlier fetch failed?); remove it and run again"; continue }
    if ($head -ne $row.Commit) { Fail '2' "$dir is at $head, not the pinned $($row.Commit); remove it and run again"; continue }
    $autocrlf = (Invoke-Native -File $git -Arguments @('-C', $dir, 'config', '--get', 'core.autocrlf') -Capture).Lines -join ''
    if ($autocrlf -ne 'false') { Fail '2' "$dir has core.autocrlf '$autocrlf', so its text files may be CRLF on disk; remove it and run again"; continue }
    if ($row.Name -eq 'message' -and (Test-Path -LiteralPath (Join-Path $cgoDir '.composed'))) {
      Invoke-Native -File $bash -Arguments @($compose, '--clean') | Out-Null
    }
    $status = (Invoke-Native -File $git -Arguments @('-C', $dir, 'status', '--porcelain', '--untracked-files=all') -Capture).Lines
    if (@($status | Where-Object { $_ }).Count -gt 0) { Fail '2' "$dir has local changes: $(@($status | Select-Object -First 5) -join ' | ')"; continue }
    Pass '2' "$($row.Name) is checked out at $($row.Commit) (existing checkout, clean)"
  } else {
    New-Item -ItemType Directory -Path $dir | Out-Null
    $steps = @(
      @{ What = 'init'; Args = @('init', '-q', $dir) },
      @{ What = 'config'; Args = @('-C', $dir, 'config', 'core.autocrlf', 'false') },
      @{ What = 'fetch'; Args = @('-C', $dir, 'fetch', '-q', '--depth', '1', $row.Url, $row.Commit) },
      @{ What = 'checkout'; Args = @('-C', $dir, '-c', 'advice.detachedHead=false', 'checkout', '-q', '--detach', 'FETCH_HEAD') }
    )
    $why = ''
    foreach ($step in $steps) {
      $run = Invoke-Native -File $git -Arguments $step.Args -Capture
      if ($run.Code -ne 0) { $why = "git $($step.What) exited $($run.Code): $(@($run.Lines | Where-Object { $_ }) -join ' ')"; break }
    }
    $head = ''
    if (-not $why) { $head = (Invoke-Native -File $git -Arguments @('-C', $dir, 'rev-parse', 'HEAD') -Capture).Lines -join '' }
    if ($why -or $head -ne $row.Commit) {
      # An empty checkout left behind would read as "at the wrong commit" on the next run.
      Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
      if (-not $why) { $why = "the checkout is at '$head'" }
      Fail '2' "$($row.Name): $($row.Commit) could not be fetched from $($row.Url) ($why). A commit can be fetched by its SHA once a branch or a pull request on GitHub holds it"
      continue
    }
    Pass '2' "$($row.Name) is checked out at $($row.Commit) (fetched from $($row.Url))"
  }
}
if ($script:failures.Count -gt 0) { Write-Host "$($script:failures.Count) check(s) failed"; exit 1 }
if ($CheckoutOnly) { Write-Host "checked out under $Workspace"; exit 0 }

# 3. the sibling set, both ways
$replaces = Get-ReplaceSiblings ([IO.File]::ReadAllText((Join-Path $cgoDir 'go.mod')))
foreach ($odd in $replaces.Odd) { Fail '3' "message's sdk/cgo/go.mod: $odd" }
$pinnedSiblings = @($rows | Where-Object { $_.Name -ne 'message' } | ForEach-Object { $_.Name })
$diff = Compare-Both $pinnedSiblings $replaces.Siblings
if ($diff.OnlyLeft.Count -gt 0) { Fail '3' "pinned, and not a sibling message's sdk/cgo/go.mod replaces by path: $($diff.OnlyLeft -join ', ')" }
if ($diff.OnlyRight.Count -gt 0) { Fail '3' "message's sdk/cgo/go.mod replaces $(@($diff.OnlyRight | ForEach-Object { "../../../$_" }) -join ', ') by path, and composition.txt does not pin it" }
if ($diff.OnlyLeft.Count -eq 0 -and $diff.OnlyRight.Count -eq 0 -and $replaces.Odd.Count -eq 0) {
  Pass '3' "the pinned siblings are exactly the ones message's sdk/cgo/go.mod replaces by path: $($replaces.Siblings -join ', ')"
}

# 4. the message commit's own sibling pins
$theirsPath = Join-Path $messageDir 'scripts\siblings.txt'
if (-not (Test-Path -LiteralPath $theirsPath)) {
  Fail '4' "message has no scripts/siblings.txt at $($pin['message'].Commit)"
} else {
  $theirs = @{}
  foreach ($row in (Read-PinRows ([IO.File]::ReadAllText($theirsPath)))) { $theirs[$row.Name] = $row }
  foreach ($name in $pinnedSiblings) {
    $mine = $pin[$name]
    if (-not $theirs.ContainsKey($name)) { Fail '4' "${name}: message's scripts/siblings.txt does not pin it"; continue }
    $t = $theirs[$name]
    if ($t.Problem) { Fail '4' "${name}: message's scripts/siblings.txt at $($pin['message'].Commit.Substring(0, 12)) is not a usable pin ($($t.Problem)); move the message pin to the commit that fills it"; continue }
    if ($t.Url -ne $mine.Url -or $t.Commit -ne $mine.Commit) { Fail '4' "${name}: composition.txt pins $($mine.Url) $($mine.Commit), message pins $($t.Url) $($t.Commit)"; continue }
    Pass '4' "${name}: the same commit message pins"
  }
  $unused = @($theirs.Keys | Where-Object { $pinnedSiblings -notcontains $_ } | Sort-Object)
  if ($unused.Count -gt 0) { Write-Host "     (message also pins $($unused -join ', '): not a build input of the composition)" }
}

# 5. compose, verify, regenerate the .def
$dll = ''
$envSaved = @{}
foreach ($k in @('PATH', 'CGO_ENABLED', 'GOOS', 'GOARCH', 'GOFLAGS')) { $envSaved[$k] = [Environment]::GetEnvironmentVariable($k, 'Process') }
try {
  if ($MingwBin) { $env:PATH = "$MingwBin;$env:PATH" }
  $env:CGO_ENABLED = '1'; $env:GOOS = 'windows'; $env:GOARCH = 'amd64'; $env:GOFLAGS = ''
  $gcc = Get-Command gcc -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $gcc) { throw "no gcc on PATH: cgo needs a mingw-w64 gcc (pass -MingwBin <dir>)" }
  Write-Host "gcc: $($gcc.Source) -- $((Invoke-Native -File $gcc.Source -Arguments @('--version') -Capture).Lines | Select-Object -First 1)"
  Write-Host "go : $((Invoke-Native -File 'go' -Arguments @('version') -Capture).Lines -join ' ')"

  $composed = Invoke-Native -File $bash -Arguments @($compose, (To-Posix (Join-Path $Workspace 'sdk')))
  if ($composed.Code -ne 0) { Fail '5' "compose.sh refused to compose (exit $($composed.Code))" }
  else {
    $verify = Invoke-Native -File 'go' -Arguments @('mod', 'verify') -Directory $cgoDir
    $gen = Invoke-Native -File 'go' -Arguments @('run', './gen') -Directory $cgoDir
    $same = Invoke-Native -File $git -Arguments @('-C', $messageDir, 'diff', '--quiet', '--', 'sdk/cgo/include/urnetwork_sdk.def')
    if ($verify.Code -ne 0) { Fail '5' "go mod verify failed in sdk/cgo" }
    if ($gen.Code -ne 0) { Fail '5' "go run ./gen failed in sdk/cgo" }
    elseif ($same.Code -ne 0) {
      Invoke-Native -File $git -Arguments @('-C', $messageDir, '--no-pager', 'diff', '--stat', '--', 'sdk/cgo/include/urnetwork_sdk.def') | Out-Null
      Fail '5' "gen does not reproduce message's committed sdk/cgo/include/urnetwork_sdk.def with this core"
      Invoke-Native -File $git -Arguments @('-C', $messageDir, 'checkout', '--', 'sdk/cgo/include/urnetwork_sdk.def') | Out-Null
    }
    if ($verify.Code -eq 0 -and $gen.Code -eq 0 -and $same.Code -eq 0) { Pass '5' "composed; go mod verify passes; gen reproduces the committed .def" }

    # 6. the composed module's own tests, with the host's GOOS/GOARCH (windows/amd64 here)
    $vet = Invoke-Native -File 'go' -Arguments @('vet', './...') -Directory $cgoDir
    $test = Invoke-Native -File 'go' -Arguments @('test', '-count=1', './...') -Directory $cgoDir
    if ($vet.Code -ne 0 -or $test.Code -ne 0) { Fail '6' "the composed module: go vet exit $($vet.Code), go test exit $($test.Code)" }
    else { Pass '6' "the composed module's vet and tests pass" }

    # 7. the library, the way the core SDK's Makefile builds its windows dll
    if (-not $Version) {
      $sdkPart = 'nosdk'
      if ($pin.ContainsKey('sdk')) { $sdkPart = $pin['sdk'].Commit.Substring(0, 12) }
      $Version = "0.0.0-composition.$($pin['message'].Commit.Substring(0, 12)).$sdkPart"
    }
    $outDir = Join-Path $cgoDir 'build\windows\amd64'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $dll = Join-Path $outDir 'URnetworkSdk.dll'
    $header = Join-Path $outDir 'URnetworkSdk.h'
    Remove-Item -LiteralPath $dll, $header -Force -ErrorAction SilentlyContinue
    $build = Invoke-Native -File 'go' -Arguments @('build', '-trimpath', '-buildmode=c-shared', '-ldflags', "-s -w -X github.com/urnetwork/sdk.Version=$Version -buildid=", '-o', $dll, '.') -Directory $cgoDir
    if ($build.Code -ne 0 -or -not (Test-Path -LiteralPath $dll)) {
      Fail '7' "go build -buildmode=c-shared failed (exit $($build.Code))"
    } else {
      $exportsGate = Invoke-Native -File $bash -Arguments @('scripts/native-exports.sh', 'sdk/cgo', (To-Posix $header), (To-Posix $dll)) -Directory $messageDir
      if ($exportsGate.Code -ne 0) { Fail '7' "message's native-exports.sh refused the library (exit $($exportsGate.Code))" }
      else { Pass '7' "built $dll; message's native-exports.sh holds its exports to the .def" }
    }
  }
} finally {
  foreach ($k in $envSaved.Keys) { [Environment]::SetEnvironmentVariable($k, $envSaved[$k], 'Process') }
}

# 11 first when vendoring: -Vendor copies the composition's three files in, and they are then
# checked like any others.
if ($Vendor) {
  foreach ($f in $abiFiles) {
    Copy-Item -LiteralPath (Join-Path $messageDir $f.Composed) -Destination (Join-Path $vendorDir $f.Name) -Force
    Write-Host "vendored $($f.Name) from message\$($f.Composed)"
  }
}
foreach ($f in $abiFiles) {
  $from = Join-Path $messageDir $f.Composed
  $to = Join-Path $vendorDir $f.Name
  if (-not (Test-Path -LiteralPath $from)) { Fail '11' "the composition has no $($f.Composed)"; continue }
  $a = Get-Sha256 $from; $b = Get-Sha256 $to
  if ($a -eq $b) { Pass '11' "vendor-include\$($f.Name) is the composition's, byte for byte ($($a.Substring(0, 16)))" }
  else { Fail '11' "vendor-include\$($f.Name) ($($b.Substring(0, 16))) is not the composition's message\$($f.Composed) ($($a.Substring(0, 16))); re-vendor with -Vendor" }
}

# 8 to 10, on the dll just built
if ($dll -and (Test-Path -LiteralPath $dll)) {
  $scratch = Join-Path $Workspace '.loadcheck'
  New-Item -ItemType Directory -Force -Path $scratch | Out-Null
  Test-Dll $dll $scratch
}

# leave message's checkout as it was fetched
if (Test-Path -LiteralPath (Join-Path $cgoDir '.composed')) { Invoke-Native -File $bash -Arguments @($compose, '--clean') | Out-Null }

if ($script:failures.Count -gt 0) {
  Write-Host ""
  Write-Host "$($script:failures.Count) check(s) failed:"
  foreach ($f in $script:failures) { Write-Host "  FAIL $f" }
  if ($Stage) { Write-Host "nothing was staged" }
  exit 1
}
if ($Stage) {
  New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
  Copy-Item -LiteralPath $dll -Destination (Join-Path $stageDir 'URnetworkSdk.dll') -Force
  Write-Host "staged $(Join-Path $stageDir 'URnetworkSdk.dll') (sha256 $(Get-Sha256 (Join-Path $stageDir 'URnetworkSdk.dll')))"
}
Write-Host "every check passed: $dll"
exit 0
