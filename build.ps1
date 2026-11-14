# Configure (once) and build one or more targets of PolySwell II.  .\build.ps1 engine_test ui_snapshot PolySwell_VST3
# Reuses the JUCE sources that gen 1 (..\PolySwell) already downloaded, when they are there.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Targets = @('PolySwell_VST3'))
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $cmake = & $vswhere -latest -products * -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
}
if (-not $cmake) { throw "CMake not found" }
if (-not (Test-Path build\CMakeCache.txt)) {
    $juce = Join-Path $PSScriptRoot "..\PolySwell\build\_deps\juce-src"
    $extra = @()
    if (Test-Path "$juce\CMakeLists.txt") { $extra = @("-DFETCHCONTENT_SOURCE_DIR_JUCE=$((Resolve-Path $juce).Path)") }
    & $cmake -B build -G "Visual Studio 17 2022" @extra
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
}
foreach ($t in $Targets) {
    & $cmake --build build --config Release --target $t -- /v:m /nologo
    if ($LASTEXITCODE -ne 0) { throw "build of $t failed" }
}
