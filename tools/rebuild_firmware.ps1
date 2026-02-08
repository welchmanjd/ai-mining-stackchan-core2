param(
    [string]$Env = 'm5stack-core2',
    [string]$SetupRepo = 'C:\Users\welch\Documents\PlatformIO\Projects\ai-mining-stackchan-setup\Resources\firmware'
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $SkipBuild) {
    Write-Host "Building firmware for env '$Env'..."
    Push-Location $repoRoot
    try {
        pio run -e $Env
        if ($LASTEXITCODE -ne 0) {
            throw "pio run failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

$buildDir = Join-Path $repoRoot ".pio\\build\\$Env"
if (-not (Test-Path $buildDir)) {
    throw "Build output not found: $buildDir"
}

$appBin = Join-Path $buildDir 'firmware.bin'
$bootloaderBin = Join-Path $buildDir 'bootloader.bin'
$partitionsBin = Join-Path $buildDir 'partitions.bin'
$bootApp0Bin = Join-Path $buildDir 'boot_app0.bin'

if (-not (Test-Path $appBin)) { throw "Missing firmware.bin in $buildDir" }
if (-not (Test-Path $bootloaderBin)) { throw "Missing bootloader.bin in $buildDir" }
if (-not (Test-Path $partitionsBin)) { throw "Missing partitions.bin in $buildDir" }

$pioHome = $env:PLATFORMIO_HOME_DIR
if ([string]::IsNullOrWhiteSpace($pioHome)) {
    $pioHome = Join-Path $env:USERPROFILE '.platformio'
}
$pythonPath = Join-Path $pioHome 'penv\\Scripts\\python.exe'
$esptoolPy = Join-Path $pioHome 'packages\\tool-esptoolpy\\esptool.py'

if (-not (Test-Path $pythonPath)) { throw "PlatformIO python not found: $pythonPath" }
if (-not (Test-Path $esptoolPy)) { throw "esptool.py not found: $esptoolPy" }

$mergedPath = Join-Path $buildDir 'stackchan_core2_merged.bin'

$mergeArgs = @(
    "`"$esptoolPy`"",
    "--chip", "esp32",
    "merge_bin",
    "-o", "`"$mergedPath`"",
    "0x1000", "`"$bootloaderBin`"",
    "0x8000", "`"$partitionsBin`"",
    "0x10000", "`"$appBin`""
)

if (Test-Path $bootApp0Bin) {
    $mergeArgs += @("0xe000", "`"$bootApp0Bin`"")
}

Write-Host "Merging binaries..."
& $pythonPath $mergeArgs
if ($LASTEXITCODE -ne 0) {
    throw "esptool.py merge_bin failed with exit code $LASTEXITCODE"
}

$firmwarePath = $mergedPath

$destMain = Join-Path $SetupRepo 'stackchan_core2.bin'
New-Item -ItemType Directory -Force -Path $SetupRepo | Out-Null
Copy-Item -Path $firmwarePath -Destination $destMain -Force
$(Get-Item -Path $destMain).LastWriteTime = Get-Date
Write-Host "Copied: $destMain"

Write-Host "Done."
