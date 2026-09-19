$ErrorActionPreference = 'Stop'

$testRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = (Resolve-Path (Join-Path $testRoot '..\..')).Path
$testBuildDir = Join-Path ([System.IO.Path]::GetTempPath()) 'pondsense-host-tests'

New-Item -ItemType Directory -Path $testBuildDir -Force | Out-Null

$k230Test = Join-Path $testBuildDir 'test_k230.exe'
$feedingTest = Join-Path $testBuildDir 'test_feeding.exe'

& gcc -std=c11 -Wall -Wextra -Werror `
  (Join-Path $testRoot 'test_k230.c') `
  (Join-Path $projectRoot 'Middleware\K230\k230.c') `
  -o $k230Test
if ($LASTEXITCODE -ne 0) { throw 'K230 protocol host test compile failed' }
& $k230Test
if ($LASTEXITCODE -ne 0) { throw 'K230 protocol host test failed' }

& gcc -std=c11 -Wall -Wextra -Werror `
  (Join-Path $testRoot 'test_feeding.c') `
  (Join-Path $projectRoot 'Application\PondSense\feeding.c') `
  -o $feedingTest
if ($LASTEXITCODE -ne 0) { throw 'feed sequence host test compile failed' }
& $feedingTest
if ($LASTEXITCODE -ne 0) { throw 'feed sequence host test failed' }

Write-Output 'Critical host tests passed.'
