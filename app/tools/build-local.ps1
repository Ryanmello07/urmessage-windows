# Builds the URmessage solution on THIS machine.
#
# Adapted from the URnetwork VPN client's app/tools/build-local.ps1. The
# toolset/SDK detection is the part worth keeping verbatim: Directory.Build.props
# pins v143 + SDK 10.0.22621.0 (the CI reference box) and a dev machine usually
# has neither exactly. Both pins are overridable, and overriding them changes
# which toolset BUILDS the app, not its runtime floor.
#
# Gone with the tunnel: -SkipDeps / the SDK zip (there is no vendored Go SDK to
# fetch) and the urnetworkd process.
#
# Note for this box: `powershell` exists, `pwsh` does not. Run it as
#   powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
#
# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
  [ValidateSet("x64", "ARM64")]
  [string]$Platform = "x64",
  [ValidateSet("Release", "Debug")]
  [string]$Configuration = "Release",
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
$app = (Resolve-Path "$PSScriptRoot\..").Path

# A running instance holds resources.pri and the exe open, and the failure it
# produces names neither: msbuild reports PRI210 0x800704c8 "File move failed"
# from a temp guid to the output dir. Just close it.
#
# Scoped to THIS worktree's output BY PATH. Killing by NAME alone would
# terminate every URmessage on the machine, so a build in one worktree kills the
# app another agent is driving in theirs. That is not a rare collision — it is
# every build.
$outPrefix = Join-Path $app 'build'
$running = Get-Process URmessage -ErrorAction SilentlyContinue | Where-Object {
  $path = $null
  try { $path = $_.MainModule.FileName } catch { }  # access denied for foreign/elevated procs
  $path -and $path.StartsWith($outPrefix, [System.StringComparison]::OrdinalIgnoreCase)
}
if ($running) {
  Write-Host "stopping running instance(s) from ${outPrefix}" -ForegroundColor Yellow
  $running | Stop-Process -Force -Confirm:$false
  Start-Sleep -Milliseconds 600
}

# --- developer shell ----------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found -- is Visual Studio (or Build Tools) installed?" }
$vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vs) { throw "no Visual Studio install with MSBuild found" }
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

# --- resolve what this box actually has ---------------------------------------
# 14.4x/14.5x => v143 is ABSENT on VS18-only boxes; map major.minor to the
# toolset name msbuild expects.
$toolsets = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name }
if (-not $toolsets) { throw "no MSVC toolset found under $vs" }
$vcver = [version]$toolsets[-1].Name
$toolset = switch ($vcver.Minor) {
  { $_ -ge 50 } { "v145"; break }
  { $_ -ge 40 } { "v143"; break }
  default       { "v143" }
}

$kitRoots = @("${env:ProgramFiles(x86)}\Windows Kits\10\Include", "$env:ProgramFiles\Windows Kits\10\Include") |
  Where-Object { Test-Path $_ }
$winsdk = $kitRoots | ForEach-Object { Get-ChildItem $_ -Directory } |
  Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' -and (Test-Path (Join-Path $_.FullName 'um\windows.h')) } |
  Sort-Object { [version]$_.Name } | Select-Object -Last 1
if (-not $winsdk) { throw "no Windows 10 SDK found" }

Write-Host "toolset  : $toolset (MSVC $vcver)" -ForegroundColor Cyan
Write-Host "win sdk  : $($winsdk.Name)" -ForegroundColor Cyan
Write-Host "building : $Platform $Configuration" -ForegroundColor Cyan

# --- build --------------------------------------------------------------------
$log = "$app\msbuild-local-$Platform.log"
$targets = if ($Clean) { "/t:Rebuild" } else { @() }
$sw = [Diagnostics.Stopwatch]::StartNew()

msbuild "$app\URmessage.sln" `
  /restore `
  @targets `
  /p:Configuration=$Configuration `
  /p:Platform=$Platform `
  /p:PlatformToolset=$toolset `
  /p:WindowsTargetPlatformVersion=$($winsdk.Name) `
  /m `
  /v:minimal `
  /clp:Summary `
  /flp:"LogFile=$log;Verbosity=detailed"

$code = $LASTEXITCODE
$sw.Stop()
if ($code -ne 0) {
  Write-Host "FAILED in $([int]$sw.Elapsed.TotalSeconds)s -- detailed log: $log" -ForegroundColor Red
  exit $code
}
Write-Host "OK in $([int]$sw.Elapsed.TotalSeconds)s -> $app\build\$Platform\$Configuration\" -ForegroundColor Green
