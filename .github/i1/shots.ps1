# THROWAWAY, NEVER MERGED (message-windows ci/i1-shots* on the owner's fork). Runs on a GitHub
# runner ONLY: it launches URmessage.exe, which nothing may do on the owner's machine.
#
# Identity phase 1, Task 6 (slice 0): every identity screen over the fabricated world, opened with
# --demo-identity=<screen>, read with UI Automation and captured with PrintWindow
# (PW_RENDERFULLCONTENT) of the app's own window. Then two flows driven with UI Automation's Invoke
# and Toggle patterns only: the first-device setup end to end, and the Remove dialog's phrase field
# coming and going. No mouse or keyboard input is synthesized.
#
# PROPERTY 2: a screen that shows or takes the words captures BLACK (99.5% of its pixels exactly
# black or more), and every other screen does not (under 50%). ci/i1-affinity-probe measured both
# shapes on both runner images: 100% and one colour excluded, 2.8% not.
#
# AND IT FAILS CLOSED (the review's should-fix 2): the 'refused' leg opens the words screen with
# --demo-capture=refused, which makes the window's ask for the exclusion fail, and the window must
# draw the refusal and no word.
#
# THE REVIEW'S BLOCKER: the 'plain-settings' leg starts the STANDARD build with no argument at all
# (a plain launch is live; with no SDK dll on the runner its mesh never answers, so ActiveRunMode()
# stays Fabricated for the whole run) and opens Settings with SelectionItemPattern: the inert
# ACCOUNT group must be there, and none of the made-up account.
#
# Run with Windows PowerShell 5.1 (UI Automation is a .NET Framework assembly).
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Variant,
  [string]$Only = '',
  [string]$Skip = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class I1Shot {
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
[void][I1Shot]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:failures = 0
$script:failed = New-Object System.Collections.Generic.List[string]
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++; $script:failed.Add($what) }
}

New-Item -ItemType Directory -Force $Out | Out-Null
$dot = [string][char]0xB7
Note "screen: $([I1Shot]::GetSystemMetrics(0)) x $([I1Shot]::GetSystemMetrics(1)); variant $Variant"

# ---- what each screen must show, typed here again on purpose (Demo/IdentityDemo.cpp, RunMode.cpp) --
$banner = 'Demo model: made-up data. Nothing on this screen is a real account, device or phrase.'
$wordsLabel = 'Demo words: not a recovery phrase'
$checkLabel = 'Demo check: the words are checked against the demo list, and nothing else.'
$phraseLabel = 'Demo phrase: type the demo words. Nothing is checked against an account.'
$caveat = 'Demo model: these words bring nothing back. A real phrase cannot bring your account back yet, either.'
$legs = @(
  @{ name = 'setup'; black = $false; must = @('Set up this device', $banner, 'Already use URmessage on another computer?',
      'Demo code, made up: no computer can use it. A real code works for 10 minutes.', 'Code: 7KQ4 M2XD 9PRF',
      'Demo only: go on as if the code was typed', 'This is my first device', 'Demo only: skip to the app') },
  @{ name = 'words'; black = $true; must = @('Write down these 25 words', $banner, $wordsLabel, 'Word 1: lighthouse',
      'Word 7: chocolate', 'Word 25: watermelon', $caveat, 'I wrote them down') },
  @{ name = 'typeback'; black = $true; must = @('Type the 25 words back', $banner, $checkLabel, 'Word 1', 'Word 25',
      'Demo only: fill in the demo words', 'Check the words') },
  @{ name = 'typeback-wrong'; black = $true; must = @('Type the 25 words back', $checkLabel,
      'Demo check: word 7 is not the word you were shown.') },
  @{ name = 'setup-done'; black = $false; must = @('Demo model: this device would be set up now', $banner,
      'See Your ID in Settings', 'Open the app') },
  @{ name = 'emoji-new'; black = $false; must = @('Compare the emoji', $banner,
      'Check that your other computer shows these seven emoji, in this order.',
      'Demo emoji, made up: no session ran, and no other computer shows them.', 'Emoji 1: Dog', 'Emoji 7: Anchor',
      'They match', "They don't match") },
  @{ name = 'add'; black = $false; must = @('Add a device', $banner, 'Code', 'Continue', 'Cancel',
      'Demo model: no code is checked here. Any code goes on to the emoji.') },
  @{ name = 'emoji-existing'; black = $false; must = @('Compare the emoji',
      'Check that the new computer shows these seven emoji, in this order.', 'Emoji 4: Pizza', 'They match') },
  @{ name = 'granted'; black = $false; must = @('Demo model: the new device would be added now', 'Done') },
  @{ name = 'ending-timeout'; black = $false; must = @('Demo ending: the code timed out', 'Try again', 'Demo only: see the other endings') },
  @{ name = 'ending-cancelled'; black = $false; must = @('Demo ending: adding the device was cancelled') },
  @{ name = 'ending-nomatch'; black = $false; must = @('Demo ending: the emoji did not match') },
  @{ name = 'ending-full'; black = $false; must = @('Demo ending: remove a device first',
      'Demo model: an account holds at most 10 devices. Remove one in Settings, Account, then add this one.') },
  @{ name = 'ending-phrase'; black = $false; must = @('Demo ending: this device can add no more until Linux laptop is opened') },
  @{ name = 'remove'; black = $false; must = @('Remove devices', $banner, 'Demo devices, made up',
      "Windows desktop, Demo device ID 5c1e 9a07 d3b2 4f68 $dot this device $dot phrase device",
      'Pixel 9, Demo device ID a20f 63d9 18c4 7be1', "Linux laptop, Demo device ID e7b4 0c5a 92f1 3d86 $dot phrase device",
      'Remove', 'Cancel'); mustNot = @($phraseLabel) },
  @{ name = 'remove-phrase'; black = $true; must = @('Remove devices', $phraseLabel,
      "Demo model: Linux laptop keeps your phrase's key, so removing it is a lock-out. Type your phrase, and choose what happens to each other device.",
      'Keep Pixel 9', 'Remove Pixel 9', 'Lock out and remove') },
  @{ name = 'remove-only'; black = $false; must = @('Remove this device',
      'Demo model: on an only device, this would end this identity.', 'End this identity');
      mustNot = @('Pixel 9, Demo device ID a20f 63d9 18c4 7be1') },
  @{ name = 'lockout'; black = $true; must = @('Lock out a phrase device', $phraseLabel, 'Lock out') },
  @{ name = 'removed'; black = $false; must = @('Demo screen: this device is no longer on your account',
      'Demo model, made up: removed on 14 Oct 2026 by Linux laptop.', 'Add this device again', 'If this was not you') },
  @{ name = 'notice-added'; black = $false; must = @('A change to your devices',
      'Demo notice, made up: Pixel 9 was added on 5 Oct 2026 by Windows desktop.', 'Remove Pixel 9', 'Dismiss') },
  @{ name = 'notice-removed'; black = $false; must = @('Demo notice, made up: Old laptop was removed on 11 Oct 2026 by Windows desktop.') },
  @{ name = 'notice-selfremoved'; black = $false; must = @(
      "Demo notice, made up: Old laptop held your recovery phrase's key and removed itself. If this was not you, lock it out.",
      'Lock it out') },
  @{ name = 'alarm-list'; black = $false; must = @('Demo alarm: a device list this device cannot explain',
      'Check my devices', 'Lock out a device', 'Change my recovery phrase') },
  @{ name = 'alarm-key'; black = $false; must = @('Demo alarm: this device''s key signed a change that this device did not make',
      'Check my devices', 'Lock out a device'); mustNot = @('Change my recovery phrase') },
  @{ name = 'alarm-rival'; black = $false; must = @('Demo alarm: a phrase you replaced was used',
      'Demo model: changing your phrase again cannot undo this. To start over with a new ID, remove each device, end the account on the last one, and set up again.');
      mustNot = @('Change my recovery phrase') },
  @{ name = 'rename'; black = $false; must = @('Rename Pixel 9', 'Device name',
      'Demo name: kept until the app closes, and no other device sees it.', 'Save') },
  @{ name = 'make-phrase'; black = $true; must = @('Make this a phrase device', $phraseLabel, 'Make it a phrase device') },
  @{ name = 'change-old'; black = $true; must = @('Change my recovery phrase', $checkLabel, 'Continue') },
  @{ name = 'change-new'; black = $true; must = @('Write down your new 25 words', $wordsLabel, 'Word 1: afternoon', 'Word 25: yesterday', $caveat) },
  @{ name = 'change-typeback'; black = $true; must = @('Type the new words back', $checkLabel, 'Check the words') },
  @{ name = 'change-done'; black = $false; must = @('Demo model: the phrase would be changed now',
      "Demo model: no phrase was changed. After a real change, your ID and your devices are the same, and Linux laptop no longer keeps your phrase's key: you would type the new phrase there to make it a phrase device again.") },
  @{ name = 'account'; black = $false; must = @('DEMO MODEL: ACCOUNT', 'Your ID', 'Demo model: a made-up ID, not an account.',
      'Your ID, Demo ID 3b9e2f4c-1a7d-8e05-9c62-4f1d0a8b7e33', 'Copy Your ID', 'Change my recovery phrase', 'DEMO MODEL: DEVICES',
      'Rename Windows desktop', 'Remove Pixel 9', 'Add a device', 'DEMO ONLY: MORE SCREENS', 'Demo only: the removed screen') }
)
function Want([string]$name) {
  if ($Only -ne '' -and -not (($Only -split ',') -contains $name)) { return $false }
  if ($Skip -ne '' -and (($Skip -split ',') -contains $name)) { return $false }
  return $true
}
$legs = @($legs | Where-Object { Want $_.name })
Note "legs: $(@($legs | ForEach-Object { $_.name }) -join ', '); only '$Only'; skip '$Skip'"

# ---- the window -----------------------------------------------------------------------------
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
function FindName($win, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
  return $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
function WaitName($win, [string]$name, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $e = FindName $win $name
    if ($null -ne $e) { return $e }
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
  $r = New-Object I1Shot+RECT
  [void][I1Shot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [I1Shot]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $stats = [I1Shot]::Stats($bmp)
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
function NoteLog([string]$root, [string]$label) {
  $lines = @((LogText $root) -split "`n" | Where-Object { $_ -match 'identity: ' })
  foreach ($l in $lines) { Note "  log ($label): $($l.Trim())" }
}
function Launch([string]$arguments, [string]$label) {
  $root = Join-Path $env:RUNNER_TEMP ('i1-' + $label + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  New-Item -ItemType Directory -Force $root | Out-Null
  $psi = [System.Diagnostics.ProcessStartInfo]::new($Exe)
  $psi.Arguments = $arguments
  $psi.UseShellExecute = $false
  $psi.EnvironmentVariables['URMESSAGE_APP_ROOT'] = $root
  $p = [System.Diagnostics.Process]::Start($psi)
  return @{ process = $p; root = $root }
}
function Close($run) {
  try {
    $win = AppWindow $run.process.Id 3
    if ($null -ne $win) {
      $wp = $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
      $wp.Close()
    }
  } catch { Note "  close: $_" }
  if (-not $run.process.WaitForExit(15000)) { try { $run.process.Kill() } catch { } }
}
function SaveNames($names, [string]$file) {
  [IO.File]::WriteAllLines($file, [string[]]$names, (New-Object Text.UTF8Encoding($false)))
}
function InvokeNamed($win, [string]$name) {
  $e = WaitName $win $name 20
  if ($null -eq $e) { Check $false "found '$name' to invoke"; return $false }
  $ip = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
  $ip.Invoke()
  Note "  invoked '$name'"
  return $true
}
function ToggleNamed($win, [string]$name) {
  $e = WaitName $win $name 20
  if ($null -eq $e) { Check $false "found '$name' to toggle"; return $false }
  $tp = $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
  $tp.Toggle()
  Note "  toggled '$name'"
  return $true
}

# ---- one launch per screen --------------------------------------------------------------------
foreach ($leg in $legs) {
  Note "---- $($leg.name)"
  $run = Launch "--demo-identity=$($leg.name)" $leg.name
  try {
    $win = AppWindow $run.process.Id 60
    if ($null -eq $win) { Check $false "$($leg.name): a window"; continue }
    $first = WaitName $win $leg.must[0] 45
    Check ($null -ne $first) "$($leg.name): '$($leg.must[0])' is on screen"
    Start-Sleep -Seconds 3
    $names = AllNames $win
    SaveNames $names (Join-Path $Out "$Variant-$($leg.name).names.txt")
    foreach ($m in $leg.must) { Check ($names.Contains($m)) "$($leg.name): shows '$m'" }
    if ($leg.ContainsKey('mustNot')) {
      foreach ($m in $leg.mustNot) { Check (-not $names.Contains($m)) "$($leg.name): does not show '$m'" }
    }
    $shot = Shot $win (Join-Path $Out "$Variant-$($leg.name).png")
    JudgeCapture "$($leg.name)" $shot $leg.black
    NoteLog $run.root $leg.name
  } finally {
    Close $run
  }
}

# ---- flow 1: the first device, end to end, by Invoke ------------------------------------------
if (Want 'flow-setup') {
  Note '---- flow-setup'
  $run = Launch '--demo-identity=setup' 'flow-setup'
  try {
    $win = AppWindow $run.process.Id 60
    if ($null -ne (WaitName $win 'This is my first device' 45)) {
      Start-Sleep -Seconds 2
      JudgeCapture 'flow-setup step 1, setup' (Shot $win (Join-Path $Out "$Variant-flow-1-setup.png")) $false
      [void](InvokeNamed $win 'This is my first device')
      Check ($null -ne (WaitName $win 'Write down these 25 words' 20)) 'flow-setup step 2: the words screen opened'
      Start-Sleep -Seconds 2
      JudgeCapture 'flow-setup step 2, words' (Shot $win (Join-Path $Out "$Variant-flow-2-words.png")) $true
      [void](InvokeNamed $win 'I wrote them down')
      Check ($null -ne (WaitName $win 'Type the 25 words back' 20)) 'flow-setup step 3: the typed-back screen opened'
      [void](InvokeNamed $win 'Check the words')
      Check ($null -ne (WaitName $win 'Demo check: word 1 is empty.' 20)) 'flow-setup step 3: an empty word 1 is named'
      Start-Sleep -Seconds 1
      JudgeCapture 'flow-setup step 3, typeback' (Shot $win (Join-Path $Out "$Variant-flow-3-typeback.png")) $true
      [void](InvokeNamed $win 'Demo only: fill in the demo words')
      [void](InvokeNamed $win 'Check the words')
      Check ($null -ne (WaitName $win 'Demo model: this device would be set up now' 20)) 'flow-setup step 4: all 25 matched, and setup is done'
      Start-Sleep -Seconds 2
      JudgeCapture 'flow-setup step 4, setup-done' (Shot $win (Join-Path $Out "$Variant-flow-4-done.png")) $false
      [void](InvokeNamed $win 'See Your ID in Settings')
      Check ($null -ne (WaitName $win 'DEMO MODEL: ACCOUNT' 20)) 'flow-setup step 5: Settings opened on its Account group'
      Start-Sleep -Seconds 2
      JudgeCapture 'flow-setup step 5, account' (Shot $win (Join-Path $Out "$Variant-flow-5-account.png")) $false
      SaveNames (AllNames $win) (Join-Path $Out "$Variant-flow-5-account.names.txt")
    } else {
      Check $false 'flow-setup: the setup screen'
    }
    NoteLog $run.root 'flow-setup'
  } finally {
    Close $run
  }
}

# ---- flow 2: the Remove dialog's phrase field, by Toggle ---------------------------------------
if (Want 'flow-remove') {
  Note '---- flow-remove'
  $run = Launch '--demo-identity=remove' 'flow-remove'
  $laptop = "Linux laptop, Demo device ID e7b4 0c5a 92f1 3d86 $dot phrase device"
  try {
    $win = AppWindow $run.process.Id 60
    if ($null -ne (WaitName $win 'Remove devices' 45)) {
      Start-Sleep -Seconds 2
      JudgeCapture 'flow-remove step 1, Pixel 9 chosen' (Shot $win (Join-Path $Out "$Variant-flow-remove-1.png")) $false
      [void](ToggleNamed $win $laptop)
      Check ($null -ne (WaitName $win $phraseLabel 20)) 'flow-remove step 2: choosing a phrase device asks for the phrase'
      Start-Sleep -Seconds 2
      JudgeCapture 'flow-remove step 2, the phrase field' (Shot $win (Join-Path $Out "$Variant-flow-remove-2.png")) $true
      [void](ToggleNamed $win $laptop)
      Start-Sleep -Seconds 2
      Check ($null -eq (FindName $win $phraseLabel)) 'flow-remove step 3: the phrase field went with the phrase device'
      JudgeCapture 'flow-remove step 3, back to no phrase' (Shot $win (Join-Path $Out "$Variant-flow-remove-3.png")) $false
    } else {
      Check $false 'flow-remove: the Remove dialog'
    }
    NoteLog $run.root 'flow-remove'
  } finally {
    Close $run
  }
}

# ---- the refusal: the words screen with the window's ask made to fail --------------------------
$refusal = 'This window could not be kept out of screen captures, so the words are not shown.'
if (Want 'refused') {
  Note '---- refused'
  $run = Launch '--demo-identity=words --demo-capture=refused' 'refused'
  try {
    $win = AppWindow $run.process.Id 60
    if ($null -eq $win) { Check $false 'refused: a window' } else {
      Check ($null -ne (WaitName $win 'Write down these 25 words' 45)) "refused: the screen's title, 'Write down these 25 words', is on screen"
      Start-Sleep -Seconds 3
      $names = AllNames $win
      SaveNames $names (Join-Path $Out "$Variant-refused.names.txt")
      Check ($names.Contains($banner)) 'refused: shows the demo banner'
      Check ($names.Contains($refusal)) "refused: shows the refusal '$refusal'"
      foreach ($m in @('Word 1: lighthouse', 'Word 25: watermelon', $wordsLabel)) {
        Check (-not $names.Contains($m)) "refused: does not show '$m'"
      }
      JudgeCapture 'refused' (Shot $win (Join-Path $Out "$Variant-refused.png")) $false
      $log = LogText $run.root
      Check ($log.Contains('identity: capture exclusion ON for words REFUSED by --demo-capture=refused')) 'refused: the log says the ask was refused'
    }
    NoteLog $run.root 'refused'
  } finally {
    Close $run
  }
}

# ---- the review's blocker: a plain launch of the standard build, and its Settings ---------------
function SelectNav($win, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::NameProperty, $name)
  $deadline = (Get-Date).AddSeconds(30)
  while ((Get-Date) -lt $deadline) {
    foreach ($e in @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond))) {
      try { $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); Note "  selected '$name' (SelectionItemPattern)"; return $true } catch { }
      try { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Note "  invoked '$name' (InvokePattern)"; return $true } catch { }
    }
    Start-Sleep -Seconds 1
  }
  return $false
}
if (Want 'plain-settings') {
  Note '---- plain-settings'
  $run = Launch '' 'plain-settings'
  try {
    $win = AppWindow $run.process.Id 60
    if ($null -eq $win) { Check $false 'plain-settings: a window' } else {
      Start-Sleep -Seconds 5
      Check (SelectNav $win 'Settings') "plain-settings: Settings is selected with UI Automation"
      Check ($null -ne (WaitName $win 'SETTINGS' 20)) 'plain-settings: the Settings page is up'
      Start-Sleep -Seconds 3
      $names = AllNames $win
      SaveNames $names (Join-Path $Out "$Variant-plain-settings.names.txt")
      Check ($names.Contains('ACCOUNT')) "plain-settings: Settings keeps its inert ACCOUNT group"
      foreach ($m in @('DEMO MODEL: ACCOUNT', 'DEMO ONLY: MORE SCREENS', 'DEMO MODEL: DEVICES', 'Copy Your ID')) {
        Check (-not $names.Contains($m)) "plain-settings: does not show '$m'"
      }
      Check (-not $names.Contains('DEMO')) 'plain-settings: the title bar carries no DEMO chip'
      JudgeCapture 'plain-settings' (Shot $win (Join-Path $Out "$Variant-plain-settings.png")) $false
      $log = LogText $run.root
      $sdk = $log.Contains('URnetworkSdk.dll could not be loaded')
      $live = $log.Contains('live: ==== live mesh session starting ====')
      Note "  the plain launch was live: $live; its mesh could not answer (no SDK dll here): $sdk"
      Check ($live) 'plain-settings: the plain launch is live (its session started)'
    }
    NoteLog $run.root 'plain-settings'
  } finally {
    Close $run
  }
}

Note "---- $Variant done: $($script:failures) FAIL"
foreach ($f in $script:failed) { Note "  failed: $f" }
[IO.File]::WriteAllLines((Join-Path $Out "$Variant-failed.txt"), [string[]]$script:failed, (New-Object Text.UTF8Encoding($false)))
if ($script:failures -gt 0) { exit 1 }
exit 0
