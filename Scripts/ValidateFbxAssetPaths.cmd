@echo off
setlocal EnableExtensions
chcp 65001 >nul

set "BASE_DIR=%~dp0.."
set "TARGET_DIR=%BASE_DIR%\Assets\Fbx"
if not "%~1"=="" set "TARGET_DIR=%~1"

echo [INFO] Starting .fbxasset scan: %TARGET_DIR%
echo.

if not exist "%TARGET_DIR%" (
    echo [ERROR] Target folder not found: %TARGET_DIR%
    echo Press any key to close this window.
    pause >nul
    exit /b 1
)

set "TEMP_PS1=%TEMP%\ValidateFbxAssetPaths_%RANDOM%_%RANDOM%.ps1"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
"$self='%~f0'; $raw=Get-Content -Raw -LiteralPath $self; $marker='###__POWERSHELL_' + 'SCRIPT_BELOW__###'; $idx=$raw.LastIndexOf($marker); if($idx -lt 0){ throw 'Embedded PowerShell marker not found.' }; $script=$raw.Substring($idx + $marker.Length); [System.IO.File]::WriteAllText('%TEMP_PS1%', $script, (New-Object System.Text.UTF8Encoding($false)));"

if errorlevel 1 (
    echo [ERROR] Failed to prepare runtime script.
    echo Press any key to close this window.
    pause >nul
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%TEMP_PS1%" -Target "%TARGET_DIR%"
set "EXIT_CODE=%ERRORLEVEL%"

del /f /q "%TEMP_PS1%" >nul 2>nul

echo.
if "%EXIT_CODE%"=="0" (
    echo [DONE] Completed.
) else (
    echo [ERROR] Failed. EXIT_CODE=%EXIT_CODE%
)
echo Press any key to close this window.
pause >nul

exit /b %EXIT_CODE%

###__POWERSHELL_SCRIPT_BELOW__###
param(
    [Parameter(Mandatory = $true)]
    [string]$Target
)

$ErrorActionPreference = "Stop"
$AbsoluteCheckMessage = [string]::Concat(
    [char]0xC808, [char]0xB300, [char]0xACBD, [char]0xB85C,
    [char]0xB2C8, [char]0xAE4C, [char]0x20,
    [char]0xD655, [char]0xC778, [char]0xD574, [char]0xBCF4, [char]0xC138, [char]0xC694,
    [char]0x2E
)

function Test-IsAbsolutePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    return (
        $Value -match '^[A-Za-z]:[\\/]' -or
        $Value -match '^\\\\' -or
        $Value -match '^/' -or
        $Value -match '^[a-zA-Z]+://'
    )
}

function Normalize-AssetLikePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $trimmed = $Value.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        return [PSCustomObject]@{
            Value      = $Value
            Changed    = $false
            IsAbsolute = $false
        }
    }

    if (Test-IsAbsolutePath -Value $trimmed) {
        return [PSCustomObject]@{
            Value      = $Value
            Changed    = $false
            IsAbsolute = $true
        }
    }

    $normalized = $trimmed -replace '\\', '/'
    $normalized = $normalized -replace '^(?:\./)+', ''

    $lower = $normalized.ToLowerInvariant()
    $assetsToken = 'assets/'
    $resourceToken = 'resource/'
    $assetsIndex = $lower.IndexOf($assetsToken)
    $resourceIndex = $lower.IndexOf($resourceToken)

    $fixed = $null
    if ($assetsIndex -ge 0 -and ($resourceIndex -lt 0 -or $assetsIndex -le $resourceIndex)) {
        $suffix = $normalized.Substring($assetsIndex + $assetsToken.Length)
        $fixed = "Assets/$suffix"
    } elseif ($resourceIndex -ge 0) {
        $suffix = $normalized.Substring($resourceIndex + $resourceToken.Length)
        $fixed = "Resource/$suffix"
    }

    if ($null -ne $fixed) {
        return [PSCustomObject]@{
            Value      = $fixed
            Changed    = ($fixed -ne $Value)
            IsAbsolute = $false
        }
    }

    return [PSCustomObject]@{
        Value      = $Value
        Changed    = $false
        IsAbsolute = $false
    }
}

function Normalize-JsonNode {
    param(
        [Parameter(Mandatory = $true)]
        [AllowNull()]
        [object]$Node,

        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string]$NodePath,

        [Parameter(Mandatory = $true)]
        [ref]$FileChanged,

        [Parameter(Mandatory = $true)]
        [ref]$FixedPaths,

        [Parameter(Mandatory = $true)]
        [ref]$AbsoluteWarnings
    )

    if ($null -eq $Node) {
        return $Node
    }

    if ($Node -is [string]) {
        $result = Normalize-AssetLikePath -Value $Node

        if ($result.IsAbsolute) {
            $AbsoluteWarnings.Value++
            Write-Host "[ABSOLUTE] $FilePath :: $NodePath = $Node" -ForegroundColor Yellow
            Write-Host ("  " + $AbsoluteCheckMessage) -ForegroundColor Yellow
            return $Node
        }

        if ($result.Changed) {
            $FileChanged.Value = $true
            $FixedPaths.Value++
            Write-Host "[FIX] $FilePath :: $NodePath" -ForegroundColor Cyan
            Write-Host "  $Node"
            Write-Host "  -> $($result.Value)"
            return $result.Value
        }

        return $Node
    }

    if ($Node -is [System.Management.Automation.PSCustomObject]) {
        foreach ($prop in $Node.PSObject.Properties) {
            $childPath = "$NodePath.$($prop.Name)"
            $prop.Value = Normalize-JsonNode `
                -Node $prop.Value `
                -FilePath $FilePath `
                -NodePath $childPath `
                -FileChanged $FileChanged `
                -FixedPaths $FixedPaths `
                -AbsoluteWarnings $AbsoluteWarnings
        }
        return $Node
    }

    if ($Node -is [System.Collections.IDictionary]) {
        foreach ($key in @($Node.Keys)) {
            $childPath = "$NodePath.$key"
            $Node[$key] = Normalize-JsonNode `
                -Node $Node[$key] `
                -FilePath $FilePath `
                -NodePath $childPath `
                -FileChanged $FileChanged `
                -FixedPaths $FixedPaths `
                -AbsoluteWarnings $AbsoluteWarnings
        }
        return $Node
    }

    if ($Node -is [System.Collections.IList]) {
        for ($i = 0; $i -lt $Node.Count; $i++) {
            $childPath = "$NodePath[$i]"
            $Node[$i] = Normalize-JsonNode `
                -Node $Node[$i] `
                -FilePath $FilePath `
                -NodePath $childPath `
                -FileChanged $FileChanged `
                -FixedPaths $FixedPaths `
                -AbsoluteWarnings $AbsoluteWarnings
        }
        return $Node
    }

    return $Node
}

try {
    $resolvedTarget = (Resolve-Path -Path $Target).Path
} catch {
    Write-Host "[ERROR] Target directory not found: $Target" -ForegroundColor Red
    exit 1
}

Write-Host "[START] Target: $resolvedTarget"

$files = Get-ChildItem -Path $resolvedTarget -Filter *.fbxasset -File -Recurse -ErrorAction SilentlyContinue
if ($null -eq $files -or $files.Count -eq 0) {
    Write-Host "[INFO] No .fbxasset files found."
    exit 0
}

$changedFiles = 0
$fixedPaths = 0
$absoluteWarnings = 0
$parseErrors = 0

foreach ($file in $files) {
    $raw = ""
    try {
        $raw = [System.IO.File]::ReadAllText($file.FullName)
    } catch {
        $parseErrors++
        Write-Host "[READ-ERROR] $($file.FullName) : $($_.Exception.Message)" -ForegroundColor Red
        continue
    }

    $json = $null
    try {
        $json = $raw | ConvertFrom-Json
    } catch {
        $parseErrors++
        Write-Host "[PARSE-ERROR] $($file.FullName) : JSON parse failed" -ForegroundColor Red
        continue
    }

    $fileChanged = $false
    $json = Normalize-JsonNode `
        -Node $json `
        -FilePath $file.FullName `
        -NodePath '$' `
        -FileChanged ([ref]$fileChanged) `
        -FixedPaths ([ref]$fixedPaths) `
        -AbsoluteWarnings ([ref]$absoluteWarnings)

    if ($fileChanged) {
        $jsonText = $json | ConvertTo-Json -Depth 100
        [System.IO.File]::WriteAllText(
            $file.FullName,
            $jsonText + [Environment]::NewLine,
            (New-Object System.Text.UTF8Encoding($false))
        )
        $changedFiles++
        Write-Host "[UPDATED] $($file.FullName)" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "========== SUMMARY =========="
Write-Host "Scanned Files    : $($files.Count)"
Write-Host "Changed Files    : $changedFiles"
Write-Host "Fixed Path Count : $fixedPaths"
Write-Host "Absolute Warnings: $absoluteWarnings"
Write-Host "Parse/Read Errors: $parseErrors"
Write-Host "============================="
exit 0
