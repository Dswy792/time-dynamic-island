<#
.SYNOPSIS
    Capture a region of the screen (or the whole virtual screen) to a PNG file.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\capture-screen.ps1 -Out builds\shot.png
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\capture-screen.ps1 -Out builds\top.png -X 0 -Y 0 -W 800 -H 200
#>
param(
    [string]$Out = "builds\screenshot.png",
    [int]$X = -2147483648,
    [int]$Y = -2147483648,
    [int]$W = -1,
    [int]$H = -1
)

$ErrorActionPreference = "Stop"

# Make this process per-monitor DPI aware so CopyFromScreen uses physical pixels,
# matching the coordinates used by the per-monitor-v2 aware app under test.
if (-not ("DpiAwareHelper" -as [type])) {
    Add-Type -Namespace Win32 -Name Dpi -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
[DllImport("user32.dll")] public static extern int GetDpiForSystem();
'@
}
try { [Win32.Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null } catch { }

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
if ($X -eq -2147483648) { $X = $vs.Left }
if ($Y -eq -2147483648) { $Y = $vs.Top }
if ($W -le 0) { $W = $vs.Width }
if ($H -le 0) { $H = $vs.Height }

$full = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Out))
$dir = Split-Path -Parent $full
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

$bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($X, $Y, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "Saved: $full ($W x $H, origin $X,$Y)"
