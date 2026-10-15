param([string[]] $Cases = @('vr_response', 'filter_response', 'display_500', 'display_14000',
  'all_steps', 'stop_restart', 'same_rpm', 'boundary_high', 'boundary_low', 'boundary_down',
  'vr_monotonic', 'hysteresis', 'millis_wrap', 'timer_config', 'display_range'))
$ErrorActionPreference = 'Stop'
$testRoot = $PSScriptRoot
$source = Join-Path $testRoot 'regression.cpp'
$sketch = Join-Path (Split-Path $testRoot) '115ATTiny85RPMGenerator.ino'
$buildRoot = Join-Path $testRoot 'build'
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$compiler = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw "MSVC compiler not found: $compiler" }
$defines = '/DF_CPU=16000000UL /D__AVR_ATtiny85__=1'
if ((Get-Content -LiteralPath $sketch -Raw) -match 'ISR\s*\(\s*TIMER1_COMPA_vect') { $defines += ' /DHOST_HAS_TIMER1_ISR=1' }
if ((Get-Content -LiteralPath $sketch -Raw) -match '\bselectVRStep\s*\(') { $defines += ' /DHOST_HAS_SELECT_VR_STEP=1' }
$batchFile = Join-Path $buildRoot 'compile.cmd'
$exe = Join-Path $buildRoot 'regression.exe'
$objectFile = Join-Path $buildRoot 'regression.obj'
@"
@echo off
call "$vcvars" >nul
"$compiler" /nologo /EHsc /std:c++17 /O2 /W4 /utf-8 $defines /I"$testRoot\stubs" "$source" /Fo"$objectFile" /Fe"$exe"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batchFile -Encoding ASCII
& $env:ComSpec /d /c $batchFile
if ($LASTEXITCODE -ne 0) { throw "Host compilation failed ($LASTEXITCODE)" }
$failed = 0
foreach ($case in $Cases) {
  & $exe $case
  if ($LASTEXITCODE -ne 0) { ++$failed }
}
Write-Output "$($Cases.Count - $failed)/$($Cases.Count) cases passed"
if ($failed -gt 0) { exit 1 }
