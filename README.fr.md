[English](README.md) | [Español](README.es.md) | **Français**

# v32as — Assembleur et éditeur de liens Vircon32

`v32as` assemble le langage d'assemblage Vircon32 en un fichier VBIN, le format
du programme d'une cartouche. À partir d'une même source, il écrit les mêmes
octets que l'outil officiel `assemble`, et il y ajoute un préprocesseur avec
macros, quelques formes de nombres supplémentaires, des objets relogeables avec
un éditeur de liens (`v32asld`), un mode listage/verbeux/pas à pas, un mode
interactif et plusieurs sorties de débogage. `--standard` désactive toutes les
extensions.

Il est écrit en C : l'analyseur lexical est généré par flex, l'analyseur
syntaxique par bison, et les symboles et les corrections de références
(fixups) vivent dans des tables de hachage. Les outils sont `bin/v32as`
(l'assembleur) et `bin/v32asld` (l'éditeur de liens et archiveur).

### En quoi il diffère de l'assembleur officiel

En bref : tout ce que l'`assemble` officiel accepte, il l'accepte et
l'assemble en octets identiques (et `--standard` donne exactement le
comportement de l'outil officiel, extensions désactivées) ; le reste n'est
qu'ajouts. De façon générale :

- **Un vrai préprocesseur de macros** à la place des `%define`/`%ifdef`/
  `%include` officiels : définitions de type fonction, expressions `%if`,
  `%eval`, `%macro` multilignes avec vérification de leurs opérandes, boucles
  `%rep`, contextes pour un flux de contrôle structuré, `.étiquettes` locales
  au fichier, `-D`/`-I` en ligne de commande, et deux bibliothèques de macros
  fournies.

- **Accès aux registres internes du processeur** (`IP`, `IR`, `IV`) par `MOV`,
  pour le code qui se modifie lui-même : voir [Registres internes](#registres-internes-extension).

- **Un langage plus agréable** : formes de nombres supplémentaires (`0b`,
  octal, suffixe `h`, décimal `#`), littéraux caractère, `true`/`false`,
  opérandes `étiquette+N`, une étiquette et une instruction sur une même
  ligne, mnémoniques et registres insensibles à la casse.

- **Assemblage et édition de liens séparés** : objets relogeables (`-C`), et
  `v32asld` pour les lier et les archiver.

- **Plus de sorties pour les outils** : listages (`-T`), une trace
  verbeuse/pas à pas, le mode interactif (`-i`), des fichiers `.debug` avec
  décalages en octets ou lignes au niveau du C (`--map`), tables de symboles,
  de variables et de relocalisations, dépendances pour Makefile.

- **La paperasse de la cartouche** : `%title`, `%texture`, `%sound` dans la
  source écrivent le XML de `packrom` et `Make.sh`/`Make.bat`.

- **Des diagnostics** qui nomment le fichier et la ligne que vous avez écrits,
  à travers `%include` et les macros, avec la chaîne d'inclusions et
  d'expansions.

## Table des matières

  * [Compiler, installer et tester](#compiler-installer-et-tester)
  * [Utilisation](#utilisation)
  * [Référence des options](#référence-des-options) ·
  * [Fichiers de sortie](#fichiers-de-sortie)
  * [Objets et éditeur de liens](#objets-et-éditeur-de-liens)
  * [Le langage](#le-langage)
  * [`--standard`](#--standard--exactement-lassembleur-officiel)
  * [Préprocesseur](#préprocesseur)
  * [Définition de la cartouche et scripts de construction](#définition-de-la-cartouche-et-scripts-de-construction)
  * [Registres internes](#registres-internes-extension)
  * [Vérification](#vérification)
  * [Licence](#licence)
  * [Historique et compatibilité](#historique-et-compatibilité)

Les pages de manuel sont dans `man/` : `v32as(1)`, `v32asld(1)`,
`v32as-language(7)`, `v32as-inc(3)`, `v32as-control(3)`.

Autres documents : **[`docs/USAGE.fr.md`](docs/USAGE.fr.md) (un guide orienté
tâches : commencez par là)**, `docs/PREPROCESSOR.md` (notes de conception du
préprocesseur), `docs/OBJECT.md` (formats d'objet et d'archive), `CHANGES.md`
(ce qui a changé à quelle étape). Note : ces autres documents sont en anglais.

## Démarrage rapide

```
    v32as game.asm              # écrit game.vbin (un programme de cartouche), comme l'« assemble game.asm » officiel
    v32as -b bios.asm           # un BIOS à la place (base 0x10000000)
    v32as -g game.asm           # écrit aussi game.vbin.debug, pour un débogueur/simulateur
    v32as -T -c game.asm        # affiche à la place un listage des encodages
    v32as -i                    # le mode interactif : tapez des instructions, voyez-les assemblées
    v32as --standard game.asm   # exactement le comportement de l'assembleur officiel, sans extensions
```

Les paquets de publication (un zip par système, depuis la page GitHub
Releases) contiennent `v32as`, `v32asld`, les bibliothèques de macros dans
`include/v32as/` à côté d'eux (trouvées là sans `-I`), ainsi que ces documents
et les pages de manuel sous `docs/v32as/`. Décompressez-en un dans votre
dossier `v32tools`, ou n'importe où dans votre `PATH`.

## Compiler, installer et tester

```
    make            # construit bin/v32as et bin/v32asld  (nécessite flex, bison, cc ; readline optionnel)
    make test       # tests différentiels/golden (y compris les fichiers .debug) (POSIX sh + od + cmp + grep ; GNU ou BSD)
    make realcheck  # contre l'assembleur officiel, construit depuis une copie de ComputerSoftware (voir plus bas)
    make install    # v32as, v32asld, lib/*.inc, les pages de manuel et les documents dans ~/bin, ~/share/v32as/lib, ~/man et ~/share/doc/v32as
    make install PREFIX=/usr/local     # ou tout autre PREFIX : PREFIX/bin, PREFIX/share/man, PREFIX/share/v32as/lib, PREFIX/share/doc/v32as
    make sysinstall # make install PREFIX=/usr/local (PREFIX et DESTDIR sont respectés)
    make uninstall  # retire ce que install a posé (mêmes PREFIX/DESTDIR/BINDIR/MANDIR/LIBDIR/DOCDIR)
    make mancheck   # vérifie les pages de manuel (sh et awk simples, groff non requis)
    make version    # inscrit la version de l'en-tête dans v32asld et les pages de manuel ; VERSION=YYYYMMDD-status en fixe d'abord une nouvelle
    make archive    # ../v32as-project-YYYYMMDD.zip (la date de VERSION), sans les produits de construction
```

`install` respecte `DESTDIR` (pour le paquetage) et les répertoires peuvent
être réglés un par un : `BINDIR`, `MANDIR`, `LIBDIR`, `DOCDIR`.
`DEST_LANG=en|es|fr` (`en` par défaut) choisit la langue des documents
installés (README, `docs/USAGE.md`) et des pages de manuel `v32as(1)` et
`v32asld(1)`, par exemple `sudo make sysinstall DEST_LANG=es` ; les autres
pages de manuel (sections 3 et 7) et les documents de conception
(`docs/OBJECT.md`, `docs/PREPROCESSOR.md`) n'existent qu'en anglais. La
version est la ligne `VERSION` de `inc/v32as.h` ; `make version
VERSION=20261126-dev` (`V=` fonctionne aussi) la réécrit, ainsi que
`src/ld.c` et la ligne `.TH` de chaque page de manuel. `make debug` construit
avec `-g -O0`. Une construction CMake existe aussi (`cmake -S . -B
build-cmake` ; voir l'en-tête de `CMakeLists.txt` pour `install`, `ctest` et
`cpack`). GNU readline est utilisé pour le mode interactif quand ses fichiers
de développement sont trouvés (`make READLINE=0` pour s'en passer). `make
clean` supprime les produits de construction.

Organisation : `src/` les sources (flex, bison, C), `inc/` les en-têtes,
`lib/` les bibliothèques de macros pour `%include`, `man/` les pages de
manuel, `docs/` les notes de conception, `tests/`, `experiments/` ; `make`
place les fichiers générés dans `build/` et les outils dans `bin/`.

## Utilisation

```
    v32as [-b|-c|-O ADDR] [-o FILE] [-g[MODE]] [--map FILE] [-r FILE] [-w] [-D NAME[=VAL]] [-U NAME] [-I DIR]
          [--rom-xml FILE] [--build-scripts[=sh,bat]] [--standard] [-T|-n] [-E [-P]] [-s] [-v] [FILE...|-]
    v32as -i [-b|-c|-O ADDR] [-T|-n] [-D NAME[=VAL]] [-U NAME] [-I DIR] [-w] [-v] [--standard]
```

Par défaut, `v32as FILE.asm` fait ce que fait l'`assemble FILE.asm` officiel :
il écrit `FILE.vbin`, un VBIN pour une cartouche. (En lisant l'entrée
standard, la sortie va sur la sortie standard.)

- `-i` démarre le mode interactif (REPL) : chaque ligne est assemblée dès
  qu'elle est saisie ; les commandes sont `.help .labels .dump .reset .quit`.
  Il n'accepte aucun fichier d'entrée (voir `-i` dans [la référence des options](#compatibilité-et-diagnostics)).
- Sans fichier d'entrée et avec un terminal sur l'entrée standard, v32as
  affiche son résumé d'utilisation (sur la sortie d'erreur, code de retour 2).
- `-` lit l'entrée standard, et il en va de même quand on l'exécute sans
  fichier alors que l'entrée standard est redirigée ou reçoit un tube ; le
  VBIN va alors sur la sortie standard. Les anciens mots nus (`bios`, `cart`,
  ...) fonctionnent toujours.
- Plusieurs fichiers d'entrée partagent un même état de préprocesseur
  (macros, définitions, contextes).
- Les erreurs et le fichier `-g` pointent vers le fichier et la ligne que vous
  avez écrits, y compris à travers `%include` et les macros (sur une ligne où
  une macro s'est développée, la colonne est omise).
- v32as est économe en mémoire (il fonctionne aussi sur v32nix, le système de
  type Unix de Vircon32) : un fichier est préprocessé et, pour la sortie
  binaire, assemblé une ligne à la fois. La manière dont cela a été fait est
  décrite dans `CHANGES.md` (2026-10-27 et 2026-11-21).

Code de retour : 0 en cas de succès (les avertissements ne le changent pas),
1 pour une erreur d'assemblage ou de préprocesseur ou un fichier d'entrée
illisible, 2 pour une erreur d'utilisation (options erronées ou en conflit, ou
aucun fichier d'entrée sur un terminal) ou un fichier de sortie qui ne peut
pas être écrit. `v32asld` suit la même convention : 1 pour une erreur d'édition
de liens, 2 pour une erreur d'utilisation, un objet invalide, une bibliothèque
`-l` introuvable ou un fichier de sortie qui ne peut pas être écrit.

## Référence des options

Voici la liste unique des options de `v32as`. `v32as --help` affiche la même
liste sous forme abrégée ; la page de manuel `v32as(1)` en donne le texte
complet.

### Sortie et cible

- `-o, --output=FILE` nomme la sortie. Par défaut, c'est le fichier d'entrée
  dont l'extension est remplacée par `.vbin` (`.v32ob` avec `-C`), ou la sortie
  standard quand on lit l'entrée standard.
- `-b, --bios` assemble pour le BIOS (0x10000000) ; `-c, --cart` pour une
  cartouche (0x20000000, la valeur par défaut) ; `-O, --origin ADDR` pour
  n'importe quelle adresse de base.
- `--v32x` (avec `--origin`) écrit un exécutable v32nix au lieu d'un VBIN : un
  en-tête de 8 mots (`V32X`, version 1, mots d'en-tête, mots de programme, mots
  de données, entrée 0, origine, indicateurs 0), puis le programme. Les mots de
  données (mis à zéro par l'`exec` de v32nix après le programme) viennent de
  `--v32x-data N`, ou du `%define __V32X_DATAWORDS N` de la source (ce
  qu'écrit `v32cc --data-after-text`). Sur la console, un mot de fichier est un
  mot machine, comme le lit l'`exec` de v32nix.
- `-T, --text` affiche un listage lisible (les encodages des instructions) au
  lieu d'écrire un VBIN. Les lignes `OFFSET:` (adresse) ne sont affichées que
  si `-b`, `-c` ou `-O` donne une base ; `-n, --none` est `-T` sans adresse de
  base. (`-B, --binary` existe toujours, et ne fait que dire ce qui est déjà
  le défaut.)
- `-C, --object` écrit un objet relogeable `FILE.v32ob` pour l'éditeur de
  liens ; voir [Objets et éditeur de liens](#objets-et-éditeur-de-liens). Il ne
  peut pas être combiné avec `-b -c -O -g -r -T -n -E -B` ni `--standard` ;
  `--map` est permis.

### Sorties de débogage et de symboles

- `-g, --debug[=MODE]` écrit aussi `OUTPUT.debug` (CSV `address,file,line[,label]`,
  une ligne par instruction ; avec des marqueurs `;@src` dans la source, une
  par marqueur) à côté du fichier de sortie. Un `-g` simple est identique
  octet pour octet au `-g program` de l'assembleur officiel (adresses de mots
  absolues, p. ex. `0x20000000`). `-g vbin` / `-g v32` donnent de vrais
  décalages en octets dans le fichier .vbin (en-tête de 0x0C) ou .v32
  (en-tête de 0x8C) ; les versions de l'outil officiel pour ces deux modes
  affichent des valeurs débordées. Un fichier source nommé littéralement
  `program`, `vbin` ou `v32` doit être écrit `./program`. Format : [Le fichier .debug](#le-fichier-debug).
  Avec des lignes `;@var` dans la source, `OUTPUT.vars` est écrit aussi, et
  `OUTPUT.sym` toujours (pas avec `--standard`) : [Le fichier .sym](#le-fichier-sym).
- `--map FILE` : FILE est le fichier de débogage du compilateur
  (`asmfile,asmline,cfile,cline[,function]`, tel que l'écrit `v32cc -g`). Le
  fichier `.debug` a alors la même disposition, mais chaque instruction nomme
  le fichier et la ligne C (et la fonction dans la colonne d'étiquette), ce que
  lit la superposition C de v32sim. Les instructions dont la ligne d'assemblage
  ne figure pas dans FILE sont omises. `--map` fonctionne aussi avec `-C` et
  pour les diagnostics :
  - Les noms de fichiers sont comparés d'abord exactement, puis par nom de base
    (la plus longue fin de répertoire en commun l'emporte), de sorte que
    `main.asm` dans la carte correspond à `out/kernel/main.asm`. Si aucune
    instruction ne correspond, un avertissement nomme les noms trouvés dans la
    carte.
  - Avec `--map FILE` (sans besoin de `-g`), chaque erreur et chaque
    avertissement de l'assembleur ou du préprocesseur est suivi de
    `a.c:123: note: the assembly line above was generated from here` (la
    fonction est nommée quand la carte la connaît) ou, pour une ligne sans
    entrée propre, de `... it follows the code generated from here` nommant la
    ligne cartographiée précédente la plus proche.
  - Avec `-C`, l'objet stocke le fichier et la ligne C (et la fonction) de
    chaque instruction à la place de ceux de l'assemblage ; les instructions
    que la carte ne connaît pas sont omises, comme avec `-g --map`. `v32asld -g`
    écrit alors le fichier `.debug` au niveau du C, identique à ce que
    produirait `v32as -g --map` sur la source jointe.
- `-r, --relocs FILE` écrit `index,0xVALUE` pour chaque mot qui contient une
  adresse résolue à partir d'une étiquette (code ou données, `label+N` inclus)
  ou calculée à partir de l'adresse de l'instruction elle-même (`MOV Rn, IP`,
  les adresses de modification de `IR`/`IV`). Les nombres simples qui
  ressemblent par hasard à des adresses ne sont jamais listés, de sorte qu'un
  chargeur peut reloger un module en ajoutant la différence de base à ces
  mots. Format : [Le fichier -r](#le-fichier--r-relocalisation).
- `--symbols FILE` écrit `address,name,kind` par étiquette, trié par adresse ;
  kind `F` (du code suit), `D` (des données suivent, ou rien). `--symbols-all`
  ajoute les étiquettes locales au fichier sous la forme `function.local`, et
  chaque `%define global_NAME number` en kind `V`, et chaque `DATASYM+N` auquel
  le programme se réfère (voir `datasym`) sous la forme `N,DATASYM+N,V` : le
  décalage dans la colonne d'adresse. Indisponible avec `-C` (utilisez
  `v32asld --symbols`), `-E` ou `-i`.
- `-MD`, `-MMD`, `-MP`, `-MF FILE`, `-MT TARGET` écrivent les sources, les
  fichiers `%include` et les fichiers `datafile` lus, sous forme de règle de
  Makefile (fichier par défaut : le nom de la sortie avec `.d`). `-MF` et `-MT`
  impliquent `-MD` ; `-MP` ajoute une règle vide pour chaque dépendance qui
  n'est pas une source nommée.

### Options du préprocesseur

- `-D, --define NAME[=VALUE]` (la valeur vaut 1 par défaut), `-U, --undef NAME`,
  traités dans l'ordre de la ligne de commande. Un `%define` d'un nom `-D`
  avertit (le fichier l'emporte quand même) ; `-D!NAME=VALUE` ou
  `--force-define NAME=VALUE` fait l'emporter la ligne de commande. Un
  `%define` qui change la valeur d'une macro avertit aussi (`-Wredefine`,
  activé par défaut ; `-Wno-redefine`, `-w` et `--standard` le font taire).
  `-Wno-redefine` ne fait pas taire l'avertissement sur un nom `-D` ; `-w` le
  fait.
- `-I, --include DIR` ajoute DIR au chemin de recherche de `%include` et
  `datafile` (peut être répété).
- `-E, --preprocess` affiche la source préprocessée avec des marqueurs
  `# LINE "FILE"` ; `-P` (avec `-E`) omet les marqueurs.
- `-Wmarkers` (désactivé par défaut ; `-Wno-markers`) avertit d'un `;@src`
  dont le `FILE:LINE` est mal formé (pas de `:`, une ligne qui n'est pas un
  nombre >= 1, rien après `;@src`), d'un `;@var` (sans NAME ou sans `loc=`),
  d'un `;@scope` (ni `{` ni `}`), d'un `;@scope }` déséquilibré et d'un bloc
  laissé ouvert. Un tel `;@src` est traité comme `;@src -` (entrée `-,0`) :
  pas de source vaut mieux qu'une mauvaise ligne. `-w` le fait taire.

### Définition de la ROM et scripts de construction

Toutes ces options sont des extensions (une erreur sous `--standard`) et
nécessitent un VBIN de BIOS ou de cartouche écrit dans un fichier (pas `-C`,
`-T`, `-n`, `-E`, `-O`, `--v32x` ni l'entrée standard). Rien n'est écrit tant
que l'une d'elles n'est pas donnée : un simple `v32as FILE.asm` n'écrit
toujours que `FILE.vbin`.

L'ensemble de la fonctionnalité est décrit dans [Définition de la cartouche et
scripts de construction](#définition-de-la-cartouche-et-scripts-de-construction).

- `--rom-xml FILE` écrit aussi la définition de ROM que lit `packrom`, à partir
  des `%title`, `%version`, `%texture` et `%sound` de la source.
- `--rom-xml-opt FILE` écrit la même chose pour la construction faite à partir
  de la sortie de `v32opt` (binaire `NAMEOpt.vbin`, titre `<title> [optimized]`).
- `--build-scripts[=FORMATS]` écrit `Make.sh` et `Make.bat` (FORMATS : `sh`,
  `bat` ou `sh,bat`, la valeur par défaut) à côté de la première source (dans
  son parent pour une disposition `src/` + `obj/`), ainsi que les définitions
  qu'ils utilisent (`NAME.xml`, `NAMEOpt.xml`) sauf si `--rom-xml` nomme la
  première.
- `--no-build-scripts` ne les écrit jamais (les scripts le passent au `v32as`
  qu'ils lancent ; la variable d'environnement `V32AS_BUILD_SCRIPT` aussi).
- `--build-script-dir DIR` écrit les scripts dans DIR (qui doit exister) à la
  place.

### Compatibilité et diagnostics

- `--standard` : exactement l'assembleur officiel ; voir [`--standard`](#--standard--exactement-lassembleur-officiel).
- `-w, --no-warnings` inhibe tous les avertissements.
- `-s, --step` fait une pause par instruction ; `-v, --verbose` montre les
  champs de bits encodés (sur la sortie d'erreur). Sous `--standard`, `-s` ne
  fait rien et `-v` affiche les messages d'étape officiels.
- `--stats` affiche sur la sortie d'erreur, à la fin, l'utilisation propre du
  tas par v32as : le pic et ce qui est vivant alors, en octets et en mots de 4
  octets, la même chose après le préprocessage (pour un fichier traité au fil
  de l'eau, après son assemblage aussi), et le nombre d'allocations (compté aux
  appels de l'allocateur ; « not available » là où la bibliothèque C ne peut
  pas donner la taille d'un bloc). Construit avec `v32cc` (v32nix,
  `__V32CC__`), il affiche des mots à la place, d'après l'allocateur de la
  bibliothèque C (`v32_heap_info`) : le **pic de break** (la taille du tas,
  trous et en-têtes de 3 mots compris, ce que doit contenir un emplacement de
  processus ; échantillonné après chaque allocation de v32as, donc une borne
  inférieure, la bibliothèque ne tenant pas de pic), et les mots utilisés,
  libres et dans le plus grand trou après le préprocessage (après
  l'assemblage, pour un fichier traité au fil de l'eau) et à la fin.
- `-i, --interactive` démarre le mode interactif (REPL) sur l'entrée standard :
  chaque ligne est assemblée et son encodage affiché dès qu'elle est saisie ;
  `.help .labels .dump .reset .quit` sont ses commandes, un `%macro` ou un
  `%if` se poursuit sur les lignes suivantes (invite `...>`). Il n'accepte
  aucun fichier d'entrée. Il se combine avec `-b -c -O -T -n` (sans aucune
  d'elles, les adresses commencent à 0), `-D -U -I`, `-w`, `-v` et
  `--standard` ; avec une option qui écrit un fichier ou exige un programme
  complet (`-o -E -B -C -g -r --symbols --map -MD --rom-xml --rom-xml-opt
  --build-scripts --v32x -s`), c'est une erreur d'utilisation. L'édition de
  ligne et l'historique nécessitent GNU readline (ou libedit de macOS) à la
  construction.
- `-h, --help`, `-V, --version`.

## Fichiers de sortie

### Le fichier .debug

`OUTPUT.debug` est un CSV, `address,file,line[,label]`, avec une ligne par
instruction ; il est écrit à côté du fichier de sortie (pour `game.vbin`,
`game.vbin.debug`). Un `-g` simple est identique octet pour octet au
`-g program` de l'assembleur officiel ; la première étiquette définie à une
adresse est écrite en quatrième colonne.

**Marqueurs `;@src` (pas besoin de `--map`).** Une ligne de commentaire à
elle seule, `;@src FILE:LINE` (ou `FILE:LINE:COL` ; `;@src -` pour « pas de
source »), telle que l'écrit `v32lua -g` (voir V32DBG-SPEC). Quand la source
contient des marqueurs, `OUTPUT.debug` reçoit d'eux `ADDRESS,FILE,LINE[,FUNCTION]` :

**une entrée par marqueur, à l'adresse de la première instruction qui le
suit** (donc deux marqueurs consécutifs nommant la même ligne donnent deux
entrées ; des marqueurs sans instruction entre eux n'en donnent qu'une, pour
le dernier). Une instruction qu'aucun marqueur ne commence n'a pas d'entrée
(celles qui précèdent le premier marqueur n'en ont aucune). FILE est séparé de
LINE au dernier `:` (ou aux deux derniers quand les deux se terminent par des
chiffres), de sorte qu'une lettre de lecteur ne pose pas de problème. Le
marqueur doit être la première chose de sa ligne (un `;@src ...` placé après
une instruction n'est qu'un commentaire). Les marqueurs traversent `%include`
et les macros en tant que commentaires qu'ils sont ; le nom de fichier du
marqueur est ce qui est écrit.

- **`;@src -`** (demande de v32sim) : la première instruction qui le suit
  reçoit l'entrée `ADDRESS,-,0` (fichier `-`, ligne 0, pas de fonction), pour
  que le simulateur sache où la dernière ligne source se termine et où le
  runtime commence. Un marqueur mal formé compte comme `;@src -`. Les
  instructions avant le premier marqueur n'ont toujours pas d'entrée.
- **FUNCTION** est la fonction dans laquelle se trouve l'instruction, écrite
  `NAME` (`__function_main:` -> `main`, `__function_f__l0:` -> `f__l0`) ; pas
  de quatrième colonne hors des fonctions. Chaque entrée d'une fonction l'a,
  pas seulement la première. Contrairement au `-g` simple (qui écrit la
  première étiquette à l'adresse), aucune autre étiquette n'est écrite en mode
  marqueurs. Une fonction commence à une étiquette `__function_NAME:` et se
  termine à la ligne `;@fn __function_NAME ...` que le compilateur Lua écrit
  après son corps, à un `;@src -`, ou à la fin du programme. Les fonctions
  s'imbriquent (le corps d'une fermeture Lua se trouve dans le code de son
  parent, et le code du parent qui le suit est de nouveau celui du parent), et
  le code entre fonctions ou après `;@src -` (initialisation globale, runtime)
  n'a pas de fonction. Un `;@fn` écrit *au-dessus* d'une étiquette (la forme
  `clobbers=` du runtime) n'est pas une fin, pour aucun langage.
  `__function_NAME_return:` n'est pas une fonction.
- **`;@lang X`** (une ligne de commentaire à elle seule, avant la première
  fonction ; la première compte, les suivantes sont ignorées) : le langage du
  producteur, `c` pour `v32cc`, `lua` pour `v32lua`. Deux choses en dépendent.
  (1) Avec n'importe quel langage **autre que `lua`** (comparé sans tenir
  compte de la casse), les fonctions ne s'imbriquent pas, donc **la prochaine
  étiquette `__function_NAME:` de premier niveau termine la fonction
  précédente** : ses plages et blocs `;@var` (fermés ou non) se terminent à
  l'adresse de cette étiquette, exactement là où la colonne FUNCTION change, et
  aucun `;@src -` ni `;@fn` n'est nécessaire après le corps. Sans `;@lang`, ou
  avec `lua`, rien ne change. (2) Le langage est enregistré dans les fichiers
  annexes, voir plus bas.
- **`# lang X`** : quand la source contient `;@lang X` (et que `--map` n'est
  pas donné), la **première ligne de `OUTPUT.debug`** est le commentaire
  `# lang X` (en mode marqueurs seulement : avec `-g` et des entrées issues de
  marqueurs `;@src` ; un fichier `-g` simple et un fichier `--map` sont
  inchangés) et la première ligne de `OUTPUT.vars` (quand il est écrit) est le
  même commentaire. **Les lecteurs doivent ignorer toute ligne qui commence
  par `#`.** Sans `;@lang`, il n'y a pas de telle ligne ; sous `--standard`,
  les marqueurs sont des commentaires, donc il n'y en a pas non plus.
- **`--map FILE` fonctionne toujours** : pour une instruction que la carte
  nomme, l'entrée de la carte est écrite ; chaque autre marqueur a toujours la
  sienne. `v32cc` n'écrit aucun marqueur aujourd'hui, donc sa sortie ne change
  pas.
- **`-C`** : l'objet stocke les entrées de marqueurs à la place des lignes
  d'assemblage (format d'objet version 2 ; le mot de fonction `0xFFFFFFFE`
  signifie « pas de fonction, et `v32asld -g` ne doit pas ajouter
  d'étiquette »), de sorte que `v32asld -g` écrit le même fichier que
  `v32as -g` sur la source jointe. La précédence de `--map` est la même.
- **`--standard`** : les marqueurs sont des commentaires ; `-g` écrit les
  lignes d'assemblage comme avant.

### Le fichier .sym

`OUTPUT.sym` (avec `-g`, pas avec `--standard` ; `OUTPUT` est le nom du
fichier de sortie, donc `prog.vbin.sym`) : les symboles avec leurs adresses
**finales**, au format du `NAME.asm.sym` de v32cc, de sorte qu'un même fichier
serve à `nm`, à une trace d'appels de panique et à un débogueur. Les adresses
sont toujours celles que voit le processeur (`0x20000003`), quel que soit le
mode `-g`. D'abord la ligne facultative `# lang X` (comme dans `.debug`), puis :

```
# v32as symbols v1: kind,name,address,words,cfile,cline
F,main,0x20000003,10,t.c,3          une fonction : une étiquette de premier niveau suivie de code ; « __function_ » est retiré du nom
V,counter,0x00000010,1,,            une variable de compilateur : %define global_NAME number (words 0 : inconnu) ou var_NAME number (v32lua, words 1)
R,_helper_data,0x20000016,2,,       une étiquette de premier niveau suivie de données
```

Ordre : fonctions, variables, données, chacune par adresse. `words` est la
distance jusqu'à l'étiquette de premier niveau suivante (jusqu'à la fin du
programme pour la dernière) ; les étiquettes locales au fichier
(`function.local`) ne terminent pas une fonction. `cfile,cline` est la
position source de la première instruction de la fonction (vide quand il n'y
en a pas). Les adresses et tailles `V` de `NAME.asm.sym` (qui connaît les types
C) sont les meilleures quand les deux existent ; ce fichier ajoute les
adresses finales des fonctions et des données de ROM, que `NAME.asm.sym`
donne sous la forme `@label` / `@rodata+OFF`. `v32asld -g` écrit le même
fichier pour les objets liés, sans lignes `V` (un objet ne porte pas de
`%define`). Les lecteurs doivent ignorer les lignes qui commencent par `#`.

### Le fichier .vars

`OUTPUT.vars` (avec `-g`, à côté de `OUTPUT.debug` ; seulement quand la source
contient des lignes `;@var` ; un ancien fichier est sinon supprimé) : les
variables locales sous forme de plages d'adresses, que seul l'assembleur peut
calculer. Adresses comme dans `.debug` (`-g`, `-g vbin`, `-g v32`), END étant
la première adresse hors portée. Une ligne CSV chacune, dans l'ordre de START
(un bloc avant ses variables) :

```
scope,START,END,DEPTH[,FUNCTION]
var,START,END,NAME,LOC,TYPE,FLAGS,DEPTH[,FUNCTION]
```

`LOC` est le `loc=` tel qu'écrit (`BP-3`, `BP+2`, `R5`, `@0x20003000` ; `-`
pour `optimized-out`), sauf qu'un **symbole est résolu** : voir plus bas.
`TYPE` est le `type=` (`-` s'il n'y en a pas), `FLAGS` les mots nus qui le
suivent (`boxed`, `optimized-out`, joints par `+` ; `-` s'il n'y en a pas ; les
mots `key=value` autres que `loc`/`type` sont ignorés). `DEPTH` vaut 0 pour la
portée propre de la fonction et 1, 2, ... pour les blocs qu'elle contient, de
sorte que le simulateur puisse choisir la plus interne de deux variables de
même nom. Les lignes `scope` sont les blocs `;@scope {` ... `}`.

- **Plages.** Un `;@var` est dans la portée à partir de la première
  instruction qui le suit. Il se termine au `;@scope }` du bloc dans lequel il
  a été déclaré ou, sans bloc, à la fin de la fonction ; ou à un autre `;@var`
  du **même nom dans le même bloc** (un nouvel emplacement, p. ex. le
  `loc=R5` de l'optimiseur, ou `;@var NAME optimized-out`). Un `;@var` de même
  nom dans un bloc *imbriqué* masque l'externe sans le terminer. Les plages
  vides (`do end`) sont omises. Un `;@scope }` déséquilibré est ignoré et un
  bloc ouvert est fermé avec sa fonction (les deux avertissent sous
  `-Wmarkers`).
- **`;@home FROM TO`** (un optimiseur a déplacé des variables ; `v32opt`
  l'écrit quand il promeut un emplacement de pile en registre) : chaque
  variable dans la portée dont l'emplacement (son `loc=` actuel) est `FROM`
  reçoit à partir de ce point un nouvel enregistrement, dans le même bloc,
  avec l'emplacement `TO` : `;@home BP-3 R5` où la valeur passe dans R5,
  `;@home R5 BP-3` où elle revient. `TO` peut être `optimized-out` (la valeur
  a disparu ; l'enregistrement reçoit cet indicateur et un `loc` à `-`).
  L'ancien enregistrement se termine ici, de sorte que `NAME.vbin.vars` liste
  la variable une fois par emplacement. Un `;@home` qui ne correspond à rien
  est ignoré. Comme les autres lignes `;@`, c'est un commentaire pour tout
  autre outil.
- **Emplacements symboliques.** Un `loc=SYMBOL` où SYMBOL est une étiquette,
  `label+N` / `label-N` (N décimal ou `0x...`), ou un nom `%define`-é comme un
  nombre ou comme l'une de ces formes (le `%define var_NAME 0x78` de v32lua ;
  le texte de remplacement doit être exactement cela, aucune autre expression)
  est écrit dans la colonne LOC sous la forme `@0xADDRESS` quand il est connu à
  la fin de l'assemblage. Une étiquette donne son adresse dans le style des
  colonnes START et END du mode `-g` (absolue pour `-g`, le décalage dans le
  fichier pour `-g vbin` / `-g v32`) ; un nombre `%define`-é est une valeur
  simple (une adresse de RAM) et est écrit tel quel, quel que soit le mode.
  Les registres (`BP-3`, `BP+2`, `R5`, ...), `@0x...` et `-` sont laissés tels
  qu'écrits, de même qu'un symbole inconnu (qui avertit aussi sous
  `-Wmarkers`). Les `%define` en vigueur à la fin du fichier comptent.
- **Multi-fichiers (`-C`).** L'objet stocke les enregistrements (format
  d'objet version 3, ou 4 avec un `;@lang`, voir `docs/OBJECT.md`) et
  `v32asld -g` écrit `OUTPUT.vars` et `OUTPUT.debug` comme le ferait `v32as -g`
  pour la source jointe ; `v32asld -R` les conserve. Un nombre `%define`-é est
  résolu à l'assemblage ; une étiquette (de l'objet, ou une globale d'un autre
  objet) est résolue par `v32asld` avec son adresse finale, dans le mode `-g`
  donné à l'éditeur de liens. Plusieurs objets : le premier `;@lang` trouvé est
  celui qui est écrit. Testé avec deux fichiers, une fonction chacun, et avec
  des fichiers de style C.
- `--standard` : les lignes `;@` sont des commentaires ; pas de `.vars`.

Exemple (`m.asm` contient les lignes `;@src prog.lua:1`, `__function_main:`,
`;@scope {`, `;@var x loc=BP-1 type=int`, deux instructions `mov`,
`;@scope }`, `;@fn __function_main` et un `hlt` ; `v32as -g m.asm` écrit
`m.vbin.vars` contenant) :

```
scope,0x20000000,0x20000004,1,main
var,0x20000000,0x20000004,x,BP-1,int,-,1,main
```

### Le fichier -r (relocalisation)

**Format du fichier `-r`** (un chargeur peut en dépendre) : une ligne par mot,
triée par `index` (croissant, sans doublon) ; `index` est l'**indice de mot, à
partir de zéro, dans l'image**, pas un décalage en octets et sans compter
l'en-tête VBIN de 3 mots (la première instruction est le mot 0) ; `0xVALUE`
est la valeur **telle qu'assemblée**, c'est-à-dire à la base pour laquelle
l'image a été assemblée ou liée, de sorte qu'un chargeur qui place l'image à
`new_base` ajoute `new_base - assembled_base` à un mot `C` (et, pour un mot
`D`, le déplacement des données) et laisse les autres mots intacts ; la
troisième colonne facultative est `C` ou `D` et est présente sur chaque ligne
si la source (ou n'importe quel objet lié) déclare un `datasym`, sur aucune
sinon. `v32as -r` comme `v32asld -r` écrivent exactement ceci.

Une source qui déclare `datasym NAME` (syntaxe étendue ; la valeur de `NAME`
est 0, donc `NAME+17` est un mot contenant 17) obtient une troisième colonne,
`,C` pour une adresse de code (qui se déplace avec l'adresse de chargement) et
`,D` pour une référence à un datasym (qui se déplace avec la base des données
du module). `v32cc --pic-data` écrit ainsi l'adresse de chaque globale. Un
datasym peut aussi s'utiliser avec `-C` ; `v32asld -r` écrit alors la même
colonne.

### Le fichier --symbols

Une ligne par étiquette, `0xADDRESS,name,kind`, triée par adresse. Kind `F` :
du code suit ; `D` : des données suivent (ou rien).

Avec `--symbols-all` : les étiquettes locales au fichier sous la forme
`function.local`, kind `V` pour chaque `%define global_NAME number` (le nombre
dans la colonne d'adresse), et `N,DATASYM+N,V` pour chaque `DATASYM+N` auquel
le programme se réfère. `v32asld --symbols` écrit la même chose pour un
programme lié (étiquettes locales sous le nom que l'assembleur leur a donné,
p. ex. `_f__loop`).

## Objets et éditeur de liens

`v32as -C x.asm` écrit un objet relogeable `x.v32ob` ; `v32asld a.v32ob
b.v32ob -o prog.vbin [-b|-c|-O ADDR] [-g] [-r FILE]` les lie. `global _name` /
`extern _name` (noms simples, sans `%`) disent quelles étiquettes sont
partagées. Lier équivaut à assembler les sources comme un seul fichier ; les
objets portent les enregistrements de débogage, donc `v32asld -g` écrit le
même fichier `.debug`. Format et détails : `docs/OBJECT.md`.

`datasym` est accepté par `-C` ; l'objet l'enregistre (format d'objet version
2) et `v32asld -r FILE` (aussi `--emit-relocs FILE`) écrit la troisième
colonne, `,C` ou `,D`, comme le fait `v32as -r`. Un datasym a la valeur 0 dans
chaque objet et n'est pas déplacé par l'édition de liens.

### Options de v32asld

- `-o, --output FILE` (par défaut `a.vbin` ; `a.v32ob` avec `-R`) ; `-b, --bios`,
  `-c, --cart` (par défaut), `-O, --origin ADDR`.
- `-g, --debug[=MODE]` écrit `OUTPUT.debug` (et `OUTPUT.vars` quand les objets
  ont des enregistrements `;@var` ; les deux commencent par `# lang X` quand
  les objets ont été faits à partir d'une source avec `;@lang X`, et les
  emplacements symboliques des `;@var` sont résolus ; aussi `OUTPUT.sym`, voir
  [Le fichier .sym](#le-fichier-sym)) ; `-r, --relocs FILE` (aussi
  `--emit-relocs FILE`) la liste de relocalisations (format ci-dessus) ;
  `--symbols FILE` la table des étiquettes.
- `-A, --archive FILE` fabrique une archive (`V32-VARC`) des objets donnés, au
  lieu de lier. Les archives données comme opérandes ou avec `-L DIR -l NAME`
  (`DIR/NAME.v32a`, sinon `DIR/libNAME.a` ; `.` est toujours cherché en
  dernier) sont explorées après les objets, un membre n'étant lié que
  lorsqu'il définit un symbole encore indéfini.
- `--entry SYMBOL` place en premier un `jmp SYMBOL` de deux mots, sauf si le
  symbole est déjà le premier mot.
- `--v32x --data N [--entry SYMBOL|OFFSET]` écrit un exécutable V32X (en-tête +
  image liée à 0x100000 ; `-O` change la base ; `-b`/`-c` et `-g vbin|v32` sont
  refusés).
- `-R, --relocatable` est une édition de liens partielle : `v32asld -R -o
  ab.v32ob a.v32ob b.v32ob` (pas `-r`, qui est la liste de relocalisations)
  joint des objets en un seul objet. Les globales restent globales, un
  `extern` qu'une autre entrée définit devient une référence à celle-ci, les
  autres restent `extern` ; relocalisations, datasyms et enregistrements de
  débogage sont conservés, de sorte que lier `ab.v32ob` équivaut à lier
  `a.v32ob b.v32ob`. Les archives et `-l` fonctionnent comme dans une édition
  de liens complète. Il refuse `-b -c -O -g -r --symbols --entry --v32x`.
- `-w, --no-warnings` n'avertit pas d'un `extern` qu'aucun code n'utilise.
- Erreurs : ``undefined reference to `x' (first used in a.v32ob)``, une fois
  par symbole ; une globale définie par deux objets ; un objet invalide ou
  tronqué.

## Le langage

### Directives de données

`integer`, `float`, `pointer`, `string`, `zstring`, `datafile`. Un mot par
élément ; `string` est un mot par caractère plus un 0 final. `zstring` est
compacté par octets : quatre caractères par mot (le premier caractère dans
l'octet de poids faible), terminé par zéro, le dernier mot complété par des
zéros. (`zstring` n'existe pas dans l'assembleur officiel ; son fichier golden
est vérifié à la main, ce n'est pas une sortie officielle.) `datafile` lit des
mots petit-boutistes.

**Recherche de `datafile`** : chemin absolu ; le répertoire du fichier qui
contient la directive ; chaque `-I DIR` ; le répertoire courant. L'erreur
liste les endroits essayés : `cannot open data file "x" (looked in out/, ../data/, .)`.
`--standard` conserve les deux anciens endroits (le répertoire du fichier et
le répertoire courant ; pas de `-I`, comme pour `%include`).

`global`, `extern` et `datasym` sont décrits sous [Objets et éditeur de
liens](#objets-et-éditeur-de-liens) et `-r`.

### Nombres, noms, étiquettes

Hexadécimal `0xFF` et `FFh`, binaire `0b1010`, octal (un zéro initial),
décimal `42` et `#42`, flottants `3.14`, littéraux caractère `'A'`,
`true`/`false`. Une étiquette et une instruction peuvent partager une ligne ;
mnémoniques et registres sont insensibles à la casse. Une étiquette commence
par un tiret bas. Un port d'E/S dans `in`/`out` est un nom (`GPU_Command`) ou
un nombre sous l'une de ces formes (`0x200`, `512`), comme dans l'outil
officiel et avec le même encodage ; un nombre qui n'est pas un port connu
s'assemble avec un avertissement.

### Étiquette plus constante

Partout où une étiquette est acceptée comme valeur, `label + integer` ou
`label - integer` l'est aussi (une étiquette, une constante) : `mov R0, _tab+2`,
`mov R1, [_tab+3]`, `mov [_tab-1], R1`, `jmp _f+4`, `pointer _tab+1, _end`.
Elle est résolue comme une étiquette simple (références avant comprises).
`--standard` la refuse.

### `--standard` : exactement l'assembleur officiel

`--standard` désactive toutes les extensions, pour remplacer `assemble` sans
différence (et pour vérifier qu'un programme lui est portable) :

| domaine | dans `--standard` |
|---|---|
| nombres | `0b101`, `FFh`/`2Ah`, `#42` et `0X1F` sont des erreurs. Un zéro initial est **décimal** (`0100` vaut 100), comme le lit l'outil officiel. Un littéral décimal supérieur à 2147483647 et un flottant dénormalisé sont des erreurs ; `-0xFFFFFFFF` boucle (wrap), comme dans l'outil officiel |
| valeurs de port | une valeur de port nue (`GPUCommand_ClearScreen`) seulement comme valeur de `out`, comme dans l'outil officiel |
| listes de données | `integer 1, 2,` (une virgule finale) est accepté, comme dans l'outil officiel |
| noms | `IP`/`IR`/`IV` (registres internes) et `zstring` n'existent pas |
| directives | seulement celles d'origine, écrites en minuscules ; pas de `%if %elif %assign %eval %includeonce`, pas de macros de type fonction (`%define F(x) ...` définit la macro simple `F` dont la valeur commence par `(x)`), pas d'étiquettes `.local`, pas de symboles du style `__FILE__` |
| `%define` | une valeur qui contient son propre nom, ou un `%`, est une erreur |
| `%include` | `"file"` seulement, relatif au seul fichier qui inclut (pas de `-I`, pas de `<file>`) |
| opérandes | `label +/- integer` est une erreur |
| `datafile` | le chemin tel qu'écrit, depuis le répertoire courant, comme l'ouvre l'outil officiel ; puis (une tolérance) à côté du fichier source. Pas de `-I` |
| ligne de commande | un seul fichier d'entrée ; `-s` ne fait rien ; `-v` affiche les messages d'étape officiels |
| `%if` non terminé | un avertissement (l'outil officiel l'accepte silencieusement) |

Quelques éléments restent acceptés sous `--standard` alors que l'outil
officiel les refuse, parce qu'ils ne peuvent pas changer les octets d'un
programme qu'il accepte : une étiquette et une instruction sur une même ligne
(`_a: hlt`), les caractères de contrôle et une marque d'ordre des octets UTF-8
initiale dans la source. Dans l'autre sens, `_` seul n'est pas une étiquette
pour v32as, alors que l'outil officiel l'autorise, et les lignes ne se
terminent qu'à LF ou CRLF (pas à un CR isolé).

Sans `--standard`, le même programme est accepté aussi, avec un piège à
connaître : un nombre avec un zéro initial est **octal** ici (`0777` vaut 511)
mais **décimal** dans l'assembleur officiel. `v32as` avertit pour chacun de
ces littéraux ; utilisez `--standard`, ou retirez le zéro initial.

## Préprocesseur

La source est préprocessée avant d'être assemblée (`-E` montre le résultat).
Les notes de conception sont dans `docs/PREPROCESSOR.md`. Une directive est
une ligne dont le premier caractère non blanc est `%` ; les noms de
directives sont insensibles à la casse, les noms de macros ne le sont pas. Les
macros ne sont remplacées que dans le code : jamais à l'intérieur de chaînes,
de littéraux caractère, de commentaires ou de nombres.

### Tout ce que possède l'assembleur officiel

```
%define NAME tokens...      %undef NAME
%ifdef NAME   %ifndef NAME  %else   %endif
%include "file"             %error "message"   %warning "message"
```

`%define NAME value` est développé là où il est *utilisé* (liaison tardive), et
une macro n'est jamais redéveloppée à l'intérieur de sa propre expansion. Les
chemins de `%include` sont relatifs au répertoire du fichier qui inclut
(limite : 20 niveaux). Les blocs `%ifdef` ignorés suppriment toutes leurs
lignes, y compris les directives inconnues. Un bloc non terminé est une erreur
(l'outil officiel le laisse passer ; `--standard` aussi, avec un
avertissement).

### Extensions

- **Macros de type fonction** : `%define MUL(a, b) ((a) * (b))`, utilisée
  comme `MUL(3, 4)`. Les arguments sont développés avant la substitution. `#x`
  fabrique une chaîne à partir de l'argument, `a ## b` joint deux jetons
  (`%define LAB(n) _label_ ## n`), `...` / `__VA_ARGS__` prennent les
  arguments restants (et `, ## __VA_ARGS__` supprime la virgule quand il n'y
  en a aucun). `#` suivi d'un chiffre reste la syntaxe de nombre `#42`. Un nom
  suivi de `(` n'est une invocation que si la macro est de type fonction.
- **`%if expr`, `%elif expr`, `%elifdef`, `%elifndef`**, `%else`, `%endif`.
- **Expressions** (`%if`, `%assign`, `%eval`) : entiers (32 bits, avec
  bouclage) et flottants (32 bits), toutes les formes de nombres que lit
  l'assembleur, `defined(NAME)`, `true`/`false`, les noms de ports et de
  constantes que connaît l'assembleur, les opérateurs `( ) + - ! ~ * / % << >>
  < <= > >= == != & ^ | && || ?:` avec la précédence du C (`&&`, `||` et `?:`
  n'évaluent pas ce qu'ils sautent), et les fonctions `int() float() abs()
  min() max() floor() ceil() round()`. Une opération avec un opérande flottant
  est une opération flottante (`7/2` vaut 3, `7/2.0` vaut 3.5).
- **`%assign NAME expr`** calcule une valeur tout de suite : `%assign N N + 1`.
  (`%eval NAME expr` est identique.)
- **`%eval(expr)`** n'importe où dans une ligne est remplacé par la valeur :
  `mov R0, %eval(WIDTH * 2 + 1)` devient `mov R0, 641`. C'est ce qui permet aux
  symboles de participer à l'arithmétique, ce que ne peut pas faire
  `%define MATH A+B` : l'assembleur n'a pas d'expressions, donc la somme doit
  exister sous forme de nombre dans la source. Les flottants sont réécrits sous
  la forme la plus courte qui se relit comme le même flottant.
- **Étiquettes `.local`** : après une étiquette globale `_draw:`, le nom
  `.loop` signifie `_draw__loop` (donc `.loop:` la définit et `jt R1, .loop`
  l'utilise).
- **Symboles** : `__FILE__`, `__LINE__`, `__COUNTER__`, `__INCLUDE_LEVEL__`,
  `__V32AS__` (date de version sous forme de nombre), `__BASE__` (première
  adresse), et l'un de `__BIOS__`, `__CART__`, `__ORIGIN__`, `__NONE__`. Un
  `%define` du même nom l'emporte.
- **Inclusions** : `%include <name>` cherche dans les répertoires `-I DIR`,
  puis dans les répertoires de bibliothèques ; `%include "name"` essaie
  d'abord le répertoire du fichier qui inclut, puis la même liste. Les
  répertoires de bibliothèques sont le `LIBDIR` de `make install` et, à côté du
  programme, `lib`, `../lib`, `../share/v32as/lib`,
  `../Vircon32/v32tools/include/v32as` et `include/v32as` (la disposition des
  zips de publication), de sorte qu'un v32as installé ou décompressé trouve ses
  bibliothèques de macros sans `-I`. `%includeonce` dans un fichier fait de
  tout `%include` ultérieur de celui-ci une opération sans effet.
- **Ligne de commande** : `-D NAME[=VALUE]` (la valeur vaut 1 par défaut),
  `-U NAME`, `-I DIR`, `-E` (affiche la source préprocessée avec des
  marqueurs `# LINE "FILE"`, `-P` sans eux).
- **`%title`, `%version`, `%texture`, `%sound`** : la définition de ROM, pour
  `--rom-xml` et `--build-scripts` ; voir [Définition de la cartouche et
  scripts de construction](#définition-de-la-cartouche-et-scripts-de-construction).
- **`%idefine`, `%xdefine`, `%ixdefine`** : un define insensible à la casse ;
  un define dont la valeur est développée tout de suite plutôt que là où il
  est utilisé ; les deux.

### Macros qui écrivent des instructions

```
%macro LIMIT reg, lo=0, hi=255        ; paramètres nommés, avec valeurs par défaut
    imax  reg, lo
    imin  reg, hi
%endmacro

%macro COUNTDOWN reg, count
    mov   reg, count
%%top:                                ; privée à chaque expansion (devient __M<serial>_top)
    isub  reg, 1
    jt    reg, %%top
%endmacro

    LIMIT R3, , 100                   ; invoquée comme une instruction (des étiquettes peuvent précéder)
    COUNTDOWN R1, 10
```

- **`%macro NAME params` ... `%endmacro`**. Les paramètres sont `name`,
  `name=default` (une valeur par défaut le rend facultatif ; un argument vide
  prend la valeur par défaut) et un dernier `rest...` (ou `...`) qui prend les
  arguments restants. `%macro NAME 2`, `1-3` ou `1-*` fixe seulement le nombre
  d'arguments. Dans le corps, un nom de paramètre, ou `%1` `%2` ..., représente
  le texte de son argument, `%0` le nombre d'arguments et `%*` leur ensemble.
  Les arguments sont séparés aux virgules hors de `()` `[]` `{}` et des
  guillemets ; `{a, b}` passe une virgule comme partie d'un seul argument. Une
  macro peut être définie plusieurs fois si les nombres d'arguments diffèrent ;
  `%unmacro NAME` la supprime.
- Un nom de macro est **sensible à la casse** (`%imacro` la rend insensible) et
  **l'emporte sur une instruction** de même nom, avec un avertissement sauf si
  `%pragma shadow` (tout) ou `%pragma shadow NAME, ...` l'autorise.
- **`%rep count` ... `%endrep`** répète son corps (au plus 1 000 000 de fois) ;
  `%@` est le numéro de l'itération. `%exitrep` quitte la boucle. Les boucles
  s'imbriquent et peuvent contenir des macros et des conditionnelles.
- **`%exitmacro`** quitte la macro (aussi depuis l'intérieur d'un `%if` ou d'un
  `%rep`). **`%rotate n`** fait tourner ce que désignent `%1` `%2` ... (vers la
  gauche pour `n` positif), la façon de parcourir une liste variadique :
  `%rep %0` / `push %1` / `%rotate 1` / `%endrep`.
- **`%%name`** est une étiquette privée à une expansion d'une macro (une par
  itération pour un `%rep` hors d'une macro ; à l'intérieur d'une macro, elle
  appartient à la macro, donc utilisez `%%name_%@` pour une étiquette par
  itération).
- **Tests d'opérandes**, utilisables comme `%ifXXX` et `%elifXXX`, et niés avec
  `n` (`%ifnreg`) : `reg` (un registre du processeur), `sysreg` (`IP` `IR`
  `IV`), `num` (une expression constante ; un nom de port nu n'est pas un
  nombre), `str` (un littéral chaîne), `label` (`_name` ou `.name`), `port` (un
  nom de port d'E/S), `idn a, b` / `idni a, b` (le même texte, exactement ou à
  la casse près), `empty`, `macro NAME [count]`, `ctx NAME`. Le texte est
  d'abord développé par les macros. C'est ce qui permet à une macro de choisir
  entre `mov` et `in` selon ses opérandes, et de signaler un mauvais opérande
  par un vrai message (`%error "INC wants a register, got %1"`).
- **Chaînes** : `%strlen N "text"`, `%substr S "text", start [, length]`
  (start compte à partir de 1), `%strcat S "a", "b"`, `%defstr S tokens` (les
  jetons développés sous forme de chaîne).
- **Contextes** : `%push name` ... `%pop` (`%repl name` renomme). `%$x` est un
  nom privé au contexte le plus interne (`%$$x` : celui du dessous), ce qui
  permet à des macros comme `IF` / `ELSE` / `ENDIF` de partager des
  étiquettes. `%ifctx name` teste le contexte le plus interne.
- **`%pragma once`** (identique à `%includeonce`), `%pragma shadow ...`.

`lib/v32as.inc` fournit des macros prêtes à l'emploi : `INC DEC ZERO NEG CLAMP
SWAP PUSHALL POPALL WAIT_FRAMES` (toutes vérifient que leurs opérandes sont
des registres) et `lib/control.inc` fournit `IF` / `ELSE` / `ENDIF` et `WHILE`
/ `ENDWHILE` ; utilisez-les avec `%include <v32as.inc>` et `%include
<control.inc>` (pages de manuel `v32as-inc(3)` et `v32as-control(3)`). Un
v32as installé ou décompressé les trouve de lui-même ; depuis l'arborescence
des sources, ajoutez `-I lib`.

### Limites

Une expansion de macro est limitée à 64 niveaux, et une exécution à environ 4
millions de lignes rejouées (un `%rep` ou une récursion emballés s'arrêtent
avec une erreur). L'imbrication de `%include` est limitée à 20 niveaux et un
`%rep` à 1 000 000 d'itérations. Les erreurs venant de l'intérieur d'une
expansion sont signalées à la ligne de l'invocation avec un `note: in
expansion of macro 'X' (defined at file:line)` ; `-g` attribue aussi le code
généré à l'invocation.

## Définition de la cartouche et scripts de construction

Une cartouche est un programme plus les textures et les sons qu'il utilise,
empaquetés par `packrom` à partir d'une définition XML. Un programme écrit en C
(`v32cc`) dit ce qui va dans la ROM avec `#texture`, `#sound`, `#title` et
`#version` ; `v32as` a les mêmes quatre sous forme de directives de
préprocesseur, et peut écrire la définition ainsi que les scripts qui
construisent la cartouche. Le tout est une extension (pas sous `--standard`),
et ne change rien au programme assemblé.

```
%title   "Space Rocks"            ; le nom dans la cartouche (63 caractères au plus ; le dernier %title l'emporte)
%version 1.2                      ; MAJOR.MINOR, chiffres.chiffres (packrom n'accepte rien d'autre : sinon une erreur)
%texture TEX_FONT  "gfx/font.png" ; TEX_FONT devient le numéro de cette texture : 0, 1, ... par ordre d'apparition
%texture TEX_SHIP  "gfx/ship.png"
%texture           "gfx/old.vtex" ; le nom est facultatif
%sound   SND_BOOM  "snd/boom.wav" ; les sons sont numérotés à part, à partir de 0

    mov R0, TEX_SHIP              ; le numéro que prend select_texture
    out GPU_SelectedTexture, R0
```

- `NAME` est une macro de type objet contenant le numéro, donc elle fonctionne
  partout où un nombre le fait (`mov R0, TEX_SHIP`, `%if`, `%eval`). Textures
  et sons sont numérotés séparément, dans l'ordre de première apparition de
  leurs fichiers ; nommer à nouveau le même fichier donne le même numéro.
  C'est la numérotation que `packrom` donne aux entrées de la définition, donc
  le programme et la cartouche s'accordent.
- Le fichier est cherché à côté du fichier source qui contient la directive,
  puis dans les répertoires `-I` (comme le fait `datafile`) ; le nom tel
  qu'écrit va dans la définition. Un fichier introuvable est un avertissement
  (le script utilise alors le nom tel qu'écrit).
- Les directives dans un bloc `%if` ignoré, ou dans une macro qui n'est jamais
  développée, ne comptent pour rien ; à l'intérieur d'un fichier `%include`-é
  elles fonctionnent, de sorte qu'un `resources.inc` partagé est un bon endroit
  pour elles.
- Avec `-C` (objets), les directives sont acceptées mais pas stockées : un
  avertissement le dit. Donnez-les dans la source de l'exécution finale de
  `v32as`.

**`--rom-xml FILE`** écrit la définition après un assemblage réussi :

```
<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<rom-definition version="1.0">
    <rom type="cartridge" title="Space Rocks" version="1.2" />
    <binary path="game.vbin" />
    <textures>
        <texture path="font.vtex" />
        <texture path="ship.vtex" />
        <texture path="gfx/old.vtex" />
    </textures>
    <sounds>
        <sound path="boom.vsnd" />
    </sounds>
</rom-definition>
```

Le type est `bios` avec `-b`. Les chemins sont relatifs au fichier XML. Une
texture `.png` ou un son `.wav` est nommé d'après le fichier `.vtex` /
`.vsnd` que `png2vircon` / `wav2vircon` en fabriquent, placé à côté du XML
(deux de même nom de base reçoivent `_2`, `_3`, ...) ; tout autre fichier est
nommé tel quel, par son chemin depuis le XML. Sans `%title`, le titre est le
nom de la sortie, sans `%version` c'est `1.0`. Le texte est échappé (`&amp;`
et ainsi de suite).

**`--build-scripts`** écrit `Make.sh` et `Make.bat`, qui reconstruisent la
cartouche à partir des sources, à commencer par l'optimiseur :

1. **Optimiser** : `v32as -E -P` écrit la source préprocessée (`FILEPre.asm`),
   puis `v32opt -O3 FILEPre.asm -o FILEOpt.asm` ; seulement quand `v32opt` est
   dans le `PATH` (ou que `V32OPT` le nomme) et que `NO_OPT` n'est pas défini.
   `V32OPT_FLAGS` remplace `-O3`. L'optimiseur reçoit le texte préprocessé
   parce qu'il ne connaît pas `%macro`, `%if` ni `%rep` : il lit chaque
   branche comme du code qui s'exécute à la suite, et il a supprimé un `mov`
   que la branche `%else` « écrasait ».
2. **Assembler** : `v32as` à nouveau, avec les options de l'exécution qui a
   écrit le script (`-b`/`-c`, `-D`, `-U`, `-I`, `-w`, `-W...`, `-g`, `--map`)
   plus `--rom-xml` / `--rom-xml-opt`, de sorte que les définitions suivent les
   lignes `%title` et `%texture` à chaque construction ; puis la même chose
   pour `FILEOpt.asm`.
3. **Convertir** : `png2vircon IN.png -o OUT.vtex` et `wav2vircon IN.wav -o
   OUT.vsnd` pour chaque ressource qui en a besoin.
4. **Empaqueter** : `packrom NAME.xml -o bin/NAME.v32`, et `packrom
   NAMEOpt.xml -o bin/NAMEOpt.v32`.

La cartouche optimisée est un bonus : si `v32opt` échoue, ou si sa sortie ne
s'assemble pas, le script le dit et construit quand même `bin/NAME.v32`. Avec
plus d'un fichier source, il n'y a pas de construction optimisée. `v32opt` est
conçu pour la sortie d'un compilateur (étiquettes `__function_NAME:`) : sur du
code écrit à la main, il fait surtout de l'optimisation par judas (peephole),
et il qualifie `-O3` d'agressif ; comparez donc les deux cartouches avant de
faire confiance à celle en `Opt`. `V32AS`, `V32OPT`, `V32OPT_FLAGS`,
`PACKROM`, `PNG2VIRCON` et `WAV2VIRCON` désignent les outils qui ne sont pas
dans le `PATH`. Les chemins dans les scripts sont relatifs au répertoire du
script, et `Make.sh` s'y place lui-même, de sorte qu'on peut le lancer de
n'importe où. Un script se termine par `BUILD SUCCESSFUL` (code de retour 0)
ou `BUILD FAILED` (code de retour 1).

Un script en cours de construction n'est jamais réécrit, et un script que vous
avez écrit n'est jamais remplacé :

- Un script porte la ligne `Generated by v32as` comme commentaire à elle seule
  (`# ` ou `REM `). Un `Make.sh` ou `Make.bat` sans elle, y compris un écrit
  par `v32cc`, est laissé tel quel avec un avertissement
  (`--no-build-scripts` le fait taire). Supprimez la ligne d'un script généré
  pour conserver vos modifications.
- Les scripts appellent `v32as` avec `--no-build-scripts` et définissent
  `V32AS_BUILD_SCRIPT=1`, que `v32as` traite de la même façon, de sorte qu'un
  `v32as` lancé depuis une construction (par un wrapper, ou un Makefile
  appelant le script) ne peut pas réécrire le script en cours d'exécution.
- Un script dont le texte ne changerait pas n'est pas touché du tout. Celui qui
  doit changer est écrit dans un fichier temporaire puis renommé par-dessus
  l'ancien, de sorte qu'un shell qui exécute l'ancien `Make.sh` continue de
  lire l'ancien texte. (`cmd.exe` lit un `.bat` par position, donc pour
  `Make.bat` ce sont les deux premiers points qui le protègent.)

Les tests sont dans `tests/rom_tests.sh` (`make test`), avec des doublures pour
`packrom`, `png2vircon`, `wav2vircon` et `v32opt` ; `Make.bat` a été vérifié
par lecture, pas en l'exécutant sous Windows.

## Registres internes (extension)

`IP`, `IR`, `IV` ne sont pas adressables par le processeur, mais `MOV` les
accepte et se développe en exactement une vraie instruction, sans registre
temporaire caché (A = adresse de l'expansion) :

| source               | se développe en         | signification                             |
|----------------------|-------------------------|-------------------------------------------|
| `MOV Rn, IP`         | `MOV Rn, A+2`           | adresse de l'instruction suivante         |
| `MOV Rn, IR`         | `MOV Rn, <this word>`   | encodage de l'instruction en cours d'exécution |
| `MOV IP, Rn / addr`  | `JMP Rn / addr`         | saut                                      |
| `MOV IR, Rn`         | `MOV [A+2], Rn`         | écrase le mot d'instruction suivant       |
| `MOV IV, Rn`         | `MOV [A+3], Rn`         | écrase l'immédiat de l'instruction suivante |

`MOV Rn, IV` est rejeté (auto-référentiel) ; IR/IV ne peuvent pas être écrits
à partir d'un immédiat. Les écritures IR/IV modifient du code à l'exécution,
donc ce code doit s'exécuter depuis la RAM (l'assembleur avertit en mode
--bios/--cart). `MOV Rn, IP` en mode --none est relatif à l'adresse 0
(avertit).

## Vérification

La sortie est identique octet pour octet à celle de l'assembleur Vircon32
officiel pour : chaque opcode sous toutes ses formes d'opérandes légales, tous
les ports d'E/S et valeurs de ports (BIOS et CART), et toutes les directives
de données. Les `tests/golden/*.hex` ont été produits par l'assembleur
officiel. `make test` exécute les tests golden, différentiels et de messages
d'erreur.

`make realcheck` vérifie cela contre les outils officiels eux-mêmes. Il
nécessite une copie du dépôt officiel
[ComputerSoftware](https://github.com/vircon32/ComputerSoftware) à côté de
celui-ci (`../ComputerSoftware` ou `../ComputerSoftware.git`) ou nommée avec
`STOCK=DIR`, construit son `assemble` et son `compile` avec `c++` dans
`build/realcheck/stock` (SDL non requis), puis assemble avec l'outil officiel,
`v32as --standard` et `v32as` simple : les programmes d'exemple de
l'assembleur officiel (en cartouche et en BIOS), les programmes de test du
compilateur C tels que livrés et tels que le compilateur officiel les compile
maintenant, le test du packer de ROM, et les propres sources de test de ce
projet. Un programme que l'outil officiel assemble doit donner le même VBIN,
octet pour octet, dans les deux modes, et les mêmes fichiers `-g` dans les
trois modes ; un programme qu'il refuse doit être refusé par `--standard` (les
quelques exceptions sont listées comme tolérances). `make realcheck COMBO=1`
(`JOBS=N` travailleurs) ajoute chaque opcode avec zéro, un et deux opérandes
pris dans un ensemble de 28 formes d'opérandes, soit environ 52 000 programmes
d'une ligne. Les différences délibérées sont dans `tests/realcheck.known` ; le
détail d'une exécution est dans `build/realcheck/report.txt`. Le workflow
`Tests` l'exécute contre les sources officielles actuelles à chaque push, et
avec le balayage complet chaque semaine.

## Licence

Licence BSD à 3 clauses, Copyright (c) 2026 Matthew Haas. Voir le fichier
`LICENSE`.

## Historique et compatibilité

`v32as` a commencé comme un portage en C du `v32as` en bash (même travail :
assembler le langage d'assemblage Vircon32 en un VBIN, avec un mode
listage/verbeux/pas à pas et un REPL interactif), avec flex pour l'analyseur
lexical, bison pour l'analyseur syntaxique et des tables de hachage à la place
des tableaux bash et de `bc`. Différences avec la version bash :

- L'en-tête VBIN porte le vrai nombre de mots (bash codait 155 en dur).
- Les ports numériques dans `IN`/`OUT` fonctionnent (bogue de bash) ; les
  ports inconnus/tronqués avertissent.
- Les littéraux entiers donnés à des opérations de classe flottante sont
  convertis en flottants ; ATAN2/POW n'acceptent que des registres (comme
  l'officiel).
- `[R+0]` est encodé `[R]` (comme l'officiel).
- Les directives de données sont `integer`, `float`, `pointer`, `string`,
  `zstring`, `datafile` (voir [Directives de données](#directives-de-données)).
- Ajouts : `true/false`, littéraux caractère, nombres `0b`, octal, `#dec`,
  suffixe `h` ; une étiquette et une instruction peuvent partager une ligne ;
  mnémoniques/registres insensibles à la casse.

Ce qui a changé à quelle étape de développement est dans `CHANGES.md`.
