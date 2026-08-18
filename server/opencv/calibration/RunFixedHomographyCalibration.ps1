param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 4)]
    [int]$Channel,
    [string]$CameraIp = "172.20.32.15",
    [string]$Username = "admin",
    [string]$OpenCvBin = "C:\Users\3-19\Downloads\opencv\build\x64\vc16\bin",
    [int]$AcceptedUpdates = 30
)

$ErrorActionPreference = "Stop"
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$opencvDirectory = Split-Path -Parent $scriptDirectory
$executable = Join-Path $scriptDirectory "out\build\vs2022-x64\Release\fixed_homography_calibrator.exe"
$arucoConfig = Join-Path $opencvDirectory "aruco_board_config.txt"
$cameraCalibration = Join-Path $opencvDirectory ("camera_calibration_ch{0}.yml" -f $Channel)
$output = Join-Path $opencvDirectory ("homography_ch{0}.yml" -f $Channel)
$cameraStreamIndex = $Channel - 1

foreach ($required in @($executable, $arucoConfig, $cameraCalibration)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required file not found: $required" }
}
$configText = Get-Content -LiteralPath $arucoConfig -Raw
if ($configText -notmatch ("(?m)^\s*BOARD\s+{0}\s+" -f $Channel)) {
    throw "Channel $Channel BOARD is not configured. Run ConfigureArucoChannel.cmd $Channel first."
}
$markerCount = [regex]::Matches($configText, ("(?m)^\s*MARKER\s+{0}\s+" -f $Channel)).Count
if ($markerCount -lt 4) { throw "Channel $Channel needs at least four configured markers." }

$securePassword = Read-Host "Camera password" -AsSecureString
$credential = [System.Net.NetworkCredential]::new($Username, $securePassword)
$encodedUser = [Uri]::EscapeDataString($credential.UserName)
$encodedPassword = [Uri]::EscapeDataString($credential.Password)
$cameraUrl = "rtsp://${encodedUser}:${encodedPassword}@${CameraIp}:554/${cameraStreamIndex}/profile10/media.smp"
$savedPath = [Environment]::GetEnvironmentVariable("Path", "Process")
try {
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = "$OpenCvBin;$savedPath"
    Write-Host "Channel $Channel fixed Homography calibration"
    Write-Host "Fire/smoke server must remain stopped. Keep all configured floor markers visible."
    & $executable $cameraUrl --channel $Channel --aruco-config $arucoConfig `
        --camera-calibration $cameraCalibration --output $output `
        --accepted-updates $AcceptedUpdates --max-frames 900
    if ($LASTEXITCODE -ne 0) { throw "Fixed Homography tool exited with code $LASTEXITCODE" }
    Write-Host "Saved: $output"
    Write-Host "The production server will load this file automatically on its next start."
}
finally {
    Remove-Variable credential -ErrorAction SilentlyContinue
    Remove-Variable encodedPassword -ErrorAction SilentlyContinue
    Remove-Variable cameraUrl -ErrorAction SilentlyContinue
    $env:Path = $savedPath
}
