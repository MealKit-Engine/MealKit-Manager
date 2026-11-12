# MealKit Manager's installer for engines from the suite release index (engine ROADMAP R2, docs/DECISIONS.md "How the Manager installs").
# Lives beside MealKitManager.exe (cmake --install). Runs without the engine: there may be none installed yet.
#   install.ps1 list   -Index <url|path>                                   → one line per engine: engine<TAB>version<TAB>size<TAB>url<TAB>sha256
#   install.ps1 engine -Index <url|path> -Version <x.y.z> -Root <engines>  → downloads, checks the SHA-256, extracts to <Root>\<version>,
#                                                                            last line: installed<TAB><folder>
# The index is engine tools\release_index.ps1's format 1. url may be http(s), file:/// or a plain path (local tests).
param(
    [Parameter(Mandatory = $true, Position = 0)][ValidateSet("list", "engine")][string]$Command,
    [Parameter(Mandatory = $true)][string]$Index,
    [string]$Version = "",
    [string]$Root = ""
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue" # Invoke-WebRequest's progress bar makes 5.1 downloads very slow
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Fail([string]$message) { Write-Host "[MealKit] $message"; exit 1 }
function Local-Path([string]$location) {
    if ($location -match '^file:///(.+)$') { return [Uri]::UnescapeDataString($Matches[1]).Replace("/", "\") }
    if ($location -match '^https?://') { return $null }
    return $location
}
function Fetch([string]$location, [string]$destination) {
    $local = Local-Path $location
    if ($local) { Copy-Item -LiteralPath $local -Destination $destination -Force }
    else { Invoke-WebRequest -Uri $location -OutFile $destination -UseBasicParsing }
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ("mealkit-install-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $temp | Out-Null
try {
    $indexFile = Join-Path $temp "index.json"
    try { Fetch $Index $indexFile } catch { Fail "Could not read the release index $Index`: $($_.Exception.Message)" }
    $data = Get-Content $indexFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($data.format -ne 1) { Fail "Release index format $($data.format) is newer than this Manager knows (1). Update MealKit Manager." }
    $engines = @($data.engines)

    if ($Command -eq "list") {
        foreach ($e in $engines) { Write-Output ("engine`t{0}`t{1}`t{2}`t{3}" -f $e.version, $e.size, $e.url, $e.sha256) }
        exit 0
    }

    if (-not $Version -or -not $Root) { Fail "engine needs -Version and -Root" }
    $entry = $engines | Where-Object { $_.version -eq $Version } | Select-Object -First 1
    if (-not $entry) { Fail "Engine $Version is not in the release index." }
    $target = Join-Path $Root $Version
    if (Test-Path (Join-Path $target "VERSION")) { Fail "Engine $Version is already installed in $target" }

    $zip = Join-Path $temp "engine.zip"
    Write-Host "[MealKit] Downloading engine $Version ($([math]::Round($entry.size / 1MB)) MB)"
    try { Fetch $entry.url $zip } catch { Fail "Download failed: $($_.Exception.Message)" }
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $entry.sha256.ToLower()) { Fail "The download doesn't match the release index (SHA-256 $hash). Nothing was installed." }

    # Extract beside the target, then rename, so a cancelled install never leaves a half engine that looks installed
    New-Item -ItemType Directory -Force $Root | Out-Null
    $staging = Join-Path $Root ".installing-$Version"
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $staging)
    if (-not (Test-Path (Join-Path $staging "INSTALLED"))) { Remove-Item -Recurse -Force $staging; Fail "The package has no INSTALLED file: not an engine package." }
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    Rename-Item $staging (Split-Path $target -Leaf)
    Write-Output "installed`t$target"
    exit 0
} finally {
    Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
