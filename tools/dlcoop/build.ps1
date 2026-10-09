# Assemble le paquet DLCoop à distribuer : out\dlcoop\paquet\ + out\dlcoop\DLCoop_<version>.zip
#   DLCoop.exe (WPF, .NET Framework 4.7.2 intégré à Windows), dlcoop_reseau.dll (source de tools/fantome/src/fantome_dll.cpp),
#   coop6.dll (mods/coop6), README.txt (anglais). Les DLL sont compilées ici, dans un dossier à part : les instances du jeu en
#   cours (qui verrouillent out\mods\coop6 et out\tools\fantome) ne gênent pas.
# Usage : .\tools\dlcoop\build.ps1 [-Relais 203.0.113.10]
#   -Relais : adresse du relais Internet préremplie dans le lanceur (fichier server.txt du paquet). Le mot de passe de
#             session n'est jamais mis dans le paquet : il se transmet à part.
#   -Sortie : dossier de sortie (défaut out\dlcoop ; un autre si le paquet est verrouillé par une partie en cours)
param([string]$Relais = "", [string]$Sortie = "")
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$base = if ($Sortie) { $Sortie } else { Join-Path $root "out\dlcoop" }
$obj = Join-Path $base "obj"
$paquet = Join-Path $base "paquet"
Remove-Item $paquet -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $obj, $paquet | Out-Null

# 1. DLL natives (MSVC 14.16, CRT statique : aucun redistribuable Visual C++ à installer)
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$reseau = Join-Path $root "tools\fantome\src\fantome_dll.cpp"
$mod = Join-Path $root "mods\coop6\Coop6Mod.cpp"
$def = Join-Path $root "mods\coop6\engine_imports.def"
$flags = "/nologo /O2 /MT /std:c++17 /EHsc /W3 /utf-8"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$obj`" && cl $flags /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /LD `"$reseau`" /Fe:dlcoop_reseau.dll /link ws2_32.lib >nul && lib /nologo /def:`"$def`" /machine:x64 /out:engine_imports.lib >nul && cl $flags /LD `"$mod`" /Fe:coop6.dll /link engine_imports.lib >nul"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation des DLL" }

# 2. Fenêtre WPF
$bin = Join-Path $base "bin"
& dotnet build (Join-Path $PSScriptRoot "DLCoop.csproj") -c Release -o $bin --nologo -v q | Out-Null
if ($LASTEXITCODE -ne 0) { throw "échec de compilation de DLCoop.exe" }

# 3. Paquet
Copy-Item (Join-Path $bin "DLCoop.exe") $paquet
Copy-Item (Join-Path $obj "dlcoop_reseau.dll"), (Join-Path $obj "coop6.dll") $paquet
Copy-Item (Join-Path $PSScriptRoot "README.txt") $paquet
if ($Relais) { [IO.File]::WriteAllText((Join-Path $paquet "server.txt"), $Relais) }
$version = (Get-Item (Join-Path $paquet "DLCoop.exe")).VersionInfo.ProductVersion
$zip = Join-Path $base "DLCoop_$version.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $paquet "*") -DestinationPath $zip
Get-ChildItem $paquet | Select-Object Name, Length
"Paquet : $zip ($((Get-Item $zip).Length) octets)"
