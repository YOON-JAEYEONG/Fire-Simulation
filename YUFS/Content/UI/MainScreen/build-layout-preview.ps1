param([string]$Root=$PSScriptRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$bitmap=[System.Drawing.Bitmap]::new(1920,1080,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics=[System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$graphics.Clear([System.Drawing.ColorTranslator]::FromHtml('#F1F5F8'))
function Texture([string]$File,[int]$X,[int]$Y,[int]$W,[int]$H,[bool]$Box=$false){
    $image=[System.Drawing.Image]::FromFile((Join-Path $Root $File))
    if(!$Box){$graphics.DrawImage($image,[System.Drawing.Rectangle]::new($X,$Y,$W,$H))}
    else {
        $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::Half
        # Match a 0.25 UMG brush margin: source 32px -> 16 logical px at this preview scale.
        $sourceX=@(0,32,96,128);$sourceY=@(0,32,96,128)
        $destX=@($X,($X+16),($X+$W-16),($X+$W));$destY=@($Y,($Y+16),($Y+$H-16),($Y+$H))
        for($row=0;$row -lt 3;$row++){for($col=0;$col -lt 3;$col++){
            $dest=[System.Drawing.Rectangle]::new($destX[$col],$destY[$row],($destX[$col+1]-$destX[$col]),($destY[$row+1]-$destY[$row]))
            $graphics.DrawImage($image,$dest,$sourceX[$col],$sourceY[$row],($sourceX[$col+1]-$sourceX[$col]),($sourceY[$row+1]-$sourceY[$row]),[System.Drawing.GraphicsUnit]::Pixel)
        }}
        $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    };$image.Dispose()
}
function Text([string]$Value,[int]$X,[int]$Y,[int]$Size=22,[string]$Hex='#253448',[bool]$Bold=$false){
    $font=[System.Drawing.Font]::new('Malgun Gothic',$Size,$(if($Bold){[System.Drawing.FontStyle]::Bold}else{[System.Drawing.FontStyle]::Regular}),[System.Drawing.GraphicsUnit]::Pixel)
    $brush=[System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($Hex))
    $graphics.DrawString($Value,$font,$brush,$X,$Y);$font.Dispose();$brush.Dispose()
}
function Icon([string]$Name,[int]$X,[int]$Y,[string]$Variant='slate',[int]$Size=32){Texture "Icons\ic_${Name}_${Variant}.png" $X $Y $Size $Size}
Texture 'Panels\bg_header.png' 0 0 1920 72
Text 'YUFS' 32 6 42 '#253448' $true;Texture 'Panels\divider.png' 182 18 1 38
Text '화재 · 대피 시뮬레이션' 206 24 23 '#253448' $true
Texture 'Icons\status_ready.png' 1690 27 18 18;Text '실험 준비' 1716 22 21
Texture 'Panels\divider.png' 0 71 1920 1
Texture 'Panels\bg_sidebar.png' 0 72 280 968;Texture 'Panels\divider.png' 279 72 1 968
$menus=@(@('home','실험 개요'),@('document','새 시뮬레이션'),@('history','최근 시나리오'),@('results','실험 결과'),@('settings','환경 설정'))
Texture 'Buttons\btn_nav_selected.png' 6 112 264 66 $true;Texture 'Buttons\nav_selection_bar.png' 0 112 5 66
for($i=0;$i -lt $menus.Count;$i++){$y=126+$i*86;$color=if($i-eq 0){'teal'}else{'slate'};Icon $menus[$i][0] 31 $y $color;Text $menus[$i][1] 82 ($y+2) 22 $(if($i-eq 0){'#147F8A'}else{'#526174'}) ($i-eq 0)}
Texture 'Panels\divider.png' 20 955 240 1;Icon 'power' 31 982;Text '프로그램 종료' 82 984
Text '실험 워크스페이스' 320 106 40 '#253448' $true
Text '시나리오를 구성하고 대피 행동을 분석합니다' 322 162 23 '#526174'
Texture 'Panels\panel_card.png' 310 218 1150 580 $true
Text '시나리오 미리보기' 340 241 26 '#253448' $true
Texture 'Illustrations\building-preview.png' 510 283 750 500
Texture 'Panels\panel_legend.png' 1250 631 180 144 $true
Icon 'route' 1263 647 'teal' 28;Text '대피 경로' 1302 647 19
Icon 'fire' 1263 688 'slate' 28;Text '발화 지점' 1302 688 19
Icon 'exit' 1263 729 'teal' 28;Text '출구' 1302 729 19
Texture 'Panels\panel_card.png' 1478 218 418 580 $true
Text '실험 설정' 1510 243 26 '#253448' $true
$settings=@(@('document','시나리오','실내 화재'),@('people','참여 인원','60명'),@('fire','발화 조건','단일 발화'),@('model','행동 모델','PADM + ML'))
for($i=0;$i -lt 4;$i++){$y=316+$i*77;Icon $settings[$i][0] 1510 $y;Text $settings[$i][1] 1556 ($y+3) 21 '#526174';Text $settings[$i][2] 1730 ($y+3) 20 '#253448' $true;Texture 'Panels\divider.png' 1510 ($y+54) 354 1}
Texture 'Buttons\btn_primary_normal.png' 1510 643 354 62 $true;Text '새 실험 구성' 1611 661 23 '#FFFFFF' $true
Texture 'Buttons\btn_secondary_normal.png' 1510 721 354 56 $true;Icon 'folder' 1545 735 'teal' 28;Text '최근 실험 불러오기' 1582 735 22 '#147F8A' $true
Texture 'Panels\panel_card.png' 310 819 750 202 $true;Text '최근 실험' 340 838 25 '#253448' $true
Texture 'Panels\bg_table_header.png' 338 883 694 40
Text '시나리오' 350 890 17 '#526174';Text '참여 인원' 810 890 17 '#526174';Text '발화 조건' 922 890 17 '#526174'
Icon 'document' 347 947 'slate' 26;Text '실내 화재 · 대피 분석' 383 949 20;Text '60명' 824 949 20;Text '단일 발화' 922 949 19
Texture 'Panels\panel_card.png' 1078 819 818 202 $true;Text '분석 항목' 1108 838 25 '#253448' $true
$analysis=@(@('clock','대피 시간','전체 대피 시간'),@('smoke','연기 노출','노출 시간 및 인원'),@('people','행동 변화','대피 경로 및 군집'))
for($i=0;$i -lt 3;$i++){$x=1105+$i*257;Texture 'Panels\panel_subcard.png' $x 885 244 110 $true;Icon $analysis[$i][0] ($x+14) 902 'teal' 30;Text $analysis[$i][1] ($x+52) 903 20 '#253448' $true;Text $analysis[$i][2] ($x+18) 953 17 '#526174'}
Texture 'Panels\bg_header.png' 0 1040 1920 40;Texture 'Panels\divider.png' 0 1040 1920 1;Texture 'Icons\status_ready.png' 26 1051 17 17;Text '준비됨' 52 1048 18 '#526174'
$bitmap.Save((Join-Path $Root 'Preview\main-screen-assembled.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose();$bitmap.Dispose()
Write-Output 'Saved assembled preview (1920x1080).'
