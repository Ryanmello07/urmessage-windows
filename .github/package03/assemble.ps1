# THROWAWAY, NEVER MERGE (ci/task58-package-03). Test package 03 (R43): the package folder, its zip,
# and the zip held to the folder both ways.
#
# The folder is URmessage-alpha\: everything the Release build wrote to app\build\x64\Release,
# less the build by-products that do not run, plus READ-ME-FIRST.txt. The by-products are the ones
# app\installer\Package.wxs leaves out of the per-user MSI (*.pdb, *.lib, *.exp, *.ilk, *.msi,
# *.wixpdb), so the package holds what the MSI installs, and each file left out is printed.
#
# The zip holds one entry per directory (its name ends in "/", the top folder's among them) and
# one per file, every name under "URmessage-alpha/" with forward slashes, as package 02d's did.
# Its list is written in 02d's form, "<name> <length>", one entry per line.
#
# Run from the repository root with pwsh 7. Never launches URmessage.exe.
param(
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Evidence,
  [Parameter(Mandatory = $true)][string]$ReadmeSha256,
  [Parameter(Mandatory = $true)][string]$DllVersion
)
$ErrorActionPreference = 'Stop'
$script:failures = 0
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++ }
}
function Sha([string]$path) { return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }

$repo = (Get-Location).Path
$bin = Join-Path $repo 'app\build\x64\Release'
$staged = Join-Path $repo 'app\third_party\urnetwork-sdk\bin\x64\URnetworkSdk.dll'
$readmeSrc = Join-Path $repo '.github\package03\READ-ME-FIRST.txt'
if (-not (Test-Path -LiteralPath $bin)) { throw "the build output $bin does not exist" }
if (Test-Path -LiteralPath $Out) { throw "$Out exists already; the package is made in a fresh folder" }
New-Item -ItemType Directory -Path $Out | Out-Null
New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
$top = Join-Path $Out 'URmessage-alpha'
New-Item -ItemType Directory -Path $top | Out-Null

# ---- 1. the build output, less its by-products -------------------------------------------------
$byproduct = @('.pdb', '.lib', '.exp', '.ilk', '.msi', '.wixpdb')
$all = @(Get-ChildItem -LiteralPath $bin -Recurse -File -Force)
$kept = 0; $left = @()
foreach ($f in $all) {
  $rel = $f.FullName.Substring($bin.Length + 1)
  if ($byproduct -contains $f.Extension.ToLowerInvariant()) {
    $left += "$rel ($($f.Length) bytes)"
    continue
  }
  $dest = Join-Path $top $rel
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
  Copy-Item -LiteralPath $f.FullName -Destination $dest
  $kept++
}
Note "the build output holds $($all.Count) files; $kept are packaged, and $($left.Count) by-products are left out:"
foreach ($l in $left) { Note "  left out: $l" }
# THE COMPLEMENT, ASSERTED: every file left out is a by-product by its extension, and nothing that
# runs is among them (the rule above is by extension only, so this holds by construction; it is
# printed so a change to the rule shows here)
$runs = @($left | Where-Object { $_ -match '\.(exe|dll|pri|xbf|mui|winmd|json|ico|otf|ttf|png|html)\b' })
Check ($runs.Count -eq 0) "nothing left out is a file the app runs ($($runs.Count))"
$left | Set-Content -LiteralPath (Join-Path $Evidence 'left-out.txt') -Encoding utf8

# ---- 2. the README, byte for byte ---------------------------------------------------------------
$readme = Join-Path $top 'READ-ME-FIRST.txt'
Check (-not (Test-Path -LiteralPath $readme)) 'the build output has no READ-ME-FIRST.txt of its own'
Copy-Item -LiteralPath $readmeSrc -Destination $readme
$srcSha = Sha $readmeSrc
$readmeSha = Sha $readme
Check ($srcSha -eq $ReadmeSha256) "the checked-out README is the one the lead accepted, byte for byte (sha256 $srcSha)"
Check ($readmeSha -eq $ReadmeSha256) "the packaged README is the same bytes (sha256 $readmeSha, $((Get-Item -LiteralPath $readme).Length) bytes)"

# ---- 3. what must be there ----------------------------------------------------------------------
foreach ($need in @('URmessage.exe', 'URnetworkSdk.dll', 'resources.pri', 'App.xbf', 'MainWindow.xbf',
                    'Microsoft.WindowsAppRuntime.dll', 'Microsoft.WindowsAppRuntime.Bootstrap.dll',
                    'Microsoft.ui.xaml.dll', 'Assets\app.ico', 'vcruntime140.dll', 'msvcp140.dll',
                    'en-us\Microsoft.ui.xaml.dll.mui')) {
  Check (Test-Path -LiteralPath (Join-Path $top $need)) "the package holds $need"
}
$fonts = @(Get-ChildItem -LiteralPath (Join-Path $top 'Assets\Fonts') -File -ErrorAction SilentlyContinue)
Check ($fonts.Count -eq 4) "the package holds the four brand faces ($($fonts.Count))"
$langs = @(Get-ChildItem -LiteralPath $top -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'Microsoft.ui.xaml.dll.mui') })
Note "language folders: $($langs.Count)"

# ---- 4. the dll is the one the composition build checked and staged, and it is checked again ----
$dll = Join-Path $top 'URnetworkSdk.dll'
$dllSha = Sha $dll
Check ($dllSha -eq (Sha $staged)) "the packaged dll is the staged one, byte for byte (sha256 $dllSha, $((Get-Item -LiteralPath $dll).Length) bytes)"
$before = @(Get-ChildItem -LiteralPath $top -Recurse -Force | ForEach-Object { "$($_.FullName.Substring($top.Length)) $(if ($_.PSIsContainer) { 'd' } else { $_.Length })" })
$check = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'app\tools\build-sdk-dll.ps1') -CheckDll $dll 2>&1 | Out-String
$code = $LASTEXITCODE
Write-Host $check
Check ($code -eq 0) "build-sdk-dll.ps1 -CheckDll holds the packaged dll to the vendored .def: checks 8, 9 and 10 (exit $code)"
$m = [regex]::Match($check, "urnet_version\(\) is '([^']*)'")
$ver = if ($m.Success) { $m.Groups[1].Value } else { '' }
Check ($ver -eq $DllVersion) "the packaged dll answers urnet_version() '$ver'"
$after = @(Get-ChildItem -LiteralPath $top -Recurse -Force | ForEach-Object { "$($_.FullName.Substring($top.Length)) $(if ($_.PSIsContainer) { 'd' } else { $_.Length })" })
Check ((@(Compare-Object $before $after)).Count -eq 0) 'the load check wrote nothing into the package folder'

# ---- 5. the zip ---------------------------------------------------------------------------------
foreach ($a in 'System.IO.Compression', 'System.IO.Compression.FileSystem') { try { Add-Type -AssemblyName $a } catch { Note "Add-Type $a : $_" } }
$zip = Join-Path $Out 'URmessage-alpha-03.zip'
$items = @(Get-Item -LiteralPath $top) + @(Get-ChildItem -LiteralPath $top -Recurse -Force)
$want = foreach ($i in $items) {
  $rel = $i.FullName.Substring($Out.Length + 1) -replace '\\', '/'
  if ($i.PSIsContainer) { [pscustomobject]@{ Name = "$rel/"; Length = 0; Path = $null } }
  else { [pscustomobject]@{ Name = $rel; Length = $i.Length; Path = $i.FullName } }
}
$want = @($want | Sort-Object -Property @{ Expression = { $_.Name.ToLowerInvariant() } }, @{ Expression = { $_.Name } })
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($w in $want) {
    if ($null -eq $w.Path) { [void]$archive.CreateEntry($w.Name) }
    else { [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $w.Path, $w.Name, [System.IO.Compression.CompressionLevel]::Optimal) }
  }
} finally { $archive.Dispose() }

# held to the folder, both ways: every entry is a file or directory of the folder with its length,
# and every file and directory of the folder is an entry
$read = [System.IO.Compression.ZipFile]::OpenRead($zip)
try { $got = @($read.Entries | ForEach-Object { [pscustomobject]@{ Name = $_.FullName; Length = $_.Length } }) } finally { $read.Dispose() }
$wantKeys = @($want | ForEach-Object { "$($_.Name) $($_.Length)" })
$gotKeys = @($got | ForEach-Object { "$($_.Name) $($_.Length)" })
$onlyZip = @($gotKeys | Where-Object { $wantKeys -notcontains $_ })
$onlyDir = @($wantKeys | Where-Object { $gotKeys -notcontains $_ })
Check ($onlyZip.Count -eq 0 -and $onlyDir.Count -eq 0) "the zip's entries are the folder's, names and lengths, both ways (only in the zip: $($onlyZip.Count); only in the folder: $($onlyDir.Count))"
Check (@($got | Where-Object { -not $_.Name.StartsWith('URmessage-alpha/') -or $_.Name.Contains('\') }).Count -eq 0) 'every entry is under URmessage-alpha/, with forward slashes'
Check ($got[0].Name -eq 'URmessage-alpha/') 'the first entry is the top folder, URmessage-alpha/'
$dirs = @($got | Where-Object { $_.Name.EndsWith('/') }).Count
$files = $got.Count - $dirs
Note "entries: $($got.Count) ($dirs directories, $files files)"
[IO.File]::WriteAllText((Join-Path $Evidence 'package03-filelist.txt'), (($gotKeys -join "`n") + "`n"))

# ---- 6. the sums --------------------------------------------------------------------------------
$zipSha = Sha $zip
$exeSha = Sha (Join-Path $top 'URmessage.exe')
$lines = @(
  "zip     $zipSha  URmessage-alpha-03.zip ($((Get-Item -LiteralPath $zip).Length) bytes, $($got.Count) entries)",
  "exe     $exeSha  URmessage-alpha/URmessage.exe ($((Get-Item -LiteralPath (Join-Path $top 'URmessage.exe')).Length) bytes)",
  "dll     $dllSha  URmessage-alpha/URnetworkSdk.dll ($((Get-Item -LiteralPath $dll).Length) bytes, urnet_version '$DllVersion')",
  "readme  $readmeSha  URmessage-alpha/READ-ME-FIRST.txt ($((Get-Item -LiteralPath $readme).Length) bytes)"
)
$lines | ForEach-Object { Note $_ }
$lines | Set-Content -LiteralPath (Join-Path $Evidence 'sha256.txt') -Encoding utf8
if ($env:GITHUB_OUTPUT) {
  "zip_sha256=$zipSha" >> $env:GITHUB_OUTPUT
  "exe_sha256=$exeSha" >> $env:GITHUB_OUTPUT
  "dll_sha256=$dllSha" >> $env:GITHUB_OUTPUT
  "readme_sha256=$readmeSha" >> $env:GITHUB_OUTPUT
  "entries=$($got.Count)" >> $env:GITHUB_OUTPUT
}

Note "$($script:failures) check(s) failed"
if ($script:failures -gt 0) { exit 1 }
exit 0
