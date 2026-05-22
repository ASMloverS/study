param(
  [string] $Runner = "build\Release\mslangc.exe",
  [string] $BenchDir = "benchmarks\runtime",
  [int] $Iterations = 5,
  [int] $Warmup = 1
)

$ErrorActionPreference = "Stop"

if ($Iterations -le 0 -or $Warmup -lt 0) {
  Write-Error "iterations must be positive and warmup non-negative"
}
if (-not (Test-Path -LiteralPath $Runner -PathType Leaf)) {
  Write-Error "runner not found: $Runner"
}
if (-not (Test-Path -LiteralPath $BenchDir -PathType Container)) {
  Write-Error "benchmark directory not found: $BenchDir"
}

$scripts = Get-ChildItem -LiteralPath $BenchDir -Filter "*.ms" |
  Sort-Object -Property Name
if ($scripts.Count -eq 0) {
  Write-Error "no benchmarks found in $BenchDir"
}

function Invoke-BenchmarkOnce {
  param(
    [string] $RunnerPath,
    [string] $ScriptPath
  )

  $timer = [System.Diagnostics.Stopwatch]::StartNew()
  & $RunnerPath --no-cache $ScriptPath *> $null
  $exit_code = $LASTEXITCODE
  $timer.Stop()
  if ($exit_code -ne 0) {
    throw "benchmark failed: $ScriptPath"
  }

  return $timer.Elapsed.TotalMilliseconds
}

Write-Output "benchmark,iterations,min_ms,median_ms,mean_ms"
foreach ($script in $scripts) {
  for ($i = 0; $i -lt $Warmup; ++$i) {
    [void] (Invoke-BenchmarkOnce $Runner $script.FullName)
  }

  $samples = @()
  for ($i = 0; $i -lt $Iterations; ++$i) {
    $samples += Invoke-BenchmarkOnce $Runner $script.FullName
  }

  $sorted = $samples | Sort-Object
  $median = $sorted[[int] [Math]::Floor($sorted.Count / 2)]
  $mean = ($samples | Measure-Object -Average).Average
  $min = ($samples | Measure-Object -Minimum).Minimum
  Write-Output ("{0},{1},{2:F3},{3:F3},{4:F3}" -f `
      $script.BaseName, $Iterations, $min, $median, $mean)
}
