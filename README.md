# RetroFX 9030

Plug-in VST3 / AU / Standalone qui reproduit le multi-effet Zoom 9030 (1991) :
façade, afficheur fluorescent 20 × 2, 9 modules, 47 types d'effets et paramètres
tels que décrits dans le manuel d'origine. Les algorithmes sont des modèles, pas
une copie du DSP d'origine. La marque est remplacée par « RETROFX ».

## Téléchargement

Dernière version : [page des Releases](https://github.com/elgaucholoco/RetroFX9030/releases/latest), fichier `RetroFX9030-x.y.z-macOS.zip`
(VST3, AU et application autonome, macOS 11+, Apple Silicon et Intel). Le fichier `INSTALLATION.txt` du zip explique où copier les plug-ins.

Le plug-in n'est pas signé par un développeur Apple identifié : si macOS le bloque, lancer une fois dans le Terminal
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"RetroFX 9030.vst3"` (et de même pour le `.component`).

## Utilisation (comme l'appareil)

| Touche | Mode Play | Mode Edit |
|---|---|---|
| PATCH ▼ ▲ | patch précédent / suivant | ▲ = COMPARE |
| EDIT ◀ ▶ | entre en Edit (REV / COMP) | module précédent / suivant |
| UTILITY / PAGE | mode Utility | page suivante |
| PRG-CHG LEARN ▼ / CTL-CHG MONITOR ▲ | apprentissage Program Change / moniteur CC | type d'effet précédent / suivant |
| BYPASS | bypass général | active / coupe le module |
| STORE | mémoriser (choisir la destination avec PATCH, puis STORE) | idem |
| EXIT | — | retour en Play |

- Molettes 1-4 : glisser verticalement ou molette de souris (Maj = fin). En Play, la molette 1 règle le niveau master.
- La lettre « E » inversée à gauche de l'afficheur indique un patch modifié et non mémorisé.
- SFX exclut EFF1 / EFF2 ; AMP en stéréo (STR on) active le routage parallèle.
- Utility : UTY1 sortie LINE/AMP et STEREO/MONO, UTY2 nommage, UTY3 canal MIDI, UTY4 rappel d'un patch ROM, UTY5 luminosité.

## Modulation temps réel

La dernière page de chaque module (DES / CTL / (%)) assigne un paramètre à un numéro de CC MIDI.
Le paramètre DAW « Pedal (CC 11) » joue le rôle de la pédale 8050 : automatisez-le avec CTL = 11.
Presets d'exemple : PEDAL WAH, DIVE BOMB, INFINITE VERB (pédale ≥ 64 = réverb infinie).
CC 7 = niveau master, CC 70-78 = modules on/off, CC 91 = bypass, Program Change = patch.

## Compilation

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target RetroFX9030_All
./build/SelfTest_artefacts/Release/SelfTest     # tests : presets, afficheur, stabilité de chaque effet
```
