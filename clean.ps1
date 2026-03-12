# Uninstall Script - Complete Removal of Dynamic Island
# Usage: .\clean.ps1 [-Force] [-Confirm]

param(
    [switch]$Force,    # Force uninstall without confirmation
    [switch]$Confirm   # Ask for confirmation before uninstall
)

$ErrorActionPreference = "SilentlyContinue"
[ArrayList]$uninstallReport = @()
$deletedCount = 0

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Dynamic Island Uninstaller" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Function to add to report
function Add-ToReport {
    param([string]$Message, [string]$Type = "Info")
    $uninstallReport.Add("[$Type] $Message") | Out-Null
}

# Step 1: Terminate running process
Write-Host "Step 1: Terminating Dynamic Island process..." -ForegroundColor Yellow
try {
    $process = Get-Process -Name "DynamicIsland" -ErrorAction Stop
    Stop-Process -Id $process.Id -Force
    Write-Host "  Terminated: DynamicIsland.exe (PID: $($process.Id))" -ForegroundColor Green
    Add-ToReport "Terminated process: DynamicIsland.exe (PID: $($process.Id))" "Success"
    $deletedCount++
} catch {
    Write-Host "  Dynamic Island is not running" -ForegroundColor Gray
    Add-ToReport "Dynamic Island was not running" "Info"
}

Write-Host ""

# Step 2: Delete files and directories
Write-Host "Step 2: Deleting files and directories..." -ForegroundColor Yellow

# Delete .obj files
Write-Host "  Deleting *.obj files..." -ForegroundColor Gray
$objFiles = Get-ChildItem -Path . -Filter *.obj -Recurse
foreach ($file in $objFiles) {
    try {
        Remove-Item -Path $file.FullName -Force
        Write-Host "    Deleted: $($file.Name)" -ForegroundColor Gray
        Add-ToReport "Deleted: $($file.FullName)" "Success"
        $deletedCount++
    } catch {
        Write-Host "    Failed to delete: $($file.Name)" -ForegroundColor Red
        Add-ToReport "Failed to delete: $($file.FullName)" "Error"
    }
}

# Delete executable
Write-Host "  Deleting DynamicIsland.exe..." -ForegroundColor Gray
if (Test-Path ".\DynamicIsland.exe") {
    try {
        Remove-Item -Path ".\DynamicIsland.exe" -Force
        Write-Host "    Deleted: DynamicIsland.exe" -ForegroundColor Green
        Add-ToReport "Deleted: DynamicIsland.exe" "Success"
        $deletedCount++
    } catch {
        Write-Host "    Failed to delete: DynamicIsland.exe" -ForegroundColor Red
        Add-ToReport "Failed to delete: DynamicIsland.exe" "Error"
    }
} else {
    Write-Host "    DynamicIsland.exe not found" -ForegroundColor Gray
    Add-ToReport "DynamicIsland.exe not found" "Info"
}

# Delete .lib files
Write-Host "  Deleting *.lib files..." -ForegroundColor Gray
$libFiles = Get-ChildItem -Path . -Filter *.lib -Recurse
foreach ($file in $libFiles) {
    try {
        Remove-Item -Path $file.FullName -Force
        Write-Host "    Deleted: $($file.Name)" -ForegroundColor Gray
        Add-ToReport "Deleted: $($file.FullName)" "Success"
        $deletedCount++
    } catch {
        Write-Host "    Failed to delete: $($file.Name)" -ForegroundColor Red
        Add-ToReport "Failed to delete: $($file.FullName)" "Error"
    }
}

# Delete .exp files
Write-Host "  Deleting *.exp files..." -ForegroundColor Gray
$expFiles = Get-ChildItem -Path . -Filter *.exp -Recurse
foreach ($file in $expFiles) {
    try {
        Remove-Item -Path $file.FullName -Force
        Write-Host "    Deleted: $($file.Name)" -ForegroundColor Gray
        Add-ToReport "Deleted: $($file.FullName)" "Success"
        $deletedCount++
    } catch {
        Write-Host "    Failed to delete: $($file.Name)" -ForegroundColor Red
        Add-ToReport "Failed to delete: $($file.FullName)" "Error"
    }
}

# Delete builds directory
Write-Host "  Deleting builds/ directory..." -ForegroundColor Gray
if (Test-Path ".\builds") {
    try {
        Remove-Item -Path ".\builds" -Recurse -Force
        Write-Host "    Deleted: builds/" -ForegroundColor Green
        Add-ToReport "Deleted: builds/ directory" "Success"
        $deletedCount++
    } catch {
        Write-Host "    Failed to delete: builds/" -ForegroundColor Red
        Add-ToReport "Failed to delete: builds/ directory" "Error"
    }
} else {
    Write-Host "    builds/ directory not found" -ForegroundColor Gray
    Add-ToReport "builds/ directory not found" "Info"
}

Write-Host ""

# Step 3: Delete registry configuration
Write-Host "Step 3: Deleting registry configuration..." -ForegroundColor Yellow

# Delete DynamicIsland settings
$regPath = "HKCU:\SOFTWARE\DynamicIsland"
if (Test-Path $regPath) {
    try {
        Remove-Item -Path $regPath -Recurse -Force
        Write-Host "  Deleted: HKCU\SOFTWARE\DynamicIsland" -ForegroundColor Green
        Add-ToReport "Deleted registry: HKCU\SOFTWARE\DynamicIsland" "Success"
        $deletedCount++
    } catch {
        Write-Host "  Failed to delete: HKCU\SOFTWARE\DynamicIsland" -ForegroundColor Red
        Add-ToReport "Failed to delete registry: HKCU\SOFTWARE\DynamicIsland" "Error"
    }
} else {
    Write-Host "  HKCU\SOFTWARE\DynamicIsland not found" -ForegroundColor Gray
    Add-ToReport "Registry HKCU\SOFTWARE\DynamicIsland not found" "Info"
}

Write-Host ""

# Step 4: Delete startup entry
Write-Host "Step 4: Deleting startup entry..." -ForegroundColor Yellow

$startupPath = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
try {
    $startupValue = Get-ItemProperty -Path $startupPath -Name "DynamicIsland" -ErrorAction Stop
    Remove-ItemProperty -Path $startupPath -Name "DynamicIsland" -Force
    Write-Host "  Deleted startup entry: DynamicIsland" -ForegroundColor Green
    Add-ToReport "Deleted startup entry: DynamicIsland" "Success"
    $deletedCount++
} catch [System.Management.Automation.PSArgumentException] {
    Write-Host "  Startup entry not found" -ForegroundColor Gray
    Add-ToReport "Startup entry 'DynamicIsland' not found" "Info"
} catch {
    Write-Host "  Failed to delete startup entry" -ForegroundColor Red
    Add-ToReport "Failed to delete startup entry" "Error"
}

Write-Host ""

# Display uninstall report
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Uninstall Report" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
foreach ($entry in $uninstallReport) {
    if ($entry -match "\[Success\]") {
        Write-Host "  $entry" -ForegroundColor Green
    } elseif ($entry -match "\[Error\]") {
        Write-Host "  $entry" -ForegroundColor Red
    } else {
        Write-Host "  $entry" -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Uninstall Complete!" -ForegroundColor Green
Write-Host "  Total items processed: $deletedCount" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
