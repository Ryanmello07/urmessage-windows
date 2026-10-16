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
#   notreadnow (R25 Q5; R27) a group whose RECORD another process holds, with a REAL lock: a helper
#              process opens it with no sharing and takes LockFileEx's exclusive lock, as the T1
#              cp3b tests' transientHold does. The restore road, the join road, then (R28) a held
#              refusal after that restore, and a relaunch that restores the group
#   permanent  (R27, R30 point 4) the record's BYTES damaged (its checksum broken), which is
#              proven permanent: the restore says so, the held refusal over it offers "Leave the old
#              copy", and the press leaves it
#   epochheld  (R28) the group's EPOCH files held by the same real lock. First held before the app
#              starts (the restore road: the held refusal says R25 Q5's row). Then the leave road:
#              the startup restore fails for a permanent reason (the epoch files' bytes damaged),
#              the held refusal offers the leave, the bytes are put back and the files held by the
#              lock instead, and the press says R25 Q5's row, calls no forget_group and erases
#              nothing. A relaunch restores the group with its history after each.
#   unproven   (R29, R30) a read DENIED by an ACL, which this build cannot classify: the record's,
#              then an epoch file's, then an epoch file's again at the press of a leave offered
#              after a permanent startup failure. Each says R29's unproven row with no leave, and a
#              relaunch with the read given back restores the group, every hash unchanged
#   absent     (R33 1, 2) a group's file MOVED AWAY before the app starts, as a quarantine or a sync
#              tool does: an epoch file (the unproven row, no leave), then the record (the store
#              erases nothing). Each put back, the next launch restores the group with its history.
#              It needs the library's R33 head: at 907109f an absence was proof
#
# The joiner leg's step 5 is re-cut for FIX 5's R34 item 7, as R37 approves: 5a shows R34 A1's row (an
# empty folder that takes no new file now refuses the commit's creation mark, inside the window), and 5b
# plants the store's own creation mark first, so the record's write is what fails: the spent row.
#
# The final run (ci/task58-shots-v6): the held leg damages its epoch states' bytes instead of
# removing them (an absence is no longer proof, R33), and the needles of the held refusals that the
# library's R33 X3 may now answer with the class's own sentinel are the generic "no leave is
# offered".
#
# R25's follow-up (ci/task58-shots-v3): the F5 sentence (adder), "Make a new join code" before and
# after a press (joiner). R27's and R28's (ci/task58-shots-v4): notreadnow with a real lock,
# permanent, and epochheld, on the T1 head after its second fix round (e0d16eac75e4).
#
# DRIVING: UI Automation patterns only (Invoke, and Value to put a code in a box). No mouse or
# keyboard input is synthesized. CAPTURE: PrintWindow(PW_RENDERFULLCONTENT) of the app's own window,
# found by its process id.
param(
  [Parameter(Mandatory = $true)][ValidateSet('joiner', 'adder', 'held', 'nowrapkey', 'notreadnow', 'permanent', 'epochheld', 'unproven', 'absent', 'names', 'listui', 'list2', 'listrestore')][string]$Leg,
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
  JoinRefusedByStore = 'URmessage could not use its own folder, so this invitation was not used. Your join code is still good: check that URmessage can read and write its folder, then paste the invitation again.'
  NoJoinCode         = 'This invitation cannot be used with any join code this device can read. Make a new join code and ask to be invited again.'
  Spent              = 'This join did not complete. Make a new join code and ask to be added again.'
  InviteVersion      = 'This invitation was made by an older URmessage. Ask them to update and invite you again.'
  Predates           = 'This group was started before invitations were sealed. Leave it and start it again.'
  CannotOpen         = 'This join code was made by an older URmessage. Ask them for a new one from an updated app.'
  Invalid            = 'This is not a valid join code. Ask them to send it again.'
  HasALeaf           = 'This join code claims to be someone already in this group. Remove their entry only if they tell you they never opened their invitation, then add this join code.'
  NotReadNow         = 'This conversation could not be read just now. Close anything else that may be using URmessage''s folder, then open URmessage again.'
  NewJoinCode        = 'This is a new join code. Send it to whoever is inviting you, and paste the invitation they send back.'
  NewJoinCodeButton  = 'Make a new join code'
  SendJoinCode       = 'Send your join code to whoever is setting the group up, and paste the invitation they send back.'
  NotInAGroup        = 'This device is not in a group yet.'
  Unproven           = 'This conversation could not be read on this device, and nothing was changed. Check that URmessage can read its folder and that no program has moved files out of it, then open URmessage again.'
  CreationUnfinished = 'This conversation was created, but URmessage could not finish saving it, and its invitation was not shown. Close anything else that may be using URmessage''s folder, then open URmessage again.'
  LeaveMarkForeign   = 'This conversation''s folder on this device holds a note about leaving a different conversation, so URmessage cannot tell whether this one was left. Nothing was changed. It cannot be joined or left on this device until the file named ''forgetting'' is moved out of that folder.'
  JoinerPasteLabel   = 'The invitation they send back. Keep it private: it was made for this device''s join code, so only this device can open it, and it works once.'
  OpenRecordNotSaved = 'URmessage could not save this new conversation on this device, so it cannot be used, and its invitation was not shown. Check that URmessage can read and write its folder, then start the conversation again.'
  NotConnected       = 'This device is not connected right now, and nothing was changed. Try again when it is connected.'
  NoWrapKey          = 'This installation cannot make a join code, so it cannot join a group. Its saved data was made by an older URmessage.'
  AdderLabel         = 'Only their device can open it. It works once. Send it privately.'
  Founded            = 'Your group is made, but it has only you in it and nothing can be sent yet. Ask somebody for their join code and paste it above - adding them is what opens the group.'
  FoundedBefore      = 'Your group is made, but it has only you in it and nothing can be sent yet. Ask somebody for their join code and paste it below - adding them is what opens the group.'
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
# ---- a REAL lock, held by another process (R27; the T1 cp3b tests' transientHold) ---------------
# A helper process opens each file with no sharing (share mode 0) and takes LockFileEx's exclusive
# lock over all of it, as a scanner or a backup tool holding a file does. Any other process's read
# then fails with a sharing or a lock violation, which R26 calls TRANSIENT. The helper keeps the
# files until the driver asks it to let go.
$holdScript = Join-Path $Work 'hold.ps1'
Set-Content -LiteralPath $holdScript -Encoding ascii -Value @'
param([string]$Tag, [string]$Dir, [string]$List)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Task58Hold {
  [StructLayout(LayoutKind.Sequential)]
  public struct OVERLAPPED { public IntPtr Internal; public IntPtr InternalHigh; public uint Offset; public uint OffsetHigh; public IntPtr hEvent; }
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
  public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern bool LockFileEx(IntPtr handle, uint flags, uint reserved, uint lowBytes, uint highBytes, ref OVERLAPPED overlapped);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern bool CloseHandle(IntPtr handle);
}
"@
$paths = @(Get-Content -LiteralPath $List | Where-Object { $_ -ne '' })
$handles = New-Object 'System.Collections.Generic.List[IntPtr]'
foreach ($p in $paths) {
  # GENERIC_READ, share mode 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL
  $h = [Task58Hold]::CreateFileW($p, [uint32]2147483648, [uint32]0, [IntPtr]::Zero, [uint32]3, [uint32]128, [IntPtr]::Zero)
  if ($h -eq [IntPtr](-1)) {
    Set-Content -LiteralPath (Join-Path $Dir "hold-$Tag.failed") -Value "CreateFileW $p failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    exit 1
  }
  $o = New-Object 'Task58Hold+OVERLAPPED'
  # LOCKFILE_EXCLUSIVE_LOCK over the whole file
  if (-not [Task58Hold]::LockFileEx($h, [uint32]2, [uint32]0, [uint32]::MaxValue, [uint32]::MaxValue, [ref]$o)) {
    Set-Content -LiteralPath (Join-Path $Dir "hold-$Tag.failed") -Value "LockFileEx $p failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    exit 1
  }
  $handles.Add($h)
}
Set-Content -LiteralPath (Join-Path $Dir "hold-$Tag.held") -Value "$($handles.Count)"
$release = Join-Path $Dir "hold-$Tag.release"
while (-not (Test-Path -LiteralPath $release)) { Start-Sleep -Milliseconds 200 }
foreach ($h in $handles) { [void][Task58Hold]::CloseHandle($h) }
Set-Content -LiteralPath (Join-Path $Dir "hold-$Tag.released") -Value 'released'
'@
$script:holds = @{}
function ReadFailure([string]$path) {
  try { [void][IO.File]::ReadAllBytes($path); return '' } catch {
    $ex = $_.Exception
    while ($null -ne $ex.InnerException) { $ex = $ex.InnerException }
    return $ex.Message
  }
}
function StartHold([string]$tag, [string[]]$paths) {
  $list = Join-Path $Work "hold-$tag.list"
  Set-Content -LiteralPath $list -Value $paths -Encoding ascii
  foreach ($suffix in 'held', 'failed', 'release', 'released') {
    Remove-Item -LiteralPath (Join-Path $Work "hold-$tag.$suffix") -ErrorAction SilentlyContinue
  }
  $p = Start-Process -FilePath 'powershell.exe' -PassThru -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $holdScript, '-Tag', $tag, '-Dir', $Work, '-List', $list)
  $script:holds[$tag] = $p
  $deadline = (Get-Date).AddSeconds(60)
  while ((Get-Date) -lt $deadline) {
    if ((Test-Path -LiteralPath (Join-Path $Work "hold-$tag.held")) -or
        (Test-Path -LiteralPath (Join-Path $Work "hold-$tag.failed")) -or $p.HasExited) { break }
    Start-Sleep -Milliseconds 200
  }
  $held = Test-Path -LiteralPath (Join-Path $Work "hold-$tag.held")
  if (-not $held) { Note "hold $tag did not take: $(Get-Content -LiteralPath (Join-Path $Work "hold-$tag.failed") -Raw -ErrorAction SilentlyContinue)" }
  Check $held "hold $tag`: a helper process (pid $($p.Id)) holds $($paths.Count) file(s) with share mode 0 and LockFileEx's exclusive lock"
  foreach ($path in $paths) {
    $why = ReadFailure $path
    Note "hold $tag`: a read of $path now answers: $why"
    Check ($why -match 'being used by another process|locked a portion') "hold $tag`: $(Split-Path -Leaf $path) does not read, with a sharing or lock violation (R26's transient)"
    Check ($why -notmatch 'denied') "hold $tag`: $(Split-Path -Leaf $path) is not denied (a denial is a different class)"
  }
}
function StopHold([string]$tag) {
  $p = $script:holds[$tag]
  if ($null -eq $p) { return }
  Set-Content -LiteralPath (Join-Path $Work "hold-$tag.release") -Value 'release'
  if (-not $p.WaitForExit(30000)) { Note "hold $tag did not let go within 30 s; stopping it"; Stop-Process -Id $p.Id -Force }
  $script:holds.Remove($tag)
  Check (Test-Path -LiteralPath (Join-Path $Work "hold-$tag.released")) "hold $tag`: the helper let go"
}
$script:denied = @()
function DenyRead([string[]]$paths) {
  foreach ($path in $paths) {
    & icacls $path /deny "${who}:(RD)" | ForEach-Object { Note "icacls: $_" }
    $script:denied += $path
    $why = ReadFailure $path
    Note "deny: a read of $path now answers: $why"
    Check ($why -match 'denied') "deny: $(Split-Path -Leaf $path)'s read is denied (R30: unproven)"
  }
}
function GiveRead([string[]]$paths) {
  foreach ($path in $paths) {
    & icacls $path /remove:d $who | ForEach-Object { Note "icacls: $_" }
    $script:denied = @($script:denied | Where-Object { $_ -ne $path })
    Check ((ReadFailure $path) -eq '') "deny: $(Split-Path -Leaf $path) reads again"
  }
}
# ---- BYTE DAMAGE (R30 point 4): the last octet of a record flipped, which breaks its checksum, a
# verdict this build makes on bytes it read and so PROVEN permanent. The original bytes are kept so
# a leg can put them back.
$script:originals = @{}
function BreakBytes([string[]]$paths) {
  foreach ($path in $paths) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $script:originals[$path] = [byte[]]$bytes.Clone()
    $bytes[$bytes.Length - 1] = [byte]($bytes[$bytes.Length - 1] -bxor 1)
    [IO.File]::WriteAllBytes($path, $bytes)
    $now = [IO.File]::ReadAllBytes($path)
    Check (($now.Length -eq $bytes.Length) -and ($now[$now.Length - 1] -ne $script:originals[$path][$now.Length - 1])) "bytes: $(Split-Path -Leaf $path)'s last octet is flipped ($($now.Length) octets)"
  }
}
function RestoreBytes([string[]]$paths) {
  foreach ($path in $paths) {
    [IO.File]::WriteAllBytes($path, $script:originals[$path])
    $now = [IO.File]::ReadAllBytes($path)
    $same = $now.Length -eq $script:originals[$path].Length
    for ($i = 0; $same -and $i -lt $now.Length; $i++) { if ($now[$i] -ne $script:originals[$path][$i]) { $same = $false } }
    Check $same "bytes: $(Split-Path -Leaf $path) is put back as it was"
  }
}
function DirState([string]$dir) {
  $files = @(Get-ChildItem -LiteralPath $dir -Recurse -File -ErrorAction SilentlyContinue | Sort-Object FullName)
  $names = ($files | ForEach-Object { $_.FullName.Substring($dir.Length) + ':' + $_.Length }) -join ','
  return "$($files.Count) file(s) [$names]"
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

# ---- listui (feat/conversation-list, commit 7b): the list's "+", its two ways in, and the rail's
# Add someone, beside a conversation the app holds. Driven with InvokePattern (and the window's
# TransformPattern for the rail's width) and read as text; nothing is typed or clicked.
$luOne = 'Only you so far. Add someone to start it. It is not kept until they join.'
$luPlus = 'New conversation or join with an invitation'
# INVOKE THE ELEMENT OF THAT NAME THAT CAN BE INVOKED: a menu's item and the button that opens it
# share their names with a tooltip, or with the text inside them, which take no InvokePattern. Looked
# for under the app's window first, then under every window of the app's process (a menu may stand
# in a popup window of its own).
function LuInvoke([int]$processId, [string]$name, [int]$seconds = 15) {
  $nameCond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
  $procCond = New-Object System.Windows.Automation.AndCondition(
    (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $processId)),
    $nameCond)
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $found = @()
    try { $found += @((AppWindow $processId).FindAll([System.Windows.Automation.TreeScope]::Descendants, $nameCond)) } catch { }
    try { $found += @([System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Descendants, $procCond)) } catch { }
    foreach ($e in $found) {
      $pattern = $null
      try { $pattern = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern) } catch { $pattern = $null }
      if ($null -ne $pattern) {
        $pattern.Invoke()
        Note "invoked '$name' ($($e.Current.ControlType.ProgrammaticName), InvokePattern)"
        return $true
      }
    }
    Start-Sleep -Milliseconds 500
  }
  Check $false "the app carries '$name' to invoke"
  return $false
}
# THE ELEMENT OF A CONTROL TYPE WHOSE NAME STARTS WITH `prefix` (a row is named by its preview and its
# time, the composer by its state), or $null.
function LuFirst([int]$processId, [System.Windows.Automation.ControlType]$type, [string]$prefix) {
  foreach ($e in (NmAll $processId)) {
    try {
      if ($e.Current.ControlType -eq $type -and ([string]$e.Current.Name).StartsWith($prefix)) { return $e }
    } catch { }
  }
  return $null
}
function LuOpenRow([int]$processId, [string]$prefix) {
  $row = LuFirst $processId ([System.Windows.Automation.ControlType]::Button) $prefix
  if ($null -eq $row) { Check $false "a row named '$prefix...' is in the list"; return $false }
  $row.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  Note "opened the row '$($row.Current.Name)' (InvokePattern)"
  Start-Sleep -Seconds 2
  return $true
}
function LuComposer([int]$processId) { return (LuFirst $processId ([System.Windows.Automation.ControlType]::Edit) 'Message') }
# The list's menu, by the "+".
function LuMenu([int]$processId, [string]$item) {
  if (-not (LuInvoke $processId $luPlus)) { return $false }
  Start-Sleep -Milliseconds 800
  return (LuInvoke $processId $item)
}

# ---- list2 and listrestore (feat/conversation-list): two conversations, founded by two of the
# rig's founders, the second joined above epoch 1.
$l2One = 'Line one of the first conversation, from its founder.'
$l2Two = 'Line one of the second conversation, from its founder.'
$l2Late = 'A second line in the first conversation, while the second is open.'
function L2Row([int]$processId, [string]$preview) {
  return (LuFirst $processId ([System.Windows.Automation.ControlType]::Button) ('unavailable, ' + $preview + ', '))
}
# A LINE IN THE OPEN THREAD: some element other than a row (a row's name opens with "unavailable, ")
# whose name holds `text`.
function L2InThread([int]$processId, [string]$text) {
  foreach ($e in (NmAll $processId)) {
    $n = NmNameOf $e
    if ($n.Contains($text) -and -not $n.StartsWith('unavailable, ')) { return $true }
  }
  return $false
}
function L2Send([int]$processId, [string]$text) {
  $box = LuComposer $processId
  if ($null -eq $box) { Check $false 'the composer is there to type into'; return $false }
  $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($text)
  Start-Sleep -Milliseconds 800
  $send = LuFirst $processId ([System.Windows.Automation.ControlType]::Button) 'Send'
  if ($null -eq $send) { Check $false 'the composer''s Send is there'; return $false }
  Note "L2: the Send button reads '$($send.Current.Name)'"
  $send.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  Note "sent '$text' (ValuePattern, InvokePattern)"
  return $true
}

$deniedDir = $null
$script:deniedMeta = $null
# ---- names (fix/live-row-names): the list's row names and the rail's density, in a live world ---
# F1: the selected row was named from the fabricated world. F2: the Advanced Mode toggle looked the
# rail's conversation up in the fabricated world, found none on a live world, and redrew nothing.
# Read with UI Automation, as TEXT, in a live launch (--live, the held road); nothing is typed or
# clicked, the window is resized with its TransformPattern, Settings and Chats are selected with
# SelectionItemPattern (or InvokePattern), and the Advanced Mode switch is flipped with TogglePattern.
$nmPreview = "From the runner's founder: invitation 1 is out."
function NmAll([int]$processId) {
  $win = AppWindow $processId
  return @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition))
}
function NmNameOf($e) { try { return [string]$e.Current.Name } catch { return '' } }
function NmButtonNames([int]$processId) {
  $out = @()
  foreach ($e in (NmAll $processId)) {
    try {
      if ($e.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button) { $out += (NmNameOf $e) }
    } catch { }
  }
  return $out
}
# THE WORLD HOLDS STILL: no strip rebuilt and no live world drawn for 10 s, so a check below cannot
# be satisfied, or upset, by a publish landing in the middle of it.
function NmDrawn { $log = LogText; return (CountIn $log 'window: live world generation') + (CountIn $log 'window: status strip built') }
function NmSettle {
  $deadline = (Get-Date).AddSeconds(90)
  $last = NmDrawn
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $now = NmDrawn
    if ($now -eq $last) { return }
    $last = $now
  }
  Note 'the world did not hold still within 90 s'
}
function NmResize([int]$processId, [int]$width, [int]$height) {
  $win = AppWindow $processId
  $tp = $win.GetCurrentPattern([System.Windows.Automation.TransformPattern]::Pattern)
  $tp.Resize($width, $height)
  Start-Sleep -Seconds 3
  $r = $win.Current.BoundingRectangle
  Note "the window is now $([int]$r.Width) x $([int]$r.Height) px (asked for $width x $height)"
  $layouts = @([regex]::Matches((LogText), 'window: layout [^\r\n]*') | ForEach-Object { $_.Value })
  $last = ''
  if ($layouts.Count -gt 0) { $last = $layouts[-1] }
  Note "the app's last layout line: $last"
  return $last
}
# P1. EVERY ROW OF THE LIVE LIST IS NAMED AS ITS OWN CONVERSATION, the selected one included.
function NmCheckNames([int]$processId, [string]$shot) {
  $buttons = @(NmButtonNames $processId)
  # A LIST ROW'S NAME ENDS WITH ITS TIME, and ". Selected" on the selected one
  # (ConversationRowAutomationName); the thread's bubble carries the same preview but is named
  # time first ("10:28. From the runner's founder: ..."), so it is not counted as a row.
  $rows = @($buttons | Where-Object { $_.Contains($nmPreview) -and ($_ -match ', \d\d:\d\d(\. Selected)?$') })
  Note "${shot}: the list's rows as UI Automation names them: $(($rows | ForEach-Object { '"' + $_ + '"' }) -join ' | ')"
  $selected = @($buttons | Where-Object { $_.EndsWith('. Selected') })
  Note "${shot}: every button named as selected: $(($selected | ForEach-Object { '"' + $_ + '"' }) -join ' | ')"
  $pattern = '^unavailable, ' + [regex]::Escape($nmPreview) + ', \d\d:\d\d\. Selected$'
  Check (($selected.Count -eq 1) -and ($selected[0] -match $pattern)) "${shot}: the selected row's name is its own conversation's, read as text: $(($selected | ForEach-Object { '"' + $_ + '"' }) -join ' | ')"
  Check ($rows.Count -eq 1) "${shot}: the live list's one row carries the live conversation's preview in its name ($($rows.Count) found)"
  $invented = @($buttons | Where-Object { $_.StartsWith('Design team') })
  Check ($invented.Count -eq 0) "${shot}: no row is named as a conversation of the fabricated world ($($invented.Count) named 'Design team, ...')"
}
function NmField([int]$processId, [string]$prefix) {
  foreach ($e in (NmAll $processId)) {
    $n = NmNameOf $e
    if ($n.StartsWith($prefix)) { return $n }
  }
  return $null
}
# THE HELP TEXT OF THE ELEMENT WHOSE NAME STARTS WITH `prefix` (S5: a rail row's whole value), or $null.
function NmFieldHelp([int]$processId, [string]$prefix) {
  foreach ($e in (NmAll $processId)) {
    $n = NmNameOf $e
    if ($n.StartsWith($prefix)) { try { return [string]$e.Current.HelpText } catch { return $null } }
  }
  return $null
}
function NmWaitField([int]$processId, [string]$prefix, [bool]$present, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  do {
    $n = NmField $processId $prefix
    if (($null -ne $n) -eq $present) { return $true }
    Start-Sleep -Milliseconds 500
  } while ((Get-Date) -lt $deadline)
  return $false
}
function NmSelectNav([int]$processId, [string]$name) {
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
  $deadline = (Get-Date).AddSeconds(30)
  do {
    $win = AppWindow $processId
    foreach ($e in @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond))) {
      try { $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); Note "selected '$name' (SelectionItemPattern)"; return $true } catch { }
      try { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Note "invoked '$name' (InvokePattern)"; return $true } catch { }
    }
    Start-Sleep -Seconds 1
  } while ((Get-Date) -lt $deadline)
  Check $false "the window carries '$name' to select"
  return $false
}
# Settings -> the Advanced Mode switch (TogglePattern) -> Chats. Answers the switch's new state.
function NmToggleAdvanced([int]$processId) {
  if (-not (NmSelectNav $processId 'Settings')) { return '' }
  Start-Sleep -Seconds 2
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, 'Advanced Mode')
  $toggle = $null
  foreach ($e in @((AppWindow $processId).FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond))) {
    try { $toggle = $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern); break } catch { }
  }
  if ($null -eq $toggle) { Check $false 'the Settings page carries the Advanced Mode switch (TogglePattern)'; return '' }
  $before = $toggle.Current.ToggleState
  $toggle.Toggle()
  Start-Sleep -Milliseconds 800
  $after = $toggle.Current.ToggleState
  Note "toggled Advanced Mode (TogglePattern): $before -> $after"
  if (-not (NmSelectNav $processId 'Chats')) { return '' }
  Start-Sleep -Seconds 1
  return [string]$after
}
function NmLastRailLine {
  $lines = @([regex]::Matches((LogText), 'rail: density -> [^\r\n]*') | ForEach-Object { $_.Value })
  if ($lines.Count -gt 0) { return $lines[-1] }
  return '(no rail density line)'
}

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

      # 5a. R34 A1 (the re-cut R37 approves): an EMPTY group folder that takes no new file. Since FIX 5
      #     (R34 item 7) a folder holding none of the store's names is one the join's commit creates, so
      #     the commit writes its creation mark there first, and that write is refused INSIDE the join's
      #     window: A1's row, nothing written, and the join code not spent
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'spent-gid.txt') -Raw).Trim()
      $gid = New-Object byte[] 32
      for ($i = 0; $i -lt 32; $i++) { $gid[$i] = [Convert]::ToByte($gidHex.Substring($i * 2, 2), 16) }
      $deniedDir = GroupDirOf $gidHex
      New-Item -ItemType Directory -Force $deniedDir | Out-Null
      & icacls $deniedDir /deny "${who}:(WD)" | ForEach-Object { Note "icacls: $_" }
      $probe = Join-Path $deniedDir 'probe'
      $created = $true
      try { [IO.File]::WriteAllText($probe, 'x') } catch { $created = $false }
      Check (-not $created) "5a: $deniedDir takes no new file"
      $sub = Join-Path $deniedDir 'probe-dir'
      $made = $true
      try { [void][IO.Directory]::CreateDirectory($sub); [IO.File]::WriteAllText((Join-Path $sub 'x'), 'x') } catch { $made = $false }
      Check $made '5a: a subdirectory of it can be made and written'
      if ($made) { Remove-Item -LiteralPath $sub -Recurse -Force }
      Set-Named $app.Id 'Paste the invitation here' (B64 'spent.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id '5a-join-refused-by-store' 'device_join refused (ErrJoinRefused+ErrStateStoreState)' 1 $say.JoinRefusedByStore @($say.JoinRefused, $say.Spent)
      Check ($null -eq (FindByName $app.Id $say.NewJoinCodeButton)) "5a: the row offers no '$($say.NewJoinCodeButton)'"
      Check (@(Get-ChildItem -LiteralPath $deniedDir -Force).Count -eq 0) '5a: nothing was written into the group''s folder'
      & icacls $deniedDir /remove:d $who | ForEach-Object { Note "icacls: $_" }

      # 5b. THE SPENT ROW: the same folder holds the store's own CREATION MARK for this group (kind 9,
      #     the group's id, framed and summed as encodeStateRecord writes it), planted before the folder
      #     takes no new file. The gate reads the creating state as not held, the commit finds the
      #     store's names there and writes no mark, the joined state lands in its epoch subfolder, and
      #     the record cannot be written: the join code is spent
      $ms = New-Object IO.MemoryStream
      $magic = [Text.Encoding]::ASCII.GetBytes('URMSTATE')
      $ms.Write($magic, 0, $magic.Length)
      $ms.WriteByte(1); $ms.WriteByte(9); $ms.WriteByte(1)
      $ms.WriteByte(0); $ms.WriteByte(0); $ms.WriteByte(0); $ms.WriteByte(32)
      $ms.Write($gid, 0, 32)
      $body = $ms.ToArray()
      $sum = [Security.Cryptography.SHA256]::Create().ComputeHash($body)
      [IO.File]::WriteAllBytes((Join-Path $deniedDir 'pending'), [byte[]]($body + $sum))
      Check (Test-Path -LiteralPath (Join-Path $deniedDir 'pending')) '5b: the creation mark is in the group''s folder'
      & icacls $deniedDir /deny "${who}:(WD)" | ForEach-Object { Note "icacls: $_" }
      Set-Named $app.Id 'Paste the invitation here' (B64 'spent.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id '5b-join-code-spent' 'device_join refused (ErrJoinCodeSpent)' 1 $say.Spent @($say.JoinRefused, $say.JoinRefusedByStore)
      Check ($null -ne (FindByName $app.Id $say.NewJoinCodeButton)) "5b: the row offers '$($say.NewJoinCodeButton)'"
      if (Test-Path -LiteralPath $deniedDir) { & icacls $deniedDir /remove:d $who | ForEach-Object { Note "icacls: $_" } }
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
      # R43 A13: the founder's line says the join-code box is ABOVE it, and the box is above it
      $founded = WaitName $app.Id $say.Founded 30
      Start-Sleep -Seconds 3
      Shot $app.Id 'A-founded'
      DumpNames $app.Id 'A-founded-uia'
      Check ($null -ne $founded) "A: the founder's line reads, verbatim: $($say.Founded)"
      Check ($null -eq (FindByName $app.Id $say.FoundedBefore)) 'A: the founder''s line no longer says the box is below it'
      $box = FindByName $app.Id 'Paste their join code here'
      if ($null -ne $founded -and $null -ne $box) {
        $b = $box.Current.BoundingRectangle
        $t = $founded.Current.BoundingRectangle
        Note "A: the join-code box spans y $($b.Top) to $($b.Bottom); the founder's line spans y $($t.Top) to $($t.Bottom)"
        Check ($b.Bottom -le $t.Top) 'A: the join-code box is above the founder''s line'
      } else {
        Check $false 'A: the join-code box and the founder''s line are both on screen, to be placed'
      }
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
      Check ($epochs.Count -ge 1) "B: the group has epoch states to damage ($($epochs.Count))"
      # their BYTES damaged, which is proven permanent; removing them is an absence, which R33 makes
      # unproven, and Task 39's held sentence is then not the true one
      BreakBytes @($epochs | ForEach-Object { $_.FullName })
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
      $before = DirState $groupDir
      Note "A: the group's directory: $before; the record's sha256 is $metaHash"

      # B. ANOTHER PROCESS HOLDS THE RECORD, with a real lock (R27)
      StartHold 'meta' @($meta)

      # C. THE RESTORE ROAD
      $app = StartApp
      $restoreSays = "$($say.NotReadNow) $($say.SendJoinCode)"
      Row $app.Id 'C1-restore-record-held' 'device_restore refused a group (ErrGroupRecordNotReadNow)' 1 $restoreSays @($say.JoinIntro, "$($say.NotReadNow) $($say.NotInAGroup) $($say.SendJoinCode)", 'Leave the old copy of this group')
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "C: the intro reads, verbatim: $($say.HeldIntro) (R25 Q4)"
      $log = LogText
      Check ((CountIn $log 'a file of a group on this device''s disk could not be read just now') -ge 1) 'C: the library''s report was ErrGroupRecordNotReadNow, in e0d16ea''s words'
      Check (($log -match 'meta could not be read: open [^\r\n]*(being used by another process|locked a portion)')) 'C: the record''s read failed with a sharing or lock violation'

      # D. THE JOIN ROAD: the invitation into that group while the record is still held
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'D1-join-record-held' 'device_join refused (ErrGroupRecordNotReadNow)' 1 $say.NotReadNow @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro)
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "D: the intro reads, verbatim: $($say.HeldIntro)"
      Check ((CountIn (LogText) 'could not be read, so the invite into it is refused') -ge 1) 'D: the library''s refusal was the gate''s unanswered read'

      # E. R28, THE RESTORE ROAD: the lock is let go and the same invitation meets the held-group
      #    gate. The startup restore could not read a file just now, so the held refusal says R25
      #    Q5's row and offers no leave
      StopHold 'meta'
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'E1-held-after-not-read-restore' 'no leave is offered (R28)' 1 $say.NotReadNow @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro)
      $log = LogText
      Check ((CountIn $log 'the leave is offered:') -eq 0) 'E: no leave was offered in this leg'
      Check ((CountIn $log 'has a record on this device''s disk') -ge 1) 'E: the refusal was the held-group gate''s'
      CloseApp $app
      Check ((Get-FileHash -LiteralPath $meta).Hash -eq $metaHash) 'E: the record is the one it was'
      $after = DirState $groupDir
      Check ($after -eq $before) "E: the group's directory is as it was ($after)"

      # F. with nothing held, the next launch restores the group, with its history
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 1 240) 'F: the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'F-restored'
      CloseApp $app
    }

    'permanent' {
      # A. the app joins the founder's group and holds it on its disk
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'A-joined'
      CloseApp $app
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'gid.txt') -Raw).Trim()
      $groupDir = GroupDirOf $gidHex
      $meta = Join-Path $groupDir 'meta'
      Check (Test-Path -LiteralPath $meta) "A: the group's record is on the disk: $meta"
      Note "A: the group's directory: $(DirState $groupDir)"

      # B. THE RECORD'S BYTES ARE DAMAGED (R30 point 4: a deny ACL is no longer damage)
      BreakBytes @($meta)

      # C. THE RESTORE SAYS SO: the library reports the record as one that does not read
      $app = StartApp
      Check (WaitLog 'a group record on this device''s disk does not read, so that group cannot be restored' 1 240) 'C: the restore reported the record as damage (ErrGroupRecordDamaged)'
      $log = LogText
      Check ((CountIn $log 'urmessage: this does not read as a record this store writes') -ge 1) 'C: the verdict was on the record''s bytes (ErrStateStoreFormat)'
      Check ((CountIn $log 'device_restore refused a group (ErrGroupReadUnproven)') -eq 0) 'C: the restore did not report it as a file it cannot classify'
      Check ((CountIn $log 'device_restore refused a group (ErrGroupRecordNotReadNow)') -eq 0) 'C: the restore did not report it as a file not read just now'
      Check ($null -ne (WaitName $app.Id "$($say.NotInAGroup) $($say.SendJoinCode)" 60)) 'C: the dialog waits, with the join road''s text'
      Start-Sleep -Seconds 2
      Shot $app.Id 'C1-restore-damaged'
      DumpNames $app.Id 'C1-restore-damaged-uia'

      # D. the invitation into that group: the held refusal over a record that does not read, with
      #    Task 39's sentence and the leave
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'D1-held-damaged' 'is not live; the leave is offered' 1 $say.HeldNotLive @($say.NotReadNow, $say.JoinIntro)
      Check ($null -ne (FindByName $app.Id 'Leave the old copy of this group')) 'D: the leave is offered'
      Check ((CountIn (LogText) 'has a record on this device''s disk that does not read') -ge 1) 'D: the refusal was the gate''s, over damage'

      # E. THE PRESS LEAVES IT. Then the same invitation is opened again; its join code was used
      #    by the first join, so it is answered with the NoJoinCode row
      Invoke-Named $app.Id 'Leave the old copy of this group' | Out-Null
      Check (WaitLog '*** LEFT ***' 1 120) 'E: the press left the group'
      $log = LogText
      Check ((CountIn $log 'did not come back; leaving it') -eq 1) 'E: the leave asked forget_group, after a restore-first that reported damage'
      Check ((CountIn $log 'forget_group was not called') -eq 0) 'E: R28''s stop was not taken: the restore-first carried no sentinel'
      Row $app.Id 'E1-left' 'device_join refused (ErrNoJoinCodeForInvite)' 1 $say.NoJoinCode @('Leave the old copy of this group', $say.HeldNotLive)
      Check (-not (Test-Path -LiteralPath $meta)) "E: the record is gone"
      Check (@(Get-ChildItem -LiteralPath $groupDir -Recurse -File -ErrorAction SilentlyContinue).Count -eq 0) "E: no file of the group is left ($(DirState $groupDir))"
      CloseApp $app
    }

    'epochheld' {
      # A. the app joins the founder's group and holds it on its disk
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'A-joined'
      CloseApp $app
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'gid.txt') -Raw).Trim()
      $groupDir = GroupDirOf $gidHex
      $epochs = @(Get-ChildItem -LiteralPath (Join-Path $groupDir 'epoch') -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
      Check ($epochs.Count -ge 1) "A: the group has epoch files ($($epochs.Count)): $(($epochs | ForEach-Object { Split-Path -Leaf $_ }) -join ', ')"
      $hashes = @{}
      foreach ($f in @(Get-ChildItem -LiteralPath $groupDir -Recurse -File)) { $hashes[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash }
      $before = DirState $groupDir
      Note "A: the group's directory: $before"
      function Unchanged([string]$when) {
        $now = DirState $groupDir
        Check ($now -eq $before) "$when`: the group's directory is as it was ($now)"
        $same = $true
        foreach ($k in $hashes.Keys) { if ((Get-FileHash -LiteralPath $k -ErrorAction SilentlyContinue).Hash -ne $hashes[$k]) { $same = $false; Note "$when`: $k changed" } }
        Check $same "$when`: every file of the group is the one it was"
      }

      # ---- PART 1, R28's restore road: the epoch files held by a real lock BEFORE the app starts
      StartHold 'epoch1' $epochs
      $app = StartApp
      Row $app.Id 'C1-restore-epoch-held' 'device_restore refused a group (ErrGroupRecordNotReadNow)' 1 "$($say.NotReadNow) $($say.SendJoinCode)" @($say.JoinIntro, 'Leave the old copy of this group')
      $log = LogText
      Check ((CountIn $log 'a file of a group on this device''s disk could not be read just now') -ge 1) 'C: the library''s report was ErrGroupRecordNotReadNow'
      Check (($log -match 'epoch\\[0-9a-f]+ could not be read: open [^\r\n]*(being used by another process|locked a portion)')) 'C: an epoch file''s read failed with a sharing or lock violation'
      # the invitation into the group: its record reads, so the gate refuses it as held on the
      # disk, and after this restore that is R25 Q5's row with no leave (R28)
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'D1-held-after-not-read-restore' 'no leave is offered' 1 $say.NotReadNow @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro)
      Check ((CountIn (LogText) 'has a record on this device''s disk') -ge 1) 'D: the refusal was the held-group gate''s'
      CloseApp $app
      StopHold 'epoch1'
      Unchanged 'D'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 1 240) 'E: with nothing held, the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'E1-restored'
      CloseApp $app

      # ---- PART 2, R28's leave road. The startup restore fails for a PERMANENT reason (the epoch
      #      files' bytes damaged: the library notes the group as one that will not come back), so
      #      the held refusal offers the leave. Then the bytes are put back, the epoch files are
      #      held by the real lock, and the leave is pressed: its restore-first could not read a
      #      file just now, so it says R25 Q5's row and does NOT call forget_group
      BreakBytes $epochs
      $app = StartApp
      # (the third key package of this leg: A's first launch and part 1's restore road published the
      # first two, and part 1's relaunch restored the group and published none)
      Check (WaitLog 'published this device''s key package' 3 240) 'F: the app holds no live group and waits'
      Check ($null -ne (WaitName $app.Id "$($say.NotInAGroup) $($say.SendJoinCode)" 60)) 'F: the dialog waits, with the join road''s text'
      $log = LogText
      Check (($log -match 'at epoch [0-9]+: [^\r\n]*this does not read as a record this store writes')) 'F: the startup restore found the epoch state''s bytes damaged (proven permanent)'
      Check ((CountIn $log 'device_restore refused a group (ErrGroupRecordNotReadNow)') -eq 1) 'F: this restore did not report a file not read just now (the count is part 1''s)'
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'F2-held-leave-offered' 'is not live; the leave is offered' 1 $say.HeldNotLive @($say.NotReadNow, $say.JoinIntro)
      Check ($null -ne (FindByName $app.Id 'Leave the old copy of this group')) 'F: the leave is offered'
      RestoreBytes $epochs
      StartHold 'epoch2' $epochs
      Invoke-Named $app.Id 'Leave the old copy of this group' | Out-Null
      Row $app.Id 'G2-leave-stops' 'forget_group was not called (R28)' 1 $say.NotReadNow @('Leave the old copy of this group', $say.HeldNotLive, $say.JoinIntro)
      $log = LogText
      Check ((CountIn $log 'did not come back; leaving it') -eq 0) 'G: forget_group was not asked'
      Check ((CountIn $log '*** LEFT ***') -eq 0) 'G: nothing was left'
      Check ((CountIn $log 'The old copy of this group could not be left') -eq 0) 'G: Task 39''s fallback was not shown'
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "G: the intro reads, verbatim: $($say.HeldIntro)"
      $now = DirState $groupDir
      Check ($now -eq $before) "G: while the files are held, the group's directory is as it was ($now)"
      CloseApp $app
      StopHold 'epoch2'
      Unchanged 'G'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 2 240) 'H: with nothing held, the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'H2-restored'
      CloseApp $app
    }

    'unproven' {
      # A. the app joins the founder's group and holds it on its disk
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'A-joined'
      CloseApp $app
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'gid.txt') -Raw).Trim()
      $groupDir = GroupDirOf $gidHex
      $meta = Join-Path $groupDir 'meta'
      $epochs = @(Get-ChildItem -LiteralPath (Join-Path $groupDir 'epoch') -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
      Check (Test-Path -LiteralPath $meta) "A: the group's record is on the disk: $meta"
      Check ($epochs.Count -ge 1) "A: the group has epoch files ($($epochs.Count))"
      $hashes = @{}
      foreach ($f in @(Get-ChildItem -LiteralPath $groupDir -Recurse -File)) { $hashes[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash }
      $before = DirState $groupDir
      Note "A: the group's directory: $before"
      function Unchanged([string]$when) {
        $now = DirState $groupDir
        Check ($now -eq $before) "$when`: the group's directory is as it was ($now)"
        $same = $true
        foreach ($k in $hashes.Keys) { if ((Get-FileHash -LiteralPath $k -ErrorAction SilentlyContinue).Hash -ne $hashes[$k]) { $same = $false; Note "$when`: $k changed" } }
        Check $same "$when`: every file of the group is the one it was"
      }
      $restoreSays = "$($say.Unproven) $($say.SendJoinCode)"

      # ---- PART 1: THE RECORD'S READ DENIED before the app starts (the deny's control: a read
      #      under it is refused, "Access is denied", 5), the restore road and the join road
      DenyRead @($meta)
      $app = StartApp
      Row $app.Id 'C1-restore-record-denied' 'device_restore refused a group (ErrGroupReadUnproven)' 1 $restoreSays @($say.JoinIntro, 'Leave the old copy of this group', "$($say.NotReadNow) $($say.SendJoinCode)")
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "C: the intro reads, verbatim: $($say.HeldIntro)"
      $log = LogText
      Check ((CountIn $log 'could not be read, and this build cannot tell whether that will pass') -ge 1) 'C: the library''s report was ErrGroupReadUnproven'
      Check (($log -match 'meta could not be read: open [^\r\n]*Access is denied')) 'C: the deny''s control inside the app: the library''s own read of the record was refused, Access is denied (5)'
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'D1-join-record-denied' 'device_join refused (ErrGroupReadUnproven)' 1 $say.Unproven @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro, $say.NewJoinCodeButton)
      Check ((CountIn (LogText) 'could not be read, so the invite into it is refused: urmessage: a file of a group on this device''s disk could not be read, and this build cannot tell') -ge 1) 'D: the library''s refusal was the gate''s unanswered read, carrying the unproven class'
      # the read given back while the app runs: the invitation now meets the held-group gate, and
      # after a restore that could not classify the record's failure, the held refusal says the
      # unproven row with no leave (the restore-road flag, R28, R29)
      GiveRead @($meta)
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'E0-held-after-record-denied-restore' 'no leave is offered (R29)' 1 $say.Unproven @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro)
      Check ((CountIn (LogText) 'the leave is offered:') -eq 0) 'E0: no leave was offered after the record-denied restore'
      CloseApp $app
      Unchanged 'D'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 1 240) 'E: with the read given back, the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'E1-restored'
      CloseApp $app

      # ---- PART 2: AN EPOCH FILE'S READ DENIED before the app starts. Its record reads, so the
      #      invitation meets the held-group gate, and after this restore the held refusal says the
      #      unproven row with no leave (R28's flag, set by either class)
      DenyRead $epochs
      $app = StartApp
      Row $app.Id 'F2-restore-epoch-denied' 'device_restore refused a group (ErrGroupReadUnproven)' 2 $restoreSays @($say.JoinIntro, 'Leave the old copy of this group')
      Check (((LogText) -match 'epoch\\[0-9a-f]+ could not be read: open [^\r\n]*Access is denied')) 'F: the library''s own read of the epoch file was refused, Access is denied (5)'
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      # D1, E0 and this refusal each say "no leave is offered": by the gate's clause, by the held
      # refusal and the flag, or (at the R33 head) by the gate's class answer (X3)
      Row $app.Id 'G2-held-after-unproven-restore' 'no leave is offered' 3 $say.Unproven @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro)
      $log = LogText
      Check ((CountIn $log 'has a record on this device''s disk') -ge 1) 'G: the refusal was the held-group gate''s'
      Check ((CountIn $log 'the leave is offered:') -eq 0) 'G: no leave was offered after the unproven restore'
      CloseApp $app
      GiveRead $epochs
      Unchanged 'G'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 2 240) 'H: with the read given back, the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'H2-restored'
      CloseApp $app

      # ---- PART 3, THE LEAVE'S RESTORE-FIRST (R29 on R28's leave road). The startup restore finds
      #      the epoch state's bytes damaged (proven permanent), so the held refusal offers the
      #      leave. The bytes are put back and the epoch file's read denied, and the leave is
      #      pressed: its restore-first cannot classify the failure, so it says the unproven row,
      #      offers no leave and does NOT call forget_group
      BreakBytes $epochs
      $app = StartApp
      Check (WaitLog 'published this device''s key package' 4 240) 'I: the app holds no live group and waits'
      Check ($null -ne (WaitName $app.Id "$($say.NotInAGroup) $($say.SendJoinCode)" 60)) 'I: the dialog waits, with the join road''s text'
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'I3-held-leave-offered' 'is not live; the leave is offered' 1 $say.HeldNotLive @($say.Unproven, $say.JoinIntro)
      Check ($null -ne (FindByName $app.Id 'Leave the old copy of this group')) 'I: the leave is offered'
      RestoreBytes $epochs
      DenyRead $epochs
      Invoke-Named $app.Id 'Leave the old copy of this group' | Out-Null
      Row $app.Id 'J3-leave-stops-unproven' 'forget_group was not called (R29)' 1 $say.Unproven @('Leave the old copy of this group', $say.HeldNotLive, $say.JoinIntro)
      $log = LogText
      Check ((CountIn $log 'did not come back; leaving it') -eq 0) 'J: forget_group was not asked'
      Check ((CountIn $log '*** LEFT ***') -eq 0) 'J: nothing was left'
      Check ((CountIn $log 'The old copy of this group could not be left') -eq 0) 'J: Task 39''s fallback was not shown'
      CloseApp $app
      GiveRead $epochs
      Unchanged 'J'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 3 240) 'K: with the read given back, the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'K3-restored'
      CloseApp $app
    }

    'absent' {
      # A. the app joins the founder's group and holds it on its disk
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'A-joined'
      CloseApp $app
      $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'gid.txt') -Raw).Trim()
      $groupDir = GroupDirOf $gidHex
      $meta = Join-Path $groupDir 'meta'
      $epochs = @(Get-ChildItem -LiteralPath (Join-Path $groupDir 'epoch') -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
      Check (Test-Path -LiteralPath $meta) "A: the group's record is on the disk: $meta"
      Check ($epochs.Count -ge 1) "A: the group has epoch files ($($epochs.Count))"
      $hashes = @{}
      foreach ($f in @(Get-ChildItem -LiteralPath $groupDir -Recurse -File)) { $hashes[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName).Hash }
      $before = DirState $groupDir
      Note "A: the group's directory: $before"
      function Unchanged([string]$when) {
        $now = DirState $groupDir
        Check ($now -eq $before) "$when`: the group's directory is as it was ($now)"
        $same = $true
        foreach ($k in $hashes.Keys) { if ((Get-FileHash -LiteralPath $k -ErrorAction SilentlyContinue).Hash -ne $hashes[$k]) { $same = $false; Note "$when`: $k changed" } }
        Check $same "$when`: every file of the group is the one it was"
      }
      $aside = Join-Path $Work 'aside'
      New-Item -ItemType Directory -Force $aside | Out-Null
      function MoveAway([string[]]$paths) {
        foreach ($path in $paths) {
          Move-Item -LiteralPath $path -Destination (Join-Path $aside (Split-Path -Leaf $path)) -Force
          Check (-not (Test-Path -LiteralPath $path)) "away: $(Split-Path -Leaf $path) is moved out of the group's directory"
        }
      }
      function PutBack([string[]]$paths) {
        foreach ($path in $paths) {
          Move-Item -LiteralPath (Join-Path $aside (Split-Path -Leaf $path)) -Destination $path -Force
          Check (Test-Path -LiteralPath $path) "away: $(Split-Path -Leaf $path) is put back"
        }
      }

      # ---- PART 1: AN EPOCH FILE MOVED AWAY before the app starts. An absence is not proof (R33 1):
      #      the restore and the invitation's refusal each say the unproven row, with no leave
      MoveAway $epochs
      $app = StartApp
      Row $app.Id 'C1-restore-epoch-absent' 'device_restore refused a group (ErrGroupReadUnproven)' 1 "$($say.Unproven) $($say.SendJoinCode)" @($say.JoinIntro, 'Leave the old copy of this group')
      Check ((CountIn (LogText) 'urmessage: this state store holds no such value') -ge 1) 'C: the library''s report named the absence'
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      Invoke-Named $app.Id 'Join' | Out-Null
      Row $app.Id 'D1-held-after-absent-restore' 'no leave is offered' 1 $say.Unproven @($say.HeldNotLive, 'Leave the old copy of this group', $say.JoinIntro)
      Check ((CountIn (LogText) 'the leave is offered:') -eq 0) 'D: no leave was offered'
      CloseApp $app
      Check ((DirState $groupDir) -ne $before) 'D: (the epoch file is still away)'
      PutBack $epochs
      Unchanged 'D'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 1 240) 'E: the file put back, the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'E1-restored'
      CloseApp $app

      # ---- PART 2: THE RECORD MOVED AWAY before the app starts. The store erases nothing (R33 2:
      #      only its own "pending" marker lets the sweep erase), so the epoch files stay; the record
      #      put back, the next launch restores the group with its history
      MoveAway @($meta)
      $epochState = DirState $groupDir
      $app = StartApp
      Check (WaitLog 'published this device''s key package' 3 240) 'F: the app holds no live group and waits'
      Start-Sleep -Seconds 3
      $plain = $null -ne (FindByName $app.Id "$($say.NotInAGroup) $($say.SendJoinCode)")
      $unproven = $null -ne (FindByName $app.Id "$($say.Unproven) $($say.SendJoinCode)")
      Note "F: with the record away the dialog says $(if ($plain) { 'the join road''s text (the store reports no group)' } elseif ($unproven) { 'the unproven row (the store reports the directory)' } else { 'neither' })"
      Check ($plain -or $unproven) 'F: the dialog says the join road''s text or the unproven row, and offers no leave'
      Check ($null -eq (FindByName $app.Id 'Leave the old copy of this group')) 'F: no leave is offered'
      Shot $app.Id 'F2-record-absent'
      DumpNames $app.Id 'F2-record-absent-uia'
      Check ((DirState $groupDir) -eq $epochState) "F: nothing of the group was erased while its record was away ($(DirState $groupDir))"
      CloseApp $app
      Check ((DirState $groupDir) -eq $epochState) 'F: nothing was erased when the app closed either'
      PutBack @($meta)
      Unchanged 'G'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' 2 240) 'G: the record put back, the next launch restored the group'
      Start-Sleep -Seconds 8
      Shot $app.Id 'G2-restored'
      CloseApp $app
    }

    'names' {
      # A. the app joins the founder's group (the held road) and holds still
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      NmSettle

      # N1. the list's names at a typical width, the 1024 x 728 a live launch opened at on the
      #     runner's own 1024 x 768 screen (S3's shots)
      $layout = NmResize $app.Id 1024 728
      NmSettle
      NmCheckNames $app.Id 'N1-typical'
      Shot $app.Id 'N1-typical'
      DumpNames $app.Id 'N1-typical-uia'

      # N2. wide enough for the inspector rail (1500 content dip), on the screen the workflow set
      $layout = NmResize $app.Id 1720 1000
      Check ($layout -match 'rail=true') "N2-wide: the window is wide enough for the inspector rail ($layout)"
      NmSettle
      NmCheckNames $app.Id 'N2-wide'
      Check (NmWaitField $app.Id 'Retention, ' $true 15) 'N2-wide: the rail shows the open conversation (its Retention row)'
      Check ($null -eq (NmField $app.Id 'Conversation id, ')) 'N2-wide: in Normal mode the rail carries no Conversation id row'
      Shot $app.Id 'N2-wide-normal'
      DumpNames $app.Id 'N2-wide-normal-uia'

      # N3. Advanced Mode ON, at run time, from Settings: the rail redraws at once, with no publish
      $drawnBefore = NmDrawn
      $state = NmToggleAdvanced $app.Id
      Check ($state -eq 'On') "N3: the Advanced Mode switch is on ($state)"
      $on = NmWaitField $app.Id 'Conversation id, live-' $true 6
      $id = NmField $app.Id 'Conversation id, '
      Note "N3: the rail's last density line: $(NmLastRailLine)"
      Check $on "N3-advanced-on: Advanced Mode redraws the rail at once: its Conversation id row is there ($id)"
      # S5 (R60): THE LONG ID IS CUT ON SCREEN AND WHOLE IN THE ROW'S HELP TEXT (its tooltip's words)
      $whole = if ($null -ne $id) { $id.Substring('Conversation id, '.Length) } else { '' }
      $help = NmFieldHelp $app.Id 'Conversation id, '
      Check (($whole.Length -eq 69) -and ($help -eq $whole)) "N3-s5: the conversation id is whole in its row's help text ($($whole.Length) characters; help '$help')"
      Check ((NmDrawn) -eq $drawnBefore) "N3-advanced-on: no live world was drawn in between, so the redraw is the toggle's ($drawnBefore -> $(NmDrawn))"
      Shot $app.Id 'N3-advanced-on'
      DumpNames $app.Id 'N3-advanced-on-uia'

      # N4. Advanced Mode OFF again: the row goes at once
      $drawnBefore = NmDrawn
      $state = NmToggleAdvanced $app.Id
      Check ($state -eq 'Off') "N4: the Advanced Mode switch is off ($state)"
      $off = NmWaitField $app.Id 'Conversation id, ' $false 6
      Note "N4: the rail's last density line: $(NmLastRailLine)"
      Check $off 'N4-advanced-off: Normal mode redraws the rail at once: the Conversation id row is gone'
      Check ((NmDrawn) -eq $drawnBefore) "N4-advanced-off: no live world was drawn in between ($drawnBefore -> $(NmDrawn))"
      NmCheckNames $app.Id 'N4-advanced-off'
      Shot $app.Id 'N4-advanced-off'
      CloseApp $app
    }

    'listui' {
      # A. the app joins the founder's group (the held road) and holds still
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
      Start-Sleep -Seconds 8
      NmSettle
      Check ($null -eq (FindByName $app.Id 'Join a group')) 'A: no dialog stands over the conversation after the join'
      Shot $app.Id 'L0-joined'

      # L1. THE "+" IN THE LIST'S HEADER, named for both ways in
      Check ($null -ne (WaitName $app.Id $luPlus 30)) 'L1: the list header carries the "+", named for both ways in'

      # L2. JOIN WITH AN INVITATION: the dialog opens beside the conversation, on the join road, with
      #     a join code, and with no sentence from the join before it
      LuMenu $app.Id 'Join with an invitation' | Out-Null
      Check (WaitLog 'the join road was asked to open for the dialog' 1 30) 'L2: the join road was asked to open'
      Check ($null -ne (WaitName $app.Id 'Join a group' 30)) 'L2: the dialog is up, titled for the join road'
      Check ($null -ne (WaitName $app.Id 'Paste the invitation here' 30)) 'L2: the box for an invitation is there'
      Check ($null -ne (WaitName $app.Id $say.SendJoinCode 30)) "L2: the road's sentence reads, verbatim: $($say.SendJoinCode)"
      $code = ReadValue $app.Id 'Your join code'
      Check ($code.Length -gt 200) "L2: the join code box holds a code ($($code.Length) characters)"
      Check ($null -eq (FindByName $app.Id 'You are in the group.')) 'L2: no sentence from the earlier join is drawn'
      Check ($null -eq (FindByName $app.Id $say.JoinIntro)) 'L2: the intro does not say "not in a group yet" beside a conversation'
      Check ($null -ne (FindByName $app.Id $say.HeldIntro)) "L2: the intro reads, verbatim: $($say.HeldIntro)"
      Start-Sleep -Seconds 2
      Shot $app.Id 'L2-join-mode'
      DumpNames $app.Id 'L2-join-mode-uia'
      LuInvoke $app.Id 'Not now' | Out-Null
      Start-Sleep -Seconds 2
      Check ($null -eq (FindByName $app.Id 'Join a group')) 'L2: Not now closes it'

      # L3. NEW CONVERSATION: a group of one beside the joined one, and its add panel
      LuMenu $app.Id 'New conversation' | Out-Null
      Check (WaitLog '*** GROUP CREATED ***' 1 60) 'L3: the app founded a group beside the one it holds'
      Check ($null -ne (WaitName $app.Id 'Add someone' 30)) 'L3: the dialog is up, titled for the add road'
      Check ($null -ne (WaitName $app.Id $say.Founded 30)) "L3: the founder's line reads, verbatim: $($say.Founded)"
      Check ($null -ne (FindByName $app.Id 'Paste their join code here')) 'L3: the box for their join code is there'
      Check ($null -eq (FindByName $app.Id 'Paste the invitation here')) 'L3: the join road is not drawn'
      Start-Sleep -Seconds 2
      Shot $app.Id 'L3-new-conversation'
      DumpNames $app.Id 'L3-new-conversation-uia'
      LuInvoke $app.Id 'Done' | Out-Null
      Start-Sleep -Seconds 3

      # L4. TWO ROWS, and the group just founded is the open one, its row and its thread saying what
      #     it is
      Check (WaitLog 'drawn: 2 conversation(s)' 1 60) 'L4: the world holds two conversations'
      NmSettle
      $buttons = @(NmButtonNames $app.Id)
      $oneRows = @($buttons | Where-Object { $_.StartsWith('unavailable, ' + $luOne + ', ') })
      Note "L4: the rows naming the group of one: $(($oneRows | ForEach-Object { '"' + $_ + '"' }) -join ' | ')"
      Check ($oneRows.Count -eq 1) "L4: one row says what the group of one is ($($oneRows.Count) found)"
      Check (@($oneRows | Where-Object { $_.EndsWith('. Selected') }).Count -eq 1) 'L4: the group just founded is the open one'
      $other = @($buttons | Where-Object { $_.StartsWith('unavailable, ' + $nmPreview + ', ') })
      Check ($other.Count -eq 1) "L4: the joined conversation's row is still there ($($other.Count) found)"
      Check ($null -ne (FindByName $app.Id $luOne)) "L4: the group of one's thread says so, verbatim: $luOne"
      Shot $app.Id 'L4-two-rows'
      DumpNames $app.Id 'L4-two-rows-uia'

      # L6. A DRAFT STAYS WITH ITS CONVERSATION (F3): typed into the joined one, gone from the group
      #     of one's composer, and back when the joined one opens again
      $draft = 'a draft that belongs to the founder''s conversation'
      LuOpenRow $app.Id ('unavailable, ' + $nmPreview + ', ') | Out-Null
      $box = LuComposer $app.Id
      Check ($null -ne $box) "L6: the joined conversation's composer is there ($(if ($box) { $box.Current.Name }))"
      if ($null -ne $box) {
        $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($draft)
        Note "L6: typed the draft into the composer (ValuePattern)"
      }
      LuOpenRow $app.Id ('unavailable, ' + $luOne + ', ') | Out-Null
      $box = LuComposer $app.Id
      $there = if ($null -ne $box) { $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value } else { '(no composer)' }
      Check ($there -eq '') "L6: the group of one's composer is empty, not the other conversation's draft ('$there')"
      Shot $app.Id 'L6-draft-not-carried'
      LuOpenRow $app.Id ('unavailable, ' + $nmPreview + ', ') | Out-Null
      $box = LuComposer $app.Id
      $back = if ($null -ne $box) { $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value } else { '(no composer)' }
      Check ($back -eq $draft) "L6: the joined conversation's draft is back when it opens again ('$back')"
      Shot $app.Id 'L6-draft-back'
      LuOpenRow $app.Id ('unavailable, ' + $luOne + ', ') | Out-Null

      # L5. THE RAIL'S ADD SOMEONE, on the group of one: the add dialog for that group
      $layout = NmResize $app.Id 1720 1000
      Check ($layout -match 'rail=true') "L5: the window is wide enough for the inspector rail ($layout)"
      NmSettle
      Check ($null -ne (WaitName $app.Id 'Add someone to this conversation' 30)) 'L5: the rail offers Add someone to the founder of a group of one'
      Shot $app.Id 'L5-rail'
      LuInvoke $app.Id 'Add someone to this conversation' | Out-Null
      Check (WaitLog 'add someone was opened from the rail' 1 30) 'L5: the rail opened the add dialog'
      Check ($null -ne (WaitName $app.Id 'Paste their join code here' 30)) 'L5: the add dialog is up from the rail'
      Check ($null -eq (FindByName $app.Id 'Paste the invitation here')) 'L5: the join road is not drawn'
      Start-Sleep -Seconds 2
      Shot $app.Id 'L5-rail-add'
      DumpNames $app.Id 'L5-rail-add-uia'
      LuInvoke $app.Id 'Done' | Out-Null
      CloseApp $app
    }

    'list2' {
      # A. TWO CONVERSATIONS: the first by the file road, the second by the fresh join code the app
      #    publishes after that join, above epoch 1
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the first founder''s group'
      Check (WaitLog 'the join spent this device''s join code' 1 60) 'A: the join spent the join code, and a fresh one is published'
      Check (WaitLog '*** JOINED the group ***' 2 240) 'A: the app joined the second founder''s group, beside the first'
      Check (WaitFile 'list-ready' 120) 'A: the rig wrote both invitations'
      Check (WaitLog 'drawn: 2 conversation(s)' 1 120) 'A: the world holds two conversations'
      Start-Sleep -Seconds 8
      NmSettle
      # B. EACH ROW IS ITS OWN CONVERSATION'S, and opening one draws its own thread and epoch
      $state = NmToggleAdvanced $app.Id
      Check ($state -eq 'On') "B: Advanced Mode is on, for the strip's epoch ($state)"
      NmSettle
      Check ($null -ne (L2Row $app.Id $l2One)) "B: a row carries the first conversation's line"
      Check ($null -ne (L2Row $app.Id $l2Two)) "B: a row carries the second conversation's line"
      Shot $app.Id 'B-two-rows'
      DumpNames $app.Id 'B-two-rows-uia'
      $row = L2Row $app.Id $l2Two
      if ($null -ne $row) { $row.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
      Start-Sleep -Seconds 3
      Check (L2InThread $app.Id $l2Two) "B: the second conversation's thread draws its own line"
      Check (-not (L2InThread $app.Id $l2One)) "B: and not the first conversation's"
      $epoch = NmField $app.Id 'epoch'
      Note "B: the strip's epoch field: '$epoch'"
      Shot $app.Id 'B-second-open'
      DumpNames $app.Id 'B-second-open-uia'
      # C. A LINE SENT IN THE SECOND SHOWS IN THE SECOND ONLY
      $mine = 'A line this computer sent into the second conversation.'
      L2Send $app.Id $mine | Out-Null
      Check (WaitLog '*** SENT ***' 1 60) 'C: the line was sent'
      Start-Sleep -Seconds 3
      Check ($null -ne (L2Row $app.Id $mine)) "C: the second conversation's row now carries the line sent into it"
      Check ($null -ne (L2Row $app.Id $l2One)) "C: the first conversation's row still carries its own line"
      Shot $app.Id 'C-sent-in-second'
      # D. A LINE INTO THE FIRST, WHILE THE SECOND IS OPEN, REACHES ITS ROW WITHIN 8 S (the push road;
      #    a subscribed loop's own poll takes 15)
      Set-Content -LiteralPath (Join-Path $rigDir 'send-g1.go') -Value 'go'
      Check (WaitFile 'sent-g1.txt' 60) 'D: the rig sent a second line into the first conversation'
      $sentAt = Get-Date
      $arrived = $false
      while (((Get-Date) - $sentAt).TotalSeconds -lt 8) {
        if ($null -ne (L2Row $app.Id $l2Late)) { $arrived = $true; break }
        Start-Sleep -Milliseconds 400
      }
      $took = [int](((Get-Date) - $sentAt).TotalMilliseconds)
      Check $arrived "D: the first conversation's row carries the new line within 8 s, while the second is open ($took ms)"
      Check (-not (L2InThread $app.Id $l2Late)) 'D: and the open thread, the second conversation''s, does not draw it'
      Shot $app.Id 'D-line-into-first'
      # E. AN INVITATION INTO A CONVERSATION ALREADY HELD: "+", Join with an invitation, the first one
      LuMenu $app.Id 'Join with an invitation' | Out-Null
      Check ($null -ne (WaitName $app.Id 'Paste the invitation here' 30)) 'E: the join road is up beside the two conversations'
      Set-Named $app.Id 'Paste the invitation here' (B64 'invite1.bin') | Out-Null
      LuInvoke $app.Id 'Join' | Out-Null
      Check ($null -ne (WaitName $app.Id 'You are already in this group on this device.' 60)) 'E: the dialog says the device is already in that group'
      Shot $app.Id 'E-already-in'
      LuInvoke $app.Id 'Not now' | Out-Null
      CloseApp $app
      # F. BOTH CONVERSATIONS COME BACK AFTER A RELAUNCH
      $before = CountIn (LogText) 'restored an existing group from disk'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' ($before + 2) 240) 'F: the relaunch restored both groups'
      Check (WaitLog 'drawn: 2 conversation(s)' 2 120) 'F: and the world holds both conversations again'
      Start-Sleep -Seconds 6
      NmSettle
      Check ($null -ne (L2Row $app.Id $l2Late)) "F: the first conversation's row is back, with its newest line"
      Check ($null -ne (L2Row $app.Id $mine)) "F: the second conversation's row is back, with the line sent into it"
      Shot $app.Id 'F-both-back'
      CloseApp $app
    }

    'listrestore' {
      # A. TWO CONVERSATIONS, as list2 makes them
      $app = StartApp
      Check (WaitLog '*** JOINED the group ***' 2 300) 'A: the app joined both founders'' groups'
      Check (WaitFile 'list-ready' 120) 'A: the rig wrote both invitations'
      Check (WaitLog 'drawn: 2 conversation(s)' 1 120) 'A: the world holds two conversations'
      Start-Sleep -Seconds 8
      CloseApp $app
      # B. ANOTHER PROCESS HOLDS THE SECOND GROUP'S RECORD (R27's real lock)
      $g2 = (Get-Content -LiteralPath (Join-Path $rigDir 'g2.txt') -Raw).Trim()
      $meta2 = Join-Path (GroupDirOf $g2) 'meta'
      Check (Test-Path -LiteralPath $meta2) "B: the second group's record is on the disk: $meta2"
      StartHold 'meta2' @($meta2)
      # C. THE FIRST IS LIVE BESIDE THE NOTICE ROW
      $before = CountIn (LogText) 'restored an existing group from disk'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' ($before + 1) 240) 'C: the relaunch restored the first group'
      Check (WaitLog 'device_restore reported' 1 120) 'C: and its restore reported the second'
      Start-Sleep -Seconds 6
      NmSettle
      Check ($null -ne (L2Row $app.Id $l2One)) "C: the first conversation is live, its row and its line there"
      Check ($null -eq (L2Row $app.Id $l2Two)) 'C: the second is not drawn as a conversation'
      Check ($null -ne (WaitName $app.Id $say.NotReadNow 30)) "C: the notice above the list says, verbatim: $($say.NotReadNow)"
      Check ($null -eq (FindByName $app.Id 'Join a group')) 'C: no dialog stands over the conversation that restored'
      Shot $app.Id 'C-notice-beside-first'
      DumpNames $app.Id 'C-notice-beside-first-uia'
      CloseApp $app
      # D. THE LOCK LET GO: both come back, and no notice stands
      StopHold 'meta2'
      $before = CountIn (LogText) 'restored an existing group from disk'
      $app = StartApp
      Check (WaitLog 'restored an existing group from disk' ($before + 2) 240) 'D: the relaunch restored both groups'
      Start-Sleep -Seconds 6
      NmSettle
      Check ($null -ne (L2Row $app.Id $l2Two)) "D: the second conversation is back"
      Check ($null -eq (FindByName $app.Id $say.NotReadNow)) 'D: and the notice is gone'
      Shot $app.Id 'D-both-back'
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
  foreach ($path in @($script:denied)) { if (Test-Path -LiteralPath $path) { & icacls $path /remove:d $who 2>$null | Out-Null } }
  foreach ($tag in @($script:holds.Keys)) {
    Set-Content -LiteralPath (Join-Path $Work "hold-$tag.release") -Value 'release'
    $p = $script:holds[$tag]
    if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force }
  }
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
