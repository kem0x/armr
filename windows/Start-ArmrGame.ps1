#requires -Version 5.1
<#
.SYNOPSIS
  Launch an Armr-supported Android game natively on Windows.

.DESCRIPTION
  Lists every games/<id>.profile manifest, lets you pick one (or pass -Game
  directly), and runs the native probe as `--game <id>`. The manifest carries
  the payload/data/lib paths and engine driver, so no game-specific paths or
  constants are hardcoded here.

.EXAMPLE
  .\Start-ArmrGame.ps1            # interactive picker
  .\Start-ArmrGame.ps1 -Game pvz1 # launch PvZ Free directly
#>
param(
    [string]$Game,
    [int]$Timeout,
    [string]$LogBase,
    [string]$ExtraArgs = "",
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$gamesDir = Join-Path $repo 'games'
$buildDir = Join-Path $repo 'native-runtime\win\build\aarch64\'
$profiles = Get-ChildItem $gamesDir -Filter '*.profile' -ErrorAction SilentlyContinue
if (-not $profiles) { throw "no game manifests in $gamesDir" }

function Read-ManifestField($path, $key) {
    foreach ($line in Get-Content $path) {
        $line = ($line -split '#')[0].Trim()
        if ($line -match '^\s*([A-Za-z_]+)\s*=\s*(.+?)\s*$' -and $Matches[1] -eq $key) {
            return $Matches[2]
        }
    }
    return $null
}

# Build the selectable game table.
$games = foreach ($f in $profiles) {
    [pscustomobject]@{
        Id    = $f.BaseName
        Name  = (Read-ManifestField $f.FullName 'name')
        Driver = (Read-ManifestField $f.FullName 'driver')
        Path  = $f.FullName
    }
}

if (-not $Game) {
    Write-Host "`nArmr - supported games" -ForegroundColor Cyan
    for ($i = 0; $i -lt $games.Count; $i++) {
        $g = $games[$i]
        Write-Host ("  [{0}] {1}  ({2}, driver={3})" -f ($i+1), $g.Name, $g.Id, $g.Driver)
    }
    $sel = Read-Host "`nSelect a game number"
    $idx = [int]$sel - 1
    if ($idx -lt 0 -or $idx -ge $games.Count) { throw "invalid selection '$sel'" }
    $Game = $games[$idx].Id
}
$chosen = $games | Where-Object { $_.Id -eq $Game }
if (-not $chosen) { throw "no manifest for game '$Game' in $gamesDir" }

if (-not $LogBase) { $LogBase = "game_$Game" }
$rt = if ($Timeout -gt 0) { $Timeout } else { [int](Read-ManifestField $chosen.Path 'render_timeout') }
if (-not $rt) { $rt = 600 }

New-Item -ItemType Directory -Force -Path (Join-Path $repo '.runtime') | Out-Null
$out = Join-Path $repo ".runtime\${LogBase}_run.log"
$err = Join-Path $repo ".runtime\${LogBase}_err.log"
Remove-Item $out, $err -Force -ErrorAction SilentlyContinue

# A profile may name its own host (`exe`) and argument template (`args`), as
# Candy Crush does; placeholders match the GUI launcher's. Otherwise the
# profile-driven host runs as `--game <id>`.
function Resolve-ProfilePath([string]$value) {
    $value = $value.Replace('{localappdata}', $env:LOCALAPPDATA).Replace('{repo}', $repo).Replace('{build}', $buildDir)
    if (-not [IO.Path]::IsPathRooted($value)) { $value = Join-Path $repo $value }
    return [IO.Path]::GetFullPath($value)
}
function Format-Arg([string]$path) { if ($path -match ' ') { return "`"$path`"" } return $path }
$exeName = Read-ManifestField $chosen.Path 'exe'
if (-not $exeName) { $exeName = 'ArmrCtrEntryProbe.exe' }
$exe = Join-Path $buildDir $exeName
if (-not (Test-Path $exe)) { throw "Not built: $exe. Run windows\Build-ArmrAarch64.ps1 first." }
$template = Read-ManifestField $chosen.Path 'args'
$payload = Resolve-ProfilePath (Read-ManifestField $chosen.Path 'payload_dir')
$rootLib = Join-Path $payload (Read-ManifestField $chosen.Path 'root_lib')
if (-not (Test-Path -LiteralPath $rootLib)) {
    throw "$($chosen.Name) is not imported. Run: windows\Import-ArmrGame.ps1 -Game $Game -Package <your .apk/.xapk>"
}
if ($template) {
    $data = Resolve-ProfilePath (Read-ManifestField $chosen.Path 'data_dir')
    New-Item -ItemType Directory -Force -Path $data | Out-Null
    $argLine = $template.Replace('{rootlib}', (Format-Arg $rootLib)).Replace('{payload}', (Format-Arg $payload)).
        Replace('{data}', (Format-Arg $data)).Replace('{id}', $Game).Replace('{name}', $chosen.Name).
        Replace('{localappdata}', $env:LOCALAPPDATA).Replace('{build}', $buildDir).Replace('{repo}', $repo)
    $argLine = ("$argLine $ExtraArgs").Trim()
} else {
    $argLine = ('--game {0} native-render-timeout={1} {2}' -f $Game, $rt, $ExtraArgs).Trim()
}
Write-Host ("Launching {0} ({1})" -f $chosen.Name, $Game) -ForegroundColor Green
Write-Host ("  exe : {0}" -f $exe)
Write-Host ("  args: {0}" -f $argLine)
Write-Host ("  log : {0}" -f $out)

if ($DryRun) { return }
$p = Start-Process -FilePath $exe -ArgumentList $argLine `
    -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
Write-Output "launched PID=$($p.Id) game=$Game out=$out err=$err"
