param(
    [Parameter(Mandatory = $true)]
    [string]$CameraIp,

    [string]$Username = "admin",

    [ValidateRange(0, 60)]
    [int]$CaptureSeconds = 0
)

$ErrorActionPreference = "Stop"

$ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffprobe -or -not $ffmpeg) {
    throw "FFmpeg is not in PATH. Install FFmpeg and reopen PowerShell."
}

$securePassword = Read-Host "Camera password" -AsSecureString
$passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
try {
    $plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
    $encodedUser = [Uri]::EscapeDataString($Username)
    $encodedPassword = [Uri]::EscapeDataString($plainPassword)
    $baseUrl = "rtsp://${encodedUser}:${encodedPassword}@${CameraIp}:554"

    foreach ($channel in 0..3) {
        $url = "${baseUrl}/${channel}/profile10/media.smp"
        Write-Host ""
        Write-Host "CH$($channel + 1): checking RTSP data streams..."

        $probeOutput = & $ffprobe.Source `
            -v error `
            -rtsp_transport tcp `
            -timeout 5000000 `
            -allowed_media_types data `
            -show_entries "stream=index,codec_type,codec_name,codec_long_name" `
            -of json `
            $url 2>&1

        if ($LASTEXITCODE -ne 0) {
            Write-Warning "CH$($channel + 1): connection failed or metadata is disabled."
            continue
        }

        $probeOutput
        if ($CaptureSeconds -le 0) {
            continue
        }

        $samplePath = Join-Path $PSScriptRoot `
            ("wiseai_ch{0}_sample.xml" -f ($channel + 1))
        & $ffmpeg.Source `
            -nostdin `
            -hide_banner `
            -loglevel error `
            -rtsp_transport tcp `
            -timeout 5000000 `
            -allowed_media_types data `
            -i $url `
            -t $CaptureSeconds `
            -copy_unknown `
            -map "0:d:0?" `
            -codec copy `
            -f data `
            -y `
            $samplePath

        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $samplePath)) {
            Write-Host "CH$($channel + 1): saved $samplePath"
        }
    }
}
finally {
    if ($passwordPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
    }
    $plainPassword = $null
}
