param(
    [string]$GameRoot = "C:\Program Files (x86)\Steam\steamapps\common\SovietRepublic"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$sourceRoot = Join-Path $GameRoot "media_soviet\editor"
$outputRoot = Join-Path $PSScriptRoot "..\assets\lock-icons"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$icons = @{
    "locked_crime.png"       = @(41, 45, 82, 81)
    "locked_demolition.png"  = @(54, 41, 89, 82)
    "locked_dlc.png"         = @(56, 44, 83, 82)
    "locked_education.png"   = @(48, 51, 92, 85)
    "locked_fires.png"       = @(55, 43, 84, 83)
    "locked_heating.png"     = @(43, 40, 84, 84)
    "locked_maintenance.png" = @(46, 43, 85, 82)
    "locked_pollution.png"   = @(55, 32, 83, 82)
    "locked_power.png"       = @(49, 46, 83, 82)
    "locked_powerfuel.png"   = @(53, 46, 82, 80)
    "locked_seasons.png"     = @(43, 40, 84, 84)
    "locked_terrain.png"     = @(33, 55, 84, 82)
    "locked_traffic.png"     = @(43, 35, 85, 75)
    "locked_waste.png"       = @(48, 49, 84, 84)
    "locked_water.png"       = @(51, 39, 81, 83)
}

foreach ($entry in $icons.GetEnumerator()) {
    $sourcePath = Join-Path $sourceRoot $entry.Key
    $bounds = $entry.Value
    $left, $top, $right, $bottom = $bounds
    $width = $right - $left + 1
    $height = $bottom - $top + 1

    $source = [System.Drawing.Bitmap]::FromFile($sourcePath)
    $cutout = New-Object System.Drawing.Bitmap $width, $height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    for ($y = 0; $y -lt $height; ++$y) {
        for ($x = 0; $x -lt $width; ++$x) {
            $color = $source.GetPixel($left + $x, $top + $y)
            $maximum = [Math]::Max($color.R, [Math]::Max($color.G, $color.B))
            $minimum = [Math]::Min($color.R, [Math]::Min($color.G, $color.B))
            $saturation = $maximum - $minimum
            $luminance = ($color.R + $color.G + $color.B) / 3.0

            # The stock card is pale, nearly neutral parchment. Preserve both
            # colourful pixels and dark neutral outlines, while converting the
            # parchment and its shadow into a soft transparent edge.
            $colourStrength = [Math]::Max(0.0, ($saturation - 10.0) / 58.0)
            $darkStrength = [Math]::Max(0.0, (205.0 - $luminance) / 105.0)
            $strength = [Math]::Min(1.0, [Math]::Max($colourStrength, $darkStrength))
            if ($strength -lt 0.08) { $strength = 0.0 }
            elseif ($strength -lt 1.0) { $strength = [Math]::Min(1.0, ($strength - 0.08) / 0.72) }
            $alpha = [int]([Math]::Round($color.A * $strength))
            $cutout.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(
                $alpha, $color.R, $color.G, $color.B))
        }
    }
    $source.Dispose()

    $output = New-Object System.Drawing.Bitmap 64, 64,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($output)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $scale = [Math]::Min(52.0 / $width, 52.0 / $height)
    $targetWidth = [int][Math]::Round($width * $scale)
    $targetHeight = [int][Math]::Round($height * $scale)
    $targetX = [int][Math]::Round((64 - $targetWidth) / 2.0)
    $targetY = [int][Math]::Round((64 - $targetHeight) / 2.0)
    $graphics.DrawImage($cutout, $targetX, $targetY, $targetWidth, $targetHeight)
    $graphics.Dispose()
    $cutout.Dispose()

    $outputPath = Join-Path $outputRoot $entry.Key
    $output.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $output.Dispose()
}

Get-ChildItem -LiteralPath $outputRoot -Filter "locked_*.png" |
    Sort-Object Name | Select-Object Name, Length
