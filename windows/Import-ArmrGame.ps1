#requires -Version 5.1
<#
.SYNOPSIS
  Stage an Android game package (.apk, .xapk or .apks) for ARMR.

.DESCRIPTION
  Reads games/<id>.profile, extracts the package into the profile's payload
  directory (64-bit ARM libraries only), creates the data directory, and runs
  any per-game import steps the profile declares:

    import_repack_stored = <payload zip>
        Rewrite a zip/OBB inside the payload with every entry stored
        uncompressed (for engines that memory-map their archive).
    import_extract = <payload zip>|<entry>|<data file>
        Copy one archive entry out to a file in the data directory.

  Save data in the data directory is never touched. Re-importing over an
  existing payload requires -Force.

  You must supply your own legally obtained copy of the game. ARMR does not
  download or distribute game files.

.EXAMPLE
  .\windows\Import-ArmrGame.ps1 -Game pvz1 -Package "D:\Downloads\pvz-free.xapk"
#>
param(
    [Parameter(Mandatory = $true)][string]$Game,
    [Parameter(Mandatory = $true)][string]$Package,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repo = Split-Path $PSScriptRoot -Parent
$profilePath = Join-Path $repo "games\$Game.profile"
if (-not (Test-Path -LiteralPath $profilePath)) {
    $known = (Get-ChildItem (Join-Path $repo 'games') -Filter '*.profile').BaseName -join ', '
    throw "No profile games\$Game.profile. Known games: $known"
}
if (-not (Test-Path -LiteralPath $Package -PathType Leaf)) {
    throw "Package not found: $Package"
}
$Package = (Resolve-Path -LiteralPath $Package).Path

function Read-ProfileValues([string]$path, [string]$key) {
    $values = @()
    foreach ($line in Get-Content -LiteralPath $path) {
        $line = ($line -split '#')[0].Trim()
        if ($line -match '^([A-Za-z_]+)\s*=\s*(.+?)$' -and $Matches[1] -eq $key) {
            $values += $Matches[2].Trim()
        }
    }
    return $values
}

function Resolve-ProfileDir([string]$value) {
    $value = $value.Replace('{localappdata}', $env:LOCALAPPDATA)
    $value = $value.Replace('{repo}', $repo)
    if (-not [IO.Path]::IsPathRooted($value)) { $value = Join-Path $repo $value }
    return [IO.Path]::GetFullPath($value)
}

function Get-SafeDestination([string]$root, [string]$entryName) {
    $dest = [IO.Path]::GetFullPath((Join-Path $root $entryName))
    $prefix = $root.TrimEnd('\') + '\'
    if (-not $dest.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing archive entry outside the payload: $entryName"
    }
    return $dest
}

# Only 64-bit ARM native code runs on ARMR; other ABIs are dead weight.
$foreignAbi = '^lib/(armeabi|armeabi-v7a|x86|x86_64|mips|mips64)/'
$foreignSplit = '^config\.(armeabi|armeabi_v7a|x86|x86_64|mips|mips64)\.apk$'

function Expand-Apk([IO.Compression.ZipArchive]$apk, [string]$payloadRoot) {
    $count = 0
    foreach ($entry in $apk.Entries) {
        $name = $entry.FullName.Replace('\', '/')
        if ($name.EndsWith('/') -or $name -match $foreignAbi) { continue }
        $dest = Get-SafeDestination $payloadRoot $name
        New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
        $count++
    }
    return $count
}

$payloadValue = (Read-ProfileValues $profilePath 'payload_dir') | Select-Object -First 1
$dataValue = (Read-ProfileValues $profilePath 'data_dir') | Select-Object -First 1
$rootLib = (Read-ProfileValues $profilePath 'root_lib') | Select-Object -First 1
$packageName = (Read-ProfileValues $profilePath 'package_name') | Select-Object -First 1
if (-not $payloadValue -or -not $dataValue -or -not $rootLib) {
    throw "games\$Game.profile needs payload_dir, data_dir and root_lib."
}
$payloadRoot = Resolve-ProfileDir $payloadValue
$dataRoot = Resolve-ProfileDir $dataValue

if ((Test-Path -LiteralPath $payloadRoot) -and
    (Get-ChildItem -LiteralPath $payloadRoot -Force | Select-Object -First 1)) {
    if (-not $Force) {
        throw "Payload already staged at $payloadRoot. Re-run with -Force to replace it (save data is kept)."
    }
    Remove-Item -LiteralPath $payloadRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $payloadRoot, $dataRoot | Out-Null

Write-Host "Importing $(Split-Path -Leaf $Package) as '$Game'" -ForegroundColor Cyan
$files = 0
$outer = [IO.Compression.ZipFile]::OpenRead($Package)
try {
    $innerApks = @($outer.Entries | Where-Object {
            $_.FullName -notmatch '/' -and $_.FullName -like '*.apk' })
    if ($innerApks.Count -eq 0) {
        # A plain APK.
        $files += Expand-Apk $outer $payloadRoot
    } else {
        # A split bundle (.xapk / .apks): base APK first, then config splits.
        $manifestEntry = $outer.GetEntry('manifest.json')
        if ($manifestEntry -and $packageName) {
            $reader = New-Object IO.StreamReader($manifestEntry.Open())
            try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
            if ($manifest.package_name -and $manifest.package_name -ne $packageName) {
                Write-Warning "Package is $($manifest.package_name) but the profile expects $packageName."
            }
        }
        $ordered = @($innerApks | Where-Object { $_.Name -notlike 'config.*' }) +
            @($innerApks | Where-Object { $_.Name -like 'config.*' -and $_.Name -notmatch $foreignSplit })
        foreach ($entry in $ordered) {
            Write-Host "  extracting $($entry.Name)"
            $stream = $entry.Open()
            $buffer = New-Object IO.MemoryStream
            try { $stream.CopyTo($buffer) } finally { $stream.Dispose() }
            $buffer.Position = 0
            $apk = New-Object IO.Compression.ZipArchive($buffer, [IO.Compression.ZipArchiveMode]::Read)
            try { $files += Expand-Apk $apk $payloadRoot } finally { $apk.Dispose(); $buffer.Dispose() }
        }
        # Expansion files ship beside the APKs as Android/obb/<package>/*.obb.
        foreach ($entry in $outer.Entries | Where-Object { $_.FullName -like 'Android/obb/*.obb' }) {
            Write-Host "  extracting $($entry.FullName)"
            $dest = Get-SafeDestination $payloadRoot $entry.FullName
            New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
            $files++
        }
    }
} finally {
    $outer.Dispose()
}

$rootLibPath = Join-Path $payloadRoot $rootLib
if (-not (Test-Path -LiteralPath $rootLibPath -PathType Leaf)) {
    throw "The package has no $rootLib. ARMR needs the 64-bit ARM (arm64-v8a) build of this game."
}

foreach ($target in @(Read-ProfileValues $profilePath 'import_repack_stored')) {
    $archive = Get-SafeDestination $payloadRoot $target
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw "import_repack_stored: $target not found in the payload." }
    Write-Host "  storing $target uncompressed"
    $temp = "$archive.tmp"
    $source = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $outStream = [IO.File]::Create($temp)
        $out = New-Object IO.Compression.ZipArchive($outStream, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($entry in $source.Entries) {
                $copy = $out.CreateEntry($entry.FullName, [IO.Compression.CompressionLevel]::NoCompression)
                $copy.LastWriteTime = $entry.LastWriteTime
                if ($entry.FullName.EndsWith('/')) { continue }
                $from = $entry.Open(); $to = $copy.Open()
                try { $from.CopyTo($to) } finally { $to.Dispose(); $from.Dispose() }
            }
        } finally { $out.Dispose(); $outStream.Dispose() }
    } finally { $source.Dispose() }
    Move-Item -LiteralPath $temp -Destination $archive -Force
}

foreach ($step in @(Read-ProfileValues $profilePath 'import_extract')) {
    $parts = $step -split '\|'
    if ($parts.Count -ne 3) { throw "import_extract must be <payload zip>|<entry>|<data file>: $step" }
    $archive = Get-SafeDestination $payloadRoot $parts[0].Trim()
    $output = Get-SafeDestination $dataRoot $parts[2].Trim()
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entry = $zip.GetEntry($parts[1].Trim())
        if (-not $entry) { throw "import_extract: $($parts[1]) not found in $($parts[0])." }
        Write-Host "  extracting $($parts[1].Trim()) -> data\$($parts[2].Trim())"
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $output, $true)
    } finally { $zip.Dispose() }
}

Write-Host ("Staged {0} files." -f $files) -ForegroundColor Green
Write-Host "  payload: $payloadRoot"
Write-Host "  data   : $dataRoot"
Write-Host "Launch it with Armr.cmd, or: .\windows\Start-ArmrGame.ps1 -Game $Game"
