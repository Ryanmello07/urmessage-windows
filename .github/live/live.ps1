# THROWAWAY, NEVER MERGED (message-windows ci/list-live* on the owner's fork). Runs on a GitHub
# runner ONLY: it launches URmessage.exe and sdk/livepeer, which nothing may do on the owner's machine.
#
# THE CONVERSATION LIST'S LIVE RUN (lead ruling R67). Both parties on this runner, by the DIRECT
# route, with no account:
#   - the app AS BUILT (no CI endpoint patch), with CI's placeholder credential file and
#     URMESSAGE_ROUTE=direct, so it dials the compiled-in alpha endpoint with its compiled pin;
#   - sdk/livepeer, built from message 03192c2b's sdk/livepeer on the library at the app's own pin,
#     run with -route direct -endpoint <the app's endpoint> -pin <the app's pin> and a state folder
#     of its own for each conversation.
# The codes and invitations are relayed between the two on this runner, through files outside the
# workspace, on both roads: livepeer founds and the app joins (A and C), the app founds and livepeer
# joins (B). Only a code's length and a digest are ever printed, and the artifact is scanned for
# every code relayed before it is uploaded.
# ONE LIVEPEER AT A TIME (the lead's standing rule, "never start a second livepeer run while one is
# live"): the app holds two and then three conversations at once, and each peer talks inside its own
# while the others stand.
#
# DRIVING: UI Automation patterns only (Invoke, Value, Transform, Window). No mouse or keyboard
# input is synthesized. CAPTURE: PrintWindow(PW_RENDERFULLCONTENT) of the app's own window. No shot
# is taken while a dialog shows a code.
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Peer,
  [Parameter(Mandatory = $true)][string]$Endpoint,
  [Parameter(Mandatory = $true)][string]$Pin,
  [Parameter(Mandatory = $true)][string]$ServerId,
  [Parameter(Mandatory = $true)][string]$Work,
  [Parameter(Mandatory = $true)][string]$Out
)
$Leg = 'live'
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


$say = @{
  NotReadNow = 'This conversation could not be read just now. Close anything else that may be using URmessage''s folder, then open URmessage again.'
}

New-Item -ItemType Directory -Force $Work, $Out | Out-Null
$root = Join-Path $Work 'app'
$peers = Join-Path $Work 'peers'
New-Item -ItemType Directory -Force $root, $peers | Out-Null
# CI's placeholder credential file, as R60 names it: the app needs a file to start, and the DIRECT
# route reads no credential. livepeer is given the same file: it reads a path and prints only its
# length, and its DIRECT route hands the text to no one.
$cred = Join-Path $Work 'ci-not-a-credential.jwt'
Set-Content -LiteralPath $cred -Value 'ci-not-a-credential (the DIRECT route reads no credential)' -Encoding ascii

# ---- the app: as built, on the DIRECT route ------------------------------------------------------
$env:URMESSAGE_APP_ROOT = $root
$env:URMESSAGE_LIVE_JWT = $cred
$env:URMESSAGE_ROUTE = 'direct'
# NO CI ENDPOINT: the build carries no patch that reads these, and they are cleared all the same
Remove-Item Env:URMESSAGE_CI_ENDPOINT -ErrorAction SilentlyContinue
Remove-Item Env:URMESSAGE_CI_PIN -ErrorAction SilentlyContinue
Remove-Item Env:URMESSAGE_LIVE_HANDSHAKE_DIR -ErrorAction SilentlyContinue
Note "the app: $Exe, root $root, route direct, credential file $cred (placeholder)"
Note "the alpha, as the app's source names it: $Endpoint, pin $Pin, server $ServerId"

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
# A LIST ROW BY ITS PREVIEW: a row is named "<name>[, N members], <preview>, <time>[. Selected]", so the
# preview stands between two ", " in it; a bubble is named "[<sender>, ]<time>. <text>", with its text
# last.
function L2Row([int]$processId, [string]$preview) {
  foreach ($e in (NmAll $processId)) {
    try {
      if ($e.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and
          ([string]$e.Current.Name).Contains(', ' + $preview + ', ')) { return $e }
    } catch { }
  }
  return $null
}
# A LINE IN THE OPEN THREAD: an element named by it, or a bubble whose name ends with it, and not a
# list row (whose preview stands between two ", ").
function L2InThread([int]$processId, [string]$text) {
  foreach ($e in (NmAll $processId)) {
    $n = NmNameOf $e
    if ($n.Contains(', ' + $text + ', ')) { continue }
    if ($n -eq $text -or $n.EndsWith('. ' + $text)) { return $true }
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
  # A DARK SEND IS NOT PRESSED: Invoke on a disabled button throws, and the check that follows the
  # send (its log line) is what says it was not sent
  if (-not $send.Current.IsEnabled) { Note "L2: the Send button is dark, so nothing is sent"; return $false }
  $send.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  Note "sent '$text' (ValuePattern, InvokePattern)"
  return $true
}

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


# ---- livepeer: ONE run at a time, each with a state folder of its own -----------------------------
# (THE PROCESS IS $script:peerProc: PowerShell's names ignore case, so $script:peer WOULD BE the -Peer
# parameter, the exe's path, which the first run of this driver found out the hard way)
$script:peerProc = $null
$script:peerName = ''
$script:peerOut = ''
$script:peerErr = ''
$script:peerOutbox = ''
$script:peerRuns = @{}
# EVERY CODE AND INVITATION RELAYED, kept in memory only, so that the artifact can be scanned for
# them before it is uploaded. None is ever printed.
$script:codes = New-Object 'System.Collections.Generic.List[string]'

function ArgLine([string[]]$a) {
  return (($a | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' ')
}
function StartPeer([string]$name, [string[]]$road, [int]$serveSeconds) {
  if ($null -ne $script:peerProc -and -not $script:peerProc.HasExited) {
    throw "livepeer $($script:peerName) is still live: never two livepeer runs at once"
  }
  $others = @(Get-Process -Name livepeer -ErrorAction SilentlyContinue)
  Check ($others.Count -eq 0) "peer ${name}: no other livepeer is running when it starts ($($others.Count))"
  $run = 1
  if ($script:peerRuns.ContainsKey($name)) { $run = $script:peerRuns[$name] + 1 }
  $script:peerRuns[$name] = $run
  $dir = Join-Path $peers $name
  New-Item -ItemType Directory -Force $dir | Out-Null
  # A FRESH OUTBOX PER RUN: a run sends a file's whole text from its start
  $outbox = Join-Path $peers "$name-outbox-$run.txt"
  [IO.File]::WriteAllText($outbox, '')
  $a = @('-jwt', $cred, '-dir', $dir, '-route', 'direct', '-endpoint', $Endpoint, '-pin', $Pin, '-server', $ServerId,
         '-outbox', $outbox, '-quiet', '-serve', "$($serveSeconds)s", '-poll', '2s') + $road
  $stdout = Join-Path $Out "peer-$name-run$run.out.txt"
  $stderr = Join-Path $Out "peer-$name-run$run.err.txt"
  $line = ArgLine $a
  Note "peer $name run ${run}: livepeer $line"
  $p = Start-Process -FilePath $Peer -ArgumentList $line -WorkingDirectory $peers `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -NoNewWindow
  $null = $p.Handle
  $script:peerProc = $p
  $script:peerName = $name
  $script:peerOut = $stdout
  $script:peerErr = $stderr
  $script:peerOutbox = $outbox
  return $run
}
function PeerText {
  $text = ''
  foreach ($f in @($script:peerOut, $script:peerErr)) {
    if ($f -eq '' -or -not (Test-Path -LiteralPath $f)) { continue }
    try {
      $fs = [IO.File]::Open($f, 'Open', 'Read', 'ReadWrite')
      $sr = New-Object IO.StreamReader($fs)
      $text += $sr.ReadToEnd()
      $sr.Close()
    } catch { }
  }
  return $text
}
function WaitPeer([string]$needle, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    if ((PeerText).Contains($needle)) { Note "peer $($script:peerName): '$needle'"; return $true }
    if ($null -eq $script:peerProc -or $script:peerProc.HasExited) { break }
    Start-Sleep -Milliseconds 500
  }
  if ((PeerText).Contains($needle)) { Note "peer $($script:peerName): '$needle'"; return $true }
  Note "peer $($script:peerName): TIMED OUT (or exited) waiting $seconds s for '$needle'"
  return $false
}
# A LINE THE PEER SENDS: appended to its outbox, which it reads by offset every poll
function PeerSays([string]$text) {
  [IO.File]::AppendAllText($script:peerOutbox, $text + "`n")
  Note "peer $($script:peerName): outbox <- '$text'"
}
# WHAT livepeer PRINTS: "-> sent record N in Xms: <quoted>" for a line it sent, and
# "<- record N from <sender> kind=.. deleted=.. reply_to=-: <quoted>" for a line it received
function PeerSent([string]$text) { return 'ms: "' + $text + '"' }
function PeerGot([string]$text) { return 'reply_to=-: "' + $text + '"' }
function PeerGroupId {
  $m = [regex]::Match((PeerText), 'group ([0-9a-f]{64}) at epoch')
  if ($m.Success) { return $m.Groups[1].Value }
  return ''
}
function WaitPeerExit([int]$seconds) {
  $p = $script:peerProc
  if ($null -eq $p) { return }
  $name = $script:peerName
  if (-not $p.WaitForExit($seconds * 1000)) {
    Note "peer $name did not finish within $seconds s; stopping it"
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit(10000) | Out-Null
    Check $false "peer ${name}: the run finished its serve on its own"
  } else {
    Check ($p.ExitCode -eq 0) "peer ${name}: the run ended cleanly after its serve (exit $($p.ExitCode))"
  }
  foreach ($l in @((PeerText) -split "`n" | Where-Object { $_ -match 'fetched=|whole log holds|FAIL:' })) { Note "peer ${name}: $($l.Trim())" }
  $script:peerProc = $null
}
function WaitPath([string]$path, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 0) { return $true }
    if ($null -ne $script:peerProc -and $script:peerProc.HasExited) { break }
    Start-Sleep -Milliseconds 500
  }
  return ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 0)
}
# A CODE THE PEER WROTE, taken off the disk: read, kept for the scan, the file deleted at once
function TakeCode([string]$path, [string]$what) {
  Start-Sleep -Milliseconds 500
  $text = ([IO.File]::ReadAllText($path)).Trim()
  $script:codes.Add($text)
  Remove-Item -LiteralPath $path -Force
  Note "relay: $what, $($text.Length) characters, sha256 $(Sha $text)..., its file deleted"
  return $text
}
# A CODE FOR THE PEER, written whole (a temp file and a rename) where it waits for it
function GiveCode([string]$text, [string]$path, [string]$what) {
  $script:codes.Add($text)
  $temp = "$path.partial"
  [IO.File]::WriteAllText($temp, $text)
  Move-Item -LiteralPath $temp -Destination $path -Force
  Note "relay: $what, $($text.Length) characters, sha256 $(Sha $text)..., written to $(Split-Path -Leaf $path)"
}
function WaitNameGone([int]$processId, [string]$name, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  do {
    $e = FindByName $processId $name
    if ($null -eq $e) { return $null }
    Start-Sleep -Milliseconds 500
  } while ((Get-Date) -lt $deadline)
  return (FindByName $processId $name)
}
function WaitRow([int]$processId, [string]$preview, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  do {
    if ($null -ne (L2Row $processId $preview)) { return $true }
    Start-Sleep -Milliseconds 700
  } while ((Get-Date) -lt $deadline)
  return $false
}
function OpenRow([int]$processId, [string]$preview) {
  $row = L2Row $processId $preview
  if ($null -eq $row) { Check $false "a row carries '$preview' to open"; return $false }
  $row.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
  Note "opened the row '$($row.Current.Name)' (InvokePattern)"
  Start-Sleep -Seconds 3
  return $true
}
# THE APP SENDS A LINE INTO THE OPEN CONVERSATION, and the peer of that conversation, live, gets it
function AppSends([int]$processId, [string]$text, [string]$what, [bool]$peerLive) {
  $sent = CountIn (LogText) '*** SENT ***'
  L2Send $processId $text | Out-Null
  Check (WaitLog '*** SENT ***' ($sent + 1) 90) "${what}: the app sent its line"
  if ($peerLive) { Check (WaitPeer (PeerGot $text) 120) "${what}: livepeer $($script:peerName) received the app's line, live" }
}
# THE PEER SAYS A LINE, and the app draws it in that conversation's row
function PeerTalks([int]$processId, [string]$text, [string]$what) {
  PeerSays $text
  Check (WaitPeer (PeerSent $text) 90) "${what}: livepeer $($script:peerName) sent its line"
  Check (WaitRow $processId $text 120) "${what}: the app's row for that conversation carries the peer's line"
}
$joinBox = 'Your join code'
$pasteInvite = 'Paste the invitation here'
$pasteJoinCode = 'Paste their join code here'
$inviteBox = 'The invitation to send them'

try {
  # ==== P1. THE FOUNDER ROAD: livepeer A founds a group with the app's join code; the app joins ====
  $app = StartApp
  Check (WaitLog "live: route direct to $Endpoint, key pinned" 1 120) "P1: the app dials the alpha's own endpoint on the DIRECT route, its key pinned ($Endpoint)"
  Check ((CountIn (LogText) 'CI ONLY') -eq 0) 'P1: this build carries no CI endpoint (no "CI ONLY" line in its log)'
  Check (WaitLog 'published this device''s key package' 1 180) 'P1: the app holds no group, and published its join code'
  Check ($null -ne (WaitName $app.Id $joinBox 60)) 'P1: the join dialog is up, with the join code'
  $code1 = ReadValue $app.Id $joinBox
  Check ($code1.Length -gt 1000) "P1: the join dialog shows this device's join code ($($code1.Length) characters)"
  GiveCode $code1 (Join-Path $peers 'A-joincode.txt') "the app's join code, to livepeer A"
  StartPeer 'A' @('-joincode', ('@' + (Join-Path $peers 'A-joincode.txt')), '-invitecodeout', (Join-Path $peers 'A-invite.txt')) 300 | Out-Null
  Check (WaitPath (Join-Path $peers 'A-invite.txt') 240) 'P1: livepeer A founded a group with the app''s join code, and made its invitation'
  Remove-Item -LiteralPath (Join-Path $peers 'A-joincode.txt') -Force -ErrorAction SilentlyContinue
  $gid1 = PeerGroupId
  Note "P1: livepeer A's group is $gid1"
  $inviteA = TakeCode (Join-Path $peers 'A-invite.txt') "livepeer A's invitation, to the app"
  Set-Named $app.Id $pasteInvite $inviteA | Out-Null
  LuInvoke $app.Id 'Join' | Out-Null
  Check (WaitLog '*** JOINED the group ***' 1 180) 'P1: the app joined livepeer A''s group from the invitation'
  Check (WaitLog 'the join spent this device''s join code' 1 90) 'P1: the join spent the join code, and the road made a fresh one'
  Check ($null -eq (WaitNameGone $app.Id $joinBox 30)) 'P1: the join closed the dialog'
  $a1 = 'Line one of the first live conversation, from livepeer A.'
  PeerTalks $app.Id $a1 'P1'
  OpenRow $app.Id $a1 | Out-Null
  Check (L2InThread $app.Id $a1) 'P1: the first conversation''s thread draws livepeer A''s line'
  $m1 = 'A line this computer sent into the first live conversation.'
  AppSends $app.Id $m1 'P1 (commit 3, the send names its group)' $true
  Start-Sleep -Seconds 2
  Shot $app.Id 'P1-first-live'
  DumpNames $app.Id 'P1-first-live-uia'
  WaitPeerExit 360

  # ==== P2. THE JOIN ROAD: the app founds a second conversation, and livepeer B joins it ============
  StartPeer 'B' @('-join', '-joincodeout', (Join-Path $peers 'B-joincode.txt'), '-invitecode', ('@' + (Join-Path $peers 'B-invite.txt')), '-wait', '5m') 300 | Out-Null
  Check (WaitPath (Join-Path $peers 'B-joincode.txt') 180) 'P2: livepeer B made its own join code'
  $codeB = TakeCode (Join-Path $peers 'B-joincode.txt') "livepeer B's join code, to the app"
  $created = CountIn (LogText) '*** GROUP CREATED ***'
  LuMenu $app.Id 'New conversation' | Out-Null
  Check (WaitLog '*** GROUP CREATED ***' ($created + 1) 120) 'P2: the app founded a second conversation beside the first'
  Set-Named $app.Id $pasteJoinCode $codeB | Out-Null
  $added = CountIn (LogText) '*** MEMBER ADDED ***'
  LuInvoke $app.Id 'Add this person to the group' | Out-Null
  Check (WaitLog '*** MEMBER ADDED ***' ($added + 1) 180) 'P2: the app added livepeer B, which opens the second conversation'
  $inviteB = ReadValue $app.Id $inviteBox
  Check ($inviteB.Length -gt 200) "P2: the invitation for livepeer B is on screen ($($inviteB.Length) characters)"
  GiveCode $inviteB (Join-Path $peers 'B-invite.txt') "the app's invitation, to livepeer B"
  Check (WaitPeer 'joined group ' 240) 'P2: livepeer B joined the app''s group from the invitation'
  Check (-not (Test-Path -LiteralPath (Join-Path $peers 'B-invite.txt'))) 'P2: livepeer B deleted the invitation''s file once it had read it'
  $gid2 = PeerGroupId
  Note "P2: the app's group, as livepeer B names it, is $gid2"
  LuInvoke $app.Id 'Done' | Out-Null
  Start-Sleep -Seconds 2
  $b1 = 'Line one of the second live conversation, from livepeer B.'
  PeerTalks $app.Id $b1 'P2'
  Check ($null -ne (L2Row $app.Id $m1)) 'P2: the first conversation''s row still carries its own newest line'
  OpenRow $app.Id $b1 | Out-Null
  Check (L2InThread $app.Id $b1) 'P2: the second conversation''s thread draws livepeer B''s line'
  Check (-not (L2InThread $app.Id $m1)) 'P2: and not the first conversation''s'
  $m2 = 'A line this computer sent into the second live conversation.'
  AppSends $app.Id $m2 'P2 (commit 3, the send names its group)' $true
  Check (WaitLog 'drawn: 2 conversation(s)' 1 60) 'P2: the world holds two conversations'
  Start-Sleep -Seconds 2
  Shot $app.Id 'P2-two-live'
  DumpNames $app.Id 'P2-two-live-uia'
  WaitPeerExit 360

  # ==== P3. A THIRD, BY THE JOIN ROAD THAT STAYS OPEN: "+", Join with an invitation; livepeer C founds =
  LuMenu $app.Id 'Join with an invitation' | Out-Null
  Check ($null -ne (WaitName $app.Id $joinBox 60)) 'P3: the join road is up beside the two conversations'
  $code3 = ReadValue $app.Id $joinBox
  Check (($code3.Length -gt 1000) -and ($code3 -ne $code1)) "P3: it shows a fresh join code, not the one the first join spent ($($code3.Length) characters)"
  GiveCode $code3 (Join-Path $peers 'C-joincode.txt') "the app's fresh join code, to livepeer C"
  StartPeer 'C' @('-joincode', ('@' + (Join-Path $peers 'C-joincode.txt')), '-invitecodeout', (Join-Path $peers 'C-invite.txt')) 300 | Out-Null
  Check (WaitPath (Join-Path $peers 'C-invite.txt') 240) 'P3: livepeer C founded a group with that join code, and made its invitation'
  Remove-Item -LiteralPath (Join-Path $peers 'C-joincode.txt') -Force -ErrorAction SilentlyContinue
  $gid3 = PeerGroupId
  Note "P3: livepeer C's group is $gid3"
  $inviteC = TakeCode (Join-Path $peers 'C-invite.txt') "livepeer C's invitation, to the app"
  Set-Named $app.Id $pasteInvite $inviteC | Out-Null
  LuInvoke $app.Id 'Join' | Out-Null
  Check (WaitLog '*** JOINED the group ***' 2 180) 'P3: the app joined livepeer C''s group, beside the two'
  Check ($null -eq (WaitNameGone $app.Id $joinBox 30)) 'P3: the join closed the dialog'
  $c1 = 'Line one of the third live conversation, from livepeer C.'
  PeerTalks $app.Id $c1 'P3'
  Check ($null -ne (L2Row $app.Id $m1)) 'P3: the first conversation''s row still carries its own newest line'
  Check ($null -ne (L2Row $app.Id $m2)) 'P3: the second conversation''s row still carries its own newest line'
  OpenRow $app.Id $c1 | Out-Null
  $m3 = 'A line this computer sent into the third live conversation.'
  AppSends $app.Id $m3 'P3 (commit 3, the send names its group)' $true
  Check (WaitLog 'drawn: 3 conversation(s)' 1 60) 'P3: the world holds three conversations'
  Start-Sleep -Seconds 2
  Shot $app.Id 'P3-three-live'
  DumpNames $app.Id 'P3-three-live-uia'
  WaitPeerExit 360
  CloseApp $app

  # ==== P4. THE THIRD'S RECORD HELD BY ANOTHER PROCESS: two live conversations and the notice row ===
  Check ($gid3 -match '^[0-9a-f]{64}$') "P4: livepeer C named the third group ($gid3)"
  $meta3 = Join-Path (GroupDirOf $gid3) 'meta'
  Check (Test-Path -LiteralPath $meta3) "P4: the third group's record is on the app's disk: $meta3"
  StartHold 'g3' @($meta3)
  $restored = CountIn (LogText) 'restored an existing group from disk'
  $reported = CountIn (LogText) 'device_restore reported'
  $app = StartApp
  Check (WaitLog 'restored an existing group from disk' ($restored + 2) 240) 'P4: the relaunch restored the first and the second'
  Check (WaitLog 'device_restore reported' ($reported + 1) 120) 'P4: and its restore reported the third, which another process holds'
  Start-Sleep -Seconds 6
  NmSettle
  Check ($null -ne (L2Row $app.Id $m1)) 'P4: the first conversation is drawn, with its newest line'
  Check ($null -ne (L2Row $app.Id $m2)) 'P4: the second conversation is drawn, with its newest line'
  Check ($null -eq (L2Row $app.Id $m3)) 'P4: the third is not drawn as a conversation'
  Check ($null -ne (WaitName $app.Id $say.NotReadNow 30)) "P4: the notice row says, verbatim: $($say.NotReadNow)"
  Check ($null -eq (FindByName $app.Id 'Join a group')) 'P4: no dialog stands over the conversations that restored'
  Shot $app.Id 'P4-two-live-and-notice'
  DumpNames $app.Id 'P4-two-live-and-notice-uia'
  # BOTH RESTORED CONVERSATIONS ARE LIVE, both ways, beside the notice: each peer restarts over its
  # own folder (a restored group; nothing is founded or joined again)
  StartPeer 'A' @('-joincode', ('@' + (Join-Path $peers 'A-joincode.txt')), '-invitecodeout', (Join-Path $peers 'A-invite.txt')) 240 | Out-Null
  Check (WaitPeer 'restored group ' 180) 'P4: livepeer A restored its group over its own folder'
  $a2 = 'A line into the first conversation, after the app restarted.'
  PeerTalks $app.Id $a2 'P4 (first)'
  OpenRow $app.Id $a2 | Out-Null
  $m4 = 'A line this computer sent into the first, after its restart.'
  AppSends $app.Id $m4 'P4 (first)' $true
  WaitPeerExit 300
  StartPeer 'B' @('-join', '-joincodeout', (Join-Path $peers 'B-joincode.txt'), '-invitecode', ('@' + (Join-Path $peers 'B-invite.txt')), '-wait', '30s') 240 | Out-Null
  Check (WaitPeer 'restored group ' 180) 'P4: livepeer B restored its group over its own folder'
  $b2 = 'A line into the second conversation, after the app restarted.'
  PeerTalks $app.Id $b2 'P4 (second)'
  OpenRow $app.Id $b2 | Out-Null
  $m5 = 'A line this computer sent into the second, after its restart.'
  AppSends $app.Id $m5 'P4 (second)' $true
  Check ($null -ne (FindByName $app.Id $say.NotReadNow)) 'P4: the notice row still stands beside the two'
  Start-Sleep -Seconds 2
  Shot $app.Id 'P4-both-talk-beside-the-notice'
  WaitPeerExit 300
  CloseApp $app
  StopHold 'g3'

  # ==== P5. ALL THREE BACK; THE APP LEAVES THE THIRD; THE OTHER TWO GO ON ===========================
  $restored = CountIn (LogText) 'restored an existing group from disk'
  $app = StartApp
  Check (WaitLog 'restored an existing group from disk' ($restored + 3) 240) 'P5: with the record let go, the relaunch restored all three'
  Start-Sleep -Seconds 6
  NmSettle
  Check ($null -ne (L2Row $app.Id $m4)) 'P5: the first conversation is back'
  Check ($null -ne (L2Row $app.Id $m5)) 'P5: the second conversation is back'
  Check ($null -ne (L2Row $app.Id $m3)) 'P5: the third conversation is back'
  Check ($null -eq (FindByName $app.Id $say.NotReadNow)) 'P5: and the notice is gone'
  Shot $app.Id 'P5-three-back'
  $layout = NmResize $app.Id 1720 1000
  Check ($layout -match 'rail=true') "P5: the window is wide enough for the inspector rail ($layout)"
  OpenRow $app.Id $m3 | Out-Null
  $left = CountIn (LogText) '*** LEFT ***'
  $goOn = CountIn (LogText) 'the other conversations go on'
  LuInvoke $app.Id 'Delete this conversation from this computer and leave it' | Out-Null
  Start-Sleep -Seconds 2
  LuInvoke $app.Id 'Delete and leave' | Out-Null
  Check (WaitLog '*** LEFT ***' ($left + 1) 120) 'P5: the app left the third conversation (commit 3, the leave names its group)'
  Check (WaitLog 'the other conversations go on' ($goOn + 1) 60) 'P5: and the other conversations go on'
  Start-Sleep -Seconds 4
  Check ($null -eq (L2Row $app.Id $m3)) 'P5: the third conversation''s row is gone'
  Check ($null -ne (L2Row $app.Id $m4)) 'P5: the first conversation''s row is still there'
  Check ($null -ne (L2Row $app.Id $m5)) 'P5: the second conversation''s row is still there'
  Shot $app.Id 'P5-third-left'
  StartPeer 'A' @('-joincode', ('@' + (Join-Path $peers 'A-joincode.txt')), '-invitecodeout', (Join-Path $peers 'A-invite.txt')) 240 | Out-Null
  Check (WaitPeer 'restored group ' 180) 'P5: livepeer A restored its group again'
  OpenRow $app.Id $m4 | Out-Null
  $m6 = 'A line this computer sent into the first, after leaving the third.'
  AppSends $app.Id $m6 'P5 (first, after the leave)' $true
  $a3 = 'A line into the first conversation, after the app left the third.'
  PeerTalks $app.Id $a3 'P5 (first, after the leave)'
  WaitPeerExit 300
  StartPeer 'B' @('-join', '-joincodeout', (Join-Path $peers 'B-joincode.txt'), '-invitecode', ('@' + (Join-Path $peers 'B-invite.txt')), '-wait', '30s') 240 | Out-Null
  Check (WaitPeer 'restored group ' 180) 'P5: livepeer B restored its group again'
  OpenRow $app.Id $m5 | Out-Null
  $m7 = 'A line this computer sent into the second, after leaving the third.'
  AppSends $app.Id $m7 'P5 (second, after the leave)' $true
  $b3 = 'A line into the second conversation, after the app left the third.'
  PeerTalks $app.Id $b3 'P5 (second, after the leave)'
  Start-Sleep -Seconds 2
  Shot $app.Id 'P5-the-others-go-on'
  WaitPeerExit 300
  CloseApp $app

  # ==== P6. A RESTART RESTORES BOTH, and not the one left ==========================================
  $restored = CountIn (LogText) 'restored an existing group from disk'
  $app = StartApp
  Check (WaitLog 'restored an existing group from disk' ($restored + 2) 240) 'P6: the relaunch restored both conversations'
  Start-Sleep -Seconds 20
  NmSettle
  Check ((CountIn (LogText) 'restored an existing group from disk') -eq ($restored + 2)) 'P6: and only those two: the third, left, does not come back'
  Check ($null -ne (L2Row $app.Id $a3)) 'P6: the first conversation is back, with its newest line'
  Check ($null -ne (L2Row $app.Id $b3)) 'P6: the second conversation is back, with its newest line'
  Check ($null -eq (L2Row $app.Id $m3)) 'P6: the third is not'
  Check ($null -eq (FindByName $app.Id $say.NotReadNow)) 'P6: no notice stands'
  Shot $app.Id 'P6-both-back'
  DumpNames $app.Id 'P6-both-back-uia'
  # WHAT THE ALPHA'S RECEIVES REPORTED, said and not judged: a report the library made live is the
  # app's to draw (R67), and a run in which it made none draws none
  $standing = CountIn (LogText) 'receive report is standing'
  $cleared = CountIn (LogText) 'receive report is cleared'
  $reported = CountIn (LogText) 'live: group_receive reported:'
  Note "P6: across the run the library's receives reported $reported time(s); a group's report stood $standing time(s) and was cleared $cleared time(s)"
  CloseApp $app

  # ==== REMOVAL: not reachable live =================================================================
  $script:notReached += 'a removed conversation: livepeer (message 03192c2b) has no way to remove a member (it makes no Group.RemoveMember call), so nothing in this run can remove the app; the hermetic list3 leg (E) is the removed conversation''s proof'
} catch {
  Note "DRIVER ERROR: $_"
  $script:failures++
} finally {
  if ($null -ne $script:peerProc -and -not $script:peerProc.HasExited) {
    Note "a livepeer run is still live at the end; waiting for its serve, then stopping it"
    if (-not $script:peerProc.WaitForExit(330000)) { Stop-Process -Id $script:peerProc.Id -Force }
  }
  foreach ($tag in @($script:holds.Keys)) {
    Set-Content -LiteralPath (Join-Path $Work "hold-$tag.release") -Value 'release'
    $p = $script:holds[$tag]
    if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force }
  }
  Get-Process URmessage -ErrorAction SilentlyContinue | Stop-Process -Force
  New-Item -ItemType Directory -Force (Join-Path $Out 'logs') | Out-Null
  Copy-Item -Path (Join-Path $root 'logs\*') -Destination (Join-Path $Out 'logs') -ErrorAction SilentlyContinue
  # THE ARTIFACT HOLDS NO CODE: every text file in it is read for a stretch of every code relayed, and
  # a hit is struck out of the file (and fails the run) before anything is uploaded
  $files = @(Get-ChildItem -LiteralPath $Out -Recurse -File | Where-Object { $_.Extension -ne '.png' })
  $hits = 0
  foreach ($f in $files) {
    $text = ''
    try { $text = [IO.File]::ReadAllText($f.FullName) } catch { continue }
    $dirty = $false
    foreach ($code in $script:codes) {
      if ($code.Length -lt 160) { continue }
      foreach ($at in 40, 120) {
        $piece = $code.Substring($at, 32)
        if ($text.Contains($piece)) {
          $hits++
          $dirty = $true
          $text = $text.Replace($piece, '[STRUCK: a stretch of a relayed code]')
        }
      }
    }
    if ($dirty) { [IO.File]::WriteAllText($f.FullName, $text); Note "SCAN: $($f.Name) held a stretch of a relayed code; struck out" }
  }
  Check ($hits -eq 0) "SCAN: the $($files.Count) text file(s) of the artifact hold no stretch of the $($script:codes.Count) code(s) relayed ($hits hit(s))"
}
if ($script:notReached.Count -gt 0) { Note "NOT REACHED in this run: $($script:notReached -join '; ')" }
Note "$script:failures check(s) failed"
if ($script:failures -gt 0) { exit 1 }
