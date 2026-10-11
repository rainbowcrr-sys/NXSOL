@echo off
rem SPDX-License-Identifier: GPL-2.0-only
rem Build the NXIcons shared library on Windows 10.
rem
rem WHAT THIS PRODUCES, AND WHY IT IS NOT CALLED .so
rem
rem   Windows shared libraries are .dll. Linux shared libraries are .so.
rem   Same concept, different file format -- a .dll cannot be loaded on
rem   Linux and a .so cannot be loaded on Windows.
rem
rem   NX System runs on Linux, so the library the system itself uses is
rem   libnxicons.so and it has to be built there. What this script builds
rem   is the Windows equivalent, so that you can compile it, load it and
rem   check the glyph data on the machine you are actually sitting at.
rem
rem   Nothing about the source is platform-specific. The same two .c files
rem   produce either platform's library.
rem
rem USAGE
rem
rem   build-icons.cmd            build libnxicons.dll (all 14 glyphs)
rem   build-icons.cmd window     build libnxicons-window.dll (window only)
rem   build-icons.cmd system     build libnxicons-system.dll (system only)
rem
rem Requires clang, which tools\windows\fetch.cmd downloads for you.

setlocal enabledelayedexpansion

set "ROOT=%~dp0..\.."
set "SRC=%ROOT%\icons\src"

if not exist "%SRC%\glyphs.c" (
    echo ERROR: %SRC%\glyphs.c not found.
    echo Run this first:  python3 icons\gen_so.py
    exit /b 1
)

where clang >nul 2>&1
if errorlevel 1 (
    echo ERROR: clang not on PATH.
    echo Run tools\windows\fetch.cmd to download LLVM, then open a NEW
    echo terminal -- PATH changes do not reach one that is already open.
    exit /b 1
)

set "MODE=%~1"
if "%MODE%"=="" (
    set "DEF=-DNXICONS_MODULE_ALL"
    set "OUT=libnxicons.dll"
) else if /i "%MODE%"=="window" (
    set "DEF=-DNXICONS_MODULE=0"
    set "OUT=libnxicons-window.dll"
) else if /i "%MODE%"=="system" (
    set "DEF=-DNXICONS_MODULE=1"
    set "OUT=libnxicons-system.dll"
) else (
    echo Unknown mode "%MODE%". Use: window / system / ^(empty for all^)
    exit /b 1
)

echo.
echo   source   %SRC%
echo   output   %OUT%
echo   flags    %DEF%
echo.

clang -std=c17 -Wall -Wextra -O2 -shared %DEF% ^
      -I"%SRC%" ^
      "%SRC%\nxicons.c" "%SRC%\glyphs.c" ^
      -o "%ROOT%\build\%OUT%"

if errorlevel 1 (
    echo.
    echo Build FAILED.
    exit /b 1
)

echo.
echo Built: %ROOT%\build\%OUT%
echo.
echo This is the Windows equivalent of the .so. To produce the actual
echo Linux .so used by NX System, run on Linux:
echo     gcc -std=c17 -Wall -Wextra -O2 -fPIC -shared -DNXICONS_MODULE_ALL -Iicons/src ^
echo         icons/src/nxicons.c icons/src/glyphs.c -o build/libnxicons.so
endlocal
