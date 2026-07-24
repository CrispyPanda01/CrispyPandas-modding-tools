# Crispy Pandas Modding Tools

Collection native d'outils de modding pour *Hearts of Iron IV*. La première
version contient un visualisateur de carte écrit en C11.

## Fonctionnalités actuelles

- détection automatique de l'installation Steam de HOI4 ;
- sélection obligatoire d'un mod au premier démarrage ;
- mémorisation et chargement automatique du dernier mod sélectionné ;
- couleur des états selon leur pays propriétaire ;
- mer et lacs en bleu vif ;
- frontières, zoom, déplacement et ajustement automatique ;
- modes États, Provinces et Régions stratégiques ;
- frontières nationales constantes dans les trois modes ;
- contours des états maintenus dans les modes Provinces et Régions stratégiques ;
- survol et informations propres à l'élément affiché.

## Prérequis Windows

- CMake 3.20 ou plus récent ;
- GCC UCRT64 et Ninja ;
- `mingw-w64-ucrt-x86_64-sdl3` ;
- `mingw-w64-ucrt-x86_64-sdl3-ttf`.

## Compiler

Depuis PowerShell :

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe `
  -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build
ctest --test-dir build --output-on-failure
```

L'exécutable est créé sous `build/crispy-pandas.exe`.

## Utilisation

```powershell
.\build\crispy-pandas.exe
.\build\crispy-pandas.exe --game "F:\SteamLibrary\steamapps\common\Hearts of Iron IV"
.\build\crispy-pandas.exe --game "..." --mod "C:\...\mon-mod"
```

Contrôles :

- `G` : choisir le dossier du jeu ;
- `M` : choisir le dossier d'un mod ;
- `1` : mode États ;
- `2` : mode Provinces ;
- `3` : mode Régions stratégiques ;
- `R` : recharger les fichiers ;
- `F` ou `Début` : ajuster toute la carte à la fenêtre ;
- molette : zoomer sous le curseur ;
- glisser avec un bouton de souris : déplacer la carte.
- clic gauche : sélectionner l'état, la province ou la région stratégique ;
- `Shift` + clic gauche : ajouter ou retirer un élément de la sélection ;
- en mode Provinces, clic droit sur un état : transférer les provinces
  sélectionnées vers cet état ;
- double-clic gauche sur un état : ouvrir son fichier source dans l'éditeur
  associé aux fichiers `.txt`.
- `Ctrl` + `Entrée` : modifier l'owner, le controller et les cores des états
  sélectionnés.

Dans la fenêtre d'édition, un champ vide reste inchangé. Owner et controller
acceptent un tag; cores accepte plusieurs tags séparés par des virgules ou des
espaces. Lorsque des cores sont fournis, les anciens sont remplacés par défaut.
La case « Garder les cores existants » permet plutôt de les conserver.

Si un état provient du jeu de base, son fichier est copié automatiquement dans
`history/states` du mod avant la modification. Les fichiers de l'installation
HOI4 ne sont jamais édités.

Lors d'un transfert de provinces, les victory points et blocs de bâtiments
provinciaux suivent leur province. Les régions stratégiques sont ajustées pour
que toutes les provinces d'un état affecté appartiennent à une seule région.
L'opération est préparée et validée en entier avant l'écriture; elle est annulée
si elle viderait un état source ou rencontrerait une structure ambiguë.

Les trois modes sont aussi accessibles avec les boutons de la barre supérieure.

La préférence est enregistrée dans
`%APPDATA%\CrispyPandas\settings.ini`. Si le dossier du mod n'existe plus, le
visualisateur demande d'en sélectionner un nouveau avant de continuer.

Les fichiers du mod remplacent ceux du jeu lorsqu'ils portent le même rôle.
Les états du mod remplacent individuellement les états de même identifiant.
