function Resolve-ArmrNativeJdk {
    param([string]$Requested)

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        $candidates.Add($Requested)
    } elseif (-not [string]::IsNullOrWhiteSpace($env:JAVA_HOME)) {
        $candidates.Add($env:JAVA_HOME)
    }
    $javacCommand = Get-Command javac.exe -ErrorAction SilentlyContinue
    if ($null -ne $javacCommand) {
        $candidates.Add((Split-Path -Parent (Split-Path -Parent $javacCommand.Source)))
    }
    foreach ($root in @(
            "C:\Program Files\Eclipse Adoptium",
            "${env:ProgramFiles(x86)}\Eclipse Adoptium")) {
        if (-not [string]::IsNullOrWhiteSpace($root) -and
            (Test-Path -LiteralPath $root -PathType Container)) {
            foreach ($candidate in (Get-ChildItem -LiteralPath $root -Directory |
                    Sort-Object Name -Descending)) {
                $candidates.Add($candidate.FullName)
            }
        }
    }

    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        $javac = Join-Path $candidate "bin\javac.exe"
        $jvm = Join-Path $candidate "bin\server\jvm.dll"
        $jni = Join-Path $candidate "include\jni.h"
        $jniMd = Join-Path $candidate "include\win32\jni_md.h"
        if ((Test-Path -LiteralPath $javac -PathType Leaf) -and
            (Test-Path -LiteralPath $jvm -PathType Leaf) -and
            (Test-Path -LiteralPath $jni -PathType Leaf) -and
            (Test-Path -LiteralPath $jniMd -PathType Leaf)) {
            return [pscustomobject]@{
                Home = (Resolve-Path -LiteralPath $candidate).Path.TrimEnd(
                    [IO.Path]::DirectorySeparatorChar,
                    [IO.Path]::AltDirectorySeparatorChar)
                Javac = (Resolve-Path -LiteralPath $javac).Path
                Jvm = (Resolve-Path -LiteralPath $jvm).Path
            }
        }
    }
    throw "A Windows JDK with javac.exe, jni.h, and bin\server\jvm.dll was not found. Set JAVA_HOME or pass -JavaHome."
}
