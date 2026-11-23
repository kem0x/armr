[CmdletBinding()]
param(
    [string]$AppRoot,
    [UInt64]$HeapWatchAllocationSize = 0,
    [UInt64]$HeapWatchRangeOffset = 0,
    [UInt64]$HeapWatchRangeSize = 0,
    [ValidateRange(16777216, 1073741824)]
    [UInt64]$HeapCapacity = 536870912,
    [ValidateRange(1, [UInt64]::MaxValue)]
    [UInt64]$InstructionBudget = 500000000,
    [UInt64]$PostAnalyticsInstructionBudget = 2000000000,
    [switch]$ImportJournal,
    [switch]$CallTelemetry,
    [switch]$CallEnvelopeTelemetry,
    [switch]$CallSvcCategoryTelemetry,
    [switch]$NanosleepCallsiteTelemetry,
    [switch]$QsortOperationTelemetry,
    [switch]$StepBoundaryTelemetry,
    [ValidateSet("HeapSort", "BionicPartition")]
    [string]$QsortAlgorithm = "BionicPartition",
    [switch]$WorkerCpuSampling,
    [switch]$WorkerExecutionSampling,
    [switch]$WorkerExecutionModuleRegions,
    [switch]$DisableCandyVorbisHostMirror,
    [switch]$DisableCandyWebP103Host,
    [string]$CandyWebP103Library,
    [switch]$DisableCandyBulkZeroStream,
    [switch]$CandyRapidJsonHostMirror,
    [switch]$DirectHeapPageTable,
    [ValidateRange(10, 360)]
    [double]$PresentPacingFps = 0,
    [ValidatePattern('^([2-9][0-9]{2}|1[0-9]{3}|2[01][0-9]{2})x([2-9][0-9]{2}|1[0-9]{3}|2[01][0-9]{2})$')]
    [string]$ScreenSize = "",
    [switch]$OpenSlTimedDiscard,
    [UInt64]$WorkerExecutionSliceTicks = 0,
    [ValidateSet("", "Read", "Write")]
    [string]$MemoryCaptureAccess = "",
    [UInt64]$MemoryCaptureGeneration = 0,
    [UInt64]$MemoryCaptureOccurrence = 1,
    [ValidateSet("Null", "Empty")]
    [string]$AnalyticsNoValue = "Null",
    [ValidateSet("SleepFor", "HighResolutionWaitable")]
    [string]$NanosleepBackend = "HighResolutionWaitable",
    [switch]$AcknowledgePopupWithoutUiForTesting,
    [switch]$CloseAfterPopupCompletionForTesting,
    [switch]$NanosleepConfigSelfTest
)

$ErrorActionPreference = "Stop"
if (@($CallTelemetry, $CallEnvelopeTelemetry,
        $CallSvcCategoryTelemetry).Where({ $_ }).Count -gt 1) {
    throw "CallTelemetry, CallEnvelopeTelemetry, and CallSvcCategoryTelemetry are incompatible."
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $repoRoot `
    "native-runtime\win\build\aarch64\ArmrCandyEntryProbe.exe"
if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
    throw "Native Candy entry probe is not built: $probe"
}
$nativeNanosleepBackend = if ($NanosleepBackend -eq "SleepFor") {
    "sleep-for"
} else {
    "high-resolution-waitable"
}
if ($DisableCandyWebP103Host -and
    -not [string]::IsNullOrWhiteSpace($CandyWebP103Library)) {
    throw "DisableCandyWebP103Host and CandyWebP103Library are incompatible."
}
if (-not $NanosleepConfigSelfTest -and
    [string]::IsNullOrWhiteSpace($AppRoot)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw "LOCALAPPDATA is unavailable. Pass -AppRoot explicitly."
    }
    $AppRoot = Join-Path $env:LOCALAPPDATA `
        "Armr\apps\com.king.candycrushsaga"
}
if ($NanosleepConfigSelfTest) {
    $rootLibrary = "config-self-test-root"
    $payloadRoot = "config-self-test-payload"
    $dataRoot = "config-self-test-data"
} else {
    $AppRoot = [IO.Path]::GetFullPath($AppRoot)
    $payloadRoot = Join-Path $AppRoot "payload"
    $dataRoot = Join-Path $AppRoot "data"
    $rootLibrary = Join-Path $payloadRoot `
        "lib\arm64-v8a\libcandycrushsaga.so"
    if (-not (Test-Path -LiteralPath $rootLibrary -PathType Leaf)) {
        throw "User-supplied Candy ARM64 library is not staged: $rootLibrary"
    }
    if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container)) {
        throw "Candy payload root is not staged: $payloadRoot"
    }
    New-Item -ItemType Directory -Path $dataRoot -Force | Out-Null
}

$provenanceScript = Join-Path $PSScriptRoot "CandyEntryProbeProvenance.ps1"
. $provenanceScript
$provenanceStamp = "$probe.provenance"
$provenanceStatus = Test-ArmrCandyEntryProbeProvenance `
    -RepoRoot $repoRoot `
    -StampPath $provenanceStamp `
    -ArtifactPaths @($probe)
if (-not $provenanceStatus.Current) {
    $rebuildCommand = "& `"$(Join-Path $PSScriptRoot 'Build-ArmrAarch64.ps1')`""
    throw "Native Candy entry probe provenance is not current: " +
        "$($provenanceStatus.Reason) Rebuild with: $rebuildCommand"
}
$provenanceLine = $provenanceStatus.Provenance.Text -replace "`r?`n", " "
Write-Output "ISOMR_CANDY_ENTRY_PROBE_PROVENANCE $provenanceLine"

$watchValues = @($HeapWatchAllocationSize, $HeapWatchRangeOffset,
    $HeapWatchRangeSize)
$watchEnabled = ($watchValues | Where-Object { $_ -ne 0 }).Count -ne 0
$captureEnabled = -not [string]::IsNullOrWhiteSpace($MemoryCaptureAccess)
if ($captureEnabled -ne ($MemoryCaptureGeneration -ne 0)) {
    throw "Memory capture requires both an access kind and a nonzero generation."
}
if ($captureEnabled -and -not $watchEnabled) {
    throw "Memory capture requires a heap watch configuration."
}
if ($MemoryCaptureOccurrence -eq 0) {
    throw "Memory capture occurrence must be nonzero."
}
if ($watchEnabled -and
    ($HeapWatchAllocationSize -eq 0 -or $HeapWatchRangeSize -eq 0 -or
     $HeapWatchRangeOffset -gt $HeapWatchAllocationSize -or
     $HeapWatchRangeSize -gt
        $HeapWatchAllocationSize - $HeapWatchRangeOffset)) {
    throw "Heap watch configuration is incomplete or out of range."
}
$probeArguments = @($rootLibrary, $payloadRoot, $dataRoot)
$probeArguments += [string]$HeapWatchAllocationSize
$probeArguments += [string]$HeapWatchRangeOffset
$probeArguments += [string]$HeapWatchRangeSize
$probeArguments += "heap-capacity=$HeapCapacity"
$probeArguments += "instruction-budget=$InstructionBudget"
if ($PostAnalyticsInstructionBudget -ne 0) {
    $probeArguments +=
        "post-analytics-instruction-budget=$PostAnalyticsInstructionBudget"
}
if ($ImportJournal) {
    $probeArguments += "import-journal"
}
if ($CallTelemetry) {
    $probeArguments += "call-telemetry"
}
if ($CallEnvelopeTelemetry) {
    $probeArguments += "call-envelope-telemetry"
}
if ($CallSvcCategoryTelemetry) {
    $probeArguments += "call-svc-category-telemetry"
}
if ($NanosleepCallsiteTelemetry) {
    $probeArguments += "nanosleep-callsite-telemetry"
}
if ($QsortOperationTelemetry) {
    $probeArguments += "qsort-operation-telemetry"
}
if ($StepBoundaryTelemetry) {
    $probeArguments += "step-boundary-telemetry"
}
$nativeQsortAlgorithm = if ($QsortAlgorithm -eq "BionicPartition") {
    "bionic-partition"
} else {
    "heap-sort"
}
$probeArguments += "qsort-algorithm=$nativeQsortAlgorithm"
if ($WorkerCpuSampling) {
    $probeArguments += "worker-cpu-sampling"
}
if ($WorkerExecutionSampling) {
    if (-not $WorkerCpuSampling) {
        throw "WorkerExecutionSampling requires WorkerCpuSampling."
    }
    $probeArguments += "worker-execution-sampling=ordinals:0,1"
}
if ($OpenSlTimedDiscard) {
    $probeArguments += "opensl-timed-discard"
}
if ($WorkerExecutionSliceTicks -ne 0) {
    if (-not $WorkerCpuSampling -or -not $WorkerExecutionSampling) {
        throw "WorkerExecutionSliceTicks requires WorkerCpuSampling and WorkerExecutionSampling."
    }
    $probeArguments += "worker-execution-slice-ticks=$WorkerExecutionSliceTicks"
}
if ($WorkerExecutionModuleRegions) {
    if (-not $WorkerCpuSampling -or -not $WorkerExecutionSampling -or
        $WorkerExecutionSliceTicks -eq 0) {
        throw "WorkerExecutionModuleRegions requires WorkerCpuSampling, WorkerExecutionSampling, and nonzero WorkerExecutionSliceTicks."
    }
    $probeArguments += "worker-execution-module-regions"
}
if (-not $DisableCandyVorbisHostMirror) {
    $probeArguments += "candy-vorbis-host-mirror"
}
if (-not $DisableCandyWebP103Host) {
    if ([string]::IsNullOrWhiteSpace($CandyWebP103Library)) {
        $CandyWebP103Library = Join-Path $repoRoot `
            "native-runtime\win\build\aarch64\ArmrWebP103.dll"
    }
    $CandyWebP103Library = [IO.Path]::GetFullPath($CandyWebP103Library)
    if (-not (Test-Path -LiteralPath $CandyWebP103Library -PathType Leaf)) {
        throw "Exact libwebp 1.0.3 wrapper is unavailable: $CandyWebP103Library"
    }
    $probeArguments += "candy-webp-1.0.3-host=$CandyWebP103Library"
}
if (-not $DisableCandyBulkZeroStream) {
    $probeArguments += "candy-bulk-zero-stream"
}
if ($CandyRapidJsonHostMirror) {
    $probeArguments += "candy-rapidjson-host-mirror"
}
if ($DirectHeapPageTable) {
    $probeArguments += "direct-heap-page-table"
}
if ($PresentPacingFps -ne 0) {
    $probeArguments += "present-pacing=$PresentPacingFps"
}
if ($ScreenSize -ne "") {
    $probeArguments += "screen-size=$ScreenSize"
}
if ($captureEnabled) {
    $probeArguments += "capture-$($MemoryCaptureAccess.ToLowerInvariant())=$($MemoryCaptureGeneration):$MemoryCaptureOccurrence"
}
$probeArguments += "analytics-no-value=$($AnalyticsNoValue.ToLowerInvariant())"
$probeArguments += "nanosleep-backend=$nativeNanosleepBackend"
if ($AcknowledgePopupWithoutUiForTesting) {
    $probeArguments += "acknowledge-popup-without-ui-for-testing"
}
if ($CloseAfterPopupCompletionForTesting) {
    if (-not $AcknowledgePopupWithoutUiForTesting) {
        throw "CloseAfterPopupCompletionForTesting requires AcknowledgePopupWithoutUiForTesting."
    }
    $probeArguments += "close-after-popup-completion-for-testing"
}
if ($NanosleepConfigSelfTest) {
    $probeArguments += "config-self-test"
}
& $probe @probeArguments
exit $LASTEXITCODE
