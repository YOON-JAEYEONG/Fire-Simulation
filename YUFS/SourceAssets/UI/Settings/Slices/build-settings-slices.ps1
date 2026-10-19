param([string]$Root=$PSScriptRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$uiRoot=Split-Path (Split-Path $Root -Parent) -Parent
$source=[System.Drawing.Bitmap]::new([string](Join-Path (Split-Path $Root -Parent) 'yufs-settings-v1.png'))
if($source.Width -ne 1672 -or $source.Height -ne 941){throw 'Crop coordinates require original image size 1672x941.'}
foreach($dir in @('Cards_Reference','StaticParts','Buttons_Reference','Skins','Preview')){New-Item -ItemType Directory -Path (Join-Path $Root $dir) -Force | Out-Null}
$crops=@(
    @('Cards_Reference\card_display.png',270,188,898,271,'reference only: contains fixed resolution and selected mode'),
    @('Cards_Reference\card_quality.png',270,473,898,198,'reference only: contains fixed selected quality'),
    @('Cards_Reference\card_debug.png',270,685,898,194,'reference only: contains fixed checkbox'),
    @('StaticParts\card_settings_guide.png',1181,188,465,681,'complete static right guidance card; ready for Image'),
    @('StaticParts\heading_display.png',294,207,260,70,'display title and illustration'),
    @('StaticParts\heading_quality.png',294,493,260,70,'quality title and illustration'),
    @('StaticParts\heading_debug.png',294,706,220,70,'debug title and illustration'),
    @('StaticParts\icon_display.png',294,207,68,70,'display illustration'),
    @('StaticParts\icon_quality.png',294,493,68,70,'quality illustration'),
    @('StaticParts\icon_debug.png',294,706,68,70,'debug illustration'),
    @('StaticParts\hint_display.png',389,421,756,28,'display help text'),
    @('StaticParts\hint_quality.png',389,632,756,27,'quality help text'),
    @('StaticParts\hint_debug_immediate.png',389,824,756,43,'immediate debug update info strip'),
    @('StaticParts\guide_display.png',1212,277,409,99,'right guide display item'),
    @('StaticParts\guide_quality.png',1212,436,409,99,'right guide quality item'),
    @('StaticParts\guide_apply.png',1212,596,409,102,'right guide apply item'),
    @('StaticParts\guide_cancel_note.png',1204,733,420,72,'right guide cancel note'),
    @('Buttons_Reference\btn_fullscreen.png',523,335,198,56,'baked-label fullscreen example'),
    @('Buttons_Reference\btn_borderless_selected.png',732,335,206,56,'baked-label selected borderless example'),
    @('Buttons_Reference\btn_windowed.png',950,335,195,56,'baked-label windowed example'),
    @('Buttons_Reference\btn_quality_low.png',389,549,180,53,'baked-label low example'),
    @('Buttons_Reference\btn_quality_medium.png',580,549,180,53,'baked-label medium example'),
    @('Buttons_Reference\btn_quality_high_selected.png',772,549,180,53,'baked-label selected high example'),
    @('Buttons_Reference\btn_quality_epic.png',964,549,180,53,'baked-label epic example'),
    @('Buttons_Reference\btn_back.png',1191,879,140,39,'baked-label back button'),
    @('Buttons_Reference\btn_cancel.png',1342,879,135,39,'baked-label cancel button'),
    @('Buttons_Reference\btn_apply.png',1487,879,159,39,'baked-label apply button')
)
$manifest=[System.Collections.Generic.List[object]]::new()
foreach($item in $crops){
    $rect=[System.Drawing.Rectangle]::new($item[1],$item[2],$item[3],$item[4]);if($rect.Right -gt $source.Width -or $rect.Bottom -gt $source.Height){throw ('Invalid crop '+$item[0])}
    $image=$source.Clone($rect,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb);$image.Save((Join-Path $Root $item[0]),[System.Drawing.Imaging.ImageFormat]::Png);$image.Dispose()
    $manifest.Add([pscustomobject]@{file=$item[0].Replace('\','/');kind='original_pixel_crop';x=$item[1];y=$item[2];width=$item[3];height=$item[4];role=$item[5];drawAs='Image';includesBackground=$true})
}
# The header illustrations extend lower than their title text. Assemble two exact crops
# on transparent canvas so neighboring dropdowns/buttons do not enter the header PNGs.
$headingParts=@(
    @('StaticParts\heading_display.png',260,70,207,386,211,170,36),
    @('StaticParts\heading_quality.png',260,70,493,386,498,168,36),
    @('StaticParts\heading_debug.png',220,70,706,386,711,128,36)
)
foreach($head in $headingParts){
    $header=[System.Drawing.Bitmap]::new($head[1],$head[2],[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $hg=[System.Drawing.Graphics]::FromImage($header);$hg.Clear([System.Drawing.Color]::Transparent)
    $icon=$source.Clone([System.Drawing.Rectangle]::new(294,$head[3],68,70),[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $title=$source.Clone([System.Drawing.Rectangle]::new($head[4],$head[5],$head[6],$head[7]),[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $hg.DrawImageUnscaled($icon,0,0);$hg.DrawImageUnscaled($title,($head[4]-294),($head[5]-$head[3]))
    $header.Save((Join-Path $Root $head[0]),[System.Drawing.Imaging.ImageFormat]::Png)
    $hg.Dispose();$header.Dispose();$icon.Dispose();$title.Dispose()
    $entry=$manifest | Where-Object {$_.file -eq $head[0].Replace('\','/')}
    $entry.kind='assembled_from_original_crops'
}
$source.Dispose()
$copies=@(
    @('MainScreen\Panels\panel_card.png','panel_card.png'),
    @('MainScreen\Panels\bg_workspace.png','bg_workspace.png'),
    @('MainScreen\Panels\bg_header.png','bg_header.png'),
    @('MainScreen\Panels\bg_sidebar.png','bg_sidebar.png'),
    @('MainScreen\Panels\divider.png','divider.png'),
    @('MainScreen\Buttons\btn_primary_normal.png','btn_primary_normal.png'),
    @('MainScreen\Buttons\btn_primary_hover.png','btn_primary_hover.png'),
    @('MainScreen\Buttons\btn_primary_pressed.png','btn_primary_pressed.png'),
    @('MainScreen\Buttons\btn_primary_disabled.png','btn_primary_disabled.png'),
    @('MainScreen\Buttons\btn_secondary_normal.png','btn_secondary_normal.png'),
    @('MainScreen\Buttons\btn_secondary_hover.png','btn_secondary_hover.png'),
    @('MainScreen\Buttons\btn_secondary_pressed.png','btn_secondary_pressed.png'),
    @('MainScreen\Buttons\btn_secondary_disabled.png','btn_secondary_disabled.png'),
    @('ScenarioSetup\Controls\field_normal.png','field_normal.png'),
    @('ScenarioSetup\Controls\field_hover.png','field_hover.png'),
    @('ScenarioSetup\Controls\field_focus.png','field_focus.png'),
    @('ScenarioSetup\Controls\field_disabled.png','field_disabled.png'),
    @('ScenarioSetup\Controls\chevron_down_slate.png','chevron_down_slate.png'),
    @('ScenarioSetup\Controls\check_unchecked_normal.png','check_unchecked_normal.png'),
    @('ScenarioSetup\Controls\check_unchecked_hover.png','check_unchecked_hover.png'),
    @('ScenarioSetup\Controls\check_unchecked_pressed.png','check_unchecked_pressed.png'),
    @('ScenarioSetup\Controls\check_checked_normal.png','check_checked_normal.png'),
    @('ScenarioSetup\Controls\check_checked_hover.png','check_checked_hover.png'),
    @('ScenarioSetup\Controls\check_checked_pressed.png','check_checked_pressed.png')
)
foreach($copy in $copies){
    $out=Join-Path $Root ('Skins\'+$copy[1]);Copy-Item -LiteralPath (Join-Path $uiRoot $copy[0]) -Destination $out
    $img=[System.Drawing.Image]::FromFile($out);$box=$copy[1] -match '^(panel_|btn_|field_)'
    $manifest.Add([pscustomobject]@{file=('Skins/'+$copy[1]);kind='reused_existing_asset';width=$img.Width;height=$img.Height;drawAs=$(if($box){'Box'}else{'Image'});margin=$(if($box){0.25}else{0})});$img.Dispose()
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Root 'asset-manifest.json') -Encoding utf8
# Visual inventory: main static pieces and original card crops.
$sheet=[System.Drawing.Bitmap]::new(1600,1200,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g=[System.Drawing.Graphics]::FromImage($sheet);$g.Clear([System.Drawing.ColorTranslator]::FromHtml('#F1F5F8'));$g.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$font=[System.Drawing.Font]::new('Segoe UI',20,[System.Drawing.FontStyle]::Bold,[System.Drawing.GraphicsUnit]::Pixel);$brush=[System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#253448'))
$g.DrawString('YUFS / Settings UI slices',$font,$brush,24,18)
$tiles=@(@('Cards_Reference\card_display.png',24,90,960,290),@('Cards_Reference\card_quality.png',24,430,960,212),@('Cards_Reference\card_debug.png',24,692,960,207),@('StaticParts\card_settings_guide.png',1030,90,465,681),@('StaticParts\heading_display.png',24,995,260,70),@('StaticParts\heading_quality.png',320,995,260,70),@('StaticParts\heading_debug.png',616,995,220,70),@('StaticParts\hint_debug_immediate.png',24,1110,756,43))
foreach($tile in $tiles){$img=[System.Drawing.Image]::FromFile((Join-Path $Root $tile[0]));$g.DrawImage($img,[System.Drawing.Rectangle]::new($tile[1],$tile[2],$tile[3],$tile[4]));$img.Dispose();$g.DrawString([System.IO.Path]::GetFileName($tile[0]),$font,$brush,$tile[1],($tile[2]-29))}
$sheet.Save((Join-Path $Root 'Preview\settings-slices-contact-sheet.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$font.Dispose();$brush.Dispose();$g.Dispose();$sheet.Dispose()
[pscustomobject]@{croppedPngCount=$crops.Count;reusedSkinCount=$copies.Count;totalAssetCount=$manifest.Count;sourceSize=@(1672,941);cropsResampled=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'validation.json') -Encoding utf8
Write-Output ('Saved {0} source crops and {1} reusable skins.' -f $crops.Count,$copies.Count)
