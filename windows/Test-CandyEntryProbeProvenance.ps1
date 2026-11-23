[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "CandyEntryProbeProvenance.ps1")

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ("isomr-candy-provenance-" + [guid]::NewGuid().ToString("N"))
try {
    $testRepo = Join-Path $tempRoot "repo"
    New-Item -ItemType Directory -Path $testRepo -Force | Out-Null
    & git -C $testRepo init -q
    if ($LASTEXITCODE -ne 0) {
        throw "git init failed for the provenance test repository."
    }

    $aarch64Dir = Join-Path $testRepo "native-runtime\win\aarch64"
    $windowsDir = Join-Path $testRepo "windows"
    $javaDir = Join-Path $testRepo "java-stubs\isomr"
    New-Item -ItemType Directory -Path `
        $aarch64Dir, $windowsDir, $javaDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $aarch64Dir "CMakeLists.txt") `
        -Value "cmake_minimum_required(VERSION 3.20)"
    Set-Content -LiteralPath (Join-Path $aarch64Dir "candy_entry_probe.cpp") `
        -Value "int main() { return 0; }"
    Set-Content -LiteralPath `
        (Join-Path $windowsDir "Build-ArmrAarch64.ps1") `
        -Value "param()"
    Set-Content -LiteralPath `
        (Join-Path $windowsDir "Invoke-CandyNativeEntryProbe.ps1") `
        -Value "param()"
    Set-Content -LiteralPath (Join-Path $windowsDir "Resolve-NativeJdk.ps1") `
        -Value "param()"
    Set-Content -LiteralPath (Join-Path $javaDir "Probe.java") `
        -Value "package isomr;"

    & git -C $testRepo add -A
    if ($LASTEXITCODE -ne 0) {
        throw "git add failed for the provenance test repository."
    }
    & git -C $testRepo -c user.name="Armr Test" `
        -c user.email="isomr-test@example.com" commit -q -m "seed"
    if ($LASTEXITCODE -ne 0) {
        throw "git commit failed for the provenance test repository."
    }

    $buildDir = Join-Path $testRepo "native-runtime\win\build\aarch64"
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    $artifact = Join-Path $buildDir "ArmrCandyEntryProbe.exe"
    $originalArtifactBytes = [Text.Encoding]::ASCII.GetBytes("probe")
    [IO.File]::WriteAllBytes($artifact, $originalArtifactBytes)
    $artifactPaths = @($artifact)

    $relativeInputPaths = @(
        "native-runtime/win/aarch64",
        "windows/Build-ArmrAarch64.ps1",
        "windows/Invoke-CandyNativeEntryProbe.ps1",
        "windows/Resolve-NativeJdk.ps1",
        "java-stubs/isomr/Probe.java")
    $stampDirectory = Join-Path $tempRoot "stamps"
    New-Item -ItemType Directory -Path $stampDirectory -Force | Out-Null
    $freshStamp = Join-Path $stampDirectory "fresh.provenance"

    $fresh = Write-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -StampPath $freshStamp `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    $freshStatus = Test-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -StampPath $freshStamp `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    if (-not $freshStatus.Current -or
        $fresh.Revision -notmatch '^[0-9a-f]{40}$' -or
        $fresh.Fingerprint -notmatch '^[0-9a-f]{64}$') {
        throw "Fresh Candy entry probe provenance was not accepted."
    }

    $missingStatus = Test-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -StampPath (Join-Path $stampDirectory "missing.provenance") `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    if ($missingStatus.Current) {
        throw "Missing Candy entry probe provenance was accepted."
    }

    $staleRevisionStamp = Join-Path $stampDirectory "stale-revision.provenance"
    [IO.File]::WriteAllText(
        $staleRevisionStamp,
        "ArmrCandyEntryProbeProvenance=1`n" +
            "revision=0000000000000000000000000000000000000000`n" +
            "fingerprint=$($fresh.Fingerprint)`n" +
            "inputs=$($fresh.Inputs)`n" +
            "artifacts=$($fresh.Artifacts)`n",
        (New-Object System.Text.UTF8Encoding($false)))
    $staleRevisionStatus = Test-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -StampPath $staleRevisionStamp `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    if ($staleRevisionStatus.Current) {
        throw "Stale Candy entry probe provenance revision was accepted."
    }

    $staleFingerprintStamp = `
        Join-Path $stampDirectory "stale-fingerprint.provenance"
    [IO.File]::WriteAllText(
        $staleFingerprintStamp,
        "ArmrCandyEntryProbeProvenance=1`n" +
            "revision=$($fresh.Revision)`n" +
            "fingerprint=$('0' * 64)`n" +
            "inputs=$($fresh.Inputs)`n" +
            "artifacts=$($fresh.Artifacts)`n",
        (New-Object System.Text.UTF8Encoding($false)))
    $staleFingerprintStatus = Test-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -StampPath $staleFingerprintStamp `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    if ($staleFingerprintStatus.Current) {
        throw "Stale Candy entry probe provenance fingerprint was accepted."
    }

    [IO.File]::WriteAllBytes(
        $artifact,
        [Text.Encoding]::ASCII.GetBytes("stale artifact"))
    $staleArtifactStatus = Test-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -StampPath $freshStamp `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    if ($staleArtifactStatus.Current) {
        throw "Stale Candy entry probe executable was accepted as fresh."
    }
    [IO.File]::WriteAllBytes($artifact, $originalArtifactBytes)

    Add-Content -LiteralPath `
        (Join-Path $aarch64Dir "candy_entry_probe.cpp") `
        -Value "// local edit"
    $dirty = Get-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    if ($dirty.Fingerprint -eq $fresh.Fingerprint) {
        throw "Dirty native input did not change Candy entry probe provenance."
    }
    $dirtyStatus = Test-ArmrCandyEntryProbeProvenance `
        -RepoRoot $testRepo `
        -StampPath $freshStamp `
        -RelativeInputPaths $relativeInputPaths `
        -ArtifactPaths $artifactPaths
    if ($dirtyStatus.Current) {
        throw "Dirty Candy entry probe provenance was accepted as fresh."
    }

    $probeLauncherText = Get-Content -LiteralPath `
        (Join-Path $PSScriptRoot "Invoke-CandyNativeEntryProbe.ps1") -Raw
    $probeBuilderText = Get-Content -LiteralPath `
        (Join-Path $PSScriptRoot "Build-ArmrAarch64.ps1") -Raw
    $probeGateText = Get-Content -LiteralPath `
        (Join-Path $PSScriptRoot "Test-NativeAarch64.ps1") -Raw
    $artifactExpression = '-ArtifactPaths @($probe)'
    $buildArtifactExpression = '-ArtifactPaths @($candyEntryProbeOutput)'
    $literalArtifactContract = '''-ArtifactPaths @($probe)'''
    $literalBuildArtifactContract = '''-ArtifactPaths @($candyEntryProbeOutput)'''
    if (-not $probeLauncherText.Contains($artifactExpression)) {
        throw "Candy entry probe launcher lacks literal artifact contract: " +
            $artifactExpression
    }
    if (-not $probeGateText.Contains($literalArtifactContract)) {
        throw "Candy entry probe gate must use the literal artifact contract: " +
            $literalArtifactContract
    }
    if (-not $probeBuilderText.Contains($buildArtifactExpression)) {
        throw "Candy entry probe builder lacks literal artifact contract: " +
            $buildArtifactExpression
    }
    if (-not $probeGateText.Contains($literalBuildArtifactContract)) {
        throw "Candy entry probe gate must use the literal build artifact contract: " +
            $literalBuildArtifactContract
    }

    Write-Output ("ISOMR_CANDY_ENTRY_PROBE_PROVENANCE " +
        "fresh=pass missing=pass stale=pass artifact=pass dirty=pass literal=pass")
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
