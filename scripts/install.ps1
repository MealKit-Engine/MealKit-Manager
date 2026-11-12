# MealKit Manager's installer for engines from the suite release index (engine ROADMAP R2, docs/DECISIONS.md "How the Manager installs").
# Lives beside MealKitManager.exe (cmake --install). Runs without the engine: there may be none installed yet.
#   install.ps1 list   -Index <url|path>                                   → one line per engine: engine<TAB>version<TAB>size<TAB>url<TAB>sha256
#   install.ps1 engine -Index <url|path> -Version <x.y.z> -Root <engines>  → downloads, checks the SHA-256, extracts to <Root>\<version>,
#                                                                            last line: installed<TAB><folder>
#   install.ps1 self -From <folder> -To <folder>                           → copies the Manager (exe, this script) from a downloaded package
#                                                                            into To (%LOCALAPPDATA%\MealKit\manager) and makes the shortcuts
#   install.ps1 shortcuts -To <folder>                                     → Start menu + desktop shortcuts to <To>\MealKitManager.exe
#                                                                            (only the Manager gets shortcuts: engine docs/DECISIONS.md "How the Manager installs")
#   install.ps1 check                                                      → compiler<TAB>ok|missing<TAB><Visual Studio folder>
#                                                                            dxc<TAB>ok|missing<TAB><dxc.exe>
# MEALKIT_SHORTCUT_DIR puts both shortcuts in that folder instead (tests).
# The index is engine tools\release_index.ps1's format 1. url may be http(s), file:/// or a plain path (local tests).
param(
    [Parameter(Mandatory = $true, Position = 0)][ValidateSet("list", "engine", "self", "shortcuts", "check")][string]$Command,
    [string]$Index = "",
    [string]$Version = "",
    [string]$Root = "",
    [string]$From = "",
    [string]$To = ""
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

function Make-Shortcut([string]$folder, [string]$exe) {
    New-Item -ItemType Directory -Force $folder | Out-Null
    $link = Join-Path $folder "MealKit Manager.lnk"
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($link)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = Split-Path $exe -Parent
    $shortcut.IconLocation = "$exe,0"
    $shortcut.Description = "MealKit Manager"
    $shortcut.Save()
    Write-Output "shortcut`t$link"
}
function Make-Shortcuts([string]$to) {
    $exe = Join-Path $to "MealKitManager.exe"
    if (-not (Test-Path $exe)) { Fail "$exe not found" }
    if ($env:MEALKIT_SHORTCUT_DIR) {
        Make-Shortcut $env:MEALKIT_SHORTCUT_DIR $exe
        return
    }
    Make-Shortcut ([Environment]::GetFolderPath("Programs")) $exe # Start menu (current user)
    Make-Shortcut ([Environment]::GetFolderPath("DesktopDirectory")) $exe
}

if ($Command -eq "check") {
    # The same lookup the engine's tools\build.ps1 does (vswhere, the VC x64 tools component). MEALKIT_VSWHERE overrides vswhere (tests)
    $vswhere = if ($env:MEALKIT_VSWHERE) { $env:MEALKIT_VSWHERE } else { Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe" }
    $vs = ""
    if (Test-Path $vswhere) {
        $vs = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
        if (-not $vs -or -not (Test-Path (Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"))) { $vs = "" }
    }
    Write-Output ("compiler`t{0}`t{1}" -f $(if ($vs) { "ok" } else { "missing" }), $vs)
    # DXC: the engine's cmake/Dxc.cmake downloads it here on the first build
    $dxc = Get-ChildItem (Join-Path $env:LOCALAPPDATA "MealKit\tools") -Directory -Filter "dxc-*" -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName "bin\x64\dxc.exe" } | Where-Object { Test-Path $_ } | Select-Object -Last 1
    Write-Output ("dxc`t{0}`t{1}" -f $(if ($dxc) { "ok" } else { "missing" }), $dxc)
    exit 0
}
if ($Command -eq "shortcuts" -or $Command -eq "self") {
    if (-not $To) { Fail "$Command needs -To" }
    if ($Command -eq "self") {
        if (-not $From) { Fail "self needs -From" }
        if (-not (Test-Path (Join-Path $From "MealKitManager.exe"))) { Fail "No MealKitManager.exe in $From" }
        New-Item -ItemType Directory -Force $To | Out-Null
        # The running Manager is the package copy, so the installed exe isn't in use (an open installed Manager would block this: close it first)
        foreach ($name in @("MealKitManager.exe", "install.ps1", "LICENSE")) {
            $src = Join-Path $From $name
            if (Test-Path $src) {
                try { Copy-Item -LiteralPath $src -Destination (Join-Path $To $name) -Force }
                catch { Fail "Could not replace $name in $To (is MealKit Manager open there? Close it and try again)" }
            }
        }
        Write-Output "installed`t$To"
    }
    Make-Shortcuts $To
    exit 0
}
if (-not $Index) { Fail "$Command needs -Index" }

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
