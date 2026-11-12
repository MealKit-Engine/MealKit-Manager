# Packs MealKit Manager as the one download users get (engine ROADMAP R2, docs/DECISIONS.md "Users download only MealKit Manager"):
#   scripts\package.ps1 [-Out <folder>]        (after build.bat)
# Writes <Out>\mealkit-manager-<version>-windows-x64.zip: MealKitManager.exe, install.ps1, LICENSE and SETUP.
# Unzipped and run, the exe sees SETUP, installs itself into %LOCALAPPDATA%\MealKit\manager with a Start menu and a desktop
# shortcut (install.ps1 self) and starts the installed copy. Prints the zip's size and SHA-256.
param([string]$Out = "")
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Out) { $Out = Join-Path $repo "build\package" }
$exe = Join-Path $repo "build\bin\MealKitManager.exe"
if (-not (Test-Path $exe)) { Write-Host "[MealKit Manager] $exe not found. Run build.bat first."; exit 1 }
$version = (& $exe --version | Out-String).Trim() -replace '^MealKit Manager\s+', ''
if ($version -notmatch '^\d+\.\d+\.\d+$') { Write-Host "[MealKit Manager] unexpected --version output: $version"; exit 1 }

$stage = Join-Path $Out "mealkit-manager-$version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $repo "scripts\install.ps1") $stage
Copy-Item (Join-Path $repo "LICENSE") $stage
[IO.File]::WriteAllText((Join-Path $stage "SETUP"), "Run MealKitManager.exe to install MealKit Manager.`n")

$zip = Join-Path $Out "mealkit-manager-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
Write-Host "[MealKit Manager] $zip"
Write-Host "size $((Get-Item $zip).Length)"
Write-Host "sha256 $((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())"
exit 0
