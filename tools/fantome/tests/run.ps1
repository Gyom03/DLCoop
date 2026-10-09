# Offline test of fantome.exe / fantome.dll with a fake game (faux_jeu.cpp): a "host" listening on 20101 and a
# "ghost" sending to port 0 and as a broadcast.
# Usage: .\tools\fantome\tests\run.ps1   (after .\tools\fantome\build.ps1; no game instance open)
# Expected result (fantome1.txt): 'reply "reply" from 127.0.0.1:0' then '... from 127.0.0.1:20101'.
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$bin = Join-Path $root "out\tools\fantome"
$t = Join-Path $root "out\tests\fantome"
Remove-Item -Recurse -Force $t -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $t | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$src = Join-Path $PSScriptRoot "faux_jeu.cpp"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$t`" && cl /nologo /O2 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS `"$src`" /Fe:faux_jeu.exe /link ws2_32.lib >nul"
if ($LASTEXITCODE -ne 0) { throw "failed to build the fake game" }
Copy-Item "$bin\fantome.exe", "$bin\fantome.dll" $t
Set-Content -Encoding Unicode "$t\fantome.ini" "[fantome]`r`nJeu=$t\faux_jeu.exe`r`nBouclage=1`r`nSonFantomes=1`r`nArguments=`r`nArgumentsFantomes="

$env:FAUX_SORTIE = "$t\hote.txt"; Remove-Item Env:FAUX_CIBLE -ErrorAction SilentlyContinue
Start-Process -FilePath "$t\fantome.exe" -ArgumentList "hote" -WindowStyle Hidden
Start-Sleep -Milliseconds 1500
$env:FAUX_SORTIE = "$t\fantome1.txt"; $env:FAUX_CIBLE = "127.0.0.1"
Start-Process -FilePath "$t\fantome.exe" -WindowStyle Hidden
Start-Sleep -Seconds 16
foreach ($f in "hote", "fantome1") {
    Write-Host "== $f"; Get-Content "$t\$f.txt"
}
foreach ($i in 0, 1) {
    Write-Host "== instance $i log"; Get-Content "$t\journaux\instance_$i.log"
}
