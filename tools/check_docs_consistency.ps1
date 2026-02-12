$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$failed = $false

function Fail([string]$msg) {
  Write-Host $msg
  $script:failed = $true
}

function Parse-DocCommands([string]$docText) {
  $cmds = New-Object System.Collections.Generic.List[string]
  [regex]::Matches($docText, '(?m)^###\s+([A-Z][A-Z0-9 ]+)$') | ForEach-Object {
    $name = $_.Groups[1].Value.Trim().ToUpperInvariant()
    if ($name -ne "COMMANDS" -and $name -ne "ERROR HANDLING" -and $name -ne "IMPLEMENTATION REFERENCE" -and $name -ne "CONNECTION") {
      $cmds.Add($name)
    }
  }
  return $cmds | Sort-Object -Unique
}

function Parse-ImplCommands([string]$implText) {
  $cmds = New-Object System.Collections.Generic.List[string]
  [regex]::Matches($implText, 'cmd\.equalsIgnoreCase\("([^"]+)"\)') | ForEach-Object {
    $cmds.Add($_.Groups[1].Value.Trim().ToUpperInvariant())
  }
  [regex]::Matches($implText, 'cmd\.startsWith\("([^"]+)"\)') | ForEach-Object {
    $raw = $_.Groups[1].Value.Trim().ToUpperInvariant()
    if ($raw.EndsWith(" ")) {
      $raw = $raw.Substring(0, $raw.Length - 1)
    }
    $cmds.Add($raw)
  }
  return $cmds | Sort-Object -Unique
}

Push-Location $repoRoot
try {
  $docPath = "docs/serial_setup.md"
  $implPath = "src/core/serial_setup.cpp"
  $docText = Get-Content -Raw $docPath
  $implText = Get-Content -Raw $implPath

  $docCommands = Parse-DocCommands $docText
  $implCommands = Parse-ImplCommands $implText
  if ($docCommands.Count -eq 0 -or $implCommands.Count -eq 0) {
    Fail "Docs consistency: failed to parse command lists"
  } else {
    $docSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $implSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($c in $docCommands) { [void]$docSet.Add($c) }
    foreach ($c in $implCommands) { [void]$implSet.Add($c) }

    $missingInDoc = @()
    foreach ($c in $implSet) {
      if (-not $docSet.Contains($c)) { $missingInDoc += $c }
    }
    $missingInImpl = @()
    foreach ($c in $docSet) {
      if (-not $implSet.Contains($c)) { $missingInImpl += $c }
    }
    if ($missingInDoc.Count -gt 0) {
      Fail ("Docs consistency: serial command(s) missing in docs: " + ($missingInDoc -join ", "))
    }
    if ($missingInImpl.Count -gt 0) {
      Fail ("Docs consistency: serial command(s) missing in implementation: " + ($missingInImpl -join ", "))
    }
  }

  $docFiles = @("README.md", "architecture.md", "docs/serial_setup.md")
  $paths = New-Object System.Collections.Generic.List[string]
  foreach ($file in $docFiles) {
    $text = Get-Content -Raw $file
    [regex]::Matches($text, '`([^`]*src/[A-Za-z0-9_\-./*]+[^`]*)`') | ForEach-Object {
      $raw = $_.Groups[1].Value
      [regex]::Matches($raw, 'src/[A-Za-z0-9_\-./*]+') | ForEach-Object {
        $paths.Add($_.Value)
      }
    }
    [regex]::Matches($text, '(?m)^\s*-\s+(core|ai|audio|ui|behavior|config|utils)/[A-Za-z0-9_\-./]+\.[A-Za-z0-9_]+') | ForEach-Object {
      $p = $_.Value.Trim()
      $p = $p -replace '^\-\s*', ''
      $paths.Add(("src/" + $p))
    }
  }

  $uniq = $paths | Sort-Object -Unique
  foreach ($p in $uniq) {
    if ($p.Contains("*")) {
      if (-not (Test-Path -Path $p)) {
        Fail "Docs consistency: missing path referenced by docs: $p"
      }
      continue
    }
    if (-not (Test-Path -LiteralPath $p)) {
      Fail "Docs consistency: missing path referenced by docs: $p"
    }
  }
} finally {
  Pop-Location
}

if ($failed) {
  Write-Host ""
  Write-Host "Docs consistency check failed."
  exit 1
}

Write-Host "Docs consistency check passed."
