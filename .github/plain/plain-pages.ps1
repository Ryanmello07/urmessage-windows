# THROWAWAY, NEVER MERGED (message-windows ci/plain-* on the owner's fork). Runs on a GitHub runner
# ONLY: it launches URmessage.exe, which nothing may do on the owner's machine.
#
# A PLAIN LAUNCH, EVERY PAGE. The standard build is started with NO argument at all, over a fresh
# URMESSAGE_APP_ROOT and no credential. A plain launch is live; on the runner there is no
# URnetworkSdk.dll, so its mesh never answers and the window draws the EMPTY world for the whole
# run, with ActiveRunMode() still Fabricated. That is the state the property is about: no page may
# draw a string that names the demo, nor any value of the fabricated fixture.
#
# Every page a plain launch can reach is visited with UI Automation only (SelectionItemPattern,
# InvokePattern, TogglePattern; no mouse or keyboard input is synthesized), read whole, and
# captured with PrintWindow: Chats, the status strip's relay drawer (opened over Chats, the page
# with the least on it), Contacts, Settings, and, with
# Advanced Mode switched on in Settings, Developer and the status strip's Advanced fields. Network
# is visited if the window offers it.
#
# Run with Windows PowerShell 5.1 (UI Automation is a .NET Framework assembly).
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Variant
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class PlainShot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][PlainShot]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:failures = 0
$script:failed = New-Object System.Collections.Generic.List[string]
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++; $script:failed.Add($what) }
}
New-Item -ItemType Directory -Force $Out | Out-Null

# Values of the fabricated fixture (Demo/DemoWorld.cpp), typed here again on purpose: none may be
# drawn by a plain launch.
$fixture = @('Design team', 'Bo Nakamura', 'Freya Nilsson', 'urmsg-01.ur.io', 'Iceland')

function AppWindow([int]$processId, [int]$seconds) {
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
function FindAllNamed($win, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
  return @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond))
}
function WaitName($win, [string]$name, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $all = FindAllNamed $win $name
    if ($all.Count -gt 0) { return $all[0] }
    Start-Sleep -Milliseconds 400
  }
  return $null
}
function AllNames($win) {
  $all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    [System.Windows.Automation.Condition]::TrueCondition)
  $names = New-Object System.Collections.Generic.List[string]
  foreach ($e in $all) {
    try { $n = $e.Current.Name } catch { $n = '' }
    if ($n -ne '' -and -not $names.Contains($n)) { $names.Add($n) }
  }
  return $names
}
function Shot($win, [string]$file) {
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $r = New-Object PlainShot+RECT
  [void][PlainShot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][PlainShot]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
}
function SelectNav($win, [string]$name) {
  $deadline = (Get-Date).AddSeconds(20)
  while ((Get-Date) -lt $deadline) {
    foreach ($e in (FindAllNamed $win $name)) {
      try { $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); Note "  selected '$name' (SelectionItemPattern)"; return $true } catch { }
      try { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Note "  invoked '$name' (InvokePattern)"; return $true } catch { }
    }
    Start-Sleep -Milliseconds 500
  }
  return $false
}
function InvokeNamed($win, [string]$name) {
  foreach ($e in (FindAllNamed $win $name)) {
    try { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Note "  invoked '$name'"; return $true } catch { }
  }
  return $false
}
function ToggleNamed($win, [string]$name) {
  foreach ($e in (FindAllNamed $win $name)) {
    try {
      $tp = $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
      $before = $tp.Current.ToggleState
      $tp.Toggle()
      Start-Sleep -Milliseconds 800
      Note "  toggled '$name': $before -> $($tp.Current.ToggleState)"
      return $true
    } catch { }
  }
  return $false
}
function LogText([string]$root) {
  $text = ''
  foreach ($f in @(Get-ChildItem -LiteralPath (Join-Path $root 'logs') -Filter *.log -ErrorAction SilentlyContinue)) {
    try {
      $fs = [IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
      $sr = New-Object IO.StreamReader($fs)
      $text += $sr.ReadToEnd()
      $sr.Close()
    } catch { }
  }
  return $text
}
# THE PROPERTY, read off one page: no name names the demo, and none carries a fixture value. The
# offenders are printed in full beside the verdict, so a failure says what it saw.
function JudgePage($win, [string]$page) {
  $names = AllNames $win
  [IO.File]::WriteAllLines((Join-Path $Out "$Variant-plain-$page.names.txt"), [string[]]$names, (New-Object Text.UTF8Encoding($false)))
  Shot $win (Join-Path $Out "$Variant-plain-$page.png")
  $demo = @($names | Where-Object { $_.ToLowerInvariant().Contains('demo') })
  $fab = @($names | Where-Object { $n = $_; @($fixture | Where-Object { $n.Contains($_) }).Count -gt 0 })
  Note "  $page`: $($names.Count) names read"
  foreach ($d in $demo) { Note "    names the demo: $d" }
  foreach ($d in $fab) { Note "    a fixture value: $d" }
  Check ($names.Count -gt 5) "plain $page`: the page was read ($($names.Count) names)"
  Check ($demo.Count -eq 0) "plain $page`: no string names the demo"
  Check ($fab.Count -eq 0) "plain $page`: no fabricated fixture value is drawn"
}

$root = Join-Path $env:RUNNER_TEMP ('plain-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $root | Out-Null
$psi = [System.Diagnostics.ProcessStartInfo]::new($Exe)
$psi.Arguments = ''
$psi.UseShellExecute = $false
$psi.EnvironmentVariables['URMESSAGE_APP_ROOT'] = $root
$p = [System.Diagnostics.Process]::Start($psi)
Note "started a plain launch, pid $($p.Id), root $root"
try {
  $win = AppWindow $p.Id 60
  Check ($null -ne $win) 'plain: a window opened'
  if ($null -ne $win) {
    Start-Sleep -Seconds 6
    $log = LogText $root
    Check ($log.Contains('live: ==== live mesh session starting ====')) 'plain: the launch is live (its session started)'
    Note "  the mesh cannot answer here (no SDK dll): $($log.Contains('URnetworkSdk.dll could not be loaded'))"

    Note '---- chats'
    Check ($null -ne (WaitName $win 'CONVERSATIONS' 20)) 'plain chats: the list pane is up'
    JudgePage $win 'chats'

    Note '---- drawer'
    Check (InvokeNamed $win 'Connection status. Activate to show the relay path.') 'plain drawer: the status strip was invoked'
    Start-Sleep -Seconds 2
    JudgePage $win 'drawer'
    [void](InvokeNamed $win 'Connection status. Activate to show the relay path.')
    Start-Sleep -Seconds 1

    Note '---- contacts'
    Check (SelectNav $win 'Contacts') 'plain contacts: selected'
    Start-Sleep -Seconds 2
    JudgePage $win 'contacts'

    Note '---- settings'
    Check (SelectNav $win 'Settings') 'plain settings: selected'
    Check ($null -ne (WaitName $win 'SETTINGS' 20)) 'plain settings: the page is up'
    Start-Sleep -Seconds 2
    JudgePage $win 'settings'

    Note '---- advanced'
    Check (ToggleNamed $win 'Advanced Mode') 'plain advanced: Advanced Mode switched on in Settings'
    Start-Sleep -Seconds 2
    JudgePage $win 'advanced'

    Note '---- developer'
    Check (SelectNav $win 'Developer') 'plain developer: selected'
    Check ($null -ne (WaitName $win 'DEVELOPER' 20)) 'plain developer: the page is up'
    Start-Sleep -Seconds 2
    JudgePage $win 'developer'

    Note '---- network'
    if ((FindAllNamed $win 'Network').Count -gt 0) {
      Check (SelectNav $win 'Network') 'plain network: selected'
      Start-Sleep -Seconds 2
      JudgePage $win 'network'
    } else {
      Note '  the window offers no Network item on a plain launch'
    }

    $log = LogText $root
    [IO.File]::WriteAllText((Join-Path $Out "$Variant-plain.log"), $log, (New-Object Text.UTF8Encoding($false)))
  }
} finally {
  try {
    $w2 = AppWindow $p.Id 3
    if ($null -ne $w2) { $w2.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close() }
  } catch { Note "  close: $_" }
  if (-not $p.WaitForExit(15000)) { try { $p.Kill() } catch { } }
}

Note "---- $Variant done: $($script:failures) FAIL"
foreach ($f in $script:failed) { Note "  failed: $f" }
[IO.File]::WriteAllLines((Join-Path $Out "$Variant-failed.txt"), [string[]]$script:failed, (New-Object Text.UTF8Encoding($false)))
if ($script:failures -gt 0) { exit 1 }
exit 0
