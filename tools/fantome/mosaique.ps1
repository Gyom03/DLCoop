# Range les fenêtres de Dying Light en mosaïque sur l'écran principal (2 × 2 pour 4 instances, 3 × 2 pour 5-6).
# Ordre : par numéro d'instance (0 = hôte en haut à gauche, puis 1, 2…), lu dans journaux\instance_N.log
# (pid de chaque instance) ; à défaut, par heure de lancement. Le titre de chaque fenêtre indique l'instance.
# Usage : .\tools\fantome\mosaique.ps1 [-Colonnes 2] [-SansBordure]
param([int] $Colonnes = 0, [switch] $SansBordure)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Fen {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] public static extern IntPtr GetStyle(IntPtr h, int idx);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] public static extern IntPtr SetStyle(IntPtr h, int idx, IntPtr v);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool SetWindowTextW(IntPtr h, string text);
}
"@
[Fen]::SetProcessDPIAware() | Out-Null   # coordonnées en vrais pixels
$zone = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
# pid -> numéro d'instance, d'après la 1re ligne des journaux de fantome.dll (« pid 1234, instance 2 »).
$instance = @{}
Get-ChildItem (Join-Path $PSScriptRoot "journaux") -Filter "instance_*.log" -ErrorAction SilentlyContinue | ForEach-Object {
    # dernière ligne « fantome.dll chargée (pid …, instance N » : les journaux sont complétés à chaque lancement
    $l1 = Get-Content $_.FullName -Encoding UTF8 | Where-Object { $_ -match 'pid \d+, instance \d+' } | Select-Object -Last 1
    if ($l1 -match 'pid (\d+), instance (\d+)') { $instance[[int]$Matches[1]] = [int]$Matches[2] }
}
$jeux = Get-Process DyingLightGame -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
    Sort-Object @{ e = { if ($instance.ContainsKey($_.Id)) { $instance[$_.Id] } else { 100 } } }, StartTime
if (-not $jeux) { throw "aucune fenêtre de Dying Light trouvée" }
$n = @($jeux).Count
if ($Colonnes -le 0) { $Colonnes = [Math]::Ceiling([Math]::Sqrt($n)) }
$lignes = [Math]::Ceiling($n / $Colonnes)
$l = [int]($zone.Width / $Colonnes); $h = [int]($zone.Height / $lignes)
$i = 0
foreach ($p in $jeux) {
    $w = $p.MainWindowHandle
    [Fen]::ShowWindow($w, 9) | Out-Null   # SW_RESTORE (sort du plein écran / agrandi)
    if ($SansBordure) {
        # WS_POPUP | WS_VISIBLE : fenêtre sans titre ni bordure
        [Fen]::SetStyle($w, -16, [IntPtr]0x90000000L) | Out-Null
    }
    $x = $zone.X + ($i % $Colonnes) * $l; $y = $zone.Y + [Math]::Floor($i / $Colonnes) * $h
    # SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW
    [Fen]::SetWindowPos($w, [IntPtr]::Zero, $x, $y, $l, $h, 0x0004 -bor 0x0020 -bor 0x0040) | Out-Null
    $num = if ($instance.ContainsKey($p.Id)) { $instance[$p.Id] } else { -1 }
    $titre = if ($num -eq 0) { "Dying Light - instance 0 (hôte)" } elseif ($num -gt 0) { "Dying Light - fantôme $num" } else { "Dying Light" }
    [Fen]::SetWindowTextW($w, $titre) | Out-Null
    "{0} : pid {1}, {6} → {2}×{3} en ({4},{5})" -f $i, $p.Id, $l, $h, $x, $y, $titre
    $i++
}
