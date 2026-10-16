# THROWAWAY, NEVER MERGE (ci/i1-affinity-probe). Does PrintWindow see a window that asked for
# SetWindowDisplayAffinity? Three launches of URmessage.exe --demo: no affinity, WDA_EXCLUDEFROMCAPTURE
# (URMSG_PROBE_AFFINITY=1) and WDA_MONITOR (=2). Each window is captured with PrintWindow, flags 2
# (PW_RENDERFULLCONTENT) and flags 0, and the share of black pixels is printed. Nothing is typed or
# clicked; the window is closed with UIA's WindowPattern.
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class ProbeShot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static string Stats(Bitmap bmp) {
    var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
    var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    int n = data.Stride * data.Height;
    byte[] px = new byte[n];
    Marshal.Copy(data.Scan0, px, 0, n);
    bmp.UnlockBits(data);
    long total = 0, black = 0, nearBlack = 0;
    var colours = new System.Collections.Generic.HashSet<int>();
    for (int y = 0; y < bmp.Height; y++) {
      for (int x = 0; x < bmp.Width; x++) {
        int i = y * data.Stride + x * 4;
        int b = px[i], g = px[i + 1], r = px[i + 2];
        total++;
        if (r == 0 && g == 0 && b == 0) black++;
        if (r < 24 && g < 24 && b < 24) nearBlack++;
        if (colours.Count < 5000) colours.Add((r << 16) | (g << 8) | b);
      }
    }
    return String.Format("pixels {0}, exactly black {1:P2}, below 24 {2:P2}, distinct colours {3}{4}",
      total, (double)black / total, (double)nearBlack / total, colours.Count, colours.Count >= 5000 ? "+" : "");
  }
}
"@
[void][ProbeShot]::SetThreadDpiAwarenessContext([IntPtr](-4))
New-Item -ItemType Directory -Force -Path $Out | Out-Null

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

foreach ($mode in @('0', '1', '2')) {
  $root = Join-Path $env:RUNNER_TEMP "probe-root-$mode"
  New-Item -ItemType Directory -Force -Path $root | Out-Null
  $psi = [System.Diagnostics.ProcessStartInfo]::new($Exe)
  $psi.Arguments = '--demo'
  $psi.UseShellExecute = $false
  $psi.EnvironmentVariables['URMESSAGE_APP_ROOT'] = $root
  $psi.EnvironmentVariables['URMSG_PROBE_AFFINITY'] = $mode
  $p = [System.Diagnostics.Process]::Start($psi)
  $win = AppWindow $p.Id 60
  if ($null -eq $win) { Write-Host "mode $mode : no window"; try { $p.Kill() } catch {}; continue }
  Start-Sleep -Seconds 8
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $r = New-Object ProbeShot+RECT
  [void][ProbeShot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  foreach ($flags in @(2, 0)) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [ProbeShot]::PrintWindow($hwnd, $hdc, [uint32]$flags)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $path = Join-Path $Out "probe-affinity$mode-flags$flags.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host ("mode {0} flags {1}: PrintWindow {2}, {3}x{4}, {5}" -f $mode, $flags, $ok, $w, $h, [ProbeShot]::Stats($bmp))
    $bmp.Dispose()
  }
  $log = Get-ChildItem -LiteralPath $root -Recurse -Filter *.log -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($log) { Select-String -LiteralPath $log.FullName -Pattern 'probe:' | ForEach-Object { Write-Host "  log: $($_.Line)" } }
  try {
    $wp = $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
    $wp.Close()
  } catch { Write-Host "  close: $_" }
  if (-not $p.WaitForExit(15000)) { try { $p.Kill() } catch {} }
}
