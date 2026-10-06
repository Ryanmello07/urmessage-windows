# Runs WiX's ICE03 on its own against a built URmessage MSI, and passes only if
# it reports exactly the rows listed below.
#
# installer\Installer.wixproj suppresses ICE03 for the MSI build, because the
# harvested Windows App SDK payload fails it on eight rows of the File table's
# Language column (explained there). WiX can suppress an ICE only for the whole
# package. But ICE03 is also what checks the syntax of the conditions and
# formatted strings Package.wxs authors by hand: the launch conditions, the
# shortcut's Target, the registry value. Suppressed package-wide, a typo in one
# of those builds green and validates green, and a launch condition missing its
# last ")" then admits every install context. So this runs ICE03 alone (wix msi
# validate -ice ICE03) and compares its rows with the list below. An extra row
# anywhere fails it, and so does a listed row that is gone: when a Windows App
# SDK bump changes the rows, update this list and the note in Installer.wixproj
# together.
#
# wix.exe is the WiX SDK's own. By default it is the WixToolset.Sdk version
# installer\Installer.wixproj pins, from the NuGet global packages folder
# (%NUGET_PACKAGES%, else %USERPROFILE%\.nuget\packages), where the MSI
# build's /restore put it; -WixExe overrides that. Each row is named by the
# file it is about, read from the MSI's own tables through the Windows
# Installer COM object, read-only, so the list does not depend on WiX's
# generated File ids. The MSI itself is not modified.
#
# Run it the way CI does, after the MSI build:
#   powershell -NoProfile -ExecutionPolicy Bypass -File app\tools\verify-msi-ice03.ps1 -MsiPath app\installer\bin\x64\Release\URmessage.msi
#
# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$MsiPath,

  [string]$WixExe
)

$ErrorActionPreference = "Stop"

# The rows ICE03 reports on the harvested payload (Windows App SDK 2.2.0,
# x64), as "<ICE>: <message>; <Table>.<Column>; <path under INSTALLFOLDER>",
# compared case-insensitively.
$expected = @(
  # Each declares 86 languages (430 characters), more than the column holds.
  "ICE03: String overflow (greater than length permitted in column); File.Language; Microsoft.ui.xaml.dll",
  "ICE03: String overflow (greater than length permitted in column); File.Language; Microsoft.UI.Xaml.Phone.dll",
  # LCID 1169, gd-gb.
  "ICE03: Invalid Language Id; File.Language; gd-gb\Microsoft.ui.xaml.dll.mui",
  "ICE03: Invalid Language Id; File.Language; gd-gb\Microsoft.UI.Xaml.Phone.dll.mui",
  # LCID 1153, mi-NZ.
  "ICE03: Invalid Language Id; File.Language; mi-NZ\Microsoft.ui.xaml.dll.mui",
  "ICE03: Invalid Language Id; File.Language; mi-NZ\Microsoft.UI.Xaml.Phone.dll.mui",
  # LCID 1152, ug-CN.
  "ICE03: Invalid Language Id; File.Language; ug-CN\Microsoft.ui.xaml.dll.mui",
  "ICE03: Invalid Language Id; File.Language; ug-CN\Microsoft.UI.Xaml.Phone.dll.mui"
)

if (-not (Test-Path -LiteralPath $MsiPath)) {
  throw "no MSI at $MsiPath"
}
$MsiPath = (Resolve-Path -LiteralPath $MsiPath).Path

if (-not $WixExe) {
  $wixproj = Join-Path $PSScriptRoot "..\installer\Installer.wixproj"
  $sdk = [regex]::Match((Get-Content -LiteralPath $wixproj -Raw), 'Sdk="WixToolset\.Sdk/([^"]+)"')
  if (-not $sdk.Success) {
    throw "could not read the WixToolset.Sdk version from $wixproj"
  }
  $packages = if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE ".nuget\packages" }
  $WixExe = Join-Path $packages ("wixtoolset.sdk\" + $sdk.Groups[1].Value.ToLowerInvariant() + "\tools\net472\x86\wix.exe")
}
if (-not (Test-Path -LiteralPath $WixExe)) {
  throw "no wix.exe at $WixExe. Build the MSI first (its /restore fetches the WiX SDK), or pass -WixExe."
}
$WixExe = (Resolve-Path -LiteralPath $WixExe).Path

# --- run ICE03 alone ---------------------------------------------------------
# Through a Process with both streams read, not `& wix.exe 2>&1`: under
# Windows PowerShell 5.1 with ErrorActionPreference Stop, the first line on
# stderr would end the script.
$work = [string](Join-Path ([System.IO.Path]::GetTempPath()) ("verify-msi-ice03-" + [guid]::NewGuid().ToString("N")))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  $psi = New-Object System.Diagnostics.ProcessStartInfo $WixExe
  $psi.Arguments = "msi validate `"$MsiPath`" -ice ICE03 -intermediateFolder `"$work`" -nologo"
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $process = [System.Diagnostics.Process]::Start($psi)
  $stderrRead = $process.StandardError.ReadToEndAsync()
  $stdout = $process.StandardOutput.ReadToEnd()
  $stderr = $stderrRead.Result
  $process.WaitForExit()
  $exitCode = $process.ExitCode
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "wix msi validate -ice ICE03: exit $exitCode ($WixExe)"

# A row reads "<source> : warning|error WIXnnnn: ICEnn: <message>; Table: <t>,
# Column: <c>, Key(s): <keys>". <source> is the MSI, or a line of Package.wxs
# when the .wixpdb sits next to the MSI.
$rows = New-Object System.Collections.Generic.List[object]
$other = New-Object System.Collections.Generic.List[string]
foreach ($line in (($stdout + "`n" + $stderr) -split "\r?\n")) {
  if ($line.Trim() -eq "") { continue }
  $match = [regex]::Match($line, '\b(?:warning|error) WIX\d+: (ICE\d+): (.+?); Table: (\w+), Column: (\w+), Key\(s\): (.+?)\s*$')
  if ($match.Success) {
    $rows.Add([pscustomobject]@{
      Ice = $match.Groups[1].Value; Message = $match.Groups[2].Value; Table = $match.Groups[3].Value
      Column = $match.Groups[4].Value; Key = $match.Groups[5].Value; Line = $line.Trim()
    })
  } elseif ($line -match '\b(?:warning|error) WIX\d+:') {
    $other.Add($line.Trim())
  }
}
if ($rows.Count -eq 0 -and $other.Count -eq 0 -and $exitCode -ne 0) {
  throw "wix msi validate exited $exitCode without reporting any row:`n$stdout`n$stderr"
}

# --- name each row by its file -----------------------------------------------
function Invoke-ComMethod($target, [string]$name, [object[]]$arguments) {
  return $target.GetType().InvokeMember($name, [System.Reflection.BindingFlags]::InvokeMethod, $null, $target, $arguments)
}

function Get-ComProperty($target, [string]$name, [object[]]$arguments) {
  return $target.GetType().InvokeMember($name, [System.Reflection.BindingFlags]::GetProperty, $null, $target, $arguments)
}

function Remove-ComObject($object) {
  if ($null -ne $object -and [System.Runtime.InteropServices.Marshal]::IsComObject($object)) {
    [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($object)
  }
}

function Get-Rows($database, [string]$sql, [int]$columns) {
  $view = Invoke-ComMethod $database "OpenView" @($sql)
  try {
    Invoke-ComMethod $view "Execute" $null | Out-Null
    $result = New-Object System.Collections.Generic.List[object]
    while ($true) {
      $record = Invoke-ComMethod $view "Fetch" $null
      if ($null -eq $record) { break }
      try {
        $values = New-Object string[] $columns
        for ($i = 1; $i -le $columns; $i++) {
          $values[$i - 1] = [string](Get-ComProperty $record "StringData" @($i))
        }
        $result.Add($values)
      } finally {
        Remove-ComObject $record
      }
    }
    return ,$result.ToArray()
  } finally {
    Invoke-ComMethod $view "Close" $null | Out-Null
    Remove-ComObject $view
  }
}

# Long name out of a FileName or DefaultDir value: "short|long" or "name", and
# a DefaultDir may add ":source" after the target part.
function Get-LongName([string]$value) {
  return ((($value -split ':')[0]) -split '\|')[-1]
}

$installer = New-Object -ComObject WindowsInstaller.Installer
$db = $null
try {
  # 0 = msiOpenDatabaseModeReadOnly
  $db = Invoke-ComMethod $installer "OpenDatabase" @($MsiPath, 0)
  $directories = @{}
  foreach ($row in (Get-Rows $db "SELECT Directory, Directory_Parent, DefaultDir FROM Directory" 3)) {
    $directories[$row[0]] = @{ Parent = $row[1]; Name = (Get-LongName $row[2]) }
  }
  $componentDirectory = @{}
  foreach ($row in (Get-Rows $db "SELECT Component, Directory_ FROM Component" 2)) {
    $componentDirectory[$row[0]] = $row[1]
  }
  # File key -> its path under INSTALLFOLDER (or under the root it does
  # install to, should it ever be outside INSTALLFOLDER).
  $filePath = @{}
  foreach ($row in (Get-Rows $db "SELECT File, Component_, FileName FROM File" 3)) {
    $parts = @(Get-LongName $row[2])
    $current = $componentDirectory[$row[1]]
    for ($depth = 0; $current -and $current -ne "INSTALLFOLDER" -and $depth -lt 64; $depth++) {
      $entry = $directories[$current]
      if (-not $entry) { break }
      if ($entry.Name -ne ".") { $parts = @($entry.Name) + $parts }
      if ($entry.Parent -eq $current) { break }
      $current = $entry.Parent
    }
    $filePath[$row[0]] = $parts -join "\"
  }
} finally {
  Remove-ComObject $db
  Remove-ComObject $installer
  [GC]::Collect()
  [GC]::WaitForPendingFinalizers()
}

$actual = New-Object System.Collections.Generic.List[string]
foreach ($row in $rows) {
  $subject = if ($row.Table -eq "File" -and $filePath.ContainsKey($row.Key)) { $filePath[$row.Key] } else { "key $($row.Key)" }
  $actual.Add("$($row.Ice): $($row.Message); $($row.Table).$($row.Column); $subject")
}

# --- compare, both ways -------------------------------------------------------
$expectedSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$expected, [System.StringComparer]::OrdinalIgnoreCase)
$actualSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$actual.ToArray(), [System.StringComparer]::OrdinalIgnoreCase)

$failures = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $actual.Count; $i++) {
  if ($expectedSet.Contains($actual[$i])) {
    Write-Host "  known:      $($actual[$i])"
  } else {
    Write-Host "  UNEXPECTED: $($actual[$i])" -ForegroundColor Red
    $failures.Add("ICE03 reports a row that is not in the list: $($actual[$i])  [$($rows[$i].Line)]")
  }
}
foreach ($entry in $expected) {
  if (-not $actualSet.Contains($entry)) {
    Write-Host "  MISSING:    $entry" -ForegroundColor Red
    $failures.Add("a listed row is gone (update the list here and in installer\Installer.wixproj): $entry")
  }
}
if ($actual.Count -ne $actualSet.Count) {
  $failures.Add("ICE03 reported the same row more than once")
}
foreach ($line in $other) {
  $failures.Add("wix msi validate reported something other than an ICE03 table row: $line")
}

if ($failures.Count -gt 0) {
  Write-Host "ICE03 check FAILED: $($actual.Count) row(s) reported, $($expected.Count) listed:" -ForegroundColor Red
  foreach ($failure in $failures) { Write-Host "  - $failure" -ForegroundColor Red }
  throw "ICE03 check failed ($($failures.Count) issue(s)); see above"
}

Write-Host "ICE03 check passed: exactly the $($expected.Count) listed rows, all in File.Language; nothing else in the package fails ICE03." -ForegroundColor Green
