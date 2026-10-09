# Test hors jeu de fantome.exe / fantome.dll avec un faux jeu (faux_jeu.cpp) : un « hôte » qui écoute sur
# 20101 et un « fantôme » qui envoie vers le port 0 et en diffusion.
# Usage : .\tools\fantome\tests\run.ps1   (après .\tools\fantome\build.ps1 ; aucune instance du jeu ouverte)
# Résultat attendu (fantome1.txt) : « reponse "reponse" de 127.0.0.1:0 » puis « … de 127.0.0.1:20101 ».
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$bin = Join-Path $root "out\tools\fantome"
$t = Join-Path $root "out\tests\fantome"
Remove-Item -Recurse -Force $t -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $t | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$src = Join-Path $PSScriptRoot "faux_jeu.cpp"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$t`" && cl /nologo /O2 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS `"$src`" /Fe:faux_jeu.exe /link ws2_32.lib >nul"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation du faux jeu" }
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
    Write-Host "== journal instance $i"; Get-Content "$t\journaux\instance_$i.log"
}
