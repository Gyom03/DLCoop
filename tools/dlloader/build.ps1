# Build winmm.dll (proxy / mod loader, MSVC 14.16, static CRT) -> out\tools\dlloader\
# Also prepares an example dlmods.ini next to it. The exports are forwarded to the system winmm (System32)
# by the forwarders in winmm_exports.h: nothing else to copy into the game folder.
# Usage: .\tools\dlloader\build.ps1 [-Out out\tools\dlloader] [-SystemWinmm C:\Windows\System32\winmm.dll]
#
# To install in the game folder:
#   winmm.dll, dlmods.ini, and the mod DLLs listed in dlmods.ini.
param(
    [string] $Out = "out\tools\dlloader",
    [string] $SystemWinmm = "C:\Windows\System32\winmm.dll"
)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out = Join-Path $root $Out
New-Item -ItemType Directory -Force $out | Out-Null

# winmm_exports.h: forwarders winmm -> system winmm, generated from the system winmm.
python "$PSScriptRoot\gen_def.py" $SystemWinmm
if ($LASTEXITCODE -ne 0) { throw "gen_def.py failed (is pefile installed? is the system winmm readable?)" }

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
# /I on the source folder for #include "winmm_exports.h" (whatever the build root).
$common = "/nologo /O2 /MT /std:c++17 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /I`"$PSScriptRoot`""
$src = Join-Path $PSScriptRoot "dllmain.cpp"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$out`" && cl $common /LD `"$src`" /Fe:winmm.dll"
if ($LASTEXITCODE -ne 0) { throw "build failed (is the game locking winmm.dll?)" }

# dlmods.ini: created only if missing (we do not overwrite an existing config).
if (-not (Test-Path (Join-Path $out "dlmods.ini"))) {
    Copy-Item (Join-Path $PSScriptRoot "dlmods.ini.exemple") (Join-Path $out "dlmods.ini")
}
Get-ChildItem $out -Include winmm.dll, dlmods.ini -Recurse | Select-Object Name, Length
