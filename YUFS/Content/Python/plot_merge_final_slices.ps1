Add-Type -AssemblyName System.Drawing
$sections = Get-Content "$PSScriptRoot/../../Saved/MergeFinalWallSlices.json" -Raw | ConvertFrom-Json
$bitmap = New-Object System.Drawing.Bitmap 1600,1600
$canvas = [System.Drawing.Graphics]::FromImage($bitmap)
$canvas.Clear([System.Drawing.Color]::White)
$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::Black),1
$font = New-Object System.Drawing.Font 'Arial',11
foreach ($height in @(120,470)) {
    $originY = if ($height -eq 120) {20} else {820}
    foreach ($item in $sections | Where-Object z -eq $height) {
        foreach ($segment in $item.segments) {
            $x1 = 50+($segment[0][0]-2200)*0.19
            $x2 = 50+($segment[1][0]-2200)*0.19
            $y1 = $originY+($segment[0][1]+10600)*0.19
            $y2 = $originY+($segment[1][1]+10600)*0.19
            $canvas.DrawLine($pen,[single]$x1,[single]$y1,[single]$x2,[single]$y2)
        }
    }
    $canvas.DrawString("Wall section Z=$height",$font,[System.Drawing.Brushes]::Red,10,$originY)
    for ($x=2500;$x -le 9500;$x+=500) { $canvas.DrawString("$x",$font,[System.Drawing.Brushes]::Blue,[single](50+($x-2200)*0.19),[single]($originY+720)) }
    for ($y=-10000;$y -le -7500;$y+=500) { $canvas.DrawString("$y",$font,[System.Drawing.Brushes]::Blue,1450,[single]($originY+($y+10600)*0.19)) }
}
$bitmap.Save("$PSScriptRoot/../../Saved/MergeFinalWallSlices.png",[System.Drawing.Imaging.ImageFormat]::Png)
$canvas.Dispose(); $bitmap.Dispose()
