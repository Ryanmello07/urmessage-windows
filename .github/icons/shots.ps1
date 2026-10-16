# THROWAWAY, NEVER MERGED (message-windows ci/icons-shots* on the owner's fork). Runs on a GitHub runner
# ONLY: it launches URmessage.exe, which nothing may do on the owner's machine.
#
# fix/icon-fallback (urnetwork/windows#12, ported): the app's demo screens, each captured with
# PrintWindow(PW_RENDERFULLCONTENT) of the app's own window, found by its process id. Nothing is typed
# or clicked: each screen is its own launch with --demo=<screen>.
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Label
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class IconShot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][IconShot]::SetThreadDpiAwarenessContext([IntPtr](-4))
New-Item -ItemType Directory -Force $Out | Out-Null
$failures = 0

function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
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
function Shot($win, [string]$name) {
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $r = New-Object IconShot+RECT
  [void][IconShot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [IconShot]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $path = Join-Path $Out "$Label-$name.png"
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Note "SHOT $path ok=$ok ${w}x${h}"
  return $ok
}

# EACH SCREEN ITS OWN LAUNCH, with a fresh app root so no launch reads another's state
$screens = @(
  @{ Name = 'chats'; Args = @('--demo=chats', '--demo-watermark=off') },
  @{ Name = 'thread'; Args = @('--demo=thread', '--demo-actions=revealed', '--demo-watermark=off') },
  @{ Name = 'inspect'; Args = @('--demo=inspect', '--demo-advanced', '--demo-watermark=off') },
  @{ Name = 'network'; Args = @('--demo=network', '--demo-watermark=off') },
  @{ Name = 'settings'; Args = @('--demo=settings', '--demo-watermark=off') },
  @{ Name = 'developer'; Args = @('--demo=developer', '--demo-advanced', '--demo-watermark=off') }
)
foreach ($s in $screens) {
  $root = Join-Path $env:RUNNER_TEMP "icons-app-$($s.Name)"
  New-Item -ItemType Directory -Force $root | Out-Null
  $env:URMESSAGE_APP_ROOT = $root
  $p = Start-Process -FilePath $Exe -ArgumentList $s.Args -PassThru
  Note "launched $($s.Name): pid $($p.Id), $($s.Args -join ' ')"
  $win = AppWindow $p.Id 60
  if ($null -eq $win) { Note "FAIL  $($s.Name): no window within 60 s"; $failures++; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; continue }
  # the window maximised to the screen (1920 x 1080), so the rail and the list both draw
  try {
    $wp = $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
    $wp.SetWindowVisualState([System.Windows.Automation.WindowVisualState]::Maximized)
  } catch { Note "maximise: $_" }
  Start-Sleep -Seconds 8
  if (-not (Shot $win $s.Name)) { Note "FAIL  $($s.Name): PrintWindow answered false"; $failures++ }
  try { $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close() } catch { }
  if (-not $p.WaitForExit(20000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
Note "$failures failure(s)"
if ($failures -gt 0) { exit 1 }
exit 0
