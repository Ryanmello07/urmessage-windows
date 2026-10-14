# THROWAWAY, NEVER MERGED (message-windows ci/task58-shots on the owner's fork). Runs on a GitHub
# runner ONLY: it launches URmessage.exe, which nothing may do on the owner's machine.
#
# Hardening plan Task 58, Steps 1 and 3: every sentence the app shows for a refusal of the join
# seal's library, screenshotted from the running app built on the T1 branch's head. One leg per run:
#   joiner     the join road, one invitation per row: an older URmessage's invitation (version 2),
#              a group the older build founded (under a v3 envelope), an invitation made for another
#              device, one with a bit of its authenticator flipped, one whose join cannot write the
#              group's record (so it costs the join code), and that same invitation again
#   adder      the add road: a damaged join code, an older URmessage's join code, an honest one
#              (the adder's copy for the invitation), and a second join code of the same device
#   held       R20: the held-group refusal of Task 39, with the dialog's intro
#   nowrapkey  a device whose identity record holds no X-Wing seed (the three part record a store
#              written before the seed was kept holds), at startup
#   notreadnow (R25 Q5, the T1 fix's F1) a group whose record could not be read just now, on the
#              restore, join and leave roads: the record's read is denied on the runner before the
#              app starts, as the red team held it, and given back between the roads
#
# R25's follow-up (ci/task58-shots-v3): the F5 sentence (adder), "Make a new join code" before and
# after a press (joiner), and the Q4 intro and Waiting text under the not-read row (notreadnow).
#
# DRIVING: UI Automation patterns only (Invoke, and Value to put a code in a box). No mouse or
# keyboard input is synthesized. CAPTURE: PrintWindow(PW_RENDERFULLCONTENT) of the app's own window,
# found by its process id.
param(
  [Parameter(Mandatory = $true)][ValidateSet('joiner', 'adder', 'held', 'nowrapkey', 'notreadnow')][string]$Leg,
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Rig,
  [Parameter(Mandatory = $true)][string]$RigWorkDir,
  [string]$OldRig = '',
  [string]$OldRigWorkDir = '',
  [Parameter(Mandatory = $true)][string]$Work,
  [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Task58Shot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][Task58Shot]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:failures = 0
$script:notReached = @()
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++ }
}

# ---- the sentences, as Live/RefusalCopy.h writes them (typed again here, on purpose) -------------
$say = @{
  JoinRefused        = 'This invitation was refused. Your join code is still good: ask for the invitation to be sent again another way.'
  NoJoinCode         = 'This invitation cannot be used with any join code this device can read. Make a new join code and ask to be invited again.'
  Spent              = 'This join did not complete. Make a new join code and ask to be added again.'
  InviteVersion      = 'This invitation was made by an older URmessage. Ask them to update and invite you again.'
  Predates           = 'This group was started before invitations were sealed. Leave it and start it again.'
  CannotOpen         = 'This join code was made by an older URmessage. Ask them for a new one from an updated app.'
  Invalid            = 'This is not a valid join code. Ask them to send it again.'
  HasALeaf           = 'This join code claims to be someone already in this group. Remove their entry only if they tell you they never opened their invitation, then add this join code.'
  NotReadNow         = 'This conversation could not be read just now. Close anything else that may be using URmessage''s folder, then try again.'
  NewJoinCode        = 'This is a new join code. Send it to whoever is inviting you, and paste the invitation they send back.'
  NewJoinCodeButton  = 'Make a new join code'
  SendJoinCode       = 'Send your join code to whoever is setting the group up, and paste the invitation they send back.'
  NotInAGroup        = 'This device is not in a group yet.'
  NoWrapKey          = 'This installation cannot make a join code, so it cannot join a group. Its saved data was made by an older URmessage.'
  AdderLabel         = 'Only their device can open it. It works once. Send it privately.'
  HeldIntro          = 'This device is connected.'
  JoinIntro          = 'This device is connected but is not in a group yet. A group is opened by somebody who is already in one: send them the join code below, and paste the invitation they send back.'
  HeldNotLive        = 'This device still holds an old copy of this group that it cannot open. Leave it, and then use this invitation again.'
}

New-Item -ItemType Directory -Force $Work, $Out | Out-Null
$root = Join-Path $Work 'app'
$rigDir = Join-Path $Work 'rig'
$handshake = Join-Path $root 'dev'
New-Item -ItemType Directory -Force $root, $rigDir, $handshake | Out-Null
$cred = Join-Path $Work 'ci-not-a-credential.jwt'
Set-Content -LiteralPath $cred -Value 'ci-not-a-credential (the DIRECT route reads no credential)' -Encoding ascii
$who = "$($env:USERDOMAIN)\$($env:USERNAME)"

# ---- the rigs: the server and the parties, and the older build ----------------------------------
$env:TASK58_RIG_DIR = $rigDir
$env:TASK58_HANDSHAKE_DIR = $handshake
$env:TASK58_LEG = $Leg
$rigProc = Start-Process -FilePath $Rig -ArgumentList '-test.run', '^TestTask58Rig$', '-test.v', '-test.timeout', '0' `
  -WorkingDirectory $RigWorkDir -RedirectStandardOutput (Join-Path $Out 'rig.out.txt') `
  -RedirectStandardError (Join-Path $Out 'rig.err.txt') -PassThru -NoNewWindow
$oldProc = $null
if ($OldRig -ne '') {
  $oldProc = Start-Process -FilePath $OldRig -ArgumentList '-test.run', '^TestTask58OldRig$', '-test.v', '-test.timeout', '0' `
    -WorkingDirectory $OldRigWorkDir -RedirectStandardOutput (Join-Path $Out 'oldrig.out.txt') `
    -RedirectStandardError (Join-Path $Out 'oldrig.err.txt') -PassThru -NoNewWindow
}
function WaitFile([string]$name, [int]$seconds) {
  $path = Join-Path $rigDir $name
  $deadline = (Get-Date).AddSeconds($seconds)
  while (-not (Test-Path -LiteralPath $path)) {
    if ((Get-Date) -gt $deadline) { Note "TIMED OUT waiting $seconds s for $name"; return $false }
    Start-Sleep -Milliseconds 500
  }
  return $true
}
if (-not (WaitFile 'endpoint.txt' 180)) { throw "the rig did not publish its endpoint (exited: $($rigProc.HasExited))" }
$endpointLines = @(Get-Content -LiteralPath (Join-Path $rigDir 'endpoint.txt') | Where-Object { $_ -ne '' })
$endpoint = $endpointLines[0]; $pin = $endpointLines[1]
Note "rig serving $endpoint, pin $pin"

# ---- the app ------------------------------------------------------------------------------------
$env:URMESSAGE_APP_ROOT = $root
$env:URMESSAGE_LIVE_JWT = $cred
$env:URMESSAGE_ROUTE = 'direct'
$env:URMESSAGE_CI_ENDPOINT = $endpoint
$env:URMESSAGE_CI_PIN = $pin

function LogText {
  $files = @(Get-ChildItem -LiteralPath (Join-Path $root 'logs') -Filter *.log -ErrorAction SilentlyContinue)
  $text = ''
  foreach ($f in $files) {
    try {
      $fs = [IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite')
      $sr = New-Object IO.StreamReader($fs)
      $text += $sr.ReadToEnd()
      $sr.Close()
    } catch { }
  }
  return $text
}
function CountIn([string]$text, [string]$needle) {
  $n = 0; $at = 0
  while (($at = $text.IndexOf($needle, $at, [StringComparison]::Ordinal)) -ge 0) { $n++; $at += $needle.Length }
  return $n
}
function WaitLog([string]$needle, [int]$count, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    if ((CountIn (LogText) $needle) -ge $count) { Note "log: '$needle' x$count"; return $true }
    Start-Sleep -Milliseconds 500
  }
  Note "log: TIMED OUT waiting $seconds s for '$needle' x$count"
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
  throw "no window for pid $processId"
}
function Shot([int]$processId, [string]$name) {
  $win = AppWindow $processId
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $r = New-Object Task58Shot+RECT
  [void][Task58Shot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [Task58Shot]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc); $g.Dispose()
  $path = Join-Path $Out "$Leg-$name.png"
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Note "SHOT $path ok=$ok ${w}x${h}"
}
function FindByName([int]$processId, [string]$name) {
  $win = AppWindow $processId
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
function DumpNames([int]$processId, [string]$file) {
  $win = AppWindow $processId
  $all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  $lines = foreach ($e in $all) {
    try { "{0}`t{1}" -f $e.Current.ControlType.ProgrammaticName, $e.Current.Name } catch { }
  }
  $lines | Set-Content -LiteralPath (Join-Path $Out "$Leg-$file.txt") -Encoding utf8
}
function Invoke-Named([int]$processId, [string]$name) {
  $e = WaitName $processId $name 30
  if ($null -eq $e) { Check $false "the window carries '$name' to invoke"; return $false }
  $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  Note "invoked '$name' (InvokePattern)"
  return $true
}
function Set-Named([int]$processId, [string]$name, [string]$value) {
  $e = WaitName $processId $name 30
  if ($null -eq $e) { Check $false "the window carries '$name' to fill"; return $false }
  $e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($value)
  Note "set '$name' to $($value.Length) characters (ValuePattern)"
  return $true
}
function ReadValue([int]$processId, [string]$name) {
  $e = WaitName $processId $name 30
  if ($null -eq $e) { Check $false "the window carries '$name' to read"; return '' }
  return $e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
}
function Sha([string]$text) {
  $h = [Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::ASCII.GetBytes($text))
  return (($h | ForEach-Object { $_.ToString('x2') }) -join '').Substring(0, 16)
}
function GroupDirOf([string]$gidHex) {
  $gid = New-Object byte[] 32
  for ($i = 0; $i -lt 32; $i++) { $gid[$i] = [Convert]::ToByte($gidHex.Substring($i * 2, 2), 16) }
  $sha = [Security.Cryptography.SHA256]::Create()
  $name = (($sha.ComputeHash($gid) | ForEach-Object { $_.ToString('x2') }) -join '')
  return Join-Path (Join-Path (DataDir) 'group') $name
}
function B64([string]$name) { return [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $rigDir $name))) }
function StartApp {
  $p = Start-Process -FilePath $Exe -ArgumentList '--live' -PassThru
  Note "app started, pid $($p.Id)"
  return $p
}
function CloseApp($p) {
  try {
    $win = AppWindow $p.Id 10
    $pattern = $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
    $pattern.Close()
    Note "asked the window to close (WindowPattern.Close)"
  } catch { Note "WindowPattern.Close failed: $_" }
  if (-not $p.WaitForExit(30000)) {
    Note "the app did not exit within 30 s of Close; stopping the process"
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit(10000) | Out-Null
  }
  Note "app exited"
}
# One row of the table on screen: wait for the worker's log line, then the sentence in the window,
# then the picture, then the UIA names beside it.
function Row([int]$processId, [string]$shot, [string]$logNeedle, [int]$count, [string]$sentence, [string[]]$absent = @()) {
  Check (WaitLog $logNeedle $count 120) "$shot`: the worker logged '$logNeedle' (x$count)"
  $text = WaitName $processId $sentence 30
  Check ($null -ne $text) "$shot`: the window carries the sentence, verbatim: $sentence"
  Start-Sleep -Seconds 2
  Shot $processId $shot
  DumpNames $processId "$shot-uia"
  foreach ($not in $absent) {
    Check ($null -eq (FindByName $processId $not)) "$shot`: the window does NOT carry: $not"
  }
}
function DataDir {
  $device = Get-ChildItem -LiteralPath (Join-Path $root 'live\state') -Recurse -File -Filter 'device' -ErrorAction SilentlyContinue |
    Where-Object { $_.Length -gt 12 -and [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($_.FullName), 0, 8) -eq 'URMSTATE' } |
    Select-Object -First 1
  if ($null -eq $device) { throw 'no device identity record under live\state' }
  return $device.Directory.FullName
}

$deniedDir = $null
$script:deniedMeta = $null
try {
  switch ($Leg) {
    'joiner' {
      $app = StartApp
      Check (WaitLog 'published this device''s key package' 1 240) 'A: the app published its join code and waits'
      $kp = Join-Path $handshake 'app.keypackage'
      Copy-Item -LiteralPath $kp -Destination (Join-Path $rigDir 'app.kp.partial')
      Move-Item -LiteralPath (Join-Path $rigDir 'app.kp.partial') -Destination (Join-Path $rigDir 'app.kp')
      Note 'A: the app''s join code is handed to both rigs'
      Check (WaitFile 'joiner-ready' 240) 'A: the rig wrote the invitations'
      Start-Sleep -Seconds 4
      Shot $app.Id 'A-waiting'
      DumpNames $app.Id 'A-uia'
      Check ($null -ne (FindByName $app.Id $say.JoinIntro)) 'A: the join road''s intro is unchanged'

      # 1. an older URmessage's invitation
      if (Test-Path -LiteralPath (Join-Path $rigDir 'old-v2.bin')) {
        $v2 = [IO.File]::ReadAllBytes((Join-Path $rigDir 'old-v2.bin'))
        Note "1: the older build's invitation is at version 0x$('{0:x2}{1:x2}' -f $v2[0], $v2[1])"
        Set-Named $app.Id 'Paste the invitation here' (B64 'old-v2.bin') | Out-Null
        Invoke-Named $app.Id 'Join' | Out-Null
        Row $app.Id '1-invite-version' 'parse_invite refused it (ErrInviteVersion)' 1 $say.InviteVersion
      } else {
        Check $false '1: the older build wrote no invitation (see old-refused.txt and oldrig.out.txt)'
        $script:notReached += 'ErrInviteVersion'
      }

      # 2. a group the older build founded, under a v3 envelope
      if (Test-Path -LiteralPath (Join-Path $rigDir 'predates.bin')) {
        Set-Named $app.Id 'Paste the invitation here' (B64 'predates.bin') | Out-Null
        Invoke-Named $app.Id 'Join' | Out-Null
        Row $app.Id '2-predates' 'device_join refused (ErrGroupPredatesJoinSeal)' 1 $say.Predates @($say.JoinRefused)
      } else {
        Check $false '2: the rig wrote no predates invitation'
        $script:notReached += 'ErrGroupPredatesJoinSeal'
      }

      # 3. an invitation made for another device
      Set-Named $app.Id 'Paste the invitation here' (B64 'other.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id '3-no-join-code-other-device' 'device_join refused (ErrNoJoinCodeForInvite)' 1 $say.NoJoinCode @($say.JoinRefused)
      Check ($null -ne (FindByName $app.Id $say.NewJoinCodeButton)) "3: the row offers '$($say.NewJoinCodeButton)'"

      # 4. one bit of the authenticator flipped: refused inside the window, the join code goes back
      Set-Named $app.Id 'Paste the invitation here' (B64 'flipped.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id '4-join-refused' 'device_join refused (ErrJoinRefused)' 1 $say.JoinRefused @($say.NewJoinCodeButton)

      # 5. a join whose group record cannot be written: the group's directory takes no new file
      #    (its epoch subdirectory still can), so the joined state commits and the record does not
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'spent-gid.txt') -Raw).Trim()
      $gid = New-Object byte[] 32
      for ($i = 0; $i -lt 32; $i++) { $gid[$i] = [Convert]::ToByte($gidHex.Substring($i * 2, 2), 16) }
      $sha = [Security.Cryptography.SHA256]::Create()
      $name = (($sha.ComputeHash($gid) | ForEach-Object { $_.ToString('x2') }) -join '')
      $deniedDir = Join-Path (Join-Path (DataDir) 'group') $name
      New-Item -ItemType Directory -Force $deniedDir | Out-Null
      & icacls $deniedDir /deny "${who}:(WD)" | ForEach-Object { Note "icacls: $_" }
      $probe = Join-Path $deniedDir 'probe'
      $created = $true
      try { [IO.File]::WriteAllText($probe, 'x') } catch { $created = $false }
      Check (-not $created) "5: $deniedDir takes no new file"
      $sub = Join-Path $deniedDir 'probe-dir'
      $made = $true
      try { [void][IO.Directory]::CreateDirectory($sub); [IO.File]::WriteAllText((Join-Path $sub 'x'), 'x') } catch { $made = $false }
      Check $made '5: a subdirectory of it can be made and written'
      if ($made) { Remove-Item -LiteralPath $sub -Recurse -Force }
      Set-Named $app.Id 'Paste the invitation here' (B64 'spent.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id '5-join-code-spent' 'device_join refused (ErrJoinCodeSpent)' 1 $say.Spent @($say.JoinRefused)
      Check ($null -ne (FindByName $app.Id $say.NewJoinCodeButton)) "5: the row offers '$($say.NewJoinCodeButton)'"
      & icacls $deniedDir /remove:d $who | ForEach-Object { Note "icacls: $_" }
      $deniedDir = $null

      # 6. the same invitation again: its join code is gone (R13)
      Set-Named $app.Id 'Paste the invitation here' (B64 'spent.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id '6-no-join-code-gone' 'device_join refused (ErrNoJoinCodeForInvite)' 2 $say.NoJoinCode @($say.JoinRefused)
      $log = LogText
      Check ((CountIn $log '*** JOINED the group ***') -eq 0) 'the app joined nothing in this leg'
      Check ((CountIn $log 'device_join refused (ErrJoinRefused)') -eq 1) 'the window''s sentence was answered once, to the flipped invitation only'

      # 7. R25 Q2: "Make a new join code", before and after a press. The box shows a NEW join code,
      #    and it is the one now in the handshake file.
      Check ($null -ne (FindByName $app.Id $say.NewJoinCodeButton)) "7: the row offers '$($say.NewJoinCodeButton)'"
      $before = ReadValue $app.Id 'Your join code'
      Note "7: the join code before the press: $($before.Length) characters, sha256 $(Sha $before)"
      Shot $app.Id '7-new-join-code-before'
      DumpNames $app.Id '7-new-join-code-before-uia'
      Invoke-Named $app.Id $say.NewJoinCodeButton | Out-Null
      Check (WaitLog '*** NEW JOIN CODE ***' 1 120) '7: the worker minted a new join code'
      Check ($null -ne (WaitName $app.Id $say.NewJoinCode 30)) "7: the window carries, verbatim: $($say.NewJoinCode)"
      Start-Sleep -Seconds 2
      $after = ReadValue $app.Id 'Your join code'
      Note "7: the join code after the press: $($after.Length) characters, sha256 $(Sha $after)"
      Check ($after.Length -gt 100 -and $before.Length -gt 100) '7: both join codes are on screen'
      Check ($after -ne $before) '7: the join code after the press is NOT the one before it'
      Check ($after -eq [Convert]::ToBase64String([IO.File]::ReadAllBytes($kp))) '7: the new join code is the one in the handshake file'
      Check ($null -eq (FindByName $app.Id $say.NewJoinCodeButton)) '7: the offer is withdrawn after the press'
      Shot $app.Id '8-new-join-code-after'
      DumpNames $app.Id '8-new-join-code-after-uia'
      CloseApp $app
    }

    'adder' {
      $app = StartApp
      Check (WaitLog 'published this device''s key package' 1 240) 'A: the app published its join code and waits'
      Check (WaitFile 'adder-ready' 120) 'A: the rig wrote the join codes'
      Check (WaitFile 'old-kp.bin' 120) 'A: the older build wrote its join code'
      Start-Sleep -Seconds 3
      Invoke-Named $app.Id 'Start a new group, with you as its owner' | Out-Null
      Check (WaitLog '*** GROUP CREATED ***' 1 120) 'A: the app founded a group'
      Start-Sleep -Seconds 3
      Shot $app.Id 'A-founded'
      # the founder holds a group: the intro does not say "not in a group yet"
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "A: the founder's intro reads, verbatim: $($say.HeldIntro)"
      Check ($null -eq (FindByName $app.Id $say.JoinIntro)) 'A: the founder''s intro does not say "not in a group yet"'

      Set-Named $app.Id 'Paste their join code here' (B64 'damaged.bin') | Out-Null
      Invoke-Named $app.Id 'Add this person to the group' | Out-Null
      Row $app.Id '1-key-package-invalid' 'refused the add (ErrKeyPackageInvalid)' 1 $say.Invalid @($say.JoinIntro)
      Check ($null -ne (FindByName $app.Id 'Paste their join code here')) '1: the add box is still there'

      if (Test-Path -LiteralPath (Join-Path $rigDir 'old-kp.bin')) {
        Set-Named $app.Id 'Paste their join code here' (B64 'old-kp.bin') | Out-Null
        Invoke-Named $app.Id 'Add this person to the group' | Out-Null
        Row $app.Id '2-cannot-open-sealed' 'refused the add (ErrJoinerCannotOpenSealedInvite)' 1 $say.CannotOpen @($say.JoinIntro)
        Check ($null -ne (FindByName $app.Id 'Paste their join code here')) '2: the add box is still there'
      } else {
        $script:notReached += 'ErrJoinerCannotOpenSealedInvite'
      }

      Set-Named $app.Id 'Paste their join code here' (B64 'honest.bin') | Out-Null
      Invoke-Named $app.Id 'Add this person to the group' | Out-Null
      Check (WaitLog '*** MEMBER ADDED ***' 1 120) '3: the honest join code was added'
      Check ($null -ne (WaitName $app.Id $say.AdderLabel 30)) "3: the adder's copy is on screen, verbatim: $($say.AdderLabel)"
      Check ($null -ne (FindByName $app.Id 'The invitation to send them')) '3: the invitation box is on screen'
      Check ($null -eq (FindByName $app.Id $say.JoinIntro)) '3: the intro does not say "not in a group yet"'
      Start-Sleep -Seconds 2
      Shot $app.Id '3-adder-copy'
      DumpNames $app.Id '3-adder-copy-uia'

      Set-Named $app.Id 'Paste their join code here' (B64 'second.bin') | Out-Null
      Invoke-Named $app.Id 'Add this person to the group' | Out-Null
      Row $app.Id '4-already-has-a-leaf' 'refused the add (ErrJoinerAlreadyHasALeaf)' 1 $say.HasALeaf @($say.JoinIntro)
      Check ($null -ne (FindByName $app.Id 'Paste their join code here')) '4: the add box is still there'
      CloseApp $app
    }

    'held' {
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'A-joined'
      CloseApp $app
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'gid.txt') -Raw).Trim()
      $gid = New-Object byte[] 32
      for ($i = 0; $i -lt 32; $i++) { $gid[$i] = [Convert]::ToByte($gidHex.Substring($i * 2, 2), 16) }
      $sha = [Security.Cryptography.SHA256]::Create()
      $name = (($sha.ComputeHash($gid) | ForEach-Object { $_.ToString('x2') }) -join '')
      $epochs = @(Get-ChildItem -LiteralPath (Join-Path $root 'live\state') -Recurse -File | Where-Object { $_.FullName -like "*\$name\epoch\*" })
      Check ($epochs.Count -ge 1) "B: the group has epoch states to remove ($($epochs.Count))"
      $epochs | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
      $app = StartApp
      Check (WaitLog 'published this device''s key package' 2 240) 'C: the app holds no live group and waits'
      $first = [IO.File]::ReadAllBytes((Join-Path $rigDir 'invite1.bin'))
      [IO.File]::WriteAllBytes((Join-Path $handshake 'app.invite.partial'), $first)
      Move-Item -LiteralPath (Join-Path $handshake 'app.invite.partial') -Destination (Join-Path $handshake 'app.invite') -Force
      Row $app.Id 'C1-held-intro' 'is not live; the leave is offered' 1 $say.HeldNotLive @($say.JoinIntro)
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "C: the intro reads, verbatim: $($say.HeldIntro)"
      Check ($null -ne (FindByName $app.Id 'Leave the old copy of this group')) 'C: the leave is offered'
      CloseApp $app
    }

    'notreadnow' {
      # A. the app joins the founder's group (the rig's held road) and holds it on its disk
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'A-joined'
      CloseApp $app
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'gid.txt') -Raw).Trim()
      $groupDir = GroupDirOf $gidHex
      $meta = Join-Path $groupDir 'meta'
      Check (Test-Path -LiteralPath $meta) "A: the group's record is on the disk: $meta"
      $metaHash = (Get-FileHash -LiteralPath $meta).Hash
      $filesBefore = @(Get-ChildItem -LiteralPath $groupDir -Recurse -File).Count
      Note "A: the group's directory holds $filesBefore file(s); the record's sha256 is $metaHash"
      function DenyMeta {
        & icacls $meta /deny "${who}:(RD)" | ForEach-Object { Note "icacls: $_" }
        $read = $true
        try { [void][IO.File]::ReadAllBytes($meta) } catch { $read = $false; Note "probe: the record does not read: $($_.Exception.Message)" }
        Check (-not $read) 'the record''s read is denied'
        Check (Test-Path -LiteralPath $meta) 'the record is still there to see'
        $script:deniedMeta = $meta
      }
      function GiveMeta {
        & icacls $meta /remove:d $who | ForEach-Object { Note "icacls: $_" }
        $read = $true
        try { [void][IO.File]::ReadAllBytes($meta) } catch { $read = $false }
        Check $read 'the record reads again'
        $script:deniedMeta = $null
      }

      # C. THE RESTORE ROAD: the record's read is denied before the app starts
      DenyMeta
      $app = StartApp
      $restoreSays = "$($say.NotReadNow) $($say.SendJoinCode)"
      Row $app.Id 'C1-restore-not-read-now' 'device_restore refused a group (ErrGroupRecordNotReadNow)' 1 $restoreSays @($say.JoinIntro, "$($say.NotReadNow) $($say.NotInAGroup) $($say.SendJoinCode)", 'Leave the old copy of this group')
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "C: the intro reads, verbatim: $($say.HeldIntro) (R25 Q4)"
      Check ((CountIn (LogText) 'urmessage: a group record on this device''s disk could not be read just now') -ge 1) 'C: the library''s report was ErrGroupRecordNotReadNow'

      # D. THE JOIN ROAD: the invitation into that group, while the record still does not read
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'D1-join-not-read-now' 'device_join refused (ErrGroupRecordNotReadNow)' 1 $say.NotReadNow @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro)
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "D: the intro reads, verbatim: $($say.HeldIntro)"
      Check ((CountIn (LogText) 'could not be read, so the invite into it is refused') -ge 1) 'D: the library''s refusal was the gate''s unanswered read'

      # E. the record reads again: the same invitation is refused over a held group, and the leave
      #    is offered (Task 39)
      GiveMeta
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'E1-held-leave-offered' 'is not live; the leave is offered' 1 $say.HeldNotLive @($say.JoinIntro)
      Check ($null -ne (FindByName $app.Id 'Leave the old copy of this group')) 'E: the leave is offered'

      # F. THE LEAVE ROAD: the read is denied again, then the leave is pressed. It restores first,
      #    which cannot read the record, and then ForgetGroup is refused before anything is written
      DenyMeta
      Invoke-Named $app.Id 'Leave the old copy of this group' | Out-Null
      Row $app.Id 'F1-leave-not-read-now' 'could not be read just now (ErrGroupRecordNotReadNow); nothing was left' 1 $say.NotReadNow @('Leave the old copy of this group', $say.HeldNotLive, $say.JoinIntro)
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "F: the intro reads, verbatim: $($say.HeldIntro)"
      Check ((CountIn (LogText) 'on its disk could not be read, so nothing was left') -ge 1) 'F: the library''s refusal was ForgetGroup''s unanswered read'
      $log = LogText
      Check ((CountIn $log '*** LEFT ***') -eq 0) 'F: nothing was left'
      Check ((CountIn $log 'The old copy of this group could not be left') -eq 0) 'F: Task 39''s fallback was not shown'
      CloseApp $app
      GiveMeta
      Check ((Get-FileHash -LiteralPath $meta).Hash -eq $metaHash) 'F: the record is the one it was'
      $filesAfter = @(Get-ChildItem -LiteralPath $groupDir -Recurse -File).Count
      Check ($filesAfter -eq $filesBefore) "F: the group's directory still holds $filesBefore file(s) ($filesAfter)"

      # G. with the record readable, the next launch restores the group: nothing of it was changed
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 1 240) 'G: the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'G-restored'
      CloseApp $app
    }

    'nowrapkey' {
      $app = StartApp
      Check (WaitLog 'published this device''s key package' 1 240) 'A: the app minted its identity and published a join code'
      Start-Sleep -Seconds 3
      Shot $app.Id 'A-first-launch'
      CloseApp $app
      # THE IDENTITY RECORD, REWRITTEN TO THE THREE PARTS A STORE WRITTEN BEFORE THE SEED WAS KEPT
      # HOLDS: magic, version, kind, count, each part length-prefixed, and the SHA-256 of all of it
      # (sdk/urmessage/statestore_durable.go, encodeStateRecord). The fourth part is the seed.
      $path = Join-Path (DataDir) 'device'
      $raw = [IO.File]::ReadAllBytes($path)
      $kind = $raw[9]; $count = $raw[10]
      Check ($raw[8] -eq 1 -and $kind -eq 4 -and $count -eq 4) "B: the identity record is version 1, kind 4, with 4 parts (it is $($raw[8]), $kind, $count)"
      $at = 11; $parts = @()
      for ($i = 0; $i -lt $count; $i++) {
        $len = ([int]$raw[$at] -shl 24) -bor ([int]$raw[$at + 1] -shl 16) -bor ([int]$raw[$at + 2] -shl 8) -bor [int]$raw[$at + 3]
        $at += 4
        $part = New-Object byte[] $len
        [Array]::Copy($raw, $at, $part, 0, $len)
        $parts += , $part
        $at += $len
      }
      Check ($at -eq $raw.Length - 32) 'B: the parts end where the checksum begins'
      Check ($parts[3].Length -eq 32) "B: the fourth part is a 32 octet seed ($($parts[3].Length))"
      $ms = New-Object IO.MemoryStream
      $ms.Write($raw, 0, 10)
      $ms.WriteByte(3)
      for ($i = 0; $i -lt 3; $i++) {
        $len = $parts[$i].Length
        $ms.WriteByte(($len -shr 24) -band 0xff); $ms.WriteByte(($len -shr 16) -band 0xff)
        $ms.WriteByte(($len -shr 8) -band 0xff); $ms.WriteByte($len -band 0xff)
        $ms.Write($parts[$i], 0, $len)
      }
      $body = $ms.ToArray()
      $sum = [Security.Cryptography.SHA256]::Create().ComputeHash($body)
      [IO.File]::WriteAllBytes($path, [byte[]]($body + $sum))
      Check ((Get-Item -LiteralPath $path).Length -eq $raw.Length - 36) "B: the record is rewritten without its seed ($($raw.Length) -> $((Get-Item -LiteralPath $path).Length) octets)"
      $app = StartApp
      Row $app.Id 'C1-no-wrap-key' 'device_key_package refused (ErrNoDeviceWrapKey)' 1 $say.NoWrapKey @('Paste the invitation here', 'Start a new group, with you as its owner')
      CloseApp $app
    }
  }
} catch {
  Note "DRIVER ERROR: $_"
  $script:failures++
} finally {
  if ($null -ne $deniedDir) { & icacls $deniedDir /remove:d $who 2>$null | Out-Null }
  if ($null -ne $script:deniedMeta) { & icacls $script:deniedMeta /remove:d $who 2>$null | Out-Null }
  Set-Content -LiteralPath (Join-Path $rigDir 'stop') -Value 'stop'
  if (-not $rigProc.WaitForExit(20000)) { Stop-Process -Id $rigProc.Id -Force }
  if ($null -ne $oldProc -and -not $oldProc.WaitForExit(20000)) { Stop-Process -Id $oldProc.Id -Force }
  Get-Process URmessage -ErrorAction SilentlyContinue | Stop-Process -Force
  New-Item -ItemType Directory -Force (Join-Path $Out 'logs') | Out-Null
  Copy-Item -Path (Join-Path $root 'logs\*') -Destination (Join-Path $Out 'logs') -ErrorAction SilentlyContinue
  Get-ChildItem -LiteralPath $rigDir -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*.txt' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Out "rigdir-$($_.Name)") }
}
if ($script:notReached.Count -gt 0) { Note "NOT REACHED in this leg: $($script:notReached -join ', ')" }
Note "$script:failures check(s) failed"
if ($script:failures -gt 0) { exit 1 }
