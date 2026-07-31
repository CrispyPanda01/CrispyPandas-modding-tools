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

## Créateur de pays

L'onglet **Créateur de pays** génère directement dans le mod sélectionné :

- le tag dans `common/country_tags/zz_crispy_pandas_countries.txt` ;
- les couleurs principale et d'interface ;
- le fichier historique avec capitale, parti et popularités ;
- toutes les clés de localisation anglaises avec UTF-8 BOM ;
- facultativement, des drapeaux placeholders en 82x52, 41x26 et 10x7.

Le formulaire permet de choisir la culture graphique et le parti dirigeant. La
collision de tag est vérifiée dans le `colors.txt` du mod lorsqu'il existe ;
sinon, le `colors.txt` vanilla sert de référence. Les fichiers sont écrits en
une transaction : si une étape échoue, ils sont restaurés.

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
- en mode Provinces, `Alt` + clic gauche : créer un nouvel état avec les
  provinces actuellement sélectionnées ;
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

La création d'un état applique les mêmes garanties. Elle choisit le prochain ID
d'état disponible après le plus grand ID chargé, utilise la clé `STATE_<ID>`,
ajoute sa localisation anglaise dans
`localisation/english/crispy_pandas_states_l_english.yml`, puis transfère les
assets provinciaux et réconcilie les régions stratégiques.

Les trois modes sont aussi accessibles avec les boutons de la barre supérieure.

## Créateur de characters

Le bouton `Outil Characters` ouvre le second outil de la collection. Il permet
de créer un personnage avec plusieurs rôles : leader de pays, advisor, général,
maréchal, amiral et scientist. Chaque rôle affiche ses paramètres propres.

Le panneau de droite charge les traits présents dans le jeu et dans le mod
(`country_leader`, `unit_leader` et `scientist_traits`), avec recherche et
sélection multiple.

Le portrait large est obligatoire. Le portrait small est optionnel. Lorsqu'il
n'est pas fourni, le large est automatiquement recadré, réduit, incliné et placé
dans le cadre officiel d'advisor/idea. Les images PNG, JPEG, BMP et TIFF sont
traitées puis produites en DDS aux dimensions vanilla :

- large : `156×210` ;
- small : `65×67`.

La création écrit ou met à jour :

```text
common/characters/zz_crispy_pandas_TAG.txt
interface/crispy_pandas_characters.gfx
gfx/leaders/TAG/portrait_TAG_token.dds
gfx/interface/ideas/idea_TAG_token.dds
history/countries/TAG - Pays.txt
```

Le nom affiché est écrit directement dans le bloc du character. L'entrée
`recruit_character` est ajoutée automatiquement à l'historique du pays. Si cet
historique provient du jeu vanilla, il est copié dans le mod avant modification.
Les cinq sorties sont validées et écrites dans une même transaction ; une
collision de token ou de portrait annule l'opération.

La préférence est enregistrée dans
`%APPDATA%\CrispyPandas\settings.ini`. Si le dossier du mod n'existe plus, le
visualisateur demande d'en sélectionner un nouveau avant de continuer.

Les fichiers du mod remplacent ceux du jeu lorsqu'ils portent le même rôle.
Les états du mod remplacent individuellement les états de même identifiant.
