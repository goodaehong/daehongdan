param(
    [string]$CameraIp = "172.20.32.15",
    [string]$Username = "admin",
    [string]$OpenCvBin = "C:\Users\3-19\Downloads\opencv\build\x64\vc16\bin"
)

$ErrorActionPreference = "Stop"
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$executable = Join-Path $scriptDirectory "out\build\vs2022-x64\Release\charuco_calibrator.exe"
$output = Join-Path (Split-Path -Parent $scriptDirectory) "camera_calibration_ch4.yml"

if (-not (Test-Path -LiteralPath $executable)) {
    throw "charuco_calibrator.exe was not found. Build the calibration project first: $executable"
}
if (-not (Test-Path -LiteralPath $OpenCvBin)) {
    throw "OpenCV DLL directory was not found: $OpenCvBin"
}

$securePassword = Read-Host "Camera password" -AsSecureString
$credential = [System.Net.NetworkCredential]::new($Username, $securePassword)
$encodedUser = [System.Uri]::EscapeDataString($credential.UserName)
$encodedPassword = [System.Uri]::EscapeDataString($credential.Password)
$cameraUrl = "rtsp://${encodedUser}:${encodedPassword}@${CameraIp}:554/3/profile10/media.smp"

$savedPath = [Environment]::GetEnvironmentVariable("Path", "Process")
try {
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = "$OpenCvBin;$savedPath"
    Write-Host "Channel 4 ChArUco calibration"
    Write-Host "Board: DICT_4X4_50, 7x5, square 50 mm, marker 35 mm"
    Write-Host "SPACE=capture, U=undo, C=calculate/save, Q=quit"
    Write-Host "Move and tilt the board; collect 20-25 diverse views."
    & $executable $cameraUrl --channel 4 --output $output
    if ($LASTEXITCODE -ne 0) {
        throw "Calibration tool exited with code $LASTEXITCODE"
    }
    if (Test-Path -LiteralPath $output) {
        Write-Host "Saved: $output"
    }
}
finally {
    Remove-Variable credential -ErrorAction SilentlyContinue
    Remove-Variable encodedPassword -ErrorAction SilentlyContinue
    Remove-Variable cameraUrl -ErrorAction SilentlyContinue
    $env:Path = $savedPath
}
