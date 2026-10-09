# Build fantome.exe and fantome.dll (MSVC 14.16, static CRT) -> out\tools\fantome\ (+ an example fantome.ini)
# Usage: .\tools\fantome\build.ps1 [-Out out\tools\fantome]
param([string] $Out = "out\tools\fantome")
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out = Join-Path $root $Out
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$common = "/nologo /O2 /MT /std:c++17 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS"
$dll = Join-Path $PSScriptRoot "src\fantome_dll.cpp"
$exe = Join-Path $PSScriptRoot "src\fantome.cpp"
$pont = Join-Path $PSScriptRoot "src\pont.cpp"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$out`" && cl $common /LD `"$dll`" /Fe:fantome.dll /link ws2_32.lib && cl $common `"$exe`" /Fe:fantome.exe /link user32.lib && cl $common `"$pont`" /Fe:pont.exe /link ws2_32.lib"
if ($LASTEXITCODE -ne 0) { throw "build failed (is a game instance locking fantome.dll?)" }
if (-not (Test-Path (Join-Path $out "fantome.ini"))) {
    Copy-Item (Join-Path $PSScriptRoot "fantome.ini.exemple") (Join-Path $out "fantome.ini")
}
foreach ($s in "mosaique.ps1", "espace.ps1", "lancer.ps1") { Copy-Item (Join-Path $PSScriptRoot $s) $out -Force }
Get-ChildItem $out -Include *.dll, *.exe, *.ini, *.ps1 -Recurse | Select-Object Name, Length
