@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

title SaturatorMixFX - Local Build

cd /d "%~dp0"

echo ============================================================
echo   SaturatorMixFX - lokaler Windows VST3 Build
echo ============================================================
echo.

where cmake >nul 2>nul
if errorlevel 1 (
    echo [FEHLER] CMake wurde nicht gefunden.
    echo Stelle sicher, dass CMake installiert und im PATH ist.
    goto :fail
)

set "SDK="

if defined VST3_SDK_ROOT (
    if exist "%VST3_SDK_ROOT%\CMakeLists.txt" set "SDK=%VST3_SDK_ROOT%"
)

if not defined SDK if exist "%~dp0vst3sdk\CMakeLists.txt" set "SDK=%~dp0vst3sdk"
if not defined SDK if exist "%~dp0..\vst3sdk\CMakeLists.txt" set "SDK=%~dp0..\vst3sdk"
if not defined SDK if exist "C:\VST3_SDK\CMakeLists.txt" set "SDK=C:\VST3_SDK"
if not defined SDK if exist "C:\SDKs\vst3sdk\CMakeLists.txt" set "SDK=C:\SDKs\vst3sdk"
if not defined SDK if exist "C:\dev\vst3sdk\CMakeLists.txt" set "SDK=C:\dev\vst3sdk"
if not defined SDK if exist "%USERPROFILE%\Documents\vst3sdk\CMakeLists.txt" set "SDK=%USERPROFILE%\Documents\vst3sdk"
if not defined SDK if exist "%USERPROFILE%\source\repos\vst3sdk\CMakeLists.txt" set "SDK=%USERPROFILE%\source\repos\vst3sdk"

if not defined SDK (
    echo [FEHLER] Steinberg VST3 SDK wurde nicht gefunden.
    echo.
    echo Moeglichkeiten:
    echo   1. Ordner "vst3sdk" direkt neben dieses Projekt legen
    echo   2. Umgebungsvariable VST3_SDK_ROOT setzen
    echo   3. SDK nach C:\VST3_SDK oder C:\SDKs\vst3sdk legen
    echo.
    goto :fail
)

echo [OK] VST3 SDK: "%SDK%"
echo.

set "BUILD_DIR=%~dp0build-local"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/2] CMake konfigurieren...
cmake -S "%~dp0" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DVST3_SDK_ROOT="%SDK%"
if errorlevel 1 goto :fail

echo.
echo [2/2] Release bauen...
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo   BUILD ERFOLGREICH
ECHO ============================================================
echo.

echo Suche SaturatorMixFX.vst3 ...
set "FOUND="
for /d /r "%BUILD_DIR%" %%D in (SaturatorMixFX.vst3) do (
    set "FOUND=%%~fD"
    goto :found
)

:found
if defined FOUND (
    echo [OK] Plugin gefunden:
    echo     !FOUND!
) else (
    echo [HINWEIS] Build war erfolgreich, aber der .vst3-Ordner wurde nicht automatisch gefunden.
    echo Build-Ordner: "%BUILD_DIR%"
)

echo.
echo Fenster kann jetzt geschlossen werden.
pause
exit /b 0

:fail
echo.
echo ============================================================
echo   BUILD FEHLGESCHLAGEN
ECHO ============================================================
echo.
echo Die Fehlermeldung steht weiter oben in diesem Fenster.
echo Bitte nichts wegklicken - den Text kannst du mir einfach schicken.
echo.
pause
exit /b 1
