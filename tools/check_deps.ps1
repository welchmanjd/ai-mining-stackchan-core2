$ErrorActionPreference = "Stop"

function Check-IncludeRule {
  param(
    [string]$Label,
    [string]$Path,
    [string]$Pattern,
    [string[]]$RgArgs = @()
  )
  $rgAll = @("-n") + $RgArgs + @($Pattern, $Path)
  $matches = rg @rgAll
  if ($LASTEXITCODE -eq 0) {
    Write-Host "Dependency rule violation: $Label"
    $matches | ForEach-Object { "  $_" } | Write-Host
    $script:failed = $true
    return
  }
  if ($LASTEXITCODE -eq 2) {
    throw "rg failed while checking $Label"
  }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$failed = $false

Push-Location $repoRoot
try {
  # config/utils should not depend on other project modules
  Check-IncludeRule "config -> (core|ai|ui|behavior|audio)" "src/config" '#include\s+"(core|ai|ui|behavior|audio)/'
  Check-IncludeRule "utils -> (core|ai|ui|behavior|audio)" "src/utils" '#include\s+"(core|ai|ui|behavior|audio)/'

  # ui can depend on config/utils and ai (DTO only), but not core/behavior/audio
  Check-IncludeRule "ui -> (core|behavior|audio)" "src/ui" '#include\s+"(core|behavior|audio)/'

  # ai can depend on audio/config/utils; ui is allowed only for ui/ui_types.h
  Check-IncludeRule "ai -> (core|behavior)" "src/ai" '#include\s+"(core|behavior)/'
  Check-IncludeRule "ai -> ui (except ui_types.h)" "src/ai" '#include\s+"ui/(?!ui_types\.h)' @("-P")

  # behavior can depend on config/utils and ai (state only), but not core/ui/audio
  Check-IncludeRule "behavior -> (core|ui|audio)" "src/behavior" '#include\s+"(core|ui|audio)/'
} finally {
  Pop-Location
}

if ($failed) {
  Write-Host ""
  Write-Host "Dependency check failed."
  exit 1
}

Write-Host "Dependency check passed."
