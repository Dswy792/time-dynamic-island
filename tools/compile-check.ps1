<#
.SYNOPSIS
    Compile one or more source files (syntax/semantic check only, no linking).

.DESCRIPTION
    Initialises the MSVC environment with vcvarsall and runs cl.exe /c against
    the given files. Object files are written to builds\check\<Tag> so that
    several agents can compile in parallel without touching the final build.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\compile-check.ps1 -Files src\time_format.cpp,src\theme.cpp -Tag time
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [string]$Tag = "check"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    Write-Host "ERROR: vswhere.exe not found - Visual Studio is not installed." -ForegroundColor Red
    exit 2
}
$vsPath = & $vsWhere -latest -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) {
    Write-Host "ERROR: no Visual Studio installation found." -ForegroundColor Red
    exit 2
}
$vcVarsAll = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcVarsAll)) {
    Write-Host "ERROR: vcvarsall.bat not found under $vsPath" -ForegroundColor Red
    exit 2
}

$envOutput = cmd /c "call `"$vcVarsAll`" x64 >nul && set"
foreach ($line in $envOutput) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:\$($matches[1])" -Value $matches[2] -Force
    }
}

# C++/WinRT headers from the Windows SDK (used by the notification listener).
$cppwinrtInclude = Join-Path $env:WindowsSdkDir ("Include\" + $env:WindowsSDKVersion + "cppwinrt")
if (-not (Test-Path $cppwinrtInclude)) {
    Write-Host "WARNING: C++/WinRT headers not found at $cppwinrtInclude" -ForegroundColor Yellow
}

$objDir = Join-Path "builds\check" $Tag
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$fileList = @()
foreach ($f in $Files) {
    foreach ($piece in ($f -split ',')) {
        if ($piece.Trim().Length -gt 0) { $fileList += $piece.Trim() }
    }
}

Write-Host "Compile check: $($fileList -join ', ')" -ForegroundColor Cyan
cl.exe /c /nologo /EHsc /std:c++17 /W4 /utf-8 /DNOMINMAX /DUNICODE /D_UNICODE /Iinclude /Iresources "/external:I$cppwinrtInclude" /external:W0 /Fo:"$objDir\\" @fileList
$exit = $LASTEXITCODE
if ($exit -ne 0) {
    Write-Host "COMPILE FAILED (exit $exit)" -ForegroundColor Red
} else {
    Write-Host "COMPILE OK" -ForegroundColor Green
}
exit $exit