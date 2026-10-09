# Génère winmm_exports.h : la winmm.dll relais (chargeur de mods de DLCoop) réexpédie TOUTES les fonctions de la vraie
# winmm.dll de Windows, mêmes ordinaux. Les renvois visent son chemin absolu (\\.\GLOBALROOT\SystemRoot\System32\winmm),
# ce qui évite de copier la DLL du système à côté du jeu ; un fichier .def ne sait pas l'écrire, d'où les directives
# « #pragma comment(linker, "/export:…") ».
# Entrée : la winmm.dll du système (argument, défaut System32). Sortie : tools/dlloader/winmm_exports.h
import pefile, sys, os

src = sys.argv[1] if len(sys.argv) > 1 else r"C:\Windows\System32\winmm.dll"
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "winmm_exports.h")
real = r"\\\\.\\GLOBALROOT\\SystemRoot\\System32\\winmm"  # échappé pour une chaîne C

pe = pefile.PE(src, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
exp = pe.DIRECTORY_ENTRY_EXPORT

named = [(s.ordinal, s.name.decode()) for s in exp.symbols if s.name]
ordinal_only = [s.ordinal for s in exp.symbols if not s.name]

lines = ["// Généré par gen_def.py : relais winmm -> winmm.dll du système (System32), mêmes ordinaux.", "#pragma once"]
for o, n in named:
    lines.append('#pragma comment(linker, "/export:{0}={1}.{0},@{2}")'.format(n, real, o))
for o in ordinal_only:
    lines.append('#pragma comment(linker, "/export:winmm_ord{0}={1}.#{0},@{0},NONAME")'.format(o, real))
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

print("exports nommes relayes :", len(named))
print("exports par ordinal seul relayes :", ordinal_only)
print("ecrit :", out)
