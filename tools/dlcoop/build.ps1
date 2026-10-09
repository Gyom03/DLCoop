# Assemble the distributable DLCoop package: out\dlcoop\paquet\ + out\dlcoop\DLCoop_<version>.zip
#   DLCoop.exe (WPF, .NET Framework 4.7.2), winmm.dll (tools/dlloader), dlcoop_reseau.dll (built from
#   tools/fantome/src/fantome_dll.cpp), coop6.dll (mods/coop6), README.txt. The DLLs are built here in a separate
#   folder, so running game instances (which lock out\mods\coop6 and out\tools\fantome) do not interfere.
# Usage: .\tools\dlcoop\build.ps1 [-Relais 203.0.113.10]
#   -Relais: Internet relay address pre-filled in the launcher (the package's server.txt file). The session
#            password is never put in the package: it is shared separately.
#   -Sortie: output folder (default out\dlcoop; use another if the package is locked by a running game)
param([string]$Relais = "", [string]$Sortie = "")
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$base = if ($Sortie) { $Sortie } else { Join-Path $root "out\dlcoop" }
$obj = Join-Path $base "obj"
$paquet = Join-Path $base "paquet"
Remove-Item $paquet -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $obj, $paquet | Out-Null

# 1. Native DLLs (MSVC 14.16, static CRT: no Visual C++ redistributable to install)
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$reseau = Join-Path $root "tools\fantome\src\fantome_dll.cpp"
$mod = Join-Path $root "mods\coop6\Coop6Mod.cpp"
$def = Join-Path $root "mods\coop6\engine_imports.def"
$flags = "/nologo /O2 /MT /std:c++17 /EHsc /W3 /utf-8 /Brepro"
# /Brepro (compiler and linker): reproducible build, no date in the DLLs. Same source = same bytes, so
# "Update available" (byte-by-byte comparison with the game folder) only reports real changes.

# 1a. Version info for the 3 DLLs (version.rc; version = <Version> from DLCoop.csproj, the same as DLCoop.exe).
$version = ([xml](Get-Content (Join-Path $PSScriptRoot "DLCoop.csproj") -Encoding UTF8)).Project.PropertyGroup.Version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "unreadable version in DLCoop.csproj: $version" }
$dlls = @(
    @{ Nom = "winmm"; Desc = "DLCoop mod loader for Dying Light 1.55 (forwards winmm calls to the Windows winmm.dll)" },
    @{ Nom = "dlcoop_reseau"; Desc = "DLCoop network fixes for Dying Light 1.55 (LAN and relay addresses, extra instances)" },
    @{ Nom = "coop6"; Desc = "DLCoop co-op mod for Dying Light 1.55 (up to 32 players)" }
)
$rcs = @()
foreach ($d in $dlls) {
    $dir = Join-Path $obj "res\$($d.Nom)"
    New-Item -ItemType Directory -Force $dir | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir "dlcoop_info.h"), (
        "#define DLCOOP_VER $($version.Replace('.', ',')),0`r`n#define DLCOOP_VER_STR `"$version`"`r`n" +
        "#define DLCOOP_DESC `"$($d.Desc)`"`r`n#define DLCOOP_NAME `"$($d.Nom)`"`r`n#define DLCOOP_FILE `"$($d.Nom).dll`"`r`n"))
    $rcs += "rc /nologo /c65001 /i `"$dir`" /fo `"$obj\$($d.Nom).res`" `"$PSScriptRoot\version.rc`" >nul"
}
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$obj`" && $($rcs -join ' && ')"
if ($LASTEXITCODE -ne 0) { throw "failed to build the version info (version.rc)" }

cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$obj`" && cl $flags /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /LD `"$reseau`" dlcoop_reseau.res /Fe:dlcoop_reseau.dll /link /Brepro ws2_32.lib >nul && lib /nologo /def:`"$def`" /machine:x64 /out:engine_imports.lib >nul && cl $flags /LD `"$mod`" coop6.res /Fe:coop6.dll /link /Brepro engine_imports.lib >nul"
if ($LASTEXITCODE -ne 0) { throw "failed to build the DLLs" }

# 1b. The winmm.dll mod loader (tools/dlloader): forwarders to the system winmm, then build.
$loader = Join-Path $root "tools\dlloader"
python "$loader\gen_def.py"
if ($LASTEXITCODE -ne 0) { throw "gen_def.py failed (is pefile installed?)" }
cmd /c "`"$vcvars`" x64 -vcvars_ver=14.16 >nul 2>nul && cd /d `"$obj`" && cl $flags /DUNICODE /D_UNICODE /I`"$loader`" /LD `"$loader\dllmain.cpp`" winmm.res /Fe:winmm.dll /link /Brepro >nul"
if ($LASTEXITCODE -ne 0) { throw "failed to build winmm.dll (the loader)" }

# 2. WPF window
$bin = Join-Path $base "bin"
& dotnet build (Join-Path $PSScriptRoot "DLCoop.csproj") -c Release -o $bin --nologo -v q | Out-Null
if ($LASTEXITCODE -ne 0) { throw "failed to build DLCoop.exe" }

# 3. Package
Copy-Item (Join-Path $bin "DLCoop.exe") $paquet
Copy-Item (Join-Path $obj "dlcoop_reseau.dll"), (Join-Path $obj "coop6.dll"), (Join-Path $obj "winmm.dll") $paquet
Copy-Item (Join-Path $PSScriptRoot "README.txt") $paquet
if ($Relais) { [IO.File]::WriteAllText((Join-Path $paquet "server.txt"), $Relais) }
$version = (Get-Item (Join-Path $paquet "DLCoop.exe")).VersionInfo.ProductVersion
$zip = Join-Path $base "DLCoop_$version.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $paquet "*") -DestinationPath $zip
Get-ChildItem $paquet | Select-Object Name, Length
"Package: $zip ($((Get-Item $zip).Length) bytes)"
