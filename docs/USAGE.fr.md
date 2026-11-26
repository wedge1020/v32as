# Utiliser v32as : d'un fichier .asm à une cartouche

Cette page est une visite guidée, orientée tâches, de `v32as`, l'assembleur
Vircon32 de remplacement direct, et de son éditeur de liens `v32asld`. La
référence exhaustive est le [README](../README.fr.md) (chaque option, chaque
format de fichier de sortie) ; les pages de manuel (`v32as(1)`, `v32asld(1)`,
`v32as-language(7)`, `v32as-inc(3)`, `v32as-control(3)`) couvrent le même
terrain sous forme de `man`. Vous trouverez ici les commandes que vous tapez
réellement, dans l'ordre où vous en aurez probablement besoin.

Tous les exemples ont été exécutés avec la construction actuelle. Les
commandes sont montrées sans invite ; la sortie est montrée telle qu'elle est
apparue (abrégée là où c'est indiqué).

---

## 1. Ce qu'est v32as

`v32as` transforme l'assembleur Vircon32 (`.asm`) en un **VBIN**, le format
du programme d'une cartouche. Pour la même source, il écrit les mêmes octets
que l'outil officiel `assemble`. En plus de cela, il ajoute :

* un préprocesseur avec macros, conditionnelles, boucles et `%include <...>` ;
* quelques formes de nombres supplémentaires, des étiquettes `.local`, et
  `MOV` avec les registres internes `IP`, `IR`, `IV` ;
* des objets relogeables (`-C`) et un éditeur de liens, `v32asld` ;
* des listages (`-T`), un mode interactif (`-i`), et des sorties de débogage
  pour le simulateur (`-g`, `--map`) ;
* les directives de définition de cartouche (`%title`, `%texture`, `%sound`)
  ainsi que le XML et les scripts de construction qui les accompagnent.

`--standard` désactive toutes les extensions ([section 11](#11-compatibilité-stricte----standard)).

Sa place dans la chaîne d'outils :

```
 game.asm ──v32as──> game.vbin ──┐
 gfx/*.png ──png2vircon──> *.vtex ├──packrom (game.xml)──> game.v32 ──> emulator
 snd/*.wav ──wav2vircon──> *.vsnd ┘
```

`v32as` remplace l'étape `assemble`. Si votre assembleur provient d'un
compilateur (`v32cc`, `v32lua`), faites pointer sa construction vers `v32as`
au lieu de `assemble` ; les options de débogage de la section 8 lisent ce que
ces compilateurs écrivent.

---

## 2. Installation

### À partir d'un zip de publication

Décompressez-le n'importe où. La disposition est :

```
v32as  v32asld                  (v32as.exe, v32asld.exe sous Windows)
include/v32as/v32as.inc         les bibliothèques pour %include
include/v32as/control.inc
docs/v32as/README.md            avec CHANGES.md, LICENSE, docs/*.md
docs/v32as/man/man1 man3 man7   les pages de manuel
```

`v32as` trouve `include/v32as/` à côté de lui, donc `%include <v32as.inc>`
fonctionne sans aucun `-I` tant que les deux restent ensemble. Mettez le
dossier dans votre `PATH` et vérifiez :

```
v32as --version
```

Pour lire les pages de manuel depuis l'arborescence décompressée, faites
pointer `MANPATH` vers `docs/v32as/man`.

### À partir des sources

Il vous faut un compilateur C, `flex` et `bison` (GNU `readline` est utilisé
pour l'édition de ligne du mode interactif quand il est trouvé).

```
make
make test        # facultatif
make install     # ~/bin, ~/share/v32as/lib, ~/man et ~/share/doc/v32as
```

`make install PREFIX=/usr/local` (ou `make sysinstall`) utilise
`PREFIX/bin`, `PREFIX/share/v32as/lib`, `PREFIX/share/man` et
`PREFIX/share/doc/v32as`. `DESTDIR`, `BINDIR`, `MANDIR`, `LIBDIR` et `DOCDIR`
sont respectés, et `DEST_LANG=en|es|fr` (`en` par défaut) choisit la langue
des documents installés et des pages de manuel `v32as(1)` et `v32asld(1)`. Le répertoire d'installation
des bibliothèques est compilé dans `v32as`, donc `%include <v32as.inc>`
fonctionne où que vous l'installiez. `make uninstall` le retire à nouveau.

---

## 3. Assembler un programme

```
; hello.asm: clear the screen to blue, forever
_main:
    mov  R0, 0xFF0000FF         ; opaque blue (ABGR)
    out  GPU_ClearColor, R0
_frame:
    out  GPU_Command, GPUCommand_ClearScreen
    wait
    jmp  _frame
```

```
v32as hello.asm
```

Ceci écrit `hello.vbin` pour une cartouche (adresse de base `0x20000000`),
exactement ce que fait `assemble hello.asm`. À retenir à propos du langage :
une étiquette commence par un tiret bas, les mnémoniques et les registres sont
insensibles à la casse, et un commentaire commence par `;`.

Variantes utiles :

| Commande | Effet |
|---|---|
| `v32as -o out/game.vbin game.asm` | Nomme la sortie. |
| `v32as -b bios.asm` | Assemble pour le BIOS (base `0x10000000`). |
| `v32as -c game.asm` | Cartouche ; c'est déjà la valeur par défaut. |
| `v32as -O 0x3000 -o ram.bin code.asm` | N'importe quelle adresse de base (code qui s'exécute depuis la RAM). |
| `v32as a.asm b.asm -o prog.vbin` | Plusieurs fichiers forment un seul programme, dans l'ordre donné. |
| `echo ' hlt' \| v32as - > out.vbin` | Lit l'entrée standard, écrit la sortie standard. |
| `v32as -w game.asm` | Aucun avertissement. |
| `v32as -v game.asm` | Trace l'encodage de chaque instruction (sortie d'erreur). |

Les options peuvent se placer avant ou après les noms de fichiers. `-b`, `-c`
et `-O` s'excluent mutuellement. Plusieurs fichiers d'entrée partagent un même
état de préprocesseur (macros, définitions) et une même table de symboles.

**Lecture de l'entrée standard.** Sans fichier d'entrée, `v32as` ne lit
l'entrée standard que si elle est redirigée ou reçoit un tube ; il écrit alors
le VBIN sur la sortie standard (il refuse d'écrire des données binaires sur un
terminal). Taper `v32as` seul dans un terminal n'attend pas d'entrée : il
affiche le résumé d'utilisation sur la sortie d'erreur et se termine avec le
code 2. Pour le mode interactif, utilisez `-i` ([section 5](#5-le-mode-interactif--i)).

**Avertissements.** Un nombre avec un zéro initial est octal dans `v32as` mais
décimal dans l'assembleur officiel, donc chacun de ces littéraux avertit :

```
oct.asm:1:11: warning: '0777' is octal here, but the official assembler reads it as decimal (use --standard for that, or drop the leading zero)
```

Retirez le zéro, ou voyez la [section 11](#11-compatibilité-stricte----standard).

---

## 4. Listages et sortie du préprocesseur

`-T` affiche l'encodage de chaque instruction au lieu d'écrire un VBIN :

```
v32as -T -c hello.asm
```

```
OFFSET:      0x20000000
instruction: 0x4E000000
immediate:   0xFF0000FF
-----------------------
OFFSET:      0x20000002
instruction: 0x60000202
-----------------------
```

Ajoutez `-b`, `-c` ou `-O ADDR` pour choisir les adresses affichées ; sans
aucune d'elles (`-T`, ou son synonyme `-n`), le listage n'a pas de lignes
`OFFSET`, seulement les mots d'instruction et d'immédiat.

`-E` affiche la source après préprocessage, avec des marqueurs de ligne tels
que `# 1 "hello.asm"` (`-P` les omet). C'est le moyen de voir ce que fait une
macro :

```
v32as -E -P game.asm
```

---

## 5. Le mode interactif (`-i`)

`-i` (`--interactive`) démarre une boucle lecture-évaluation-affichage :
chaque ligne que vous tapez est assemblée dès que vous appuyez sur Entrée, et
son encodage est affiché. Il n'accepte **aucun fichier d'entrée**.

```
v32as -i
```

```
v32as 20261121-dev interactive mode: .help for commands, Ctrl-D to quit
v32as> mov R0, 5
instruction: 0x4E000000
immediate:   0x00000005
-----------------------
v32as> _x: iadd R0, 1
label:       _x = 0x00000002
instruction: 0x9A000000
immediate:   0x00000001
-----------------------
v32as> jmp _x
instruction: 0x0A000000
immediate:   0x00000002
-----------------------
v32as> .labels
0x00000002  _x
v32as> .dump
0x00000000  0x4E000000
0x00000001  0x00000005
0x00000002  0x9A000000
0x00000003  0x00000001
0x00000004  0x0A000000
0x00000005  0x00000002
v32as> .quit
```

Les commandes sont :

| Commande | Effet |
|---|---|
| `.help` | Affiche les commandes. |
| `.labels` | Liste les étiquettes définies jusqu'ici. |
| `.dump` | Affiche chaque mot assemblé jusqu'ici. |
| `.reset` | Oublie tout ce qui a été assemblé jusqu'ici. |
| `.quit` | Quitte (Ctrl-D aussi). |

Les étiquettes, les directives de données et les directives de préprocesseur
fonctionnent aussi ; un bloc `%macro` ou `%if` se poursuit sur d'autres lignes
(l'invite devient `...>`) jusqu'à sa fermeture. Les adresses commencent à 0.
Choisissez une autre base avec `-b`, `-c`, `-O ADDR` (ou `-T`/`-n` pour le
listage sans base) ; l'invite affiche alors l'adresse courante :

```
v32as -i -c
```

```
[0x20000000] > mov R0, 1
```

Les options qui ont un sens pour une session se combinent avec `-i` :
`-b -c -O -T -n`, `-D -U -I`, `-w`, `-v` et `--standard`. Celles qui écrivent
des fichiers ou exigent un programme complet ne le font pas, et sont une
erreur (code 2) : `-o -E -B -C -g -r --symbols --map -MD --rom-xml
--build-scripts --v32x -s`, ainsi que tout fichier d'entrée.

---

## 6. Le préprocesseur et les bibliothèques de macros

Toute ligne commençant par `%` est une directive de préprocesseur. Outre les
`%define`, `%ifdef`, `%include`, `%error` et `%warning` officiels, `v32as`
possède des macros de type fonction, `%if`/`%elif`, `%assign`, `%eval(...)`,
`%macro` (macros d'instructions), des boucles `%rep` et plus encore. Le
résumé est dans `v32as-language(7)` et le texte complet dans le README
([Préprocesseur](../README.fr.md#préprocesseur)).

Deux bibliothèques toutes prêtes sont livrées avec `v32as` (voir
`v32as-inc(3)` et `v32as-control(3)`) :

* `v32as.inc` : `INC DEC ZERO NEG CLAMP SWAP PUSHALL POPALL WAIT_FRAMES` ;
* `control.inc` : `IF` / `ELSE` / `ENDIF` et `WHILE` / `ENDWHILE` structurés.

`%include <name>` cherche d'abord dans les répertoires `-I`, puis dans le
répertoire de bibliothèques trouvé à côté de `v32as` (ou celui qu'a utilisé
`make install`), de sorte qu'aucun `-I` n'est nécessaire une fois l'outil
installé. (`--standard` n'a rien de tout cela.) Un exemple concret :

```
; blink.asm: flash the screen every 30 frames
%include <v32as.inc>
%include <control.inc>

%define FLASH_EVERY 30

_main:
    ZERO    R1, R2              ; R1 = frame counter
_frame:
    INC     R1
    mov     R3, R1
    imod    R3, FLASH_EVERY     ; R3 = R1 mod 30
    IF      R3                  ; the condition is a register: non-zero?
        mov R0, 0xFF000000      ; black
    ELSE
        mov R0, 0xFFFFFFFF      ; white
    ENDIF
    out     GPU_ClearColor, R0
    out     GPU_Command, GPUCommand_ClearScreen
    WAIT_FRAMES
    jmp     _frame
```

```
v32as blink.asm
v32as -E -P blink.asm
```

La seconde commande montre ce que l'assembleur voit réellement (les
commentaires et les lignes vides de la bibliothèque sont omis ici) :

```
_main:
      mov   R1, 0
      mov   R2, 0
_frame:
    iadd  R1, 1
    mov     R3, R1
    imod    R3, 30     ; R3 = R1 mod 30
    jf    R3, __C1_else
        mov R0, 0xFF000000      ; black
    jmp   __C1_end
__C1_else:
        mov R0, 0xFFFFFFFF      ; white
__C1_end:
    out     GPU_ClearColor, R0
    out     GPU_Command, GPUCommand_ClearScreen
    wait
    jmp     _frame
```

À savoir :

* Les noms de macros sont sensibles à la casse et écrits en capitales ; les
  mnémoniques ne le sont pas.
* Une macro vérifie ses opérandes : `INC 5` est signalé à votre ligne, suivi
  d'une note qui nomme la macro :

  ```
  inc5.asm:2: error: INC: the first operand must be a register
  inc5.asm:2: note: in expansion of macro 'INC' (defined at .../include/v32as/v32as.inc:21)
  ```
* Définissez les vôtres avec `%macro` ... `%endmacro` ; `%%name` est une
  étiquette privée à chaque expansion :

  ```
  %macro COUNTDOWN reg, count
      mov   reg, count
  %%top:
      isub  reg, 1
      jt    reg, %%top
  %endmacro
      COUNTDOWN R1, 10
  ```
* Définitions en ligne de commande : `-DDEBUG`, `-DLEVEL=3`, `-UDEBUG`. Un
  `%define` du même nom dans la source avertit et l'emporte, sauf si vous
  écrivez `-D!LEVEL=3`.
* `-MD -MP` écrit une règle de Makefile listant les fichiers `%include` et
  `datafile`, pour les systèmes de construction (`def.d` pour `def.asm`).

---

## 7. Code qui se modifie : `IP`, `IR`, `IV`

`MOV` accepte les registres internes et se développe en exactement une vraie
instruction, sans registre temporaire :

```
    mov R1, IP        ; address of the next instruction
    mov IP, R2        ; a jump
```

`MOV IR, Rn` et `MOV IV, Rn` écrasent à l'exécution le mot d'instruction
suivant ou son immédiat, donc ce code doit s'exécuter depuis la RAM (`v32as`
avertit sinon). `MOV Rn, IV` est rejeté. Tout cela est une extension,
désactivée sous `--standard`. Le tableau est dans le README
([Registres internes](../README.fr.md#registres-internes-extension)).

---

## 8. Sorties de débogage pour le simulateur

### `-g`

```
v32as -g blink.asm
```

écrit, à côté de `blink.vbin` :

* `blink.vbin.debug`, CSV `address,file,line[,label]`, une ligne par
  instruction (la première étiquette à cette adresse est la quatrième
  colonne) ;
* `blink.vbin.sym`, les symboles avec leurs adresses finales.

```
0x20000000,blink.asm,8,_main
0x20000002,blink.asm,8
0x20000004,blink.asm,10,_frame
...
```

Un `-g` simple est identique octet pour octet au `-g program` de l'assembleur
officiel (adresses de mots absolues). `-g vbin` et `-g v32` donnent à la place
de vrais décalages en octets dans un fichier `.vbin` ou `.v32`. Les erreurs et
le fichier `.debug` pointent vers la ligne que **vous** avez écrite, y compris
à travers `%include` et les macros (le code qu'une macro produit est attribué
à la ligne qui l'a invoquée).

Les compilateurs qui écrivent des commentaires marqueurs `;@src FILE:LINE`
(`v32lua -g`) obtiennent leurs propres lignes dans `.debug`, et les lignes
`;@var` produisent aussi `.vars` ; les formats sont dans le README
([Fichiers de sortie](../README.fr.md#fichiers-de-sortie)).

### `--map` (programmes C issus de `v32cc`)

`v32cc -g` écrit une carte CSV des lignes d'assemblage vers les lignes C
(`asmfile,asmline,cfile,cline[,function]`). Donnez-la à `v32as` et le fichier
`.debug` nomme alors le fichier et la ligne C :

```
v32as -g --map main.map main.asm
```

```
0x20000000,main.c,10,main
0x20000002,main.c,11,main
0x20000004,main.c,12,main
```

Les instructions dont la ligne d'assemblage ne figure pas dans la carte sont
omises. Avec `--map` et sans `-g`, chaque erreur ou avertissement est suivi
d'une note nommant la ligne C dont il provient.

### Symboles et relocalisations

```
v32as -g -r game.rel --symbols game.sym game.asm
```

`--symbols FILE` écrit `0xADDRESS,name,kind` par étiquette (`F` code, `D`
données ; ajoutez `--symbols-all` pour les étiquettes locales au fichier).
`-r FILE` liste les mots qui contiennent une adresse, `index,0xVALUE`, pour
qu'un chargeur puisse reloger un module.

---

## 9. Construire une ROM : `%title`, `%texture`, `%sound`

Une cartouche est le programme plus les textures et les sons qu'il utilise,
empaquetés par `packrom` à partir d'une définition XML. `v32as` peut tirer les
deux de la source :

```
%title   "Space Rocks"
%version 1.2
%texture TEX_FONT "gfx/font.png"
%texture TEX_SHIP "gfx/ship.png"
%sound   SND_BOOM "snd/boom.wav"

_main:
    mov R0, TEX_SHIP
    out GPU_SelectedTexture, R0
    mov R1, SND_BOOM
    out SPU_SelectedSound, R1
    hlt
```

`TEX_SHIP` et `SND_BOOM` deviennent les numéros d'entrée (textures et sons
sont numérotés séparément, à partir de 0, dans l'ordre de première
apparition), de sorte que le programme et la cartouche s'accordent toujours.
Les fichiers sont cherchés à côté de la source, puis dans les répertoires
`-I`.

### La définition

```
v32as game.asm --rom-xml game.xml
```

écrit `game.vbin` et `game.xml` :

```
<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<rom-definition version="1.0">
    <rom type="cartridge" title="Space Rocks" version="1.2" />
    <binary path="game.vbin" />
    <textures>
        <texture path="font.vtex" />
        <texture path="ship.vtex" />
    </textures>
    <sounds>
        <sound path="boom.vsnd" />
    </sounds>
</rom-definition>
```

Les chemins sont relatifs au XML. Un `.png` ou un `.wav` est nommé d'après le
`.vtex` ou le `.vsnd` que `png2vircon` / `wav2vircon` en fabriquent, et que
vous placez ensuite à côté du XML.

### Scripts de construction

```
v32as --build-scripts game.asm
```

écrit `Make.sh` et `Make.bat` (ainsi que `game.xml`, `gameOpt.xml`) à côté de
la source, et assemble comme d'habitude. Lancez le script pour tout
reconstruire :

```
./Make.sh
```

Il exécute, dans l'ordre : l'optimiseur (`v32opt`, seulement s'il est
installé ; sinon une note dit qu'il est ignoré), `v32as`, `png2vircon` et
`wav2vircon` pour chaque ressource, et `packrom game.xml -o bin/game.v32`. Il
se termine par `BUILD SUCCESSFUL` ou `BUILD FAILED`. Les chemins à l'intérieur
sont relatifs au script, donc on peut le lancer de n'importe où.
`--build-scripts=sh` n'écrit que le script shell. Les outils qui ne sont pas
dans le `PATH` peuvent être nommés dans `V32AS`, `V32OPT`, `PACKROM`,
`PNG2VIRCON`, `WAV2VIRCON` ; `NO_OPT=1` saute l'optimiseur.

Un script n'est remplacé que s'il porte la ligne `Generated by v32as` ; un
`Make.sh` écrit à la main (ou venant de `v32cc`) est laissé tel quel avec un
avertissement.

Ces options nécessitent un VBIN écrit dans un fichier : pas avec `-C`, `-T`,
`-n`, `-E`, `-O`, `--v32x` ni l'entrée standard, et ce sont des erreurs avec
`--standard`. Détails : README ([Définition de la cartouche et scripts de
construction](../README.fr.md#définition-de-la-cartouche-et-scripts-de-construction)).

---

## 10. Assemblage et édition de liens séparés : `-C` et `v32asld`

Assemblez chaque fichier en un objet relogeable, puis liez :

```
; a.asm
global _start
extern _helper
_start:
    mov R0, 3
    call _helper
    hlt
```

```
; b.asm
global _helper
_helper:
    iadd R0, 1
    ret
```

```
v32as -C a.asm
v32as -C b.asm
v32asld -g -r prog.rel --symbols prog.sym -o prog.vbin a.v32ob b.v32ob
```

`global` et `extern` (noms simples, sans `%`) disent quelles étiquettes sont
partagées ; toute autre étiquette est locale à son fichier. Les objets sont
placés dans l'ordre de la ligne de commande, donc le premier mot du premier
objet est le point d'entrée (`--entry _name` place un `jmp` devant quand vous
en voulez un autre). Le résultat est identique à l'assemblage des sources
jointes en un seul fichier, et `-g` écrit le même `.debug`. La base est
l'affaire de l'éditeur de liens : `-b`, `-c` et `-O ADDR` vont à `v32asld`,
pas à `v32as -C`.

Un nom non résolu est signalé une fois par symbole :

```
v32asld: error: undefined reference to `_helper' (first used in a.v32ob)
```

Et aussi : `v32asld -A libb.a b.v32ob` fabrique une archive (nommez-la ensuite
parmi les entrées, ou utilisez `-L DIR -l b`) ; `v32asld -R -o ab.v32ob
a.v32ob b.v32ob` joint des objets en un seul objet. Les deux sont décrits
dans `v32asld(1)` ; le format d'objet est dans `docs/OBJECT.md`.

---

## 11. Compatibilité stricte : `--standard`

```
v32as --standard game.asm
```

désactive toutes les extensions, de sorte que vous puissiez vérifier qu'un
programme s'assemble avec l'`assemble` officiel et obtenir son comportement
exact : pas de nombres binaires ni à suffixe `h`, pas de `IP`/`IR`/`IV`,
seulement les directives de préprocesseur d'origine (pas de `%if`, `%macro`,
macros de type fonction, inclusions `<file>` ni `-I`), `label+N` est une
erreur, un seul fichier d'entrée, `-v` affiche les messages d'étape officiels,
et un zéro initial signifie décimal :

```
mov R0, 0777
```

s'assemble en `0x309` (777) avec `--standard` et en `0x1FF` (511, octal) sans
lui. Le tableau complet est dans le README
([`--standard`](../README.fr.md#--standard--exactement-lassembleur-officiel)).

---

## 12. Code de retour

| Code | Signification |
|---|---|
| 0 | Succès (les avertissements ne le changent pas). |
| 1 | Une erreur d'assemblage ou de préprocesseur, ou un fichier d'entrée illisible. |
| 2 | Une erreur d'utilisation (options erronées ou en conflit, aucun fichier d'entrée sur un terminal), ou un fichier de sortie qui ne peut pas être écrit. |

`v32asld` utilise 1 pour une erreur d'édition de liens et 2 pour une erreur
d'utilisation, un objet ou une archive invalide, ou une bibliothèque `-l`
introuvable.

---

## 13. Dépannage

Les diagnostics ont la forme `file:line[:column]: error: message`. Les erreurs
à l'intérieur d'une macro sont signalées à l'invocation, suivies d'une note
qui nomme la macro et l'endroit où elle est définie.

| Message | Cause et correction |
|---|---|
| `label 'main' must start with an underscore` | Toute étiquette commence par `_` : écrivez `_main:`. |
| `unknown identifier 'R16'` | Ni un registre, ni un mnémonique, ni un nom de port. Les registres sont `R0`..`R15` (`CR SR DR BP SP` sont `R11`..`R15`). Avec `--standard`, `IP`, `IR`, `IV` sont aussi inconnus. |
| `undefined label '_nowhere'` | Un saut ou un `mov` utilise une étiquette qui n'est jamais définie. Dans un programme réparti en objets, marquez-la `extern` et liez. |
| `duplicate label '_main'` | Deux sources la définissent. Plusieurs fichiers d'entrée forment **un seul** programme, donc chaque étiquette ne peut être définie qu'une fois (assemblez-les séparément avec `-C` si les deux ont besoin de `_main`). |
| `syntax error, unexpected "','", expecting end of line` | Trop d'opérandes pour l'instruction. |
| `cannot open include file "nope.inc"` | Introuvable à côté du fichier qui inclut ou dans les répertoires `-I`. Pour `%include <v32as.inc>`, vérifiez que `include/v32as/` se trouve à côté de `v32as`, ou ajoutez `-I DIR`. |
| `unterminated %if block` | Un `%if` / `%ifdef` sans `%endif`. (Un simple avertissement sous `--standard`.) |
| `'label +/- integer' is not part of the official assembler's language` | Une extension utilisée sous `--standard`. |
| `'0777' is octal here, but the official assembler reads it as decimal` | Retirez le zéro initial, ou utilisez `--standard`. |
| `--origin cannot be combined with --bios or --cart` | Ne donnez qu'un seul de `-b`, `-c`, `-O`. |
| `--interactive cannot be combined with ...` | Voir la [section 5](#5-le-mode-interactif--i). |
| `-g needs binary output to a file` | `-g` et `-r` nécessitent un VBIN écrit dans un fichier (`-o FILE` ou un seul fichier d'entrée), pas un listage, `-E`, `-C` ni la sortie standard. |
| `--rom-xml and --build-scripts need a BIOS or cartridge VBIN written to a file` | Retirez `-C`, `-T`, `-n`, `-E`, `-O`, `--v32x`, ou donnez `-o FILE`. |

Si une macro fait quelque chose d'inattendu, regardez `v32as -E -P file.asm`.
Si `v32as` semble se bloquer, c'est qu'il lit l'entrée standard : redirigez ou
envoyez la source par un tube, ou donnez un nom de fichier. L'exécuter sans
argument dans un terminal affiche à la place le texte d'utilisation.

---

## Et ensuite

* [README](../README.fr.md) : chaque option, chaque format de fichier de
  sortie, et tout le préprocesseur.
* `man v32as`, `man v32asld`, `man v32as-language`, `man v32as-inc`,
  `man v32as-control`.
* `docs/PREPROCESSOR.md` (notes de conception), `docs/OBJECT.md` (formats
  d'objet et d'archive), `CHANGES.md` (ces documents sont en anglais).
