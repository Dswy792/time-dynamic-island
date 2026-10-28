<#
.SYNOPSIS
    Build Dynamic Island with the Microsoft C++ toolchain.

.DESCRIPTION
    Locates Visual Studio with vswhere, compiles the resources, the sources and
    links DynamicIsland.exe. Intermediate files go to builds\obj, the resource
    file to builds\res.

.PARAMETER Clean
    Remove the intermediate build directories before compiling.

.PARAMETER KillRunning
    Stop a running DynamicIsland.exe before linking (the exe is locked while it
    is running).

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Clean
    .\build.ps1 -KillRunning
#>
param(
    [switch]$Clean,
    [switch]$KillRunning
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Build Dynamic Island 5.0" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$buildDir = "builds"
$objDir = Join-Path $buildDir "obj"
$resDir = Join-Path $buildDir "res"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning $buildDir..." -ForegroundColor Yellow
    Remove-Item -Path $buildDir -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force -Path $objDir | Out-Null
New-Item -ItemType Directory -Force -Path $resDir | Out-Null

# ---------------------------------------------------------------------------
# Locate Visual Studio and import its environment
# ---------------------------------------------------------------------------
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    Write-Host "ERROR: vswhere.exe not found. Install Visual Studio with the" -ForegroundColor Red
    Write-Host "       'Desktop development with C++' workload." -ForegroundColor Red
    exit 1
}

$vsPath = & $vsWhere -latest -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) {
    Write-Host "ERROR: No Visual Studio installation found." -ForegroundColor Red
    exit 1
}

$vcVarsAll = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcVarsAll)) {
    Write-Host "ERROR: vcvarsall.bat not found under: $vsPath" -ForegroundColor Red
    exit 1
}

Write-Host "Visual Studio: $vsPath" -ForegroundColor Green
$envOutput = cmd /c "call `"$vcVarsAll`" x64 >nul && set"
foreach ($line in $envOutput) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:\$($matches[1])" -Value $matches[2] -Force
    }
}
Write-Host "MSVC environment ready." -ForegroundColor Green
Write-Host ""

# ---------------------------------------------------------------------------
# Compile resources
# ---------------------------------------------------------------------------
Write-Host "Compiling resources..." -ForegroundColor Green
$resFile = Join-Path $resDir "DynamicIsland.res"
& rc.exe /nologo /fo $resFile resources\DynamicIsland.rc
if ($LASTEXITCODE -ne 0) {
    Write-Host "Resource compilation failed." -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# Compile sources
# ---------------------------------------------------------------------------
Write-Host "Compiling sources..." -ForegroundColor Green
$commonFlags = @(
    "/c", "/nologo", "/O2", "/GL", "/EHsc", "/std:c++17", "/W4", "/utf-8",
    "/DNOMINMAX", "/DUNICODE", "/D_UNICODE",
    "/Iinclude", "/Iresources"
)
$sources = Get-ChildItem -Path "src" -Filter "*.cpp" | Sort-Object Name
foreach ($source in $sources) {
    $objectFile = Join-Path $objDir ($source.BaseName + ".obj")
    Write-Host ("  " + $source.Name) -ForegroundColor DarkGray
    & cl.exe @commonFlags "/Fo:$objectFile" $source.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Compilation failed: $($source.Name)" -ForegroundColor Red
        exit 1
    }
}
Write-Host "Compilation successful." -ForegroundColor Green
Write-Host ""

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
if (Test-Path "DynamicIsland.exe") {
    try {
        Remove-Item -Path "DynamicIsland.exe" -Force
    } catch {
        if ($KillRunning) {
            Write-Host "Stopping the running DynamicIsland.exe..." -ForegroundColor Yellow
            Get-Process -Name DynamicIsland -ErrorAction SilentlyContinue | Stop-Process -Force
            Start-Sleep -Milliseconds 400
            Remove-Item -Path "DynamicIsland.exe" -Force -ErrorAction SilentlyContinue
        }
    }
    if (Test-Path "DynamicIsland.exe") {
        Write-Host "ERROR: DynamicIsland.exe is locked. Close the running app" -ForegroundColor Red
        Write-Host "       or run: .\build.ps1 -KillRunning" -ForegroundColor Red
        exit 1
    }
}

Write-Host "Linking..." -ForegroundColor Green
$objects = Get-ChildItem -Path $objDir -Filter "*.obj" | ForEach-Object { $_.FullName }
& link.exe /nologo /OUT:DynamicIsland.exe /SUBSYSTEM:WINDOWS "/ENTRY:wWinMainCRTStartup" `
    /OPT:REF /OPT:ICF /LTCG $objects $resFile `
    shell32.lib user32.lib gdi32.lib gdiplus.lib ole32.lib comctl32.lib `
    dwmapi.lib advapi32.lib
if ($LASTEXITCODE -ne 0) {
    Write-Host "Linking failed." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SUCCESS! Created: DynamicIsland.exe" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
$exeSize = [math]::Round((Get-Item "DynamicIsland.exe").Length / 1KB, 1)
Write-Host "Size    : $exeSize KB" -ForegroundColor Yellow
Write-Host "Location: $(Join-Path (Get-Location) 'DynamicIsland.exe')" -ForegroundColor Yellow
Write-Host "Run     : .\DynamicIsland.exe        (settings: .\DynamicIsland.exe --settings)" -ForegroundColor White
Write-Host ""