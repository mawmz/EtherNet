param([ValidateRange(1, 32)][int]$Jobs = 6)
$ErrorActionPreference = 'Stop'
$modRoot = Split-Path $PSScriptRoot -Parent
$buildRelative = 'build/switch-release'
$buildAbsolute = Join-Path $modRoot $buildRelative
$savedDevkit = $env:DEVKITPRO
$savedMsystem = $env:MSYSTEM
Push-Location $modRoot
try {
    $env:DEVKITPRO = '/opt/devkitpro'
    Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
    $cmake = 'C:/devkitPro/msys2/usr/bin/cmake.exe'
    & $cmake --preset release-bf2 -B $buildRelative
    if ($LASTEXITCODE -ne 0) { throw 'Standalone configure failed.' }
    & $cmake --build $buildRelative -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Standalone compilation failed.' }

    function Read-Artifact([string]$Name, [string]$Magic) {
        $path = Join-Path $buildAbsolute $Name
        $bytes = [IO.File]::ReadAllBytes($path)
        if ($bytes.Length -lt 128) { throw "Empty or truncated artifact: $Name" }
        if ([Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne $Magic) {
            throw "Invalid artifact header: $Name"
        }
        return ,$bytes
    }
    $null = Read-Artifact 'EtherNet.elf' ([string][char]0x7f + 'ELF')
    $null = Read-Artifact 'exefs/subsdk9' 'NSO0'
    $npdm = Read-Artifact 'exefs/main.npdm' 'META'
    $aciOffset = [BitConverter]::ToUInt32($npdm, 0x70)
    if ($aciOffset + 0x18 -gt $npdm.Length -or
        [Text.Encoding]::ASCII.GetString($npdm, $aciOffset, 4) -ne 'ACI0' -or
        [BitConverter]::ToUInt64($npdm, $aciOffset + 0x10) -ne [uint64]0x0100E95004038000) {
        throw 'Invalid BF2 NPDM program ID or access-control header.'
    }
    foreach ($pair in @(@('EtherNet.nso', 'exefs/subsdk9'), @('bf2.npdm', 'exefs/main.npdm'))) {
        $sourceHash = (Get-FileHash -LiteralPath (Join-Path $buildAbsolute $pair[0])).Hash
        $packageHash = (Get-FileHash -LiteralPath (Join-Path $buildAbsolute $pair[1])).Hash
        if ($sourceHash -ne $packageHash) { throw "Stale package copy: $($pair[1])" }
    }
    $ninja = Get-Content -Raw -LiteralPath (Join-Path $buildAbsolute 'build.ninja')
    if ($ninja -match 'other stuff[/\\](xc2-multiplayer|xenomods)[/\\]') {
        throw 'Build still depends on an old checkout.'
    }
    if ($ninja -match '(UtilityMenu|InputBuffer|EnemyTargetInput|CameraTools|DebugStuff|PluginManager)\.cpp') {
        throw 'Unrelated inherited gameplay module unexpectedly included in EtherNet.'
    }
    $undefined = & C:/devkitPro/devkitA64/bin/aarch64-none-elf-nm.exe -u -C (Join-Path $buildAbsolute 'EtherNet.elf')
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect ELF imports.' }
    if ($undefined -match 'ethernet::|xenomods::|skylaunch::|imgui_xeno_|InputHelper::') {
        throw 'Unresolved mod-internal symbols in standalone ELF.'
    }
    Write-Host "PASS: EtherNet BF2 build, ELF/NSO/NPDM, package hashes, isolated sources."
    Write-Host "Artifacts: $buildAbsolute\exefs"
    Write-Host 'Switch rendering, navigation, and logging are not validated by this script.'
} finally {
    $env:DEVKITPRO = $savedDevkit
    $env:MSYSTEM = $savedMsystem
    Pop-Location
}
