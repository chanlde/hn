param(
    [string]$OutDir = "production",
    [string]$ProjectName = "SolarClean",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

function Get-FirmwareVersion {
    param([string]$ConfigPath)

    $values = @{
        Major = $null
        Minor = $null
        Modify = $null
        Debug = $null
    }

    foreach ($line in Get-Content -LiteralPath $ConfigPath) {
        if ($line -match 'USER_FIRMWARE_MAJOR_VERSION\s+\((\d+)\)') { $values.Major = $matches[1] }
        if ($line -match 'USER_FIRMWARE_MINOR_VERSION\s+\((\d+)\)') { $values.Minor = $matches[1] }
        if ($line -match 'USER_FIRMWARE_MODIFY_VERSION\s+\((\d+)\)') { $values.Modify = $matches[1] }
        if ($line -match 'USER_FIRMWARE_DEBUG_VERSION\s+\((\d+)\)') { $values.Debug = $matches[1] }
    }

    if ($null -eq $values.Major -or $null -eq $values.Minor -or
        $null -eq $values.Modify -or $null -eq $values.Debug) {
        throw "Failed to parse firmware version from $ConfigPath"
    }

    if ([int]$values.Debug -eq 0) {
        return ("V{0}.{1}.{2}" -f $values.Major, $values.Minor, $values.Modify)
    }
    return ("V{0}.{1}.{2}.{3}" -f $values.Major, $values.Minor, $values.Modify, $values.Debug)
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Get-FirmwareVersion -ConfigPath (Join-Path (Get-Location) "BSP\dji_sdk_config.h")
}

$items = @(
    @{
        Source = "Bootloader\MDK-ARM\Bootloader\Bootloader.bin"
        Target = ("{0}_Bootloader.bin" -f $ProjectName)
        Address = "0x08000000"
    },
    @{
        Source = "APP\MDK-ARM\APP\APP.bin"
        Target = ("{0}_{1}.bin" -f $ProjectName, $Version)
        Address = "0x08020000"
    }
)

$outRoot = Join-Path (Get-Location) $OutDir
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$manifest = @(
    ("{0} separated flashing files" -f $ProjectName),
    "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')",
    ("Firmware version: {0}" -f $Version),
    ""
)

foreach ($item in $items) {
    $src = Join-Path (Get-Location) $item.Source
    if (!(Test-Path -LiteralPath $src)) {
        throw "Missing file: $src"
    }
    $dst = Join-Path $outRoot $item.Target
    Copy-Item -LiteralPath $src -Destination $dst -Force
    $file = Get-Item -LiteralPath $dst
    $manifest += ("{0}" -f $item.Target)
    $manifest += ("  burn address: {0}" -f $item.Address)
    $manifest += ("  size: {0} bytes" -f $file.Length)
    $manifest += ""
}

$manifest += "First-time flashing: burn Bootloader first, then APP."
$manifest += "APP update only: burn APP file only."
$manifest += "Do not use full-chip erase when preserving Bootloader."

$manifestPath = Join-Path $outRoot "SolarClean_separated_flash_manifest.txt"
[System.IO.File]::WriteAllLines($manifestPath, $manifest, [System.Text.Encoding]::UTF8)

Write-Host "Generated separated flashing files in $outRoot"
