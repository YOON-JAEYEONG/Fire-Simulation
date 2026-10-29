param([string]$Root=$PSScriptRoot)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$sourcePath=Join-Path (Split-Path $Root -Parent) 'Concepts\yufs-home-guide-v1.png'
$source=[System.Drawing.Bitmap]::new([string]$sourcePath)
if($source.Width -ne 1672 -or $source.Height -ne 941){$source.Dispose();throw 'Expected the original 1672x941 home-guide image. Crop coordinates require review for another source.'}
foreach($folder in @('Cards','Parts','Buttons','Preview')){New-Item -ItemType Directory -Path (Join-Path $Root $folder) -Force | Out-Null}
# Exact source-pixel crops. No resampling, AI redraw, color replacement or text regeneration.
$specs=@(
    @('Cards\home_how_to_use.png',270,190,1001,493,'Image_PreviewCard: usage guide including CTA', $true),
    @('Cards\home_before_start.png',1287,190,364,493,'Image_SettingsCard: preparation checklist', $true),
    @('Cards\home_time_example.png',270,699,652,191,'Image_RecentCard: time-reference example', $true),
    @('Cards\home_results_guide.png',938,699,713,191,'Image_AnalysisCard: result-reading guide', $true),
    @('Parts\how_to_content.png',289,208,967,373,'usage guide body without CTA strip', $false),
    @('Parts\step_01_setup.png',296,289,289,287,'step 1 text and illustration', $false),
    @('Parts\step_02_run.png',649,289,303,287,'step 2 text and illustration', $false),
    @('Parts\step_03_results.png',1023,289,234,287,'step 3 text and illustration', $false),
    @('Parts\illustration_setup.png',303,438,260,134,'setup illustration', $false),
    @('Parts\illustration_evacuation.png',670,429,279,139,'evacuation illustration', $false),
    @('Parts\illustration_results.png',1053,420,174,157,'results illustration', $false),
    @('Parts\start_hint_and_button.png',289,596,966,73,'complete hint strip including CTA', $false),
    @('Parts\start_hint.png',303,610,430,49,'first-experiment hint icon and text', $false),
    @('Parts\check_map.png',1311,269,317,77,'map checklist item', $false),
    @('Parts\check_policy.png',1311,361,317,79,'policy checklist item', $false),
    @('Parts\check_communication.png',1311,455,317,78,'communication checklist item', $false),
    @('Parts\check_recording.png',1311,549,317,77,'recording checklist item', $false),
    @('Parts\time_diagram.png',297,757,600,70,'example time diagram', $false),
    @('Parts\time_example_caption.png',297,843,595,35,'time example caption', $false),
    @('Parts\result_evacuation_rate.png',959,757,219,77,'evacuation rate explanatory item', $false),
    @('Parts\result_average_time.png',1182,757,214,77,'average evacuation time explanatory item', $false),
    @('Parts\result_people.png',1407,757,226,77,'evacuated people explanatory item', $false),
    @('Parts\results_hint.png',959,846,677,34,'results comparison hint', $false),
    @('Buttons\btn_start_simulation.png',991,604,256,59,'baked-label CTA image; connect UMG Button separately', $false)
)
$manifest=[System.Collections.Generic.List[object]]::new()
foreach($s in $specs){
    $rect=[System.Drawing.Rectangle]::new($s[1],$s[2],$s[3],$s[4])
    if($rect.Right -gt $source.Width -or $rect.Bottom -gt $source.Height){throw ('Crop exceeds source: '+$s[0])}
    $crop=$source.Clone($rect,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $crop.Save((Join-Path $Root $s[0]),[System.Drawing.Imaging.ImageFormat]::Png);$crop.Dispose()
    $scale=1920.0/$source.Width
    $manifest.Add([pscustomobject]@{file=$s[0].Replace('\','/');sourceX=$s[1];sourceY=$s[2];width=$s[3];height=$s[4];role=$s[5];completeCard=$s[6];drawAs='Image';includesText=$true;sourcePixelPreserved=$true;reference1920=[pscustomobject]@{x=[Math]::Round($s[1]*$scale,2);y=[Math]::Round($s[2]*$scale,2);width=[Math]::Round($s[3]*$scale,2);height=[Math]::Round($s[4]*$scale,2)}})
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Root 'slice-manifest.json') -Encoding utf8

# Four-card contact sheet. This file is a preview; individual deliverables above remain at native resolution.
$sheet=[System.Drawing.Bitmap]::new(1600,1150,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g=[System.Drawing.Graphics]::FromImage($sheet);$g.Clear([System.Drawing.ColorTranslator]::FromHtml('#F1F5F8'))
$g.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$font=[System.Drawing.Font]::new('Segoe UI',22,[System.Drawing.FontStyle]::Bold,[System.Drawing.GraphicsUnit]::Pixel)
$brush=[System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#253448'))
$g.DrawString('YUFS / Home guide - ready-to-use image slices',$font,$brush,30,20)
$places=@(@(0,30,100,1130,558),@(1,1190,100,380,515),@(2,30,760,730,214),@(3,790,760,780,209))
foreach($p in $places){
    $s=$specs[$p[0]];$img=[System.Drawing.Image]::FromFile((Join-Path $Root $s[0]));$g.DrawImage($img,[System.Drawing.Rectangle]::new($p[1],$p[2],$p[3],$p[4]));$img.Dispose()
    $g.DrawString([System.IO.Path]::GetFileName($s[0]),$font,$brush,$p[1],($p[2]-34))
}
$g.DrawString('Cards = whole panels with text | Parts = individual instructional images | Buttons = CTA',$font,$brush,30,1074)
$sheet.Save((Join-Path $Root 'Preview\cards-contact-sheet.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$font.Dispose();$brush.Dispose();$g.Dispose();$sheet.Dispose()

# Verify every cropped pixel matches its original location, including alpha.
$verified=0
foreach($item in $manifest){
    $check=[System.Drawing.Bitmap]::new([string](Join-Path $Root $item.file))
    for($y=0;$y -lt $item.height;$y++){for($x=0;$x -lt $item.width;$x++){
        if($check.GetPixel($x,$y).ToArgb() -ne $source.GetPixel(($item.sourceX+$x),($item.sourceY+$y)).ToArgb()){
            $check.Dispose();$source.Dispose();throw ('Pixel mismatch: '+$item.file)
        }
    }}
    $check.Dispose();$verified++
}
$source.Dispose()
[pscustomobject]@{cropCount=$verified;allPixelsMatchOriginal=$true;sourceSize=@(1672,941);resampled=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'validation.json') -Encoding utf8
Write-Output ('Saved and pixel-verified {0} cropped PNGs.' -f $verified)
