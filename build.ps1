# Build Script for Dynamic Island
# Properly initializes Visual Studio environment

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Build Dynamic Island" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$buildDir = "builds"
$objDir = Join-Path $buildDir "obj"
$resDir = Join-Path $buildDir "res"

New-Item -ItemType Directory -Force -Path $objDir | Out-Null
New-Item -ItemType Directory -Force -Path $resDir | Out-Null

# Find Visual Studio and setup environment
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -requires Microsoft.Component.MSBuild -property installationPath
    if ($vsPath) {
        $vcVarsAll = "$vsPath\VC\Auxiliary\Build\vcvarsall.bat"
        if (Test-Path $vcVarsAll) {
            Write-Host "Setting up Visual Studio environment..." -ForegroundColor Green
            
            # Setup environment variables by calling vcvarsall.bat and capturing output
            $envOutput = cmd /c "call `"$vcVarsAll`" x64 && set"
            
            # Parse and set environment variables
            $envOutput | ForEach-Object {
                if ($_ -match '^([^=]+)=(.*)$') {
                    $name = $matches[1]
                    $value = $matches[2]
                    Set-Item -Path "Env:\$name" -Value $value -Force
                }
            }
            
            Write-Host "Environment configured!" -ForegroundColor Green
        }
    }
}

# Compile resource file
Write-Host "Compiling resources..." -ForegroundColor Green
$resFile = Join-Path $resDir "DynamicIsland.res"
$rcResult = Start-Process -FilePath "rc.exe" -ArgumentList "/nologo", "/fo", $resFile, "resources\DynamicIsland.rc" -NoNewWindow -Wait -PassThru -WorkingDirectory (Get-Location)
if ($rcResult.ExitCode -ne 0) {
    Write-Host "Resource compilation failed, continuing without resources..." -ForegroundColor Yellow
}

# Compile
Write-Host ""
Write-Host "Compiling src\*.cpp..." -ForegroundColor Green

cl.exe /c /O2 /EHsc /std:c++17 /W4 /nologo /Iinclude /Iresources /utf-8 /Fo:"$objDir\\" src\*.cpp

if ($LASTEXITCODE -ne 0) {
    Write-Host "Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Compilation successful! Linking..." -ForegroundColor Green
Write-Host ""

# Link
if (Test-Path $resFile) {
    Write-Host "Linking with resources..." -ForegroundColor Green
    link.exe /nologo /OUT:DynamicIsland.exe /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OPT:REF /OPT:ICF "$objDir\*.obj" $resFile shell32.lib user32.lib gdi32.lib ole32.lib comctl32.lib dwmapi.lib advapi32.lib
} else {
    Write-Host "Linking without resources..." -ForegroundColor Yellow
    link.exe /nologo /OUT:DynamicIsland.exe /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OPT:REF /OPT:ICF "$objDir\*.obj" shell32.lib user32.lib gdi32.lib ole32.lib comctl32.lib dwmapi.lib advapi32.lib
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "Linking failed!" -ForegroundColor Red
    exit 1
}

# Cleanup
Get-ChildItem -Path $objDir -Filter "*.obj" | Remove-Item -Force

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  SUCCESS! Created: DynamicIsland.exe" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$exeSize = (Get-Item "DynamicIsland.exe").Length / 1KB
Write-Host "Size: $([math]::Round($exeSize, 1)) KB" -ForegroundColor Yellow
Write-Host "Location: $(Get-Location)\DynamicIsland.exe" -ForegroundColor Yellow
Write-Host ""
Write-Host "Features:" -ForegroundColor Cyan
Write-Host "  ✓ Elliptical shape (not rectangle)" -ForegroundColor Green
Write-Host "  ✓ Auto-updates every second" -ForegroundColor Green
Write-Host "  ✓ Date on left, time on right" -ForegroundColor Green
Write-Host "  ✓ Smooth, rounded font" -ForegroundColor Green
Write-Host "  ✓ Smaller, softer design" -ForegroundColor Green
Write-Host "  ✓ Modern capsule appearance" -ForegroundColor Green
Write-Host ""
Write-Host "Run with: .\DynamicIsland.exe" -ForegroundColor White
Write-Host ""
