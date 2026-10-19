param([string]$Root=$PSScriptRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
foreach($folder in @('Controls','Shared','Preview','Concepts')){New-Item -ItemType Directory -Path (Join-Path $Root $folder) -Force | Out-Null}
$items=[System.Collections.Generic.List[object]]::new()
function Color([string]$Hex){[System.Drawing.ColorTranslator]::FromHtml($Hex)}
function BeginImage([int]$Width,[int]$Height){
    $script:Bitmap=[System.Drawing.Bitmap]::new($Width,$Height,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $script:Graphics=[System.Drawing.Graphics]::FromImage($script:Bitmap)
    $script:Graphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $script:Graphics.Clear([System.Drawing.Color]::Transparent)
}
function RoundRect([single]$X,[single]$Y,[single]$W,[single]$H,[single]$R,[string]$Fill,[string]$Stroke){
    $path=[System.Drawing.Drawing2D.GraphicsPath]::new();$d=2*$R
    $path.AddArc($X,$Y,$d,$d,180,90);$path.AddArc($X+$W-$d,$Y,$d,$d,270,90);$path.AddArc($X+$W-$d,$Y+$H-$d,$d,$d,0,90);$path.AddArc($X,$Y+$H-$d,$d,$d,90,90);$path.CloseFigure()
    $brush=[System.Drawing.SolidBrush]::new((Color $Fill));$script:Graphics.FillPath($brush,$path);$brush.Dispose()
    $pen=[System.Drawing.Pen]::new((Color $Stroke),2);$script:Graphics.DrawPath($pen,$path);$pen.Dispose();$path.Dispose()
}
function EndImage([string]$File,[string]$Role,[double]$Margin=0){
    $script:Bitmap.Save((Join-Path $Root $File),[System.Drawing.Imaging.ImageFormat]::Png)
    $items.Add([pscustomobject]@{file=$File.Replace('\','/');width=$script:Bitmap.Width;height=$script:Bitmap.Height;role=$Role;drawAs=$(if($Margin -gt 0){'Box'}else{'Image'});margin=$Margin})
    $script:Graphics.Dispose();$script:Bitmap.Dispose()
}
$states=@('normal','hover','pressed','disabled')
foreach($checked in @($false,$true)){
    foreach($state in $states){
        BeginImage 64 64
        $fill='#FFFFFF';$border='#A6B5C2';$mark='#FFFFFF'
        if($checked){$fill='#147F8A';$border=$fill}
        switch($state){
            hover {if($checked){$fill='#1995A1';$border=$fill}else{$fill='#EAF6F7';$border='#147F8A'}}
            pressed {if($checked){$fill='#106570';$border=$fill}else{$fill='#D5ECEE';$border='#106570'}}
            disabled {$fill='#EEF2F5';$border='#DCE4EB';$mark='#A0AFBC'}
        }
        RoundRect 5 5 54 54 10 $fill $border
        if($checked){$pen=[System.Drawing.Pen]::new((Color $mark),5);$pen.StartCap=[System.Drawing.Drawing2D.LineCap]::Round;$pen.EndCap=[System.Drawing.Drawing2D.LineCap]::Round;$script:Graphics.DrawLine($pen,18,32,28,42);$script:Graphics.DrawLine($pen,28,42,47,21);$pen.Dispose()}
        $kind=if($checked){'checked'}else{'unchecked'};EndImage "Controls\check_${kind}_${state}.png" "CheckBox $kind / $state"
    }
}
foreach($style in @(@('normal','#FFFFFF','#CCD7E1'),@('hover','#FFFFFF','#8EAFB8'),@('focus','#F7FCFC','#147F8A'),@('disabled','#F2F5F7','#E1E7ED'))){
    BeginImage 128 128;RoundRect 2 2 124 124 16 $style[1] $style[2];EndImage "Controls\field_$($style[0]).png" "ComboBox/SpinBox field / $($style[0])" 0.25
}
foreach($arrow in @('up','down','left')){
    foreach($variant in @(@('slate','#526174'),@('muted','#A0AFBC'))){
        BeginImage 64 64;$pen=[System.Drawing.Pen]::new((Color $variant[1]),4);$pen.StartCap=[System.Drawing.Drawing2D.LineCap]::Round;$pen.EndCap=[System.Drawing.Drawing2D.LineCap]::Round
        switch($arrow){up {$script:Graphics.DrawLine($pen,19,39,32,26);$script:Graphics.DrawLine($pen,32,26,45,39)} down {$script:Graphics.DrawLine($pen,19,26,32,39);$script:Graphics.DrawLine($pen,32,39,45,26)} left {$script:Graphics.DrawLine($pen,39,18,25,32);$script:Graphics.DrawLine($pen,25,32,39,46)}}
        $pen.Dispose();EndImage "Controls\chevron_${arrow}_$($variant[0]).png" "$arrow indicator / $($variant[0])"
    }
}
$main=Join-Path (Split-Path $Root -Parent) 'MainScreen'
$shared=@('Panels\bg_workspace.png','Panels\bg_header.png','Panels\bg_sidebar.png','Panels\panel_card.png','Panels\panel_subcard.png','Panels\panel_legend.png','Panels\divider.png','Buttons\btn_primary_normal.png','Buttons\btn_primary_hover.png','Buttons\btn_primary_pressed.png','Buttons\btn_primary_disabled.png','Buttons\btn_secondary_normal.png','Buttons\btn_secondary_hover.png','Buttons\btn_secondary_pressed.png','Buttons\btn_secondary_disabled.png','Buttons\btn_nav_selected.png','Icons\ic_document_teal.png','Icons\ic_results_slate.png','Icons\ic_settings_slate.png','Icons\ic_home_slate.png','Icons\ic_fire_teal.png','Icons\ic_clock_teal.png','Icons\status_ready.png','Brand\logo_yufs.png')
foreach($relative in $shared){
    $name=[System.IO.Path]::GetFileName($relative);Copy-Item -LiteralPath (Join-Path $main $relative) -Destination (Join-Path $Root "Shared\$name")
    $img=[System.Drawing.Image]::FromFile((Join-Path $Root "Shared\$name"));$box=$name -match '^(panel_|btn_)'
    $items.Add([pscustomobject]@{file="Shared/$name";width=$img.Width;height=$img.Height;role='reused main screen asset';drawAs=$(if($box){'Box'}else{'Image'});margin=$(if($box){0.25}else{0})});$img.Dispose()
}
$items | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Root 'asset-manifest.json') -Encoding utf8
BeginImage 1440 720;$script:Graphics.Clear((Color '#F1F5F8'));$script:Graphics.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$font=[System.Drawing.Font]::new('Segoe UI',15,[System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel);$title=[System.Drawing.Font]::new('Segoe UI',28,[System.Drawing.FontStyle]::Bold,[System.Drawing.GraphicsUnit]::Pixel);$brush=[System.Drawing.SolidBrush]::new((Color '#253448'))
$script:Graphics.DrawString('YUFS / Scenario Setup Controls',$title,$brush,28,22)
$controls=@($items | Where-Object {$_.file -like 'Controls/*'})
for($i=0;$i -lt $controls.Count;$i++){
    $x=24+($i%6)*236;$y=86+[int][Math]::Floor($i/6)*202
    RoundRect $x $y 220 178 12 '#FFFFFF' '#DCE4EB'
    $img=[System.Drawing.Image]::FromFile((Join-Path $Root $controls[$i].file));$size=if($controls[$i].file -like '*field_*'){84}else{64};$script:Graphics.DrawImage($img,[System.Drawing.Rectangle]::new(($x+68),($y+20),$size,$size));$img.Dispose()
    $name=[System.IO.Path]::GetFileNameWithoutExtension($controls[$i].file).Replace('_',"`n");$script:Graphics.DrawString($name,$font,$brush,($x+14),($y+112))
}
$font.Dispose();$title.Dispose();$brush.Dispose();$script:Bitmap.Save((Join-Path $Root 'Preview\scenario-controls.png'),[System.Drawing.Imaging.ImageFormat]::Png);$script:Graphics.Dispose();$script:Bitmap.Dispose()
Write-Output ('Saved {0} UI textures and controls preview.' -f $items.Count)
