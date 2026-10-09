# Offline test of the winmm.dll loader: the race between loading the mods and the game starting.
# The fake game (faux_jeu_winmm.cpp) takes the game lock as soon as main runs; launched as instance 1 (DLF_INDEX=1)
# while the lock is already held (a real game open, or a fake host launched here), it must find it "free" (renamed by
# dlcoop_reseau.dll). Usage: .\tests\dlloader\run.ps1 [-Loader path\winmm.dll] [-Fois 20]
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
if ($LASTEXITCODE -ne 0) { throw "failed to build the fake game" }
Copy-Item (Join-Path $root $Loader) "$t\winmm.dll" -Force
Copy-Item (Join-Path $root $Reseau) "$t\dlcoop_reseau.dll" -Force
Set-Content -Encoding UTF8 "$t\dlmods.ini" "[loader]`r`nMods=dlcoop_reseau.dll`r`n"

# Is the lock held? Otherwise a fake host (instance 0) holds it during the test.
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
"loader: $Loader"
"over $Fois launches as instance 1: lock renamed (OK) $($res.ok) ; lock ALREADY TAKEN $($res.pris) ; mod loaded after main $($res.tard)"
"last output: $(Get-Content "$t\sortie.txt")"
if ($res.ok -eq $Fois) { "PASS" } else { "FAIL" }
