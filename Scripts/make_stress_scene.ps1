# Generates a 5,000-entity stress scene for the ECS-vs-OOP benchmark.
#
# Only ~40 characters are placed where the camera can see them (Transform
# visible=true); the other ~4,960 have Transform visible=false so
# SkinnedMeshSystem never adds them to the renderer's per-frame command
# list (see Gameplay/Animation/SkinnedMeshSystem.h: "if (!t->enabled ||
# !t->visible) continue;" while building SkinnedDrawCommands). That list
# feeds every GPU pass - opaque, transparent, AND the key/sun directional
# shadow map - so visible=false is what actually keeps them off the GPU,
# not just their position. Every entity still gets a Transform (and thus
# counts in Engine::Initialize's "Entities:" total and gets walked by
# per-frame systems such as animation), so the scene stresses CPU-side
# entity handling without becoming GPU-bound on fill rate or draw calls.
#
# They are also placed behind the camera (z <= -60, more negative than
# the camera's z=-8) as a second, independent line of defense: even if
# something ever renders off of Transform.visible incorrectly, being
# behind the camera fails DeferredRenderSystem's per-frame view-frustum
# cull (BuildSkinnedCullingSphere + cameraFrustum.Contains) unconditionally.
#
# Two earlier, broken attempts and what they measured (see task-8-report.md
# for full CSV data):
#  1. Far offscreen characters placed far down +Z instead of behind the
#     camera (still visible=true). NOT culled: a 60-degree FOV frustum's
#     cross-section widens faster than Z increases, so a +-50 unit lateral
#     spread at z=400 is still well inside the view cone there (half-width
#     ~231 units). drawCalls: 69945 (effectively all 5000 characters drawn).
#  2. Far characters moved behind the camera (this script's z placement)
#     but still visible=true. The main opaque pass now culls them
#     correctly, but the key-light shadow pass does not: its shadow
#     frustum is sized from a scene-wide bounding box over every
#     visible=true skinned/material entity regardless of camera facing,
#     so all ~4,960 of them still generated one (cheap, depth-only) draw
#     call each. drawCalls: 5465 (505 baseline + ~4960 shadow-only draws).
#  3. This script (visible=false on top of the behind-camera position):
#     drawCalls matches the 40-visible-only baseline of 505.
#
# ASCII only on purpose: PowerShell 5.1 (powershell.exe) and 7 (pwsh) can
# both run this file, and non-ASCII source text has bitten bench scripts in
# this repo before (see commit 7d5a49c) when read in the wrong codepage.
#
# Format verified against Assets/Scenes/DuellumCycli/PrototypeDungeon.scene,
# a real scene that already loads Alice_Swimsuit_white as a boneCount=0
# SkinnedMesh (see task-8-report.md for the diff).

$repo = Split-Path -Parent $PSScriptRoot
$mesh = "Assets/Fbx/Alice_Swimsuit_white.fbxasset"
$mat  = "Assets/Materials/Alice_Swimsuit_white_0.mat"
$tex  = "Resource/Textures/Alice_Swimsuit_white/Alice_Swimsuit_white_D_0.png"

$entityCount = 5000
$visibleCount = 40

$entities = @()

# Camera
$entities += [pscustomobject]@{
    Camera = [pscustomobject]@{ FOV = 60.0; 'Far Plane' = 1000.0; 'Near Plane' = 0.1; primary = $true }
    Transform = [pscustomobject]@{ enabled = $true; visible = $true
        position = [pscustomobject]@{ x = 0.0; y = 2.0; z = -8.0 }
        rotation = [pscustomobject]@{ x = 0.0; y = 0.0; z = 0.0 }
        scale    = [pscustomobject]@{ x = 1.0; y = 1.0; z = 1.0 } }
    guid = "10001"; name = "MainCamera"
}

# Light
$entities += [pscustomobject]@{
    PointLight = [pscustomobject]@{ castShadow = $false; enabled = $true; intensity = 3.0; range = 60.0
        color = [pscustomobject]@{ x = 1.0; y = 0.92; z = 0.8 } }
    Transform = [pscustomobject]@{ enabled = $true; visible = $true
        position = [pscustomobject]@{ x = 0.0; y = 6.0; z = 0.0 }
        rotation = [pscustomobject]@{ x = 0.0; y = 0.0; z = 0.0 }
        scale    = [pscustomobject]@{ x = 1.0; y = 1.0; z = 1.0 } }
    guid = "10002"; name = "Point Light"
}

# 5,000 characters. First $visibleCount form a grid in front of the camera
# (z = 0..6, camera at z=-8 facing +Z). The rest sit behind the camera
# (z <= -60, camera at z=-8) laid out on the same kind of x/z grid so they
# stay spread apart, but the sign of z alone puts them outside the near
# plane regardless of x, so the frustum cull always rejects them.
$cameraZ = -8.0
for ($i = 0; $i -lt $entityCount; $i++) {
    $isVisible = $i -lt $visibleCount
    if ($isVisible) {
        $x = ($i % 10) * 1.6 - 7.2
        $z = [math]::Floor($i / 10) * 2.0
    } else {
        $x = (($i % 50) - 25) * 2.0
        $z = $cameraZ - 60.0 - [math]::Floor($i / 50) * 2.0
    }
    $entities += [pscustomobject]@{
        SkinnedMesh = [pscustomobject]@{ boneCount = 0; instanceAssetPath = $mesh; meshAssetPath = "Alice_Swimsuit_white" }
        Material = [pscustomobject]@{ assetPath = $mat; albedoTexturePath = $tex
            alpha = 1.0; ambientOcclusion = 1.0; metalness = 0.0; normalStrength = 1.0
            outlineWidth = 0.0; roughness = 0.5; shadingMode = -1; shadowStrength = 1.0; transparent = $false
            color = [pscustomobject]@{ x = 1.0; y = 1.0; z = 1.0 }
            outlineColor = [pscustomobject]@{ x = 0.0; y = 0.0; z = 0.0 } }
        Transform = [pscustomobject]@{ enabled = $true; visible = $isVisible
            position = [pscustomobject]@{ x = [double]$x; y = 0.0; z = [double]$z }
            rotation = [pscustomobject]@{ x = 0.0; y = 3.14159265; z = 0.0 }
            scale    = [pscustomobject]@{ x = 0.01; y = 0.01; z = 0.01 } }
        guid = "$(20000 + $i)"; name = "Char_$i"
    }
}

# Sanity checks before writing: guid uniqueness and total entity count.
$guids = $entities | ForEach-Object { $_.guid }
$uniqueGuids = $guids | Select-Object -Unique
if ($uniqueGuids.Count -ne $guids.Count) {
    throw "guid collision detected: $($guids.Count) entities but only $($uniqueGuids.Count) unique guids"
}
$expected = $entityCount + 2
if ($entities.Count -ne $expected) {
    throw "entity count mismatch: expected $expected, got $($entities.Count)"
}

$outDir = Join-Path $repo "Assets\Scenes\Bench"
$out = Join-Path $outDir "EcsStress5000.scene"
New-Item -ItemType Directory -Force $outDir | Out-Null

$scene = [pscustomobject]@{ entities = $entities; sceneName = "EcsStress5000"; version = 1 }
$json = $scene | ConvertTo-Json -Depth 8

# Write UTF-8 without a BOM: the engine's existing .scene files have none,
# and Set-Content -Encoding UTF8 on Windows PowerShell 5.1 would add one.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($out, $json, $utf8NoBom)

Write-Host "Wrote: $out"
Write-Host ("Entities: {0} (camera=1 light=1 characters={1}, visible={2})" -f $entities.Count, $entityCount, $visibleCount)
