[CmdletBinding()]
param()

$script:ArmrCandyProbeDefaultInputPaths = @(
    "native-runtime/win/aarch64",
    "windows/Build-ArmrAarch64.ps1",
    "windows/Resolve-NativeJdk.ps1",
    "java-stubs/isomr/Probe.java")

function Get-ArmrCandyEntryProbeInputFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [string[]]$RelativeInputPaths = $script:ArmrCandyProbeDefaultInputPaths
    )

    $allowedExtensions = @(
        ".c",
        ".cc",
        ".cpp",
        ".h",
        ".hpp",
        ".java",
        ".ps1",
        ".txt")
    $files = New-Object System.Collections.Generic.List[string]
    $repoPath = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\')

    foreach ($relativePath in $RelativeInputPaths) {
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }
        $fullPath = Join-Path $repoPath `
            ($relativePath.Trim() -replace '/', '\')
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            $files.Add($fullPath)
        } elseif (Test-Path -LiteralPath $fullPath -PathType Container) {
            $childFiles = Get-ChildItem -LiteralPath $fullPath -File -Recurse
            foreach ($child in $childFiles) {
                if ($allowedExtensions -contains $child.Extension.ToLowerInvariant()) {
                    $files.Add($child.FullName)
                }
            }
        }
    }

    # Ordinal sort: culture-sensitive ordering differs between Windows
    # PowerShell (NLS) and PowerShell 7 (ICU), which flip-flopped the stamp.
    $uniqueList = New-Object System.Collections.Generic.List[string]
    foreach ($file in $files) { $uniqueList.Add($file) }
    $uniqueList.Sort([System.StringComparer]::Ordinal)
    $seen = New-Object 'System.Collections.Generic.HashSet[string]'
    $uniqueFiles = @()
    foreach ($file in $uniqueList) {
        if ($seen.Add($file)) { $uniqueFiles += $file }
    }
    if ($uniqueFiles.Count -eq 0) {
        throw "No Candy entry probe native/build input files were found."
    }
    return $uniqueFiles
}

function Get-ArmrCandyEntryProbeInputFingerprint {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [string[]]$RelativeInputPaths = $script:ArmrCandyProbeDefaultInputPaths,

        [string[]]$ArtifactPaths = @()
    )

    $repoPath = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\')
    $files = Get-ArmrCandyEntryProbeInputFiles `
        -RepoRoot $repoPath `
        -RelativeInputPaths $RelativeInputPaths

    $revisionOutput = & git -C $repoPath rev-parse HEAD
    if ($LASTEXITCODE -ne 0) {
        throw "git rev-parse HEAD failed for Candy entry probe provenance."
    }
    $revision = ([string]$revisionOutput |
        Select-Object -First 1).Trim()
    if ([string]::IsNullOrWhiteSpace($revision)) {
        throw "git rev-parse HEAD returned an empty Candy entry probe revision."
    }

    $digestInput = New-Object System.Text.StringBuilder
    [void]$digestInput.AppendLine("revision=$revision")
    foreach ($file in $files) {
        $relativePath = $file.Substring($repoPath.Length + 1) -replace '\\', '/'
        $fileHash = Get-FileHash -LiteralPath $file -Algorithm SHA256
        [void]$digestInput.AppendLine(
            "file=$relativePath sha256=$($fileHash.Hash.ToLowerInvariant())")
    }
    foreach ($artifactPath in ($ArtifactPaths | Sort-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Candy entry probe provenance artifact is missing: $artifactPath"
        }
        $resolvedArtifactPath = [IO.Path]::GetFullPath($artifactPath)
        if (-not $resolvedArtifactPath.StartsWith(
                $repoPath + '\',
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Candy entry probe provenance artifact is outside the repo: " +
                $resolvedArtifactPath
        }
        $artifactRelativePath = `
            $resolvedArtifactPath.Substring($repoPath.Length + 1) -replace '\\', '/'
        $artifactHash = Get-FileHash `
            -LiteralPath $resolvedArtifactPath `
            -Algorithm SHA256
        [void]$digestInput.AppendLine(
            "artifact=$artifactRelativePath " +
            "sha256=$($artifactHash.Hash.ToLowerInvariant())")
    }

    $sha256 = [Security.Cryptography.SHA256]::Create()
    $hashBytes = $sha256.ComputeHash(
        [Text.Encoding]::UTF8.GetBytes($digestInput.ToString()))
    return ([BitConverter]::ToString($hashBytes) -replace '-', '').ToLowerInvariant()
}

function Get-ArmrCandyEntryProbeProvenance {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [string[]]$RelativeInputPaths = $script:ArmrCandyProbeDefaultInputPaths,

        [string[]]$ArtifactPaths = @()
    )

    $repoPath = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\')
    $revisionOutput = & git -C $repoPath rev-parse HEAD
    if ($LASTEXITCODE -ne 0) {
        throw "git rev-parse HEAD failed for Candy entry probe provenance."
    }
    $revision = ([string]$revisionOutput |
        Select-Object -First 1).Trim()
    $fingerprint = Get-ArmrCandyEntryProbeInputFingerprint `
        -RepoRoot $repoPath `
        -RelativeInputPaths $RelativeInputPaths `
        -ArtifactPaths $ArtifactPaths
    $files = Get-ArmrCandyEntryProbeInputFiles `
        -RepoRoot $repoPath `
        -RelativeInputPaths $RelativeInputPaths
    $artifactCount = @($ArtifactPaths | Sort-Object -Unique).Count
    $text = "ArmrCandyEntryProbeProvenance=1`n" +
        "revision=$revision`n" +
        "fingerprint=$fingerprint`n" +
        "inputs=$($files.Count)`n" +
        "artifacts=$artifactCount"

    return [pscustomobject]@{
        Text = $text
        Revision = $revision
        Fingerprint = $fingerprint
        Inputs = $files.Count
        Artifacts = $artifactCount
        InputFiles = $files
    }
}

function Write-ArmrCandyEntryProbeProvenance {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$StampPath,

        [string[]]$RelativeInputPaths = $script:ArmrCandyProbeDefaultInputPaths,

        [string[]]$ArtifactPaths = @()
    )

    $provenance = Get-ArmrCandyEntryProbeProvenance `
        -RepoRoot $RepoRoot `
        -RelativeInputPaths $RelativeInputPaths `
        -ArtifactPaths $ArtifactPaths
    $stampDirectory = Split-Path -Parent $StampPath
    if (-not (Test-Path -LiteralPath $stampDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $stampDirectory -Force | Out-Null
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText(
        $StampPath,
        $provenance.Text + [Environment]::NewLine,
        $utf8NoBom)
    return $provenance
}

function Test-ArmrCandyEntryProbeProvenance {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string]$StampPath,

        [string[]]$RelativeInputPaths = $script:ArmrCandyProbeDefaultInputPaths,

        [string[]]$ArtifactPaths = @()
    )

    if (-not (Test-Path -LiteralPath $StampPath -PathType Leaf)) {
        return [pscustomobject]@{
            Current = $false
            Reason = "Candy entry probe provenance stamp is missing: $StampPath"
            Provenance = $null
            StoredRevision = $null
            StoredFingerprint = $null
            StoredInputs = $null
            StoredArtifacts = $null
        }
    }

    $current = Get-ArmrCandyEntryProbeProvenance `
        -RepoRoot $RepoRoot `
        -RelativeInputPaths $RelativeInputPaths `
        -ArtifactPaths $ArtifactPaths
    $storedRaw = Get-Content -LiteralPath $StampPath -Raw
    $storedText = (($storedRaw -replace "`r`n", "`n") -replace "`r", "`n").Trim()
    $lines = $storedText -split "`n"
    $stored = @{}
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            $stored[$matches[1]] = $matches[2]
        }
    }

    $validFormat = $lines[0] -eq "ArmrCandyEntryProbeProvenance=1" -and
        -not [string]::IsNullOrWhiteSpace($stored["revision"]) -and
        -not [string]::IsNullOrWhiteSpace($stored["fingerprint"]) -and
        $stored["inputs"] -match '^\d+$' -and
        $stored["artifacts"] -match '^\d+$'
    $revisionMatches = $validFormat -and
        $stored["revision"] -eq $current.Revision
    $fingerprintMatches = $validFormat -and
        $stored["fingerprint"] -eq $current.Fingerprint
    $inputsMatch = $validFormat -and
        [int]$stored["inputs"] -eq $current.Inputs
    $artifactsMatch = $validFormat -and
        [int]$stored["artifacts"] -eq $current.Artifacts

    if (-not $validFormat) {
        $reason = "Candy entry probe provenance stamp is malformed: $StampPath"
    } elseif (-not $revisionMatches) {
        $reason = "Candy entry probe provenance revision " +
            "'$($stored["revision"])' does not match current " +
            "'$($current.Revision)'."
    } elseif (-not $fingerprintMatches) {
        $reason = "Candy entry probe provenance fingerprint does not match " +
            "current tracked native/build inputs."
    } elseif (-not $inputsMatch) {
        $reason = "Candy entry probe provenance input count " +
            "'$($stored["inputs"])' does not match current '$($current.Inputs)'."
    } elseif (-not $artifactsMatch) {
        $reason = "Candy entry probe provenance artifact count " +
            "'$($stored["artifacts"])' does not match current " +
            "'$($current.Artifacts)'."
    } else {
        $reason = $null
    }

    return [pscustomobject]@{
        Current = -not $reason
        Reason = $reason
        Provenance = $current
        StoredRevision = $stored["revision"]
        StoredFingerprint = $stored["fingerprint"]
        StoredInputs = $stored["inputs"]
        StoredArtifacts = $stored["artifacts"]
    }
}
