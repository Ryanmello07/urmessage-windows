# THROWAWAY, NEVER MERGED (message-windows ci/s1-live* on the owner's fork). Runs on a GitHub
# runner ONLY: it launches URmessage.exe, which nothing may do on the owner's machine.
#
# IDENTITY PHASE 1, SLICE 1 (PLAN Task 16): THE LIVE SETUP, UP TO "YOUR ID", WITH THE APP ALONE.
# R67's rig without livepeer (livepeer and the probe gain identities only in Task 15): the app AS
# BUILT, a plain launch with no argument over a fresh URMESSAGE_APP_ROOT, CI's placeholder
# credential file and URMESSAGE_ROUTE=direct, so it dials the compiled-in alpha endpoint with its
# compiled pin. No account, no JWT and no secret.
#
# THE WALK: "Set up this device" -> "This is my first device" -> the 25 words (read by UI
# Automation from their names, as a screen reader reads them; kept in memory and NEVER printed: only
# a SHA-256 digest is) -> typing them back with the first word wrong -> that word named -> the right
# word -> "This device is set up" -> Settings -> Account: "Your ID" and this device.
#
# THE WORDS NEVER LEAVE THIS PROCESS. No line printed carries one; the names of the two words
# screens are never dumped; and before anything is uploaded every text file the run wrote, and the
# app's log, is scanned for any two of the words in a row, and the run fails if one holds them.
#
# DRIVING: UI Automation patterns only (Invoke, Value, Window). No mouse or keyboard input is
# synthesized. CAPTURE: PrintWindow of the app's own window.
#
# Run with Windows PowerShell 5.1 (UI Automation is a .NET Framework assembly).
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Work,
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
public static class S1Shot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static double[] Stats(Bitmap bmp) {
    var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
    var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    int n = data.Stride * data.Height;
    byte[] px = new byte[n];
    Marshal.Copy(data.Scan0, px, 0, n);
    bmp.UnlockBits(data);
    long total = 0, black = 0;
    var colours = new System.Collections.Generic.HashSet<int>();
    for (int y = 0; y < bmp.Height; y++) {
      for (int x = 0; x < bmp.Width; x++) {
        int i = y * data.Stride + x * 4;
        int b = px[i], g = px[i + 1], r = px[i + 2];
        total++;
        if (r == 0 && g == 0 && b == 0) black++;
        if (colours.Count < 100000) colours.Add((r << 16) | (g << 8) | b);
      }
    }
    return new double[] { total == 0 ? 0 : (double)black / total, colours.Count };
  }
}
"@
[void][S1Shot]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:failures = 0
$script:failed = New-Object System.Collections.Generic.List[string]
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++; $script:failed.Add($what) }
}
function Digest([string]$text) {
  $h = [Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($text))
  return (($h | ForEach-Object { $_.ToString('x2') }) -join '').Substring(0, 16)
}

New-Item -ItemType Directory -Force $Work, $Out | Out-Null
$dot = [string][char]0xB7
Note "screen: $([S1Shot]::GetSystemMetrics(0)) x $([S1Shot]::GetSystemMetrics(1)); variant $Variant"

$root = Join-Path $Work 'app'
New-Item -ItemType Directory -Force $root | Out-Null
# CI's placeholder credential file, as R60 names it: the app needs a file to start, and the DIRECT
# route reads no credential.
$cred = Join-Path $Work 'ci-not-a-credential.jwt'
Set-Content -LiteralPath $cred -Value 'ci-not-a-credential (the DIRECT route reads no credential)' -Encoding ascii
$env:URMESSAGE_APP_ROOT = $root
$env:URMESSAGE_LIVE_JWT = $cred
$env:URMESSAGE_ROUTE = 'direct'
Remove-Item Env:URMESSAGE_CI_ENDPOINT -ErrorAction SilentlyContinue
Remove-Item Env:URMESSAGE_CI_PIN -ErrorAction SilentlyContinue
Remove-Item Env:URMESSAGE_LIVE_HANDSHAKE_DIR -ErrorAction SilentlyContinue

function LogText {
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
function WaitLog([string]$needle, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    if ((LogText).Contains($needle)) { Note "log: '$needle'"; return $true }
    Start-Sleep -Milliseconds 500
  }
  Note "log: TIMED OUT waiting $seconds s for '$needle'"
  return $false
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
function AllElements($win) {
  return @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    [System.Windows.Automation.Condition]::TrueCondition))
}
function AllNames($win) {
  $names = New-Object System.Collections.Generic.List[string]
  foreach ($e in (AllElements $win)) {
    try { $n = $e.Current.Name } catch { $n = '' }
    if ($n -ne '' -and -not $names.Contains($n)) { $names.Add($n) }
  }
  return $names
}
function FindNamed($win, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
  return $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
function FindById($win, [string]$id) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
  return $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
function WaitNamed($win, [string]$name, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $e = FindNamed $win $name
    if ($null -ne $e) { return $e }
    Start-Sleep -Milliseconds 400
  }
  return $null
}
function InvokeNamed($win, [string]$name) {
  $e = WaitNamed $win $name 30
  if ($null -eq $e) { return $false }
  try { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Note "invoked '$name'"; return $true } catch { Note "  '$name' takes no Invoke: $_"; return $false }
}
function SetField($win, [string]$name, [string]$value) {
  $e = FindNamed $win $name
  if ($null -eq $e) { return 'absent' }
  try {
    $e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($value)
    return 'set'
  } catch { return "no value pattern ($($_.Exception.GetType().Name))" }
}
function Shot($win, [string]$file) {
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $r = New-Object S1Shot+RECT
  [void][S1Shot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [S1Shot]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $bmp.Save((Join-Path $Out "$Variant-$file.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $stats = [S1Shot]::Stats($bmp)
  $bmp.Dispose()
  return @{ ok = $ok; black = $stats[0]; colours = [int]$stats[1]; w = $w; h = $h }
}
function JudgeCapture([string]$what, $shot, [bool]$black) {
  $share = '{0:P2}' -f $shot.black
  if ($black) {
    Check ($shot.ok -and $shot.black -ge 0.995) "$what captures black ($share of $($shot.w)x$($shot.h) pixels exactly black, $($shot.colours) colours)"
  } else {
    Check ($shot.ok -and $shot.black -lt 0.5) "$what does not capture black ($share exactly black, $($shot.colours) colours)"
  }
}
function DumpNames($win, [string]$file) {
  [IO.File]::WriteAllLines((Join-Path $Out "$Variant-$file.names.txt"), [string[]](AllNames $win), (New-Object Text.UTF8Encoding($false)))
}
function NamesNoDemo($win, [string]$what) {
  $demo = @((AllNames $win) | Where-Object { $_.ToLowerInvariant().Contains('demo') })
  foreach ($d in $demo) { Note "    names the demo: $d" }
  Check ($demo.Count -eq 0) "$what`: no string names the demo"
}
# THE WORDS, two in a row, anywhere in a text: what a leak would look like (the library's own test
# reads "two in a row"); one short word alone is ordinary English
function HoldsWords([string]$text) {
  if (-not $script:words -or $script:words.Count -ne 25) { return $false }
  for ($i = 0; $i -lt 24; $i++) {
    if ($text.Contains($script:words[$i] + ' ' + $script:words[$i + 1])) { return $true }
  }
  return $false
}
function WriteFailed {
  [IO.File]::WriteAllLines((Join-Path $Out "$Variant-failed.txt"), [string[]]$script:failed, (New-Object Text.UTF8Encoding($false)))
}

$script:words = $null
$p = Start-Process -FilePath $Exe -PassThru
Note "started a plain launch (no argument), pid $($p.Id), root $root, route direct, placeholder credential"
try {
  $win = AppWindow $p.Id 60
  Check ($null -ne $win) 'a window opened'
  if ($null -eq $win) { throw 'no window' }

  # ---- 1. "Set up this device" ----------------------------------------------------------------
  Check (WaitLog 'live: route direct to ' 120) 'the app dials the alpha on the DIRECT route'
  Check (WaitLog 'live: this device has no identity' 240) 'the worker reads no identity and waits for the setup'
  Check ($null -ne (WaitNamed $win 'Set up this device' 60)) 'setup: "Set up this device" is drawn'
  Start-Sleep -Seconds 2
  NamesNoDemo $win 'setup'
  Check ($null -ne (FindNamed $win 'This is my first device')) 'setup: "This is my first device" is offered'
  Check ($null -ne (FindNamed $win 'This build cannot add this computer to an account you already use.')) 'setup: it says this build cannot add this computer to an existing account, and shows no code'
  JudgeCapture 'setup' (Shot $win 'setup') $false
  DumpNames $win 'setup'

  # ---- 2. the 25 words ------------------------------------------------------------------------
  Check (InvokeNamed $win 'This is my first device') 'setup: "This is my first device" invoked'
  Check ($null -ne (WaitNamed $win 'Write down these 25 words' 120)) 'words: the words screen is drawn'
  Start-Sleep -Seconds 2
  $found = @{}
  foreach ($e in (AllElements $win)) {
    try { $n = $e.Current.Name } catch { $n = '' }
    $m = [regex]::Match($n, '^Word ([0-9]+): ([a-z]+)$')
    if ($m.Success) { $found[[int]$m.Groups[1].Value] = $m.Groups[2].Value }
  }
  $script:words = @(1..25 | ForEach-Object { $found[$_] })
  $complete = @($script:words | Where-Object { $_ }).Count -eq 25
  Check $complete "words: 25 words are on the screen, numbered 1 to 25 (digest $(if ($complete) { Digest ($script:words -join ' ') } else { 'none' }))"
  Check ($null -ne (FindNamed $win 'Your recovery phrase')) 'words: under "Your recovery phrase"'
  Check ($null -ne (FindNamed $win 'These words cannot bring your account back yet.')) 'words: with "These words cannot bring your account back yet."'
  $copies = @((AllNames $win) | Where-Object { $_ -like 'Copy*' })
  Check ($copies.Count -eq 0) 'words: nothing on the screen copies them'
  JudgeCapture 'words' (Shot $win 'words') $true

  # ---- 3. typing them back --------------------------------------------------------------------
  Check (InvokeNamed $win 'I wrote them down') 'words: "I wrote them down" invoked'
  Check ($null -ne (WaitNamed $win 'Type the 25 words back' 30)) 'typeback: the type-back screen is drawn'
  Start-Sleep -Seconds 2
  $masked = 0
  for ($i = 1; $i -le 25; $i++) {
    $f = FindNamed $win "Word $i"
    if ($null -ne $f -and [bool]$f.Current.IsPassword) { $masked++ }
  }
  Check ($masked -eq 25) "typeback: every word field is a password field, masked ($masked of 25)"
  Check ($null -ne (FindNamed $win 'Show the words I typed')) 'typeback: the reveal switch is there'
  JudgeCapture 'typeback' (Shot $win 'typeback') $true

  # ---- 4. the first word wrong ------------------------------------------------------------------
  $wrongWord = if ($script:words[0] -ne 'abandon') { 'abandon' } else { 'ability' }
  $set = @()
  $set += SetField $win 'Word 1' $wrongWord
  for ($i = 2; $i -le 25; $i++) { $set += SetField $win "Word $i" $script:words[$i - 1] }
  $notSet = @($set | Where-Object { $_ -ne 'set' })
  Check ($notSet.Count -eq 0) "typeback: all 25 fields take a value by UI Automation ($(if ($notSet.Count) { $notSet[0] } else { 'all set' }))"
  Check (InvokeNamed $win 'Check the words') 'typeback: "Check the words" invoked with word 1 wrong'
  $named = $null
  $deadline = (Get-Date).AddSeconds(120)
  while ((Get-Date) -lt $deadline -and $null -eq $named) {
    $status = FindById $win 'IdentityWordStatus'
    if ($null -ne $status -and $status.Current.Name -like 'Word 1 is not the word you were shown*') { $named = $status }
    else { Start-Sleep -Milliseconds 500 }
  }
  Check ($null -ne $named) 'typeback-wrong: the library named word 1, and the screen says so'
  $field1 = FindNamed $win 'Word 1'
  Check ($null -ne $field1 -and $field1.Current.HelpText -eq 'Not the word you were shown.') 'typeback-wrong: word 1 is marked in its own field'
  Check (-not (LogText).Contains('the identity is made')) 'typeback-wrong: nothing was made'
  JudgeCapture 'typeback-wrong' (Shot $win 'typeback-wrong') $true

  # ---- 5. the right word: the identity -----------------------------------------------------------
  Check ((SetField $win 'Word 1' $script:words[0]) -eq 'set') 'typeback-wrong: word 1 typed again'
  Check (InvokeNamed $win 'Check the words') 'typeback-wrong: "Check the words" invoked with every word right'
  Check ($null -ne (WaitNamed $win 'This device is set up' 240)) 'setup-done: "This device is set up" is drawn'
  Check (WaitLog 'the identity is made, and the join road goes on' 30) 'setup-done: the worker made the identity and goes on to the join road'
  Start-Sleep -Seconds 2
  NamesNoDemo $win 'setup-done'
  JudgeCapture 'setup-done' (Shot $win 'setup-done') $false
  DumpNames $win 'setup-done'

  # ---- 6. Settings -> Account ----------------------------------------------------------------------
  Check (InvokeNamed $win 'See Your ID in Settings') 'setup-done: "See Your ID in Settings" invoked'
  Start-Sleep -Seconds 3
  # the join dialog comes up once the setup is closed (today's join-code road): put it off
  if ($null -ne (WaitNamed $win 'Not now' 15)) { [void](InvokeNamed $win 'Not now'); Start-Sleep -Seconds 2 }
  $yourId = FindById $win 'AccountYourId'
  $idText = ''
  if ($null -ne $yourId) { $idText = $yourId.Current.Name -replace '^Your ID, ', '' }
  $idShape = $idText -match '^[0-9a-f]{8}-[0-9a-f]{4}-8[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'
  Check $idShape "account: Your ID is shown, a UUIDv8 of 36 characters (digest $(Digest $idText))"
  $deviceRow = @((AllNames $win) | Where-Object { $_ -match ('^Device ID [0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4} ' + [regex]::Escape($dot) + ' this device ' + [regex]::Escape($dot) + ' phrase device$') })
  Check ($deviceRow.Count -eq 1) 'account: this device is listed with its device ID and the phrase-device mark'
  # SCOPE H4 and G12: Settings' own two device rows read the identity too
  $thisRow = @((AllNames $win) | Where-Object { $_ -match '^This device, [0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}$' })
  Check ($thisRow.Count -eq 1) 'account: Settings names this device by its device ID'
  Check ((AllNames $win).Contains('Linked devices, 1')) 'account: "Linked devices" is counted from the identity''s list (1)'
  NamesNoDemo $win 'account'
  JudgeCapture 'account' (Shot $win 'account') $false
  DumpNames $win 'account'

  # ---- 7. the store, and the log ----------------------------------------------------------------
  Check (Test-Path -LiteralPath (Join-Path $root 'live\identity-slice1\state')) 'store: the state is in live\identity-slice1'
  Check (-not (Test-Path -LiteralPath (Join-Path $root 'live\state'))) 'store: nothing was made in build 03''s live\state'
  $log = LogText
  Check (-not (HoldsWords $log)) 'log: the app''s log holds none of the words'
  [IO.File]::WriteAllText((Join-Path $Out "$Variant-app.log"), $log, (New-Object Text.UTF8Encoding($false)))
} catch {
  Check $false "the walk stopped: $($_.Exception.Message)"
} finally {
  try {
    $w2 = AppWindow $p.Id 3
    if ($null -ne $w2) { $w2.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close() }
  } catch { Note "  close: $_" }
  if (-not $p.WaitForExit(15000)) { try { $p.Kill() } catch { } }
}

# ---- 8. NOTHING THAT LEAVES THIS RUNNER HOLDS THE WORDS --------------------------------------------
$leaks = @()
foreach ($f in @(Get-ChildItem -LiteralPath $Out -Recurse -File | Where-Object { $_.Extension -ne '.png' })) {
  if (HoldsWords ([IO.File]::ReadAllText($f.FullName))) { $leaks += $f.Name; Remove-Item -LiteralPath $f.FullName -Force }
}
Check ($leaks.Count -eq 0) "artifact: no file this run wrote holds the words ($($leaks.Count) removed)"
$script:words = $null

Note "---- $Variant done: $($script:failures) FAIL"
foreach ($f in $script:failed) { Note "  failed: $f" }
WriteFailed
if ($script:failures -gt 0) { exit 1 }
exit 0
