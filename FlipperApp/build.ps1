$ErrorActionPreference = "Stop"
$ufbtHome = if ($env:UFBT_HOME) { $env:UFBT_HOME } else { Join-Path $env:USERPROFILE ".ufbt" }
$oldUfbT = $env:UFBT_HOME
$oldToolchainPath = $env:FBT_TOOLCHAIN_PATH
$oldPythonPath = $env:PYTHONPATH
$env:UFBT_HOME = $ufbtHome

if (-not $env:FBT_TOOLCHAIN_PATH) {
    if (Test-Path -LiteralPath (Join-Path $ufbtHome "toolchain\x86_64-windows")) {
        $env:FBT_TOOLCHAIN_PATH = $ufbtHome
    } elseif (Test-Path -LiteralPath (Join-Path $ufbtHome "current\toolchain\x86_64-windows")) {
        $env:FBT_TOOLCHAIN_PATH = Join-Path $ufbtHome "current"
    }
}

Push-Location $PSScriptRoot
try {
    $ufbtCommand = Get-Command ufbt -ErrorAction SilentlyContinue
    if ($ufbtCommand) {
        & $ufbtCommand.Source build
    } else {
        $localUfbt = Join-Path $PSScriptRoot ".tooling\bin\ufbt.exe"
        if (-not (Test-Path -LiteralPath $localUfbt)) { throw "uFBT is not installed or bundled" }
        $localPackageRoot = Join-Path $PSScriptRoot ".tooling"
        $env:PYTHONPATH = if ($oldPythonPath) { "$localPackageRoot;$oldPythonPath" } else { $localPackageRoot }
        & $localUfbt build
    }
    if ($LASTEXITCODE -ne 0) { throw "uFBT build failed" }

    $builtFap = Join-Path $ufbtHome "build\switch_macro.fap"
    $distDir = Join-Path $PSScriptRoot "dist"
    New-Item -ItemType Directory -Force -Path $distDir | Out-Null
    Copy-Item -LiteralPath $builtFap -Destination (Join-Path $distDir "SwitchMacro.fap") -Force
    Write-Host "Built $(Join-Path $distDir 'SwitchMacro.fap')"
} finally {
    Pop-Location
    $env:UFBT_HOME = $oldUfbT
    $env:FBT_TOOLCHAIN_PATH = $oldToolchainPath
    $env:PYTHONPATH = $oldPythonPath
}
