Add-Type -AssemblyName System.Drawing

$msxHdPath = "D:\fMSX-PS3\msx_hd_logo.png"
if (-not (Test-Path $msxHdPath)) {
    Write-Error "msx_hd_logo.png not found"
    exit 1
}

$rawLogo = [System.Drawing.Bitmap]::FromFile($msxHdPath)

# Set ColorKey for instantaneous transparent rendering of dark background
$ia = New-Object System.Drawing.Imaging.ImageAttributes
$ia.SetColorKey([System.Drawing.Color]::FromArgb(0, 0, 0), [System.Drawing.Color]::FromArgb(30, 30, 45))

function Draw-RadialGradient([System.Drawing.Graphics]$g, [int]$w, [int]$h, [System.Drawing.Color]$centerCol, [System.Drawing.Color]$edgeCol) {
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddRectangle($rect)
    $pbg = New-Object System.Drawing.Drawing2D.PathGradientBrush($path)
    $pbg.CenterPoint = New-Object System.Drawing.PointF(($w / 2), ($h * 0.42))
    $pbg.CenterColor = $centerCol
    $pbg.SurroundColors = @($edgeCol)
    $g.FillRectangle($pbg, $rect)
    $pbg.Dispose()
    $path.Dispose()
}

$sfCenter = New-Object System.Drawing.StringFormat
$sfCenter.Alignment = [System.Drawing.StringAlignment]::Center

# ----------------------------------------------------
# 1. Generate pkg/ICON0.PNG (320 x 176)
# ----------------------------------------------------
$iconW = 320
$iconH = 176
$iconBmp = New-Object System.Drawing.Bitmap($iconW, $iconH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gIcon = [System.Drawing.Graphics]::FromImage($iconBmp)
$gIcon.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$gIcon.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$gIcon.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

$cCenter = [System.Drawing.Color]::FromArgb(255, 18, 44, 88)
$cEdge   = [System.Drawing.Color]::FromArgb(255, 4, 8, 18)
Draw-RadialGradient $gIcon $iconW $iconH $cCenter $cEdge

# Subtle top highlight
$penHi = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(100, 0, 229, 255), 1.5)
$gIcon.DrawLine($penHi, 4, 2, $iconW - 4, 2)
$penHi.Dispose()

# Draw MSX HD Logo
$logoW = 168
$logoH = [int]($logoW * 1590 / 3840) # ~70px
$logoX = [int](($iconW - $logoW) / 2)
$logoY = 12

# Subtle glow behind logo
$bGlow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(45, 0, 229, 255))
$gIcon.FillEllipse($bGlow, ($logoX - 10), ($logoY - 6), ($logoW + 20), ($logoH + 12))
$bGlow.Dispose()

# Badge background container
$bBadge = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(240, 6, 12, 24))
$gIcon.FillRectangle($bBadge, $logoX, $logoY, $logoW, $logoH)
$bBadge.Dispose()

# Draw HD logo image
$destRectIcon = New-Object System.Drawing.Rectangle($logoX, $logoY, $logoW, $logoH)
$gIcon.DrawImage($rawLogo, $destRectIcon, 0, 0, $rawLogo.Width, $rawLogo.Height, [System.Drawing.GraphicsUnit]::Pixel, $ia)

# Badge border
$penBadge = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(180, 0, 200, 255), 1.5)
$gIcon.DrawRectangle($penBadge, $logoX, $logoY, $logoW, $logoH)
$penBadge.Dispose()

# Title: MSX4PS3
$fTitle = New-Object System.Drawing.Font("Segoe UI", 20, [System.Drawing.FontStyle]::Bold)
$bShadow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(220, 0, 0, 0))
$gIcon.DrawString("MSX4PS3", $fTitle, $bShadow, ($iconW / 2 + 2), 94, $sfCenter)

$bCyan = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0, 229, 255))
$gIcon.DrawString("MSX4PS3", $fTitle, $bCyan, ($iconW / 2), 92, $sfCenter)

# Subtitle: v1.00 - PS3 PORT by theheroGAC
$fSub = New-Object System.Drawing.Font("Segoe UI", 8.5, [System.Drawing.FontStyle]::Bold)
$bGold = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 216, 64))
$gIcon.DrawString("v1.00  -  PS3 PORT by theheroGAC", $fSub, $bShadow, ($iconW / 2 + 1), 133, $sfCenter)
$gIcon.DrawString("v1.00  -  PS3 PORT by theheroGAC", $fSub, $bGold, ($iconW / 2), 132, $sfCenter)

$fSub2 = New-Object System.Drawing.Font("Segoe UI", 7, [System.Drawing.FontStyle]::Regular)
$bDim = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(200, 160, 200, 240))
$gIcon.DrawString("MSX / MSX2 / MSX2+ EMULATOR", $fSub2, $bDim, ($iconW / 2), 151, $sfCenter)

# Outer Border
$penBorder = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(140, 40, 140, 255), 2)
$gIcon.DrawRectangle($penBorder, 1, 1, $iconW - 2, $iconH - 2)
$penBorder.Dispose()

$iconBmp.Save("D:\fMSX-PS3\pkg\ICON0.PNG", [System.Drawing.Imaging.ImageFormat]::Png)
$gIcon.Dispose()
$iconBmp.Dispose()
Write-Host "ICON0.PNG generated successfully."

# ----------------------------------------------------
# 2. Generate pkg/PIC1.PNG (1920 x 1080)
# ----------------------------------------------------
$picW = 1920
$picH = 1080
$picBmp = New-Object System.Drawing.Bitmap($picW, $picH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$gPic = [System.Drawing.Graphics]::FromImage($picBmp)
$gPic.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$gPic.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$gPic.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

$cCenterPic = [System.Drawing.Color]::FromArgb(255, 14, 34, 72)
$cEdgePic   = [System.Drawing.Color]::FromArgb(255, 3, 6, 14)
Draw-RadialGradient $gPic $picW $picH $cCenterPic $cEdgePic

# Tech grid lines
$penGrid = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(20, 0, 180, 255), 1)
for ($x = 0; $x -lt $picW; $x += 60) {
    $gPic.DrawLine($penGrid, $x, 0, $x, $picH)
}
for ($y = 0; $y -lt $picH; $y += 60) {
    $gPic.DrawLine($penGrid, 0, $y, $picW, $y)
}
$penGrid.Dispose()

# Central cyan glow
$brushGlow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(35, 0, 200, 255))
$gPic.FillEllipse($brushGlow, [int](($picW - 1000) / 2), 100, 1000, 480)
$brushGlow.Dispose()

# MSX HD Logo on PIC1
$picLogoW = 680
$picLogoH = [int]($picLogoW * 1590 / 3840) # ~281px
$picLogoX = [int](($picW - $picLogoW) / 2)
$picLogoY = 140

# Badge container
$bPicBadge = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(235, 6, 12, 26))
$gPic.FillRectangle($bPicBadge, $picLogoX, $picLogoY, $picLogoW, $picLogoH)
$bPicBadge.Dispose()

# Draw HD logo
$destRectPic = New-Object System.Drawing.Rectangle($picLogoX, $picLogoY, $picLogoW, $picLogoH)
$gPic.DrawImage($rawLogo, $destRectPic, 0, 0, $rawLogo.Width, $rawLogo.Height, [System.Drawing.GraphicsUnit]::Pixel, $ia)

# Border around badge
$penPicBadge = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(200, 0, 210, 255), 2.5)
$gPic.DrawRectangle($penPicBadge, $picLogoX, $picLogoY, $picLogoW, $picLogoH)
$penPicBadge.Dispose()

# Title: MSX4PS3
$fPicTitle = New-Object System.Drawing.Font("Segoe UI", 66, [System.Drawing.FontStyle]::Bold)
$bPicShadow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(240, 0, 0, 0))
$gPic.DrawString("MSX4PS3", $fPicTitle, $bPicShadow, ($picW / 2 + 4), 474, $sfCenter)

$bPicTitle = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0, 229, 255))
$gPic.DrawString("MSX4PS3", $fPicTitle, $bPicTitle, ($picW / 2), 470, $sfCenter)

# Version & Port Credit
$fPicSub = New-Object System.Drawing.Font("Segoe UI", 26, [System.Drawing.FontStyle]::Bold)
$bPicGold = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 216, 64))
$gPic.DrawString("v1.00  |  PS3 PORT by theheroGAC", $fPicSub, $bPicShadow, ($picW / 2 + 2), 582, $sfCenter)
$gPic.DrawString("v1.00  |  PS3 PORT by theheroGAC", $fPicSub, $bPicGold, ($picW / 2), 580, $sfCenter)

# Description line
$fPicDesc = New-Object System.Drawing.Font("Segoe UI", 20, [System.Drawing.FontStyle]::Regular)
$bPicWhite = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(240, 220, 240, 255))
$gPic.DrawString("High Definition MSX / MSX2 / MSX2+ Architecture", $fPicDesc, $bPicWhite, ($picW / 2), 638, $sfCenter)

$fPicFoot = New-Object System.Drawing.Font("Segoe UI", 15, [System.Drawing.FontStyle]::Regular)
$bPicDim = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(160, 140, 180, 220))
$gPic.DrawString("RSX 1080p Acceleration  *  48kHz Low-Latency Audio  *  C-BIOS & OEM Hardware", $fPicFoot, $bPicDim, ($picW / 2), 690, $sfCenter)

$picBmp.Save("D:\fMSX-PS3\pkg\PIC1.PNG", [System.Drawing.Imaging.ImageFormat]::Png)
$gPic.Dispose()
$picBmp.Dispose()
Write-Host "PIC1.PNG generated successfully."

# ----------------------------------------------------
# 3. Generate pkg/USRDIR/data/splash.bmp (1280 x 720, 24-bit RGB)
# ----------------------------------------------------
$splW = 1280
$splH = 720
$splBmp = New-Object System.Drawing.Bitmap($splW, $splH, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$gSpl = [System.Drawing.Graphics]::FromImage($splBmp)
$gSpl.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$gSpl.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$gSpl.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

$cCenterSpl = [System.Drawing.Color]::FromArgb(255, 15, 36, 72)
$cEdgeSpl   = [System.Drawing.Color]::FromArgb(255, 4, 8, 16)
Draw-RadialGradient $gSpl $splW $splH $cCenterSpl $cEdgeSpl

# Grid lines
$penGridSpl = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(18, 0, 180, 255), 1)
for ($x = 0; $x -lt $splW; $x += 40) {
    $gSpl.DrawLine($penGridSpl, $x, 0, $x, $splH)
}
for ($y = 0; $y -lt $splH; $y += 40) {
    $gSpl.DrawLine($penGridSpl, 0, $y, $splW, $y)
}
$penGridSpl.Dispose()

# Logo on splash screen
$splLogoW = 500
$splLogoH = [int]($splLogoW * 1590 / 3840) # ~207px
$splLogoX = [int](($splW - $splLogoW) / 2)
$splLogoY = 85

# Badge container
$bSplBadge = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(235, 6, 12, 26))
$gSpl.FillRectangle($bSplBadge, $splLogoX, $splLogoY, $splLogoW, $splLogoH)
$bSplBadge.Dispose()

# Draw HD logo
$destRectSpl = New-Object System.Drawing.Rectangle($splLogoX, $splLogoY, $splLogoW, $splLogoH)
$gSpl.DrawImage($rawLogo, $destRectSpl, 0, 0, $rawLogo.Width, $rawLogo.Height, [System.Drawing.GraphicsUnit]::Pixel, $ia)

# Border around badge
$penSplBadge = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(190, 0, 210, 255), 2)
$gSpl.DrawRectangle($penSplBadge, $splLogoX, $splLogoY, $splLogoW, $splLogoH)
$penSplBadge.Dispose()

# Title: MSX4PS3
$fSplTitle = New-Object System.Drawing.Font("Segoe UI", 48, [System.Drawing.FontStyle]::Bold)
$bSplShadow = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(220, 0, 0, 0))
$gSpl.DrawString("MSX4PS3", $fSplTitle, $bSplShadow, ($splW / 2 + 3), 333, $sfCenter)

$bSplCyan = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0, 229, 255))
$gSpl.DrawString("MSX4PS3", $fSplTitle, $bSplCyan, ($splW / 2), 330, $sfCenter)

# Subtitle
$fSplSub = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
$bSplGold = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 216, 64))
$gSpl.DrawString("v1.00  |  PS3 PORT by theheroGAC", $fSplSub, $bSplShadow, ($splW / 2 + 2), 412, $sfCenter)
$gSpl.DrawString("v1.00  |  PS3 PORT by theheroGAC", $fSplSub, $bSplGold, ($splW / 2), 410, $sfCenter)

$fSplDesc = New-Object System.Drawing.Font("Segoe UI", 13, [System.Drawing.FontStyle]::Regular)
$bSplDim = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(200, 160, 200, 240))
$gSpl.DrawString("High Definition MSX / MSX2 / MSX2+ Emulation Core", $fSplDesc, $bSplDim, ($splW / 2), 455, $sfCenter)

# Save 24-bit BMP
New-Item -ItemType Directory -Force -Path "D:\fMSX-PS3\pkg\USRDIR\data" | Out-Null
$splBmp.Save("D:\fMSX-PS3\pkg\USRDIR\data\splash.bmp", [System.Drawing.Imaging.ImageFormat]::Bmp)
$gSpl.Dispose()
$splBmp.Dispose()
Write-Host "splash.bmp generated successfully."

$rawLogo.Dispose()
$ia.Dispose()
