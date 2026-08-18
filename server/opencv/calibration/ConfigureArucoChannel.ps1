param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 4)]
    [int]$Channel,
    [string]$ConfigPath = ""
)

$ErrorActionPreference = "Stop"
$culture = [Globalization.CultureInfo]::InvariantCulture
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$opencvDirectory = Split-Path -Parent $scriptDirectory
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $opencvDirectory "aruco_board_config.txt"
}

function Read-Number([string]$Prompt, [double]$DefaultValue) {
    while ($true) {
        $raw = Read-Host ("{0} [{1}]" -f $Prompt, $DefaultValue.ToString("0.########", $culture))
        if ([string]::IsNullOrWhiteSpace($raw)) { return $DefaultValue }
        $value = 0.0
        if ([double]::TryParse($raw, [Globalization.NumberStyles]::Float, $culture, [ref]$value)) {
            return $value
        }
        Write-Warning "Enter a number using a decimal point, for example 31.5"
    }
}

function Read-RequiredNumber([string]$Prompt) {
    while ($true) {
        $raw = Read-Host ("{0} (required)" -f $Prompt)
        $value = 0.0
        if (-not [string]::IsNullOrWhiteSpace($raw) -and
            [double]::TryParse($raw, [Globalization.NumberStyles]::Float, $culture, [ref]$value)) {
            return $value
        }
        Write-Warning "Enter a number using a decimal point, for example 31.5"
    }
}

function Read-IntegerList([string]$Prompt, [string]$DefaultValue) {
    while ($true) {
        $raw = Read-Host ("{0} [{1}]" -f $Prompt, $DefaultValue)
        if ([string]::IsNullOrWhiteSpace($raw)) { $raw = $DefaultValue }
        $values = @()
        $valid = $true
        foreach ($part in ($raw -split ',')) {
            $parsed = 0
            if (-not [int]::TryParse($part.Trim(), [ref]$parsed) -or $parsed -lt 0 -or $parsed -gt 49) {
                $valid = $false
                break
            }
            $values += $parsed
        }
        if ($valid -and ($values | Select-Object -Unique).Count -eq $values.Count -and $values.Count -ge 4) {
            return $values
        }
        Write-Warning "Enter at least four unique DICT_4X4_50 IDs, for example 0,1,2,3,4"
    }
}

$existingLines = @()
if (Test-Path -LiteralPath $ConfigPath) {
    $existingLines = @(Get-Content -LiteralPath $ConfigPath)
}

function Existing-Values([string]$Pattern) {
    foreach ($line in $existingLines) {
        $match = [regex]::Match($line, $Pattern)
        if ($match.Success) {
            $values = @()
            for ($index = 1; $index -lt $match.Groups.Count; $index++) {
                $values += $match.Groups[$index].Value
            }
            return $values
        }
    }
    return @()
}

$factory = Existing-Values '^\s*FACTORY\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)'
$factoryDefaults = if ($factory.Count -eq 4) { @($factory | ForEach-Object { [double]::Parse($_, $culture) }) } else { @(0.0, 0.0, 60.0, 60.0) }
$scaleMatch = Existing-Values '^\s*MODEL_SCALE\s+([-+0-9.eE]+)'
$scaleDefault = if ($scaleMatch.Count -eq 1) { [double]::Parse($scaleMatch[0], $culture) } else { 50.0 }
$board = Existing-Values ("^\s*BOARD\s+{0}\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)" -f $Channel)
$hasExistingBoard = $board.Count -eq 4
if ($hasExistingBoard) {
    $boardDefaults = @($board | ForEach-Object { [double]::Parse($_, $culture) })
}

Write-Host "Configure channel $Channel ArUco factory coordinates"
Write-Host "Coordinates are REAL factory metres, not camera pixels or model centimetres."
Write-Host "Press Enter to keep a displayed default."
$factoryMinX = Read-Number "Factory minimum X (m)" $factoryDefaults[0]
$factoryMinY = Read-Number "Factory minimum Y (m)" $factoryDefaults[1]
$factoryMaxX = Read-Number "Factory maximum X (m)" $factoryDefaults[2]
$factoryMaxY = Read-Number "Factory maximum Y (m)" $factoryDefaults[3]
$modelScale = Read-Number "Model scale (1 model metre represents this many factory metres)" $scaleDefault
if ($hasExistingBoard) {
    $boardMinX = Read-Number "Channel $Channel area minimum X (factory m)" $boardDefaults[0]
    $boardMinY = Read-Number "Channel $Channel area minimum Y (factory m)" $boardDefaults[1]
    $boardMaxX = Read-Number "Channel $Channel area maximum X (factory m)" $boardDefaults[2]
    $boardMaxY = Read-Number "Channel $Channel area maximum Y (factory m)" $boardDefaults[3]
} else {
    Write-Host "Channel $Channel has no saved coordinates. Enter all four channel bounds."
    $boardMinX = Read-RequiredNumber "Channel $Channel area minimum X (factory m)"
    $boardMinY = Read-RequiredNumber "Channel $Channel area minimum Y (factory m)"
    $boardMaxX = Read-RequiredNumber "Channel $Channel area maximum X (factory m)"
    $boardMaxY = Read-RequiredNumber "Channel $Channel area maximum Y (factory m)"
}

if ($factoryMaxX -le $factoryMinX -or $factoryMaxY -le $factoryMinY -or
    $boardMaxX -le $boardMinX -or $boardMaxY -le $boardMinY -or
    $boardMinX -lt $factoryMinX -or $boardMinY -lt $factoryMinY -or
    $boardMaxX -gt $factoryMaxX -or $boardMaxY -gt $factoryMaxY -or
    $modelScale -le 0) {
    throw "Factory/channel bounds or model scale are invalid."
}

$ids = Read-IntegerList "Visible marker IDs, comma separated" "0,1,2,3,4"
$markerRecords = @()
foreach ($id in $ids) {
    $existing = Existing-Values ("^\s*MARKER\s+{0}\s+{1}\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)" -f $Channel, $id)
    $hasExistingMarker = $existing.Count -eq 4
    $defaultX = if ($existing.Count -eq 4) { [double]::Parse($existing[0], $culture) } else { ($boardMinX + $boardMaxX) * 0.5 }
    $defaultY = if ($existing.Count -eq 4) { [double]::Parse($existing[1], $culture) } else { ($boardMinY + $boardMaxY) * 0.5 }
    $defaultSideCm = if ($existing.Count -eq 4) { [double]::Parse($existing[2], $culture) * 100.0 } else { 4.0 }
    $defaultRotation = if ($existing.Count -eq 4) { [double]::Parse($existing[3], $culture) } else { 0.0 }
    Write-Host "Marker ID $id"
    if ($hasExistingMarker) {
        $x = Read-Number "  centre factory X (m)" $defaultX
        $y = Read-Number "  centre factory Y (m)" $defaultY
    } else {
        $x = Read-RequiredNumber "  centre factory X (m)"
        $y = Read-RequiredNumber "  centre factory Y (m)"
    }
    $sideCm = Read-Number "  printed BLACK square side (model cm)" $defaultSideCm
    $rotation = Read-Number "  clockwise rotation from printed upright direction (deg)" $defaultRotation
    if ($x -lt $boardMinX -or $x -gt $boardMaxX -or
        $y -lt $boardMinY -or $y -gt $boardMaxY -or $sideCm -le 0) {
        throw "Marker ID $id lies outside the channel area or has invalid size."
    }
    $markerRecords += [pscustomobject]@{ Id=$id; X=$x; Y=$y; SideM=$sideCm/100.0; Rotation=$rotation }
}

$otherGeometry = @($existingLines | Where-Object {
    $_ -match '^\s*(BOARD|MARKER)\s+' -and
    $_ -notmatch ("^\s*(BOARD|MARKER)\s+{0}(\s|$)" -f $Channel)
})
$outputLines = @(
    "# Fixed ArUco installation geometry. Coordinates are real factory metres."
    "VERSION 1"
    "DICTIONARY DICT_4X4_50"
    "GRID 60 0 59"
    ("FACTORY {0} {1} {2} {3}" -f $factoryMinX.ToString("0.########",$culture),$factoryMinY.ToString("0.########",$culture),$factoryMaxX.ToString("0.########",$culture),$factoryMaxY.ToString("0.########",$culture))
    ("MODEL_SCALE {0}" -f $modelScale.ToString("0.########",$culture))
    "# minimum markers, minimum inlier corners, maximum RMS px, hold ms, update frames, smoothing"
    "QUALITY 4 12 2.0 1500 1 0.45"
    ""
)
$outputLines += $otherGeometry
$outputLines += ""
$outputLines += "# Channel $Channel"
$outputLines += ("BOARD {0} {1} {2} {3} {4}" -f $Channel,$boardMinX.ToString("0.########",$culture),$boardMinY.ToString("0.########",$culture),$boardMaxX.ToString("0.########",$culture),$boardMaxY.ToString("0.########",$culture))
foreach ($marker in $markerRecords) {
    $outputLines += ("MARKER {0} {1} {2} {3} {4} {5}" -f $Channel,$marker.Id,$marker.X.ToString("0.########",$culture),$marker.Y.ToString("0.########",$culture),$marker.SideM.ToString("0.########",$culture),$marker.Rotation.ToString("0.########",$culture))
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backup = "$ConfigPath.$timestamp.bak"
$temporary = "$ConfigPath.tmp"
if (Test-Path -LiteralPath $ConfigPath) { Copy-Item -LiteralPath $ConfigPath -Destination $backup }
[IO.File]::WriteAllLines($temporary, $outputLines, [Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporary -Destination $ConfigPath -Force
Write-Host "Saved: $ConfigPath"
if (Test-Path -LiteralPath $backup) { Write-Host "Backup: $backup" }
$homographyPath = Join-Path $opencvDirectory ("homography_ch{0}.yml" -f $Channel)
if (Test-Path -LiteralPath $homographyPath) {
    $staleBackup = "$homographyPath.$timestamp.stale.bak"
    Move-Item -LiteralPath $homographyPath -Destination $staleBackup -Force
    Write-Host "Old Homography disabled: $staleBackup"
}
Write-Host "Next: RunFixedHomographyCalibration.cmd $Channel"
Write-Host "Or run SetupArucoChannel.cmd $Channel next time to configure and calibrate in one command."
