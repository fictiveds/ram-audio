[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$BuildType = "Release",
    [string]$BuildDir,
    [string]$OutDir,
    [string]$ToolsDir,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Text)
    Write-Host "[windows] $Text"
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [Parameter(Mandatory = $true)][string[]]$Args
    )

    Write-Step ("> {0} {1}" -f $Exe, ($Args -join " "))
    & $Exe @Args
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Exe"
    }
}

function Download-IfMissing {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (Test-Path $Destination) {
        return
    }

    Write-Step ("download: {0}" -f $Url)

    $curlExe = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -ne $curlExe) {
        & $curlExe.Source -L --fail --output $Destination $Url
        if ($LASTEXITCODE -ne 0) {
            throw "curl download failed (${LASTEXITCODE}): $Url"
        }
        return
    }

    $oldProgress = $ProgressPreference
    try {
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing
    }
    finally {
        $ProgressPreference = $oldProgress
    }
}

function Expand-IfMissing {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$ExpectedPath,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (Test-Path $ExpectedPath) {
        return
    }

    Write-Step ("extract: {0}" -f $ZipPath)

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null

    $tarExe = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($null -ne $tarExe) {
        & $tarExe.Source -xf $ZipPath -C $Destination
        if ($LASTEXITCODE -ne 0) {
            throw "tar extraction failed (${LASTEXITCODE}): $ZipPath"
        }
        return
    }

    $oldProgress = $ProgressPreference
    try {
        $ProgressPreference = "SilentlyContinue"
        Expand-Archive -LiteralPath $ZipPath -DestinationPath $Destination -Force
    }
    finally {
        $ProgressPreference = $oldProgress
    }
}

function Normalize-CMakeInputTimestamps {
    param([Parameter(Mandatory = $true)][string]$SourceRoot)

    $nowUtc = (Get-Date).ToUniversalTime().AddSeconds(-1)
    $updated = 0

    $files = Get-ChildItem -Path $SourceRoot -Recurse -File | Where-Object {
        $_.Name -ieq "CMakeLists.txt" -or $_.Extension -ieq ".cmake"
    }

    foreach ($file in $files) {
        if ($file.LastWriteTimeUtc -gt $nowUtc) {
            $file.LastWriteTimeUtc = $nowUtc
            $updated++
        }
    }

    if ($updated -gt 0) {
        Write-Step ("normalized future timestamps for {0} CMake file(s)" -f $updated)
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = (Resolve-Path (Join-Path $scriptDir "..")).Path

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $rootDir ("build\windows-{0}" -f $BuildType.ToLowerInvariant())
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $rootDir "dist\windows"
}
if ([string]::IsNullOrWhiteSpace($ToolsDir)) {
    $ToolsDir = Join-Path $rootDir ".tools\windows"
}

$llvmMingwVersion = "20260407"
$cmakeVersion = "3.31.6"
$ninjaVersion = "1.12.1"

$llvmZipName = "llvm-mingw-$llvmMingwVersion-ucrt-x86_64.zip"
$cmakeZipName = "cmake-$cmakeVersion-windows-x86_64.zip"
$ninjaZipName = "ninja-win.zip"

$llvmUrl = if ($env:LLVM_MINGW_URL) { $env:LLVM_MINGW_URL } else { "https://github.com/mstorsjo/llvm-mingw/releases/download/$llvmMingwVersion/$llvmZipName" }
$cmakeUrl = if ($env:CMAKE_WINDOWS_ZIP_URL) { $env:CMAKE_WINDOWS_ZIP_URL } else { "https://github.com/Kitware/CMake/releases/download/v$cmakeVersion/$cmakeZipName" }
$ninjaUrl = if ($env:NINJA_WINDOWS_ZIP_URL) { $env:NINJA_WINDOWS_ZIP_URL } else { "https://github.com/ninja-build/ninja/releases/download/v$ninjaVersion/$ninjaZipName" }

$llvmZipPath = Join-Path $ToolsDir $llvmZipName
$cmakeZipPath = Join-Path $ToolsDir $cmakeZipName
$ninjaZipPath = Join-Path $ToolsDir ("ninja-{0}.zip" -f $ninjaVersion)

$llvmRoot = Join-Path $ToolsDir ("llvm-mingw-{0}-ucrt-x86_64" -f $llvmMingwVersion)
$cmakeRoot = Join-Path $ToolsDir ("cmake-{0}-windows-x86_64" -f $cmakeVersion)
$ninjaRoot = Join-Path $ToolsDir ("ninja-{0}" -f $ninjaVersion)

$llvmBin = Join-Path $llvmRoot "bin"
$cmakeBin = Join-Path $cmakeRoot "bin"

New-Item -ItemType Directory -Path $ToolsDir -Force | Out-Null
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Download-IfMissing -Url $llvmUrl -Destination $llvmZipPath
Expand-IfMissing -ZipPath $llvmZipPath -ExpectedPath (Join-Path $llvmBin "clang++.exe") -Destination $ToolsDir

Download-IfMissing -Url $cmakeUrl -Destination $cmakeZipPath
Expand-IfMissing -ZipPath $cmakeZipPath -ExpectedPath (Join-Path $cmakeBin "cmake.exe") -Destination $ToolsDir

Download-IfMissing -Url $ninjaUrl -Destination $ninjaZipPath
if (-not (Test-Path (Join-Path $ninjaRoot "ninja.exe"))) {
    New-Item -ItemType Directory -Path $ninjaRoot -Force | Out-Null
    Write-Step ("extract: {0}" -f $ninjaZipPath)

    $tarExe = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($null -ne $tarExe) {
        & $tarExe.Source -xf $ninjaZipPath -C $ninjaRoot
        if ($LASTEXITCODE -ne 0) {
            throw "tar extraction failed (${LASTEXITCODE}): $ninjaZipPath"
        }
    }
    else {
        $oldProgress = $ProgressPreference
        try {
            $ProgressPreference = "SilentlyContinue"
            Expand-Archive -LiteralPath $ninjaZipPath -DestinationPath $ninjaRoot -Force
        }
        finally {
            $ProgressPreference = $oldProgress
        }
    }
}

$cmakeExe = Join-Path $cmakeBin "cmake.exe"
$ctestExe = Join-Path $cmakeBin "ctest.exe"
$ninjaExe = Join-Path $ninjaRoot "ninja.exe"
$objdumpExe = Join-Path $llvmBin "llvm-objdump.exe"

if (-not (Test-Path $cmakeExe)) { throw "cmake.exe not found: $cmakeExe" }
if (-not (Test-Path $ctestExe)) { throw "ctest.exe not found: $ctestExe" }
if (-not (Test-Path $ninjaExe)) { throw "ninja.exe not found: $ninjaExe" }
if (-not (Test-Path (Join-Path $llvmBin "clang++.exe"))) { throw "clang++.exe not found: $(Join-Path $llvmBin "clang++.exe")" }

$env:PATH = "$llvmBin;$cmakeBin;$ninjaRoot;$env:PATH"

Write-Step ("root: {0}" -f $rootDir)
Write-Step ("build dir: {0}" -f $BuildDir)
Write-Step ("out dir: {0}" -f $OutDir)

Normalize-CMakeInputTimestamps -SourceRoot $rootDir

Invoke-Checked -Exe $cmakeExe -Args @(
    "-S", $rootDir,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_CXX_COMPILER=clang++"
)

Invoke-Checked -Exe $cmakeExe -Args @("--build", $BuildDir, "-j")

if (-not $SkipTests) {
    Invoke-Checked -Exe $ctestExe -Args @("--test-dir", $BuildDir, "--output-on-failure")
}

$mainExe = Join-Path $BuildDir "ram_audio.exe"
$testExe = Join-Path $BuildDir "ram_audio_telemetry_test.exe"

if (-not (Test-Path $mainExe)) {
    throw "Build output not found: $mainExe"
}

Copy-Item -Path $mainExe -Destination (Join-Path $OutDir "ram_audio.exe") -Force
if (Test-Path $testExe) {
    Copy-Item -Path $testExe -Destination (Join-Path $OutDir "ram_audio_telemetry_test.exe") -Force
}

$dllCandidates = New-Object System.Collections.Generic.HashSet[string] ([StringComparer]::OrdinalIgnoreCase)
$null = $dllCandidates.Add("libc++.dll")
$null = $dllCandidates.Add("libunwind.dll")
$null = $dllCandidates.Add("libc++abi.dll")
$null = $dllCandidates.Add("libwinpthread-1.dll")
$null = $dllCandidates.Add("libgcc_s_seh-1.dll")

if (Test-Path $objdumpExe) {
    $objdumpOut = & $objdumpExe -p $mainExe
    foreach ($line in $objdumpOut) {
        if ($line -match "DLL Name:\s+(.+)$") {
            $dllName = $Matches[1].Trim()
            if (-not [string]::IsNullOrWhiteSpace($dllName)) {
                $null = $dllCandidates.Add($dllName)
            }
        }
    }
}

foreach ($dllName in $dllCandidates) {
    if ($dllName -match "^(?i)(api-ms-win-.*|kernel32\.dll|ntdll\.dll|user32\.dll|gdi32\.dll|advapi32\.dll|shell32\.dll|ole32\.dll|oleaut32\.dll|ws2_32\.dll|ucrtbase\.dll|msvcp.*\.dll|vcruntime.*\.dll)$") {
        continue
    }

    $candidate = Join-Path $llvmBin $dllName
    if (Test-Path $candidate) {
        Copy-Item -Path $candidate -Destination (Join-Path $OutDir $dllName) -Force
    }
}

Write-Step "done"
Write-Step ("binary: {0}" -f (Join-Path $OutDir "ram_audio.exe"))
