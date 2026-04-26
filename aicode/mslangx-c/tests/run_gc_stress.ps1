param(
  [Parameter(Mandatory = $true)]
  [string]$Runner,

  [Parameter(Mandatory = $true)]
  [string]$Root,

  [Parameter(Mandatory = $false)]
  [int]$Passes = 2
)

$runnerPath = (Resolve-Path -LiteralPath $Runner).Path
$rootPath = (Resolve-Path -LiteralPath $Root).Path
$scripts = Get-ChildItem -LiteralPath $rootPath -Recurse -Filter '*.ms' -File |
  Sort-Object FullName
$passed = 0
$failed = 0

if ($scripts.Count -eq 0) {
  Write-Host "No .ms scripts found under $rootPath"
  exit 0
}

for ($pass = 1; $pass -le $Passes; $pass += 1) {
  Write-Host "Pass $pass of $Passes"
  foreach ($script in $scripts) {
    $relativePath = $script.FullName.Substring($rootPath.Length + 1)
    $result = & $runnerPath $script.FullName
    $exitCode = $LASTEXITCODE

    if ($result) {
      $result | ForEach-Object { Write-Host $_ }
    }

    if ($exitCode -eq 0) {
      $passed += 1
      Write-Host "PASS $relativePath"
    } else {
      $failed += 1
      Write-Host "FAIL $relativePath (exit $exitCode)"
    }
  }
}

Write-Host "Summary: $passed passed, $failed failed, $($scripts.Count * $Passes) total"
if ($failed -eq 0) {
  exit 0
}

exit 1
