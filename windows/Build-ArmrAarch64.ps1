param(
    [string]$DynarmicSource = $env:ISOMR_DYNARMIC_SOURCE,
    [string]$BoostInclude = $env:ISOMR_BOOST_INCLUDE,
    [string]$AngleRoot = $env:ISOMR_ANGLE_ROOT,
    [string]$JavaHome = $env:JAVA_HOME,
    [string]$WebP103Source = $env:ISOMR_WEBP103_SOURCE,
    [string]$RapidJson110Source = $env:ISOMR_RAPIDJSON110_SOURCE,
    [switch]$Clean,
    [switch]$CoreOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $repoRoot "native-runtime\win\aarch64"
$nativeBuildRoot = Join-Path $repoRoot "native-runtime\win\build"
$buildDir = Join-Path $nativeBuildRoot "aarch64"
$webpBuildDir = Join-Path $nativeBuildRoot "webp103"
$output = Join-Path $buildDir "ArmrAarch64Smoke.exe"
$elfOutput = Join-Path $buildDir "ArmrElf64Smoke.exe"
$jumpsOutput = Join-Path $buildDir "ArmrBionicJumpsSmoke.exe"
$probeOutput = Join-Path $buildDir "ArmrElf64Probe.exe"
$profileProbeOutput = Join-Path $buildDir "ArmrProfileProbe.exe"
$candyEntrySmokeOutput = Join-Path $buildDir "ArmrCandyEntrySmoke.exe"
$candyEntryProbeOutput = Join-Path $buildDir "ArmrCandyEntryProbe.exe"
$candyProbeProvenanceOutput = `
    Join-Path $buildDir "ArmrCandyEntryProbe.exe.provenance"
$candyProbeProvenanceScript = `
    Join-Path $PSScriptRoot "CandyEntryProbeProvenance.ps1"
$webp103Output = Join-Path $buildDir "ArmrWebP103.dll"
$webp103Library = Join-Path $webpBuildDir "webpdecoder.lib"
$threadOutput = Join-Path $buildDir "ArmrThreadSmoke.exe"
$desktopJniOutput = Join-Path $buildDir "ArmrGuestJniDesktopSmoke.exe"
$nativeGlesOutput = Join-Path $buildDir "ArmrNativeGlesSmoke.exe"
$vorbisRuntimeOutputs = @(
    (Join-Path $buildDir "vorbisfile.dll"),
    (Join-Path $buildDir "vorbis.dll"),
    (Join-Path $buildDir "ogg.dll"))
$probeClasses = Join-Path $buildDir "java-classes"
. (Join-Path $PSScriptRoot "Resolve-NativeJdk.ps1")
$requiredRevision = "9d4582339990d4eae53f1dc7160686920fc2075c"
$requiredWebP103Revision = "0fe1a89dbf1930fc2554dbe76adad5d962054ead"
$requiredRapidJson110Revision = "f54b0e47a08782a6131cc3d60f94d038fa6e0a51"

function Resolve-DynarmicSource {
    param([string]$Requested)

    $candidate = $Requested
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = Join-Path $repoRoot "native-runtime\win\third_party\dynarmic"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $candidate "CMakeLists.txt") -PathType Leaf)) {
        throw "Pinned Dynarmic source not found. Set ISOMR_DYNARMIC_SOURCE or place it under native-runtime\win\third_party\dynarmic\."
    }
    $candidate = (Resolve-Path -LiteralPath $candidate).Path
    $revisionOutput = & git -C $candidate rev-parse HEAD 2>$null
    $gitExitCode = $LASTEXITCODE
    $revision = ([string]($revisionOutput | Select-Object -First 1)).Trim()
    if ($gitExitCode -ne 0 -or $revision -ne $requiredRevision) {
        throw "Dynarmic must be a clean checkout at revision $requiredRevision (found '$revision')."
    }
    # The runtime needs one small Dynarmic patch (A64 hint-instruction hooks).
    # A clean pinned checkout gets the patch applied here; a checkout that
    # already carries exactly that patch is accepted; anything else is refused.
    $patch = Join-Path $repoRoot `
        "native-runtime\win\third_party\patches\dynarmic-a64-hook-hint-instructions.patch"
    $patchedFiles = @(Select-String -LiteralPath $patch -Pattern '^\+\+\+ b/(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value }) | Sort-Object
    $untracked = @(& git -C $candidate ls-files --others --exclude-standard)
    $changed = @(& git -C $candidate diff --name-only HEAD) | Sort-Object
    if ($untracked.Count -gt 0) {
        throw "Dynarmic source has untracked files; use a clean pinned checkout."
    }
    if ($changed.Count -eq 0) {
        & git -C $candidate apply $patch
        if ($LASTEXITCODE -ne 0) {
            throw "Could not apply $patch to the Dynarmic checkout."
        }
        Write-Host "Applied Dynarmic patch: $(Split-Path -Leaf $patch)"
    } else {
        & git -C $candidate apply --reverse --check $patch 2>$null
        $alreadyPatched = $LASTEXITCODE -eq 0
        if (-not $alreadyPatched -or
            (Compare-Object $changed $patchedFiles).Count -gt 0) {
            throw "Dynarmic source has local changes other than $(Split-Path -Leaf $patch); use a clean pinned checkout."
        }
    }
    return $candidate
}

function Resolve-BoostInclude {
    param([string]$Requested)

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        $candidates.Add($Requested)
    }
    $vcpkgPointer = Join-Path $env:LOCALAPPDATA "vcpkg\vcpkg.path.txt"
    if (Test-Path -LiteralPath $vcpkgPointer -PathType Leaf) {
        $vcpkgRoot = (Get-Content -LiteralPath $vcpkgPointer -Raw).Trim()
        if (-not [string]::IsNullOrWhiteSpace($vcpkgRoot)) {
            $candidates.Add((Join-Path $vcpkgRoot "installed\x64-windows\include"))
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "boost\version.hpp") -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Native Boost headers not found. Set ISOMR_BOOST_INCLUDE to a directory containing boost\version.hpp."
}

function Resolve-WebP103Source {
    param([string]$Requested)

    $candidate = $Requested
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = Join-Path $repoRoot `
            "native-runtime\win\third_party\libwebp-1.0.3"
    }
    if (-not (Test-Path -LiteralPath `
            (Join-Path $candidate "src\webp\decode.h") -PathType Leaf)) {
        throw "Exact libwebp 1.0.3 source not found. Set ISOMR_WEBP103_SOURCE or pass -WebP103Source."
    }
    $candidate = (Resolve-Path -LiteralPath $candidate).Path
    $revisionOutput = & git -C $candidate rev-parse HEAD 2>$null
    $gitExitCode = $LASTEXITCODE
    $revision = ([string]($revisionOutput | Select-Object -First 1)).Trim()
    if ($gitExitCode -ne 0 -or $revision -ne $requiredWebP103Revision) {
        throw "libwebp must be a clean checkout of v1.0.3 at $requiredWebP103Revision (found '$revision')."
    }
    if ((& git -C $candidate status --short).Count -gt 0) {
        throw "libwebp 1.0.3 source has local changes; use a clean pinned checkout."
    }
    return $candidate
}

function Resolve-RapidJson110Source {
    param([string]$Requested)

    $candidate = $Requested
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = Join-Path $repoRoot `
            "native-runtime\win\third_party\rapidjson-1.1.0"
    }
    if (-not (Test-Path -LiteralPath `
            (Join-Path $candidate "include\rapidjson\document.h") `
            -PathType Leaf)) {
        throw "Exact RapidJSON 1.1.0 source not found. Set ISOMR_RAPIDJSON110_SOURCE or pass -RapidJson110Source."
    }
    $candidate = (Resolve-Path -LiteralPath $candidate).Path
    $revisionOutput = & git -C $candidate rev-parse HEAD 2>$null
    $gitExitCode = $LASTEXITCODE
    $revision = ([string]($revisionOutput | Select-Object -First 1)).Trim()
    if ($gitExitCode -ne 0 -or $revision -ne $requiredRapidJson110Revision) {
        throw "RapidJSON must be a clean checkout of v1.1.0 at $requiredRapidJson110Revision (found '$revision')."
    }
    if ((& git -C $candidate status --short).Count -gt 0) {
        throw "RapidJSON 1.1.0 source has local changes; use a clean pinned checkout."
    }
    return $candidate
}

function Resolve-AngleRoot {
    param(
        [string]$Requested,
        [string]$ResolvedBoostInclude
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        $candidates.Add($Requested)
    }
    if (-not [string]::IsNullOrWhiteSpace($ResolvedBoostInclude)) {
        $candidates.Add((Split-Path -Parent $ResolvedBoostInclude))
    }
    foreach ($candidate in $candidates) {
        $required = @(
            (Join-Path $candidate "include\EGL\egl.h"),
            (Join-Path $candidate "include\GLES2\gl2.h"),
            (Join-Path $candidate "lib\libEGL.lib"),
            (Join-Path $candidate "lib\libGLESv2.lib"),
            (Join-Path $candidate "bin\libEGL.dll"),
            (Join-Path $candidate "bin\libGLESv2.dll"))
        if (($required | Where-Object {
                    -not (Test-Path -LiteralPath $_ -PathType Leaf)
                }).Count -eq 0) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Native ANGLE was not found. Set ISOMR_ANGLE_ROOT to an x64-windows prefix containing include, lib, and bin ANGLE files."
}

function Resolve-MsvcEnvironment {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "Visual Studio Installer's vswhere.exe was not found."
    }
    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($installation)) {
        throw "MSVC x64 build tools were not found."
    }
    $vcvars = Join-Path $installation "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
        throw "MSVC environment script not found: $vcvars"
    }
    return $vcvars
}

$dynarmic = Resolve-DynarmicSource -Requested $DynarmicSource
$webp103 = Resolve-WebP103Source -Requested $WebP103Source
$rapidJson110 = Resolve-RapidJson110Source -Requested $RapidJson110Source
$boost = Resolve-BoostInclude -Requested $BoostInclude
$angle = Resolve-AngleRoot -Requested $AngleRoot `
    -ResolvedBoostInclude $boost
$jdk = Resolve-ArmrNativeJdk -Requested $JavaHome
$vcvars64 = Resolve-MsvcEnvironment
foreach ($tool in @("cmake.exe", "ninja.exe")) {
    if ($null -eq (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool is required for the native AArch64 execution build."
    }
}

$resolvedBuildRoot = [IO.Path]::GetFullPath($nativeBuildRoot).TrimEnd('\') + '\'
$resolvedBuildDir = [IO.Path]::GetFullPath($buildDir)
$resolvedWebPBuildDir = [IO.Path]::GetFullPath($webpBuildDir)
foreach ($resolvedOutputDir in @($resolvedBuildDir, $resolvedWebPBuildDir)) {
    if (-not $resolvedOutputDir.StartsWith(
            $resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Native build output escaped the native build directory: $resolvedOutputDir"
    }
}
if ($Clean) {
    foreach ($cleanDir in @($buildDir, $webpBuildDir)) {
        if (Test-Path -LiteralPath $cleanDir) {
            Remove-Item -LiteralPath $cleanDir -Recurse -Force
        }
    }
}
New-Item -ItemType Directory -Path $buildDir, $webpBuildDir -Force |
    Out-Null

# The profile-driven game probe (ArmrCtrEntryProbe.exe, used by `--game`
# titles and the launcher) is built by default. -CoreOnly skips it; the
# native test gate uses that to check the core runtime in isolation.
$ctrExperimentalFlag = if ($CoreOnly) {
    "-DISOMR_BUILD_EXPERIMENTAL_CTR=OFF"
} else {
    "-DISOMR_BUILD_EXPERIMENTAL_CTR=ON"
}
$command = "call `"$vcvars64`" >NUL && " +
    "cmake -S `"$webp103`" -B `"$webpBuildDir`" -G Ninja " +
    "-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF " +
    "-DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF " +
    "-DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF " +
    "-DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF " +
    "-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF " +
    "-DWEBP_BUILD_EXTRAS=OFF && " +
    "cmake --build `"$webpBuildDir`" --target webpdecoder && " +
    "cmake -S `"$projectDir`" -B `"$buildDir`" -G Ninja " +
    "-DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 " +
    "$ctrExperimentalFlag " +
    "-DISOMR_DYNARMIC_SOURCE=`"$dynarmic`" " +
    "-DISOMR_JAVA_HOME=`"$($jdk.Home)`" " +
    "-DISOMR_ANGLE_ROOT=`"$angle`" " +
    "-DISOMR_WEBP103_INCLUDE_DIR=`"$webp103`" " +
    "-DISOMR_WEBP103_LIBRARY=`"$webp103Library`" " +
    "-DISOMR_RAPIDJSON110_INCLUDE_DIR=`"$(Join-Path $rapidJson110 'include')`" " +
    "-DBOOST_INCLUDEDIR=`"$boost`" && " +
    "cmake --build `"$buildDir`" --config Release"
# A stray quote in any PATH entry (e.g. `...\PowerShell\7"`) makes
# vcvars64.bat fail with "\<dir> was unexpected at this time". Normalize the
# PATH for this process only.
$env:Path = (($env:Path -split ';') | ForEach-Object { $_.Trim().Trim('"') } |
    Where-Object { $_ }) -join ';'
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Native AArch64 execution build failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
    throw "Native AArch64 smoke executable is missing: $output"
}
if (-not (Test-Path -LiteralPath $elfOutput -PathType Leaf)) {
    throw "Native ELF64 smoke executable is missing: $elfOutput"
}
if (-not (Test-Path -LiteralPath $jumpsOutput -PathType Leaf)) {
    throw "Native Bionic jumps smoke executable is missing: $jumpsOutput"
}
if (-not (Test-Path -LiteralPath $probeOutput -PathType Leaf)) {
    throw "Native ELF64 probe executable is missing: $probeOutput"
}
if (-not (Test-Path -LiteralPath $profileProbeOutput -PathType Leaf)) {
    throw "Native profile probe executable is missing: $profileProbeOutput"
}
if (-not (Test-Path -LiteralPath $candyEntrySmokeOutput -PathType Leaf)) {
    throw "Native Candy entry smoke executable is missing: $candyEntrySmokeOutput"
}
if (-not (Test-Path -LiteralPath $candyEntryProbeOutput -PathType Leaf)) {
    throw "Native Candy entry probe executable is missing: $candyEntryProbeOutput"
}
if (-not (Test-Path -LiteralPath $threadOutput -PathType Leaf)) {
    throw "Native thread/TLS smoke executable is missing: $threadOutput"
}
if (-not (Test-Path -LiteralPath $desktopJniOutput -PathType Leaf)) {
    throw "Native desktop JNI smoke executable is missing: $desktopJniOutput"
}
if (-not (Test-Path -LiteralPath $nativeGlesOutput -PathType Leaf)) {
    throw "Native GLES smoke executable is missing: $nativeGlesOutput"
}
if (-not (Test-Path -LiteralPath $webp103Output -PathType Leaf)) {
    throw "Exact libwebp 1.0.3 wrapper is missing: $webp103Output"
}
foreach ($vorbisRuntimeOutput in $vorbisRuntimeOutputs) {
    if (-not (Test-Path -LiteralPath $vorbisRuntimeOutput -PathType Leaf)) {
        throw "Native Vorbis runtime dependency is missing: $vorbisRuntimeOutput"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $probeClasses "isomr\Probe.class") -PathType Leaf)) {
    throw "Compiled tracked JNI probe class is missing: $probeClasses"
}

. $candyProbeProvenanceScript
Write-ArmrCandyEntryProbeProvenance `
    -RepoRoot $repoRoot `
    -StampPath $candyProbeProvenanceOutput `
    -ArtifactPaths @($candyEntryProbeOutput) | Out-Null
$candyProbeProvenanceStatus = Test-ArmrCandyEntryProbeProvenance `
    -RepoRoot $repoRoot `
    -StampPath $candyProbeProvenanceOutput `
    -ArtifactPaths @($candyEntryProbeOutput)
if (-not $candyProbeProvenanceStatus.Current) {
    throw "Native Candy entry probe provenance was not written correctly: " +
        $candyProbeProvenanceStatus.Reason
}

[pscustomobject]@{
    Build = "ok"
    Aarch64Output = $output
    Elf64Output = $elfOutput
    BionicJumpsOutput = $jumpsOutput
    ProbeOutput = $probeOutput
    ProfileProbeOutput = $profileProbeOutput
    CandyEntrySmokeOutput = $candyEntrySmokeOutput
    CandyEntryProbeOutput = $candyEntryProbeOutput
    CandyEntryProbeProvenance = $candyProbeProvenanceOutput
    WebP103Output = $webp103Output
    ThreadOutput = $threadOutput
    GuestJniDesktopOutput = $desktopJniOutput
    NativeGlesOutput = $nativeGlesOutput
    VorbisRuntimeOutputs = $vorbisRuntimeOutputs -join ";"
    ProbeClassPath = $probeClasses
    Jvm = $jdk.Jvm
    DynarmicRevision = $requiredRevision
    DynarmicSource = $dynarmic
    WebP103Revision = $requiredWebP103Revision
    WebP103Source = $webp103
    BoostInclude = $boost
    AngleRoot = $angle
} | Format-List
