param(
    [Parameter(Mandatory = $true)]
    [string]$Axf,

    [Parameter(Mandatory = $true)]
    [string]$OutDir,

    [Parameter(Mandatory = $true)]
    [ValidateSet("APP", "Bootloader")]
    [string]$Kind,

    [string]$ProjectName = "SolarClean",
    [string]$Address = "",
    [string]$Config = "..\..\BSP\dji_sdk_config.h",
    [string]$FromElf = "fromelf"
)

$ErrorActionPreference = "Stop"

function Resolve-FromWorkingDirectory {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Get-FirmwareVersion {
    param([string]$ConfigPath)

    $version = @{
        Major = $null
        Minor = $null
        Modify = $null
        Debug = $null
    }

    foreach ($line in Get-Content -LiteralPath $ConfigPath) {
        if ($line -match 'USER_FIRMWARE_MAJOR_VERSION\s+\((\d+)\)') { $version.Major = $matches[1] }
        if ($line -match 'USER_FIRMWARE_MINOR_VERSION\s+\((\d+)\)') { $version.Minor = $matches[1] }
        if ($line -match 'USER_FIRMWARE_MODIFY_VERSION\s+\((\d+)\)') { $version.Modify = $matches[1] }
        if ($line -match 'USER_FIRMWARE_DEBUG_VERSION\s+\((\d+)\)') { $version.Debug = $matches[1] }
    }

    if ($null -eq $version.Major -or $null -eq $version.Minor -or
        $null -eq $version.Modify -or $null -eq $version.Debug) {
        throw "Failed to parse firmware version from $ConfigPath"
    }

    if ([int]$version.Debug -eq 0) {
        return ("V{0}.{1}.{2}" -f $version.Major, $version.Minor, $version.Modify)
    }
    return ("V{0}.{1}.{2}.{3}" -f $version.Major, $version.Minor, $version.Modify, $version.Debug)
}

$resolvedAxf = Resolve-FromWorkingDirectory $Axf
$resolvedOutDir = Resolve-FromWorkingDirectory $OutDir
$resolvedConfig = Resolve-FromWorkingDirectory $Config

if (!(Test-Path -LiteralPath $resolvedAxf)) {
    throw "AXF not found: $resolvedAxf"
}
if (!(Test-Path -LiteralPath $resolvedConfig)) {
    throw "Config not found: $resolvedConfig"
}

New-Item -ItemType Directory -Force -Path $resolvedOutDir | Out-Null

if ([string]::IsNullOrWhiteSpace($Address)) {
    if ($Kind -eq "APP") {
        $Address = "0x08020000"
    } else {
        $Address = "0x08000000"
    }
}

if ($Kind -eq "APP") {
    $version = Get-FirmwareVersion -ConfigPath $resolvedConfig
    $fileName = "{0}_{1}.bin" -f $ProjectName, $version
} else {
    $fileName = "{0}_Bootloader.bin" -f $ProjectName
}

$outPath = Join-Path $resolvedOutDir $fileName
& $FromElf --bin -o $outPath $resolvedAxf
if ($LASTEXITCODE -ne 0) {
    throw "fromelf failed with exit code $LASTEXITCODE"
}

Write-Host "Generated named bin: $outPath"
