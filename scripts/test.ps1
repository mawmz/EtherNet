param([string]$Compiler = 'C:/msys64/mingw64/bin/g++.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!(Test-Path -LiteralPath $Compiler)) { throw "Host C++ compiler not found: $Compiler. Use -Compiler to select MinGW g++." }
$outputRoot = Join-Path $projectRoot 'build/host-tests'
$null = New-Item -ItemType Directory -Force -Path $outputRoot
$oldPath = $env:PATH
$env:PATH = (Split-Path $Compiler -Parent) + ';' + $env:PATH
Push-Location $projectRoot
try {
    foreach ($name in @('shared_framing','field_tether','field_recovery','interaction_lifetime','notification')) {
        $sources = @("tests/${name}_tests.cpp")
        if ($name -in @('shared_framing','field_tether','field_recovery')) {
            $sources += 'src/ethernet/camera/SharedFraming.cpp'
        }
        if ($name -eq 'field_tether') { $sources += 'src/ethernet/tether/FieldTether.cpp' }
        $output = Join-Path $outputRoot "$name.exe"
        & $Compiler -std=c++20 -Iinclude @sources -o $output
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $name" }
        & $output
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $name" }
    }
    Write-Host 'PASS: all five host test suites.'
} finally {
    Pop-Location
    $env:PATH = $oldPath
}
