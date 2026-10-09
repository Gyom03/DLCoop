# Test hors jeu du chargeur winmm.dll : course entre le chargement des mods et le début du jeu.
# Le faux jeu (faux_jeu_winmm.cpp) prend le verrou du jeu dès main ; lancé en instance 1 (DLF_INDEX=1) alors que
# le verrou est déjà tenu (vrai jeu ouvert, ou faux hôte lancé ici), il doit le trouver « libre » (renommé par
# dlcoop_reseau.dll). Usage : .\tests\dlloader\run.ps1 [-Loader chemin\winmm.dll] [-Fois 20]
param(
    [string] $Loader = "out\tools\dlloader\winmm.dll",
    [string] $Reseau = "out\dlcoop\paquet\dlcoop_reseau.dll",
    [int] $Fois = 20
)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$t = Join-Path $root "out\tests\dlloader"
New-Item -ItemType Directory -Force $t | Out-Null
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$src = Join-Path $PSScriptRoot "faux_jeu_winmm.cpp"
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$t`" && cl /nologo /O2 /MT /utf-8 `"$src`" /Fe:faux_jeu.exe >nul"
if ($LASTEXITCODE -ne 0) { throw "échec de compilation du faux jeu" }
Copy-Item (Join-Path $root $Loader) "$t\winmm.dll" -Force
Copy-Item (Join-Path $root $Reseau) "$t\dlcoop_reseau.dll" -Force
Set-Content -Encoding UTF8 "$t\dlmods.ini" "[loader]`r`nMods=dlcoop_reseau.dll`r`n"

# Verrou tenu ? Sinon faux hôte (instance 0) qui le garde pendant le test.
$hote = $null
if (-not (Get-Process DyingLightGame -ErrorAction SilentlyContinue)) {
    $env:DLF_INDEX = "0"; $env:FAUX_ATTENTE = "120000"
    $hote = Start-Process "$t\faux_jeu.exe" -WorkingDirectory $t -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 1500
    Remove-Item Env:FAUX_ATTENTE
}

$env:DLF_INDEX = "1"; $env:DLF_LOOPBACK = "1"
$res = @{ ok = 0; pris = 0; tard = 0 }
for ($i = 0; $i -lt $Fois; $i++) {
    $p = Start-Process "$t\faux_jeu.exe" -WorkingDirectory $t -PassThru -Wait -WindowStyle Hidden `
        -RedirectStandardOutput "$t\sortie.txt"
    switch ($p.ExitCode) { 0 { $res.ok++ } 2 { $res.pris++ } default { $res.tard++ } }
}
if ($hote) { Stop-Process -Id $hote.Id -Force -ErrorAction SilentlyContinue }
"chargeur : $Loader"
"sur $Fois lancements en instance 1 : verrou renommé (OK) $($res.ok) ; verrou DEJA PRIS $($res.pris) ; mod chargé après main $($res.tard)"
"dernière sortie : $(Get-Content "$t\sortie.txt")"
if ($res.ok -eq $Fois) { "CONFORME" } else { "NON CONFORME" }
