[English](USAGE.md) | **Español**

# Usar v32as: de un archivo .asm a un cartucho

Esta página es un recorrido orientado a tareas por `v32as`, el ensamblador
de Vircon32 que sustituye directamente al oficial, y por su enlazador
`v32asld`. La referencia exhaustiva es el [README](../README.es.md) (todas
las opciones, todos los formatos de archivos de salida); las páginas del
manual (`v32as(1)`, `v32asld(1)`, `v32as-language(7)`, `v32as-inc(3)`,
`v32as-control(3)`) cubren lo mismo en formato `man`. Aquí encontrarás los
comandos que de verdad escribes, en el orden en que probablemente los
necesitarás. (Los demás documentos a los que se hace referencia, como
`docs/OBJECT.md`, `docs/PREPROCESSOR.md` y `CHANGES.md`, están en inglés.)

Todos los ejemplos se ejecutaron con la compilación actual. Los comandos se
muestran sin indicador de línea; la salida se muestra tal como apareció
(recortada donde se indica).

---

## 1. Qué es v32as

`v32as` convierte el ensamblador de Vircon32 (`.asm`) en un **VBIN**, el
formato del programa de un cartucho. Con el mismo código fuente escribe los
mismos bytes que la herramienta oficial `assemble`. Además añade:

* un preprocesador con macros, condicionales, bucles y `%include <...>`;
* algunas formas numéricas extra, etiquetas `.local`, y `MOV` con los
  registros internos `IP`, `IR`, `IV`;
* objetos reubicables (`-C`) y un enlazador, `v32asld`;
* listados (`-T`), un modo interactivo (`-i`) y salidas de depuración para el
  simulador (`-g`, `--map`);
* las directivas de definición del cartucho (`%title`, `%texture`, `%sound`)
  y el XML y los scripts de construcción que las acompañan.

`--standard` desactiva todas las extensiones ([sección 11](#11-compatibilidad-estricta---standard)).

Dónde encaja en la cadena de herramientas:

```
 game.asm ──v32as──> game.vbin ──┐
 gfx/*.png ──png2vircon──> *.vtex ├──packrom (game.xml)──> game.v32 ──> emulator
 snd/*.wav ──wav2vircon──> *.vsnd ┘
```

`v32as` sustituye el paso de `assemble`. Si tu ensamblador sale de un
compilador (`v32cc`, `v32lua`), haz que su construcción apunte a `v32as` en
lugar de a `assemble`; las opciones de depuración de la sección 8 leen lo que
escriben esos compiladores.

---

## 2. Instalación

### Desde un zip de publicación

Descomprímelo donde quieras. La estructura es:

```
v32as  v32asld                  (v32as.exe, v32asld.exe on Windows)
include/v32as/v32as.inc         the %include libraries
include/v32as/control.inc
docs/v32as/README.md            with CHANGES.md, LICENSE, docs/*.md
docs/v32as/man/man1 man3 man7   the manual pages
```

`v32as` encuentra `include/v32as/` junto a sí mismo, de modo que
`%include <v32as.inc>` funciona sin ningún `-I` mientras los dos sigan
juntos. Pon la carpeta en tu `PATH` y comprueba:

```
v32as --version
```

Para leer las páginas del manual desde el árbol descomprimido, apunta
`MANPATH` a `docs/v32as/man`.

### Desde el código fuente

Necesitas un compilador de C, `flex` y `bison` (GNU `readline` se usa para
la edición de línea en el modo interactivo cuando se encuentra).

```
make
make test        # optional
make install     # ~/bin, ~/share/v32as/lib, ~/man and ~/share/doc/v32as
```

`make install PREFIX=/usr/local` (o `make sysinstall`) usa
`PREFIX/bin`, `PREFIX/share/v32as/lib`, `PREFIX/share/man` y
`PREFIX/share/doc/v32as`. Se respetan `DESTDIR`, `BINDIR`, `MANDIR`, `LIBDIR`
y `DOCDIR`, y `DEST_LANG=en|es|fr` (por omisión `en`) elige el idioma de los
documentos instalados y de las páginas del manual `v32as(1)` y `v32asld(1)`. El directorio de instalación de las
bibliotecas está compilado dentro de `v32as`, de modo que
`%include <v32as.inc>` funciona dondequiera que lo instales. `make
uninstall` lo desinstala de nuevo.

---

## 3. Ensamblar un programa

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

Esto escribe `hello.vbin` para un cartucho (dirección base `0x20000000`),
exactamente lo que hace `assemble hello.asm`. Cosas que conviene recordar
del lenguaje: una etiqueta empieza con un guion bajo, los mnemónicos y los
registros no distinguen mayúsculas de minúsculas, y un comentario empieza
con `;`.

Variaciones útiles:

| Comando | Efecto |
|---|---|
| `v32as -o out/game.vbin game.asm` | Da nombre a la salida. |
| `v32as -b bios.asm` | Ensambla para la BIOS (base `0x10000000`). |
| `v32as -c game.asm` | Cartucho; ya es el valor por defecto. |
| `v32as -O 0x3000 -o ram.bin code.asm` | Cualquier dirección base (código que se ejecuta desde la RAM). |
| `v32as a.asm b.asm -o prog.vbin` | Varios archivos forman un único programa, en el orden dado. |
| `echo ' hlt' \| v32as - > out.vbin` | Lee STDIN, escribe STDOUT. |
| `v32as -w game.asm` | Sin advertencias. |
| `v32as -v game.asm` | Rastrea la codificación de cada instrucción (STDERR). |

Las opciones pueden ir antes o después de los nombres de archivo. `-b`, `-c`
y `-O` se excluyen entre sí. Varios archivos de entrada comparten un único
estado del preprocesador (macros, definiciones) y una única tabla de
símbolos.

**Lectura de STDIN.** Sin archivo de entrada, `v32as` lee STDIN solo cuando
está redirigida o es una tubería; entonces escribe el VBIN en STDOUT (se
niega a escribir datos binarios en un terminal). Escribir `v32as` a secas en
un terminal no espera entrada: imprime el resumen de uso en STDERR y termina
con estado 2. Para el modo interactivo usa `-i` ([sección 5](#5-el-modo-interactivo--i)).

**Advertencias.** Un número con un cero inicial es octal en `v32as` pero
decimal en el ensamblador oficial, así que cada literal de ese tipo
advierte:

```
oct.asm:1:11: warning: '0777' is octal here, but the official assembler reads it as decimal (use --standard for that, or drop the leading zero)
```

Quita el cero, o consulta la [sección 11](#11-compatibilidad-estricta---standard).

---

## 4. Listados y la salida del preprocesador

`-T` imprime la codificación de cada instrucción en lugar de escribir un
VBIN:

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

Añade `-b`, `-c` u `-O ADDR` para elegir las direcciones mostradas; sin
ninguna de ellas (`-T`, o su sinónimo `-n`) el listado no tiene líneas
`OFFSET`, solo las palabras de instrucción y de inmediato.

`-E` imprime el código fuente tras el preprocesado, con marcadores de línea
como `# 1 "hello.asm"` (`-P` los omite). Es la manera de ver lo que hace una
macro:

```
v32as -E -P game.asm
```

---

## 5. El modo interactivo (`-i`)

`-i` (`--interactive`) inicia un bucle de lectura-evaluación-impresión:
cada línea que escribes se ensambla en cuanto pulsas Intro, y se imprime su
codificación. **No admite archivos de entrada**.

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

Los comandos son:

| Comando | Efecto |
|---|---|
| `.help` | Muestra los comandos. |
| `.labels` | Lista las etiquetas definidas hasta ahora. |
| `.dump` | Muestra todas las palabras ensambladas hasta ahora. |
| `.reset` | Olvida todo lo ensamblado hasta ahora. |
| `.quit` | Sale (también con Ctrl-D). |

También funcionan las etiquetas, las directivas de datos y las directivas
del preprocesador; un bloque `%macro` o `%if` continúa en las líneas
siguientes (el indicador cambia a `...>`) hasta que se cierra. Las
direcciones empiezan en 0. Elige otra base con `-b`, `-c`, `-O ADDR` (o
`-T`/`-n` para el listado sin base); el indicador muestra entonces la
dirección actual:

```
v32as -i -c
```

```
[0x20000000] > mov R0, 1
```

Las opciones que tienen sentido en una sesión se combinan con `-i`:
`-b -c -O -T -n`, `-D -U -I`, `-w`, `-v` y `--standard`. Las que escriben
archivos o necesitan un programa completo no, y son un error (estado 2):
`-o -E -B -C -g -r --symbols --map -MD --rom-xml --build-scripts --v32x -s`,
y cualquier archivo de entrada.

---

## 6. El preprocesador y las bibliotecas de macros

Toda línea que empieza por `%` es una directiva del preprocesador. Además de
los `%define`, `%ifdef`, `%include`, `%error` y `%warning` oficiales,
`v32as` tiene macros tipo función, `%if`/`%elif`, `%assign`, `%eval(...)`,
`%macro` (macros de sentencia), bucles `%rep` y más. El resumen está en
`v32as-language(7)` y el texto completo en el README
([Preprocesador](../README.es.md#preprocesador)).

Con `v32as` se incluyen dos bibliotecas ya hechas (consulta `v32as-inc(3)` y
`v32as-control(3)`):

* `v32as.inc`: `INC DEC ZERO NEG CLAMP SWAP PUSHALL POPALL WAIT_FRAMES`;
* `control.inc`: `IF` / `ELSE` / `ENDIF` y `WHILE` / `ENDWHILE` estructurados.

`%include <name>` busca primero en los directorios `-I` y luego en el
directorio de bibliotecas que se encuentra junto a `v32as` (o el que usó
`make install`), de modo que no hace falta `-I` una vez instalada la
herramienta. (`--standard` no tiene nada de esto.) Un ejemplo completo:

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

El segundo comando muestra lo que el ensamblador ve realmente (aquí se han
omitido los comentarios y las líneas en blanco de la biblioteca):

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

Cosas que conviene saber:

* Los nombres de macro distinguen mayúsculas de minúsculas y se escriben en
  mayúsculas; los mnemónicos no.
* Una macro comprueba sus operandos: `INC 5` se informa en tu línea, seguida
  de una nota que nombra la macro:

  ```
  inc5.asm:2: error: INC: the first operand must be a register
  inc5.asm:2: note: in expansion of macro 'INC' (defined at .../include/v32as/v32as.inc:21)
  ```
* Define las tuyas con `%macro` ... `%endmacro`; `%%name` es una etiqueta
  privada de cada expansión:

  ```
  %macro COUNTDOWN reg, count
      mov   reg, count
  %%top:
      isub  reg, 1
      jt    reg, %%top
  %endmacro
      COUNTDOWN R1, 10
  ```
* Definiciones en la línea de comandos: `-DDEBUG`, `-DLEVEL=3`, `-UDEBUG`. Un
  `%define` del mismo nombre en el código fuente advierte y gana, salvo que
  escribas `-D!LEVEL=3`.
* `-MD -MP` escribe una regla de Makefile que lista los archivos de
  `%include` y de `datafile`, para los sistemas de construcción (`def.d`
  para `def.asm`).

---

## 7. Código que se modifica a sí mismo: `IP`, `IR`, `IV`

`MOV` acepta los registros internos y se expande exactamente en una
instrucción real, sin registro temporal:

```
    mov R1, IP        ; address of the next instruction
    mov IP, R2        ; a jump
```

`MOV IR, Rn` y `MOV IV, Rn` sobrescriben en tiempo de ejecución la palabra
de la siguiente instrucción o su inmediato, de modo que ese código debe
ejecutarse desde la RAM (`v32as` advierte en caso contrario). `MOV Rn, IV`
se rechaza. Todo esto es una extensión, desactivada bajo `--standard`. La
tabla está en el README ([Registros
internos](../README.es.md#registros-internos-extensión)).

---

## 8. Salidas de depuración para el simulador

### `-g`

```
v32as -g blink.asm
```

escribe, junto a `blink.vbin`:

* `blink.vbin.debug`, CSV `address,file,line[,label]`, una línea por
  instrucción (la primera etiqueta en esa dirección es la cuarta columna);
* `blink.vbin.sym`, los símbolos con sus direcciones finales.

```
0x20000000,blink.asm,8,_main
0x20000002,blink.asm,8
0x20000004,blink.asm,10,_frame
...
```

El `-g` simple es idéntico byte a byte al `-g program` del ensamblador
oficial (direcciones de palabra absolutas). `-g vbin` y `-g v32` dan en su
lugar desplazamientos reales en bytes dentro de un archivo `.vbin` o `.v32`.
Los errores y el archivo `.debug` apuntan a la línea que **tú** escribiste,
también a través de `%include` y de las macros (el código que genera una
macro se atribuye a la línea que la invocó).

Los compiladores que escriben comentarios marcadores `;@src FILE:LINE`
(`v32lua -g`) obtienen sus propias líneas en `.debug`, y las líneas `;@var`
también producen `.vars`; los formatos están en el README ([Archivos de
salida](../README.es.md#archivos-de-salida)).

### `--map` (programas en C de `v32cc`)

`v32cc -g` escribe un mapa CSV de líneas de ensamblador a líneas de C
(`asmfile,asmline,cfile,cline[,function]`). Dáselo a `v32as` y el archivo
`.debug` nombra en su lugar el archivo y la línea de C:

```
v32as -g --map main.map main.asm
```

```
0x20000000,main.c,10,main
0x20000002,main.c,11,main
0x20000004,main.c,12,main
```

Las instrucciones cuya línea de ensamblador no está en el mapa se omiten.
Con `--map` y sin `-g`, cada error o advertencia va seguido de una nota que
nombra la línea de C de la que procede.

### Símbolos y reubicaciones

```
v32as -g -r game.rel --symbols game.sym game.asm
```

`--symbols FILE` escribe `0xADDRESS,name,kind` por etiqueta (`F` código, `D`
datos; añade `--symbols-all` para las etiquetas locales al archivo). `-r
FILE` lista las palabras que contienen una dirección, `index,0xVALUE`, de
modo que un cargador pueda reubicar un módulo.

---

## 9. Construir una ROM: `%title`, `%texture`, `%sound`

Un cartucho es el programa más las texturas y los sonidos que usa,
empaquetados por `packrom` a partir de una definición XML. `v32as` puede
tomar ambas cosas del código fuente:

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

`TEX_SHIP` y `SND_BOOM` pasan a ser los números de entrada (las texturas y
los sonidos se numeran por separado, desde 0, en orden de primera
aparición), de modo que el programa y el cartucho siempre coinciden. Los
archivos se buscan junto al código fuente y luego en los directorios `-I`.

### La definición

```
v32as game.asm --rom-xml game.xml
```

escribe `game.vbin` y `game.xml`:

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

Las rutas son relativas al XML. Un `.png` o un `.wav` se nombra como el
`.vtex` o el `.vsnd` que `png2vircon` / `wav2vircon` hacen a partir de él, y
que luego colocas junto al XML.

### Scripts de construcción

```
v32as --build-scripts game.asm
```

escribe `Make.sh` y `Make.bat` (y `game.xml`, `gameOpt.xml`) junto al código
fuente, y ensambla como de costumbre. Ejecuta el script para reconstruirlo
todo:

```
./Make.sh
```

Ejecuta, en orden: el optimizador (`v32opt`, solo cuando está instalado; en
caso contrario una nota dice que se omite), `v32as`, `png2vircon` y
`wav2vircon` para cada recurso, y `packrom game.xml -o bin/game.v32`.
Termina con `BUILD SUCCESSFUL` o `BUILD FAILED`. Las rutas que contiene son
relativas al script, de modo que se puede ejecutar desde cualquier lugar.
`--build-scripts=sh` escribe solo el script de shell. Las herramientas que no
están en el `PATH` se pueden nombrar en `V32AS`, `V32OPT`, `PACKROM`,
`PNG2VIRCON`, `WAV2VIRCON`; `NO_OPT=1` omite el optimizador.

Un script solo se sustituye si lleva la línea `Generated by v32as`; un
`Make.sh` escrito a mano (o uno de `v32cc`) se deja en paz con una
advertencia.

Estas opciones necesitan un VBIN escrito en un archivo: no con `-C`, `-T`,
`-n`, `-E`, `-O`, `--v32x` ni STDIN, y son errores con `--standard`.
Detalles: README ([Definición del cartucho y scripts de
construcción](../README.es.md#definición-del-cartucho-y-scripts-de-construcción)).

---

## 10. Ensamblado y enlazado por separado: `-C` y `v32asld`

Ensambla cada archivo en un objeto reubicable y luego enlaza:

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

`global` y `extern` (nombres simples, sin `%`) indican qué etiquetas se
comparten; cualquier otra etiqueta es local a su archivo. Los objetos se
colocan en el orden de la línea de comandos, de modo que la primera palabra
del primer objeto es el punto de entrada (`--entry _name` pone un `jmp`
delante cuando quieres otro). El resultado es idéntico a ensamblar los
fuentes unidos en un solo archivo, y `-g` escribe el mismo `.debug`. La base
es cosa del enlazador: `-b`, `-c` y `-O ADDR` van a `v32asld`, no a `v32as
-C`.

Un nombre sin resolver se informa una vez por símbolo:

```
v32asld: error: undefined reference to `_helper' (first used in a.v32ob)
```

Más: `v32asld -A libb.a b.v32ob` crea un archivo de biblioteca (luego nómbralo
entre las entradas, o usa `-L DIR -l b`); `v32asld -R -o ab.v32ob a.v32ob
b.v32ob` une objetos en un solo objeto. Ambas cosas están en `v32asld(1)`;
el formato de objeto está en `docs/OBJECT.md`.

---

## 11. Compatibilidad estricta: `--standard`

```
v32as --standard game.asm
```

desactiva todas las extensiones, de modo que puedes comprobar que un
programa se ensambla con el `assemble` oficial y obtener su comportamiento
exacto: sin números binarios ni con sufijo `h`, sin `IP`/`IR`/`IV`, solo las
directivas originales del preprocesador (sin `%if`, `%macro`, macros tipo
función, inclusiones `<file>` ni `-I`), `label+N` es un error, un solo
archivo de entrada, `-v` imprime los mensajes de etapa oficiales, y un cero
inicial significa decimal:

```
mov R0, 0777
```

se ensambla como `0x309` (777) con `--standard` y como `0x1FF` (511, octal)
sin él. La tabla completa está en el README
([`--standard`](../README.es.md#--standard-exactamente-el-ensamblador-oficial)).

---

## 12. Estado de salida

| Estado | Significado |
|---|---|
| 0 | Éxito (las advertencias no lo cambian). |
| 1 | Un error de ensamblado o del preprocesador, o un archivo de entrada que no se puede leer. |
| 2 | Un error de uso (opciones incorrectas o incompatibles, ningún archivo de entrada en un terminal), o un archivo de salida que no se puede escribir. |

`v32asld` usa 1 para un error de enlazado y 2 para un error de uso, un
objeto o archivo de biblioteca incorrecto, o una biblioteca `-l` ausente.

---

## 13. Resolución de problemas

Los diagnósticos tienen la forma `file:line[:column]: error: message`. Los
errores dentro de una macro se informan en la invocación, seguidos de una
nota que nombra la macro y dónde está definida.

| Mensaje | Causa y solución |
|---|---|
| `label 'main' must start with an underscore` | Toda etiqueta empieza por `_`: escribe `_main:`. |
| `unknown identifier 'R16'` | No es un registro, un mnemónico ni un nombre de puerto. Los registros son `R0`..`R15` (`CR SR DR BP SP` son `R11`..`R15`). Con `--standard`, `IP`, `IR`, `IV` también son desconocidos. |
| `undefined label '_nowhere'` | Un salto o un `mov` usa una etiqueta que nunca se define. En un programa repartido en objetos, márcala como `extern` y enlaza. |
| `duplicate label '_main'` | Dos fuentes la definen. Varios archivos de entrada forman **un** programa, así que cada etiqueta puede definirse una sola vez (ensámblalos por separado con `-C` si ambos necesitan `_main`). |
| `syntax error, unexpected "','", expecting end of line` | Demasiados operandos para la instrucción. |
| `cannot open include file "nope.inc"` | No se encuentra junto al archivo que incluye ni en los directorios `-I`. Para `%include <v32as.inc>` comprueba que `include/v32as/` esté junto a `v32as`, o añade `-I DIR`. |
| `unterminated %if block` | Un `%if` / `%ifdef` sin `%endif`. (Solo una advertencia bajo `--standard`.) |
| `'label +/- integer' is not part of the official assembler's language` | Una extensión usada bajo `--standard`. |
| `'0777' is octal here, but the official assembler reads it as decimal` | Quita el cero inicial, o usa `--standard`. |
| `--origin cannot be combined with --bios or --cart` | Da solo uno de `-b`, `-c`, `-O`. |
| `--interactive cannot be combined with ...` | Consulta la [sección 5](#5-el-modo-interactivo--i). |
| `-g needs binary output to a file` | `-g` y `-r` necesitan un VBIN escrito en un archivo (`-o FILE` o un único archivo de entrada), no un listado, `-E`, `-C` ni STDOUT. |
| `--rom-xml and --build-scripts need a BIOS or cartridge VBIN written to a file` | Quita `-C`, `-T`, `-n`, `-E`, `-O`, `--v32x`, o da `-o FILE`. |

Si una macro hace algo inesperado, mira `v32as -E -P file.asm`. Si `v32as`
parece colgarse, está leyendo STDIN: canaliza o redirige el código fuente, o
da un nombre de archivo. Ejecutarlo sin argumentos en un terminal imprime en
su lugar el texto de uso.

---

## Siguientes pasos

* [README](../README.es.md): todas las opciones, los formatos de los
  archivos de salida y todo el preprocesador.
* `man v32as`, `man v32asld`, `man v32as-language`, `man v32as-inc`,
  `man v32as-control`.
* `docs/PREPROCESSOR.md` (notas de diseño), `docs/OBJECT.md` (formatos de
  objeto y de archivo de biblioteca), `CHANGES.md`.
