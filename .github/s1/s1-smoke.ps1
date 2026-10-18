# THROWAWAY, NEVER MERGE (ci/s1-zip*). Runs on a GitHub runner ONLY: it launches URmessage.exe.
#
# THE SLICE-1 ZIP, SMOKED AS SHIPPED (PLAN Task 16, Step 4; LR R43): extracted into a fresh folder and
# started with no argument, no URMESSAGE_* variable and a fresh profile, as a person who has just
# unzipped it would - so with no credential either. A plain launch is live, so the library loads,
# the session starts, and the window says there is no credential; it draws no demo string. Driven by
# UI Automation only; captured with PrintWindow.
#
# Run with Windows PowerShell 5.1.
param(
  [Parameter(Mandatory = $true)][string]$Zip,
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Leg
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class S1Smoke {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][S1Smoke]::SetThreadDpiAwarenessContext([IntPtr](-4))
$script:failures = 0
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++ }
}
New-Item -ItemType Directory -Force $Out | Out-Null
$local = $env:LOCALAPPDATA
$data = Join-Path $local 'URmessage'
Check (-not (Test-Path -LiteralPath $data)) "no $data exists before the launch"
$vars = @(Get-ChildItem Env: | Where-Object { $_.Name -like 'URMESSAGE_*' } | ForEach-Object { $_.Name })
Check ($vars.Count -eq 0) "no URMESSAGE_* variable is set for the launch ($($vars -join ', '))"
$fresh = Join-Path $env:RUNNER_TEMP ('fresh-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fresh | Out-Null
Expand-Archive -LiteralPath $Zip -DestinationPath $fresh
$top = Join-Path $fresh 'URmessage-identity-slice1'
$exe = Join-Path $top 'URmessage.exe'
Check (Test-Path -LiteralPath $exe) "the extracted folder holds $exe"
Check (Test-Path -LiteralPath (Join-Path $top 'URnetworkSdk.dll')) 'the extracted folder holds URnetworkSdk.dll'
$before = @(Get-ChildItem -LiteralPath $top -Recurse -File).Count
$p = Start-Process -FilePath $exe -PassThru
Note "started $exe with no argument, pid $($p.Id)"
try {
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
  $win = $null
  $deadline = (Get-Date).AddSeconds(60)
  while ((Get-Date) -lt $deadline -and $null -eq $win) {
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
    if ($null -eq $win) { Start-Sleep -Milliseconds 500 }
  }
  Check ($null -ne $win) 'a window opened'
  $log = Join-Path $data 'app\logs\urmessage-app.log'
  $text = ''
  $deadline = (Get-Date).AddSeconds(60)
  while ((Get-Date) -lt $deadline -and -not $text.Contains('no usable credential')) {
    Start-Sleep -Milliseconds 500
    try {
      $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); $text = $sr.ReadToEnd(); $sr.Close()
    } catch { }
  }
  Check ($text.Contains('live: ==== live mesh session starting ====')) 'the log: the plain launch is live'
  Check ($text.Contains('live: URnetworkSdk.dll loaded')) 'the log: the library loaded'
  Check ($text.Contains('no usable credential')) 'the log: no credential was found, as shipped'
  Check (-not $text.Contains('this device has no identity')) 'the log: with no credential, no session reached the identity'
  Start-Sleep -Seconds 3
  $names = New-Object System.Collections.Generic.List[string]
  if ($null -ne $win) {
    foreach ($e in @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition))) {
      try { $n = $e.Current.Name } catch { $n = '' }
      if ($n -ne '' -and -not $names.Contains($n)) { $names.Add($n) }
    }
  }
  Check ($names.Contains('URmessage needs its credential')) 'the window says URmessage needs its credential'
  $demo = @($names | Where-Object { $_.ToLowerInvariant().Contains('demo') })
  foreach ($d in $demo) { Note "    names the demo: $d" }
  Check ($demo.Count -eq 0) 'no string names the demo (a plain launch is clean)'
  [IO.File]::WriteAllLines((Join-Path $Out "$Leg-names.txt"), [string[]]$names, (New-Object Text.UTF8Encoding($false)))
  if ($null -ne $win) {
    $hwnd = [IntPtr]$win.Current.NativeWindowHandle
    $r = New-Object S1Smoke+RECT
    [void][S1Smoke]::GetWindowRect($hwnd, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap(($r.R - $r.L), ($r.B - $r.T))
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [void][S1Smoke]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save((Join-Path $Out "$Leg-first-start.png"), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  }
  [IO.File]::WriteAllText((Join-Path $Out "$Leg-app.log"), $text, (New-Object Text.UTF8Encoding($false)))
} finally {
  try { if ($null -ne $win) { $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close() } } catch { }
  if (-not $p.WaitForExit(15000)) { try { $p.Kill() } catch { } }
}
Check (@(Get-ChildItem -LiteralPath $top -Recurse -File).Count -eq $before) 'the launch wrote nothing into the extracted folder'
Check (-not (Test-Path -LiteralPath (Join-Path $data 'live\state'))) 'nothing was made in an older build''s live\state'
Note "$($script:failures) check(s) failed"
if ($script:failures -gt 0) { exit 1 }
exit 0
