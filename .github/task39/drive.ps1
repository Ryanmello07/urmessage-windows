# THROWAWAY, NEVER MERGED (message-windows ci/task39-shots on the owner's fork). Runs on a GitHub
# runner ONLY: it launches URmessage.exe, which nothing may do on the owner's machine.
#
# Hardening plan Task 39, Step 2: the two sentences, each screenshotted from the running app built
# on T1a's library, with a state directory damaged for the purpose. One leg per run:
#   epoch        the group's epoch states are gone, so it does not restore
#   record       the group's record is cut to half its length, so it does not read
#   notrestored  the group's directory cannot be listed, so Restore stops before it tries the
#                group: the leave answers ErrGroupNotRestored, the app restores first, and the
#                listing is given back before the leave is pressed, so the group comes back
#
# DRIVING: UI Automation patterns only (Invoke). No mouse or keyboard input is synthesized.
# CAPTURE: PrintWindow(PW_RENDERFULLCONTENT) of the app's own window, found by its process id.
param(
  [Parameter(Mandatory = $true)][ValidateSet('epoch', 'record', 'notrestored')][string]$Leg,
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Rig,
  [Parameter(Mandatory = $true)][string]$RigWorkDir,
  [Parameter(Mandatory = $true)][string]$Work,
  [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Task39Shot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][Task39Shot]::SetThreadDpiAwarenessContext([IntPtr](-4))

$script:failures = 0
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++ }
}

New-Item -ItemType Directory -Force $Work, $Out | Out-Null
$root = Join-Path $Work 'app'
$rigDir = Join-Path $Work 'rig'
$handshake = Join-Path $root 'dev'
New-Item -ItemType Directory -Force $root, $rigDir, $handshake | Out-Null
$cred = Join-Path $Work 'ci-not-a-credential.jwt'
Set-Content -LiteralPath $cred -Value 'ci-not-a-credential (the DIRECT route reads no credential)' -Encoding ascii

# ---- the rig: the server and the founder --------------------------------------------------------
$env:TASK39_RIG_DIR = $rigDir
$env:TASK39_HANDSHAKE_DIR = $handshake
$env:TASK39_RIG_ONCE = if ($Leg -eq 'notrestored') { '1' } else { '0' }
$rigProc = Start-Process -FilePath $Rig -ArgumentList '-test.run', '^TestTask39Rig$', '-test.v', '-test.timeout', '0' `
  -WorkingDirectory $RigWorkDir -RedirectStandardOutput (Join-Path $Out 'rig.out.txt') `
  -RedirectStandardError (Join-Path $Out 'rig.err.txt') -PassThru -NoNewWindow
$endpointFile = Join-Path $rigDir 'endpoint.txt'
$deadline = (Get-Date).AddSeconds(180)
while (-not (Test-Path -LiteralPath $endpointFile)) {
  if ((Get-Date) -gt $deadline -or $rigProc.HasExited) { throw "the rig did not publish its endpoint (exited: $($rigProc.HasExited))" }
  Start-Sleep -Milliseconds 500
}
Set-Content -LiteralPath (Join-Path $rigDir 'gate-open') -Value 'open'
$endpointLines = @(Get-Content -LiteralPath $endpointFile | Where-Object { $_ -ne '' })
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
  $r = New-Object Task39Shot+RECT
  [void][Task39Shot]::GetWindowRect($hwnd, [ref]$r)
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [Task39Shot]::PrintWindow($hwnd, $hdc, 2)
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
function DumpNames([int]$processId, [string]$file) {
  $win = AppWindow $processId
  $all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  $lines = foreach ($e in $all) {
    try { "{0}`t{1}" -f $e.Current.ControlType.ProgrammaticName, $e.Current.Name } catch { }
  }
  $lines | Set-Content -LiteralPath (Join-Path $Out "$Leg-$file.txt") -Encoding utf8
}
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

$sentence = 'This device still holds an old copy of this group that it cannot open. Leave it, and then use this invitation again.'
$buttonName = 'Leave the old copy of this group'

try {
  # ---- A: join the group, so the device holds it ----------------------------------------------
  $app = StartApp
  Check (WaitLog '*** JOINED the group ***' 1 240) 'A: the app joined the founder''s group'
  Start-Sleep -Seconds 10
  Shot $app.Id 'A-joined'
  CloseApp $app

  # ---- B: damage the state directory, with the app stopped ------------------------------------
  $gidHex = (Get-Content -LiteralPath (Join-Path $rigDir 'gid.txt') -Raw).Trim()
  $gid = New-Object byte[] 32
  for ($i = 0; $i -lt 32; $i++) { $gid[$i] = [Convert]::ToByte($gidHex.Substring($i * 2, 2), 16) }
  $sha = [Security.Cryptography.SHA256]::Create()
  $name = (($sha.ComputeHash($gid) | ForEach-Object { $_.ToString('x2') }) -join '')
  $stateDir = Join-Path $root 'live\state'
  $groupFiles = @(Get-ChildItem -LiteralPath $stateDir -Recurse -File | Where-Object { $_.FullName -like "*\$name\*" })
  Note "group $gidHex is directory $name; $($groupFiles.Count) file(s) under it:"
  $groupFiles | ForEach-Object { Note "  $($_.FullName.Substring($stateDir.Length)) ($($_.Length) octets)" }
  $groupDir = $null
  switch ($Leg) {
    'epoch' {
      $epochs = @($groupFiles | Where-Object { $_.FullName -like "*\$name\epoch\*" })
      Check ($epochs.Count -ge 1) "B: the group has epoch states to remove ($($epochs.Count))"
      $epochs | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
      $left = @(Get-ChildItem -LiteralPath $stateDir -Recurse -File | Where-Object { $_.FullName -like "*\$name\epoch\*" })
      Check ($left.Count -eq 0) 'B: no epoch state of the group is left'
      $meta = @($groupFiles | Where-Object { $_.Name -eq 'meta' -and $_.Directory.Name -eq $name })
      Check ($meta.Count -eq 1) 'B: the group record is still there, whole'
    }
    'record' {
      $meta = @($groupFiles | Where-Object { $_.Name -eq 'meta' -and $_.Directory.Name -eq $name })
      Check ($meta.Count -eq 1) 'B: exactly one group record'
      $bytes = [IO.File]::ReadAllBytes($meta[0].FullName)
      $half = New-Object byte[] ([int][Math]::Floor($bytes.Length / 2))
      [Array]::Copy($bytes, $half, $half.Length)
      [IO.File]::WriteAllBytes($meta[0].FullName, $half)
      Check ((Get-Item -LiteralPath $meta[0].FullName).Length -eq $half.Length) "B: the record is cut from $($bytes.Length) to $($half.Length) octets"
    }
    'notrestored' {
      # NOTHING IS CHANGED ON THE DISK HERE. The store walks its whole directory when it opens, so
      # a directory it cannot list stops the app there; the listing is taken away in C, after the
      # store is open and before the first restore, while the rig's gate holds the connect back.
      $named = Get-ChildItem -LiteralPath $stateDir -Recurse -Directory | Where-Object { $_.Name -eq $name } | Select-Object -First 1
      $groupDir = $named.Parent.FullName
      $groupMeta = Join-Path $named.FullName 'meta'
      Check (Test-Path -LiteralPath $groupMeta) "B: the group's record is at $groupMeta"
      Remove-Item -LiteralPath (Join-Path $rigDir 'gate-open') -Force
      Note 'B: the rig''s gate is shut'
    }
  }

  # ---- C: the app again, the invitation, the refusal, the leave --------------------------------
  $app = StartApp
  if ($Leg -eq 'notrestored') {
    Check (WaitLog 'device built; durable state in' 2 120) 'C: the app opened its state store (and is held at connect)'
    $who = "$($env:USERDOMAIN)\$($env:USERNAME)"
    & icacls $groupDir /deny "${who}:(RD)" | ForEach-Object { Note "icacls: $_" }
    $listed = $true
    try { [void][IO.Directory]::GetDirectories($groupDir) } catch { $listed = $false }
    Check (-not $listed) "C: $groupDir cannot be listed"
    $readable = $true
    try { [void][IO.File]::ReadAllBytes($groupMeta) } catch { $readable = $false }
    Check $readable 'C: the group record itself still reads'
    Check ((CountIn (LogText) 'CONNECTED to the alpha mesh') -eq 1) 'C: the app has not connected yet in this launch'
    Set-Content -LiteralPath (Join-Path $rigDir 'gate-open') -Value 'open'
    Note 'C: the rig''s gate is open'
  }
  Check (WaitLog 'published this device''s key package' 2 240) 'C: the app holds no live group and waits for an invitation'
  if ($Leg -eq 'notrestored') {
    # THE SAME INVITATION AS IN A: the gate refuses before the Welcome is opened
    $first = [IO.File]::ReadAllBytes((Join-Path $rigDir 'invite1.bin'))
    $partial = Join-Path $handshake 'app.invite.partial'
    [IO.File]::WriteAllBytes($partial, $first)
    Move-Item -LiteralPath $partial -Destination (Join-Path $handshake 'app.invite') -Force
    Note 'C: the invitation from A is back in the handshake directory'
  }
  Check (WaitLog 'is not live; the leave is offered' 1 240) 'C: the join was refused over a held group that is not live'
  Start-Sleep -Seconds 6
  Shot $app.Id 'C1-refused'
  DumpNames $app.Id 'C1-uia'
  $text = FindByName $app.Id $sentence
  Check ($null -ne $text) 'C: the window carries the sentence, verbatim'
  $button = FindByName $app.Id $buttonName
  Check ($null -ne $button) 'C: the window carries the leave button'
  if ($Leg -eq 'notrestored') {
    & icacls $groupDir /remove:d "$($env:USERDOMAIN)\$($env:USERNAME)" | ForEach-Object { Note "icacls: $_" }
    $listed = $true
    try { [void][IO.Directory]::GetDirectories($groupDir) } catch { $listed = $false }
    Check $listed "C: $groupDir can be listed again"
  }
  if ($null -ne $button) {
    $invoke = $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    $invoke.Invoke()
    Note 'C: the leave button was invoked (InvokePattern)'
  }
  if ($Leg -eq 'notrestored') {
    Check (WaitLog 'restoring first' 1 120) 'C: the leave answered ErrGroupNotRestored, and the restore ran'
    Check (WaitLog '*** RESTORED ***' 1 120) 'C: the group came back'
    Start-Sleep -Seconds 4
    Shot $app.Id 'C2-after-leave'
  } else {
    Check (WaitLog '*** LEFT *** the old copy' 1 120) 'C: the old copy was left'
    Start-Sleep -Seconds 1
    Shot $app.Id 'C2-after-leave'
    Check (WaitLog '*** JOINED the group ***' 2 120) 'C: the same invitation was joined again'
  }
  Start-Sleep -Seconds 12
  Shot $app.Id 'C3-in-the-group'
  DumpNames $app.Id 'C3-uia'
  CloseApp $app
} catch {
  Note "DRIVER ERROR: $_"
  $script:failures++
} finally {
  if ($null -ne $groupDir) { & icacls $groupDir /remove:d "$($env:USERDOMAIN)\$($env:USERNAME)" 2>$null | Out-Null }
  Set-Content -LiteralPath (Join-Path $rigDir 'stop') -Value 'stop'
  if (-not $rigProc.WaitForExit(20000)) { Stop-Process -Id $rigProc.Id -Force }
  Get-Process URmessage -ErrorAction SilentlyContinue | Stop-Process -Force
  New-Item -ItemType Directory -Force (Join-Path $Out 'logs') | Out-Null
  Copy-Item -Path (Join-Path $root 'logs\*') -Destination (Join-Path $Out 'logs') -ErrorAction SilentlyContinue
}
Note "$script:failures check(s) failed"
if ($script:failures -gt 0) { exit 1 }
