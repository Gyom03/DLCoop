# Launches the host + N ghosts, -Ecart seconds apart, presses Space in each window 20 s after it launches
# ("press any key" screen), then tiles the windows.
# Usage (from out\tools\fantome): .\lancer.ps1 [-Fantomes 5] [-Ecart 10] [-SansHote] [-SansEspace]
param([int] $Fantomes = 5, [int] $Ecart = 10, [switch] $SansHote, [switch] $SansEspace)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$liste = @()
if (-not $SansHote) { $liste += "hote" }
$liste += 1..$Fantomes | ForEach-Object { "$_" }

function PidInstance([string] $num) {
    $n = if ($num -eq "hote") { 0 } else { [int]$num }
    $l = Get-Content (Join-Path $PSScriptRoot "journaux\instance_$n.log") -Encoding UTF8 -ErrorAction SilentlyContinue |
        Where-Object { $_ -match 'pid \d+, instance \d+' } | Select-Object -Last 1
    if ($l -match 'pid (\d+),') { return [int]$Matches[1] }
    return 0
}

$attente = @()   # launched instances, waiting for their Space: @{ Pid; Quand }
function EspacesDus {
    foreach ($a in @($script:attente)) {
        if ((Get-Date) -ge $a.Quand) {
            & (Join-Path $PSScriptRoot "espace.ps1") -Processus $a.Pid
            $script:attente = @($script:attente | Where-Object { $_.Pid -ne $a.Pid })
        }
    }
}

foreach ($num in $liste) {
    & .\fantome.exe $num | Out-Null
    $p = PidInstance $num
    "launched: $num (pid $p)"
    if ($p -and -not $SansEspace) { $attente += @{ Pid = $p; Quand = (Get-Date).AddSeconds(20) } }
    $fin = (Get-Date).AddSeconds($Ecart)
    while ((Get-Date) -lt $fin) { EspacesDus; Start-Sleep -Milliseconds 500 }
}
while ($attente.Count -gt 0) { EspacesDus; Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 2
& (Join-Path $PSScriptRoot "mosaique.ps1") -Colonnes 3
