param([switch]$App, [switch]$StageGame, [switch]$Fresh)
$ErrorActionPreference = 'Stop'
$projectRoot = (Split-Path -Parent $PSScriptRoot).Replace('\', '/')
Set-Location -LiteralPath $projectRoot
$zigPath = Join-Path $projectRoot '.tools/zig-x86_64-windows-0.14.1/zig.exe'
$cmakePath = Join-Path $projectRoot '.tools/cmake-3.31.8-windows-x86_64/bin/cmake.exe'
$ninjaPath = Join-Path $projectRoot '.tools/ninja/ninja.exe'
if (-not (Test-Path -LiteralPath $zigPath) -or -not (Test-Path -LiteralPath $cmakePath) -or -not (Test-Path -LiteralPath $ninjaPath)) {
    throw 'Portable tools missing. Run python tools/bootstrap_windows.py first.'
}
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.tools/zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $projectRoot '.tools/zig-local'
foreach ($tool in @('ar', 'ranlib', 'rc')) {
    $wrapper = Join-Path $projectRoot ".tools/zig-$tool.cmd"
    Set-Content -LiteralPath $wrapper -Encoding Ascii -Value "@echo off`r`n`"%~dp0zig-x86_64-windows-0.14.1\zig.exe`" $tool %*"
}
$buildPath = if ($App) { 'build/windows-app' } else { 'build/windows-core' }
$appOption = if ($App) { 'ON' } else { 'OFF' }
$options = @('-S', '.', '-B', $buildPath, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninjaPath",
    "-DCMAKE_C_COMPILER=$zigPath", '-DCMAKE_C_COMPILER_ARG1=cc',
    "-DCMAKE_CXX_COMPILER=$zigPath", '-DCMAKE_CXX_COMPILER_ARG1=c++',
    "-DCMAKE_AR:FILEPATH=$projectRoot/.tools/zig-ar.cmd",
    "-DCMAKE_C_COMPILER_AR:FILEPATH=$projectRoot/.tools/zig-ar.cmd",
    "-DCMAKE_CXX_COMPILER_AR:FILEPATH=$projectRoot/.tools/zig-ar.cmd",
    "-DCMAKE_RANLIB:FILEPATH=$projectRoot/.tools/zig-ranlib.cmd",
    "-DCMAKE_RC_COMPILER:FILEPATH=$projectRoot/.tools/zig-rc.cmd",
    '-DCMAKE_BUILD_TYPE=Debug', "-DWOLF_BUILD_APP=$appOption")
if ($Fresh) { $options += '--fresh' }
if ($StageGame) {
    python tools/prepare_game.py --stage
    if ($LASTEXITCODE -ne 0) { throw 'Game conversion failed' }
    $options += "-DWOLF_GAME_DIR=$(Join-Path $projectRoot 'build/game')"
}
& $cmakePath @options
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmakePath --build $buildPath --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
& (Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe') --test-dir $buildPath --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }
