# Presses Space in the Dying Light windows (gets past the "press any key" screen).
# Usage: .\espace.ps1                 all game windows
#        .\espace.ps1 -Processus 1234,5678  only these processes
# The window is brought to the front, then the key is sent via SendInput (the game reads the raw keyboard).
param([int[]] $Processus = @())
$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Clavier {
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit, Size = 40)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    public static void Espace() {
        // Space key: virtual code 0x20, scan code 0x39; press then release
        INPUT[] t = new INPUT[2];
        t[0].type = 1; t[0].ki.wVk = 0x20; t[0].ki.wScan = 0x39;
        t[1].type = 1; t[1].ki.wVk = 0x20; t[1].ki.wScan = 0x39; t[1].ki.dwFlags = 2;
        SendInput(1, new INPUT[] { t[0] }, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(60);
        SendInput(1, new INPUT[] { t[1] }, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@
$jeux = Get-Process DyingLightGame -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero -and ($Processus.Count -eq 0 -or $Processus -contains $_.Id) }
foreach ($p in $jeux) {
    # Alt pressed briefly: lets SetForegroundWindow succeed from a background process
    [Clavier]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [Clavier]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [Clavier]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
    [Clavier]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 300
    [Clavier]::Espace()
    Start-Sleep -Milliseconds 300
    "Space sent: pid $($p.Id) ($($p.MainWindowTitle))"
}
