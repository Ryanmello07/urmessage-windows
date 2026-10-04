# Asserts that a built URmessage MSI installs a working app, not just that WiX
# compiled it. installer\Package.wxs harvests whatever the app build left in
# its output folder, so a build that silently lost the Windows App Runtime,
# resources.pri, the compiled XAML, the brand faces or the VC++ runtime would
# still produce a valid, installable MSI. This reads the MSI and fails if it
# did.
#
# It needs nothing beyond Windows PowerShell 5.1 or pwsh 7 and the Windows
# Installer COM object (WindowsInstaller.Installer, present on every Windows,
# CI runners included). It installs, registers and extracts nothing. It checks
# two things.
#
# 1. The MSI's own tables, with the database opened READ-ONLY. Adapted from
#    the URnetwork VPN client's app\tools\verify-msi-payload.ps1, which
#    queries the same File table.
#      - every file installs under INSTALLFOLDER, and at least -MinFileCount
#        do;
#      - each required file (listed below) is there AT ITS PATH under
#        INSTALLFOLDER: a file in the wrong folder is as broken as a missing
#        one;
#      - the package is per-user by default: ALLUSERS=2 and
#        MSIINSTALLPERUSER=1;
#      - every shortcut targets [INSTALLFOLDER]URmessage.exe and carries the
#        AppUserModelID the app sets on itself at startup
#        (src\Common\Ids.h, kAppUserModelId).
#
# 2. How Windows Installer itself resolves the package. It makes three
#    temporary copies of the MSI: as built, with ALLUSERS=1, and with the
#    MSIINSTALLPERUSER row deleted. On each it opens an installer session
#    (Installer.OpenPackage, UI level none) and runs only CostInitialize,
#    FileCost, CostFinalize and LaunchConditions, the engine and actions
#    msiexec runs, without the install. Then it deletes the copies.
#      - As built, LaunchConditions passes, INSTALLFOLDER resolves under
#        %LOCALAPPDATA%\Programs\, and a default install requests every
#        required file's component and every shortcut's component (request
#        state 3, local). A file in a feature that a default install leaves
#        out is as missing as a file that is not in the MSI.
#      - With ALLUSERS=1, and with MSIINSTALLPERUSER deleted (each selects
#        the per-machine context), LaunchConditions refuses it. This is
#        checked by behaviour, not by reading the condition, because a
#        malformed condition evaluates as an error and LaunchConditions
#        passes it.
#    These model a first install, so the build must not be installed on the
#    machine that runs this. Each build has its own ProductCode, so a fresh
#    build never is.
#
# -Release is for an MSI that is going to ship. It also requires
# URnetworkSdk.dll, without which the app's live path (the one an ordinary
# launch takes) cannot load the SDK. Upstream CI builds its MSI without the
# SDK dll, as a build check only, so it runs this without -Release.
#
# Run it the way CI does:
#   powershell -NoProfile -ExecutionPolicy Bypass -File app\tools\verify-msi-payload.ps1 -MsiPath app\installer\bin\x64\Release\URmessage.msi
#
# This repository has no test framework and none may be added. This is a build
# check, like the diagnose gate, not a framework.
#
# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$MsiPath,

  [switch]$Release,

  # The x64 Release MSI measured 304 files on 2026-10-04 (Windows App SDK
  # 2.2.0, no SDK dll staged): 299 harvested, plus the exe and the four fonts.
  # The floor sits far enough below that for a Windows App SDK bump not to make
  # this flaky, and far above what a harvest of the wrong folder leaves.
  [int]$MinFileCount = 250
)

$ErrorActionPreference = "Stop"

# Paths relative to INSTALLFOLDER, compared case-insensitively.
#   URmessage.exe, resources.pri: without the pri every string renders as its
#     key id.
#   App.xbf, MainWindow.xbf: the app's compiled XAML.
#   Microsoft.WindowsAppRuntime.dll, Microsoft.ui.xaml.dll: the self-contained
#     runtime. The app is unpackaged and nothing installs it machine-wide.
#   Assets\Fonts\pp_neue_montreal_regular.ttf: the body face. Without it the
#     app falls back to a system face with no error anywhere.
#   vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll: the VC++ runtime
#     URmessage.exe imports, which a Release build stages next to it
#     (App.vcxproj, UrmStageVCRuntime) because nothing installs the VC++
#     Redistributable. Without them the installed app does not start on a
#     machine that lacks the Redistributable, and nothing else in CI can
#     tell: --diagnose runs the build output, not the MSI, and a runner that
#     has the Redistributable loads its system copies.
$required = @(
  "URmessage.exe",
  "resources.pri",
  "App.xbf",
  "MainWindow.xbf",
  "Microsoft.WindowsAppRuntime.dll",
  "Microsoft.ui.xaml.dll",
  "Assets\Fonts\pp_neue_montreal_regular.ttf",
  "vcruntime140.dll",
  "vcruntime140_1.dll",
  "msvcp140.dll"
)
if ($Release) {
  $required += @("URnetworkSdk.dll")
}

# What every shortcut must open: the installed exe. Package.wxs names it by
# directory, not by [#file] (see the shortcut's comment there).
$expectedTarget = "[INSTALLFOLDER]URmessage.exe"

if (-not (Test-Path -LiteralPath $MsiPath)) {
  throw "no MSI at $MsiPath"
}
$MsiPath = (Resolve-Path -LiteralPath $MsiPath).Path

# The AppUserModelID the shortcut must carry, read from the one place the app
# defines it, so the two cannot drift apart unnoticed.
$idsPath = Join-Path $PSScriptRoot "..\src\Common\Ids.h"
$idsText = Get-Content -LiteralPath $idsPath -Raw
$aumidMatch = [regex]::Match($idsText, 'kAppUserModelId\[\]\s*=\s*L"([^"]+)"')
if (-not $aumidMatch.Success) {
  throw "could not read kAppUserModelId from $idsPath"
}
$expectedAumid = $aumidMatch.Groups[1].Value

function Invoke-ComMethod($target, [string]$name, [object[]]$arguments) {
  return $target.GetType().InvokeMember($name, [System.Reflection.BindingFlags]::InvokeMethod, $null, $target, $arguments)
}

function Get-ComProperty($target, [string]$name, [object[]]$arguments) {
  return $target.GetType().InvokeMember($name, [System.Reflection.BindingFlags]::GetProperty, $null, $target, $arguments)
}

function Set-ComProperty($target, [string]$name, [object[]]$arguments) {
  [void]$target.GetType().InvokeMember($name, [System.Reflection.BindingFlags]::SetProperty, $null, $target, $arguments)
}

# Releases a COM object now rather than at some later garbage collection: an
# MSI handle that stays open keeps its file open, and the copies below must be
# deleted.
function Remove-ComObject($object) {
  if ($null -ne $object -and [System.Runtime.InteropServices.Marshal]::IsComObject($object)) {
    [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($object)
  }
}

# The innermost message of an error, which for a COM call is the installer's.
function Get-ErrorText($errorRecord) {
  return $errorRecord.Exception.GetBaseException().Message
}

# Every row of a query, as an array of string arrays.
function Get-Rows($database, [string]$sql, [int]$columns) {
  $view = Invoke-ComMethod $database "OpenView" @($sql)
  try {
    Invoke-ComMethod $view "Execute" $null | Out-Null
    $rows = New-Object System.Collections.Generic.List[object]
    while ($true) {
      $record = Invoke-ComMethod $view "Fetch" $null
      if ($null -eq $record) { break }
      try {
        $values = New-Object string[] $columns
        for ($i = 1; $i -le $columns; $i++) {
          $values[$i - 1] = [string](Get-ComProperty $record "StringData" @($i))
        }
        $rows.Add($values)
      } finally {
        Remove-ComObject $record
      }
    }
    return ,$rows.ToArray()
  } finally {
    Invoke-ComMethod $view "Close" $null | Out-Null
    Remove-ComObject $view
  }
}

function Invoke-Sql($database, [string]$sql) {
  $view = Invoke-ComMethod $database "OpenView" @($sql)
  try {
    Invoke-ComMethod $view "Execute" $null | Out-Null
  } finally {
    Invoke-ComMethod $view "Close" $null | Out-Null
    Remove-ComObject $view
  }
}

function Test-Table($database, [string]$name) {
  $hits = Get-Rows $database "SELECT Name FROM _Tables WHERE Name = '$name'" 1
  return ($hits.Count -gt 0)
}

# Long name out of a FileName or DefaultDir value: "short|long" or "name", and
# a DefaultDir may add ":source" after the target part.
function Get-LongName([string]$value) {
  $target = ($value -split ':')[0]
  return ($target -split '\|')[-1]
}

# Opens an installer session on one copy of the MSI and runs the costing
# actions, then LaunchConditions, and returns what resolved. Nothing is
# installed: that would take InstallValidate, InstallInitialize and the
# execute sequence, and none of them runs. -Components are read after
# CostFinalize, which sets their request states.
function Invoke-Session([string]$path, [string[]]$components) {
  $result = @{ Error = $null; LaunchConditions = $null; Property = @{}; ComponentState = @{} }
  $session = $null
  try {
    # 0: no options, so the session sees the machine's state as msiexec would.
    $session = Invoke-ComMethod $installer "OpenPackage" @($path, 0)
    foreach ($action in @("CostInitialize", "FileCost", "CostFinalize")) {
      $status = [int](Invoke-ComMethod $session "DoAction" @($action))
      if ($status -ne 1) { throw "$action returned $status" }
    }
    foreach ($name in @("ALLUSERS", "MSIINSTALLPERUSER", "INSTALLFOLDER")) {
      $result.Property[$name] = [string](Get-ComProperty $session "Property" @($name))
    }
    foreach ($component in $components) {
      $result.ComponentState[$component] = [int](Get-ComProperty $session "ComponentRequestState" @($component))
    }
    # 1 = the conditions pass, 3 = a condition refused the install.
    $result.LaunchConditions = [int](Invoke-ComMethod $session "DoAction" @("LaunchConditions"))
  } catch {
    $result.Error = Get-ErrorText $_
  } finally {
    Remove-ComObject $session
  }
  return $result
}

$failures = New-Object System.Collections.Generic.List[string]
$installer = New-Object -ComObject WindowsInstaller.Installer
$db = $null
$workDir = $null

try {
  # 0 = msiOpenDatabaseModeReadOnly
  $db = Invoke-ComMethod $installer "OpenDatabase" @($MsiPath, 0)

  # --- per-user ----------------------------------------------------------------
  $properties = @{}
  foreach ($row in (Get-Rows $db "SELECT Property, Value FROM Property" 2)) {
    $properties[$row[0]] = $row[1]
  }
  if ($properties["ALLUSERS"] -ne "2" -or $properties["MSIINSTALLPERUSER"] -ne "1") {
    $failures.Add("the package is not per-user by default: ALLUSERS='$($properties["ALLUSERS"])' MSIINSTALLPERUSER='$($properties["MSIINSTALLPERUSER"])' (want 2 and 1, from Package Scope=perUserOrMachine)")
  }

  # --- where every file installs ---------------------------------------------
  $directories = @{}
  foreach ($row in (Get-Rows $db "SELECT Directory, Directory_Parent, DefaultDir FROM Directory" 3)) {
    $directories[$row[0]] = @{ Parent = $row[1]; Name = (Get-LongName $row[2]) }
  }
  if (-not $directories.ContainsKey("INSTALLFOLDER")) {
    throw "the MSI has no INSTALLFOLDER directory"
  }

  # The path of a directory relative to INSTALLFOLDER ("" for INSTALLFOLDER
  # itself), or $null when it is not under INSTALLFOLDER at all.
  function Get-RelativeDirectory([string]$directory) {
    $parts = @()
    $current = $directory
    for ($depth = 0; $current -ne "INSTALLFOLDER"; $depth++) {
      if ($depth -gt 64 -or -not $directories.ContainsKey($current)) { return $null }
      $entry = $directories[$current]
      if ($entry.Name -ne ".") { $parts = @($entry.Name) + $parts }
      if ([string]::IsNullOrEmpty($entry.Parent) -or $entry.Parent -eq $current) { return $null }
      $current = $entry.Parent
    }
    return ($parts -join "\")
  }

  $componentDirectory = @{}
  foreach ($row in (Get-Rows $db "SELECT Component, Directory_ FROM Component" 2)) {
    $componentDirectory[$row[0]] = $row[1]
  }

  $installed = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
  $componentOf = @{}
  $outside = New-Object System.Collections.Generic.List[string]
  $files = Get-Rows $db "SELECT File, Component_, FileName FROM File" 3
  foreach ($row in $files) {
    $relativeDirectory = Get-RelativeDirectory $componentDirectory[$row[1]]
    $name = Get-LongName $row[2]
    if ($null -eq $relativeDirectory) {
      $outside.Add("$name (component $($row[1]))")
      continue
    }
    if ($relativeDirectory) { $name = "$relativeDirectory\$name" }
    [void]$installed.Add($name)
    $componentOf[$name] = $row[1]
  }

  Write-Host "MSI file table: $($files.Count) files ($MsiPath)"

  if ($outside.Count -gt 0) {
    $failures.Add("$($outside.Count) file(s) install outside INSTALLFOLDER: $(($outside | Select-Object -First 5) -join ', ')")
  }
  if ($files.Count -lt $MinFileCount) {
    $failures.Add("file count $($files.Count) is below the floor of $MinFileCount. Check that installer\Package.wxs's <Files> harvest points at a populated `$(var.BinDir), and that BinDir was passed to the wixproj build.")
  }
  foreach ($path in $required) {
    if (-not $installed.Contains($path)) {
      $failures.Add("required file '$path' is not in the MSI under INSTALLFOLDER")
    }
  }

  # --- the shortcuts: what they open, and their AppUserModelID ----------------
  # Get-Rows already returns one array of rows; @() around it would nest it.
  $shortcuts = @()
  if (Test-Table $db "Shortcut") {
    $shortcuts = Get-Rows $db "SELECT Shortcut, Target, Component_ FROM Shortcut" 3
  }
  $aumids = @{}
  if (Test-Table $db "MsiShortcutProperty") {
    foreach ($row in (Get-Rows $db "SELECT Shortcut_, PropertyKey, PropVariantValue FROM MsiShortcutProperty" 3)) {
      if ($row[1] -eq "System.AppUserModel.ID") { $aumids[$row[0]] = $row[2] }
    }
  }
  if ($shortcuts.Count -eq 0) {
    $failures.Add("the MSI authors no shortcut (Package.wxs puts one in the Start menu)")
  }
  foreach ($shortcut in $shortcuts) {
    $id = $shortcut[0]
    if ($shortcut[1] -cne $expectedTarget) {
      $failures.Add("shortcut '$id' targets '$($shortcut[1])', but the installed exe is '$expectedTarget'")
    }
    if (-not $aumids.ContainsKey($id)) {
      $failures.Add("shortcut '$id' has no System.AppUserModel.ID (want '$expectedAumid', Common/Ids.h kAppUserModelId)")
    } elseif ($aumids[$id] -cne $expectedAumid) {
      $failures.Add("shortcut '$id' has System.AppUserModel.ID '$($aumids[$id])', but the app sets '$expectedAumid' (Common/Ids.h kAppUserModelId)")
    }
  }

  # The components a default install must request: each required file's, and
  # each shortcut's. Component -> what it carries, for the messages.
  $mustInstall = [ordered]@{}
  foreach ($path in $required) {
    if ($componentOf.ContainsKey($path) -and -not $mustInstall.Contains($componentOf[$path])) {
      $mustInstall[$componentOf[$path]] = $path
    }
  }
  foreach ($shortcut in $shortcuts) {
    if (-not $mustInstall.Contains($shortcut[2])) {
      $mustInstall[$shortcut[2]] = "the shortcut '$($shortcut[0])'"
    }
  }

  $productCode = $properties["ProductCode"]
  if (-not $productCode) {
    throw "the MSI has no ProductCode"
  }
  Remove-ComObject $db
  $db = $null

  # --- how Windows Installer resolves it, without installing ------------------
  # ProductState 5 = installed, 1 = advertised, for this user (2 = installed
  # for another user only, -1 = not installed). Installed here, a session
  # would run in maintenance mode: request states would describe a repair, and
  # "Installed OR" in the launch condition would admit every context.
  $productState = [int](Get-ComProperty $installer "ProductState" @($productCode))
  if ($productState -eq 5 -or $productState -eq 1) {
    $failures.Add("this build ($productCode) is installed on this machine (ProductState $productState), so the session checks cannot model a first install. Run this where the build is not installed.")
  } else {
    $workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("verify-msi-payload-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $workDir | Out-Null
    # 2 = msiUILevelNone: a refused launch condition must not open a dialog.
    Set-ComProperty $installer "UILevel" @(2)

    $programs = $env:LOCALAPPDATA.TrimEnd('\') + "\Programs\"
    $variants = @(
      @{ Name = "as built"; Sql = @(); Want = 1 },
      @{ Name = "ALLUSERS=1"; Sql = @("UPDATE Property SET Value = '1' WHERE Property = 'ALLUSERS'"); Want = 3 },
      @{ Name = "MSIINSTALLPERUSER deleted"; Sql = @("DELETE FROM Property WHERE Property = 'MSIINSTALLPERUSER'"); Want = 3 }
    )
    for ($index = 0; $index -lt $variants.Count; $index++) {
      $variant = $variants[$index]
      $asBuilt = ($index -eq 0)
      # [string]: Join-Path's output is wrapped, and COM refuses the wrapper
      # (DISP_E_TYPEMISMATCH).
      $copy = [string](Join-Path $workDir "copy$index.msi")
      Copy-Item -LiteralPath $MsiPath -Destination $copy
      Set-ItemProperty -LiteralPath $copy -Name IsReadOnly -Value $false
      if ($variant.Sql.Count -gt 0) {
        # 1 = msiOpenDatabaseModeTransact
        $copyDb = Invoke-ComMethod $installer "OpenDatabase" @($copy, 1)
        try {
          foreach ($sql in $variant.Sql) { Invoke-Sql $copyDb $sql }
          Invoke-ComMethod $copyDb "Commit" $null | Out-Null
        } finally {
          Remove-ComObject $copyDb
        }
      }

      $components = if ($asBuilt) { [string[]]@($mustInstall.Keys) } else { [string[]]@() }
      $resolved = Invoke-Session $copy $components
      if ($resolved.Error) {
        $failures.Add("the '$($variant.Name)' session failed: $($resolved.Error)")
        continue
      }
      Write-Host ("session '{0}': ALLUSERS='{1}' MSIINSTALLPERUSER='{2}' INSTALLFOLDER='{3}' LaunchConditions={4} (want {5}; 1 passes, 3 refuses)" -f
        $variant.Name, $resolved.Property["ALLUSERS"], $resolved.Property["MSIINSTALLPERUSER"], $resolved.Property["INSTALLFOLDER"],
        $resolved.LaunchConditions, $variant.Want)

      if ($resolved.LaunchConditions -ne $variant.Want) {
        if ($asBuilt) {
          $failures.Add("as built, the package does not pass LaunchConditions (it returned $($resolved.LaunchConditions)), so a default install is refused")
        } else {
          $failures.Add("the per-machine context ($($variant.Name)) is not refused: LaunchConditions returned $($resolved.LaunchConditions), want 3. Check the per-user launch condition in installer\Package.wxs; a malformed condition passes.")
        }
      }
      if ($asBuilt) {
        $folder = $resolved.Property["INSTALLFOLDER"]
        if (-not ($folder.Length -gt $programs.Length -and $folder.StartsWith($programs, [System.StringComparison]::OrdinalIgnoreCase))) {
          $failures.Add("as built, INSTALLFOLDER resolves to '$folder', not under $programs (%LOCALAPPDATA%\Programs, where Windows Installer redirects ProgramFiles64Folder for a per-user install)")
        }
        $states = foreach ($component in $mustInstall.Keys) { "$($mustInstall[$component])=$($resolved.ComponentState[$component])" }
        Write-Host "session 'as built': component request states (3 = local): $($states -join ', ')"
        foreach ($component in $mustInstall.Keys) {
          $state = $resolved.ComponentState[$component]
          if ($state -ne 3) {
            $failures.Add("a default install does not install $($mustInstall[$component]): its component $component has request state $state, want 3 (local). Is it in a feature a default install leaves out?")
          }
        }
      }
    }
  }
} finally {
  Remove-ComObject $db
  Remove-ComObject $installer
  [GC]::Collect()
  [GC]::WaitForPendingFinalizers()
  if ($workDir -and (Test-Path -LiteralPath $workDir)) {
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $workDir) {
      Write-Warning "could not delete the temporary MSI copies in $workDir"
    }
  }
}

$mode = if ($Release) { "release" } else { "build check" }
if ($failures.Count -gt 0) {
  Write-Host "MSI payload verification FAILED ($mode):" -ForegroundColor Red
  foreach ($failure in $failures) { Write-Host "  - $failure" -ForegroundColor Red }
  throw "MSI payload verification failed ($($failures.Count) issue(s)); see above"
}

Write-Host "MSI payload verification passed ($mode): $($files.Count) files, all under INSTALLFOLDER; all $($required.Count) required paths present, and a default install requests each; per-user (ALLUSERS=2, MSIINSTALLPERUSER=1), resolving INSTALLFOLDER under $programs; the per-machine context refused (ALLUSERS=1, MSIINSTALLPERUSER deleted); $($shortcuts.Count) shortcut(s) to $expectedTarget with AppUserModelID $expectedAumid." -ForegroundColor Green
