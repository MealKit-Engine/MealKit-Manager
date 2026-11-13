# MealKit Manager's installer for engines from the suite release index (engine ROADMAP R2, docs/DECISIONS.md "How the Manager installs").
# Lives beside MealKitManager.exe (cmake --install). Runs without the engine: there may be none installed yet.
#   install.ps1 list   -Index <url|path>                                   → one line per engine: engine<TAB>version<TAB>size<TAB>url<TAB>sha256
#   install.ps1 engine -Index <url|path> -Version <x.y.z> -Root <engines>  → downloads, checks the SHA-256, extracts to <Root>\<version>,
#                                                                            last line: installed<TAB><folder>
#                                                                            also tool<TAB>name<TAB>version<TAB>size<TAB>url<TAB>sha256<TAB>protocol<TAB>engines,
#                                                                            manager<TAB>version<TAB>size<TAB>url<TAB>sha256
#   install.ps1 tool -Index <url|path> -Name <tool> -Root <tools>         → installs or updates <Root>\<tool> (refused while it or the editor runs),
#                                                                            last line: installed<TAB><folder>
#   install.ps1 self -From <folder> -To <folder>                           → copies the Manager (exe, this script) from a downloaded package
#                                                                            into To (%LOCALAPPDATA%\MealKit\manager) and makes the shortcuts
#   install.ps1 shortcuts -To <folder>                                     → Start menu shortcut to <To>\MealKitManager.exe, the desktop one
#                                                                            only on the first install (deleting it keeps it deleted)
#                                                                            (only the Manager gets shortcuts: engine docs/DECISIONS.md "How the Manager installs")
#                                                                            and registers it in Windows "Installed apps" (uninstall entry)
#   install.ps1 check                                                      → compiler<TAB>ok|missing<TAB><Visual Studio folder>
#                                                                            dxc<TAB>ok|missing<TAB><dxc.exe>
#   install.ps1 update -Index <url|path> -To <folder>                      → the index's "manager" entry: download, SHA-256, replace the files in To
#                                                                            (the running exe is renamed to MealKitManager.old.exe), last line: updated<TAB><version>
#   install.ps1 uninstall -To <folder> [-WaitPid <pid>]                    → waits for that process to quit, removes the shortcuts, the uninstall entry
#                                                                            and To. Engines, tools and the user lists stay.
# MEALKIT_SHORTCUT_DIR puts both shortcuts in that folder instead, MEALKIT_UNINSTALL_KEY renames the uninstall entry (tests).
# The index is engine tools\release_index.ps1's format 1. url may be http(s), file:/// or a plain path (local tests).
param(
    [Parameter(Mandatory = $true, Position = 0)][ValidateSet("list", "engine", "tool", "self", "shortcuts", "check", "update", "uninstall")][string]$Command,
    [string]$Name = "",
    [string]$Index = "",
    [string]$Version = "",
    [string]$Root = "",
    [string]$From = "",
    [string]$To = "",
    [int]$WaitPid = 0
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
function Shortcut-Folders {
    if ($env:MEALKIT_SHORTCUT_DIR) { return @($env:MEALKIT_SHORTCUT_DIR) }
    return @([Environment]::GetFolderPath("Programs"), [Environment]::GetFolderPath("DesktopDirectory")) # Start menu (current user), desktop
}
# The desktop shortcut is made once per install: a later install/update refreshes only the Start menu one, so a desktop
# shortcut the user deleted stays deleted. The marker lives in To, so uninstalling (which removes To) starts over
function Desktop-Marker([string]$to) { return Join-Path $to "desktop_shortcut.txt" }
function Uninstall-Key {
    $name = if ($env:MEALKIT_UNINSTALL_KEY) { $env:MEALKIT_UNINSTALL_KEY } else { "MealKitManager" }
    return "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$name"
}
# Windows "Installed apps" entry for the current user; uninstalling there runs MealKitManager.exe --uninstall
function Register([string]$to) {
    $exe = Join-Path $to "MealKitManager.exe"
    $ver = ((& $exe --version | Out-String).Trim() -replace '^MealKit Manager\s+', '')
    $key = Uninstall-Key
    New-Item -Path $key -Force | Out-Null
    $values = @{ DisplayName = "MealKit Manager"; DisplayVersion = $ver; Publisher = "MealKit"; DisplayIcon = "$exe,0";
                 InstallLocation = $to; UninstallString = "`"$exe`" --uninstall" }
    foreach ($k in $values.Keys) { New-ItemProperty -Path $key -Name $k -Value $values[$k] -PropertyType String -Force | Out-Null }
    foreach ($k in @("NoModify", "NoRepair")) { New-ItemProperty -Path $key -Name $k -Value 1 -PropertyType DWord -Force | Out-Null }
    Write-Output "registered`t$key"
}
function Make-Shortcuts([string]$to) {
    $exe = Join-Path $to "MealKitManager.exe"
    if (-not (Test-Path $exe)) { Fail "$exe not found" }
    if ($env:MEALKIT_SHORTCUT_DIR) { Make-Shortcut $env:MEALKIT_SHORTCUT_DIR $exe }
    else {
        Make-Shortcut ([Environment]::GetFolderPath("Programs")) $exe
        $marker = Desktop-Marker $to
        if (-not (Test-Path $marker)) {
            $desktop = [Environment]::GetFolderPath("DesktopDirectory")
            if (-not (Test-Path (Join-Path $desktop "MealKit Manager.lnk"))) { Make-Shortcut $desktop $exe }
            Set-Content -LiteralPath $marker -Value "The desktop shortcut was made once; install.ps1 doesn't make it again." -Encoding utf8
        }
    }
    Register $to
}

if ($Command -eq "uninstall") {
    if (-not $To) { Fail "uninstall needs -To" }
    if ($WaitPid) { Wait-Process -Id $WaitPid -Timeout 30 -ErrorAction SilentlyContinue }
    foreach ($folder in Shortcut-Folders) {
        $link = Join-Path $folder "MealKit Manager.lnk"
        if (Test-Path $link) { Remove-Item -Force $link; Write-Output "removed`t$link" }
    }
    $key = Uninstall-Key
    if (Test-Path $key) { Remove-Item -Recurse -Force $key; Write-Output "removed`t$key" }
    # The folder holds this script: PowerShell has read it already, so it can go too
    if (Test-Path $To) { Remove-Item -Recurse -Force $To; Write-Output "removed`t$To" }
    exit 0
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

    $mgr = $data.manager
    if ($Command -eq "tool") {
        if (-not $Name -or -not $Root) { Fail "tool needs -Name and -Root" }
        $entry = @($data.tools) | Where-Object { $_ -and $_.name -eq $Name } | Select-Object -First 1
        if (-not $entry) { Fail "Tool $Name is not in the release index." }
        $target = Join-Path $Root $Name
        # Applied only once the tool and the editor are closed (engine docs/DECISIONS.md "How the Manager installs"): files in use can't be replaced
        $running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
            $p = $null; try { $p = $_.Path } catch {}
            $p -and ($p.StartsWith($target + "\", [StringComparison]::OrdinalIgnoreCase) -or $_.ProcessName -eq "MealKitEditor") })
        if ($running.Count) { Fail ("Close {0} first: {1} is running." -f $(if ($running[0].ProcessName -eq "MealKitEditor") { "the editor" } else { $Name }), $running[0].ProcessName) }
        $zip = Join-Path $temp "tool.zip"
        Write-Host "[MealKit] Downloading $Name $($entry.version)"
        try { Fetch $entry.url $zip } catch { Fail "Download failed: $($_.Exception.Message)" }
        $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne $entry.sha256.ToLower()) { Fail "The download doesn't match the release index (SHA-256 $hash). Nothing was installed." }
        New-Item -ItemType Directory -Force $Root | Out-Null
        $staging = Join-Path $Root ".installing-$Name"
        if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($zip, $staging)
        if (-not (Test-Path (Join-Path $staging "mealkit-tool.txt"))) { Remove-Item -Recurse -Force $staging; Fail "The package has no mealkit-tool.txt: not a tool package." }
        # Swap folders: the old one goes aside first, so a failure leaves the installed version working
        $old = Join-Path $Root ".old-$Name"
        if (Test-Path $old) { Remove-Item -Recurse -Force $old }
        if (Test-Path $target) { Rename-Item $target (Split-Path $old -Leaf) }
        try { Rename-Item $staging $Name }
        catch { if (Test-Path $old) { Rename-Item $old $Name }; Fail "Could not put $Name in place: $($_.Exception.Message)" }
        if (Test-Path $old) { Remove-Item -Recurse -Force $old -ErrorAction SilentlyContinue }
        Write-Output "installed`t$target"
        exit 0
    }
    if ($Command -eq "update") {
        if (-not $To) { Fail "update needs -To" }
        if (-not $mgr) { Fail "The release index has no MealKit Manager entry." }
        $zip = Join-Path $temp "manager.zip"
        try { Fetch $mgr.url $zip } catch { Fail "Download failed: $($_.Exception.Message)" }
        $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
        if ($hash -ne $mgr.sha256.ToLower()) { Fail "The download doesn't match the release index (SHA-256 $hash). Nothing was changed." }
        $new = Join-Path $temp "manager"
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($zip, $new)
        if (-not (Test-Path (Join-Path $new "MealKitManager.exe"))) { Fail "The package has no MealKitManager.exe." }
        # Windows lets a running exe be renamed but not overwritten: the old one steps aside and is deleted on the next start
        $exe = Join-Path $To "MealKitManager.exe"
        $old = Join-Path $To "MealKitManager.old.exe"
        if (Test-Path $old) { Remove-Item -Force $old -ErrorAction SilentlyContinue }
        if (Test-Path $exe) { Rename-Item $exe "MealKitManager.old.exe" }
        try {
            foreach ($name in @("MealKitManager.exe", "install.ps1", "LICENSE")) {
                $src = Join-Path $new $name
                if (Test-Path $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $To $name) -Force }
            }
        } catch {
            if (-not (Test-Path $exe) -and (Test-Path $old)) { Rename-Item $old "MealKitManager.exe" }
            Fail "Could not replace the Manager files: $($_.Exception.Message)"
        }
        Register $To
        Write-Output "updated`t$($mgr.version)"
        exit 0
    }

    if ($Command -eq "list") {
        if ($mgr) { Write-Output ("manager`t{0}`t{1}`t{2}`t{3}" -f $mgr.version, $mgr.size, $mgr.url, $mgr.sha256) }
        foreach ($t in @($data.tools)) {
            if ($t) { Write-Output ("tool`t{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}" -f $t.name, $t.version, $t.size, $t.url, $t.sha256, $t.protocol, (@($t.engines) -join ",")) }
        }
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
