param(
  [Parameter(Mandatory = $true)]
  [string]$CTest,

  [Parameter(Mandatory = $true)]
  [string]$BuildDir
)

$ctestPath = (Resolve-Path -LiteralPath $CTest).Path
$buildDirPath = (Resolve-Path -LiteralPath $BuildDir).Path

& $ctestPath --test-dir $buildDirPath -C Debug --output-on-failure -R 'runtime_core|modules\.'
exit $LASTEXITCODE
