# Copies the built plugin to the system VST3 folder. Run from an ADMINISTRATOR PowerShell with the DAW closed.
$ErrorActionPreference = 'Stop'
$src  = Join-Path $PSScriptRoot "build\PolySwell_artefacts\Release\VST3\PolySwell II.vst3"
$dest = "C:\Program Files\Common Files\VST3"
if (-not (Test-Path $src)) { throw "Not built yet: run .\build.ps1 PolySwell_VST3 first." }
& (Join-Path $PSScriptRoot "build\Release\engine_test.exe") | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { throw "DSP tests failed - not installing." }
Copy-Item $src $dest -Recurse -Force
Write-Host "Installed PolySwell II to $dest" -ForegroundColor Green
