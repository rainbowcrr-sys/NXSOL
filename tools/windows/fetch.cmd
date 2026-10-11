@echo off
rem SPDX-License-Identifier: GPL-2.0-only
rem NX System On Linux -- fetch.cmd, a wrapper so this can be run from CMD.
rem Copyright (C) 2026 rainbowcrr-sys -- GPL-2.0-only
rem
rem Double-clicking a .ps1 does nothing useful on a default Windows 10, so
rem this exists purely to launch the PowerShell script with the right
rem execution policy for this one invocation. Nothing is installed globally
rem and no machine policy is changed: -ExecutionPolicy Bypass is scoped to
rem this process only.
rem
rem   fetch.cmd              download everything into environment\
rem   fetch.cmd -List        show what would be downloaded
rem   fetch.cmd -CheckPath   check whether the tools are on PATH
rem   fetch.cmd -OpenPages   open the official download pages

setlocal
set "PS1=%~dp0fetch.ps1"

if not exist "%PS1%" (
    echo fetch.ps1 is missing next to this file.
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*

endlocal & exit /b %ERRORLEVEL%
