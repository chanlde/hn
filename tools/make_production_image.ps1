param(
    [string]$BootBin = "Bootloader\MDK-ARM\Bootloader\Bootloader.bin",
    [string]$AppBin = "APP\MDK-ARM\APP\APP.bin",
    [string]$OutDir = "production",
    [string]$ImageName = "SolarClean_boot_app",
    [uint32]$FlashBase = 0x08000000,
    [uint32]$AppOffset = 0x00020000,
    [uint32]$AppSlotSize = 0x000A0000
)

$ErrorActionPreference = "Stop"

function Write-IntelHexRecord {
    param(
        [System.IO.StreamWriter]$Writer,
        [byte]$RecordType,
        [uint16]$Address,
        [byte[]]$Data
    )

    $count = [byte]$Data.Length
    $sum = [int]$count + (($Address -shr 8) -band 0xFF) + ($Address -band 0xFF) + [int]$RecordType
    foreach ($b in $Data) {
        $sum += [int]$b
    }
    $checksum = ((- $sum) -band 0xFF)

    $line = ":{0:X2}{1:X4}{2:X2}" -f $count, $Address, $RecordType
    foreach ($b in $Data) {
        $line += "{0:X2}" -f $b
    }
    $line += "{0:X2}" -f $checksum
    $Writer.WriteLine($line)
}

function Write-BinAsHex {
    param(
        [System.IO.StreamWriter]$Writer,
        [byte[]]$Bytes,
        [uint32]$BaseAddress
    )

    $currentUpper = [uint32]::MaxValue
    for ($offset = 0; $offset -lt $Bytes.Length; $offset += 16) {
        $absolute = [uint32]($BaseAddress + $offset)
        $upper = [uint32]($absolute -shr 16)
        if ($upper -ne $currentUpper) {
            $upperBytes = [byte[]]@((($upper -shr 8) -band 0xFF), ($upper -band 0xFF))
            Write-IntelHexRecord -Writer $Writer -RecordType 0x04 -Address 0 -Data $upperBytes
            $currentUpper = $upper
        }

        $length = [Math]::Min(16, $Bytes.Length - $offset)
        $chunk = New-Object byte[] $length
        [Array]::Copy($Bytes, $offset, $chunk, 0, $length)
        Write-IntelHexRecord -Writer $Writer -RecordType 0x00 -Address ([uint16]($absolute -band 0xFFFF)) -Data $chunk
    }
}

$bootPath = Join-Path (Get-Location) $BootBin
$appPath = Join-Path (Get-Location) $AppBin
if (!(Test-Path -LiteralPath $bootPath)) {
    throw "Bootloader bin not found: $bootPath"
}
if (!(Test-Path -LiteralPath $appPath)) {
    throw "APP bin not found: $appPath"
}

$boot = [System.IO.File]::ReadAllBytes($bootPath)
$app = [System.IO.File]::ReadAllBytes($appPath)

if ($boot.Length -gt $AppOffset) {
    throw ("Bootloader is too large: {0} bytes > app offset {1} bytes" -f $boot.Length, $AppOffset)
}
if ($app.Length -gt $AppSlotSize) {
    throw ("APP is too large: {0} bytes > slot size {1} bytes" -f $app.Length, $AppSlotSize)
}

$outRoot = Join-Path (Get-Location) $OutDir
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$combinedBinPath = Join-Path $outRoot ($ImageName + ".bin")
$combinedHexPath = Join-Path $outRoot ($ImageName + ".hex")
$manifestPath = Join-Path $outRoot ($ImageName + "_manifest.txt")

$combinedLength = $AppOffset + $app.Length
$combined = New-Object byte[] $combinedLength
for ($i = 0; $i -lt $combined.Length; $i++) {
    $combined[$i] = 0xFF
}
[Array]::Copy($boot, 0, $combined, 0, $boot.Length)
[Array]::Copy($app, 0, $combined, $AppOffset, $app.Length)
[System.IO.File]::WriteAllBytes($combinedBinPath, $combined)

$writer = New-Object System.IO.StreamWriter($combinedHexPath, $false, [System.Text.Encoding]::ASCII)
try {
    Write-BinAsHex -Writer $writer -Bytes $boot -BaseAddress $FlashBase
    Write-BinAsHex -Writer $writer -Bytes $app -BaseAddress ($FlashBase + $AppOffset)
    Write-IntelHexRecord -Writer $writer -RecordType 0x01 -Address 0 -Data ([byte[]]@())
} finally {
    $writer.Close()
}

$manifest = @(
    "SolarClean production image",
    "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')",
    "",
    ("Bootloader: {0}" -f $BootBin),
    ("Bootloader address: 0x{0:X8}" -f $FlashBase),
    ("Bootloader size: {0} bytes" -f $boot.Length),
    "",
    ("APP: {0}" -f $AppBin),
    ("APP address: 0x{0:X8}" -f ($FlashBase + $AppOffset)),
    ("APP size: {0} bytes" -f $app.Length),
    ("APP slot size: {0} bytes" -f $AppSlotSize),
    "",
    ("Combined BIN: {0}" -f $combinedBinPath),
    ("Combined BIN burn address: 0x{0:X8}" -f $FlashBase),
    ("Combined BIN size: {0} bytes" -f $combined.Length),
    "",
    ("Combined HEX: {0}" -f $combinedHexPath),
    "Combined HEX carries addresses; no manual offset is needed."
)
[System.IO.File]::WriteAllLines($manifestPath, $manifest, [System.Text.Encoding]::UTF8)

Write-Host "Generated:"
Write-Host "  $combinedBinPath"
Write-Host "  $combinedHexPath"
Write-Host "  $manifestPath"
