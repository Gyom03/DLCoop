# Compile winmm.dll (relais / chargeur de mods, MSVC 14.16, CRT statique) -> out\tools\dlloader\
# Prépare aussi dlmods.ini (exemple) à côté. Les exports sont renvoyés vers la winmm du système
# (System32) par les forwarders de winmm_exports.h : rien d'autre à copier dans le dossier du jeu.
# Usage : .\tools\dlloader\build.ps1 [-Out out\tools\dlloader] [-SystemWinmm C:\Windows\System32\winmm.dll]
#
# À installer dans le dossier du jeu :
#   winmm.dll, dlmods.ini, et les DLL de mods citées dans dlmods.ini.
param(
    [string] $Out = "out\tools\dlloader",
    [string] $SystemWinmm = "C:\Windows\System32\winmm.dll"
)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out = Join-Path $root $Out
New-Item -ItemType Directory -Force $out | Out-Null

# winmm_exports.h : forwarders winmm -> winmm du système, générés depuis la winmm du système.
python "$PSScriptRoot\gen_def.py" $SystemWinmm
if ($LASTEXITCODE -ne 0) { throw "gen_def.py a échoué (pefile installé ? winmm du système lisible ?)" }

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
# /I sur le dossier des sources pour #include "winmm_exports.h" (quelle que soit la racine de compilation).
$common = "/nologo /O2 /MT /std:c++17 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /I`"$PSScriptRoot`""
$src = Join-Path $PSScriptRoot "dllmain.cpp"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$out`" && cl $common /LD `"$src`" /Fe:winmm.dll"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation (le jeu verrouille-t-il winmm.dll ?)" }

# dlmods.ini : créé seulement s'il manque (on n'écrase pas une config existante).
if (-not (Test-Path (Join-Path $out "dlmods.ini"))) {
    Copy-Item (Join-Path $PSScriptRoot "dlmods.ini.exemple") (Join-Path $out "dlmods.ini")
}
Get-ChildItem $out -Include winmm.dll, dlmods.ini -Recurse | Select-Object Name, Length
