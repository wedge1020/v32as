# ----------------------------------------------------------------------------
#   CMake build for v32as, the Vircon32 assembler (and its linker, v32asld)
#
#   Laid out after the CMake builds of v32lua, v32opt and v32c++ (v32lua is
#   the reference). This is an ALTERNATIVE to the base Makefile, which keeps
#   working exactly as before (`make`, `make test`, `make sysinstall`, ...);
#   the two never share build products. Build out of tree, and NOT in
#   ./build: the Makefile keeps its objects and generated sources there and
#   its `make clean` removes the directory.
#
#       cmake -S . -B build-cmake   (MSYS2/MinGW: add -G "MSYS Makefiles")
#       cmake --build build-cmake
#       ctest --test-dir build-cmake       (the regression tests; needs sh)
#       cmake --install build-cmake        (may need sudo / an elevated shell)
#       cmake --build build-cmake --target uninstall
#       cpack --config build-cmake/CPackConfig.cmake
#                                          (.tar.gz / .deb / .rpm, or .zip on Windows)
#
#   Default install locations (as for `make sysinstall`; the community tools
#   share v32tools/, next to the DevTools):
#
#       Linux / macOS: /usr/local/bin/v32as, v32asld
#                      /usr/local/share/man/man{1,3,7}/...
#                      /usr/local/share/v32as/lib/*.inc      (the %include library)
#                      /usr/local/Vircon32/v32tools/docs/v32as/
#       Windows      : <Program Files>/Vircon32/v32tools/v32as.exe, v32asld.exe
#                      <Program Files>/Vircon32/v32tools/include/v32as/*.inc
#                      <Program Files>/Vircon32/v32tools/docs/v32as/  (documents, manual pages)
#
#   Change the root with -DCMAKE_INSTALL_PREFIX=<dir> when configuring (or
#   `cmake --install ... --prefix <dir>`; DESTDIR works). The %include
#   library directory is compiled into v32as (V32AS_LIBDIR) as a fallback
#   after the -I directories; v32as also looks beside itself (lib, ../lib,
#   ../share/v32as/lib, include/v32as, ...), so a relocated tree keeps working.
#
#   flex and bison are REQUIRED (the lexer and parser are generated; none
#   are shipped). On Windows: pacman -S bison flex in the MSYS2 shell.
#   GNU readline is used for the interactive mode when found
#   (-DV32AS_READLINE=OFF to opt out).
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.13...3.31)

# Refuse an in-source build: CMake's generated Makefile would overwrite the
# project's own hand-written base Makefile.
#
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "v32as: in-source CMake builds are not supported -- they would "
        "overwrite the base Makefile. Build in a separate directory:\n"
        "    cmake -S . -B build-cmake\n"
        "First remove the CMakeCache.txt and CMakeFiles/ just created here.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Choose the type of build (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# ----------------------------------------------------------------------------
#   VERSION (single source: inc/v32as.h)
# ----------------------------------------------------------------------------

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/inc/v32as.h" V32AS_VERSION_LINE
     REGEX "^#define[ \t]+VERSION[ \t]+\"[^\"]*\"")
string(REGEX REPLACE "^#define[ \t]+VERSION[ \t]+\"([^\"]*)\".*" "\\1"
       V32AS_VERSION "${V32AS_VERSION_LINE}")
if(NOT V32AS_VERSION)
    message(FATAL_ERROR "v32as: could not read VERSION from inc/v32as.h")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/inc/v32as.h")

# project() only takes a numeric version: derive YYYY.MM.DD from the leading
# date (the "-dev"/"-release" status stays in V32AS_VERSION).
#
if(V32AS_VERSION MATCHES "^([0-9][0-9][0-9][0-9])([0-9][0-9])([0-9][0-9])")
    set(V32AS_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
else()
    set(V32AS_NUMERIC_VERSION "0.0.0")
endif()

project(v32as
        VERSION ${V32AS_NUMERIC_VERSION}
        DESCRIPTION "Assembler and linker for the Vircon32 fantasy console"
        HOMEPAGE_URL "https://github.com/wedge1020/v32as"
        LANGUAGES C)

# ----------------------------------------------------------------------------
#   IDENTIFY HOST ENVIRONMENT
# ----------------------------------------------------------------------------

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_BITS "64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(TARGET_BITS "32")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TARGET_OS "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TARGET_OS "mac")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TARGET_OS "linux")
else()
    set(TARGET_OS "${CMAKE_SYSTEM_NAME}")
endif()

# v32as is POSIX-style C (getopt_long, strcasecmp, <unistd.h>), which MinGW,
# glibc, musl, macOS and the BSDs all provide but Visual C++ does not. The
# Vircon32 DevTools are likewise built with MSYS2 + MinGW on Windows.
#
include(CheckIncludeFile)
check_include_file(getopt.h  V32AS_HAVE_GETOPT_H)
check_include_file(unistd.h  V32AS_HAVE_UNISTD_H)
check_include_file(strings.h V32AS_HAVE_STRINGS_H)
if(MSVC OR NOT V32AS_HAVE_GETOPT_H OR NOT V32AS_HAVE_UNISTD_H OR NOT V32AS_HAVE_STRINGS_H)
    message(FATAL_ERROR
        "v32as: needs a GCC- or Clang-style toolchain with a POSIX-style C "
        "library (getopt_long, <unistd.h>, <strings.h>). On Windows, build "
        "with MSYS2 + MinGW-w64, e.g. from a MINGW64 shell:\n"
        "    cmake -S . -B build-cmake -G \"MSYS Makefiles\"")
endif()

# ----------------------------------------------------------------------------
#   INSTALL LOCATIONS
# ----------------------------------------------------------------------------

# On Windows, CMake's default prefix would be "<Program Files>/v32as". Install
# under "<Program Files>/Vircon32" instead -- the DevTools' own root -- with
# v32as.exe in v32tools/, beside v32opt.exe, v32lua.exe and v32c++.exe, so one
# PATH entry covers the community tools. An explicit -DCMAKE_INSTALL_PREFIX
# always wins.
#
if(TARGET_OS STREQUAL "windows" AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    get_filename_component(V32AS_PREFIX_NAME   "${CMAKE_INSTALL_PREFIX}" NAME)
    get_filename_component(V32AS_PROGRAM_FILES "${CMAKE_INSTALL_PREFIX}" DIRECTORY)
    if(V32AS_PREFIX_NAME STREQUAL PROJECT_NAME)
        set(CMAKE_INSTALL_PREFIX "${V32AS_PROGRAM_FILES}/Vircon32" CACHE PATH
            "Install path prefix, prepended onto install directories." FORCE)
    endif()
endif()

include(GNUInstallDirs)   # bin, share/man (CMAKE_INSTALL_PREFIX defaults to /usr/local on Unix)

if(TARGET_OS STREQUAL "windows")
    set(V32AS_TOOLS_ROOT        "v32tools")
    set(V32AS_DEFAULT_BINDIR    "v32tools")
    set(V32AS_DEFAULT_MANROOT   "v32tools/docs/v32as/man")
    set(V32AS_DEFAULT_LIBDIR    "v32tools/include/v32as")
else()
    set(V32AS_TOOLS_ROOT        "Vircon32/v32tools")
    set(V32AS_DEFAULT_BINDIR    "${CMAKE_INSTALL_BINDIR}")
    set(V32AS_DEFAULT_MANROOT   "${CMAKE_INSTALL_MANDIR}")
    set(V32AS_DEFAULT_LIBDIR    "${CMAKE_INSTALL_DATADIR}/v32as/lib")
endif()
set(V32AS_DEFAULT_DOCDIR        "${V32AS_TOOLS_ROOT}/docs/v32as")

set(V32AS_INSTALL_BINDIR "${V32AS_DEFAULT_BINDIR}" CACHE STRING
    "Where v32as and v32asld are installed (relative to CMAKE_INSTALL_PREFIX)")
set(V32AS_INSTALL_MANROOT "${V32AS_DEFAULT_MANROOT}" CACHE STRING
    "Manual tree: man1/, man3/ and man7/ pages go under it (relative to CMAKE_INSTALL_PREFIX)")
set(V32AS_INSTALL_LIBDIR "${V32AS_DEFAULT_LIBDIR}" CACHE STRING
    "Where lib/*.inc (the %include library) goes, relative to CMAKE_INSTALL_PREFIX or absolute; compiled into v32as")
set(V32AS_INSTALL_DOCDIR "${V32AS_DEFAULT_DOCDIR}" CACHE STRING
    "Where the README and docs/ are installed (relative to CMAKE_INSTALL_PREFIX; empty = not installed)")

# The absolute %include directory v32as is built to search.
#
if(IS_ABSOLUTE "${V32AS_INSTALL_LIBDIR}")
    set(V32AS_LIBRARY_PATH "${V32AS_INSTALL_LIBDIR}")
else()
    set(V32AS_LIBRARY_PATH "${CMAKE_INSTALL_PREFIX}/${V32AS_INSTALL_LIBDIR}")
endif()

# ----------------------------------------------------------------------------
#   PARSER AND LEXER (bison and flex are required: nothing generated is shipped)
# ----------------------------------------------------------------------------

find_package(BISON 3.0)
find_package(FLEX)
if(NOT BISON_FOUND OR NOT FLEX_FOUND)
    message(FATAL_ERROR
        "v32as: bison and flex are required to generate the parser and lexer "
        "(found: bison=${BISON_FOUND}, flex=${FLEX_FOUND}). Install them "
        "(Debian: bison flex; macOS: brew install bison flex; MSYS2: pacman -S bison flex) "
        "or put them on the PATH.")
endif()

set(V32AS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${V32AS_GENERATED_DIR}")
BISON_TARGET(v32as_parser "${CMAKE_CURRENT_SOURCE_DIR}/src/parser.y"
             "${V32AS_GENERATED_DIR}/parser.tab.c"
             DEFINES_FILE "${V32AS_GENERATED_DIR}/parser.tab.h"
             COMPILE_FLAGS "-Wall")
FLEX_TARGET(v32as_lexer "${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.l"
            "${V32AS_GENERATED_DIR}/lexer.c")
ADD_FLEX_BISON_DEPENDENCY(v32as_lexer v32as_parser)
set(V32AS_PARSER_SRC ${BISON_v32as_parser_OUTPUTS} ${FLEX_v32as_lexer_OUTPUTS})

# ----------------------------------------------------------------------------
#   SOURCE FILES
# ----------------------------------------------------------------------------

set(V32AS_CORE_SRC
    src/asm.c
    src/tables.c
    src/pp.c
    src/buildscript.c
    src/ppexpr.c
    src/main.c)
file(GLOB V32AS_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h")

# ----------------------------------------------------------------------------
#   EXECUTABLES
# ----------------------------------------------------------------------------

add_executable(v32as ${V32AS_CORE_SRC} ${V32AS_PARSER_SRC} ${V32AS_HEADERS})
add_executable(v32asld src/ld.c ${V32AS_HEADERS})

# gnu11, as the base Makefile: strdup, strcasecmp and getopt_long come with it.
set_target_properties(v32as v32asld PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON)

target_include_directories(v32as PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/inc" "${V32AS_GENERATED_DIR}")
target_include_directories(v32asld PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/inc")

# the compiled-in %include directory (main.c only), searched after the -I ones
set_source_files_properties(src/main.c PROPERTIES
    COMPILE_DEFINITIONS "V32AS_LIBDIR=\"${V32AS_LIBRARY_PATH}\"")

# Warning flags, matching the base Makefile where the compiler supports them
# (flex/bison output trips -Wextra: that is theirs, not ours)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(v32as   PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(v32asld PRIVATE -Wall -Wextra -Wno-unused-parameter)
    set_source_files_properties(${BISON_v32as_parser_OUTPUT_SOURCE} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-sign-compare")
    set_source_files_properties(${FLEX_v32as_lexer_OUTPUTS} PROPERTIES
        COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-function")
endif()

# The math library, where it is a separate one.
if(UNIX)
    target_link_libraries(v32as PRIVATE m)
endif()

# GNU readline (interactive mode), optional: AUTO uses it when found.
set(V32AS_READLINE "AUTO" CACHE STRING "Use GNU readline for the interactive mode: AUTO, ON or OFF")
set_property(CACHE V32AS_READLINE PROPERTY STRINGS AUTO ON OFF)
set(V32AS_HAVE_READLINE FALSE)
if(NOT V32AS_READLINE STREQUAL "OFF")
    find_path(V32AS_READLINE_INCLUDE_DIR readline/readline.h)
    find_library(V32AS_READLINE_LIBRARY NAMES readline)
    if(V32AS_READLINE_INCLUDE_DIR AND V32AS_READLINE_LIBRARY)
        set(V32AS_HAVE_READLINE TRUE)
        target_compile_definitions(v32as PRIVATE HAVE_READLINE)
        target_include_directories(v32as PRIVATE "${V32AS_READLINE_INCLUDE_DIR}")
        target_link_libraries(v32as PRIVATE "${V32AS_READLINE_LIBRARY}")
    elseif(V32AS_READLINE STREQUAL "ON")
        message(FATAL_ERROR "v32as: V32AS_READLINE=ON but GNU readline was not found")
    endif()
endif()

# MinGW: link the C runtime support statically, so the installed .exe files
# need no MinGW DLLs next to them.
option(V32AS_STATIC_MINGW "Statically link MinGW runtime libraries" ON)
if(MINGW AND V32AS_STATIC_MINGW)
    target_link_options(v32as   PRIVATE -static)
    target_link_options(v32asld PRIVATE -static)
endif()

# ----------------------------------------------------------------------------
#   SHOW BUILD INFORMATION IN PRETTY FORMAT
# ----------------------------------------------------------------------------

message(STATUS "******** v32as ********")
message(STATUS "Version: ${V32AS_VERSION}")
if(NOT TARGET_OS STREQUAL "mac")
    message(STATUS "Target OS: ${TARGET_OS} ${TARGET_BITS}bit")
else()
    message(STATUS "Target OS: ${TARGET_OS} ${TARGET_BITS}bit (SDK: ${CMAKE_OSX_SYSROOT})")
endif()
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")
if(CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Build type: (chosen at build time)")
else()
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "Parser: bison ${BISON_VERSION}, flex ${FLEX_VERSION}; readline: ${V32AS_HAVE_READLINE}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS " * executables  -> ${V32AS_INSTALL_BINDIR}")
message(STATUS " * manual pages -> ${V32AS_INSTALL_MANROOT}/man1, man3, man7")
message(STATUS " * %include lib -> ${V32AS_INSTALL_LIBDIR} (compiled in: ${V32AS_LIBRARY_PATH})")
if(V32AS_INSTALL_DOCDIR)
    message(STATUS " * documents    -> ${V32AS_INSTALL_DOCDIR}")
endif()

# ----------------------------------------------------------------------------
#   DEFINE THE INSTALL PROCESS
# ----------------------------------------------------------------------------

install(TARGETS v32as v32asld
        RUNTIME DESTINATION "${V32AS_INSTALL_BINDIR}"
        COMPONENT binaries)

file(GLOB V32AS_MAN1 "${CMAKE_CURRENT_SOURCE_DIR}/man/man1/*.1")
file(GLOB V32AS_MAN3 "${CMAKE_CURRENT_SOURCE_DIR}/man/man3/*.3")
file(GLOB V32AS_MAN7 "${CMAKE_CURRENT_SOURCE_DIR}/man/man7/*.7")
install(FILES ${V32AS_MAN1} DESTINATION "${V32AS_INSTALL_MANROOT}/man1" COMPONENT documentation)
install(FILES ${V32AS_MAN3} DESTINATION "${V32AS_INSTALL_MANROOT}/man3" COMPONENT documentation)
install(FILES ${V32AS_MAN7} DESTINATION "${V32AS_INSTALL_MANROOT}/man7" COMPONENT documentation)

# The %include library
file(GLOB V32AS_LIBRARY_FILES "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.inc")
install(FILES ${V32AS_LIBRARY_FILES}
        DESTINATION "${V32AS_INSTALL_LIBDIR}"
        COMPONENT library)

if(V32AS_INSTALL_DOCDIR)
    file(GLOB V32AS_DOC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/*.md")
    install(FILES README.md CHANGES.md LICENSE
            DESTINATION "${V32AS_INSTALL_DOCDIR}"
            COMPONENT documentation)
    install(FILES ${V32AS_DOC_FILES}
            DESTINATION "${V32AS_INSTALL_DOCDIR}/docs"
            COMPONENT documentation)
endif()

if(TARGET_OS STREQUAL "windows")
    install(CODE "message(STATUS \"v32as: add ${CMAKE_INSTALL_PREFIX}/${V32AS_INSTALL_BINDIR} to your PATH\")")
endif()

# `cmake --build <dir> --target uninstall` removes whatever the last install
# put down (CMake has no built-in uninstall; this replays install_manifest.txt),
# then the directories that leaves empty.
#
if(TARGET_OS STREQUAL "windows")
    set(V32AS_UNIX_VIRCON32_DIR "")                              # that is the prefix
else()
    set(V32AS_UNIX_VIRCON32_DIR "${CMAKE_INSTALL_PREFIX}/Vircon32")
endif()
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        @ONLY)
    add_custom_target(uninstall
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        COMMENT "Uninstalling v32as")
endif()

# ----------------------------------------------------------------------------
#   TESTS (ctest)
# ----------------------------------------------------------------------------

# The same suites as the Makefile's `make test` / `make mancheck`: the
# regression tests (tests/run_tests.sh: golden output, error messages, the
# preprocessor, linking, maps, ...), %include found beside the program, and
# the manual page lint. They are POSIX sh scripts: skipped without an sh
# (a plain Windows shell; use the MSYS2 one).
#
option(BUILD_TESTING "Define the ctest tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_test(NAME version COMMAND v32as --version)
    set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "${V32AS_VERSION}")
    find_program(V32AS_SH NAMES sh bash)
    if(V32AS_SH)
        add_test(NAME regression
                 COMMAND "${V32AS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh"
                         "$<TARGET_FILE:v32as>" "$<TARGET_FILE:v32asld>")
        set_tests_properties(regression PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
        add_test(NAME exe_relative
                 COMMAND "${V32AS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/exe_relative.sh"
                         "$<TARGET_FILE:v32as>")
        add_test(NAME rom_tests
                 COMMAND "${V32AS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rom_tests.sh"
                         "$<TARGET_FILE:v32as>")
        set_tests_properties(rom_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
        add_test(NAME mancheck
                 COMMAND "${V32AS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/mancheck.sh"
                         ${V32AS_MAN1} ${V32AS_MAN3} ${V32AS_MAN7})
    endif()
endif()

# ----------------------------------------------------------------------------
#   DEFINE THE PACKAGE CREATION WITH CPACK
# ----------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "v32as")
set(CPACK_PACKAGE_VENDOR "Matthew Haas")
set(CPACK_PACKAGE_CONTACT "Matthew Haas")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Assembler and linker for the Vircon32 fantasy console")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wedge1020/v32as")
set(CPACK_PACKAGE_FILE_NAME "v32as-${V32AS_VERSION}-${TARGET_OS}${TARGET_BITS}")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()

if(TARGET_OS STREQUAL "linux")
    set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Matthew Haas")
    set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_GENERATOR "TGZ")
    find_program(V32AS_DPKG_DEB dpkg-deb)
    if(V32AS_DPKG_DEB)
        list(APPEND CPACK_GENERATOR "DEB")
    endif()
    find_program(V32AS_RPMBUILD rpmbuild)
    if(V32AS_RPMBUILD)
        list(APPEND CPACK_GENERATOR "RPM")
    endif()
elseif(TARGET_OS STREQUAL "windows")
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
