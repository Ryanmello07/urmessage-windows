# THROWAWAY, NEVER MERGE (ci/task58-package-03). Test package 03 (R43), smoked AS SHIPPED.
#
# The README's way, on a runner that has never run URmessage: the zip extracted to a fresh folder,
# no %LOCALAPPDATA%\URmessage, no %URMESSAGE_LIVE_JWT% and no other URMESSAGE_* variable, and
# URmessage.exe started with NO arguments. No credential of any kind is on the runner, so the app
# must say so: the credential dialog, naming the folder and the file it reads.
#
# The window is read with UI Automation and captured with PrintWindow; nothing is typed or clicked
# but one UIA Invoke of the dialog's Close, and the window is then closed with WindowPattern.Close.
# Run with Windows PowerShell 5.1 (UI Automation is a .NET Framework assembly).
param(
  [Parameter(Mandatory = $true)][string]$Dir,
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Os,
  [Parameter(Mandatory = $true)][string]$DllVersion
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Pkg03Shot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][Pkg03Shot]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:failures = 0
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++ }
}
function AppWindow([int]$processId, [int]$seconds = 60) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $processId)
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
      [System.Windows.Automation.TreeScope]::Children, $cond)
    if ($null -ne $win) { return $win }
    Start-Sleep -Milliseconds 500
  }
  return $null
}
function FindByName([int]$processId, [string]$name) {
  $win = AppWindow $processId 5
  if ($null -eq $win) { return $null }
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
  return $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
function WaitName([int]$processId, [string]$name, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $e = FindByName $processId $name
    if ($null -ne $e) { return $e }
    Start-Sleep -Milliseconds 500
  }
  return $null
}
function Shot([int]$processId, [string]$name) {
  $win = AppWindow $processId 10
  if ($null -eq $win) { Check $false "a window to capture for $name"; return }
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $r = New-Object Pkg03Shot+RECT
  [void][Pkg03Shot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [Pkg03Shot]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $path = Join-Path $Out "package03-$Os-$name.png"
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Check $ok "PrintWindow captured the window: $path (${w}x${h})"
}
function DumpNames([int]$processId, [string]$file) {
  $win = AppWindow $processId 10
  if ($null -eq $win) { return }
  $all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  $lines = foreach ($e in $all) {
    try { "{0}`t{1}`t{2}" -f $e.Current.ControlType.ProgrammaticName, $e.Current.AutomationId, $e.Current.Name } catch { }
  }
  $lines | Set-Content -LiteralPath (Join-Path $Out "package03-$Os-$file.txt") -Encoding utf8
}
function Listing([string]$root) {
  return @(Get-ChildItem -LiteralPath $root -Recurse -Force | ForEach-Object {
    "$($_.FullName.Substring($root.Length)) $(if ($_.PSIsContainer) { 'd' } else { $_.Length })" })
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$osInfo = Get-CimInstance Win32_OperatingSystem
Note "runner $Os : $($osInfo.Caption), version $($osInfo.Version), build $($osInfo.BuildNumber); user $env:USERNAME"

# ---- 1. AS SHIPPED: a fresh profile and no credential --------------------------------------------
$local = $env:LOCALAPPDATA
$urm = Join-Path $local 'URmessage'
Check (-not [string]::IsNullOrEmpty($local)) "LOCALAPPDATA is $local"
Check (-not (Test-Path -LiteralPath $urm)) "no $urm exists before the launch"
foreach ($scope in 'Process', 'User', 'Machine') {
  $v = [Environment]::GetEnvironmentVariable('URMESSAGE_LIVE_JWT', $scope)
  Check ([string]::IsNullOrEmpty($v)) "URMESSAGE_LIVE_JWT is unset ($scope)"
}
$set = @(Get-ChildItem env: | Where-Object { $_.Name -like 'URMESSAGE*' } | ForEach-Object { $_.Name })
Check ($set.Count -eq 0) "no URMESSAGE_* variable is set for the launch ($($set -join ', '))"
$exe = Join-Path $Dir 'URmessage.exe'
Check (Test-Path -LiteralPath $exe) "the extracted folder holds $exe"
$before = Listing $Dir
Note "the extracted folder holds $($before.Count) entries"

# ---- 2. run it with NO flags ---------------------------------------------------------------------
$p = Start-Process -FilePath $exe -WorkingDirectory $Dir -PassThru
Note "started pid $($p.Id)"
Start-Sleep -Seconds 2
$cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($p.Id)").CommandLine
Note "its command line: $cmd"
$args0 = ($cmd -replace '^\s*("[^"]*"|\S+)', '').Trim()
Check ($args0 -eq '') "it was started with no arguments ('$args0')"

# ---- 3. the window: the credential dialog, naming the folder and the file ------------------------
$title = 'URmessage needs its credential'
$credDir = Join-Path $local 'URmessage\dev'
$body = "This computer has no URmessage credential yet. Put the credential file you were sent in $credDir and name it user1.jwt, then start URmessage again."
$t = WaitName $p.Id $title 120
Check ($null -ne $t) "the window carries the dialog's title, verbatim: $title"
$b = WaitName $p.Id $body 30
Check ($null -ne $b) "the window names the folder and the file it reads, verbatim: $body"
Start-Sleep -Seconds 3
DumpNames $p.Id 'uia'
Shot $p.Id 'credential'
Check ($null -eq (FindByName $p.Id 'Join')) 'the dialog offers no Join'
Check ($null -eq (FindByName $p.Id 'Copy join code')) 'the dialog offers no join code'

# ---- 4. the app's own log -------------------------------------------------------------------------
$log = Join-Path $local 'URmessage\app\logs\urmessage-app.log'
$deadline = (Get-Date).AddSeconds(30)
$text = ''
while ((Get-Date) -lt $deadline) {
  if (Test-Path -LiteralPath $log) {
    $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    try { $text = (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    if ($text.Contains('no usable credential')) { break }
  }
  Start-Sleep -Milliseconds 500
}
Check ($text.Contains('live: ==== live mesh session starting ====')) 'the log: a plain launch started the live session'
Check ($text.Contains("live: URnetworkSdk.dll loaded; sdk version $DllVersion")) "the log: the packaged dll loaded, sdk version $DllVersion"
Check ($text.Contains("live: no usable credential at $credDir\user1.jwt")) "the log: no usable credential at $credDir\user1.jwt"

# ---- 5. close it -------------------------------------------------------------------------------
# THE DIALOG'S OWN Close: the title bar has two more buttons by that name, so the button is looked
# for inside the dialog, which UI Automation shows as a window named by its title
$win = AppWindow $p.Id 10
$closeCond = New-Object System.Windows.Automation.AndCondition(
  (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, 'Close')),
  (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)))
$dialogCond = New-Object System.Windows.Automation.AndCondition(
  (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $title)),
  (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Window)))
$dialogClose = $null
if ($null -ne $win) {
  foreach ($c in @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $closeCond))) { Note "a 'Close' button: automation id '$($c.Current.AutomationId)'" }
  $dialog = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $dialogCond)
  Check ($null -ne $dialog) "UI Automation shows the dialog as a window named '$title'"
  if ($null -ne $dialog) { $dialogClose = $dialog.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $closeCond) }
}
if ($null -ne $dialogClose) {
  Note "the dialog's Close: automation id '$($dialogClose.Current.AutomationId)'"
  $dialogClose.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  Note "invoked the dialog's Close (InvokePattern)"
  $gone = $false
  $deadline = (Get-Date).AddSeconds(15)
  while ((Get-Date) -lt $deadline) { if ($null -eq (FindByName $p.Id $title)) { $gone = $true; break }; Start-Sleep -Milliseconds 500 }
  Check $gone 'the dialog closed'
} else {
  Check $false 'the dialog has a Close button to invoke'
}
try {
  $win = AppWindow $p.Id 10
  $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close()
  Note 'asked the window to close (WindowPattern.Close)'
} catch { Note "WindowPattern.Close failed: $_" }
$exited = $p.WaitForExit(30000)
Check $exited 'the app exited after the window was closed'
if (-not $exited) {
  Stop-Process -Id $p.Id -Force
  $p.WaitForExit(10000) | Out-Null
  Note 'the app was stopped'
} else {
  Note "the app's exit code: $($p.ExitCode)"
}

# ---- 6. the folder is as it was shipped, and what the launch wrote ------------------------------
$after = Listing $Dir
Check ((@(Compare-Object $before $after)).Count -eq 0) 'the launch wrote nothing into the extracted folder'
if (Test-Path -LiteralPath $urm) {
  Listing $urm | Set-Content -LiteralPath (Join-Path $Out "package03-$Os-state-tree.txt") -Encoding utf8
  Note "the launch wrote under $urm : $((Listing $urm) -join '; ')"
}
if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $Out "package03-$Os-urmessage-app.log") }

Note "$($script:failures) check(s) failed"
if ($script:failures -gt 0) { exit 1 }
exit 0
