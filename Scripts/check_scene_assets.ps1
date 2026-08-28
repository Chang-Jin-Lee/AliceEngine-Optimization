# 씬이 참조하는 파일을 재귀로 따라가며 저장소에 추적되고 있는지 확인한다.
#
# 씬 -> .fbxasset -> source_fbx / materials -> .mat -> 텍스처 순서로 훑는다.
# 클론한 사람이 씬을 열었을 때 무엇이 비는지 미리 알기 위한 것이다.
#
# 사용법:
#   pwsh -File Scripts\check_scene_assets.ps1
#   pwsh -File Scripts\check_scene_assets.ps1 -Scene 'Assets\Scenes\#01PrototypeMap.scene'
#
# 종료 코드: 미추적이 없으면 0, 있으면 1.

param(
    [string]$Scene = 'Assets\Scenes\Bench\EcsStress5000.scene'
)

if (-not (Test-Path $Scene)) { Write-Output "씬 파일이 없다: $Scene"; exit 1 }

$needed = New-Object System.Collections.Generic.HashSet[string]
function Add-Path([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return }
    [void]$needed.Add(($p -replace '/','\').Trim())
}

$s = Get-Content $Scene -Raw | ConvertFrom-Json
foreach ($e in $s.entities) {
    if ($e.SkinnedMesh) { Add-Path $e.SkinnedMesh.instanceAssetPath }
    if ($e.Material)    { Add-Path $e.Material.assetPath; Add-Path $e.Material.albedoTexturePath }
}

# .fbxasset 안의 source_fbx / materials 를 따라간다.
foreach ($p in @($needed)) {
    if ($p -notlike '*.fbxasset' -or -not (Test-Path $p)) { continue }
    $a = Get-Content $p -Raw | ConvertFrom-Json
    Add-Path $a.source_fbx
    foreach ($m in $a.materials) { Add-Path $m }
}

# .mat 안의 텍스처 경로를 따라간다.
foreach ($p in @($needed)) {
    if ($p -notlike '*.mat' -or -not (Test-Path $p)) { continue }
    $raw = Get-Content $p -Raw
    foreach ($m in [regex]::Matches($raw, '"(Resource/[^"]+\.(png|tga|jpg|jpeg|dds))"')) {
        Add-Path $m.Groups[1].Value
    }
}

$tracked = @{}
git ls-files | ForEach-Object { $tracked[($_ -replace '/','\')] = $true }

$missingLocal = @()   # 저장소에도 없고 디스크에도 없다 - 원본을 찾아야 한다
$untracked    = @()   # 디스크에는 있으나 커밋되지 않았다
foreach ($p in ($needed | Sort-Object)) {
    if ($tracked.ContainsKey($p)) { continue }
    if (Test-Path $p) { $untracked += $p } else { $missingLocal += $p }
}

Write-Output ("씬: {0}" -f $Scene)
Write-Output ("참조 {0}개 / 미추적 {1}개 (로컬 존재 {2}, 로컬에도 없음 {3})" -f `
    $needed.Count, ($untracked.Count + $missingLocal.Count), $untracked.Count, $missingLocal.Count)

if ($untracked.Count -gt 0) {
    Write-Output ''
    Write-Output '--- 디스크에는 있으나 커밋되지 않음 ---'
    foreach ($p in $untracked) {
        $ignored = (git check-ignore $p 2>$null)
        $why = if ($ignored) { ' (gitignore)' } else { '' }
        Write-Output ("  {0}{1}" -f $p, $why)
    }
}
if ($missingLocal.Count -gt 0) {
    Write-Output ''
    Write-Output '--- 디스크에도 없음. 원본을 찾거나 씬에서 정리해야 한다 ---'
    foreach ($p in $missingLocal) { Write-Output "  $p" }
}

if ($untracked.Count -eq 0 -and $missingLocal.Count -eq 0) {
    Write-Output ''
    Write-Output '미추적 없음. 클론한 상태로 그대로 열린다.'
    exit 0
}
exit 1
