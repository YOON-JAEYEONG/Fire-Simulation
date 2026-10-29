param([string]$OutputRoot = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$script:Manifest = [System.Collections.Generic.List[object]]::new()
foreach ($folder in @('Buttons','Icons','Panels','Brand','Preview')) {
    New-Item -ItemType Directory -Path (Join-Path $OutputRoot $folder) -Force | Out-Null
}
function Color([string]$Hex) { [System.Drawing.ColorTranslator]::FromHtml($Hex) }
function Canvas([int]$Width,[int]$Height) {
    $script:B = [System.Drawing.Bitmap]::new($Width,$Height,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $script:G = [System.Drawing.Graphics]::FromImage($script:B)
    $script:G.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $script:G.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $script:G.Clear([System.Drawing.Color]::Transparent)
}
function Save([string]$Relative,[string]$Role,[double]$Margin=0) {
    $path = Join-Path $OutputRoot $Relative
    $script:B.Save($path,[System.Drawing.Imaging.ImageFormat]::Png)
    $script:Manifest.Add([pscustomobject]@{file=$Relative.Replace('\','/');width=$script:B.Width;height=$script:B.Height;role=$Role;umg_draw_as=$(if($Margin -gt 0){'Box'}else{'Image'});box_margin=$Margin})
    $script:G.Dispose(); $script:B.Dispose()
}
function Rounded([single]$X,[single]$Y,[single]$W,[single]$H,[single]$R,[string]$Fill,[string]$Stroke='',[single]$LineWidth=2) {
    $p=[System.Drawing.Drawing2D.GraphicsPath]::new(); $d=2*$R
    $p.AddArc($X,$Y,$d,$d,180,90); $p.AddArc($X+$W-$d,$Y,$d,$d,270,90)
    $p.AddArc($X+$W-$d,$Y+$H-$d,$d,$d,0,90); $p.AddArc($X,$Y+$H-$d,$d,$d,90,90); $p.CloseFigure()
    if($Fill){$brush=[System.Drawing.SolidBrush]::new((Color $Fill));$script:G.FillPath($brush,$p);$brush.Dispose()}
    if($Stroke){$pen=[System.Drawing.Pen]::new((Color $Stroke),$LineWidth);$script:G.DrawPath($pen,$p);$pen.Dispose()}
    $p.Dispose()
}
function Line([single]$X1,[single]$Y1,[single]$X2,[single]$Y2) { $script:G.DrawLine($script:P,$X1,$Y1,$X2,$Y2) }
function Ellipse([single]$X,[single]$Y,[single]$W,[single]$H) {$script:G.DrawEllipse($script:P,$X,$Y,$W,$H)}
function Poly($Coords,[bool]$Close=$false) {
    $pts=[System.Collections.Generic.List[System.Drawing.PointF]]::new()
    for($i=0;$i -lt $Coords.Count;$i+=2){$pts.Add([System.Drawing.PointF]::new($Coords[$i],$Coords[$i+1]))}
    if($Close){$script:G.DrawPolygon($script:P,$pts.ToArray())}else{$script:G.DrawLines($script:P,$pts.ToArray())}
}

# Square 128px brushes retain identical corner radii in all states.
# UMG Box margins 0.25 preserve 32px corners at source resolution.
$styles=@{
    primary=@(@('#147F8A','#147F8A'),@('#1995A1','#1995A1'),@('#106570','#106570'),@('#DEE5E9','#DEE5E9'))
    secondary=@(@('#FFFFFF','#147F8A'),@('#EAF6F7','#147F8A'),@('#D5ECEE','#106570'),@('#F2F5F7','#DCE3E9'))
    nav=@(@('',''),@('#EDF4F6',''),@('#D1E9ED',''),@('',''))
}
$states=@('normal','hover','pressed','disabled')
foreach($style in @('primary','secondary','nav')) {
    for($i=0;$i -lt 4;$i++) {
        Canvas 128 128
        Rounded 2 2 124 124 16 $styles[$style][$i][0] $styles[$style][$i][1] 2
        Save "Buttons\btn_${style}_$($states[$i]).png" "${style} button / $($states[$i])" 0.25
    }
}
Canvas 128 128; Rounded 2 2 124 124 16 '#D9EFF2' ''; Save 'Buttons\btn_nav_selected.png' 'selected navigation row' 0.25
Canvas 8 64; $brush=[System.Drawing.SolidBrush]::new((Color '#147F8A'));$script:G.FillRectangle($brush,0,0,8,64);$brush.Dispose();Save 'Buttons\nav_selection_bar.png' 'selected navigation left edge'
foreach($panel in @(@('card','#FFFFFF','#DCE4EB'),@('subcard','#FFFFFF','#E1E7ED'),@('legend','#F8FBFD','#DCE4EB'))) {
    Canvas 128 128; Rounded 2 2 124 124 16 $panel[1] $panel[2]; Save "Panels\panel_$($panel[0]).png" 'stretchable panel' 0.25
}
foreach($surface in @(@('workspace','#F1F5F8'),@('sidebar','#F8FAFC'),@('header','#F8FAFC'),@('table_header','#F0F5F8'))) {
    Canvas 16 16;$script:G.Clear((Color $surface[1])); Save "Panels\bg_$($surface[0]).png" 'solid background'
}
Canvas 64 64;$script:G.Clear((Color '#DCE4EB'));Save 'Panels\divider.png' 'tintable divider (use height 1 logical px)'
Canvas 256 256
$pen=[System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(45,190,211,223),1)
for($i=0;$i -le 256;$i+=32){$script:G.DrawLine($pen,$i,0,$i,256);$script:G.DrawLine($pen,0,$i,256,$i)}
$pen.Dispose();Save 'Panels\technical_grid_tile.png' 'optional tileable preview grid'

# Original deterministic line icons, designed on a 64-unit grid and rendered at 128px.
$iconNames=@('home','document','history','results','settings','power','folder','people','fire','model','clock','smoke','route','exit','chevron_right','minimize','maximize','close')
$variants=@(@('slate','#526174'),@('teal','#147F8A'),@('white','#FFFFFF'))
foreach($variant in $variants) {
    foreach($name in $iconNames) {
        Canvas 128 128; $script:G.ScaleTransform(2,2)
        $script:P=[System.Drawing.Pen]::new((Color $variant[1]),2.8)
        $script:P.StartCap=[System.Drawing.Drawing2D.LineCap]::Round;$script:P.EndCap=[System.Drawing.Drawing2D.LineCap]::Round;$script:P.LineJoin=[System.Drawing.Drawing2D.LineJoin]::Round
        switch($name) {
            home {Poly @(10,29,32,10,54,29);Poly @(16,26,16,53,27,53,27,38,37,38,37,53,48,53,48,26)}
            document {Poly @(18,10,37,10,48,21,48,54,18,54) $true;Poly @(37,10,37,21,48,21);Line 25 29 41 29;Line 25 36 41 36;Line 25 43 37 43}
            history {Ellipse 10 10 44 44;Line 32 19 32 33;Line 32 33 43 39}
            results {Line 11 53 54 53;Poly @(17,46,17,33,24,33,24,46);Poly @(29,46,29,16,36,16,36,46);Poly @(41,46,41,26,48,26,48,46)}
            settings {
                $pts=@();for($j=0;$j -lt 32;$j++){$a=2*[Math]::PI*$j/32;$r=if(($j%4)-in @(1,2)){23}else{18};$pts+=32+$r*[Math]::Cos($a);$pts+=32+$r*[Math]::Sin($a)};Poly $pts $true;Ellipse 24 24 16 16
            }
            power {$script:G.DrawArc($script:P,12,13,40,40,-58,296);Line 32 8 32 29}
            folder {Poly @(10,49,10,17,26,17,31,23,53,23,53,29);Poly @(10,49,17,29,56,29,49,49) $true}
            people {Ellipse 25 12 14 14;Ellipse 9 19 10 10;Ellipse 45 19 10 10;$script:G.DrawArc($script:P,20,30,24,26,180,180);Line 20 43 20 50;Line 44 43 44 50;Line 20 50 44 50;$script:G.DrawArc($script:P,5,33,16,21,180,150);$script:G.DrawArc($script:P,43,33,16,21,210,150)}
            fire {
                $iconPath=[System.Drawing.Drawing2D.GraphicsPath]::new();$iconPath.AddBezier(32,9,38,20,49,24,48,38);$iconPath.AddBezier(48,38,48,57,17,57,16,39);$iconPath.AddBezier(16,39,14,30,22,26,23,20);$iconPath.AddBezier(23,20,24,28,29,27,32,9);$script:G.DrawPath($script:P,$iconPath);$iconPath.Dispose();Poly @(31,33,26,44,33,50,39,44,31,33)
            }
            model {Poly @(13,20,32,10,51,20,51,44,32,54,13,44) $true;Poly @(13,20,32,31,51,20);Line 32 31 32 54;Ellipse 28 27 8 8}
            clock {Ellipse 10 10 44 44;Line 32 19 32 33;Line 32 33 42 38}
            smoke {foreach($y in @(21,32,43)){$iconPath=[System.Drawing.Drawing2D.GraphicsPath]::new();$iconPath.AddBezier(11,$y,19,($y-12),25,($y-12),33,$y);$iconPath.AddBezier(33,$y,41,($y+12),47,($y+12),55,$y);$script:G.DrawPath($script:P,$iconPath);$iconPath.Dispose()}}
            route {$script:P.DashPattern=[single[]]@(3,2);Line 8 32 45 32;$script:P.DashStyle=[System.Drawing.Drawing2D.DashStyle]::Solid;Poly @(41,22,53,32,41,42)}
            exit {Poly @(10,54,10,10,38,10,38,22);Line 38 43 38 54;Line 10 54 38 54;Line 28 32 56 32;Poly @(47,23,56,32,47,41)}
            chevron_right {Poly @(25,16,41,32,25,48)}
            minimize {Line 16 39 48 39}
            maximize {Poly @(17,17,47,17,47,47,17,47) $true}
            close {Line 18 18 46 46;Line 46 18 18 46}
        }
        $script:P.Dispose(); Save "Icons\ic_${name}_$($variant[0]).png" "$name / $($variant[0])"
    }
}
foreach($dot in @(@('ready','#16996A'),@('warning','#E48B3B'),@('neutral','#8A98A7'))) {
    Canvas 64 64;$brush=[System.Drawing.SolidBrush]::new((Color $dot[1]));$script:G.FillEllipse($brush,8,8,48,48);$brush.Dispose();Save "Icons\status_$($dot[0]).png" 'status dot'
}
Canvas 512 160
$script:G.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$font=[System.Drawing.Font]::new('Segoe UI',108,[System.Drawing.FontStyle]::Bold,[System.Drawing.GraphicsUnit]::Pixel)
$brush=[System.Drawing.SolidBrush]::new((Color '#253448'));$script:G.DrawString('YUFS',$font,$brush,4,2);$brush.Dispose();$font.Dispose();Save 'Brand\logo_yufs.png' 'YUFS wordmark; optional alternative to UMG text'

$heroPath=Join-Path $OutputRoot 'Illustrations\building-preview.png'
if(Test-Path -LiteralPath $heroPath){
    $hero=[System.Drawing.Image]::FromFile($heroPath)
    $script:Manifest.Add([pscustomobject]@{file='Illustrations/building-preview.png';width=$hero.Width;height=$hero.Height;role='transparent building preview illustration';umg_draw_as='Image';box_margin=0})
    $hero.Dispose()
}
$script:Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputRoot 'asset-manifest.json') -Encoding utf8
# Contact sheet of every authored control, created directly from the source PNGs.
$files=@($script:Manifest | Where-Object {$_.file -notlike 'Brand/*' -and $_.file -notlike 'Illustrations/*'})
$cols=8;$cellW=240;$cellH=164;$rows=[int][Math]::Ceiling($files.Count/$cols)
Canvas ($cols*$cellW) (80+$rows*$cellH);$script:G.Clear((Color '#F1F5F8'))
$font=[System.Drawing.Font]::new('Segoe UI',13,[System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel)
$title=[System.Drawing.Font]::new('Segoe UI',26,[System.Drawing.FontStyle]::Bold,[System.Drawing.GraphicsUnit]::Pixel)
$brush=[System.Drawing.SolidBrush]::new((Color '#253448'));$script:G.DrawString('YUFS / Main Screen UI Assets',$title,$brush,24,20)
for($i=0;$i -lt $files.Count;$i++){
    $x=($i%$cols)*$cellW;$y=80+[int][Math]::Floor($i/$cols)*$cellH
    $dark=$files[$i].file -like '*_white.png'
    Rounded ($x+8) ($y+4) 224 116 10 $(if($dark){'#34465B'}else{'#FFFFFF'}) '#DCE4EB'
    $img=[System.Drawing.Image]::FromFile((Join-Path $OutputRoot $files[$i].file))
    if($files[$i].file -like 'Buttons/btn_*'){$script:G.DrawImage($img,[System.Drawing.Rectangle]::new(($x+24),($y+30),192,60))}
    else{$script:G.DrawImage($img,[System.Drawing.Rectangle]::new(($x+88),($y+24),64,64))}
    $img.Dispose();$label=[System.IO.Path]::GetFileNameWithoutExtension($files[$i].file);$script:G.DrawString($label,$font,$brush,($x+12),($y+127))
}
$font.Dispose();$title.Dispose();$brush.Dispose();Save 'Preview\ui-assets-contact-sheet.png' 'preview only; do not import'
Write-Output ('Created {0} textures plus contact sheet.' -f ($script:Manifest.Count-1))

