# PowerShell script to remove all build-generated directories.
#
# Note: if you have previously run install.ps1 and registered the TSF DLLs
# locally, run scripts\uninstall.ps1 first to unregister them before cleaning.

$buildDirs = @(
    "build_x64",
    "build_x86",
    "build_arm64",
    "build_msi_generated",
    "dist"
)

Write-Host "1. Stopping running instances..."
Stop-Process -Name "McBopomofoServer*" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "McBopomofoConfig*" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Write-Host "2. Removing build artifacts..."
foreach ($dir in $buildDirs) {
    $path = Join-Path $PSScriptRoot $dir
    if (Test-Path $path) {
        Write-Host "  Removing $dir..."
        Remove-Item -Path $path -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Done."
