# Compile coop6.dll (MSVC 14.16, CRT statique) -> out\mods\coop6\ (+ coop6.ini d'exemple s'il manque).
# Usage : .\mods\coop6\build.ps1   (aucune instance du jeu ne doit utiliser la DLL : elle serait verrouillée)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out = Join-Path $root "out\mods\coop6"
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$def = Join-Path $PSScriptRoot "engine_imports.def"
$src = Join-Path $PSScriptRoot "Coop6Mod.cpp"
$common = "/nologo /O2 /MT /std:c++17 /EHsc /W3 /utf-8"
# engine_imports.lib : bibliothèque d'import des quelques exports d'engine_x64_rwdi.dll utilisés par le mod.
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$out`" && lib /nologo /def:`"$def`" /machine:x64 /out:engine_imports.lib >nul && cl $common /LD `"$src`" /Fe:coop6.dll /link engine_imports.lib"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation (coop6.dll verrouillée par une instance du jeu ?)" }
if (-not (Test-Path (Join-Path $out "coop6.ini"))) { Copy-Item (Join-Path $PSScriptRoot "coop6.ini") $out }
Get-ChildItem $out -Include coop6.dll, coop6.ini -Recurse | Select-Object Name, Length
