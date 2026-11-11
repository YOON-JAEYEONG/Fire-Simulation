param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$ExpectedCommit,
    [string]$ExpectedBranch,
    [switch]$SkipHash,
    [switch]$RequireBuild
)
$ErrorActionPreference = 'Stop'
try {
    $checkoutRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
    $projectFile = Join-Path $checkoutRoot 'YUFS/YUFS.uproject'
    $projectInfo = Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json
    if ($projectInfo.EngineAssociation -ne '5.7') { throw 'Expected Unreal Engine 5.7 project.' }
    $branchName = & git -c "safe.directory=$checkoutRoot" -C $checkoutRoot branch --show-current
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the checkout branch.' }
    $commitId = & git -c "safe.directory=$checkoutRoot" -C $checkoutRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the checkout commit.' }
    if ($ExpectedCommit -and $commitId -ne $ExpectedCommit) { throw "Wrong commit: $commitId" }
    if ($ExpectedBranch -and $branchName -ne $ExpectedBranch) { throw "Wrong branch: $branchName" }
    $contentRoot = Join-Path $checkoutRoot 'YUFS/Content'
    $manifest = Get-Content -LiteralPath (Join-Path $checkoutRoot 'YUFS/Docs/CLONE_DATA_SHA256.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$manifest.binaryData -or !$manifest.unrealPackages) { throw 'Checkout manifest is missing BIN or Unreal package entries.' }
    foreach ($entry in $manifest.binaryData) {
        $dataPath = Join-Path $contentRoot $entry.file
        $dataFile = Get-Item -LiteralPath $dataPath -ErrorAction SilentlyContinue
        if (!$dataFile -or $dataFile.Length -ne $entry.bytes) { throw "Missing/incomplete LFS data: $($entry.file). Run git lfs pull." }
        if (!$SkipHash) {
            $hashStream = [System.IO.File]::OpenRead($dataPath)
            $hasher = [System.Security.Cryptography.SHA256]::Create()
            try { $actualHash = [BitConverter]::ToString($hasher.ComputeHash($hashStream)).Replace('-','') }
            finally { $hashStream.Dispose(); $hasher.Dispose() }
            if ($actualHash -ne $entry.sha256) {
                throw "Wrong SHA256: $($entry.file). Run git lfs pull and verify the download."
            }
        }
    }
    foreach ($relativeAsset in $manifest.unrealPackages) {
        $assetPath = Join-Path $contentRoot $relativeAsset
        if (!(Test-Path -LiteralPath $assetPath -PathType Leaf)) { throw "Missing Unreal package: $relativeAsset" }
        $assetStream = [System.IO.File]::OpenRead($assetPath)
        try {
            $packageTag = New-Object byte[] 4
            if ($assetStream.Read($packageTag,0,4) -ne 4 -or [BitConverter]::ToUInt32($packageTag,0) -ne 0x9E2A83C1L) {
                throw "Not an Unreal package (possibly an LFS pointer): $relativeAsset"
            }
        } finally { $assetStream.Dispose() }
    }
    if ($RequireBuild) {
        $moduleFile = Get-Item -LiteralPath (Join-Path $checkoutRoot 'YUFS/Binaries/Win64/UnrealEditor-YUFS.dll')
        $engineRoot = if ($env:YUFS_ENGINE_ROOT) { $env:YUFS_ENGINE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
        $engineModules = Get-Content -LiteralPath (Join-Path $engineRoot 'Engine/Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
        $localModules = Get-Content -LiteralPath (Join-Path $checkoutRoot 'YUFS/Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
        if (!$localModules.BuildId -or $localModules.BuildId -ne $engineModules.BuildId) {
            throw 'Local DLL was built for another engine. Run Build-And-Launch-YUFS-Safe.cmd.'
        }
        $latestSource = Get-ChildItem -LiteralPath (Join-Path $checkoutRoot 'YUFS/Source') -Recurse -File |
            Where-Object { $_.Extension -in '.h','.cpp','.cs' } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if ($latestSource.LastWriteTimeUtc -gt $moduleFile.LastWriteTimeUtc) {
            throw 'Source is newer than the local DLL. Run Build-And-Launch-YUFS-Safe.cmd first.'
        }
    }
    Write-Output "Project: $projectFile"
    Write-Output "Branch: $branchName"
    Write-Output "Commit: $commitId"
    Write-Output 'PASS: required maps, visual assets and BIN files for the final IT and legacy Main maps are present in THIS checkout.'
    exit 0
} catch {
    Write-Error -ErrorAction Continue "Checkout validation failed: $($_.Exception.Message)"
    exit 1
}
