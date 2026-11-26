[English](README.md) | **Español** | [Français](README.fr.md)

# v32as: ensamblador y enlazador de Vircon32

`v32as` ensambla el lenguaje ensamblador de Vircon32 en un archivo VBIN, el
formato del programa de un cartucho. Con el mismo código fuente escribe los
mismos bytes que la herramienta oficial `assemble`, y añade un preprocesador
con macros, algunas formas numéricas extra, objetos reubicables con un
enlazador (`v32asld`), un modo de listado/detallado/paso a paso, un modo
interactivo y varias salidas de depuración. `--standard` desactiva todas las
extensiones.

Está escrito en C: el analizador léxico lo genera flex, el analizador
sintáctico bison, y los símbolos y las referencias pendientes (*fixups*)
viven en tablas hash. Las herramientas son `bin/v32as` (el ensamblador) y
`bin/v32asld` (el enlazador y archivador de bibliotecas).

> Nota: los demás documentos enlazados desde aquí (`docs/PREPROCESSOR.md`,
> `docs/OBJECT.md`, `CHANGES.md`) están en inglés; solo esta página y
> [`docs/USAGE.es.md`](docs/USAGE.es.md) están traducidas.

### En qué se diferencia del ensamblador oficial

En resumen: todo lo que acepta el `assemble` oficial, lo acepta y lo
ensambla en los mismos bytes (y `--standard` da exactamente el comportamiento
de la herramienta oficial, con las extensiones desactivadas); el resto son
añadidos. En términos generales:

- **Un verdadero preprocesador de macros** en lugar de los
  `%define`/`%ifdef`/`%include` oficiales: definiciones tipo función,
  expresiones `%if`, `%eval`, `%macro` de varias líneas con comprobaciones de
  sus operandos, bucles `%rep`, contextos para el flujo de control
  estructurado, `.etiquetas` locales al archivo, `-D`/`-I` en la línea de
  comandos y dos bibliotecas de macros incluidas.

- **Acceso a los registros internos de la CPU** (`IP`, `IR`, `IV`) mediante
  `MOV`, para código que se modifica a sí mismo: consulta [Registros
  internos](#registros-internos-extensión).

- **Un lenguaje más amigable**: formas numéricas extra (`0b`, octal, sufijo
  `h`, decimal con `#`), literales de carácter, `true`/`false`, operandos
  `etiqueta+N`, etiquetas e instrucción en una misma línea, mnemónicos y
  registros que no distinguen mayúsculas de minúsculas.

- **Ensamblado y enlazado por separado**: objetos reubicables (`-C`), y
  `v32asld` para enlazarlos y archivarlos.

- **Más salidas para herramientas**: listados (`-T`), un rastreo
  detallado/paso a paso, el modo interactivo (`-i`), archivos `.debug` con
  desplazamientos en bytes o líneas a nivel de C (`--map`), tablas de
  símbolos, de variables y de reubicaciones, dependencias para Makefile.

- **Papeleo del cartucho**: `%title`, `%texture`, `%sound` en el código
  fuente escriben el XML de `packrom` y `Make.sh`/`Make.bat`.

- **Diagnósticos** que nombran el archivo y la línea que escribiste, a
  través de `%include` y de las macros, con la cadena de inclusiones y de
  expansiones.

## Contenido

  * [Compilar, instalar y probar](#compilar-instalar-y-probar)
  * [Uso](#uso)
  * [Referencia de opciones](#referencia-de-opciones) ·
  * [Archivos de salida](#archivos-de-salida)
  * [Objetos y el enlazador](#objetos-y-el-enlazador)
  * [El lenguaje](#el-lenguaje)
  * [`--standard`](#--standard-exactamente-el-ensamblador-oficial)
  * [Preprocesador](#preprocesador)
  * [Definición del cartucho y scripts de construcción](#definición-del-cartucho-y-scripts-de-construcción)
  * [Registros internos](#registros-internos-extensión)
  * [Verificación](#verificación)
  * [Licencia](#licencia)
  * [Historia y compatibilidad](#historia-y-compatibilidad)

Las páginas del manual están en `man/`: `v32as(1)`, `v32asld(1)`,
`v32as-language(7)`, `v32as-inc(3)`, `v32as-control(3)`.

Otros documentos: **[`docs/USAGE.es.md`](docs/USAGE.es.md) (una guía
orientada a tareas: empieza por aquí)**, `docs/PREPROCESSOR.md` (notas de
diseño del preprocesador), `docs/OBJECT.md` (formatos de objeto y de
archivo), `CHANGES.md` (qué cambió en cada ronda).

## Inicio rápido

```
    v32as game.asm              # writes game.vbin (a cartridge program), as the official 'assemble game.asm'
    v32as -b bios.asm           # a BIOS instead (base 0x10000000)
    v32as -g game.asm           # also game.vbin.debug, for a debugger/simulator
    v32as -T -c game.asm        # print a listing of the encodings instead
    v32as -i                    # the interactive mode: type instructions, see them assembled
    v32as --standard game.asm   # the official assembler's behaviour exactly, no extensions
```

Los paquetes de publicación (un zip por sistema, desde la página de GitHub
Releases) contienen `v32as`, `v32asld`, las bibliotecas de macros en
`include/v32as/` junto a ellos (se encuentran allí sin `-I`), y estos
documentos y las páginas del manual bajo `docs/v32as/`. Descomprime uno en tu
carpeta `v32tools`, o en cualquier lugar de tu `PATH`.

## Compilar, instalar y probar

```
    make            # builds bin/v32as and bin/v32asld  (needs flex, bison, cc; readline optional)
    make test       # differential/golden tests (including .debug files) (POSIX sh + od + cmp + grep; GNU or BSD)
    make realcheck  # against the official assembler, built from a ComputerSoftware checkout (see below)
    make install    # v32as, v32asld, lib/*.inc, the man pages and the documents into ~/bin, ~/share/v32as/lib, ~/man and ~/share/doc/v32as
    make install PREFIX=/usr/local     # or any other PREFIX: PREFIX/bin, PREFIX/share/man, PREFIX/share/v32as/lib, PREFIX/share/doc/v32as
    make sysinstall # make install PREFIX=/usr/local (PREFIX and DESTDIR are honoured)
    make uninstall  # removes what install put there (same PREFIX/DESTDIR/BINDIR/MANDIR/LIBDIR/DOCDIR)
    make mancheck   # lints the man pages (plain sh and awk, no groff needed)
    make version    # stamps the header's version into v32asld and the man pages; VERSION=YYYYMMDD-status sets a new one first
    make archive    # ../v32as-project-YYYYMMDD.zip (the date of the VERSION), without build products
```

`install` respeta `DESTDIR` (para empaquetar) y los directorios se pueden
fijar uno por uno: `BINDIR`, `MANDIR`, `LIBDIR`, `DOCDIR`. `DEST_LANG=en|es|fr`
(por omisión `en`) elige el idioma de los documentos instalados (README,
`docs/USAGE.md`) y de las páginas del manual `v32as(1)` y `v32asld(1)`, por
ejemplo `sudo make sysinstall DEST_LANG=es`; las demás páginas del manual
(secciones 3 y 7) y los documentos de diseño (`docs/OBJECT.md`,
`docs/PREPROCESSOR.md`) existen solo en inglés. La versión es la línea
`VERSION` de `inc/v32as.h`; `make version VERSION=20261126-dev` (también
vale `V=`) la reescribe, junto con `src/ld.c` y la línea `.TH` de cada página
del manual. `make debug` compila con `-g -O0`. También hay una compilación con CMake (`cmake -S . -B build-cmake`;
mira el principio de `CMakeLists.txt` para la instalación, `ctest` y
`cpack`). Para el modo interactivo se usa GNU readline cuando se encuentran
sus archivos de desarrollo (`make READLINE=0` para no usarlo). `make clean`
elimina los productos de la compilación.

Estructura: `src/` fuentes (flex, bison, C), `inc/` cabeceras, `lib/`
bibliotecas de macros para `%include`, `man/` páginas del manual, `docs/`
notas de diseño, `tests/`, `experiments/`; `make` deja los archivos generados
en `build/` y las herramientas en `bin/`.

## Uso

```
    v32as [-b|-c|-O ADDR] [-o FILE] [-g[MODE]] [--map FILE] [-r FILE] [-w] [-D NAME[=VAL]] [-U NAME] [-I DIR]
          [--rom-xml FILE] [--build-scripts[=sh,bat]] [--standard] [-T|-n] [-E [-P]] [-s] [-v] [FILE...|-]
    v32as -i [-b|-c|-O ADDR] [-T|-n] [-D NAME[=VAL]] [-U NAME] [-I DIR] [-w] [-v] [--standard]
```

Por defecto `v32as FILE.asm` hace lo que hace el `assemble FILE.asm`
oficial: escribe `FILE.vbin`, un VBIN para un cartucho. (Al leer de STDIN, la
salida va a STDOUT.)

- `-i` inicia el modo interactivo (REPL): cada línea se ensambla en cuanto se introduce; los comandos son
  `.help .labels .dump .reset .quit`. No admite archivos de entrada (consulta `-i` en [la referencia de opciones](#compatibilidad-y-diagnósticos)).
- Sin archivos de entrada y con un terminal en STDIN, v32as imprime su resumen de uso (en STDERR, estado de salida 2).
- `-` lee de STDIN, y también lo hace ejecutarlo sin archivos cuando STDIN está redirigida o es una tubería; el VBIN va entonces a STDOUT.
  Las palabras sueltas heredadas (`bios`, `cart`, ...) siguen funcionando.
- Varios archivos de entrada comparten un único estado del preprocesador (macros, definiciones, contextos).
- Los errores y el archivo de `-g` apuntan al archivo y la línea que escribiste, también a través de `%include` y de las macros (en una
  línea donde se expandió una macro se omite la columna).
- v32as es austero con la memoria (también funciona en v32nix, el sistema tipo Unix de Vircon32): un archivo se preprocesa y, para la
  salida binaria, se ensambla línea a línea. Cómo se logró está en `CHANGES.md` (2026-10-27 y 2026-11-21).

Estado de salida: 0 si todo va bien (las advertencias no lo cambian), 1 para
un error de ensamblado o del preprocesador o un archivo de entrada ilegible,
2 para un error de uso (opciones incorrectas o incompatibles, o ningún
archivo de entrada en un terminal) o un archivo de salida que no se puede
escribir. `v32asld` usa lo mismo: 1 para un error de enlazado, 2 para un
error de uso, un objeto incorrecto, una biblioteca `-l` ausente o un archivo
de salida que no se puede escribir.

## Referencia de opciones

Esta es la lista única de opciones de `v32as`. `v32as --help` imprime la
misma lista en forma abreviada; la página del manual `v32as(1)` tiene el
texto completo.

### Salida y destino

- `-o, --output=FILE` da nombre a la salida. Por defecto es el archivo de entrada con su extensión sustituida por `.vbin`
  (`.v32ob` con `-C`), o STDOUT al leer de STDIN.
- `-b, --bios` ensambla para la BIOS (0x10000000); `-c, --cart` para un cartucho (0x20000000, el valor por defecto);
  `-O, --origin ADDR` para cualquier dirección base.
- `--v32x` (con `--origin`) escribe un ejecutable de v32nix en lugar de un VBIN: una cabecera de 8 palabras (`V32X`, versión 1, palabras de cabecera, palabras
  de programa, palabras de datos, entrada 0, origen, indicadores 0), y luego el programa. Las palabras de datos (que el `exec` de v32nix pone a cero tras el programa) proceden
  de `--v32x-data N`, o del `%define __V32X_DATAWORDS N` del código fuente (lo que escribe `v32cc --data-after-text`). En la consola una palabra
  de archivo es una palabra de máquina, tal como la lee el `exec` de v32nix.
- `-T, --text` imprime un listado legible (las codificaciones de las instrucciones) en lugar de escribir un VBIN.
  Las líneas `OFFSET:` (dirección) solo se imprimen cuando `-b`, `-c` u `-O` dan una base; `-n, --none` es `-T` sin dirección base.
  (`-B, --binary` sigue existiendo, y solo dice lo que ya es el valor por defecto.)
- `-C, --object` escribe un objeto reubicable `FILE.v32ob` para el enlazador; consulta [Objetos y el enlazador](#objetos-y-el-enlazador).
  No se puede combinar con `-b -c -O -g -r -T -n -E -B` ni con `--standard`; `--map` está permitido.

### Salidas de depuración y de símbolos

- `-g, --debug[=MODE]` escribe además `OUTPUT.debug` (CSV `address,file,line[,label]`, una línea por instrucción; con marcadores `;@src` en el
  código fuente, una por marcador) junto al archivo de salida. El `-g` simple es idéntico byte a byte al `-g program` del ensamblador oficial (direcciones de palabra absolutas,
  p. ej. `0x20000000`). `-g vbin` / `-g v32` dan desplazamientos reales en bytes dentro del archivo .vbin (cabecera de 0x0C) o .v32 (cabecera de 0x8C); las
  versiones de la herramienta oficial de esos dos imprimen valores desbordados. Un archivo fuente que se llame literalmente `program`, `vbin` o `v32` debe escribirse `./program`.
  Formato: [El archivo .debug](#el-archivo-debug). Con líneas `;@var` en el código fuente, también se escribe `OUTPUT.vars`, y `OUTPUT.sym`
  siempre (no con `--standard`): [El archivo .sym](#el-archivo-sym).
- `--map FILE`: FILE es el archivo de depuración del compilador (`asmfile,asmline,cfile,cline[,function]`, tal como lo escribe `v32cc -g`). El archivo `.debug` tiene entonces
  el mismo formato, pero cada instrucción nombra el archivo y la línea de C (y la función como columna de etiqueta), que es lo que lee la superposición de C de v32sim.
  Las instrucciones cuya línea de ensamblador no está en FILE se omiten. `--map` también funciona con `-C` y para los diagnósticos:
  - Los nombres de archivo se comparan primero de forma exacta y luego por nombre base (gana la cola de directorios compartida más larga), de modo que `main.asm` en el mapa coincide con
    `out/kernel/main.asm`. Si no coincide ninguna instrucción, una advertencia nombra los nombres encontrados en el mapa.
  - Con `--map FILE` (no hace falta `-g`) cada error y advertencia del ensamblador o del preprocesador va seguido de
    `a.c:123: note: the assembly line above was generated from here` (se nombra la función cuando el mapa la tiene) o, para una línea sin
    entrada propia, de `... it follows the code generated from here`, que nombra la línea mapeada anterior más cercana.
  - Con `-C` el objeto guarda el archivo y la línea de C (y la función) de cada instrucción en lugar de los de ensamblador; las instrucciones que el mapa
    no conoce se omiten, como en `-g --map`. `v32asld -g` escribe entonces el archivo `.debug` a nivel de C, igual que lo escribiría `v32as -g --map` sobre el
    código fuente unido.
- `-r, --relocs FILE` escribe `index,0xVALUE` por cada palabra que contiene una dirección resuelta a partir de una etiqueta (código o datos, incluido `label+N`) o
  calculada a partir de la dirección de la propia instrucción (`MOV Rn, IP`, las direcciones de parcheo de `IR`/`IV`). Los números simples que casualmente parecen direcciones
  nunca se listan, de modo que un cargador puede reubicar un módulo sumando la diferencia de base a esas palabras. Formato: [El archivo -r](#el-archivo--r-de-reubicación).
- `--symbols FILE` escribe `address,name,kind` por etiqueta, ordenado por dirección; tipo `F` (sigue código), `D` (siguen datos, o nada).
  `--symbols-all` añade las etiquetas locales al archivo como `function.local`, cada `%define global_NAME number` como tipo `V`, y cada `DATASYM+N` a la que
  el programa hace referencia (consulta `datasym`) como `N,DATASYM+N,V`: el desplazamiento en la columna de dirección. No disponible con `-C` (usa `v32asld --symbols`), `-E` ni `-i`.
- `-MD`, `-MMD`, `-MP`, `-MF FILE`, `-MT TARGET` escriben los fuentes, los archivos de `%include` y los archivos de `datafile` leídos, como una regla de Makefile (archivo
  por defecto: el nombre de la salida con `.d`). `-MF` y `-MT` implican `-MD`; `-MP` añade una regla vacía por cada dependencia que no sea un fuente nombrado.

### Preprocesador

- `-D, --define NAME[=VALUE]` (el valor por defecto es 1), `-U, --undef NAME`, procesadas en el orden de la línea de comandos.
  Un `%define` de un nombre de `-D` produce una advertencia (el archivo sigue ganando); `-D!NAME=VALUE` o `--force-define NAME=VALUE` hace que gane la línea de comandos.
  Un `%define` que cambia el valor de una macro también advierte (`-Wredefine`, activada por defecto; `-Wno-redefine`, `-w` y `--standard` la silencian). `-Wno-redefine` no
  silencia la advertencia sobre un nombre de `-D`; `-w` sí.
- `-I, --include DIR` añade DIR a la ruta de búsqueda de `%include` y `datafile` (puede repetirse).
- `-E, --preprocess` imprime el código fuente preprocesado con marcadores `# LINE "FILE"`; `-P` (con `-E`) omite los marcadores.
- `-Wmarkers` (desactivada por defecto; `-Wno-markers`) advierte de un `;@src` cuyo `FILE:LINE` está mal formado (sin `:`, una línea que no es un número >= 1,
  nada tras `;@src`), un `;@var` (sin NAME o sin `loc=`), un `;@scope` (que no es `{` ni `}`), un `;@scope }` sin pareja y un bloque que queda abierto.
  Un `;@src` así se trata como `;@src -` (entrada `-,0`): mejor ninguna fuente que una línea equivocada. `-w` la silencia.

### Definición de ROM y scripts de construcción

Todas estas son extensiones (un error bajo `--standard`) y necesitan un VBIN
de BIOS o de cartucho escrito en un archivo (no `-C`, `-T`, `-n`, `-E`, `-O`,
`--v32x` ni STDIN). No se escribe nada a menos que se dé una de ellas: un
simple `v32as FILE.asm` sigue escribiendo solo `FILE.vbin`.

Toda la funcionalidad se describe en [Definición del cartucho y scripts de
construcción](#definición-del-cartucho-y-scripts-de-construcción).

- `--rom-xml FILE` escribe además la definición de ROM que lee `packrom`, a partir de los `%title`, `%version`, `%texture` y `%sound` del código fuente.
- `--rom-xml-opt FILE` escribe lo mismo para la compilación hecha a partir de la salida de `v32opt` (binario `NAMEOpt.vbin`, título `<title> [optimized]`).
- `--build-scripts[=FORMATS]` escribe `Make.sh` y `Make.bat` (FORMATS: `sh`, `bat` o `sh,bat`, el valor por defecto) junto al primer fuente (en su directorio padre para una
  estructura `src/` + `obj/`), y las definiciones que usan (`NAME.xml`, `NAMEOpt.xml`) salvo que `--rom-xml` nombre la primera.
- `--no-build-scripts` nunca los escribe (los scripts lo pasan al `v32as` que ejecutan; también lo hace la variable de entorno `V32AS_BUILD_SCRIPT`).
- `--build-script-dir DIR` escribe los scripts en DIR (que debe existir) en su lugar.

### Compatibilidad y diagnósticos

- `--standard`: exactamente el ensamblador oficial; consulta [`--standard`](#--standard-exactamente-el-ensamblador-oficial).
- `-w, --no-warnings` inhibe todas las advertencias.
- `-s, --step` hace una pausa por sentencia; `-v, --verbose` muestra los campos de bits codificados (en STDERR). Bajo `--standard`, `-s` no hace nada y `-v` imprime los
  mensajes de etapa del ensamblador oficial.
- `--stats` imprime en STDERR, al final, el uso de memoria dinámica del propio v32as: la marca de agua máxima y lo que sigue vivo entonces, en bytes y en palabras de 4 bytes, lo mismo
  tras el preprocesado (para un archivo procesado en flujo, también tras ensamblarlo), y el número de asignaciones (contadas en las llamadas al asignador; "not available" donde la biblioteca de C no puede dar el tamaño de un bloque).
  Compilado con `v32cc` (v32nix, `__V32CC__`) imprime palabras en su lugar, desde el asignador de la biblioteca de C (`v32_heap_info`): el **peak break** (el tamaño del montón, con huecos
  y cabeceras de 3 palabras incluidos, que es lo que tiene que contener una ranura de proceso; muestreado tras cada asignación que hace v32as, por lo que es una cota inferior, ya que la biblioteca no guarda ninguna
  marca de agua máxima), y las palabras en uso, libres y en el hueco mayor tras el preprocesado (tras el ensamblado, para un archivo procesado en flujo) y al final.
- `-i, --interactive` inicia el modo interactivo (REPL) sobre STDIN: cada línea se ensambla y se imprime su codificación en cuanto se
  introduce; `.help .labels .dump .reset .quit` son sus comandos, un `%macro` o `%if` continúa en las líneas siguientes (indicador `...>`).
  No admite archivos de entrada. Se combina con `-b -c -O -T -n` (sin ninguna de ellas las direcciones empiezan en 0), `-D -U -I`, `-w`, `-v` y
  `--standard`; con una opción que escribe un archivo o que necesita un programa completo (`-o -E -B -C -g -r --symbols --map -MD --rom-xml
  --rom-xml-opt --build-scripts --v32x -s`) es un error de uso. La edición de línea y el historial necesitan GNU readline (o libedit de macOS) en el momento de compilar.
- `-h, --help`, `-V, --version`.

## Archivos de salida

### El archivo .debug

`OUTPUT.debug` es CSV, `address,file,line[,label]`, con una línea por
instrucción; se escribe junto al archivo de salida (para `game.vbin`,
`game.vbin.debug`). El `-g` simple es idéntico byte a byte al `-g program`
del ensamblador oficial; la primera etiqueta definida en una dirección se
escribe como cuarta columna.

**Marcadores `;@src` (no hace falta `--map`).** Una línea de comentario por
sí sola, `;@src FILE:LINE` (o `FILE:LINE:COL`; `;@src -` para "sin fuente"),
tal como la escribe `v32lua -g` (consulta V32DBG-SPEC). Cuando el código
fuente tiene algún marcador, `OUTPUT.debug` recibe de ellos
`ADDRESS,FILE,LINE[,FUNCTION]`:

**una entrada por marcador, en la dirección de la primera instrucción que le
sigue** (así, dos marcadores consecutivos que nombran la misma línea dan dos
entradas; los marcadores sin instrucción entre ellos dan una, la del último).
Una instrucción con la que no empieza ningún marcador no tiene entrada (las
anteriores al primer marcador no tienen ninguna). FILE se separa de LINE en
el último `:` (o en los dos últimos cuando ambos terminan en dígitos), de
modo que una letra de unidad no da problemas. El marcador debe ser lo primero
de su línea (un `;@src ...` al final de una instrucción es solo un
comentario). Los marcadores sobreviven a `%include` y a las macros como los
comentarios que son; el nombre de archivo del marcador es lo que se escribe.

- **`;@src -`** (petición de v32sim): la primera instrucción que le sigue recibe la entrada `ADDRESS,-,0` (archivo `-`, línea 0, sin función), de modo que el simulador sabe dónde
  termina la última línea de código fuente y empieza el runtime. Un marcador mal formado cuenta como `;@src -`. Las instrucciones anteriores al primer marcador siguen
  sin tener entrada.
- **FUNCTION** es la función en la que está la instrucción, escrita `NAME` (`__function_main:` -> `main`, `__function_f__l0:` -> `f__l0`); no hay cuarta
  columna fuera de las funciones. Todas las entradas de una función la tienen, no solo la primera. A diferencia del `-g` simple (que escribe la primera etiqueta en la
  dirección), en el modo con marcadores no se escribe ninguna otra etiqueta. Una función empieza en una etiqueta `__function_NAME:` y termina en la línea
  `;@fn __function_NAME ...` que el compilador de Lua escribe tras su cuerpo, en un `;@src -`, o al final del programa. Las funciones se anidan (el cuerpo de una
  clausura de Lua está dentro del código de su padre, y el código del padre posterior a ella vuelve a ser del padre), y el código entre funciones o tras
  `;@src -` (inicialización global, el runtime) no tiene función. Un `;@fn` escrito *encima* de una etiqueta (la forma `clobbers=` del runtime) no es un
  final, para ningún lenguaje. `__function_NAME_return:` no es una función.
- **`;@lang X`** (una línea de comentario por sí sola, antes de la primera función; cuenta la primera, las posteriores se ignoran): el lenguaje del
  productor, `c` para `v32cc`, `lua` para `v32lua`. De él dependen dos cosas. (1) Con cualquier lenguaje **distinto de `lua`** (comparado sin distinguir mayúsculas
  de minúsculas) las funciones no se anidan, de modo que **la siguiente etiqueta `__function_NAME:` de nivel superior termina la función anterior**: sus rangos y
  bloques de `;@var` (cerrados o no) terminan en la dirección de esa etiqueta, exactamente donde cambia la columna FUNCTION, y no hace falta ningún `;@src -` ni `;@fn` tras
  el cuerpo. Sin `;@lang`, o con `lua`, nada cambia. (2) El lenguaje se registra en los archivos laterales, véase más abajo.
- **`# lang X`**: cuando el código fuente tiene `;@lang X` (y no se da `--map`), la **primera línea de `OUTPUT.debug`** es el comentario
  `# lang X` (solo en el modo con marcadores: con `-g` con entradas de marcadores `;@src`; un archivo de `-g` simple y un archivo de `--map` no cambian) y la
  primera línea de `OUTPUT.vars` (cuando se escribe) es el mismo comentario. **Los lectores deben saltarse toda línea que empiece por `#`.** Sin
  `;@lang` no hay tal línea; bajo `--standard` los marcadores son comentarios, así que tampoco la hay.
- **`--map FILE` sigue funcionando**: para una instrucción que el mapa nombra, se escribe la entrada del mapa; cada uno de los demás marcadores sigue recibiendo la suya. `v32cc`
  no escribe marcadores hoy, así que su salida no cambia.
- **`-C`**: el objeto guarda las entradas de los marcadores en lugar de las líneas de ensamblador (versión 2 del formato de objeto; la palabra de función `0xFFFFFFFE` significa "sin
  función, y `v32asld -g` no debe añadir una etiqueta"), de modo que `v32asld -g` escribe el mismo archivo que `v32as -g` sobre el código fuente unido. La precedencia de `--map`
  es la misma.
- **`--standard`**: los marcadores son comentarios; `-g` escribe las líneas de ensamblador como antes.

### El archivo .sym

`OUTPUT.sym` (con `-g`, no con `--standard`; `OUTPUT` es el nombre del
archivo de salida, así `prog.vbin.sym`): los símbolos con sus direcciones
**finales**, en el formato del `NAME.asm.sym` de v32cc, de modo que un mismo
archivo sirva para `nm`, para un backtrace de pánico y para un depurador. Las
direcciones son siempre las que ve la CPU (`0x20000003`), sea cual sea el
modo de `-g`. Primero la línea opcional `# lang X` (como en `.debug`), y
luego:

```
# v32as symbols v1: kind,name,address,words,cfile,cline
F,main,0x20000003,10,t.c,3          a function: a top-level label followed by code; "__function_" is removed from the name
V,counter,0x00000010,1,,            a compiler variable: %define global_NAME number (words 0: not known) or var_NAME number (v32lua, words 1)
R,_helper_data,0x20000016,2,,       a top-level label followed by data
```

Orden: funciones, variables, datos, cada grupo por dirección. `words` es la
distancia a la siguiente etiqueta de nivel superior (hasta el final del
programa para la última); las etiquetas locales al archivo
(`function.local`) no terminan una función. `cfile,cline` es la posición en
el código fuente de la primera instrucción de la función (vacía cuando no
hay). Las direcciones y tamaños `V` de `NAME.asm.sym` (que conoce los tipos
de C) son los mejores cuando existen ambos; este archivo añade las
direcciones finales de funciones y de datos de ROM, que `NAME.asm.sym` da
como `@label` / `@rodata+OFF`. `v32asld -g` escribe el mismo archivo para
los objetos enlazados, sin líneas `V` (un objeto no lleva `%define`s). Los
lectores deben saltarse las líneas que empiezan por `#`.

### El archivo .vars

`OUTPUT.vars` (con `-g`, junto a `OUTPUT.debug`; solo cuando el código fuente
tiene líneas `;@var`; en caso contrario se elimina un archivo antiguo): las
variables locales como rangos de direcciones, que solo el ensamblador puede
calcular. Las direcciones son como en `.debug` (`-g`, `-g vbin`, `-g v32`),
siendo END la primera dirección que queda fuera de ámbito. Una línea CSV
cada una, en orden de START (un bloque antes de sus variables):

```
scope,START,END,DEPTH[,FUNCTION]
var,START,END,NAME,LOC,TYPE,FLAGS,DEPTH[,FUNCTION]
```

`LOC` es el `loc=` tal como se escribió (`BP-3`, `BP+2`, `R5`, `@0x20003000`; `-` para `optimized-out`), salvo que un **símbolo se resuelve**: véase
más abajo. `TYPE` es el `type=` (`-`
si no hay), `FLAGS` las palabras sueltas que lo siguen (`boxed`, `optimized-out`, unidas con `+`; `-` si no hay; las palabras `key=value` distintas de
`loc`/`type` se ignoran). `DEPTH` es 0 para el ámbito propio de la función y 1, 2, ... para los bloques dentro de ella, de modo que el simulador pueda elegir la
más interna de dos variables con el mismo nombre. Las líneas scope son los bloques `;@scope {` ... `}`.

- **Rangos.** Un `;@var` está en ámbito desde la primera instrucción que le sigue. Termina en el `;@scope }` del bloque en el que se declaró
  o, sin bloque, al final de la función; o en otro `;@var` del **mismo nombre en el mismo bloque** (un nuevo hogar,
  p. ej. el `loc=R5` del optimizador, o `;@var NAME optimized-out`). Un `;@var` del mismo nombre en un bloque *anidado* oculta al otro y no termina
  el exterior. Los rangos vacíos (`do end`) se omiten. Un `;@scope }` sin pareja se ignora y un bloque abierto se cierra con su
  función (ambos advierten con `-Wmarkers`).
- **`;@home FROM TO`** (un optimizador movió variables; `v32opt` lo escribe cuando promueve una posición de pila a un registro): toda variable
  en ámbito cuyo hogar (su `loc=` actual) sea `FROM` recibe a partir de este punto un nuevo registro, en el mismo bloque, con el hogar `TO`:
  `;@home BP-3 R5` cuando el valor pasa a R5, `;@home R5 BP-3` cuando vuelve. `TO` puede ser `optimized-out` (el valor ha desaparecido;
  el registro recibe ese indicador y `loc` `-`). El registro antiguo termina aquí, de modo que `NAME.vbin.vars` lista la variable una vez por hogar. Un `;@home`
  que no coincide con nada se ignora. Como las demás líneas `;@`, es un comentario para cualquier otra herramienta.
- **Ubicaciones simbólicas.** Un `loc=SYMBOL` donde SYMBOL es una etiqueta, `label+N` / `label-N` (N decimal o `0x...`), o un nombre definido con `%define` como
  número o como una de esas formas (el `%define var_NAME 0x78` de v32lua; el texto de sustitución debe ser exactamente eso, sin otra expresión) se escribe en
  la columna LOC como `@0xADDRESS` cuando se conoce al final del ensamblado. Una etiqueta da su dirección al estilo de las columnas START y END del modo de `-g`
  (absoluta para `-g`, el desplazamiento en el archivo para `-g vbin` / `-g v32`); un número definido con `%define` es un valor simple (una dirección
  de RAM) y se escribe tal cual, sea cual sea el modo. Los registros (`BP-3`, `BP+2`, `R5`, ...), `@0x...` y `-` se dejan como se escribieron, igual que un
  símbolo que no se conoce (que además advierte con `-Wmarkers`). Cuentan los `%define`s vigentes al final del archivo.
- **Varios archivos (`-C`).** El objeto guarda los registros (versión 3 del formato de objeto, o 4 con un `;@lang`, consulta `docs/OBJECT.md`) y `v32asld -g`
  escribe `OUTPUT.vars` y `OUTPUT.debug` como lo haría `v32as -g` sobre el código fuente unido; `v32asld -R` los conserva. Un número definido con `%define` se
  resuelve al ensamblar; una etiqueta (del objeto, o una global de otro objeto) la resuelve `v32asld` con su dirección final, en el
  modo de `-g` dado al enlazador. Con varios objetos: se escribe el primer `;@lang` encontrado. Probado con dos archivos, una función cada uno,
  y con archivos de estilo C.
- `--standard`: las líneas `;@` son comentarios; no hay `.vars`.

Ejemplo (`m.asm` tiene las líneas `;@src prog.lua:1`, `__function_main:`,
`;@scope {`, `;@var x loc=BP-1 type=int`, dos instrucciones `mov`,
`;@scope }`, `;@fn __function_main` y un `hlt`; `v32as -g m.asm` escribe
`m.vbin.vars` con este contenido):

```
scope,0x20000000,0x20000004,1,main
var,0x20000000,0x20000004,x,BP-1,int,-,1,main
```

### El archivo -r (de reubicación)

**Formato del archivo `-r`** (un cargador puede depender de él): una línea
por palabra, ordenada por `index` (ascendente, sin duplicados); `index` es el
**índice de palabra basado en cero dentro de la imagen**, no un
desplazamiento en bytes y sin contar la cabecera VBIN de 3 palabras (la
primera instrucción es la palabra 0); `0xVALUE` es el valor **tal como se
ensambló**, es decir, en la base para la que se ensambló o enlazó la imagen,
de modo que un cargador que coloque la imagen en `new_base` suma
`new_base - assembled_base` a una palabra `C` (y, para una palabra `D`, el
desplazamiento de los datos) y deja en paz las demás; la tercera columna,
opcional, es `C` o `D` y está presente en todas las líneas si el código
fuente (o cualquier objeto enlazado) declara un `datasym`, y en ninguna en
caso contrario. Tanto `v32as -r` como `v32asld -r` escriben exactamente esto.

Un código fuente que declara `datasym NAME` (sintaxis ampliada; el valor de
`NAME` es 0, así que `NAME+17` es una palabra que contiene 17) recibe una
tercera columna, `,C` para una dirección de código (se mueve con la
dirección de carga) y `,D` para una referencia a un datasym (se mueve con la
base de los datos del módulo). `v32cc --pic-data` escribe así la dirección de
cada global. Un datasym también se puede usar con `-C`; `v32asld -r` escribe
entonces la misma columna.

### El archivo --symbols

Una línea por etiqueta, `0xADDRESS,name,kind`, ordenada por dirección. Tipo
`F`: sigue código; `D`: siguen datos (o nada).

Con `--symbols-all`: las etiquetas locales al archivo como `function.local`,
tipo `V` para cada `%define global_NAME number` (el número en la columna de
dirección), y `N,DATASYM+N,V` para cada `DATASYM+N` a la que el programa
hace referencia. `v32asld --symbols` escribe lo mismo para un programa
enlazado (las etiquetas locales con el nombre que les dio el ensamblador,
p. ej. `_f__loop`).

## Objetos y el enlazador

`v32as -C x.asm` escribe un objeto reubicable `x.v32ob`; `v32asld a.v32ob
b.v32ob -o prog.vbin [-b|-c|-O ADDR] [-g] [-r FILE]` los enlaza. `global
_name` / `extern _name` (nombres simples, sin `%`) indican qué etiquetas se
comparten. Enlazar equivale a ensamblar los fuentes como un único archivo;
los objetos llevan los registros de depuración, de modo que `v32asld -g`
escribe el mismo archivo `.debug`. Formato y detalles: `docs/OBJECT.md`.

`datasym` lo acepta `-C`; el objeto lo registra (versión 2 del formato de
objeto) y `v32asld -r FILE` (también `--emit-relocs FILE`) escribe la
tercera columna, `,C` o `,D`, como hace `v32as -r`. Un datasym tiene el
valor 0 en todos los objetos y el enlazado no lo mueve.

### Opciones de v32asld

- `-o, --output FILE` (por defecto `a.vbin`; `a.v32ob` con `-R`); `-b, --bios`, `-c, --cart` (por defecto), `-O, --origin ADDR`.
- `-g, --debug[=MODE]` escribe `OUTPUT.debug` (y `OUTPUT.vars` cuando los objetos tienen registros `;@var`; ambos empiezan con `# lang X` cuando
  los objetos se hicieron a partir de un código fuente con `;@lang X`, y se resuelven las ubicaciones simbólicas de `;@var`; también `OUTPUT.sym`, consulta
  [El archivo .sym](#el-archivo-sym)); `-r, --relocs FILE` (también `--emit-relocs FILE`)
  la lista de reubicaciones (formato más arriba); `--symbols FILE` la tabla de etiquetas.
- `-A, --archive FILE` crea un archivo de biblioteca (`V32-VARC`) con los objetos dados, en lugar de enlazar. Los archivos de biblioteca dados como operandos o con
  `-L DIR -l NAME` (`DIR/NAME.v32a`, si no `DIR/libNAME.a`; `.` se busca siempre en último lugar) se explotan después de los objetos, y un miembro se enlaza
  solo cuando define un símbolo aún sin definir.
- `--entry SYMBOL` pone al principio un `jmp SYMBOL` de dos palabras, salvo que el símbolo ya sea la primera palabra.
- `--v32x --data N [--entry SYMBOL|OFFSET]` escribe un ejecutable V32X (cabecera + imagen enlazada en 0x100000; `-O` cambia la base; `-b`/`-c` y `-g vbin|v32`
  se rechazan).
- `-R, --relocatable` es un enlazado parcial: `v32asld -R -o ab.v32ob a.v32ob b.v32ob` (no `-r`, que es la lista de reubicaciones) une
  objetos en un solo objeto. Los globales siguen siendo globales, un `extern` que define otra entrada pasa a ser una referencia a ella, los
  demás siguen siendo `extern`; se conservan las reubicaciones, los datasyms y los registros de depuración, de modo que enlazar `ab.v32ob` equivale a enlazar
  `a.v32ob b.v32ob`. Los archivos de biblioteca y `-l` funcionan como en un enlazado completo. Rechaza `-b -c -O -g -r --symbols --entry --v32x`.
- `-w, --no-warnings` no advierte de un `extern` que ningún código usa.
- Errores: ``undefined reference to `x' (first used in a.v32ob)``, una vez por símbolo; un global definido por dos objetos; un objeto incorrecto o truncado.

## El lenguaje

### Directivas de datos

`integer`, `float`, `pointer`, `string`, `zstring`, `datafile`. Una palabra
por elemento; `string` es una palabra por carácter más un 0 final.
`zstring` va empaquetada en bytes: cuatro caracteres por palabra (el primer
carácter en el byte bajo), terminada en cero, con la última palabra rellena
de ceros. (`zstring` no está en el ensamblador oficial; su archivo golden
está verificado a mano, no es salida oficial.) `datafile` lee palabras en
little-endian.

**Búsqueda de `datafile`**: ruta absoluta; el directorio del archivo que
contiene la directiva; cada `-I DIR`; el directorio actual. El error lista
los lugares probados: `cannot open data file "x" (looked in out/, ../data/, .)`.
`--standard` conserva los dos lugares antiguos (el directorio del archivo y
el directorio actual; sin `-I`, como para `%include`).

`global`, `extern` y `datasym` se describen en [Objetos y el
enlazador](#objetos-y-el-enlazador) y en `-r`.

### Números, nombres, etiquetas

Hexadecimal `0xFF` y `FFh`, binario `0b1010`, octal (un cero inicial),
decimal `42` y `#42`, flotantes `3.14`, literales de carácter `'A'`,
`true`/`false`. Una etiqueta y una sentencia pueden compartir línea; los
mnemónicos y los registros no distinguen mayúsculas de minúsculas. Una
etiqueta empieza con un guion bajo. Un puerto de E/S en `in`/`out` es un
nombre (`GPU_Command`) o un número en cualquiera de estas formas (`0x200`,
`512`), como en la herramienta oficial y con la misma codificación; un número
que no es un puerto conocido se ensambla con una advertencia.

### Etiqueta más constante

Dondequiera que se acepta una etiqueta como valor, también se acepta
`label + integer` o `label - integer` (una etiqueta, una constante): `mov R0,
_tab+2`, `mov R1, [_tab+3]`, `mov [_tab-1], R1`, `jmp _f+4`, `pointer
_tab+1, _end`. Se resuelve como una etiqueta simple (incluidas las
referencias adelantadas). `--standard` lo rechaza.

### `--standard`: exactamente el ensamblador oficial

`--standard` desactiva todas las extensiones, para sustituir directamente a
`assemble` (y para comprobar que un programa es portable a él):

| área | en `--standard` |
|---|---|
| números | `0b101`, `FFh`/`2Ah`, `#42` y `0X1F` son errores. Un cero inicial es **decimal** (`0100` es 100), como lo lee la herramienta oficial. Un literal decimal por encima de 2147483647 y un flotante denormal son errores; `-0xFFFFFFFF` se desborda, como en la herramienta oficial |
| valores de puerto | un valor de puerto suelto (`GPUCommand_ClearScreen`) solo como valor de `out`, como en la herramienta oficial |
| listas de datos | `integer 1, 2,` (una coma final) se acepta, como en la herramienta oficial |
| nombres | `IP`/`IR`/`IV` (registros internos) y `zstring` no existen |
| directivas | solo las originales, escritas en minúsculas; sin `%if %elif %assign %eval %includeonce`, sin macros tipo función (`%define F(x) ...` define la macro simple `F` cuyo valor empieza por `(x)`), sin etiquetas `.local`, sin símbolos del estilo de `__FILE__` |
| `%define` | un valor que contiene su propio nombre, o un `%`, es un error |
| `%include` | solo `"file"`, relativo únicamente al archivo que incluye (sin `-I`, sin `<file>`) |
| operandos | `label +/- integer` es un error |
| `datafile` | la ruta tal como está escrita, desde el directorio actual, como la abre la herramienta oficial; después (una tolerancia) junto al archivo fuente. Sin `-I` |
| línea de comandos | un solo archivo de entrada; `-s` no hace nada; `-v` imprime los mensajes de etapa oficiales |
| `%if` sin cerrar | una advertencia (la herramienta oficial lo acepta en silencio) |

Algunas cosas se siguen aceptando bajo `--standard` que la herramienta
oficial rechaza, porque no pueden cambiar los bytes de un programa que esta
acepta: una etiqueta y una sentencia en una línea (`_a: hlt`), los caracteres
de control y una marca de orden de bytes UTF-8 inicial en el código fuente.
En sentido contrario, `_` sola no es una etiqueta para v32as, aunque la
herramienta oficial la permite, y las líneas terminan solo en LF o CRLF (no
en un CR suelto).

Sin `--standard`, el mismo programa también se acepta, con una trampa que
conviene conocer: un número con un cero inicial es **octal** aquí (`0777` es
511) pero **decimal** en el ensamblador oficial. `v32as` advierte de cada
literal así; usa `--standard`, o quita el cero inicial.

## Preprocesador

El código fuente se preprocesa antes de ensamblarse (`-E` muestra el
resultado). Las notas de diseño están en `docs/PREPROCESSOR.md`. Una
directiva es una línea cuyo primer carácter no blanco es `%`; los nombres de
directiva no distinguen mayúsculas de minúsculas, los de macro sí. Las macros
se sustituyen solo en el código: nunca dentro de cadenas, literales de
carácter, comentarios o números.

### Todo lo que tiene el ensamblador oficial

```
%define NAME tokens...      %undef NAME
%ifdef NAME   %ifndef NAME  %else   %endif
%include "file"             %error "message"   %warning "message"
```

`%define NAME value` se expande donde se *usa* (enlace tardío), y una macro
nunca se vuelve a expandir dentro de su propia expansión. Las rutas de
`%include` son relativas al directorio del archivo que incluye (límite: 20
niveles). Los bloques `%ifdef` omitidos descartan todas las líneas, incluidas
las directivas desconocidas. Un bloque sin cerrar es un error (la herramienta
oficial lo deja pasar; `--standard` también, con una advertencia).

### Extensiones

- **Macros tipo función**: `%define MUL(a, b) ((a) * (b))`, usada como `MUL(3, 4)`. Los argumentos se expanden
  antes de la sustitución. `#x` convierte el argumento en una cadena, `a ## b` une dos tokens
  (`%define LAB(n) _label_ ## n`), `...` / `__VA_ARGS__` toman los argumentos restantes (y `, ## __VA_ARGS__`
  elimina la coma cuando no hay ninguno). `#` seguido de un dígito sigue siendo la sintaxis numérica `#42`. Un nombre
  seguido de `(` solo es una invocación cuando la macro es de tipo función.
- **`%if expr`, `%elif expr`, `%elifdef`, `%elifndef`**, `%else`, `%endif`.
- **Expresiones** (`%if`, `%assign`, `%eval`): enteros (de 32 bits, con desbordamiento cíclico) y flotantes (de 32 bits), todas las formas numéricas que lee el ensamblador, `defined(NAME)`, `true`/`false`, los nombres de puertos y de constantes que
  conoce el ensamblador, los operadores `( ) + - ! ~ * / % << >> < <= > >= == != & ^ | && || ?:` con la precedencia de C
  (`&&`, `||` y `?:` no evalúan lo que se saltan), y las funciones `int() float() abs() min() max()
  floor() ceil() round()`. Una operación con un operando flotante es una operación en coma flotante (`7/2` es 3, `7/2.0` es 3.5).
- **`%assign NAME expr`** calcula un valor ahora: `%assign N N + 1`. (`%eval NAME expr` es lo mismo.)
- **`%eval(expr)`** en cualquier lugar de una línea se sustituye por el valor: `mov R0, %eval(WIDTH * 2 + 1)` se convierte en
  `mov R0, 641`. Esto es lo que permite que los símbolos participen en aritmética, algo que `%define MATH A+B` no puede hacer:
  el ensamblador no tiene expresiones, así que la suma tiene que existir como número en el código fuente. Los flotantes se escriben
  de nuevo en la forma más corta que se lee como el mismo flotante.
- **Etiquetas `.local`**: tras una etiqueta global `_draw:`, el nombre `.loop` significa `_draw__loop`
  (así `.loop:` la define y `jt R1, .loop` la usa).
- **Símbolos**: `__FILE__`, `__LINE__`, `__COUNTER__`, `__INCLUDE_LEVEL__`, `__V32AS__` (fecha de la versión como
  número), `__BASE__` (primera dirección), y uno de `__BIOS__`, `__CART__`, `__ORIGIN__`, `__NONE__`.
  Un `%define` del mismo nombre gana.
- **Inclusiones**: `%include <name>` busca en los directorios `-I DIR` y luego en los directorios de bibliotecas; `%include "name"` prueba
  primero el directorio del archivo que incluye y luego la misma lista. Los directorios de bibliotecas son el `LIBDIR` de `make install`
  y, junto al programa, `lib`, `../lib`, `../share/v32as/lib`, `../Vircon32/v32tools/include/v32as` e `include/v32as`
  (la estructura de los zips de publicación), de modo que un v32as instalado o descomprimido encuentra sus bibliotecas de macros sin `-I`. `%includeonce` en un archivo hace que cualquier
  `%include` posterior de él no haga nada.
- **Línea de comandos**: `-D NAME[=VALUE]` (el valor por defecto es 1), `-U NAME`, `-I DIR`, `-E` (imprime el
  código fuente preprocesado con marcadores `# LINE "FILE"`, `-P` sin ellos).
- **`%title`, `%version`, `%texture`, `%sound`**: la definición de ROM, para `--rom-xml` y `--build-scripts`; consulta
  [Definición del cartucho y scripts de construcción](#definición-del-cartucho-y-scripts-de-construcción).
- **`%idefine`, `%xdefine`, `%ixdefine`**: una definición que no distingue mayúsculas de minúsculas; una definición cuyo valor se expande
  ahora en lugar de donde se usa; ambas cosas.

### Macros que escriben instrucciones

```
%macro LIMIT reg, lo=0, hi=255        ; named parameters, with defaults
    imax  reg, lo
    imin  reg, hi
%endmacro

%macro COUNTDOWN reg, count
    mov   reg, count
%%top:                                ; private to each expansion (becomes __M<serial>_top)
    isub  reg, 1
    jt    reg, %%top
%endmacro

    LIMIT R3, , 100                   ; invoked like an instruction (labels may come first)
    COUNTDOWN R1, 10
```

- **`%macro NAME params` ... `%endmacro`**. Los parámetros son `name`, `name=default` (un valor por defecto lo hace
  opcional; un argumento vacío toma el valor por defecto) y un último `rest...` (o `...`) que toma los argumentos
  restantes. `%macro NAME 2`, `1-3` o `1-*` solo fija cuántos argumentos hay. Dentro del cuerpo un nombre de
  parámetro, o `%1` `%2` ..., representa el texto de su argumento, `%0` el número de argumentos y `%*`
  todos ellos. Los argumentos se separan en las comas que están fuera de `()` `[]` `{}` y de comillas; `{a, b}` pasa una coma
  como parte de un único argumento. Una macro puede definirse varias veces si los números de argumentos difieren; `%unmacro NAME`
  la elimina.
- El nombre de una macro **distingue mayúsculas de minúsculas** (`%imacro` lo hace insensible) y **prevalece sobre una instrucción** del
  mismo nombre, con una advertencia salvo que `%pragma shadow` (todo) o `%pragma shadow NAME, ...` lo permita.
- **`%rep count` ... `%endrep`** repite su cuerpo (como máximo 1.000.000 de veces); `%@` es el número de iteración.
  `%exitrep` sale del bucle. Los bucles se anidan y pueden contener macros y condicionales.
- **`%exitmacro`** sale de la macro (también desde dentro de un `%if` o `%rep`). **`%rotate n`** rota aquello a lo que se refieren `%1`
  `%2` ... (a la izquierda para `n` positivo), la forma de recorrer una lista variádica:
  `%rep %0` / `push %1` / `%rotate 1` / `%endrep`.
- **`%%name`** es una etiqueta privada de una expansión de una macro (una por iteración para un `%rep` fuera de una macro;
  dentro de una macro es de la macro, así que usa `%%name_%@` para una etiqueta por iteración).
- **Pruebas de operandos**, utilizables como `%ifXXX` y `%elifXXX`, y negadas con `n` (`%ifnreg`):
  `reg` (un registro de la CPU), `sysreg` (`IP` `IR` `IV`), `num` (una expresión constante; un nombre de puerto suelto no es
  un número), `str` (un literal de cadena), `label` (`_name` o `.name`), `port` (un nombre de puerto de E/S),
  `idn a, b` / `idni a, b` (el mismo texto, exactamente o ignorando mayúsculas y minúsculas), `empty`, `macro NAME [count]`, `ctx NAME`.
  El texto se expande primero como macro. Esto es lo que permite que una macro elija entre `mov` e `in` según
  sus operandos, y que informe de un operando incorrecto con un mensaje de verdad (`%error "INC wants a register, got %1"`).
- **Cadenas**: `%strlen N "text"`, `%substr S "text", start [, length]` (start cuenta desde 1), `%strcat S "a",
  "b"`, `%defstr S tokens` (los tokens expandidos como cadena).
- **Contextos**: `%push name` ... `%pop` (`%repl name` renombra). `%$x` es un nombre privado del contexto
  más interno (`%$$x`: el de debajo), que es lo que permite que macros como `IF` / `ELSE` / `ENDIF` compartan etiquetas.
  `%ifctx name` prueba el contexto más interno.
- **`%pragma once`** (igual que `%includeonce`), `%pragma shadow ...`.

`lib/v32as.inc` tiene `INC DEC ZERO NEG CLAMP SWAP PUSHALL POPALL
WAIT_FRAMES` ya hechas (todas comprueban que sus operandos son registros) y
`lib/control.inc` tiene `IF` / `ELSE` / `ENDIF` y `WHILE` / `ENDWHILE`;
úsalas con `%include <v32as.inc>` y `%include <control.inc>` (páginas del
manual `v32as-inc(3)` y `v32as-control(3)`). Un v32as instalado o
descomprimido las encuentra por sí mismo; desde el árbol de fuentes, añade
`-I lib`.

### Límites

Una expansión de macro está limitada a 64 niveles, y una ejecución a unos 4
millones de líneas reproducidas (un `%rep` o una recursión descontrolados se
detienen con un error). El anidamiento de `%include` está limitado a 20
niveles y un `%rep` a 1.000.000 de iteraciones. Los errores desde dentro de
una expansión se informan en la línea de la invocación con un `note: in
expansion of macro 'X' (defined at file:line)`; `-g` también atribuye el
código generado a la invocación.

## Definición del cartucho y scripts de construcción

Un cartucho es un programa más las texturas y los sonidos que usa,
empaquetados por `packrom` a partir de una definición XML. Un programa
escrito en C (`v32cc`) dice qué entra en la ROM con `#texture`, `#sound`,
`#title` y `#version`; `v32as` tiene esas mismas cuatro como directivas del
preprocesador, y puede escribir la definición y los scripts que construyen el
cartucho. Todo ello es una extensión (no bajo `--standard`), y no cambia
nada en el programa ensamblado.

```
%title   "Space Rocks"            ; the name in the cartridge (at most 63 characters; the last %title wins)
%version 1.2                      ; MAJOR.MINOR, digits.digits (packrom accepts nothing else: an error otherwise)
%texture TEX_FONT  "gfx/font.png" ; TEX_FONT becomes the number of this texture: 0, 1, ... in order of appearance
%texture TEX_SHIP  "gfx/ship.png"
%texture           "gfx/old.vtex" ; the name is optional
%sound   SND_BOOM  "snd/boom.wav" ; sounds are numbered on their own, from 0

    mov R0, TEX_SHIP              ; the number select_texture takes
    out GPU_SelectedTexture, R0
```

- `NAME` es una macro simple que contiene el número, así que funciona dondequiera que funcione un número (`mov R0, TEX_SHIP`, `%if`, `%eval`). Las texturas y
  los sonidos se numeran por separado, en el orden en que aparecen por primera vez sus archivos; nombrar de nuevo el mismo archivo da el mismo número. Es la numeración
  que `packrom` da a las entradas de la definición, de modo que el programa y el cartucho coinciden.
- El archivo se busca junto al archivo fuente que contiene la directiva, y luego en los directorios `-I` (lo que hace `datafile`); el
  nombre tal como está escrito va a la definición. Un archivo que no se encuentra es una advertencia (el script usa entonces el nombre tal como está escrito).
- Las directivas en un bloque `%if` omitido, o en una macro que nunca se expande, no cuentan para nada; dentro de un archivo con `%include` funcionan, de modo que un
  `resources.inc` compartido es un buen lugar para ellas.
- Con `-C` (objetos) las directivas se aceptan pero no se guardan: una advertencia lo indica. Dalas en el código fuente de la ejecución final de `v32as`.

**`--rom-xml FILE`** escribe la definición tras un ensamblado correcto:

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

El tipo es `bios` con `-b`. Las rutas son relativas al archivo XML. Una
textura `.png` o un sonido `.wav` se nombra como el archivo `.vtex` /
`.vsnd` que `png2vircon` / `wav2vircon` hacen a partir de él, colocado junto
al XML (dos con el mismo nombre base reciben `_2`, `_3`, ...); cualquier otro
archivo se nombra tal cual, por su ruta desde el XML. Sin `%title` el título
es el nombre de la salida, sin `%version` es `1.0`. El texto se escapa
(`&amp;` y demás).

**`--build-scripts`** escribe `Make.sh` y `Make.bat`, que reconstruyen el
cartucho a partir de los fuentes, desde el optimizador en adelante:

1. **Optimizar**: `v32as -E -P` escribe el código fuente preprocesado (`FILEPre.asm`), y luego `v32opt -O3 FILEPre.asm -o FILEOpt.asm`; solo cuando `v32opt` está en el
   `PATH` (o `V32OPT` lo nombra) y `NO_OPT` no está definida. `V32OPT_FLAGS` sustituye a `-O3`. El optimizador recibe el texto preprocesado porque no
   conoce `%macro`, `%if` ni `%rep`: lee cada rama como código que se ejecuta una tras otra, y eliminó un `mov` que la rama `%else` "sobrescribía".
2. **Ensamblar**: `v32as` de nuevo, con las opciones de la ejecución que escribió el script (`-b`/`-c`, `-D`, `-U`, `-I`, `-w`, `-W...`, `-g`, `--map`)
   más `--rom-xml` / `--rom-xml-opt`, de modo que las definiciones siguen a las líneas `%title` y `%texture` en cada compilación; luego lo mismo para `FILEOpt.asm`.
3. **Convertir**: `png2vircon IN.png -o OUT.vtex` y `wav2vircon IN.wav -o OUT.vsnd` para cada recurso que lo necesite.
4. **Empaquetar**: `packrom NAME.xml -o bin/NAME.v32`, y `packrom NAMEOpt.xml -o bin/NAMEOpt.v32`.

El cartucho optimizado es un extra: si `v32opt` falla, o su salida no se
ensambla, el script lo dice y aun así construye `bin/NAME.v32`. Con más de
un archivo fuente no hay compilación optimizada. `v32opt` está hecho para la
salida de compiladores (etiquetas `__function_NAME:`): sobre código escrito a
mano hace sobre todo optimización de mirilla (*peephole*), y llama a `-O3`
agresivo, así que compara los dos cartuchos antes de fiarte del `Opt`.
`V32AS`, `V32OPT`, `V32OPT_FLAGS`, `PACKROM`, `PNG2VIRCON` y `WAV2VIRCON`
nombran herramientas que no están en el `PATH`. Las rutas de los scripts son
relativas al directorio del script, y `Make.sh` cambia a él por sí mismo, de
modo que se puede ejecutar desde cualquier lugar. Un script termina con
`BUILD SUCCESSFUL` (salida 0) o `BUILD FAILED` (salida 1).

Un script a mitad de una construcción nunca se reescribe, y un script que
escribiste tú nunca se sustituye:

- Un script lleva la línea `Generated by v32as` como comentario propio (`# ` o `REM `). Un `Make.sh` o `Make.bat` sin ella, incluido uno
  escrito por `v32cc`, se deja en paz con una advertencia (`--no-build-scripts` la silencia). Borra la línea de un script generado para conservar tus ediciones.
- Los scripts llaman a `v32as` con `--no-build-scripts` y establecen `V32AS_BUILD_SCRIPT=1`, que `v32as` trata igual, de modo que un `v32as` lanzado desde
  dentro de una construcción (por un envoltorio, o un Makefile que llame al script) no puede reescribir el script que se está ejecutando.
- Un script cuyo texto no cambiaría no se toca en absoluto. Uno que tiene que cambiar se escribe en un archivo temporal y se renombra sobre el
  antiguo, de modo que un shell que esté ejecutando el `Make.sh` antiguo sigue leyendo el texto antiguo. (`cmd.exe` lee un `.bat` por posición, así que para `Make.bat`
  los dos primeros puntos son lo que lo protege.)

Las pruebas están en `tests/rom_tests.sh` (`make test`), con sustitutos para
`packrom`, `png2vircon`, `wav2vircon` y `v32opt`; `Make.bat` se comprobó
leyéndolo, no ejecutándolo en Windows.

## Registros internos (extensión)

`IP`, `IR`, `IV` no son direccionables por la CPU, pero `MOV` los acepta y se
expande exactamente en una instrucción real, sin usar ningún registro
temporal oculto (A = dirección de la expansión):

| origen               | se expande en           | significado                               |
|----------------------|-------------------------|-------------------------------------------|
| `MOV Rn, IP`         | `MOV Rn, A+2`           | dirección de la siguiente instrucción     |
| `MOV Rn, IR`         | `MOV Rn, <this word>`   | codificación de la instrucción en ejecución|
| `MOV IP, Rn / addr`  | `JMP Rn / addr`         | salto                                     |
| `MOV IR, Rn`         | `MOV [A+2], Rn`         | sobrescribe la palabra de la siguiente instrucción |
| `MOV IV, Rn`         | `MOV [A+3], Rn`         | sobrescribe el inmediato de la siguiente instrucción|

`MOV Rn, IV` se rechaza (autorreferente); IR/IV no se pueden escribir desde
un inmediato. Las escrituras en IR/IV parchean el código en tiempo de
ejecución, así que ese código debe ejecutarse desde la RAM (el ensamblador
advierte en el modo --bios/--cart). `MOV Rn, IP` en el modo --none es
relativo a la dirección 0 (advierte).

## Verificación

La salida es idéntica byte a byte a la del ensamblador oficial de Vircon32
para: todos los códigos de operación en todas las formas de operandos
legales, todos los puertos de E/S y valores de puerto (BIOS y CART), y todas
las directivas de datos. Los `tests/golden/*.hex` los produjo el ensamblador
oficial. `make test` ejecuta las pruebas golden, diferenciales y de mensajes
de error.

`make realcheck` comprueba eso contra las propias herramientas oficiales.
Necesita una copia del repositorio oficial
[ComputerSoftware](https://github.com/vircon32/ComputerSoftware) junto a este
(`../ComputerSoftware` o `../ComputerSoftware.git`) o indicada con
`STOCK=DIR`, compila su `assemble` y su `compile` con `c++` en
`build/realcheck/stock` (no hace falta SDL), y luego ensambla con la
herramienta oficial, con `v32as --standard` y con `v32as` simple: los
programas de ejemplo del ensamblador oficial (como cartucho y como BIOS), los
programas de prueba del compilador de C tal como se distribuyen y tal como
los compila ahora el compilador oficial, la prueba del empaquetador de ROM, y
los propios fuentes de prueba de este proyecto. Un programa que la
herramienta oficial ensambla debe dar el mismo VBIN, byte a byte, en ambos
modos, y los mismos archivos `-g` en los tres modos; uno que ella rechaza
debería ser rechazado por `--standard` (los pocos que no lo son figuran como
tolerancias). `make realcheck COMBO=1` (`JOBS=N` procesos) añade todos los
códigos de operación con ninguno, uno y dos operandos tomados de un conjunto
de 28 formas de operando, unos 52.000 programas de una línea. Las
diferencias deliberadas están en `tests/realcheck.known`; los detalles de una
ejecución están en `build/realcheck/report.txt`. El workflow `Tests` lo
ejecuta contra las fuentes oficiales actuales en cada push, y con el barrido
completo cada semana.

## Licencia

Licencia BSD de 3 cláusulas, Copyright (c) 2026 Matthew Haas. Consulta el
archivo `LICENSE`.

## Historia y compatibilidad

`v32as` empezó como un port a C del `v32as` en bash (el mismo trabajo:
ensamblar el lenguaje ensamblador de Vircon32 en un VBIN, con un modo de
listado/detallado/paso a paso y un REPL interactivo), con flex para el
analizador léxico, bison para el sintáctico y tablas hash en lugar de
matrices de bash y `bc`. Diferencias con la versión en bash:

- La cabecera VBIN lleva el número real de palabras (bash lo fijaba en 155).
- Los puertos numéricos en `IN`/`OUT` funcionan (error de bash); los puertos desconocidos o truncados advierten.
- Los literales enteros dados a operaciones de clase flotante se convierten a flotante; ATAN2/POW solo admiten registros (como el oficial).
- `[R+0]` se codifica como `[R]` (como el oficial).
- Las directivas de datos son `integer`, `float`, `pointer`, `string`, `zstring`, `datafile` (consulta [Directivas de datos](#directivas-de-datos)).
- Añadido: `true/false`, literales de carácter, `0b`, octal, `#dec`, números con sufijo `h`; una etiqueta y una sentencia pueden compartir línea;
  los mnemónicos/registros no distinguen mayúsculas de minúsculas.

Lo que cambió en cada ronda de desarrollo está en `CHANGES.md`.
