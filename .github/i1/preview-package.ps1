# THROWAWAY, NEVER MERGE (ci/i1-preview*). Identity phase 1, Task 6 Step 4: the preview zip, made
# the way package 03 was made (ci/task58-package-03's assemble.ps1), from the identity preview build
# (/p:UrmIdentityPreview=true).
#
# The folder is URmessage-identity-preview\: everything the Release build wrote to
# app\build\x64\Release, less the build by-products the per-user MSI leaves out (*.pdb, *.lib, *.exp,
# *.ilk, *.msi, *.wixpdb), plus READ-ME-FIRST.txt. It must NOT hold URnetworkSdk.dll: this build has
# no live mode, and the README says the library that encrypts is not in the zip.
#
# The zip holds one entry per directory (its name ends in "/", the top folder's among them) and one
# per file, every name under "URmessage-identity-preview/" with forward slashes, and is held to the
# folder both ways.
#
# Run from the repository root with pwsh 7. Never launches URmessage.exe.
param(
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][string]$Evidence
)
$ErrorActionPreference = 'Stop'
$script:failures = 0
function Note([string]$text) { Write-Host "[$(Get-Date -Format HH:mm:ss)] $text" }
function Check([bool]$ok, [string]$what) {
  if ($ok) { Note "PASS  $what" } else { Note "FAIL  $what"; $script:failures++ }
}
function Sha([string]$path) { return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() }

$name = 'URmessage-identity-preview'
$zipName = 'URmessage-identity-preview-slice0.zip'
$repo = (Get-Location).Path
$bin = Join-Path $repo 'app\build\x64\Release'
$readmeSrc = Join-Path $repo '.github\i1\READ-ME-FIRST.txt'
if (-not (Test-Path -LiteralPath $bin)) { throw "the build output $bin does not exist" }
if (Test-Path -LiteralPath $Out) { throw "$Out exists already; the package is made in a fresh folder" }
New-Item -ItemType Directory -Path $Out | Out-Null
New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
$top = Join-Path $Out $name
New-Item -ItemType Directory -Path $top | Out-Null

# ---- 1. the build output, less its by-products, and no SDK -------------------------------------
Check (-not (Test-Path -LiteralPath (Join-Path $bin 'URnetworkSdk.dll'))) 'the preview build output holds no URnetworkSdk.dll'
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
$runs = @($left | Where-Object { $_ -match '\.(exe|dll|pri|xbf|mui|winmd|json|ico|otf|ttf|png|html)\b' })
Check ($runs.Count -eq 0) "nothing left out is a file the app runs ($($runs.Count))"
$left | Set-Content -LiteralPath (Join-Path $Evidence 'left-out.txt') -Encoding utf8

# ---- 2. the README ------------------------------------------------------------------------------
$readme = Join-Path $top 'READ-ME-FIRST.txt'
Check (-not (Test-Path -LiteralPath $readme)) 'the build output has no READ-ME-FIRST.txt of its own'
Copy-Item -LiteralPath $readmeSrc -Destination $readme
$readmeSha = Sha $readme
Check ($readmeSha -eq (Sha $readmeSrc)) "the packaged README is the checked-out one, byte for byte (sha256 $readmeSha, $((Get-Item -LiteralPath $readme).Length) bytes)"
$readmeText = [IO.File]::ReadAllText($readme)
Check ($readmeText.Contains('These are the screens, with made-up data; nothing is encrypted in this build.')) "the README carries the owner's line"

# ---- 3. what must be there, and what must not ---------------------------------------------------
foreach ($need in @('URmessage.exe', 'resources.pri', 'App.xbf', 'MainWindow.xbf',
                    'Microsoft.WindowsAppRuntime.dll', 'Microsoft.WindowsAppRuntime.Bootstrap.dll',
                    'Microsoft.ui.xaml.dll', 'Assets\app.ico', 'vcruntime140.dll', 'msvcp140.dll',
                    'en-us\Microsoft.ui.xaml.dll.mui')) {
  Check (Test-Path -LiteralPath (Join-Path $top $need)) "the package holds $need"
}
Check (-not (Test-Path -LiteralPath (Join-Path $top 'URnetworkSdk.dll'))) 'the package holds no URnetworkSdk.dll'
Check (Test-Path -LiteralPath (Join-Path $top 'IDENTITY-PREVIEW-BUILD.txt')) 'the package holds IDENTITY-PREVIEW-BUILD.txt, the mark the MSI refuses'
$fonts = @(Get-ChildItem -LiteralPath (Join-Path $top 'Assets\Fonts') -File -ErrorAction SilentlyContinue)
Check ($fonts.Count -eq 4) "the package holds the four brand faces ($($fonts.Count))"

# ---- 4. the zip ---------------------------------------------------------------------------------
foreach ($a in 'System.IO.Compression', 'System.IO.Compression.FileSystem') { try { Add-Type -AssemblyName $a } catch { Note "Add-Type $a : $_" } }
$zip = Join-Path $Out $zipName
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
$read = [System.IO.Compression.ZipFile]::OpenRead($zip)
try { $got = @($read.Entries | ForEach-Object { [pscustomobject]@{ Name = $_.FullName; Length = $_.Length } }) } finally { $read.Dispose() }
$wantKeys = @($want | ForEach-Object { "$($_.Name) $($_.Length)" })
$gotKeys = @($got | ForEach-Object { "$($_.Name) $($_.Length)" })
$onlyZip = @($gotKeys | Where-Object { $wantKeys -notcontains $_ })
$onlyDir = @($wantKeys | Where-Object { $gotKeys -notcontains $_ })
Check ($onlyZip.Count -eq 0 -and $onlyDir.Count -eq 0) "the zip's entries are the folder's, names and lengths, both ways (only in the zip: $($onlyZip.Count); only in the folder: $($onlyDir.Count))"
Check (@($got | Where-Object { -not $_.Name.StartsWith("$name/") -or $_.Name.Contains('\') }).Count -eq 0) "every entry is under $name/, with forward slashes"
Check ($got[0].Name -eq "$name/") "the first entry is the top folder, $name/"
Check (@($got | Where-Object { $_.Name -like '*URnetworkSdk*' }).Count -eq 0) 'no entry of the zip is URnetworkSdk'
$dirs = @($got | Where-Object { $_.Name.EndsWith('/') }).Count
$files = $got.Count - $dirs
Note "entries: $($got.Count) ($dirs directories, $files files)"
[IO.File]::WriteAllText((Join-Path $Evidence 'preview-filelist.txt'), (($gotKeys -join "`n") + "`n"))

# ---- 5. the sums --------------------------------------------------------------------------------
$zipSha = Sha $zip
$exeSha = Sha (Join-Path $top 'URmessage.exe')
$lines = @(
  "zip     $zipSha  $zipName ($((Get-Item -LiteralPath $zip).Length) bytes, $($got.Count) entries)",
  "exe     $exeSha  $name/URmessage.exe ($((Get-Item -LiteralPath (Join-Path $top 'URmessage.exe')).Length) bytes)",
  "readme  $readmeSha  $name/READ-ME-FIRST.txt ($((Get-Item -LiteralPath $readme).Length) bytes)"
)
$lines | ForEach-Object { Note $_ }
$lines | Set-Content -LiteralPath (Join-Path $Evidence 'sha256.txt') -Encoding utf8
if ($env:GITHUB_OUTPUT) {
  "zip_sha256=$zipSha" >> $env:GITHUB_OUTPUT
  "exe_sha256=$exeSha" >> $env:GITHUB_OUTPUT
  "readme_sha256=$readmeSha" >> $env:GITHUB_OUTPUT
  "entries=$($got.Count)" >> $env:GITHUB_OUTPUT
}

Note "$($script:failures) check(s) failed"
if ($script:failures -gt 0) { exit 1 }
exit 0
