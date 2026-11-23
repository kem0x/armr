param(
    [string]$DynarmicSource = $env:ISOMR_DYNARMIC_SOURCE,
    [string]$BoostInclude = $env:ISOMR_BOOST_INCLUDE,
    [string]$AngleRoot = $env:ISOMR_ANGLE_ROOT,
    [string]$JavaHome = $env:JAVA_HOME,
    [string]$WebP103Source = $env:ISOMR_WEBP103_SOURCE,
    [string]$RapidJson110Source = $env:ISOMR_RAPIDJSON110_SOURCE
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$builder = Join-Path $PSScriptRoot "Build-ArmrAarch64.ps1"
$jdkResolver = Join-Path $PSScriptRoot "Resolve-NativeJdk.ps1"
$cmakeCache = Join-Path $repoRoot "native-runtime\win\build\aarch64\CMakeCache.txt"
$source = Join-Path $repoRoot "native-runtime\win\aarch64\aarch64_smoke.cpp"
$guestSessionSource = Join-Path $repoRoot "native-runtime\win\aarch64\aarch64_guest_session.cpp"
$profileRuntimeSource = Join-Path $repoRoot "native-runtime\win\aarch64\aarch64_profile_runtime.cpp"
$output = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrAarch64Smoke.exe"
$elfSource = Join-Path $repoRoot "native-runtime\win\aarch64\elf64_loader.cpp"
$moduleSource = Join-Path $repoRoot "native-runtime\win\aarch64\elf64_modules.cpp"
$importSource = Join-Path $repoRoot "native-runtime\win\aarch64\import_registry.cpp"
$heapSource = Join-Path $repoRoot "native-runtime\win\aarch64\guest_heap.cpp"
$functionTableSource = Join-Path $repoRoot "native-runtime\win\aarch64\guest_function_table.cpp"
$jniSource = Join-Path $repoRoot "native-runtime\win\aarch64\guest_jni.cpp"
$bionicMemorySource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_memory.cpp"
$atexitSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_atexit.cpp"
$addressSpaceSource = Join-Path $repoRoot "native-runtime\win\aarch64\guest_address_space.cpp"
$memoryOperationSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_memory_ops.cpp"
$processSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_process.cpp"
$propertySource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_properties.cpp"
$randomSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_random.cpp"
$stdioSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_stdio.cpp"
$timeSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_time.cpp"
$threadStateSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_thread_state.cpp"
$fileSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_files.cpp"
$formatSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_format.cpp"
$localeSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_locales.cpp"
$mathSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_math.cpp"
$numberSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_numbers.cpp"
$syncSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_sync.cpp"
$tlsSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_tls.cpp"
$threadLifecycleSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_threads.cpp"
$threadLifecycleHeader = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_threads.h"
$workerExecutionHeader = Join-Path $repoRoot "native-runtime\win\aarch64\worker_execution_telemetry.h"
$bionicRuntimeSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_runtime.cpp"
$bionicJumpsSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_jumps.cpp"
$bionicJumpsHeader = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_jumps.h"
$bionicJumpsFixture = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_jumps_fixture.cpp"
$bionicJumpsSmoke = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_jumps_smoke.cpp"
$nativeGlesSource = Join-Path $repoRoot "native-runtime\win\aarch64\native_gles.cpp"
$nativeGlesHeader = Join-Path $repoRoot "native-runtime\win\aarch64\native_gles.h"
$nativeGlesSmoke = Join-Path $repoRoot "native-runtime\win\aarch64\native_gles_smoke.cpp"
$nativeOpenSlSource = Join-Path $repoRoot "native-runtime\win\aarch64\native_opensles.cpp"
$nativeOpenSlHeader = Join-Path $repoRoot "native-runtime\win\aarch64\native_opensles.h"
$elfSmoke = Join-Path $repoRoot "native-runtime\win\aarch64\elf64_smoke.cpp"
$elfProbe = Join-Path $repoRoot "native-runtime\win\aarch64\elf64_probe.cpp"
$profileProbe = Join-Path $repoRoot "native-runtime\win\aarch64\profile_probe.cpp"
$candyBridgeSource = Join-Path $repoRoot "native-runtime\win\aarch64\candy_entry_bridge.cpp"
$candyBridgeHeader = Join-Path $repoRoot "native-runtime\win\aarch64\candy_entry_bridge.h"
$candyFixtureSource = Join-Path $repoRoot "native-runtime\win\aarch64\candy_entry_fixture.cpp"
$candySmokeSource = Join-Path $repoRoot "native-runtime\win\aarch64\candy_entry_smoke.cpp"
$candyProbeSource = Join-Path $repoRoot "native-runtime\win\aarch64\candy_entry_probe.cpp"
$candyVorbisSource = Join-Path $repoRoot "native-runtime\win\aarch64\candy_vorbis_host.cpp"
$candyVorbisHeader = Join-Path $repoRoot "native-runtime\win\aarch64\candy_vorbis_host.h"
$candyWebPSource = Join-Path $repoRoot "native-runtime\win\aarch64\candy_webp_host.cpp"
$candyWebPHeader = Join-Path $repoRoot "native-runtime\win\aarch64\candy_webp_host.h"
$candyWebPShim = Join-Path $repoRoot "native-runtime\win\aarch64\candy_webp_103_shim.cpp"
$candyRapidJsonSource = Join-Path $repoRoot "native-runtime\win\aarch64\candy_rapidjson_host.cpp"
$candyRapidJsonHeader = Join-Path $repoRoot "native-runtime\win\aarch64\candy_rapidjson_host.h"
$waitableTimerSource = Join-Path $repoRoot "native-runtime\win\aarch64\bionic_waitable_timer_smoke.cpp"
$ctrProbeSource = Join-Path $repoRoot "native-runtime\win\aarch64\ctr_entry_probe.cpp"
$candyProbeScript = Join-Path $PSScriptRoot "Invoke-CandyNativeEntryProbe.ps1"
$candyProbeProvenanceScript = Join-Path $PSScriptRoot `
    "CandyEntryProbeProvenance.ps1"
$candyEntryOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrCandyEntrySmoke.exe"
$candyProbeOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrCandyEntryProbe.exe"
$waitableTimerOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrBionicWaitableTimerSmoke.exe"
$elfOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrElf64Smoke.exe"
$bionicJumpsOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrBionicJumpsSmoke.exe"
$threadSource = Join-Path $repoRoot "native-runtime\win\aarch64\thread_smoke.cpp"
$threadOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrThreadSmoke.exe"
$desktopJniSource = Join-Path $repoRoot "native-runtime\win\aarch64\desktop_jvm.cpp"
$desktopJniHeader = Join-Path $repoRoot "native-runtime\win\aarch64\desktop_jvm.h"
$guestJniBackendHeader = Join-Path $repoRoot "native-runtime\win\aarch64\guest_jni_backend.h"
$desktopJniSmoke = Join-Path $repoRoot "native-runtime\win\aarch64\guest_jni_desktop_smoke.cpp"
$probeSource = Join-Path $repoRoot "java-stubs\isomr\Probe.java"
$desktopJniOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrGuestJniDesktopSmoke.exe"
$nativeGlesOutput = Join-Path $repoRoot "native-runtime\win\build\aarch64\ArmrNativeGlesSmoke.exe"
$probeClasses = Join-Path $repoRoot "native-runtime\win\build\aarch64\java-classes"
. (Join-Path $PSScriptRoot "Resolve-NativeJdk.ps1")
$jdk = Resolve-ArmrNativeJdk -Requested $JavaHome

foreach ($script in @($builder, $jdkResolver, $candyProbeScript,
        $candyProbeProvenanceScript)) {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $script, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count -gt 0) {
        throw ($errors | ForEach-Object Message -join "; ")
    }
}
if ((Select-String -LiteralPath @($candyBridgeSource, $candyBridgeHeader, $candyFixtureSource, $candySmokeSource, $candyProbeSource, $candyVorbisSource, $candyVorbisHeader, $candyWebPSource, $candyWebPHeader, $candyWebPShim, $candyRapidJsonSource, $candyRapidJsonHeader, $waitableTimerSource, $nativeOpenSlSource, $nativeOpenSlHeader, $candyProbeScript, $candyProbeProvenanceScript) `
        -Pattern 'wsl\.exe|mingw|x86_64-w64|\.runtime[\\/]|native-runtime[\\/](hybrid|jit)').Count -gt 0) {
    throw "Native Candy entry slice contains a forbidden legacy dependency."
}
$candyProbeText = Get-Content -LiteralPath $candyProbeScript -Raw
foreach ($requiredProbeContract in @(
    "Armr\apps\com.king.candycrushsaga",
    "lib\arm64-v8a\libcandycrushsaga.so",
    "ArmrCandyEntryProbe.exe",
    "Test-ArmrCandyEntryProbeProvenance",
    "Native Candy entry probe provenance is not current",
    "ISOMR_CANDY_ENTRY_PROBE_PROVENANCE",
    '-ArtifactPaths @($probe)',
    "Build-ArmrAarch64.ps1",
    '[switch]$OpenSlTimedDiscard',
    '$probeArguments += "opensl-timed-discard"',
    '[switch]$DisableCandyVorbisHostMirror',
    'if (-not $DisableCandyVorbisHostMirror)',
    '$probeArguments += "candy-vorbis-host-mirror"',
    '[string]$CandyWebP103Library',
    '[switch]$DisableCandyWebP103Host',
    '$probeArguments += "candy-webp-1.0.3-host=$CandyWebP103Library"',
    '[switch]$DisableCandyBulkZeroStream',
    'if (-not $DisableCandyBulkZeroStream)',
    '$probeArguments += "candy-bulk-zero-stream"',
    '[switch]$CandyRapidJsonHostMirror',
    '$probeArguments += "candy-rapidjson-host-mirror"',
    '[switch]$QsortOperationTelemetry',
    '$probeArguments += "qsort-operation-telemetry"',
    '[switch]$StepBoundaryTelemetry',
    '$probeArguments += "step-boundary-telemetry"',
    '[ValidateSet("HeapSort", "BionicPartition")]',
    '[string]$QsortAlgorithm = "BionicPartition"',
    '"qsort-algorithm=$nativeQsortAlgorithm"',
    '[ValidateSet("SleepFor", "HighResolutionWaitable")]',
    '[string]$NanosleepBackend = "HighResolutionWaitable"',
    '"nanosleep-backend=$nativeNanosleepBackend"')) {
    if (-not $candyProbeText.Contains($requiredProbeContract)) {
        throw "Native Candy entry probe is missing staging contract: $requiredProbeContract"
    }
}
$candyProbeSourceText = Get-Content -LiteralPath $candyProbeSource -Raw
$candyConsumerText = $candyProbeSourceText + "`n" +
    (Get-Content -LiteralPath $candyBridgeSource -Raw)
foreach ($requiredNanosleepConsumer in @(
    'L"nanosleep-backend="',
    'L"sleep-for"',
    'L"high-resolution-waitable"',
    'config.nanosleep_backend',
    'option == L"qsort-operation-telemetry"',
    'ConfigureQsortOperationTelemetry(',
    'option == L"step-boundary-telemetry"',
    'TakeQsortOperationSnapshot(',
    'options.step_boundary_telemetry',
    'L"qsort-algorithm="',
    'ConfigureQsortAlgorithm(',
    'option == L"opensl-timed-discard"',
    'config.opensl_mode',
    'option == L"candy-vorbis-host-mirror"',
    'ConfigureCandyVorbisHostMirror(',
    'L"candy-webp-1.0.3-host="',
    'ConfigureCandyWebP103Host(',
    'option == L"candy-bulk-zero-stream"',
    'options.bulk_zero_stream')) {
    if (-not $candyConsumerText.Contains($requiredNanosleepConsumer)) {
        throw "Native Candy entry path is missing configured consumer: $requiredNanosleepConsumer"
    }
}
$candyWebPText = Get-Content -LiteralPath $candyWebPSource -Raw
$candyWebPHeaderText = Get-Content -LiteralPath $candyWebPHeader -Raw
$candyWebPShimText = Get-Content -LiteralPath $candyWebPShim -Raw
foreach ($requiredWebPContract in @(
    'kExpectedDecoderVersion = 0x010003',
    'kMaximumTransfer = 64U * 1024U * 1024U',
    'kModuleOffset = 0x4e232dcULL',
    '0xd10503ffU, 0xa90f7bfdU',
    'RangesOverlap(',
    'IsWritableRange(',
    'ConfigureCandyWebP103HostForTesting(',
    'ArmrWebP103GetDecoderVersion',
    'ArmrWebP103DecodeRGBAInto',
    'WebPDecodeRGBAInto(')) {
    if (-not ($candyWebPText + $candyWebPHeaderText +
              $candyWebPShimText).Contains($requiredWebPContract)) {
        throw "Candy exact WebP 1.0.3 host is missing contract: $requiredWebPContract"
    }
}
$bionicTimeText = Get-Content -LiteralPath $timeSource -Raw
foreach ($requiredNanosleepBackendContract in @(
    'CreateWaitableTimerExW',
    'CREATE_WAITABLE_TIMER_HIGH_RESOLUTION',
    'SetWaitableTimerEx',
    'WaitForSingleObject',
    'SleepForNanoseconds',
    '"usleep"',
    'requested=sleep-for effective=sleep-for',
    'requested=high-resolution-waitable effective=high-resolution-waitable',
    'requested=high-resolution-waitable effective=unsupported')) {
    if (-not $bionicTimeText.Contains($requiredNanosleepBackendContract)) {
        throw "Native Bionic time is missing nanosleep backend contract: $requiredNanosleepBackendContract"
    }
}
$bionicThreadsText = Get-Content -LiteralPath $threadLifecycleSource -Raw
$bionicThreadsHeaderText = Get-Content -LiteralPath $threadLifecycleHeader -Raw
$workerExecutionText = Get-Content -LiteralPath $workerExecutionHeader -Raw
$guestSessionText = Get-Content -LiteralPath $guestSessionSource -Raw
foreach ($requiredExecutionContract in @(
    'kMaxWorkerPcGroups = 64',
    'selector must be ordinals:0,1',
    'worker execution sampling requires worker CPU sampling',
    'execution_slice_ticks',
    'worker_execution_slice_ticks',
    'ResolveWorkerExecutionGuestPcLocalOnly',
    'WorkerHostResidency',
    'SuspendThread',
    'GetThreadContext',
    'ResumeThread',
    'VirtualQuery',
    'AddOwnerThreadExecutionBreakpoint',
    'RemoveOwnerThreadExecutionBreakpoint',
    'pending_execution_breakpoint_address_',
    'std::memory_order_seq_cst',
    'kMaximumRetries = 64',
    'WorkerExecutionPhase::kDestroyed')) {
    if (-not ($bionicThreadsText + $bionicThreadsHeaderText +
              $workerExecutionText +
              $guestSessionText).Contains($requiredExecutionContract)) {
        throw "Worker execution sampling lacks contract: $requiredExecutionContract"
    }
}
if ($bionicThreadsHeaderText.Contains('raw_pc') -or
    $candyProbeSourceText -match 'SuspendThread|GetThreadContext|StackWalk|GetPC\(' -or
    $workerExecutionText -match 'mutex|hash|encrypt|module|offset|range') {
    throw "Worker execution public/source contract is not address-free or owner-only."
}
if ([regex]::Matches($guestSessionText, 'void AddTicks\(').Count -ne 1 -or
    [regex]::Matches($guestSessionText, 'void CallSVC\(').Count -ne 1 -or
    [regex]::Matches(
        $guestSessionText,
        'WorkerExecutionPhase::k(?:Destroyed|CallFailure|CallEntry|BeforeRun|AfterRun|InterSlice|CallSuccess)').Count -ne 7 -or
    -not (Get-Content -LiteralPath $profileRuntimeSource -Raw).Contains(
        'created->SetOwnerThreadInterSliceCallback(')) {
    throw "Worker execution slicing added a hot-path hook or lacks owner-session configuration."
}
foreach ($requiredWorkerCpuAtomicContract in @(
    'std::atomic<bool> worker_cpu_sampling_enabled{false};',
    'worker_cpu_sampling_enabled.load(std::memory_order_acquire)',
    'worker_cpu_sampling_enabled.store(',
    'std::memory_order_release);')) {
    if (-not $bionicThreadsText.Contains($requiredWorkerCpuAtomicContract)) {
        throw "Native Bionic worker CPU sampling is missing atomic contract: $requiredWorkerCpuAtomicContract"
    }
}
$disabledCreateStart = $bionicThreadsText.IndexOf(
    'if (!shared_->worker_cpu_sampling_enabled.load(')
$enabledCreateStart = $bionicThreadsText.IndexOf(
    '// Enabled creation is one bounded publication transaction.')
if ($disabledCreateStart -lt 0 -or
    $enabledCreateStart -le $disabledCreateStart) {
    throw "Native Bionic worker CPU sampling lacks a distinct disabled Create branch."
}
$disabledCreateBody = $bionicThreadsText.Substring(
    $disabledCreateStart, $enabledCreateStart - $disabledCreateStart)
foreach ($requiredDisabledCreateStatement in @(
    'const std::lock_guard lock(shared_->mutex);',
    'record = std::make_shared<ThreadRecord>(',
    'shared_->threads.emplace(handle, record);',
    'shared_->address_space->Write(output_address, &handle,',
    'std::thread(&BionicThreads::WorkerMain, shared_, record);',
    '(void)shared_->address_space->Write(output_address, &zero,',
    '(void)shared_->heap->Release(handle, &ignored);')) {
    if (-not $disabledCreateBody.Contains($requiredDisabledCreateStatement)) {
        throw "Native Bionic sampling-disabled Create path changed: $requiredDisabledCreateStatement"
    }
}
if ($disabledCreateBody.Contains('worker_start_groups') -or
    $disabledCreateBody.Contains('start_group')) {
    throw "Native Bionic sampling-disabled Create path performs start-group work."
}
$watchArgumentsEnd = $candyProbeText.IndexOf(
    '$probeArguments += [string]$HeapWatchRangeSize')
$heapCapacityForward = $candyProbeText.IndexOf(
    '$probeArguments += "heap-capacity=$HeapCapacity"')
$instructionBudgetForward = $candyProbeText.IndexOf(
    '$probeArguments += "instruction-budget=$InstructionBudget"')
$postAnalyticsBudgetForward = $candyProbeText.IndexOf(
    '"post-analytics-instruction-budget=$PostAnalyticsInstructionBudget"')
$importJournalOption = $candyProbeText.IndexOf('if ($ImportJournal)')
if (-not $candyProbeText.Contains(
        '[ValidateRange(1, [UInt64]::MaxValue)]') -or
    -not $candyProbeText.Contains(
        '[UInt64]$InstructionBudget = 500000000') -or
    -not $candyProbeText.Contains(
        '[UInt64]$PostAnalyticsInstructionBudget = 2000000000') -or
    -not $candyProbeText.Contains(
        '[ValidateRange(16777216, 1073741824)]') -or
    -not $candyProbeText.Contains(
        '[UInt64]$HeapCapacity = 536870912') -or
    $watchArgumentsEnd -lt 0 -or
    $heapCapacityForward -le $watchArgumentsEnd -or
    $instructionBudgetForward -le $heapCapacityForward -or
    $postAnalyticsBudgetForward -le $instructionBudgetForward -or
    $importJournalOption -le $postAnalyticsBudgetForward) {
    throw "Native Candy entry probe launcher lacks ordered heap-capacity/instruction-budget plumbing."
}
$builderText = Get-Content -LiteralPath $builder -Raw
$candyProbeProvenanceText = `
    Get-Content -LiteralPath $candyProbeProvenanceScript -Raw
$candyBridgeText = Get-Content -LiteralPath $candyBridgeSource -Raw
$candyEntryProbeText = Get-Content -LiteralPath $candyProbeSource -Raw
$candyRapidJsonText = Get-Content -LiteralPath $candyRapidJsonSource -Raw
$ctrProbeText = Get-Content -LiteralPath $ctrProbeSource -Raw
if (-not $builderText.Contains(
        "-DISOMR_BUILD_EXPERIMENTAL_CTR=OFF")) {
    throw "Native build script lacks the -CoreOnly game-probe-off configuration."
}
foreach ($requiredProvenanceContract in @(
    "Get-ArmrCandyEntryProbeInputFingerprint",
    "Get-ArmrCandyEntryProbeProvenance",
    "Write-ArmrCandyEntryProbeProvenance",
    "Test-ArmrCandyEntryProbeProvenance",
    "fingerprint=",
    "artifact=")) {
    if (-not $candyProbeProvenanceText.Contains(
            $requiredProvenanceContract)) {
        throw "Candy entry probe provenance helper is missing contract: " +
            $requiredProvenanceContract
    }
}
if (-not $builderText.Contains("Write-ArmrCandyEntryProbeProvenance") -or
    -not $builderText.Contains("ArmrCandyEntryProbe.exe.provenance") -or
    -not $builderText.Contains(
        '-ArtifactPaths @($candyEntryProbeOutput)')) {
    throw "Native Candy entry probe build lacks the provenance stamp contract."
}
& powershell -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $PSScriptRoot "Test-CandyEntryProbeProvenance.ps1")
if ($LASTEXITCODE -ne 0) {
    throw "Candy entry probe provenance test failed: $LASTEXITCODE"
}
if ($candyBridgeText.Contains("MessageBoxW(") -or
    -not $candyBridgeText.Contains(
        'GetProcAddress(common_controls, "TaskDialogIndirect")') -or
    -not $candyBridgeText.Contains(
        'if (!request.noncancelable)') -or
    -not $candyBridgeText.Contains(
        'dialog.nDefaultButton = kRequestedButtonId') -or
    -not $candyBridgeText.Contains(
        'popup dialog returned an invalid dismissal result')) {
    throw "Production popup behavior lacks the owned TaskDialog contract."
}
$referencesMarker = $candyEntryProbeText.IndexOf(
    'ISOMR_CANDY_ENTRY_PROBE stage=references result=pass')
$telemetryEnable = $candyEntryProbeText.IndexOf(
    'runtime.SetMainCallTelemetryMode(call_telemetry_mode, &error)')
$telemetryMarker = $candyEntryProbeText.IndexOf(
    'ISOMR_AARCH64_CALL_TELEMETRY config=enabled')
$bridgeCall = $candyEntryProbeText.IndexOf(
    'isomr::CandyEntryBridge::Run(')
if (-not $candyEntryProbeText.Contains(
        'ISOMR_GUEST_HEAP_ACCESS config=shared-lock-generation-span-cache')) {
    throw "Candy entry probe is missing the consumed GuestHeap access configuration marker."
}
if (-not $candyEntryProbeText.Contains(
        'ISOMR_GUEST_HEAP_METADATA config=offset-map-augmented-free-avl-first-fit-v2')) {
    throw "Candy entry probe is missing the GuestHeap metadata configuration marker."
}
foreach ($requiredRapidJsonContract in @(
    'BuildCandyRapidJsonHybridDocument',
    'kGuestValueSize = 24',
    'kGuestMemberSize = 48',
    'kGuestAllocatorSize = 40',
    'kShortStringMaximum = 21',
    'Load16(document_bytes.data() + 22U)',
    'Candy RapidJSON legacy allocator layout mismatch',
    'ISOMR_CANDY_RAPIDJSON_HOST_MIRROR')) {
    if (-not ($candyRapidJsonText + $candyBridgeText +
              $candyEntryProbeText).Contains($requiredRapidJsonContract)) {
        throw "Candy RapidJSON host mirror lacks contract: $requiredRapidJsonContract"
    }
}
if (-not $candyEntryProbeText.Contains('option == L"call-telemetry"') -or
    -not $candyEntryProbeText.Contains(
        'option == L"call-envelope-telemetry"') -or
    -not $candyEntryProbeText.Contains(
        'option == L"call-svc-category-telemetry"') -or
    -not $candyEntryProbeText.Contains(
        'option == L"nanosleep-callsite-telemetry"') -or
    -not $candyEntryProbeText.Contains(
        'option == L"worker-cpu-sampling"') -or
    -not $candyEntryProbeText.Contains(
        'ISOMR_AARCH64_CALL_ENVELOPE config=enabled scope=main-bridge svc-timing=off memory-counters=off') -or
    -not $candyEntryProbeText.Contains(
        'ISOMR_AARCH64_CALL_SVC_CATEGORY config=enabled scope=main-bridge address-free=1 per-svc-lines=off memory-counters=off import-journal=unchanged') -or
    -not $candyProbeText.Contains('[switch]$CallEnvelopeTelemetry') -or
    -not $candyProbeText.Contains('[switch]$CallSvcCategoryTelemetry') -or
    -not $candyProbeText.Contains('[switch]$NanosleepCallsiteTelemetry') -or
    -not $candyProbeText.Contains('[switch]$WorkerCpuSampling') -or
    -not $candyProbeText.Contains('[switch]$WorkerExecutionSampling') -or
    -not $candyProbeText.Contains('[switch]$WorkerExecutionModuleRegions') -or
    -not $candyProbeText.Contains('[UInt64]$WorkerExecutionSliceTicks = 0') -or
    -not $candyProbeText.Contains(
        '$probeArguments += "call-envelope-telemetry"') -or
    -not $candyProbeText.Contains(
        '$probeArguments += "call-svc-category-telemetry"') -or
    -not $candyProbeText.Contains(
        '$probeArguments += "nanosleep-callsite-telemetry"') -or
    -not $candyProbeText.Contains(
        '$probeArguments += "worker-cpu-sampling"') -or
    -not $candyProbeText.Contains(
        '$probeArguments += "worker-execution-sampling=ordinals:0,1"') -or
    -not $candyProbeText.Contains(
        '$probeArguments += "worker-execution-slice-ticks=$WorkerExecutionSliceTicks"') -or
    -not $candyProbeText.Contains(
        '$probeArguments += "worker-execution-module-regions"') -or
    $referencesMarker -lt 0 -or $telemetryEnable -le $referencesMarker -or
    $telemetryMarker -le $telemetryEnable -or $bridgeCall -le $telemetryMarker) {
    throw "Candy telemetry is not opt-in immediately before bridge calls."
}
if (-not $ctrProbeText.Contains(
        'config.file_mounts = {{"/app", argv[2]}, {"/data", argv[3], true}};') -or
    -not $ctrProbeText.Contains(
        'config.guest_current_directory = "/data";')) {
    throw "Retained experimental CTR lacks writable /data or cwd /data."
}
if ((Select-String -LiteralPath @($builder, $jdkResolver, $source, $guestSessionSource, $profileRuntimeSource, $elfSource, $moduleSource, $importSource, $heapSource, $functionTableSource, $jniSource, $bionicMemorySource, $atexitSource, $addressSpaceSource, $memoryOperationSource, $processSource, $propertySource, $randomSource, $stdioSource, $timeSource, $threadStateSource, $fileSource, $formatSource, $localeSource, $mathSource, $numberSource, $syncSource, $tlsSource, $threadLifecycleSource, $bionicRuntimeSource, $bionicJumpsSource, $bionicJumpsHeader, $bionicJumpsFixture, $bionicJumpsSmoke, $waitableTimerSource, $nativeGlesSource, $nativeGlesHeader, $nativeGlesSmoke, $nativeOpenSlSource, $nativeOpenSlHeader, $elfSmoke, $elfProbe, $profileProbe, $threadSource, $desktopJniSource, $desktopJniHeader, $guestJniBackendHeader, $desktopJniSmoke, $probeSource) `
        -Pattern 'wsl\.exe|mingw|x86_64-w64|\.runtime[\\/]|candycrush|cut.the.rope').Count -gt 0) {
    throw "Native AArch64 gate contains a forbidden legacy or payload dependency."
}

& $builder -CoreOnly -DynarmicSource $DynarmicSource -BoostInclude $BoostInclude `
    -AngleRoot $AngleRoot -JavaHome $jdk.Home `
    -WebP103Source $WebP103Source `
    -RapidJson110Source $RapidJson110Source
if ($LASTEXITCODE -ne 0) {
    throw "Native AArch64 build failed: $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf) -or
    -not (Select-String -LiteralPath $cmakeCache -SimpleMatch `
        "ISOMR_BUILD_EXPERIMENTAL_CTR:BOOL=OFF" -Quiet)) {
    throw "Core-only gate build did not turn the game probe off."
}
$candyEvidenceResult =
    & $candyProbeOutput --self-test-evidence 2>&1
$candyEvidenceExitCode = $LASTEXITCODE
$candyEvidenceResult | Write-Output
if ($candyEvidenceExitCode -ne 0 -or
    @($candyEvidenceResult | Where-Object {
        $_ -eq "ISOMR_GUEST_HEAP_ACCESS config=shared-lock-generation-span-cache"
    }).Count -ne 1 -or
    @($candyEvidenceResult | Where-Object {
        $_ -eq "ISOMR_GUEST_HEAP_METADATA config=offset-map-augmented-free-avl-first-fit-v2"
    }).Count -ne 1 -or
    $candyEvidenceResult -notcontains
        "ISOMR_CANDY_ENTRY_EVIDENCE_SELF_TEST=pass") {
    throw "Candy entry evidence self-test failed (exit $candyEvidenceExitCode)."
}
$launcherDefaultResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest 2>&1 |
        ForEach-Object { "$_" })
$launcherDefaultExitCode = $LASTEXITCODE
$launcherDefaultResult | Write-Output
$launcherDefaultPostAnalyticsBudget = @(
    $launcherDefaultResult | Where-Object {
        $_ -eq
            "ISOMR_CANDY_INSTRUCTION_BUDGET config=post-analytics-2000000000"
    })
$launcherDefaultBackendEvidence = @($launcherDefaultResult | Where-Object {
    $_ -like "ISOMR_NANOSLEEP_BACKEND requested=*"
})
$launcherDefaultSelfTestEvidence = @($launcherDefaultResult | Where-Object {
    $_ -like "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=*"
})
$launcherDefaultWorkerCpuEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_WORKER_CPU_SAMPLING config=*" })
$launcherDefaultWorkerCpuDefault =
    $launcherDefaultWorkerCpuEvidence.Count -eq 1 -and
    $launcherDefaultWorkerCpuEvidence[0] -eq
        "ISOMR_WORKER_CPU_SAMPLING config=disabled cadence=watchdog-5s limit=32 start-group=first-ordinal-equality address-free=1"
$launcherDefaultWorkerExecutionEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_WORKER_EXECUTION_SAMPLING config=*" })
$launcherDefaultWorkerExecutionDefault =
    $launcherDefaultWorkerExecutionEvidence.Count -eq 1 -and
    $launcherDefaultWorkerExecutionEvidence[0] -eq
        "ISOMR_WORKER_EXECUTION_SAMPLING config=disabled cadence=watchdog-5s selector=ordinals:0,1 boundaries=existing-only pc-group=exact-process-local host-residency=one-win32-control-sample address-free=1"
$launcherDefaultOpenSlEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_NATIVE_OPENSL config=*" })
$launcherDefaultOpenSlDefault =
    $launcherDefaultOpenSlEvidence.Count -eq 1 -and
    $launcherDefaultOpenSlEvidence[0] -eq
        "ISOMR_NATIVE_OPENSL config=disabled scope=timed-discard audio-player=wall-clock-paced sink=none callback=managed-thread address-free=1"
$launcherDefaultVorbisEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_VORBIS_HOST config=*" })
$launcherDefaultVorbisEnabled =
    $launcherDefaultVorbisEvidence.Count -eq 1 -and
    $launcherDefaultVorbisEvidence[0] -eq
        "ISOMR_CANDY_VORBIS_HOST config=enabled default=off profile=candy-crush-1.333.2.1 substitutions=10 source=pinned-memory-or-aasset-provider-bounded-copy decoder=host-libvorbisfile audio=real-pcm addresses=none"
$launcherDefaultWebPEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_WEBP_103_HOST config=*" })
$launcherDefaultWebPEnabled =
    $launcherDefaultWebPEvidence.Count -eq 1 -and
    $launcherDefaultWebPEvidence[0] -eq
        "ISOMR_CANDY_WEBP_103_HOST config=enabled default=off profile=candy-crush-1.333.2.1 substitutions=1 decoder=libwebp-1.0.3 exact-version=0x010003 addresses=none"
$launcherDefaultBulkZeroEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_BULK_ZERO_STREAM config=*" })
$launcherDefaultBulkZeroEnabled =
    $launcherDefaultBulkZeroEvidence.Count -eq 1 -and
    $launcherDefaultBulkZeroEvidence[0] -eq
        "ISOMR_CANDY_BULK_ZERO_STREAM config=enabled default=off profile=candy-crush-1.333.2.1 signatures=exact-loop-and-ostream-write mode=owner-thread-paused"
$launcherDefaultRapidJsonEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR config=*" })
$launcherDefaultRapidJsonDisabled =
    $launcherDefaultRapidJsonEvidence.Count -eq 1 -and
    $launcherDefaultRapidJsonEvidence[0] -eq
        "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR config=disabled default=off profile=candy-crush-1.333.2.1 parser-value-version=1.1.0 parser-value-revision=f54b0e47a08782a6131cc3d60f94d038fa6e0a51 allocator-layout=legacy-five-field scope=caller-0x4fe403c-minimum-4096 mode=owner-thread-paused addresses=none"
$launcherDefaultQsortTelemetryEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_BIONIC_QSORT_TELEMETRY config=*" })
$launcherDefaultQsortTelemetryDefault =
    $launcherDefaultQsortTelemetryEvidence.Count -eq 1 -and
    $launcherDefaultQsortTelemetryEvidence[0] -eq
        "ISOMR_BIONIC_QSORT_TELEMETRY config=disabled scope=operation-boundary fields=count,element-size,comparisons,swaps,wall-ns,executor-setup-wall-ns,comparator-wall-ns,swap-wall-ns,residual-wall-ns addresses=none"
$launcherDefaultStepTelemetryEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_STEP_TELEMETRY config=*" })
$launcherDefaultStepTelemetryDefault =
    $launcherDefaultStepTelemetryEvidence.Count -eq 1 -and
    $launcherDefaultStepTelemetryEvidence[0] -eq
        "ISOMR_CANDY_STEP_TELEMETRY config=disabled scope=guest-step-call,frame-cadence,queued-pointer-latency step-fields=wall-ns,thread-cpu-available,thread-cpu-100ns,qsort-started,qsort-completed,qsort-failed,qsort-comparisons,qsort-swaps,qsort-wall-ns,qsort-executor-setup-wall-ns,qsort-comparator-wall-ns,qsort-swap-wall-ns,qsort-residual-wall-ns,qsort-open-end frame-fields=iteration-complete,frame-wall-ns,guest-draws,host-presents,lit-presents,pointer-events pointer-fields=sequence,action,queue-wait-ns,guest-call-ns,total-ns addresses=none"
$launcherDefaultLongCallPcEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_LONG_CALL_PC config=*" })
$launcherDefaultLongCallPcDefault =
    $launcherDefaultLongCallPcEvidence.Count -eq 1 -and
    $launcherDefaultLongCallPcEvidence[0] -eq
        "ISOMR_CANDY_LONG_CALL_PC config=disabled trigger=step-wall-at-least-250000000ns boundary=deterministic-xorshift-jitter-184464-through-315535-mean-249999.5 cadence=first-then-deterministic-xorshift-1-in-8 fields=step,wall-threshold-ns,total-samples,retained,pc,lr,parent-lr,x0-min,x0-max,x1-min,x1-max,count,error,region-size,region,count,region-error addresses=module-relative-and-register-values-local-only"
$launcherDefaultQsortAlgorithmEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_BIONIC_QSORT_ALGORITHM config=*" })
$launcherDefaultQsortAlgorithmDefault =
    $launcherDefaultQsortAlgorithmEvidence.Count -eq 1 -and
    $launcherDefaultQsortAlgorithmEvidence[0] -eq
        "ISOMR_BIONIC_QSORT_ALGORITHM config=bionic-partition pivot=median-three-nine small=insertion equal=three-way"
$launcherDefaultPopupEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_POPUP_DIALOG config=*" })
$launcherDefaultPopupMode =
    $launcherDefaultPopupEvidence.Count -eq 1 -and
    $launcherDefaultPopupEvidence[0] -eq
        "ISOMR_CANDY_POPUP_DIALOG config=system-task-dialog"
$launcherDefaultPopupCloseEvidence = @($launcherDefaultResult |
    Where-Object { $_ -like "ISOMR_CANDY_POPUP_CLOSE config=*" })
$launcherDefaultPopupCloseMode =
    $launcherDefaultPopupCloseEvidence.Count -eq 1 -and
    $launcherDefaultPopupCloseEvidence[0] -eq
        "ISOMR_CANDY_POPUP_CLOSE config=disabled"
$launcherDefaultSupported =
    $launcherDefaultExitCode -eq 0 -and
    $launcherDefaultBackendEvidence.Count -eq 1 -and
    $launcherDefaultBackendEvidence[0] -eq
        "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=high-resolution-waitable" -and
    $launcherDefaultSelfTestEvidence.Count -eq 1 -and
    $launcherDefaultSelfTestEvidence[0] -eq
        "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=pass" -and
    $launcherDefaultWorkerCpuDefault -and
    $launcherDefaultWorkerExecutionDefault -and
    $launcherDefaultOpenSlDefault -and
    $launcherDefaultVorbisEnabled -and
    $launcherDefaultWebPEnabled -and
    $launcherDefaultBulkZeroEnabled -and
    $launcherDefaultRapidJsonDisabled -and
    $launcherDefaultQsortTelemetryDefault -and
    $launcherDefaultStepTelemetryDefault -and
    $launcherDefaultLongCallPcDefault -and
    $launcherDefaultQsortAlgorithmDefault -and
    $launcherDefaultPopupMode -and
    $launcherDefaultPopupCloseMode
$launcherDefaultUnsupported =
    $launcherDefaultExitCode -eq 1 -and
    $launcherDefaultBackendEvidence.Count -eq 1 -and
    $launcherDefaultBackendEvidence[0] -eq
        "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=unsupported" -and
    $launcherDefaultSelfTestEvidence.Count -eq 1 -and
    $launcherDefaultSelfTestEvidence[0] -like
        "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=refused*" -and
    $launcherDefaultWorkerCpuDefault -and
    $launcherDefaultWorkerExecutionDefault -and
    $launcherDefaultOpenSlDefault -and
    $launcherDefaultVorbisEnabled -and
    $launcherDefaultWebPEnabled -and
    $launcherDefaultBulkZeroEnabled -and
    $launcherDefaultRapidJsonDisabled -and
    $launcherDefaultQsortTelemetryDefault -and
    $launcherDefaultStepTelemetryDefault -and
    $launcherDefaultQsortAlgorithmDefault -and
    $launcherDefaultPopupMode -and
    $launcherDefaultPopupCloseMode
if (-not $launcherDefaultSupported -and -not $launcherDefaultUnsupported) {
    throw "Candy launcher-default nanosleep config did not pass or fail explicitly (exit $launcherDefaultExitCode)."
}
if ($launcherDefaultPostAnalyticsBudget.Count -ne 1) {
    throw "Candy launcher default post-analytics instruction budget is not exact."
}
$launcherWebPControlResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor -DisableCandyWebP103Host 2>&1 |
        ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -or
    @($launcherWebPControlResult | Where-Object {
        $_ -eq
            "ISOMR_CANDY_WEBP_103_HOST config=disabled default=off profile=candy-crush-1.333.2.1 substitutions=1 decoder=libwebp-1.0.3 exact-version=0x010003 addresses=none"
    }).Count -ne 1 -or
    $launcherWebPControlResult -notcontains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy launcher exact WebP 1.0.3 control selection is not exact."
}
$launcherVorbisControlResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor -DisableCandyVorbisHostMirror 2>&1 |
        ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -or
    @($launcherVorbisControlResult | Where-Object {
        $_ -eq
            "ISOMR_CANDY_VORBIS_HOST config=disabled default=off profile=candy-crush-1.333.2.1 substitutions=10 source=pinned-memory-or-aasset-provider-bounded-copy decoder=host-libvorbisfile audio=real-pcm addresses=none"
    }).Count -ne 1 -or
    $launcherVorbisControlResult -notcontains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy launcher Vorbis control selection is not exact."
}
$launcherBulkZeroControlResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor -DisableCandyBulkZeroStream 2>&1 |
        ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -or
    @($launcherBulkZeroControlResult | Where-Object {
        $_ -eq
            "ISOMR_CANDY_BULK_ZERO_STREAM config=disabled default=off profile=candy-crush-1.333.2.1 signatures=exact-loop-and-ostream-write mode=owner-thread-paused"
    }).Count -ne 1 -or
    $launcherBulkZeroControlResult -notcontains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy launcher bulk zero-stream control selection is not exact."
}
$launcherRapidJsonTreatmentResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor -CandyRapidJsonHostMirror 2>&1 |
        ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -or
    @($launcherRapidJsonTreatmentResult | Where-Object {
        $_ -eq
            "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR config=enabled default=off profile=candy-crush-1.333.2.1 parser-value-version=1.1.0 parser-value-revision=f54b0e47a08782a6131cc3d60f94d038fa6e0a51 allocator-layout=legacy-five-field scope=caller-0x4fe403c-minimum-4096 mode=owner-thread-paused addresses=none"
    }).Count -ne 1 -or
    $launcherRapidJsonTreatmentResult -notcontains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy launcher RapidJSON host-mirror treatment selection is not exact."
}
$testPopupResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor `
        -AcknowledgePopupWithoutUiForTesting `
        -CloseAfterPopupCompletionForTesting 2>&1 |
        ForEach-Object { "$_" })
$testPopupExitCode = $LASTEXITCODE
$testPopupResult | Write-Output
if ($testPopupExitCode -ne 0 -or
    @($testPopupResult | Where-Object {
        $_ -eq
            "ISOMR_CANDY_POPUP_DIALOG config=test-acknowledge-without-ui"
    }).Count -ne 1 -or
    @($testPopupResult | Where-Object {
        $_ -eq "ISOMR_CANDY_POPUP_DIALOG config=system-task-dialog"
    }).Count -ne 0 -or
    @($testPopupResult | Where-Object {
        $_ -eq "ISOMR_CANDY_POPUP_CLOSE config=test-after-completion"
    }).Count -ne 1 -or
    $testPopupResult -notcontains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy test-only popup acknowledgement selection failed (exit $testPopupExitCode)."
}
$popupCloseLauncherDependencyRejected = $false
try {
    & $candyProbeScript -NanosleepConfigSelfTest `
        -CloseAfterPopupCompletionForTesting 2>&1 | Out-Null
} catch {
    $popupCloseLauncherDependencyRejected =
        $_.Exception.Message -like
            '*CloseAfterPopupCompletionForTesting requires AcknowledgePopupWithoutUiForTesting.*'
}
if (-not $popupCloseLauncherDependencyRejected) {
    throw "Candy launcher test popup close dependency did not fail closed."
}
$duplicatePopupResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 `
    "acknowledge-popup-without-ui-for-testing" `
    "acknowledge-popup-without-ui-for-testing" "config-self-test" `
    2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicatePopupResult -notcontains "duplicate popup dialog mode" -or
    $duplicatePopupResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy duplicate popup dialog selection did not fail closed."
}
$invalidPopupResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 `
    "acknowledge-popup-without-ui-for-testing-suffixed" `
    "config-self-test" 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $invalidPopupResult -notcontains "invalid diagnostic option" -or
    $invalidPopupResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy invalid popup dialog selection did not fail closed."
}
$missingPopupCloseDependencyResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 `
    "close-after-popup-completion-for-testing" `
    "config-self-test" 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $missingPopupCloseDependencyResult -notcontains
        "test popup close requires no-UI popup acknowledgement" -or
    $missingPopupCloseDependencyResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy native test popup close dependency did not fail closed."
}
$duplicatePopupCloseResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 `
    "acknowledge-popup-without-ui-for-testing" `
    "close-after-popup-completion-for-testing" `
    "close-after-popup-completion-for-testing" `
    "config-self-test" 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicatePopupCloseResult -notcontains
        "duplicate test popup close mode" -or
    $duplicatePopupCloseResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy duplicate test popup close selection did not fail closed."
}
$disabledPostAnalyticsBudgetResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -PostAnalyticsInstructionBudget 0 -NanosleepBackend SleepFor 2>&1 |
        ForEach-Object { "$_" })
$disabledPostAnalyticsBudgetExitCode = $LASTEXITCODE
$disabledPostAnalyticsBudgetResult | Write-Output
if ($disabledPostAnalyticsBudgetExitCode -ne 0 -or
    @($disabledPostAnalyticsBudgetResult | Where-Object {
        $_ -like "ISOMR_CANDY_INSTRUCTION_BUDGET config=post-analytics-*"
    }).Count -ne 0) {
    throw "Candy launcher post-analytics instruction budget disable did not omit the option."
}
$explicitSleepForResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor 2>&1 |
        ForEach-Object { "$_" })
$explicitSleepForExitCode = $LASTEXITCODE
$explicitSleepForResult | Write-Output
if ($explicitSleepForExitCode -ne 0 -or
    $explicitSleepForResult -notcontains
        "ISOMR_NANOSLEEP_BACKEND requested=sleep-for effective=sleep-for" -or
    $explicitSleepForResult -notcontains
        "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=pass") {
    throw "Candy explicit SleepFor nanosleep config failed (exit $explicitSleepForExitCode)."
}
$maximalConfigResult = @(& $candyProbeScript -NanosleepConfigSelfTest `
    -NanosleepBackend SleepFor `
    -HeapWatchAllocationSize 4096 -HeapWatchRangeOffset 64 `
    -HeapWatchRangeSize 128 -ImportJournal -CallSvcCategoryTelemetry `
    -NanosleepCallsiteTelemetry -QsortOperationTelemetry `
    -StepBoundaryTelemetry -WorkerCpuSampling `
    -WorkerExecutionSampling -OpenSlTimedDiscard `
    -MemoryCaptureAccess Read -MemoryCaptureGeneration 7 `
    -MemoryCaptureOccurrence 2 2>&1 | ForEach-Object { "$_" })
$maximalConfigExitCode = $LASTEXITCODE
$maximalConfigResult | Write-Output
foreach ($maximalConfigEvidence in @(
    "ISOMR_HEAP_WATCH config=allocation-size4096 range-offset64 range-size128",
    "ISOMR_IMPORT_JOURNAL config=enabled capacity=64",
    "ISOMR_MEMORY_CAPTURE config=access-read generation7 occurrence2",
    "ISOMR_BIONIC_QSORT_TELEMETRY config=enabled scope=operation-boundary fields=count,element-size,comparisons,swaps,wall-ns,executor-setup-wall-ns,comparator-wall-ns,swap-wall-ns,residual-wall-ns addresses=none",
    "ISOMR_CANDY_STEP_TELEMETRY config=enabled scope=guest-step-call,frame-cadence,queued-pointer-latency step-fields=wall-ns,thread-cpu-available,thread-cpu-100ns,qsort-started,qsort-completed,qsort-failed,qsort-comparisons,qsort-swaps,qsort-wall-ns,qsort-executor-setup-wall-ns,qsort-comparator-wall-ns,qsort-swap-wall-ns,qsort-residual-wall-ns,qsort-open-end frame-fields=iteration-complete,frame-wall-ns,guest-draws,host-presents,lit-presents,pointer-events pointer-fields=sequence,action,queue-wait-ns,guest-call-ns,total-ns addresses=none",
    "ISOMR_CANDY_LONG_CALL_PC config=enabled trigger=step-wall-at-least-250000000ns boundary=deterministic-xorshift-jitter-184464-through-315535-mean-249999.5 cadence=first-then-deterministic-xorshift-1-in-8 fields=step,wall-threshold-ns,total-samples,retained,pc,lr,parent-lr,x0-min,x0-max,x1-min,x1-max,count,error,region-size,region,count,region-error addresses=module-relative-and-register-values-local-only",
    "ISOMR_WORKER_CPU_SAMPLING config=enabled cadence=watchdog-5s limit=32 start-group=first-ordinal-equality address-free=1",
    "ISOMR_WORKER_EXECUTION_SAMPLING config=enabled cadence=watchdog-5s selector=ordinals:0,1 boundaries=existing-only pc-group=exact-process-local host-residency=one-win32-control-sample address-free=1",
    "ISOMR_NATIVE_OPENSL config=enabled scope=timed-discard audio-player=wall-clock-paced sink=none callback=managed-thread address-free=1",
    "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST options=import-journal-enabled telemetry-svc-category nanosleep-callsite-enabled worker-cpu-enabled capture-enabled",
    "ISOMR_CANDY_WORKER_CPU elapsed=5 total=2 sampled=2 dropped=0 records=ordinal=7,start-group=3,lifecycle=active,cpu=available,cumulative-100ns=250,delta-100ns=150;ordinal=9,start-group=9,lifecycle=completed,cpu=unavailable,cumulative-100ns=unavailable,delta-100ns=unavailable",
    "ISOMR_NANOSLEEP_CALLSITE_CONFIG caller=module-relative parent=validated-one-frame-x29-record host-addresses=none top=8 per-request-lines=off",
    "ISOMR_NANOSLEEP_BACKEND requested=sleep-for effective=sleep-for",
    "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass")) {
    if ($maximalConfigExitCode -ne 0 -or
        $maximalConfigResult -notcontains $maximalConfigEvidence) {
        throw "Candy maximal compatible config forwarding failed (exit $maximalConfigExitCode): $maximalConfigEvidence"
    }
}
$workerCpuFormatLines = @($maximalConfigResult | Where-Object {
    $_ -like "ISOMR_CANDY_WORKER_CPU *"
})
if ($workerCpuFormatLines.Count -ne 1 -or
    $workerCpuFormatLines[0] -match '0x|handle|address|hash|thread=|guest|routine|module|offset|range') {
    throw "Candy worker CPU watchdog formatting is not uniquely address-free."
}
$workerExecutionFormatLines = @($maximalConfigResult | Where-Object {
    $_ -like "ISOMR_CANDY_WORKER_EXECUTION *"
})
if ($workerExecutionFormatLines.Count -ne 1 -or
    $workerExecutionFormatLines[0] -match
        '0x|address|hash|routine|module|offset|range|handle|thread|guest|raw|pc=') {
    throw "Candy worker execution formatting is not uniquely address-free."
}
$sliceTreatmentTicks = [UInt64]10000000
$sliceTreatmentResult = @(& $candyProbeScript -NanosleepConfigSelfTest `
    -NanosleepBackend SleepFor -WorkerCpuSampling `
    -WorkerExecutionSampling `
    -WorkerExecutionSliceTicks $sliceTreatmentTicks 2>&1 |
    ForEach-Object { "$_" })
$sliceTreatmentExitCode = $LASTEXITCODE
$sliceTreatmentResult | Write-Output
$sliceTreatmentMarker =
    "ISOMR_WORKER_EXECUTION_SAMPLING config=enabled cadence=watchdog-5s selector=ordinals:0,1 boundaries=inter-slice slice-ticks=$sliceTreatmentTicks callback=owner-thread-noop pc-group=exact-process-local host-residency=one-win32-control-sample address-free=1"
if ($sliceTreatmentExitCode -ne 0 -or
    @($sliceTreatmentResult | Where-Object {
        $_ -eq $sliceTreatmentMarker
    }).Count -ne 1 -or
    @($sliceTreatmentResult | Where-Object {
        $_ -like "ISOMR_WORKER_EXECUTION_SAMPLING *boundaries=existing-only*"
    }).Count -ne 0) {
    throw "Candy worker execution slice treatment marker is not exact."
}
$sliceLauncherDependencyRejected = $false
try {
    & $candyProbeScript -NanosleepConfigSelfTest `
        -WorkerExecutionSliceTicks 1000 2>&1 | Out-Null
} catch {
    $sliceLauncherDependencyRejected =
        $_.Exception.Message -like
            '*WorkerExecutionSliceTicks requires WorkerCpuSampling and WorkerExecutionSampling.*'
}
if (-not $sliceLauncherDependencyRejected) {
    throw "Candy launcher worker execution slice dependency did not fail closed."
}
$moduleRegionResult = @(& $candyProbeScript -NanosleepConfigSelfTest `
    -NanosleepBackend SleepFor -WorkerCpuSampling `
    -WorkerExecutionSampling `
    -WorkerExecutionSliceTicks $sliceTreatmentTicks `
    -WorkerExecutionModuleRegions 2>&1 |
    ForEach-Object { "$_" })
$moduleRegionExitCode = $LASTEXITCODE
$moduleRegionResult | Write-Output
$moduleRegionMarker =
    "ISOMR_WORKER_EXECUTION_MODULE_REGIONS config=enabled cadence=watchdog-5s selector=ordinals:0,1 region-size=4096 fields=pc,region addresses=module-relative-local-only"
$moduleRegionFormat =
    "ISOMR_CANDY_WORKER_EXECUTION_MODULE_REGIONS elapsed=5 region-size=4096 records=ordinal=0,pc-group=4,pc=lib%20test.so+0x1ffff,region=lib%20test.so+0x1f000;ordinal=1,pc-group=unavailable,pc=unavailable,region=unavailable"
if ($moduleRegionExitCode -ne 0 -or
    @($moduleRegionResult | Where-Object { $_ -eq $moduleRegionMarker }).Count -ne 1 -or
    @($moduleRegionResult | Where-Object { $_ -eq $moduleRegionFormat }).Count -ne 1 -or
    @($moduleRegionResult | Where-Object {
        $_ -like "ISOMR_CANDY_WORKER_EXECUTION_MODULE_REGIONS *" -and
        $_ -match 'raw-address=|host-address='
    }).Count -ne 0) {
    throw "Candy worker module-region telemetry is not exact and local-only."
}
$moduleRegionLauncherDependencyRejected = $false
try {
    & $candyProbeScript -NanosleepConfigSelfTest `
        -WorkerExecutionModuleRegions 2>&1 | Out-Null
} catch {
    $moduleRegionLauncherDependencyRejected =
        $_.Exception.Message -like
            '*WorkerExecutionModuleRegions requires WorkerCpuSampling, WorkerExecutionSampling, and nonzero WorkerExecutionSliceTicks.*'
}
if (-not $moduleRegionLauncherDependencyRejected) {
    throw "Candy launcher worker module-region dependency did not fail closed."
}
$duplicateWorkerCpuResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "config-self-test" `
    "worker-cpu-sampling" "worker-cpu-sampling" 2>&1 |
    ForEach-Object { "$_" })
$duplicateWorkerCpuExitCode = $LASTEXITCODE
if ($duplicateWorkerCpuExitCode -ne 2 -or
    $duplicateWorkerCpuResult -notcontains
        "duplicate worker CPU sampling mode" -or
    $duplicateWorkerCpuResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy duplicate worker CPU sampling option did not fail closed."
}
$duplicateOpenSlResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "opensl-timed-discard" `
    "opensl-timed-discard" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or $duplicateOpenSlResult -notcontains
        "duplicate OpenSL timed-discard mode" -or
    $duplicateOpenSlResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy duplicate OpenSL timed-discard option did not fail closed."
}
$vorbisHostConfigResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "candy-vorbis-host-mirror" `
    "config-self-test" 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -or
    $vorbisHostConfigResult -notcontains
        "ISOMR_CANDY_VORBIS_HOST config=enabled default=off profile=candy-crush-1.333.2.1 substitutions=10 source=pinned-memory-or-aasset-provider-bounded-copy decoder=host-libvorbisfile audio=real-pcm addresses=none" -or
    $vorbisHostConfigResult -notcontains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy Vorbis host mirror configuration evidence is incomplete."
}
$duplicateVorbisHostResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "candy-vorbis-host-mirror" `
    "candy-vorbis-host-mirror" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicateVorbisHostResult -notcontains
        "duplicate Candy Vorbis host mirror mode" -or
    $duplicateVorbisHostResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy duplicate Vorbis host mirror option did not fail closed."
}
$duplicateQsortTelemetryResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "qsort-operation-telemetry" `
    "qsort-operation-telemetry" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicateQsortTelemetryResult -notcontains
        "duplicate qsort operation telemetry mode" -or
    $duplicateQsortTelemetryResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy duplicate qsort operation telemetry did not fail closed."
}
$duplicateStepTelemetryResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "step-boundary-telemetry" `
    "step-boundary-telemetry" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicateStepTelemetryResult -notcontains
        "duplicate step boundary telemetry mode" -or
    $duplicateStepTelemetryResult -contains
        "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy duplicate step boundary telemetry did not fail closed."
}
$explicitBionicPartitionResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor -QsortAlgorithm BionicPartition 2>&1 |
        ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -or
    @($explicitBionicPartitionResult | Where-Object {
        $_ -eq "ISOMR_BIONIC_QSORT_ALGORITHM config=bionic-partition pivot=median-three-nine small=insertion equal=three-way"
    }).Count -ne 1) {
    throw "Candy Bionic partition qsort selection is not exact."
}
$explicitHeapSortResult = @(
    & $candyProbeScript -NanosleepConfigSelfTest `
        -NanosleepBackend SleepFor -QsortAlgorithm HeapSort 2>&1 |
        ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0 -or
    @($explicitHeapSortResult | Where-Object {
        $_ -eq "ISOMR_BIONIC_QSORT_ALGORITHM config=heap-sort pivot=none small=none equal=none"
    }).Count -ne 1) {
    throw "Candy heap-sort qsort control selection is not exact."
}
$duplicateQsortAlgorithmResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "qsort-algorithm=heap-sort" `
    "qsort-algorithm=bionic-partition" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicateQsortAlgorithmResult -notcontains
        "duplicate qsort algorithm mode") {
    throw "Candy duplicate qsort algorithm did not fail closed."
}
$invalidQsortAlgorithmResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "qsort-algorithm=quick" `
    "config-self-test" 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $invalidQsortAlgorithmResult -notcontains
        "invalid qsort algorithm mode") {
    throw "Candy invalid qsort algorithm did not fail closed."
}
$executionWithoutCpuResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "config-self-test" `
    "worker-execution-sampling=ordinals:0,1" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or $executionWithoutCpuResult -notcontains
        "worker execution sampling requires worker CPU sampling") {
    throw "Candy worker execution sampling without CPU did not fail closed."
}
$duplicateWorkerExecutionResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "worker-cpu-sampling" `
    "worker-execution-sampling=ordinals:0,1" `
    "worker-execution-sampling=ordinals:0,1" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or $duplicateWorkerExecutionResult -notcontains
        "duplicate worker execution sampling mode") {
    throw "Candy duplicate worker execution option did not fail closed."
}
foreach ($invalidExecutionSelector in @(
    "ordinals:1,0", "ordinals:0,0", "ordinals:0,1,2", "group:0")) {
    $invalidExecutionResult = @(& $candyProbeOutput `
        "config-self-test-root" "config-self-test-payload" `
        "config-self-test-data" 0 0 0 "worker-cpu-sampling" `
        "worker-execution-sampling=$invalidExecutionSelector" `
        "config-self-test" 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 2 -or $invalidExecutionResult -notcontains
            "invalid worker execution sampling selector") {
        throw "Candy invalid worker execution selector did not fail closed: $invalidExecutionSelector"
    }
}
$duplicateWorkerExecutionSliceResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "worker-cpu-sampling" `
    "worker-execution-sampling=ordinals:0,1" `
    "worker-execution-slice-ticks=1000" `
    "worker-execution-slice-ticks=1000" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicateWorkerExecutionSliceResult -notcontains
        "duplicate worker execution slice ticks") {
    throw "Candy duplicate worker execution slice option did not fail closed."
}
foreach ($invalidExecutionSliceTicks in @(
    "", "0", "-1", "+1", "1x", "18446744073709551616")) {
    $invalidExecutionSliceResult = @(& $candyProbeOutput `
        "config-self-test-root" "config-self-test-payload" `
        "config-self-test-data" 0 0 0 "worker-cpu-sampling" `
        "worker-execution-sampling=ordinals:0,1" `
        "worker-execution-slice-ticks=$invalidExecutionSliceTicks" `
        "config-self-test" 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 2 -or
        $invalidExecutionSliceResult -notcontains
            "invalid worker execution slice ticks") {
        throw "Candy invalid worker execution slice ticks did not fail closed: '$invalidExecutionSliceTicks'"
    }
}
foreach ($missingSliceDependencyOptions in @(
    @("worker-execution-slice-ticks=1000"),
    @("worker-cpu-sampling", "worker-execution-slice-ticks=1000"))) {
    $missingSliceDependencyResult = @(& $candyProbeOutput `
        "config-self-test-root" "config-self-test-payload" `
        "config-self-test-data" 0 0 0 $missingSliceDependencyOptions `
        "config-self-test" 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 2 -or
        $missingSliceDependencyResult -notcontains
            "worker execution slice ticks require worker CPU and execution sampling") {
        throw "Candy worker execution slice dependency did not fail closed."
    }
}
$highResolutionResult = @(& $candyProbeScript -NanosleepConfigSelfTest `
    -NanosleepBackend HighResolutionWaitable 2>&1 |
    ForEach-Object { "$_" })
$highResolutionExitCode = $LASTEXITCODE
$highResolutionResult | Write-Output
$highResolutionSupported =
    $highResolutionExitCode -eq 0 -and
    $highResolutionResult -contains
        "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=high-resolution-waitable" -and
    $highResolutionResult -contains
        "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=pass"
$highResolutionUnsupported =
    $highResolutionExitCode -eq 1 -and
    $highResolutionResult -contains
        "ISOMR_NANOSLEEP_BACKEND requested=high-resolution-waitable effective=unsupported" -and
    @($highResolutionResult | Where-Object {
        $_ -like "ISOMR_CANDY_NANOSLEEP_CONFIG_SELF_TEST=refused*"
    }).Count -eq 1
if (-not $highResolutionSupported -and -not $highResolutionUnsupported) {
    throw "Candy high-resolution nanosleep config did not pass or fail explicitly (exit $highResolutionExitCode)."
}
$nativeDefaultResult = @(& $candyProbeOutput `
    --self-test-nanosleep-config 2>&1 | ForEach-Object { "$_" })
$nativeDefaultExitCode = $LASTEXITCODE
if ($nativeDefaultExitCode -ne 0 -or
    $nativeDefaultResult -notcontains
        "ISOMR_NANOSLEEP_BACKEND requested=sleep-for effective=sleep-for") {
    throw "Candy native nanosleep default selection failed (exit $nativeDefaultExitCode)."
}
foreach ($invalidConfigCase in @(
    @{ Options = @("nanosleep-backend=invalid"); Error = "invalid nanosleep backend" },
    @{ Options = @("nanosleep-backend=sleep-for", "nanosleep-backend=high-resolution-waitable"); Error = "duplicate nanosleep backend" }
)) {
    $invalidConfigResult = @(& $candyProbeOutput `
        --self-test-nanosleep-config @($invalidConfigCase.Options) 2>&1 |
        ForEach-Object { "$_" })
    $invalidConfigExitCode = $LASTEXITCODE
    if ($invalidConfigExitCode -ne 2 -or
        $invalidConfigResult -notcontains $invalidConfigCase.Error -or
        @($invalidConfigResult | Where-Object {
            $_ -match '^ISOMR_NANOSLEEP_BACKEND requested='
        }).Count -ne 0) {
        throw "Candy invalid nanosleep selection did not fail before initialization."
    }
}
$unknownConfigResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "config-self-test" `
    "nanosleep-backend=sleep-for" "unknown-compatible-looking-option" `
    2>&1 | ForEach-Object { "$_" })
$unknownConfigExitCode = $LASTEXITCODE
if ($unknownConfigExitCode -ne 2 -or
    $unknownConfigResult -notcontains "invalid diagnostic option" -or
    $unknownConfigResult -contains "ISOMR_CANDY_ENTRY_CONFIG_SELF_TEST=pass") {
    throw "Candy unknown config option did not fail closed (exit $unknownConfigExitCode)."
}
$waitableTimerResult = @(& $waitableTimerOutput 2>&1 |
    ForEach-Object { "$_" })
$waitableTimerExitCode = $LASTEXITCODE
$waitableTimerResult | Write-Output
if ($waitableTimerExitCode -ne 0 -or
    $waitableTimerResult -notcontains
        "ISOMR_BIONIC_WAITABLE_TIMER_SMOKE=pass") {
    throw "Bionic waitable timer smoke failed (exit $waitableTimerExitCode)."
}
$invalidTelemetryCases = @(
    @{ Arguments = @("call-envelope-telemetry", "call-envelope-telemetry") }
    @{ Arguments = @("call-telemetry", "call-envelope-telemetry") }
    @{ Arguments = @("call-telemetry", "call-telemetry") }
    @{ Arguments = @("call-svc-category-telemetry", "call-svc-category-telemetry") }
    @{ Arguments = @("call-telemetry", "call-svc-category-telemetry") }
    @{ Arguments = @("call-envelope-telemetry", "call-svc-category-telemetry") }
)
foreach ($invalidTelemetryCase in $invalidTelemetryCases) {
    $invalidTelemetryArguments = $invalidTelemetryCase.Arguments
    $invalidTelemetryResult = & $candyProbeOutput `
        "missing-root-library" "missing-payload-root" "missing-data-root" `
        0 0 0 @invalidTelemetryArguments 2>&1 | ForEach-Object { "$_" }
    $invalidTelemetryExitCode = $LASTEXITCODE
    if ($invalidTelemetryExitCode -ne 2 -or
        $invalidTelemetryResult -notcontains
            "duplicate or incompatible call telemetry mode") {
        throw "Candy call telemetry selection did not fail closed (exit $invalidTelemetryExitCode)."
    }
}
$launcherConflictRejected = $false
try {
    & $candyProbeScript -AppRoot "missing-app-root" `
        -CallTelemetry -CallEnvelopeTelemetry -CallSvcCategoryTelemetry
} catch {
    $launcherConflictRejected =
        $_.Exception.Message -eq
            "CallTelemetry, CallEnvelopeTelemetry, and CallSvcCategoryTelemetry are incompatible."
}
if (-not $launcherConflictRejected) {
    throw "Candy launcher accepted incompatible call telemetry modes."
}
$duplicateCallsiteResult = @(& $candyProbeOutput `
    "missing-root-library" "missing-payload-root" "missing-data-root" `
    0 0 0 "nanosleep-callsite-telemetry" `
    "nanosleep-callsite-telemetry" 2>&1 | ForEach-Object { "$_" })
$duplicateCallsiteExitCode = $LASTEXITCODE
if ($duplicateCallsiteExitCode -ne 2 -or
    $duplicateCallsiteResult -notcontains
        "duplicate nanosleep callsite telemetry mode") {
    throw "Candy duplicate nanosleep callsite telemetry did not fail closed."
}
$result = & $output 2>&1
$exitCode = $LASTEXITCODE
$result | Write-Output
if ($exitCode -ne 0 -or
    $result -notcontains "ISOMR_AARCH64 add=123" -or
    $result -notcontains "ISOMR_AARCH64 memory=0xfeedbeefcafef00d" -or
    $result -notcontains "ISOMR_AARCH64 svc=84" -or
    $result -notcontains "ISOMR_AARCH64_EXECUTION=pass") {
    throw "Native AArch64 execution evidence is incomplete (exit $exitCode)."
}

"ISOMR_NATIVE_AARCH64=pass"

$bionicJumpsResult = & $bionicJumpsOutput 2>&1
$bionicJumpsExitCode = $LASTEXITCODE
$bionicJumpsResult | Write-Output
if ($bionicJumpsExitCode -ne 0 -or
    $bionicJumpsResult -notcontains "ISOMR_BIONIC_JUMPS imports=setjmp,longjmp control=non-local zero=one nonzero=preserved" -or
    $bionicJumpsResult -notcontains "ISOMR_BIONIC_JUMPS restore=x18-x30,sp,v8-v15 guest-buffer=256 thread-bound=pass" -or
    $bionicJumpsResult -notcontains "ISOMR_BIONIC_JUMPS reject=null,range,corrupt,cross-thread cleanup=pass" -or
    $bionicJumpsResult -notcontains "ISOMR_NATIVE_BIONIC_JUMPS=pass") {
    throw "Native Bionic jumps evidence is incomplete (exit $bionicJumpsExitCode)."
}

$candyEntryResult = & $candyEntryOutput 2>&1
$candyEntryExitCode = $LASTEXITCODE
$candyEntryResult | Write-Output
$envelopeConfigLines = @($candyEntryResult | Where-Object {
    $_ -eq "ISOMR_AARCH64_CALL_ENVELOPE config=enabled scope=main-bridge svc-timing=off memory-counters=off"
})
$envelopeRecordLines = @($candyEntryResult | Where-Object {
    $_ -match '^ISOMR_AARCH64_CALL_ENVELOPE ordinal='
})
$envelopeRecordPattern = '^ISOMR_AARCH64_CALL_ENVELOPE ordinal=\d+ stage=[a-z0-9-]+ result=(success|failure) halt=0x[0-9a-f]+ wall-ns=\d+ cpu-available=[01] cpu-100ns=\d+ ticks=\d+ runs=\d+ code-reads=\d+ svc=\d+ inter-slice=\d+ inter-slice-wall-ns=\d+ heap-metadata-advances=\d+$'
$categoryConfigLine = "ISOMR_AARCH64_CALL_SVC_CATEGORY config=enabled scope=main-bridge address-free=1 per-svc-lines=off memory-counters=off import-journal=unchanged"
$categoryConfigLines = @($candyEntryResult | Where-Object {
    $_ -eq $categoryConfigLine
})
$categoryRecordLines = @($candyEntryResult | Where-Object {
    $_ -match '^ISOMR_AARCH64_CALL_SVC_CATEGORY ordinal='
})
$importRecordLines = @($candyEntryResult | Where-Object {
    $_ -match '^ISOMR_AARCH64_CALL_SVC_IMPORT ordinal='
})
$nanosleepRecordLines = @($candyEntryResult | Where-Object {
    $_ -match '^ISOMR_AARCH64_CALL_NANOSLEEP ordinal='
})
$pointerLatencyLines = @($candyEntryResult | Where-Object {
    $_ -match '^ISOMR_CANDY_POINTER_LATENCY sequence='
})
$pointerLatencyPattern = '^ISOMR_CANDY_POINTER_LATENCY sequence=(?<sequence>\d+) action=(?<action>down|move|up|cancel) queue-wait-ns=(?<queueWait>\d+) guest-call-ns=(?<guestCall>\d+) total-ns=(?<total>\d+) addresses=none$'
$pointerLatencyValid = $pointerLatencyLines.Count -eq 3
for ($index = 0; $pointerLatencyValid -and
     $index -lt $pointerLatencyLines.Count; ++$index) {
    $pointerLatencyMatch = [regex]::Match(
        $pointerLatencyLines[$index], $pointerLatencyPattern)
    $expectedAction = @('down', 'move', 'up')[$index]
    if (-not $pointerLatencyMatch.Success -or
        [UInt64]$pointerLatencyMatch.Groups['sequence'].Value -ne
            [UInt64]($index + 1) -or
        $pointerLatencyMatch.Groups['action'].Value -ne $expectedAction -or
        [UInt64]$pointerLatencyMatch.Groups['queueWait'].Value -eq 0 -or
        [UInt64]$pointerLatencyMatch.Groups['guestCall'].Value -eq 0 -or
        [UInt64]$pointerLatencyMatch.Groups['total'].Value -ne
            [UInt64]$pointerLatencyMatch.Groups['queueWait'].Value +
            [UInt64]$pointerLatencyMatch.Groups['guestCall'].Value) {
        $pointerLatencyValid = $false
    }
}
$duplicateWorkerModuleRegionsResult = @(& $candyProbeOutput `
    "config-self-test-root" "config-self-test-payload" `
    "config-self-test-data" 0 0 0 "worker-cpu-sampling" `
    "worker-execution-sampling=ordinals:0,1" `
    "worker-execution-slice-ticks=1000" `
    "worker-execution-module-regions" `
    "worker-execution-module-regions" "config-self-test" 2>&1 |
    ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 2 -or
    $duplicateWorkerModuleRegionsResult -notcontains
        "duplicate worker execution module regions mode") {
    throw "Candy duplicate worker module-region option did not fail closed."
}
foreach ($missingModuleRegionDependencies in @(
    @("worker-execution-module-regions"),
    @("worker-cpu-sampling", "worker-execution-sampling=ordinals:0,1",
      "worker-execution-module-regions"))) {
    $missingModuleRegionResult = @(& $candyProbeOutput `
        "config-self-test-root" "config-self-test-payload" `
        "config-self-test-data" 0 0 0 $missingModuleRegionDependencies `
        "config-self-test" 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 2 -or
        $missingModuleRegionResult -notcontains
            "worker execution module regions require worker CPU, execution, and inter-slice sampling") {
        throw "Candy worker module-region dependency did not fail closed."
    }
}
$importRecordPattern = '^ISOMR_AARCH64_CALL_SVC_IMPORT ordinal=(?<ordinal>\d+) stage=(?<stage>[a-z0-9-]+) result=(?<result>success|failure)( rank\d+-name=[A-Za-z0-9_.%-]+ rank\d+-count=\d+ rank\d+-total-wall-ns=\d+ rank\d+-max-wall-ns=\d+ rank\d+-over-1ms-count=\d+ rank\d+-over-1ms-wall-ns=\d+){0,8} residual-count=(?<residualCount>\d+) residual-wall-ns=(?<residualWall>\d+)$'
$categoryRecordPattern = '^ISOMR_AARCH64_CALL_SVC_CATEGORY ordinal=(?<ordinal>\d+) stage=(?<stage>[a-z0-9-]+) result=(?<result>success|failure) total-count=(?<totalCount>\d+) total-wall-ns=(?<totalWall>\d+) condition-wait-count=(?<c1>\d+) condition-wait-wall-ns=(?<w1>\d+) mutex-rwlock-block-count=(?<c2>\d+) mutex-rwlock-block-wall-ns=(?<w2>\d+) futex-wait-count=(?<c3>\d+) futex-wait-wall-ns=(?<w3>\d+) pthread-once-join-count=(?<c4>\d+) pthread-once-join-wall-ns=(?<w4>\d+) qsort-bsearch-count=(?<c5>\d+) qsort-bsearch-wall-ns=(?<w5>\d+) allocation-free-count=(?<c6>\d+) allocation-free-wall-ns=(?<w6>\d+) jni-gles-count=(?<c7>\d+) jni-gles-wall-ns=(?<w7>\d+) other-count=(?<c8>\d+) other-wall-ns=(?<w8>\d+)$'
$nanosleepRecordPattern = '^ISOMR_AARCH64_CALL_NANOSLEEP ordinal=(?<ordinal>\d+) stage=(?<stage>[a-z0-9-]+) result=(?<result>success|failure) handler-count=(?<handlerCount>\d+) handler-total-wall-ns=(?<handlerTotal>\d+) handler-max-wall-ns=(?<handlerMax>\d+) successful-requested-count=(?<successfulCount>\d+) requested-total-ns=(?<requestedTotal>\d+) requested-max-ns=(?<requestedMax>\d+)$'
function Test-ArmrNanosleepAccountingMatch {
    param(
        [System.Text.RegularExpressions.Match]$NanosleepMatch,
        [System.Text.RegularExpressions.Match]$CategoryMatch,
        [System.Text.RegularExpressions.Match]$ImportMatch,
        [AllowNull()][object]$RankedNanosleep
    )

    if (-not $NanosleepMatch.Success -or -not $CategoryMatch.Success -or
        -not $ImportMatch.Success) {
        return $false
    }
    foreach ($correlationGroup in @('ordinal', 'stage', 'result')) {
        if ($NanosleepMatch.Groups[$correlationGroup].Value -ne
                $CategoryMatch.Groups[$correlationGroup].Value -or
            $NanosleepMatch.Groups[$correlationGroup].Value -ne
                $ImportMatch.Groups[$correlationGroup].Value) {
            return $false
        }
    }

    [UInt64]$handlerCount = $NanosleepMatch.Groups['handlerCount'].Value
    [UInt64]$handlerTotal = $NanosleepMatch.Groups['handlerTotal'].Value
    [UInt64]$handlerMaximum = $NanosleepMatch.Groups['handlerMax'].Value
    [UInt64]$successfulCount =
        $NanosleepMatch.Groups['successfulCount'].Value
    [UInt64]$requestedTotal =
        $NanosleepMatch.Groups['requestedTotal'].Value
    [UInt64]$requestedMaximum =
        $NanosleepMatch.Groups['requestedMax'].Value
    [UInt64]$categoryTotalCount =
        $CategoryMatch.Groups['totalCount'].Value
    [UInt64]$categoryTotalWall = $CategoryMatch.Groups['totalWall'].Value
    if ($handlerCount -eq 0 -or $successfulCount -eq 0 -or
        $successfulCount -gt $handlerCount -or
        $requestedMaximum -gt $requestedTotal -or
        $handlerMaximum -gt $handlerTotal -or
        $handlerCount -gt $categoryTotalCount -or
        $handlerTotal -gt $categoryTotalWall) {
        return $false
    }

    if ($null -ne $RankedNanosleep) {
        return $handlerCount -eq $RankedNanosleep.Count -and
            $handlerTotal -eq $RankedNanosleep.TotalWall -and
            $handlerMaximum -eq $RankedNanosleep.MaxWall
    }

    [UInt64]$residualCount = $ImportMatch.Groups['residualCount'].Value
    [UInt64]$residualWall = $ImportMatch.Groups['residualWall'].Value
    return $handlerCount -le $residualCount -and
        $handlerTotal -le $residualWall
}
$categoryAccountingLines = @($categoryRecordLines) + @(
    'ISOMR_AARCH64_CALL_SVC_CATEGORY ordinal=0 stage=parser-below-rank result=success total-count=10 total-wall-ns=1500 condition-wait-count=0 condition-wait-wall-ns=0 mutex-rwlock-block-count=0 mutex-rwlock-block-wall-ns=0 futex-wait-count=0 futex-wait-wall-ns=0 pthread-once-join-count=0 pthread-once-join-wall-ns=0 qsort-bsearch-count=0 qsort-bsearch-wall-ns=0 allocation-free-count=0 allocation-free-wall-ns=0 jni-gles-count=0 jni-gles-wall-ns=0 other-count=10 other-wall-ns=1500',
    'ISOMR_AARCH64_CALL_SVC_CATEGORY ordinal=0 stage=parser-mixed result=failure total-count=4 total-wall-ns=250 condition-wait-count=0 condition-wait-wall-ns=0 mutex-rwlock-block-count=0 mutex-rwlock-block-wall-ns=0 futex-wait-count=0 futex-wait-wall-ns=0 pthread-once-join-count=0 pthread-once-join-wall-ns=0 qsort-bsearch-count=0 qsort-bsearch-wall-ns=0 allocation-free-count=0 allocation-free-wall-ns=0 jni-gles-count=0 jni-gles-wall-ns=0 other-count=4 other-wall-ns=250')
$importAccountingLines = @($importRecordLines) + @(
    'ISOMR_AARCH64_CALL_SVC_IMPORT ordinal=0 stage=parser-below-rank result=success rank1-name=rank1 rank1-count=1 rank1-total-wall-ns=200 rank1-max-wall-ns=200 rank1-over-1ms-count=0 rank1-over-1ms-wall-ns=0 rank2-name=rank2 rank2-count=1 rank2-total-wall-ns=190 rank2-max-wall-ns=190 rank2-over-1ms-count=0 rank2-over-1ms-wall-ns=0 rank3-name=rank3 rank3-count=1 rank3-total-wall-ns=180 rank3-max-wall-ns=180 rank3-over-1ms-count=0 rank3-over-1ms-wall-ns=0 rank4-name=rank4 rank4-count=1 rank4-total-wall-ns=170 rank4-max-wall-ns=170 rank4-over-1ms-count=0 rank4-over-1ms-wall-ns=0 rank5-name=rank5 rank5-count=1 rank5-total-wall-ns=160 rank5-max-wall-ns=160 rank5-over-1ms-count=0 rank5-over-1ms-wall-ns=0 rank6-name=rank6 rank6-count=1 rank6-total-wall-ns=150 rank6-max-wall-ns=150 rank6-over-1ms-count=0 rank6-over-1ms-wall-ns=0 rank7-name=rank7 rank7-count=1 rank7-total-wall-ns=140 rank7-max-wall-ns=140 rank7-over-1ms-count=0 rank7-over-1ms-wall-ns=0 rank8-name=rank8 rank8-count=1 rank8-total-wall-ns=130 rank8-max-wall-ns=130 rank8-over-1ms-count=0 rank8-over-1ms-wall-ns=0 residual-count=2 residual-wall-ns=180',
    'ISOMR_AARCH64_CALL_SVC_IMPORT ordinal=0 stage=parser-mixed result=failure rank1-name=nanosleep rank1-count=3 rank1-total-wall-ns=200 rank1-max-wall-ns=100 rank1-over-1ms-count=0 rank1-over-1ms-wall-ns=0 residual-count=1 residual-wall-ns=50')
$nanosleepAccountingLines = @($nanosleepRecordLines) + @(
    'ISOMR_AARCH64_CALL_NANOSLEEP ordinal=0 stage=parser-below-rank result=success handler-count=1 handler-total-wall-ns=120 handler-max-wall-ns=120 successful-requested-count=1 requested-total-ns=500000 requested-max-ns=500000',
    'ISOMR_AARCH64_CALL_NANOSLEEP ordinal=0 stage=parser-mixed result=failure handler-count=3 handler-total-wall-ns=200 handler-max-wall-ns=100 successful-requested-count=2 requested-total-ns=16500001 requested-max-ns=15000000')
$categoryAccountingValid = $true
$categoryByKey = @{}
foreach ($categoryRecordLine in $categoryAccountingLines) {
    $categoryMatch = [regex]::Match($categoryRecordLine, $categoryRecordPattern)
    if (-not $categoryMatch.Success) {
        $categoryAccountingValid = $false
        continue
    }
    $categoryKey = '{0}|{1}|{2}' -f `
        $categoryMatch.Groups['ordinal'].Value,
        $categoryMatch.Groups['stage'].Value,
        $categoryMatch.Groups['result'].Value
    if (-not $categoryByKey.ContainsKey($categoryKey)) {
        $categoryByKey[$categoryKey] =
            [System.Collections.Generic.List[System.Text.RegularExpressions.Match]]::new()
    }
    $categoryByKey[$categoryKey].Add($categoryMatch)
    [UInt64]$countSum = 0
    [UInt64]$wallSum = 0
    foreach ($bucket in 1..8) {
        [UInt64]$bucketCount = $categoryMatch.Groups["c$bucket"].Value
        [UInt64]$bucketWall = $categoryMatch.Groups["w$bucket"].Value
        if ($bucketCount -eq 0 -and $bucketWall -ne 0) {
            $categoryAccountingValid = $false
        }
        $countSum += $bucketCount
        $wallSum += $bucketWall
    }
    [UInt64]$categoryTotalCount =
        $categoryMatch.Groups['totalCount'].Value
    [UInt64]$categoryTotalWall = $categoryMatch.Groups['totalWall'].Value
    if ($countSum -ne $categoryTotalCount -or
        $wallSum -ne $categoryTotalWall -or
        ($categoryTotalCount -eq 0 -and $categoryTotalWall -ne 0)) {
        $categoryAccountingValid = $false
    }
}
$importAccountingValid =
    $importAccountingLines.Count -eq $categoryAccountingLines.Count
$importKeys = @{}
$importsByKey = @{}
$rankPattern = ' rank(?<nameRank>\d+)-name=(?<name>[A-Za-z0-9_.%-]+) rank(?<countRank>\d+)-count=(?<count>\d+) rank(?<wallRank>\d+)-total-wall-ns=(?<wall>\d+) rank(?<maxRank>\d+)-max-wall-ns=(?<max>\d+) rank(?<overCountRank>\d+)-over-1ms-count=(?<overCount>\d+) rank(?<overWallRank>\d+)-over-1ms-wall-ns=(?<overWall>\d+)'
foreach ($importLine in $importAccountingLines) {
    $importMatch = [regex]::Match($importLine, $importRecordPattern)
    if (-not $importMatch.Success) {
        $importAccountingValid = $false
        continue
    }
    $importKey = '{0}|{1}|{2}' -f `
        $importMatch.Groups['ordinal'].Value,
        $importMatch.Groups['stage'].Value,
        $importMatch.Groups['result'].Value
    if (-not $categoryByKey.ContainsKey($importKey)) {
        $importAccountingValid = $false
        continue
    }
    $importOccurrence = if ($importKeys.ContainsKey($importKey)) {
        [int]$importKeys[$importKey]
    } else {
        0
    }
    if ($importOccurrence -ge $categoryByKey[$importKey].Count) {
        $importAccountingValid = $false
        continue
    }
    $importKeys[$importKey] = $importOccurrence + 1
    $categoryMatch = $categoryByKey[$importKey][$importOccurrence]
    [UInt64]$expectedCount = $categoryMatch.Groups['totalCount'].Value
    [UInt64]$expectedWall = $categoryMatch.Groups['totalWall'].Value
    [UInt64]$rankedCount = 0
    [UInt64]$rankedWall = 0
    [UInt64]$previousWall = [UInt64]::MaxValue
    [string]$previousName = ''
    $nanosleepImport = $null
    $rankMatches = [regex]::Matches($importLine, $rankPattern)
    for ($rankIndex = 0; $rankIndex -lt $rankMatches.Count; ++$rankIndex) {
        $rankMatch = $rankMatches[$rankIndex]
        $expectedRank = $rankIndex + 1
        $rankFields = @('nameRank', 'countRank', 'wallRank', 'maxRank',
            'overCountRank', 'overWallRank')
        if (@($rankFields | Where-Object {
                [int]$rankMatch.Groups[$_].Value -ne $expectedRank
            }).Count -ne 0) {
            $importAccountingValid = $false
        }
        $name = $rankMatch.Groups['name'].Value
        $nameWithoutEscapes = [regex]::Replace($name, '%[0-9A-F]{2}', '')
        if ($nameWithoutEscapes.Contains('%')) {
            $importAccountingValid = $false
        }
        [UInt64]$count = $rankMatch.Groups['count'].Value
        [UInt64]$wall = $rankMatch.Groups['wall'].Value
        [UInt64]$maximum = $rankMatch.Groups['max'].Value
        [UInt64]$overCount = $rankMatch.Groups['overCount'].Value
        [UInt64]$overWall = $rankMatch.Groups['overWall'].Value
        if ($count -eq 0 -or $maximum -gt $wall -or
            $overCount -gt $count -or $overWall -gt $wall -or
            ($wall -eq 0 -and ($maximum -ne 0 -or $overWall -ne 0)) -or
            ($overCount -eq 0 -and $overWall -ne 0) -or
            ($overCount -ne 0 -and $overWall -eq 0) -or
            $wall -gt $previousWall -or
            ($wall -eq $previousWall -and $rankIndex -ne 0 -and
                [string]::CompareOrdinal($previousName, $name) -gt 0)) {
            $importAccountingValid = $false
        }
        $previousWall = $wall
        $previousName = $name
        $rankedCount += $count
        $rankedWall += $wall
        if ($name -eq 'nanosleep') {
            $nanosleepImport = [pscustomobject]@{
                Count = $count
                TotalWall = $wall
                MaxWall = $maximum
            }
        }
    }
    [UInt64]$residualCount = $importMatch.Groups['residualCount'].Value
    [UInt64]$residualWall = $importMatch.Groups['residualWall'].Value
    if (($residualCount -eq 0 -and $residualWall -ne 0) -or
        $rankedCount + $residualCount -ne
            $expectedCount -or
        $rankedWall + $residualWall -ne $expectedWall) {
        $importAccountingValid = $false
    }
    if (-not $importsByKey.ContainsKey($importKey)) {
        $importsByKey[$importKey] =
            [System.Collections.Generic.List[object]]::new()
    }
    $importsByKey[$importKey].Add([pscustomobject]@{
        Match = $importMatch
        Nanosleep = $nanosleepImport
    })
}
foreach ($categoryKey in $categoryByKey.Keys) {
    if (-not $importKeys.ContainsKey($categoryKey) -or
        [int]$importKeys[$categoryKey] -ne $categoryByKey[$categoryKey].Count) {
        $importAccountingValid = $false
    }
}
$nanosleepAccountingValid = $true
$nanosleepKeys = @{}
foreach ($nanosleepLine in $nanosleepAccountingLines) {
    $nanosleepMatch = [regex]::Match(
        $nanosleepLine, $nanosleepRecordPattern)
    if (-not $nanosleepMatch.Success) {
        $nanosleepAccountingValid = $false
        continue
    }
    $nanosleepKey = '{0}|{1}|{2}' -f `
        $nanosleepMatch.Groups['ordinal'].Value,
        $nanosleepMatch.Groups['stage'].Value,
        $nanosleepMatch.Groups['result'].Value
    if (-not $categoryByKey.ContainsKey($nanosleepKey) -or
        -not $importsByKey.ContainsKey($nanosleepKey)) {
        $nanosleepAccountingValid = $false
        continue
    }
    $nanosleepOccurrence = if ($nanosleepKeys.ContainsKey($nanosleepKey)) {
        [int]$nanosleepKeys[$nanosleepKey]
    } else {
        0
    }
    if ($nanosleepOccurrence -ge $categoryByKey[$nanosleepKey].Count -or
        $nanosleepOccurrence -ge $importsByKey[$nanosleepKey].Count) {
        $nanosleepAccountingValid = $false
        continue
    }
    $nanosleepKeys[$nanosleepKey] = $nanosleepOccurrence + 1
    $nanosleepImportAccounting =
        $importsByKey[$nanosleepKey][$nanosleepOccurrence]
    if (-not (Test-ArmrNanosleepAccountingMatch `
            -NanosleepMatch $nanosleepMatch `
            -CategoryMatch $categoryByKey[$nanosleepKey][$nanosleepOccurrence] `
            -ImportMatch $nanosleepImportAccounting.Match `
            -RankedNanosleep $nanosleepImportAccounting.Nanosleep)) {
        $nanosleepAccountingValid = $false
    }
}
$nanosleepRejectFixtureLines = @(
    'ISOMR_AARCH64_CALL_NANOSLEEP ordinal=0 stage=parser-below-rank result=success handler-count=11 handler-total-wall-ns=120 handler-max-wall-ns=120 successful-requested-count=1 requested-total-ns=500000 requested-max-ns=500000',
    'ISOMR_AARCH64_CALL_NANOSLEEP ordinal=0 stage=parser-below-rank result=success handler-count=3 handler-total-wall-ns=181 handler-max-wall-ns=181 successful-requested-count=1 requested-total-ns=500000 requested-max-ns=500000'
)
$nanosleepRejectFixtureValid = $true
$nanosleepRejectFixtureKey = '0|parser-below-rank|success'
$nanosleepRejectCategoryMatch =
    $categoryByKey[$nanosleepRejectFixtureKey][0]
$nanosleepRejectImportAccounting =
    $importsByKey[$nanosleepRejectFixtureKey][0]
foreach ($nanosleepRejectFixtureLine in $nanosleepRejectFixtureLines) {
    $nanosleepRejectMatch = [regex]::Match(
        $nanosleepRejectFixtureLine, $nanosleepRecordPattern)
    if (-not $nanosleepRejectMatch.Success -or
        (Test-ArmrNanosleepAccountingMatch `
            -NanosleepMatch $nanosleepRejectMatch `
            -CategoryMatch $nanosleepRejectCategoryMatch `
            -ImportMatch $nanosleepRejectImportAccounting.Match `
            -RankedNanosleep $nanosleepRejectImportAccounting.Nanosleep)) {
        $nanosleepRejectFixtureValid = $false
    }
}
$envelopeDefaultIndex = [array]::IndexOf(
    $candyEntryResult,
    "ISOMR_AARCH64_CALL_ENVELOPE_DEFAULT=off")
$envelopeConfigIndex = [array]::IndexOf(
    $candyEntryResult,
    "ISOMR_AARCH64_CALL_ENVELOPE config=enabled scope=main-bridge svc-timing=off memory-counters=off")
$firstEnvelopeRecordIndex = -1
for ($index = 0; $index -lt $candyEntryResult.Count; ++$index) {
    if ($candyEntryResult[$index] -match
        '^ISOMR_AARCH64_CALL_ENVELOPE ordinal=') {
        $firstEnvelopeRecordIndex = $index
        break
    }
}
$categoryDefaultIndex = [array]::IndexOf(
    $candyEntryResult,
    "ISOMR_AARCH64_CALL_SVC_CATEGORY_DEFAULT=off")
$categoryConfigIndex = [array]::IndexOf(
    $candyEntryResult, $categoryConfigLine)
$firstCategoryRecordIndex = -1
for ($index = 0; $index -lt $candyEntryResult.Count; ++$index) {
    if ($candyEntryResult[$index] -match
        '^ISOMR_AARCH64_CALL_SVC_CATEGORY ordinal=') {
        $firstCategoryRecordIndex = $index
        break
    }
}
$guestSessionText = Get-Content -Raw $guestSessionSource
$timeSourceText = Get-Content -Raw $timeSource
$callSvcSourceMatch = [regex]::Match(
    $guestSessionText,
    '(?s)void CallSVC\(std::uint32_t immediate\) override \{(?<body>.*?)\r?\n    \}\r?\n    void ExceptionRaised')
$nanosleepSoleReadValid =
    $callSvcSourceMatch.Success -and
    $callSvcSourceMatch.Groups['body'].Value -notmatch 'BionicTimespec' -and
    $callSvcSourceMatch.Groups['body'].Value -notmatch `
        'BionicTimespecToNanoseconds' -and
    $callSvcSourceMatch.Groups['body'].Value -notmatch 'ReadMemory\(' -and
    [regex]::Matches(
        $timeSourceText,
        'memory->Read\(\(\*registers\)\[0\], &requested,').Count -eq 1
if ($candyEntryExitCode -ne 0 -or
    $envelopeConfigLines.Count -ne 1 -or
    $envelopeDefaultIndex -lt 0 -or
    $envelopeConfigIndex -le $envelopeDefaultIndex -or
    $firstEnvelopeRecordIndex -le $envelopeConfigIndex -or
    $envelopeRecordLines.Count -ne 15 -or
    @($envelopeRecordLines | Where-Object {
        $_ -notmatch $envelopeRecordPattern
    }).Count -ne 0 -or
    @($envelopeRecordLines | Where-Object {
        $_ -match 'ordinal=15 stage=step-2 result=success '
    }).Count -ne 1 -or
    $categoryConfigLines.Count -ne 1 -or
    $categoryDefaultIndex -lt 0 -or
    $categoryConfigIndex -le $categoryDefaultIndex -or
    $firstCategoryRecordIndex -le $categoryConfigIndex -or
    $categoryRecordLines.Count -ne 19 -or
    -not $categoryAccountingValid -or
    -not $importAccountingValid -or
    -not $nanosleepAccountingValid -or
    -not $nanosleepRejectFixtureValid -or
    -not $nanosleepSoleReadValid -or
    -not $pointerLatencyValid -or
    @($categoryRecordLines | Where-Object {
        $_ -match ' result=success '
    }).Count -eq 0 -or
    @($categoryRecordLines | Where-Object {
        $_ -match 'ordinal=10 stage=analytics-response-2 result=failure '
    }).Count -ne 1 -or
    @($categoryRecordLines | Where-Object {
        $_ -match 'ordinal=9 stage=analytics-response-2 result=failure '
    }).Count -ne 1 -or
    $candyEntryResult -notcontains "ISOMR_CANDY_ENTRY_FIXTURE handles=pass create=pass lifecycle=pass close-lifecycle=pause-stop-terminate-flag-destroy display=pass step=pass analytics=no-value-null-and-empty-owner-thread-after-step-failure-atomic age-signals=offline-error--100-owner-thread-after-step input=inter-slice-dispatched-post-call-win32-down-move-up-scaled-clamped-pointer0 worker-failure=first-latched-before-call-and-in-flight-slice-abort-actual-error cleanup=pass" -or
    $candyEntryResult -notcontains "ISOMR_NANOSLEEP_CALLSITE_FORMAT top=8 deterministic=count-immediate-parent-thread residual=reconciled unresolved=suppressed absolute-addresses=none" -or
    $candyEntryResult -notcontains "ISOMR_NATIVE_CANDY_ENTRY=pass") {
    throw "Native Candy entry evidence is incomplete (exit $candyEntryExitCode)."
}

$elfResult = & $elfOutput 2>&1
$elfExitCode = $LASTEXITCODE
$elfResult | Write-Output
$telemetryLines = @($elfResult | Where-Object {
    $_ -match '^ISOMR_AARCH64_CALL_TELEMETRY result='
})
$telemetryPattern = '^ISOMR_AARCH64_CALL_TELEMETRY result=(success|failure) halt=0x[0-9a-f]+ wall-ns=\d+ cpu-available=[01] cpu-100ns=\d+ ticks=\d+ runs=\d+ code-reads=\d+ scalar-reads=\d+ vector-reads=\d+ writes=\d+ exclusive-writes=\d+ svc=\d+ svc-wall-ns=\d+ cntpct=\d+ heap-reads=\d+ module-reads=\d+ heap-writes=\d+ module-writes=\d+ heap-read-cache-hits=\d+ heap-read-cache-validations=\d+ heap-write-cache-hits=\d+ heap-write-cache-validations=\d+ heap-metadata-advances=\d+ memory-samples=\d+ memory-sample-wall-ns=\d+$'
if ($elfExitCode -ne 0 -or
    $telemetryLines.Count -ne 22 -or
    @($elfResult | Where-Object {
        $_ -eq "ISOMR_NANOSLEEP_CALLSITE_CONFIG caller=module-relative parent=validated-one-frame-x29-record host-addresses=none top=8 per-request-lines=off"
    }).Count -ne 1 -or
    @($telemetryLines | Where-Object { $_ -notmatch $telemetryPattern }).Count -ne 0 -or
    $elfResult -notcontains "ISOMR_GUEST_HEAP_ACCESS_CACHE scalar=hit vector=hit reuse=invalidated realloc=invalidated protection=invalidated unmap=invalidated adjacent-read=fallback cross-write=reject isolation=pass concurrency=pass forensic=preserved" -or
    $elfResult -notcontains "ISOMR_GUEST_HEAP_METADATA_SCALE allocations=32768 alternating-reuse=pass first-fit=pass aligned-first-fit=pass equal-size-low-offset=pass avl-query-visits-le64=pass failure-diagnostic=no-fit-metadata-oom-snapshot-pass metadata-oom-rollback=pass invariants=pass cleanup=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_CALL_ENVELOPE_FIXTURE default=off mode=explicit success=failure-reset ticks-runs-code=positive svc=count-without-timing memory-counters=off inter-slice=count-time budget=preserved metadata=global-concurrent-wrap-safe" -or
    $elfResult -notcontains "ISOMR_AARCH64_SVC_CATEGORY_FIXTURE default=off mode=explicit categories=condition-wait,mutex-rwlock-block,futex-wait,pthread-once-join,qsort-bsearch,allocation-free,jni-gles,other imports=aggregate-threshold-boundary-unresolved-residual nanosleep=handler-sole-read-value-capture-success-after-remainder-failures-unpublished-mode-isolated-reset-overflow-suppressed success=failure-reset sums=count-wall memory-counters=off timing=single-boundary" -or
    $elfResult -notcontains "ISOMR_ELF64 map=16384 entry=0x1000001000 segments=2" -or
    $elfResult -notcontains "ISOMR_ELF64 relocations=2 relative=1 imports=1" -or
    $elfResult -notcontains "ISOMR_ELF64 execute=42" -or
    $elfResult -notcontains "ISOMR_ELF64 reject=3 permissions=pass" -or
    $elfResult -notcontains "ISOMR_ELF64 hashes=sysv-gnu malformed-gnu=reject exports=tls-ignored-absolute-value-end-marker" -or
    $elfResult -notcontains "ISOMR_ELF64_LOADER=pass" -or
    $elfResult -notcontains "ISOMR_ELF64_GRAPH modules=2 needed=libprovider.so" -or
    $elfResult -notcontains "ISOMR_ELF64_GRAPH import=0x2100001100 execute=42" -or
    $elfResult -notcontains "ISOMR_ELF64_GRAPH initializers=1 init_x3=99" -or
    $elfResult -notcontains "ISOMR_ELF64_GRAPH reject=2 path=pass cycle=pass" -or
    $elfResult -notcontains "ISOMR_ELF64_GRAPH host-needed=1 unknown=reject" -or
    $elfResult -notcontains "ISOMR_ELF64_GRAPH dynamic-sibling=rollback-address-reuse-commit-pass" -or
    $elfResult -notcontains "ISOMR_ELF64_GRAPH=pass" -or
    $elfResult -notcontains "ISOMR_IMPORT cells=1 address=0x600000000000 dispatch=42" -or
    $elfResult -notcontains "ISOMR_IMPORT symbols=2 callable=1 data=1 resolve=pass dispatch=reject relocation=pass concurrency=pass ownership=provider" -or
    $elfResult -notcontains "ISOMR_IMPORT reject=7 duplicate-cross-kind=pass zero=pass overlap=pass sealed=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_IMPORTS=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_FUNCTION_TABLE slots=2 null=pass object=0x720000000010 table=0x720000000000 execute=42" -or
    $elfResult -notcontains "ISOMR_GUEST_FUNCTION_TABLE reject=7 registry=sealed unresolved=pass duplicate=pass range=pass empty=pass incomplete=pass mutation=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_FUNCTION_TABLE setup=overflow-capacity-zero cleanup=partial-normal" -or
    $elfResult -notcontains "ISOMR_NATIVE_GUEST_FUNCTION_TABLE=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_OPENSL iids=engine-play-queue-config-volume-data engine=realize-get-interface output-mix=create-realize audio-player=timed-discard queue=managed-callback position=10ms guest-vtables=aarch64 invalid=named cleanup=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_OPENSL=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMORY imports=7 malloc=0x610000000000 guest=42" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMORY calloc=zero realloc=preserve posix_memalign=aligned-preserve free=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMORY reject=2 overflow=pass invalid-free=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMORY bounds=pass realloc-oom=preserve" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMORY concurrency=4 allocations=clean" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_MEMORY=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMOPS imports=24 memset=0x620000000018 guest=0x2a2a2a2a2a2a2a2a" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMOPS memcpy=pass memcpy-chk=bounded memmove=pass memcmp=pass memchr=found-missing-zero wmemchr=utf32-found-missing-zero strcpy=terminated-copy" -or
    $elfResult -notcontains "ISOMR_BIONIC_STRINGS strlen=14 strlen-chk=bounded strnlen=terminated-bounded-zero strchr=found-missing-terminator strrchr=last-terminator strpbrk=found-missing-empty strspn=prefix-all-missing-empty strstr=found-missing-empty strcmp=equal/order strncmp=prefix-zero-order strcasecmp=ascii-equal-order-reject" -or
    $elfResult -notcontains "ISOMR_BIONIC_MEMOPS reject=6 bounds=pass freed=pass string=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_MEMOPS=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_TIME imports=15 clock=0x630000000000 guest=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_TIME monotonic=advance realtime=pass boottime=pass nanosleep=duration-remainder-zero usleep=10ms-selected-backend" -or
    $elfResult -notcontains "ISOMR_BIONIC_TIME cpu=pass resolution=1000ns timeval=pass time=store-null" -or
    $elfResult -notcontains "ISOMR_BIONIC_TIME localtime=epoch-known overwrite=stable mktime=roundtrip-normalize threads=isolated stack-guard=slot5-stable-disjoint cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_TIME reject=10 clock=pass output=pass time=output localtime=address-range mktime=address-range nanosleep=duration,request,remainder" -or
    $elfResult -notcontains "ISOMR_NANOSLEEP_CALLSITE_FIXTURE default=off-clock-hook-frame-read-zero exact=500000 adjacent=excluded parent=one-frame-valid-keyed-invalid-zero-misaligned-partial-unmapped-unresolved threads=2 callers=2 overlap=close-waits success=backend-and-remainder-failures-excluded generation=stale-reset table=128-overflow checked=count-request-body resolver=immediate-parent-code-boundaries-sanitized-unresolved" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_TIME=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_ERRNO threads=2 addresses=distinct values=100,101" -or
    $elfResult -notcontains "ISOMR_BIONIC_ERRNO time-errors=22,14 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_ERRNO=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_THREAD imports=5 pthread_self=0x640000000000 guest=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_THREAD ids=distinct equal=pass yield=pass setname=current-win32-utf8 restore=detach reject=3,14,34" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_THREAD_IDENTITY=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_SYNC imports=31 mutex=contention-4x250" -or
    $elfResult -notcontains "ISOMR_BIONIC_SYNC mutexattr=recursive init-settype-destroy=pass condattr=realtime-monotonic init-setclock-destroy=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_SYNC cond=signal-broadcast-shutdown-wake timedwait=realtime-monotonic-110" -or
    $elfResult -notcontains "ISOMR_BIONIC_SYNC reject=6 ownership=pass cleanup=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_SYNC=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_TLS imports=4 keys=2 isolated=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_TLS destructors=2-pass worker=pass reuse=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_TLS reject=4 capacity=11 address=14 key=22" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_TLS=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_THREAD_LIFECYCLE imports=18 once=single create=3 concurrent=2 join=pass detach=pass snapshot=active-clean worker-failure=first-latched-detached process-exit=cooperative-cancel-wake-join-reap" -or
    $elfResult -notcontains "ISOMR_BIONIC_BSEARCH guest-comparator=aarch64 found=pass missing=pass zero=pass overflow=reject same-thread-tls=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_QSORT guest-comparator=aarch64 heapsort=ascending-duplicates in-place=pass zero=pass overflow-readonly=reject same-thread-tls=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_QSORT_BIONIC_PARTITION guest-comparator=aarch64 median-three-nine=pass insertion=pass equal-three-way=pass shapes=reverse,ordered,all-equal,duplicate-heavy in-place=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_QSORT_TELEMETRY default=off operation-boundary=lines aggregate-only=snapshot-coherent fields=count,element-size,comparisons,swaps,wall-ns,executor-setup-wall-ns,comparator-wall-ns,swap-wall-ns,residual-wall-ns addresses=none" -or
    @($elfResult | Where-Object {
        $_ -match '^ISOMR_BIONIC_QSORT_OPERATION sequence=1 state=begin count=6 element-size=4$'
    }).Count -ne 1 -or
    @($elfResult | Where-Object {
        $_ -match '^ISOMR_BIONIC_QSORT_OPERATION sequence=1 state=end result=pass comparisons=[1-9][0-9]* swaps=[1-9][0-9]* wall-ns=[1-9][0-9]* executor-setup-wall-ns=[0-9]+ comparator-wall-ns=[1-9][0-9]* swap-wall-ns=[1-9][0-9]* residual-wall-ns=[0-9]+$'
    }).Count -ne 1 -or
    @($elfResult | Where-Object {
        $_ -match '^ISOMR_BIONIC_QSORT_OPERATION sequence=[2-5] state=begin count=64 element-size=4$'
    }).Count -ne 4 -or
    @($elfResult | Where-Object {
        $_ -match '^ISOMR_BIONIC_QSORT_OPERATION sequence=[2-5] state=end result=pass comparisons=[1-9][0-9]* swaps=[1-9][0-9]* wall-ns=[1-9][0-9]* executor-setup-wall-ns=[0-9]+ comparator-wall-ns=[1-9][0-9]* swap-wall-ns=[1-9][0-9]* residual-wall-ns=[0-9]+$'
    }).Count -ne 4 -or
    @($elfResult | Where-Object {
        $_ -like 'ISOMR_BIONIC_QSORT_OPERATION * state=begin *'
    }).Count -ne 5 -or
    @($elfResult | Where-Object {
        $_ -like 'ISOMR_BIONIC_QSORT_OPERATION * state=end *'
    }).Count -ne 5 -or
    $elfResult -notcontains "ISOMR_BIONIC_THREAD_LIFECYCLE guest=42,101 tpidr=pass slot5-canary=pass stack=pass sessions=13 destructors=2" -or
    $elfResult -notcontains "ISOMR_BIONIC_THREAD_LIFECYCLE reject=7 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_WORKER_CPU default=off configure=before-create reject=after-create ordinals=monotonic start-groups=retained-first-success-equality-distinct-stable lifecycle=active,completed cpu=available-monotonic-100ns lifetime=detached-joined-snapshot-held bounded=32-truncated handle-cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_WORKER_EXECUTION default=off requires=worker-cpu selector=ordinals-0,1 selected-only=pass boundaries=owner-session coherent=single-writer-multi-reader-seq-cst-bounded monotonic=pass pc-groups=exact-process-local-stable-64-overflow host-residency=one-win32-control-sample-resume-pass-address-free address-free-dto=pass local-pc-lookup=explicit-only destruction=unavailable-pc-retained lifetime=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_WORKER_EXECUTION_SLICE default=zero selected-ordinals=0,1 ordinal2=zero configure=before-create failure=propagated cleanup=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_THREAD_LIFECYCLE=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_SESSION main=42 import=dispatch worker-sessions=3 tpidr=pass stack=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_SESSION reject=2 executable=checked budget=runtime-reconfig-bounded cleanup=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_SESSION exclusive=ldxp-stxp shared-monitor=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_INTER_SLICE default=off resume=same-call budget=total-preserved,next-slice-owner-callback-only callback=cadenced-owner-thread-failure-checked pc-sample=paused-owner-callback-only lifetime=cleared" -or
    $elfResult -notcontains "ISOMR_AARCH64_INTER_SLICE_EXECUTION synthetic-loop=bounded result=control-equal runs=multiple telemetry=monotonic-owner-thread source-calibration=synthetic-only" -or
    $elfResult -notcontains "ISOMR_AARCH64_EXECUTION_BREAKPOINT default=off exact-instruction=skipped owner-thread=required resume=registers-sp-pc-validated same-call=pass additive=two-address-independent-remove clear=restores-original invalid-resume=reject callback-refusal=checked lifetime=cleared" -or
    @($elfResult | Where-Object {
        $_ -match '^ISOMR_AARCH64_SLICE_CALIBRATION candidate=(100000|1000000|2000000) boundaries=\d+ runs=\d+ repeats=7 control-median-ns=\d+ treatment-median-ns=\d+ overhead-bps=-?\d+ scope=synthetic-only$'
    }).Count -ne 3 -or
    $elfResult -notcontains "ISOMR_AARCH64_MEMORY_CALLBACK mode=fast" -or
    $elfResult -notcontains "ISOMR_AARCH64_MEMORY_CALLBACK mode=forensic" -or
    $elfResult -notcontains "ISOMR_AARCH64_MEMORY_CALLBACK fast=checked-read-write-refusal forensic=heap-watch capture=read-write" -or
    $elfResult -notcontains "ISOMR_AARCH64_CALL_TELEMETRY_FIXTURE default=off ordinary-read-write=pass svc=one exclusive-write=pass budget=failure ticks-code=positive reset=pass sample=one-per-1024" -or
    $elfResult -notcontains "ISOMR_NATIVE_AARCH64_SESSION=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_ENTRY_ABI general=pass fp=pass mixed=pass vector=pass stack=pass cleanup=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_PROFILE modules=1 imports=746 init=0 entry=42 jni=0x00010006" -or
    $elfResult -notcontains "ISOMR_AARCH64_PROFILE_CODE_RESOLVER exact-signature=pass mismatch=reject execution-breakpoint=owner-thread-same-call clear=restores-original" -or
    $elfResult -notcontains "ISOMR_AARCH64_CODE_SUBSTITUTION default=off install=pre-execution root-or-contained-module=pass signature=exact-two-instructions handler=sealed-import-svc-ret call=pass mismatch=reject" -or
    $elfResult -notcontains "ISOMR_CANDY_VORBIS_HOST_MIRROR default=off substitutions=10 source=pinned-memory-or-aasset-provider-bounded-copy counters=zero configuration=pass" -or
    $elfResult -notcontains "ISOMR_CANDY_WEBP_103_HOST default=off exact-version=0x010003 substitutions=1 copy=full-output-seeded-padding-preserved refusal=null,overlap,decode-failure counters=pass addresses=none" -or
    $elfResult -notcontains "ISOMR_CANDY_RAPIDJSON_HOST_MIRROR default=off parser-value=rapidjson-1.1.0 allocator=legacy-five-field nodes=all-json-types cleanup=pass addresses=none" -or
    $elfResult -notcontains "ISOMR_AARCH64_PROFILE reject=4 order=pass export=pass jni-order=pass cleanup=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_PROFILE_WORKER_EXECUTION_SLICE selected-owner-session=configured unselected=unsliced results=control-equal cleanup=pass" -or
    $elfResult -notcontains "ISOMR_AARCH64_PROFILE unimplemented=named-fail-closed" -or
    $elfResult -notcontains "ISOMR_NATIVE_AARCH64_PROFILE=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_FILES imports=30 mounts=/app-readonly,/data-writable cwd=/app-bounded relative=cwd-contained writable=open-write-pwrite-truncate-sync-reopen open-open64-__open_2-read-pread-seek=pass urandom=win32-csprng-read-only fcntl-fcntl64=setlk-getlk-contention-split-convert-whole-unlock stat-lstat=no-follow-handles statfs=win32-final-handle-volume-blocks-flags mkdir=parent-handle-contained mode=0777 errno=17,13" -or
    $elfResult -notcontains "ISOMR_BIONIC_FILES directory=opendir-readdir-closedir handle-enumeration-contained dirent64=280 stable-per-stream-interleaved utf8-types=pass eof-errno-preserved reject=2,9,13,14,20 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_FILES remove=file-empty-directory-handle-contained unlink=file-directory21 readonly=13 missing=2 nonempty=39 address=14" -or
    $elfResult -notcontains "ISOMR_BIONIC_FILES stat=128 size=10 mode=regular aliases=4" -or
    $elfResult -notcontains "ISOMR_BIONIC_FILES containment=pass flags=readonly errno=2,9,13,14" -or
    $elfResult -notcontains "ISOMR_BIONIC_FILES cleanup=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_FILES=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_ASSETS manager=guest-opaque from-java=jni-provenance stable=pass open=/app/assets-contained root-file=/data-contained empty=miss mode=0-3 length64=checked remaining64=initial-read-seek-eof-invalid-closed read=bounded seek64=set-cur-end-bounded-preserve buffer=stable-owned local-copy=owned-bounded-preserve-cursor unsafe=null asset=guest-opaque close=owned cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_LOG print=aarch64-variadic bounded-format tag=guest-copy sink=stderr return=length provenance=checked" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI tables=vm-8 env-233 reserved-null=pass per-thread=distinct" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI methods=GetEnv-GetVersion-FindClass-NewGlobalRef-DeleteGlobalRef-DeleteLocalRef-IsSameObject-NewLocalRef-GetObjectClass-NewObjectV-NewString-GetStringLength-GetStringChars-ReleaseStringChars-NewStringUTF-GetStringUTFLength-GetStringUTFChars-ReleaseStringUTFChars-GetArrayLength-NewByteArray-GetByteArrayElements-ReleaseByteArrayElements-GetByteArrayRegion-SetByteArrayRegion-GetMethodID-CallObjectMethodV-CallBooleanMethodV-CallFloatMethodV-CallVoidMethodV-GetStaticMethodID-CallStaticObjectMethodV-CallStaticBooleanMethodV-CallStaticIntMethodV-CallStaticLongMethodV-CallStaticVoidMethodV-GetStaticFieldID-GetStaticObjectField-GetStaticIntField-RegisterNatives-UnregisterNatives-GetJavaVM-ExceptionOccurred-ExceptionCheck-ExceptionClear-FatalErrorDiagnostic" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI reflected=FromReflectedMethod-slot7-ToReflectedMethod-slot9 roundtrip=pass static-provenance=checked instance-fields=GetFieldID-slot94-GetObjectField-slot95-GetIntField-slot100 PackageInfo.versionName=1.2.3-versionCode=0 provenance=checked CallIntMethodV=slot50 String.length=5 provenance=checked cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI RegisterNatives=slot215 lp64-array=24-byte transactional=replace-generation duplicate-invalid-no-partial class-ref-independent opaque-handle UnregisterNatives=slot216 class-scoped-idempotent-stale-removed cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallVoidMethodV=slot62 Thread.setName=current-win32-thread-utf8 HapticManager.external-display-haptics=boolean-state,vibrate=jint-reject-then-count Activity.keep-screen-on=boolean-state,startup-loading-finished=observed-lifecycle-state input-map-provider=initialized AppNotRespondingTaskProcessor.scheduleTaskOnUiThread=validated-local-request-no-android-looper,setActive=boolean-state AdsPlatformAbstraction=setActivity-identity-clear,initNetworkListener-local-pointer-state,uninit=local-state-cleared AdsHttp.resetHttpInstance=validated-local-request-no-transport GoogleBillingLibGlue.connect=requested-no-service,disconnect=cleared-no-service FacebookSdkWrapper.tearDown=validated-null-or-correlation-no-sdk Braze.configure=validated-local-state-no-transport,disconnect=local-state-cleared NotificationTokenProvider.disconnect=local-state-recorded-no-transport push-and-local-notifications.cancelAllNotifications=separate-local-requests-no-transport push-notifications.disconnect=local-state-cleared local-notifications.disconnect=local-state-cleared LocalNotification.schedule=validated-metadata-no-transport DeviceImpl=start-stop-screen-info-state PlatformBridge.removeSplashScreen=guest-ready-state CloudStorage.ClearKey=neutral-key-receiver-provenance HttpManager.addHeader=local-header-state HttpManager.executeTask=execution-requested-no-network HttpManager.cancelTask=local-cancellation-id-no-network HttpManager.shutDown=local-state-cleared-no-network observation=bounded-http-api-only-owner-thread-ring provenance=checked reject=nonboolean,activity,pointer" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallFloatMethodV=slot56 table=exact AudioSessionHandler.GetHardwareOutputVolume=win32-core-audio-range-s0-abi receiver-method-va-list-provenance=checked named-fail-closed" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallStaticBooleanMethodV=slot118 Device.isTablet=false DeviceImpl.isInternetReachableCached=false-no-network-backend Network.isInternetReachable=false-no-network-backend GameLib.isInMultiWindowMode=false GooglePlayServices.isAvailable=false AdvertisingId.received=true provenance=checked reject=backend" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallStaticLongMethodV=slot133 AppInstallTimeProvider.install-update=unavailable-zero Activity-provenance=checked Debug.getPss=0-kb provenance=checked reject=backend fail-closed=unknown" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallStaticObjectMethodV=AndroidCallstackUtils.getPackageVersionName-context string=1.2.3 DefaultFileSystem.getDefaultAppDirectory-activity=/data-guest-path Braze.ForcedIGPOrientation.valueOf=validated-owned-enum provenance=checked reject=backend,context,activity" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallStaticVoidMethodV=slot142 UncaughtExceptionWriter.enable-neutral-setBuildId-state=pass GameLib.orientation=update-init-4bool-state GameLib.setupPlatform=5int-state FFBaseSdk.init=neutral-state AdvertisingId=opaque-register-neutral-request AnalyticsReceiver=validated-local-request FirebaseAnalytics=local-init-collection-disabled-disconnect-cleared ReferrerProvider=validated-local-request-clear-state WebViewHelper=validated-user-agent-request checked-diagnostic=pass reject=backend,nonbool" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI NewObjectV=slot29 identity=pass provenance=pass reject=8" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI NewLocalRef=slot25 object-class-null identity=pass reject=2 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI NewString=slot163 utf16=pass empty=pass reject=4 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI byte-array=slots171,176,184,192,200,208 zero-init=pass regions=pass elements=copy-commit-abort global-identity=pass reject=4 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI NewStringUTF=slot167 empty=pass identity=pass provenance=pass reject=3" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI string-chars=slots164-166 length=5 utf16=pass identity=pass reject=9 utf-chars=slots168-170 canonical-modified-utf8 length=12 empty=pass identity=pass cross-api=reject cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallObjectMethodV=loadClass-string-class identity=pass reject=4" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallObjectMethodV=intent-string-array neutral-null=pass reject=2 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallObjectMethodV=getSystemService-audio AudioManager=pass reject=2 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallObjectMethodV=getProperty-output-audio rate=48000 frames=256 reject=1 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallObjectMethodV=CloudStorage.Load fresh-null key-receiver-provenance=checked cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI CallObjectMethodV=HttpManager.createTask local-task-state-no-network args=J,I,String,I,Z,HttpHeaders,byte-array provenance=checked reject=boolean,body cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI ExceptionOccurred=slot15 none=pass reject=1" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI static-field=slots144,145,150 SDK_INT=35 BUILD_STRINGS=20-stable AUDIO_SERVICE=audio OUTPUT_AUDIO_PROPERTIES=strings provenance=pass reject=10 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_GUEST_JNI reject=version-unimplemented cleanup=pass" -or
    $elfResult -notcontains "ISOMR_NATIVE_GUEST_JNI=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_PROCESS getpid=win32-process-id uid-euid-gid-egid=app10000 strerror-r=bounded-known-unknown-erange34 getpagesize=4096 getauxval=hwcap0-pagesize4096-clktck100 unknown=errno2 sysconf=pagesize4096-clktck100-physical-pages-win32 unknown=errno22 syscall178=gettid syscall270=self-process-vm-readv-bounded getenv=controlled-empty host-hidden" -or
    $elfResult -notcontains "ISOMR_BIONIC_MAPPINGS mmap=anonymous-private-rw-page-aligned-zero,shared-file-read-close-fd-reuse-writeback-owned,read-only-shared-protection-write-does-not-invent-writeback,locked-shared-process-exit-unlock-writeback,prot-none-536866816-sparse mprotect=read-only-enforced-rw-restored-partial-content-preserved madvise=normal-anonymous-dontneed-zero-file-dontneed-reject-unchanged munmap=partial-holes-pass committed=page-accounted reject=errno22 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_SIGNALS sigaltstack=per-thread-enable-query-disable sigemptyset=zero sigfillset=all64 sigaction=guest-registry-query pthread_kill=esrch-probe-enotsup-delivery reject=errno14,22 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_PROPERTIES known=sdk35-arm64 unknown=empty bounds=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_LOCALES new-free-use mbtowc-wcrtomb=utf8 mb-cur-max=4 per-thread=pass cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_ATEXIT reverse-order=pass invalid=reject process-exit-discard=pending-and-new" -or
    $elfResult -notcontains "ISOMR_BIONIC_MATH imports=39 fp-vector-abi sincosf=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_NUMBERS imports=11 strtol-strtoll_l-endptr=pass locale-provenance=checked fp-vector=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_RANDOM rand-srand range=31bit repeat=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_FORMAT imports=4 snprintf=aarch64-x3-x7-v0-stack+0 full-truncate-size-query=pass vsnprintf=aarch64-va-list truncation=pass __vsprintf_chk=aarch64-x0-x4-va-list-fortified-fit-overflow-reject sscanf=aarch64-x2-x7-stack+0,+8 bounded-preflight integer-string-char-percent-n=pass mismatch=0 input-failure=-1" -or
    $elfResult -notcontains "ISOMR_BIONIC_STDIO imports=46 streams=bionic-sF[3]-stdin-stdout-stderr fopen=modes-rwa-plus-suffixes fread-fseek-ftell-fclose=pass extended=fseeko-ftello-fgetc-ungetc-fgets-positions-fileno-fdopen-locks fflush=pass fwrite=zero-length nonempty=fail-closed reject=errno9,22 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_DYNAMIC_LOADER flags=lazy-now scopes=global,module,host export=guest import=cell,data handles=opaque errors=per-thread-clear reject=7 cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_DYNAMIC_LOADER dynamic-sibling=configured-only transactional-rollback-address-reuse initializer=once-same-thread-reentry unlocked quarantine=failed dlsym=visibility-checked process-exit-discard=unique-handle-all-references-active-work-rejected cleanup=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_RUNTIME imports=980 components=27 sealed=pass" -or
    $elfResult -notcontains "ISOMR_BIONIC_RUNTIME heap-files-time-thread-sync-tls-jumps-jni-gles-opensl=shared" -or
    $elfResult -notcontains "ISOMR_BIONIC_RUNTIME cleanup=pass main-destructor=reject reinitialize=reject" -or
    $elfResult -notcontains "ISOMR_NATIVE_BIONIC_RUNTIME=pass") {
    throw "Native ELF64 loader evidence is incomplete (exit $elfExitCode)."
}

"ISOMR_NATIVE_ELF64=pass"
"ISOMR_NATIVE_ELF64_GRAPH=pass"

$nativeGlesResult = & $nativeGlesOutput 2>&1
$nativeGlesExitCode = $LASTEXITCODE
$nativeGlesResult | Write-Output
if ($nativeGlesExitCode -ne 0 -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES strings=vendor,renderer,version,shading-language,extensions guest-owned provenance=current-context invalid=null-error" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES state=max-texture-size,max-cube-map-texture-size,max-texture-image-units,max-vertex-attribs,max-varying-vectors,max-vertex-uniform-vectors,max-fragment-uniform-vectors,max-combined-texture-image-units,max-renderbuffer-size,framebuffer-binding guest-write=checked unsupported=fail-closed" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES resources=textures generation=driver active-unit=checked binding=texture2d-owned-per-unit parameters=integer-driver-state upload=image-subimage-alpha-rgb-rgba8-guest-copy-stack9-checked-null-allocation-zero-width-height-no-guest-read delete=zero-unknown-duplicate-owned-bound-all-units renderbuffers=generation-bind-depth16-stencil8-depth24-stencil8-storage-delete-unknown-noop-owned-bound-unbind framebuffers=generation-owned-bind-texture-renderbuffer-status-delete-unknown-noop-owned-bound-unbind viewport=driver-state depth-range=clamped-driver-state clear=color-depth-stencil-fp-abi-mask-checked-driver-state capabilities=enable-disable-driver-state write-masks=color-depth-stencil-driver-state blend=func-separate-equation-separate-driver-state raster=depth-func,cull-face,front-face,scissor-driver-state vertex-attrib-arrays=enable-disable-driver-state vertex-pointers=guest-metadata-checked draw-elements=active-program-attributes-only-inactive-stale-skip-zero-count-no-read-client-indices-attributes-bounded-host-staging-black-baseline-no-normal-readback-explicit-capture-census vertex-constant=2fv,3fv,4fv-guest-copy-driver-state shaders=create-source-compile-status-log-delete-pending-attached-usable-release-on-program-delete programs=create-attach-bind-attrib-link-status-log-reflect-uniform-attrib-locations-use-delete uniforms=1i,1f,1fv,2fv,4f,4fv,matrix4fv-checked-driver-state driver-owned driver-state=verified guest-write=checked rollback=pass ownership=guest-shutdown" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES pixel-store=pack-unpack-alignment-1,2,4,8 state=driver unsupported=fail-closed" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES proc-address=known-guest-cell unknown=null guest-name=checked" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES input=win32-left-pointer ordered=down-move-up move=coalesced inter-slice=dispatch-only-queued-until-call-complete capture=press-release cancel=wm-close-wm-quit close=instance-owned owner-thread=checked request=owner-pass-foreign-postshutdown-reject teardown=foreign-refused-owner-destroyed" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES context=angle-d3d11-hardware-es2 window=hidden-by-default-explicit-show fixed-client=1280x720 pump=nonblocking present=first-lit-sampled-second-after-lit-no-readback-explicit-capture-readback-zero-payload-smoke absence=fail-closed cleanup=pass" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES runtime-shutdown=late-owner-refusal-retry-pass repeat=reject cleanup=pass" -or
    $nativeGlesResult -notcontains "ISOMR_NATIVE_GLES=pass") {
    throw "Native GLES evidence is incomplete (exit $nativeGlesExitCode)."
}

$desktopJniResult = & $desktopJniOutput $jdk.Jvm $probeClasses 2>&1
$desktopJniExitCode = $LASTEXITCODE
$desktopJniResult | Write-Output
if ($desktopJniExitCode -ne 0 -or
    $desktopJniResult -notcontains "ISOMR_GUEST_JNI_DESKTOP class=pass methods=pass void-method=slot62-backend-pass-refusal-pass static-field=refused static-int-field=refused local-ref=refused constructor=refused string=refused string-utf16=refused string-length-chars=refused identity=pass exception-object=refused exception=pass threads=pass cleanup=pass" -or
    $desktopJniResult -notcontains "ISOMR_NATIVE_GUEST_JNI_DESKTOP=pass") {
    throw "Native desktop GuestJni evidence is incomplete (exit $desktopJniExitCode)."
}

$threadResult = & $threadOutput 2>&1
$threadExitCode = $LASTEXITCODE
$threadResult | Write-Output
if ($threadExitCode -ne 0 -or
    $threadResult -notcontains "ISOMR_THREAD workers=2 peak=2 ids=distinct" -or
    $threadResult -notcontains "ISOMR_THREAD tls0=0x3000002000 tls1=0x3100002000" -or
    $threadResult -notcontains "ISOMR_THREAD canary0=0x1111222233334400 canary1=0x1111222233334401" -or
    $threadResult -notcontains "ISOMR_THREAD stack=pass joins=pass" -or
    $threadResult -notcontains "ISOMR_NATIVE_THREADS=pass") {
    throw "Native thread/TLS evidence is incomplete (exit $threadExitCode)."
}
