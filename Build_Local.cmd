@echo off
setlocal

rem Local build of cangaroo with MSYS2 MinGW64 (same toolchain as the Windows CI job).
rem
rem Usage: double click, or run Build_Local.cmd
rem        Build_Local.cmd nopause   (any argument skips the final pause, for scripts)
rem Output:
rem   bin\cangaroo.exe   needs C:\msys64\mingw64\bin in PATH
rem   dist\cangaroo.exe  standalone: Qt, MinGW and Python runtime copied next to it
rem
rem Required MSYS2 packages (MINGW64):
rem   pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-qt6 mingw-w64-x86_64-qt6-tools
rem             mingw-w64-x86_64-make mingw-w64-x86_64-ntldd mingw-w64-x86_64-python
rem             mingw-w64-x86_64-pybind11 mingw-w64-x86_64-pkgconf

rem Installation folder of MSYS2
set "MSYS2_DIR=C:\msys64"

rem -------------------------------------------------------------------------------------------------

cd /d "%~dp0"

if not exist "%MSYS2_DIR%\usr\bin\bash.exe" (
    echo MSYS2 not found in "%MSYS2_DIR%". Set MSYS2_DIR in this file.
    goto Fail
)
if not exist "%MSYS2_DIR%\mingw64\bin\qmake6.exe" (
    echo qmake6 not found. Install the MSYS2 packages listed in this file.
    goto Fail
)

rem The exe is locked while cangaroo is running
tasklist /FI "IMAGENAME eq cangaroo.exe" | find /I "cangaroo.exe" >NUL
if not errorlevel 1 (
    echo cangaroo.exe is running. Close it and start the build again.
    goto Fail
)

set "MSYSTEM=MINGW64"
set "CHERE_INVOKING=1"
set "COMSPEC=%SystemRoot%\System32\cmd.exe"

"%MSYS2_DIR%\usr\bin\bash.exe" -lc "./scripts/build_local_windows.sh"
if errorlevel 1 (
    echo:
    echo Build FAILED.
    goto Fail
)

echo:
echo Build OK. Standalone program: %~dp0dist\cangaroo.exe
if "%~1" == "" pause
exit /b 0

:Fail
if "%~1" == "" pause
exit /b 1
