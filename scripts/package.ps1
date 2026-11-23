# Packs MealKit Manager as the one download users get (engine ROADMAP R2, docs/DECISIONS.md "Users download only MealKit Manager"):
#   scripts\package.ps1 [-Out <folder>]        (after build.bat)
# Writes <Out>\mealkit-manager-<version>-windows-x64.zip (linux-x64 on Linux, PowerShell 7): MealKitManager.exe (MealKitManager), the icon font, install.ps1, LICENSE and SETUP.
# On Linux "installs itself" puts the Manager in ~/.local/share/MealKit/manager with an application menu entry (icon MealKitManager.png beside the exe; no desktop icon).
# Unzipped and run, the exe sees SETUP, installs itself into %LOCALAPPDATA%\MealKit\manager with a Start menu and a desktop
# shortcut (install.ps1 self) and starts the installed copy. Prints the zip's size and SHA-256.
param([string]$Out = "")
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Out) { $Out = Join-Path $repo "build\package" }
$onLinux = ($PSVersionTable.PSVersion.Major -ge 6) -and $IsLinux
$platform = if ($onLinux) { "linux-x64" } else { "windows-x64" }
if ($onLinux -and (& uname -m) -ne "x86_64") { Write-Host "[MealKit Manager] only x86_64 Linux is packaged"; exit 1 }
$build = if ($onLinux) { Join-Path $repo "build/linux/bin" } else { Join-Path $repo "build\bin" }
$exe = Join-Path $build $(if ($onLinux) { "MealKitManager" } else { "MealKitManager.exe" })
if (-not (Test-Path $exe)) { Write-Host "[MealKit Manager] $exe not found. Build first (build.bat, or mk-build.ps1 on Linux)."; exit 1 }
$version = (& $exe --version | Out-String).Trim() -replace '^MealKit Manager\s+', ''
if ($version -notmatch '^\d+\.\d+\.\d+$') { Write-Host "[MealKit Manager] unexpected --version output: $version"; exit 1 }

$stage = Join-Path $Out "mealkit-manager-$version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $repo "third_party/fluent-icons/FluentSystemIcons-Regular.ttf") $stage # the icon font is loaded from beside the exe
if ($onLinux) { Copy-Item (Join-Path $repo "res/MealKitManager.png") $stage } # the .desktop entry's icon
Copy-Item (Join-Path $repo "scripts/install.ps1") $stage
Copy-Item (Join-Path $repo "LICENSE") $stage
[IO.File]::WriteAllText((Join-Path $stage "SETUP"), "Run $(Split-Path $exe -Leaf) to install MealKit Manager.`n")

$zip = Join-Path $Out "mealkit-manager-$version-$platform.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
Write-Host "[MealKit Manager] $zip"
Write-Host "size $((Get-Item $zip).Length)"
Write-Host "sha256 $((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())"
exit 0
