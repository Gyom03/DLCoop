# Test hors jeu de coop6.dll (après .\mods\coop6\build.ps1) : compile coop6_host.exe et le lance sur les DLL du
# jeu 1.55 (lues seulement, jamais modifiées). Résultat attendu : « RÉSULTAT : CONFORME » ; journal du mod :
# out\tests\coop6\coop6.log.
# Usage : .\tests\mod_install\run_coop6.ps1 [-Jeu "C:\Program Files (x86)\Steam\steamapps\common\Dying Light"]
param([string] $Jeu = "C:\Program Files (x86)\Steam\steamapps\common\Dying Light")
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$t = Join-Path $root "out\tests\coop6"
Remove-Item -Recurse -Force $t -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $t | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$src = Join-Path $PSScriptRoot "coop6_host.cpp"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$t`" && cl /nologo /O2 /MT /utf-8 /DUNICODE /D_UNICODE `"$src`" /Fe:coop6_host.exe >nul"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation du banc" }
Copy-Item (Join-Path $root "out\mods\coop6\coop6.dll"), (Join-Path $root "mods\coop6\coop6.ini") $t
& "$t\coop6_host.exe" (Join-Path $Jeu "gamedll_x64_rwdi.dll") (Join-Path $Jeu "engine_x64_rwdi.dll") "$t\coop6.dll"
$code = $LASTEXITCODE
Write-Host "== journal du mod"
Get-Content "$t\coop6.log" -Encoding UTF8
exit $code
