param(
  [Parameter(Mandatory = $true)]
  [string]$CTest,

  [Parameter(Mandatory = $true)]
  [string]$BuildDir,

  [Parameter(Mandatory = $false)]
  [string]$Configuration = "Debug"
)

$ctestPath = (Resolve-Path -LiteralPath $CTest).Path
$buildDirPath = (Resolve-Path -LiteralPath $BuildDir).Path

& $ctestPath --test-dir $buildDirPath -C $Configuration --output-on-failure -R 'runtime_core|modules\.'
exit $LASTEXITCODE
