param(
    # --------------------------------------------------------
    # Public release version used in output file names
    # Example: 1.0.0
    # --------------------------------------------------------
    [Parameter(Mandatory = $true)]
    [string]$Version,

    # --------------------------------------------------------
    # Path to the built Release directory
    # --------------------------------------------------------
    [string]$BuildRoot = "C:\Users\Admin\Downloads\Projects\OCRtoODT-Qt-Windows\build\MSVC2022_vcpkg\Release",

    # --------------------------------------------------------
    # Optional Qt bin directory containing windeployqt.exe
    # Example: C:\Qt\6.10.0\msvc2022_64\bin
    # If omitted, the script tries to find windeployqt in PATH.
    # --------------------------------------------------------
    [string]$QtBinDir = "",

    # --------------------------------------------------------
    # Clean dist/ before packaging
    # --------------------------------------------------------
    [switch]$CleanDist
)

# ============================================================
# Resolve project root
# ============================================================

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# ============================================================
# Define important paths
# ============================================================

$ManifestPath = Join-Path $ProjectRoot "packaging\windows-deploy-manifest.txt"
$DistRoot     = Join-Path $ProjectRoot "dist"
$PackageName  = "OCRtoODT-v$Version-windows-x64"
$PackageDir   = Join-Path $DistRoot $PackageName
$ZipPath      = Join-Path $DistRoot "$PackageName.zip"
$ShaPath      = Join-Path $DistRoot "SHA256SUMS"

# ============================================================
# Validate build output
# ============================================================

$ExeSource = Join-Path $BuildRoot "OCRtoODT.exe"

if (-not (Test-Path $BuildRoot)) {
    throw "Build root not found: $BuildRoot"
}

if (-not (Test-Path $ExeSource)) {
    throw "Executable not found: $ExeSource"
}

if (-not (Test-Path $ManifestPath)) {
    throw "Manifest not found: $ManifestPath"
}

# ============================================================
# Locate windeployqt
# ============================================================

if ($QtBinDir -and $QtBinDir.Trim() -ne "") {
    $WinDeployQt = Join-Path $QtBinDir "windeployqt.exe"
} else {
    $Cmd = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($null -eq $Cmd) {
        throw "windeployqt.exe not found. Pass -QtBinDir or add Qt bin folder to PATH."
    }
    $WinDeployQt = $Cmd.Source
}

if (-not (Test-Path $WinDeployQt)) {
    throw "windeployqt.exe not found at: $WinDeployQt"
}

Write-Host "Using windeployqt: $WinDeployQt"
Write-Host "Project root     : $ProjectRoot"
Write-Host "Build root       : $BuildRoot"
Write-Host "Package dir      : $PackageDir"

# ============================================================
# Prepare dist directory
# ============================================================

if ($CleanDist -and (Test-Path $DistRoot)) {
    Remove-Item -Recurse -Force $DistRoot
}

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null

if (Test-Path $PackageDir) {
    Remove-Item -Recurse -Force $PackageDir
}

New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

# ============================================================
# Copy main executable first
# ============================================================

Copy-Item -Path $ExeSource -Destination $PackageDir -Force

$ExeTarget = Join-Path $PackageDir "OCRtoODT.exe"

# ============================================================
# Run windeployqt
# This step copies Qt DLLs, plugins and related runtime files.
# ============================================================

& $WinDeployQt `
    --release `
    --force `
    --compiler-runtime `
    $ExeTarget

if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

# ============================================================
# Copy non-Qt runtime files from manifest
# Manifest entries are relative to the Release build folder.
# ============================================================

$ManifestEntries = Get-Content $ManifestPath |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and -not $_.StartsWith("#") }

foreach ($Entry in $ManifestEntries) {
    $SourcePath = Join-Path $BuildRoot $Entry
    $TargetPath = Join-Path $PackageDir $Entry

    if (-not (Test-Path $SourcePath)) {
        throw "Manifest entry not found in build output: $Entry"
    }

    $TargetParent = Split-Path -Parent $TargetPath
    if ($TargetParent -and -not (Test-Path $TargetParent)) {
        New-Item -ItemType Directory -Force -Path $TargetParent | Out-Null
    }

    if ((Get-Item $SourcePath).PSIsContainer) {
        Copy-Item -Path $SourcePath -Destination $TargetPath -Recurse -Force
    } else {
        Copy-Item -Path $SourcePath -Destination $TargetPath -Force
    }

    Write-Host "Copied manifest entry: $Entry"
}

# ============================================================
# Copy top-level documentation files
# These files help both users and developers in the release.
# ============================================================

$DocFiles = @(
    "README.md",
    "CHANGELOG.md",
    "SECURITY.md",
    "SUPPORT.md",
    "THIRD_PARTY_NOTICES.md"
)

foreach ($DocFile in $DocFiles) {
    $DocSource = Join-Path $ProjectRoot $DocFile
    if (Test-Path $DocSource) {
        Copy-Item -Path $DocSource -Destination $PackageDir -Force
        Write-Host "Copied documentation file: $DocFile"
    }
}

# ============================================================
# Safety cleanup
# Runtime cache must never be shipped inside a release package.
# ============================================================

$CachePath = Join-Path $PackageDir "cache"
if (Test-Path $CachePath) {
    Remove-Item -Recurse -Force $CachePath
    Write-Host "Removed runtime cache from package"
}

# ============================================================
# Rebuild ZIP archive
# ============================================================

if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}

Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -CompressionLevel Optimal

# ============================================================
# Generate SHA256SUMS
# ============================================================

$Hash = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash *$PackageName.zip" | Set-Content -Path $ShaPath -Encoding ASCII

# ============================================================
# Final output
# ============================================================

Write-Host ""
Write-Host "Packaging completed successfully."
Write-Host "Package directory : $PackageDir"
Write-Host "ZIP archive       : $ZipPath"
Write-Host "SHA256SUMS        : $ShaPath"