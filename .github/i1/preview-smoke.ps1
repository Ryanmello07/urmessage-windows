# THROWAWAY, NEVER MERGE (ci/i1-preview*). Identity phase 1, Task 6 Step 4: the preview zip, smoked
# AS SHIPPED, the README's way, on a runner that has never run URmessage: the zip extracted to a
# fresh folder, no %LOCALAPPDATA%\URmessage-identity-preview and no %LOCALAPPDATA%\URmessage, no
# URMESSAGE_* variable, no credential of any kind, and URmessage.exe started with NO arguments. It
# must open in the fabricated mode, on "Set up this device".
#
# The window is read with UI Automation and captured with PrintWindow; nothing is typed or clicked
# but one UIA Invoke of "This is my first device", whose words screen must capture black, and the
# window is then closed with WindowPattern.Close. Run with Windows PowerShell 5.1.
param(
  [Parameter(Mandatory = $true)][string]$Dir,
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Os
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class PreviewShot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static double Black(Bitmap bmp) {
    var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
    var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    int n = data.Stride * data.Height;
    byte[] px = new byte[n];
    Marshal.Copy(data.Scan0, px, 0, n);
    bmp.UnlockBits(data);
    long total = 0, black = 0;
    for (int y = 0; y < bmp.Height; y++) {
      for (int x = 0; x < bmp.Width; x++) {
        int i = y * data.Stride + x * 4;
        total++;
        if (px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0) black++;
      }
    }
    return total == 0 ? 0 : (double)black / total;
  }
}
"@
[void][PreviewShot]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:failures = 0
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++ }
}
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
function WaitName($win, [string]$name, [int]$seconds) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $e = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    if ($null -ne $e) { return $e }
    Start-Sleep -Milliseconds 400
  }
  return $null
}
function Shot($win, [string]$label) {
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $r = New-Object PreviewShot+RECT
  [void][PreviewShot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [PreviewShot]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $path = Join-Path $Out "preview-$Os-$label.png"
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $black = [PreviewShot]::Black($bmp)
  $bmp.Dispose()
  Note "captured $path (${w}x${h}, $('{0:P2}' -f $black) exactly black)"
  return @{ ok = $ok; black = $black }
}
function DumpNames($win, [string]$label) {
  $all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  $lines = foreach ($e in $all) {
    try { "{0}`t{1}`t{2}" -f $e.Current.ControlType.ProgrammaticName, $e.Current.AutomationId, $e.Current.Name } catch { }
  }
  [IO.File]::WriteAllLines((Join-Path $Out "preview-$Os-$label.uia.txt"), [string[]]$lines, (New-Object Text.UTF8Encoding($false)))
}
function Listing([string]$root) {
  return @(Get-ChildItem -LiteralPath $root -Recurse -Force | ForEach-Object {
    "$($_.FullName.Substring($root.Length)) $(if ($_.PSIsContainer) { 'd' } else { $_.Length })" })
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$osInfo = Get-CimInstance Win32_OperatingSystem
Note "runner $Os : $($osInfo.Caption), version $($osInfo.Version), build $($osInfo.BuildNumber); user $env:USERNAME"

# ---- 1. AS SHIPPED: a fresh profile and no credential ------------------------------------------
$local = $env:LOCALAPPDATA
$own = Join-Path $local 'URmessage-identity-preview'
$real = Join-Path $local 'URmessage'
Check (-not (Test-Path -LiteralPath $own)) "no $own exists before the launch"
Check (-not (Test-Path -LiteralPath $real)) "no $real exists before the launch"
$set = @(Get-ChildItem env: | Where-Object { $_.Name -like 'URMESSAGE*' } | ForEach-Object { $_.Name })
Check ($set.Count -eq 0) "no URMESSAGE_* variable is set for the launch ($($set -join ', '))"
$exe = Join-Path $Dir 'URmessage.exe'
Check (Test-Path -LiteralPath $exe) "the extracted folder holds $exe"
Check (-not (Test-Path -LiteralPath (Join-Path $Dir 'URnetworkSdk.dll'))) 'the extracted folder holds no URnetworkSdk.dll'
$before = Listing $Dir

# ---- 2. run it with NO flags --------------------------------------------------------------------
$p = Start-Process -FilePath $exe -WorkingDirectory $Dir -PassThru
Note "started pid $($p.Id)"
Start-Sleep -Seconds 2
$cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $($p.Id)").CommandLine
Note "its command line: $cmd"
$args0 = ($cmd -replace '^\s*("[^"]*"|\S+)', '').Trim()
Check ($args0 -eq '') "it was started with no arguments ('$args0')"

# ---- 3. the fabricated mode, on "Set up this device" -------------------------------------------
$win = AppWindow $p.Id 90
Check ($null -ne $win) 'a window opened'
if ($null -ne $win) {
  $banner = 'Demo model: made-up data. Nothing on this screen is a real account, device or phrase.'
  Check ($null -ne (WaitName $win 'Set up this device' 90)) "the window opens on 'Set up this device'"
  Check ($null -ne (WaitName $win $banner 10)) "the screen carries the demo banner: $banner"
  Check ($null -ne (WaitName $win 'DEMO' 10)) "the title bar's chip says DEMO"
  Check ($null -ne (WaitName $win 'This is my first device' 10)) "it offers 'This is my first device'"
  Check ($null -ne (WaitName $win 'Code: 7KQ4 M2XD 9PRF' 10)) 'it shows the made-up code'
  Start-Sleep -Seconds 3
  DumpNames $win 'setup'
  $s1 = Shot $win 'setup'
  Check ($s1.ok -and $s1.black -lt 0.5) "the setup screen does not capture black ($('{0:P2}' -f $s1.black))"

  # ---- 4. one Invoke: the words screen, which must capture black ------------------------------
  $first = WaitName $win 'This is my first device' 10
  if ($null -ne $first) {
    $first.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Note "invoked 'This is my first device' (InvokePattern)"
  }
  Check ($null -ne (WaitName $win 'Write down these 25 words' 20)) 'the words screen opened'
  Check ($null -ne (WaitName $win 'Demo words: not a recovery phrase' 10)) "the words are labelled 'Demo words: not a recovery phrase'"
  Start-Sleep -Seconds 2
  DumpNames $win 'words'
  $s2 = Shot $win 'words'
  Check ($s2.ok -and $s2.black -ge 0.995) "the words screen captures black ($('{0:P2}' -f $s2.black))"

  # ---- 5. close it -------------------------------------------------------------------------------
  try {
    $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close()
    Note 'asked the window to close (WindowPattern.Close)'
  } catch { Note "WindowPattern.Close failed: $_" }
}
$exited = $p.WaitForExit(30000)
Check $exited 'the app exited after the window was closed'
if (-not $exited) { Stop-Process -Id $p.Id -Force; $p.WaitForExit(10000) | Out-Null; Note 'the app was stopped' }

# ---- 6. what the launch wrote, and where ------------------------------------------------------
$after = Listing $Dir
Check ((@(Compare-Object $before $after)).Count -eq 0) 'the launch wrote nothing into the extracted folder'
Check (-not (Test-Path -LiteralPath $real)) "the launch wrote nothing under $real (the real app's folder)"
$log = Join-Path $own 'app\logs\urmessage-app.log'
Check (Test-Path -LiteralPath $log) "the launch kept its log in its own folder: $log"
$text = ''
if (Test-Path -LiteralPath $log) {
  $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
  try { $text = (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
  Copy-Item -LiteralPath $log -Destination (Join-Path $Out "preview-$Os-urmessage-app.log")
  Listing $own | Set-Content -LiteralPath (Join-Path $Out "preview-$Os-state-tree.txt") -Encoding utf8
}
Check (-not $text.Contains('live: this launch is live')) 'the log: no launch of it went live'
Check (-not $text.Contains('URnetworkSdk.dll loaded')) 'the log: the SDK was never loaded'
Check ($text.Contains('identity: screen -> setup (capture allowed)')) "the log: the plain launch opened the setup screen"
Check ($text.Contains('identity: capture exclusion ON for words')) 'the log: the words screen asked for capture exclusion'
$text -split "`n" | Where-Object { $_ -match 'identity: |live: ' } | ForEach-Object { Note "  log: $($_.Trim())" }

Note "$($script:failures) check(s) failed"
if ($script:failures -gt 0) { exit 1 }
exit 0
