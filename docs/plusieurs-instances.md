# Plusieurs instances du jeu sur un même PC

Sources : `tools/fantome/` (outil de test et de développement) et `tools/dlcoop/` (même mécanisme, intégré au lanceur).

Dying Light n'autorise qu'une instance à la fois et une seule identité Steam. Pour lancer **plusieurs fenêtres du jeu
sur un même PC** (tests de coop seul, ou plusieurs joueurs sur une machine), `dlcoop_reseau.dll` (le même DLL que le
réseau du lanceur) lève ces deux verrous, par instance :

1. **Verrou d'instance unique.** Le jeu crée `Global\ChromeEngine4DIMutex` et refuse de démarrer s'il existe déjà. Pour
   une instance supplémentaire (numéro > 0), le DLL intercepte l'appel et **renomme** le verrou
   (`…_fantomeN`) : chaque instance a le sien. C'est pour cela que le mod doit être chargé **avant** que le jeu ne prenne
   son verrou (voir [chargement des mods](chargement-des-mods.md)).
2. **Identité Steam.** Les instances supplémentaires reçoivent une identité **dérivée** du vrai SteamID (le compte réel
   et les sauvegardes cloud ne sont pas touchés ; seul l'affichage passe par un intermédiaire). L'instance principale
   (numéro 0) garde l'identité Steam réelle.

**Adresses réseau.** Chaque instance parle sur `127.0.0.(N+1)`, et le jeu voit les autres sur leurs adresses locales.
En réseau local entre deux PC, `pont.exe` relaie les paquets `127.0.0.N` d'un PC à l'autre ; par Internet, c'est le rôle
du [serveur relais](serveur-relais.md). Le correctif du défaut « port 0 » de la 1.55 (paquets envoyés vers le port 0,
corrigés en 20101) est appliqué dans les deux sens.

**Mémoire.** Chaque instance consomme ~3,3 Go de mémoire engagée au menu. Pour beaucoup d'instances sur un PC, prévoir
un grand fichier d'échange (taille initiale = taille maximale).

## Avec le lanceur DLCoop

Tout ceci est intégré : *Instance > Additional instance*, et le lanceur pose les bonnes variables d'environnement par
instance (`DLF_INDEX`, `DLF_LOOPBACK`, `DLF_ADRESSE`, `DLF_NOM`). En réseau local, cocher aussi « Several instances on
this PC ». Voir [lanceur](lanceur.md).

## Avec les outils bruts (`tools/fantome`)

`fantome.exe` lance une instance (numéro 0 = hôte avec l'identité réelle, 1..32 = instances supplémentaires), réglages
dans `fantome.ini` (dont `Jeu=` et `Mods=` pour charger le mod). `pont.exe` relaie les adresses entre deux PC d'un
réseau local. Détails dans `tools/fantome/README.md`.
