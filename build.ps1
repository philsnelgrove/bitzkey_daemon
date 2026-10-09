# Configures and builds the daemon (Release) on Windows, for developing it, then runs the tests.
# Run from anywhere:  .\build.ps1
# Uses the CMake that ships with Visual Studio Build Tools when none is on PATH.
Set-Location $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft Visual Studio", "$env:ProgramFiles\Microsoft Visual Studio" `
        -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cmake) { Write-Error "CMake not found. Install Visual Studio Build Tools with the C++ workload."; exit 1 }

& $cmake --preset windows
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build --preset release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot "build\Release\bitzkey_tests.exe")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built $PSScriptRoot\build\Release\bitzkey_daemon.exe"
