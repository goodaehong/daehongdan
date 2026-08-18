param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 4)]
    [int]$Channel
)

$ErrorActionPreference = "Stop"
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Channel $Channel one-step ArUco setup"
Write-Host "Step 1/2: enter factory and marker coordinates."
& (Join-Path $scriptDirectory "ConfigureArucoChannel.ps1") -Channel $Channel

Write-Host ""
Write-Host "Step 2/2: detect the installed markers and save a fixed Homography."
& (Join-Path $scriptDirectory "RunFixedHomographyCalibration.ps1") -Channel $Channel

Write-Host ""
Write-Host "Channel $Channel setup complete. Restart the production server."
