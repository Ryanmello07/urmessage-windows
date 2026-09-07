# Launch URmessage and PROVE it rendered — the checkpoint-1 gate.
#
# A scaffold that has not been run is a hypothesis. This script launches the
# built exe, finds its window, captures the SCREEN, and reports the pixels, so
# "it renders in the brand" is a measurement rather than a claim.
#
# Four things here are not incidental; each is a trap this project has paid for.
#
#  1. DPI. app.manifest declares PerMonitorV2, so the app works in PHYSICAL
#     pixels. A harness that is not itself DPI-aware reads a virtualised rect
#     (1536x875 where the app logs 1920x1094 at 125%) and then "proves" the
#     wrong size. SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2) is the
#     FIRST call, before anything measures anything.
#  2. FindWindow is not used. From a PowerShell P/Invoke it silently returns 0
#     unless DllImport sets CharSet=CharSet.Unicode — the default marshals ANSI
#     into the W entry point. EnumWindows + GetClassNameW instead, and every
#     string import below is explicitly Unicode.
#  3. Processes are selected by EXECUTABLE PATH, never by name or window title.
#     Those are identical across worktrees, and killing by name would kill
#     another agent's run.
#  4. The capture is CopyFromScreen of an ACTIVE window, not PrintWindow.
#     PrintWindow asks the window to draw ITSELF and never composites a system
#     backdrop, so a PrintWindow capture can pass while the pixels on screen are
#     wrong. That gap is exactly how the Mica regression shipped in the VPN
#     client. (A LOCKED session inverts this: screen capture then returns the
#     lock screen and PrintWindow is the only thing that works. If the numbers
#     below look like a lock screen, that is why.)
#
# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
  [string]$Configuration = "Release",
  [string]$Platform = "x64",
  [int]$SettleMs = 1200,
  # Switches handed to URmessage.exe, e.g. "--demo=thread". NEVER name this
  # $Args: that shadows PowerShell's automatic $args variable inside the
  # script and the shadowing is silent. Forwarded by SPLAT below, so an empty
  # value passes NOTHING - Start-Process rejects an empty -ArgumentList.
  [string]$AppArgs = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$exe = Join-Path $repo "app\build\$Platform\$Configuration\URmessage.exe"
if (-not (Test-Path $exe)) { throw "not built: $exe" }
$exe = (Resolve-Path $exe).Path

$outDir = Join-Path $repo ".verify"
New-Item -ItemType Directory -Force $outDir | Out-Null
$shot = Join-Path $outDir "urmessage-window.png"

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class Win {
  // CharSet.Unicode on EVERY string import. See the header: the default
  // marshals ANSI into the W entry point and the call silently fails.
  [DllImport("user32.dll", SetLastError=true)]
  public static extern int SetProcessDpiAwarenessContext(IntPtr value);

  public delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);
  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool EnumWindows(EnumProc cb, IntPtr lparam);

  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern int GetClassNameW(IntPtr hwnd, StringBuilder buf, int max);

  [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern int GetWindowTextW(IntPtr hwnd, StringBuilder buf, int max);

  [DllImport("user32.dll", SetLastError=true)]
  public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);

  [DllImport("user32.dll")]
  public static extern bool IsWindowVisible(IntPtr hwnd);

  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int left, top, right, bottom; }

  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);

  [DllImport("user32.dll")]
  public static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")]
  public static extern bool ShowWindow(IntPtr hwnd, int cmd);
  [DllImport("user32.dll")]
  public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")]
  public static extern uint GetDpiForWindow(IntPtr hwnd);

  [DllImport("user32.dll", SetLastError=true)]
  public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);

  // PW_RENDERFULLCONTENT = 2. Asks the window to draw ITSELF into a DC:
  // immune to z-order and to any dimming overlay, and it also draws parts that
  // are off-screen. Its ONE blind spot is a system backdrop (Mica/acrylic),
  // which it never composites — and this app deliberately has none (see
  // WindowShell.cpp), so for THIS window it is a faithful capture and not the
  // lie it would be for a backdrop change.
  [DllImport("user32.dll")]
  public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);

  // Any Shell_SystemDialogProxy on screen (a Windows Security / firewall
  // prompt) dims the whole desktop, so a CopyFromScreen taken while one is up
  // reads every colour lighter than it is. Detected rather than guessed at.
  public static int SystemDialogCount() {
    int n = 0;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      var c = new StringBuilder(256); GetClassNameW(h, c, c.Capacity);
      if (c.ToString() == "Shell_SystemDialogProxy") n++;
      return true;
    }, IntPtr.Zero);
    return n;
  }

  public class Found { public IntPtr Hwnd; public string Cls; public string Text; }

  public static List<Found> WindowsOf(uint targetPid) {
    var list = new List<Found>();
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != targetPid) return true;
      if (!IsWindowVisible(h)) return true;
      var c = new StringBuilder(256); GetClassNameW(h, c, c.Capacity);
      var t = new StringBuilder(512); GetWindowTextW(h, t, t.Capacity);
      list.Add(new Found { Hwnd = h, Cls = c.ToString(), Text = t.ToString() });
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
"@

# (1) FIRST, before anything measures anything. -4 = PER_MONITOR_AWARE_V2.
[void][Win]::SetProcessDpiAwarenessContext([IntPtr](-4))

# (3) Kill any previous run OF THIS EXACT EXE PATH before starting a new one.
function Stop-ByPath([string]$path) {
  Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $p = $null
    try { $p = $_.MainModule.FileName } catch { }
    $p -and ($p -eq $path)
  } | ForEach-Object {
    Write-Host "stopping pid $($_.Id) ($path)" -ForegroundColor Yellow
    Stop-Process -Id $_.Id -Force -Confirm:$false
  }
}
Stop-ByPath $exe

# Per-worktree app state, so this run cannot collide with another agent's.
$env:URMESSAGE_APP_ROOT = Join-Path $repo ".localstate-verify"
New-Item -ItemType Directory -Force $env:URMESSAGE_APP_ROOT | Out-Null

# Splat rather than a ternary: -ArgumentList "" is a parameter-binding error,
# so the argument has to be ABSENT, not empty, when there is nothing to pass.
$extra = @{}
if ($AppArgs) { $extra['ArgumentList'] = $AppArgs }
$proc = Start-Process -FilePath $exe -PassThru @extra
Write-Host "launched pid $($proc.Id): $exe" -ForegroundColor Cyan

# (2) Poll for the window rather than sleeping a guess.
$deadline = (Get-Date).AddSeconds(20)
$win = $null
while ((Get-Date) -lt $deadline) {
  if ($proc.HasExited) { throw "the process exited with code $($proc.ExitCode) before showing a window" }
  $found = [Win]::WindowsOf([uint32]$proc.Id)
  if ($found.Count -gt 0) {
    $r = New-Object Win+RECT
    foreach ($f in $found) {
      [void][Win]::GetWindowRect($f.Hwnd, [ref]$r)
      if (($r.right - $r.left) -gt 200 -and ($r.bottom - $r.top) -gt 200) { $win = $f; break }
    }
    if ($win) { break }
  }
  Start-Sleep -Milliseconds 200
}
if (-not $win) { throw "no visible window of pid $($proc.Id) after 20s" }

[void][Win]::ShowWindow($win.Hwnd, 9)          # SW_RESTORE
[void][Win]::SetForegroundWindow($win.Hwnd)
# SetForegroundWindow is refused when the caller does not own the foreground,
# which is the normal case for a script. HWND_TOPMOST (-1) is the part that
# actually matters for a capture: it guarantees nothing else is drawn OVER the
# window, whatever has focus. SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE = 0x13.
[void][Win]::SetWindowPos($win.Hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x13)
# The window reveal is a ~500ms spring plus a staggered opacity ripple. Capture
# before it settles and the screenshot shows a half-faded window and proves
# nothing about the finished frame.
Start-Sleep -Milliseconds $SettleMs

$rect = New-Object Win+RECT
[void][Win]::GetWindowRect($win.Hwnd, [ref]$rect)
$w = $rect.right - $rect.left
$h = $rect.bottom - $rect.top
$dpi = [Win]::GetDpiForWindow($win.Hwnd)
$fg = [Win]::GetForegroundWindow()

Write-Host ""
Write-Host "class name  : $($win.Cls)"
Write-Host "window text : $($win.Text)"
Write-Host "window rect : ${w}x${h} at ($($rect.left),$($rect.top))  [PHYSICAL pixels, harness is PerMonitorV2]"
Write-Host "window dpi  : $dpi  (scale $([math]::Round($dpi/96.0,2)))"
Write-Host "dips        : $([int]($w*96/$dpi))x$([int]($h*96/$dpi))"
Write-Host "foreground  : $(if ($fg -eq $win.Hwnd) { 'YES - this window is active, so CopyFromScreen is valid' } else { 'NO - the capture may be of another window' })"

$dialogs = [Win]::SystemDialogCount()
if ($dialogs -gt 0) {
  Write-Host ""
  Write-Host "WARNING: $dialogs Shell_SystemDialogProxy window(s) are on screen (a Windows" -ForegroundColor Yellow
  Write-Host "         Security / firewall prompt). Those DIM THE WHOLE DESKTOP, so every" -ForegroundColor Yellow
  Write-Host "         colour in the screen capture below reads lighter than it is. The" -ForegroundColor Yellow
  Write-Host "         PrintWindow capture is unaffected and is the one to trust here." -ForegroundColor Yellow
}

# (4a) CopyFromScreen — what the desktop actually shows, dimming and all.
$shotScreen = Join-Path $outDir "urmessage-window-screen.png"
$bmpScreen = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmpScreen)
$g.CopyFromScreen($rect.left, $rect.top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$g.Dispose()
$bmpScreen.Save($shotScreen, [System.Drawing.Imaging.ImageFormat]::Png)

# (4b) PrintWindow(PW_RENDERFULLCONTENT) — the window drawing itself. Immune to
# z-order and to the dimming overlay. Valid for THIS app precisely because it
# has no system backdrop; it would be a lie for one that did.
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g2 = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g2.GetHdc()
$printed = [Win]::PrintWindow($win.Hwnd, $hdc, 2)
$g2.ReleaseHdc($hdc)
$g2.Dispose()
if (-not $printed) { Write-Host "PrintWindow FAILED; falling back to the screen capture" -ForegroundColor Red; $bmp.Dispose(); $bmp = $bmpScreen }
$bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)

# A magnified crop of the wordmark. A font reference with a wrong family name
# produces NO error, just silent fallback to a system face, so the ONLY way to
# catch it is to look at the letterforms. PP NeueBit is a pixel/bitmap face and
# is unmistakable next to Segoe UI at 4x.
$cropW = [Math]::Min([int](260 * $dpi / 96.0), $bmp.Width)
$cropH = [Math]::Min([int](48 * $dpi / 96.0), $bmp.Height)
$zoom = 4
$big = New-Object System.Drawing.Bitmap(($cropW * $zoom), ($cropH * $zoom))
$g3 = [System.Drawing.Graphics]::FromImage($big)
$g3.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g3.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$destRect = New-Object System.Drawing.Rectangle(0, 0, ($cropW * $zoom), ($cropH * $zoom))
$g3.DrawImage($bmp, $destRect, 0, 0, $cropW, $cropH, [System.Drawing.GraphicsUnit]::Pixel)
$g3.Dispose()
$shotWordmark = Join-Path $outDir "urmessage-wordmark-4x.png"
$big.Save($shotWordmark, [System.Drawing.Imaging.ImageFormat]::Png)
$big.Dispose()

# Probe the pixels the brand actually depends on. Coordinates are relative to
# the window's top-left, in PHYSICAL pixels, so they are scaled by the window's
# own DPI — the harness measured that above rather than assuming 96.
function Px($b, [int]$x, [int]$y) {
  if ($x -lt 0 -or $y -lt 0 -or $x -ge $b.Width -or $y -ge $b.Height) { return "(out of bounds)" }
  $c = $b.GetPixel($x, $y)
  return ("#{0:X2}{1:X2}{2:X2}" -f $c.R, $c.G, $c.B)
}
$s = $dpi / 96.0
Write-Host ""
Write-Host "pixel probes            PrintWindow   screen      expected"
Write-Host ("  title bar (4,4)        {0}      {1}     #101010  page" -f (Px $bmp 4 4), (Px $bmpScreen 4 4))
Write-Host ("  page, 75%/75%          {0}      {1}     #101010  page" -f (Px $bmp ([int]($w*0.75)) ([int]($h*0.75))), (Px $bmpScreen ([int]($w*0.75)) ([int]($h*0.75))))
Write-Host ("  pane header strip      {0}      {1}     #151515  sheet, one step above the page" -f (Px $bmp ([int](200*$s)) ([int](153*$s))), (Px $bmpScreen ([int](200*$s)) ([int](153*$s))))
Write-Host ("  group header strip     {0}      {1}     #151515  sheet" -f (Px $bmp ([int](200*$s)) ([int](227*$s))), (Px $bmpScreen ([int](200*$s)) ([int](227*$s))))
Write-Host ("  row hairline           {0}      {1}     ~#2E2E2E #1FFFFFFF over the page" -f (Px $bmp ([int](200*$s)) ([int](284*$s))), (Px $bmpScreen ([int](200*$s)) ([int](284*$s))))
$bmp.Dispose(); $bmpScreen.Dispose()

Write-Host ""
Write-Host "screenshot (PrintWindow) : $shot" -ForegroundColor Green
Write-Host "screenshot (screen)      : $shotScreen" -ForegroundColor Green
Write-Host "wordmark crop at 4x      : $shotWordmark" -ForegroundColor Green

# ---- the OTHER side of the one breakpoint ---------------------------------
# Below kWideBreakpointDip the list pane fills the window and there is no
# thread pane; at or above it they sit side by side. Capturing only the default
# size would leave half of ApplyBreakpoint never having been drawn, which is
# the exact "written and never run" failure this checkpoint exists to avoid.
# SWP_NOMOVE is NOT set here (we are resizing); SWP_NOZORDER|SWP_NOACTIVATE =
# 0x14, and the window is already topmost.
$wideW = [int](1200 * $s)
$wideH = [int](800 * $s)
[void][Win]::SetWindowPos($win.Hwnd, [IntPtr]::Zero, $rect.left, $rect.top, $wideW, $wideH, 0x14)
Start-Sleep -Milliseconds 700
$r2 = New-Object Win+RECT
[void][Win]::GetWindowRect($win.Hwnd, [ref]$r2)
$w2 = $r2.right - $r2.left; $h2 = $r2.bottom - $r2.top
$bmpWide = New-Object System.Drawing.Bitmap($w2, $h2)
$g4 = [System.Drawing.Graphics]::FromImage($bmpWide)
$hdc2 = $g4.GetHdc()
[void][Win]::PrintWindow($win.Hwnd, $hdc2, 2)
$g4.ReleaseHdc($hdc2)
$g4.Dispose()
$shotWide = Join-Path $outDir "urmessage-window-wide.png"
$bmpWide.Save($shotWide, [System.Drawing.Imaging.ImageFormat]::Png)
$bmpWide.Dispose()
Write-Host ("screenshot (wide {0}x{1} dip) : {2}" -f [int]($w2*96/$dpi), [int]($h2*96/$dpi), $shotWide) -ForegroundColor Green

Write-Host ""
Write-Host "app still running as pid $($proc.Id)" -ForegroundColor Yellow
