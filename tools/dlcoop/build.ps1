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
$flags = "/nologo /O2 /MT /std:c++17 /EHsc /W3 /utf-8 /Brepro"
# /Brepro (compilateur et éditeur de liens) : compilation reproductible, sans date dans les DLL. Même source = mêmes octets,
# donc « Update available » (comparaison octet par octet avec le dossier du jeu) ne signale que de vrais changements.

# 1a. Informations de version des 3 DLL (version.rc ; version = <Version> de DLCoop.csproj, la même que DLCoop.exe).
$version = ([xml](Get-Content (Join-Path $PSScriptRoot "DLCoop.csproj") -Encoding UTF8)).Project.PropertyGroup.Version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "version illisible dans DLCoop.csproj : $version" }
$dlls = @(
    @{ Nom = "winmm"; Desc = "DLCoop mod loader for Dying Light 1.55 (forwards winmm calls to the Windows winmm.dll)" },
    @{ Nom = "dlcoop_reseau"; Desc = "DLCoop network fixes for Dying Light 1.55 (LAN and relay addresses, extra instances)" },
    @{ Nom = "coop6"; Desc = "DLCoop co-op mod for Dying Light 1.55 (up to 32 players)" }
)
$rcs = @()
foreach ($d in $dlls) {
    $dir = Join-Path $obj "res\$($d.Nom)"
    New-Item -ItemType Directory -Force $dir | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir "dlcoop_info.h"), (
        "#define DLCOOP_VER $($version.Replace('.', ',')),0`r`n#define DLCOOP_VER_STR `"$version`"`r`n" +
        "#define DLCOOP_DESC `"$($d.Desc)`"`r`n#define DLCOOP_NAME `"$($d.Nom)`"`r`n#define DLCOOP_FILE `"$($d.Nom).dll`"`r`n"))
    $rcs += "rc /nologo /c65001 /i `"$dir`" /fo `"$obj\$($d.Nom).res`" `"$PSScriptRoot\version.rc`" >nul"
}
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$obj`" && $($rcs -join ' && ')"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation des informations de version (version.rc)" }

cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$obj`" && cl $flags /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /LD `"$reseau`" dlcoop_reseau.res /Fe:dlcoop_reseau.dll /link /Brepro ws2_32.lib >nul && lib /nologo /def:`"$def`" /machine:x64 /out:engine_imports.lib >nul && cl $flags /LD `"$mod`" coop6.res /Fe:coop6.dll /link /Brepro engine_imports.lib >nul"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation des DLL" }

# 1b. Chargeur de mods winmm.dll (tools/dlloader) : forwarders vers la winmm du système, puis compilation.
$loader = Join-Path $root "tools\dlloader"
python "$loader\gen_def.py"
if ($LASTEXITCODE -ne 0) { throw "gen_def.py a échoué (pefile installé ?)" }
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$obj`" && cl $flags /DUNICODE /D_UNICODE /I`"$loader`" /LD `"$loader\dllmain.cpp`" winmm.res /Fe:winmm.dll /link /Brepro >nul"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation de winmm.dll (chargeur)" }

# 2. Fenêtre WPF
$bin = Join-Path $base "bin"
& dotnet build (Join-Path $PSScriptRoot "DLCoop.csproj") -c Release -o $bin --nologo -v q | Out-Null
if ($LASTEXITCODE -ne 0) { throw "échec de compilation de DLCoop.exe" }

# 3. Paquet
Copy-Item (Join-Path $bin "DLCoop.exe") $paquet
Copy-Item (Join-Path $obj "dlcoop_reseau.dll"), (Join-Path $obj "coop6.dll"), (Join-Path $obj "winmm.dll") $paquet
Copy-Item (Join-Path $PSScriptRoot "README.txt") $paquet
if ($Relais) { [IO.File]::WriteAllText((Join-Path $paquet "server.txt"), $Relais) }
$version = (Get-Item (Join-Path $paquet "DLCoop.exe")).VersionInfo.ProductVersion
$zip = Join-Path $base "DLCoop_$version.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $paquet "*") -DestinationPath $zip
Get-ChildItem $paquet | Select-Object Name, Length
"Paquet : $zip ($((Get-Item $zip).Length) octets)"
