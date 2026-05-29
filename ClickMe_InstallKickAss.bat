@echo off
setlocal enabledelayedexpansion
title KickAss VST3 — One-Click Install

:: =============================================================================
::  KickAss VST3 — One-Click Installer
::  Copies the VST3 bundle to C:\Program Files\Common Files\VST3\
::  Run me after unzipping. Right-click → "Run as administrator" if needed.
:: =============================================================================

echo.
echo   ╔══════════════════════════════════════╗
echo   ║    KickAss VST3 — One-Click Install  ║
echo   ╚══════════════════════════════════════╝
echo.

set "SRC=%~dp0KickAss.vst3"
set "DEST=%CommonProgramFiles%\VST3\KickAss.vst3"

:: Check source exists
if not exist "%SRC%" (
    echo   [FAIL] Can't find KickAss.vst3 next to this script.
    echo          Make sure this .bat is in the same folder as KickAss.vst3\
    echo          and that you unzipped everything first.
    echo.
    pause
    exit /b 1
)

echo   Source : %SRC%
echo   Dest   : %DEST%
echo.

:: Try the copy
xcopy /E /I /Y "%SRC%" "%DEST%" >/dev/null 2>&1
if errorlevel 1 (
    echo   [WARN] Copy failed — probably need admin rights.
    echo          Right-click this script ^> "Run as administrator"
    echo          and try again.
    echo.
    pause
    exit /b 1
)

echo   [OK] KickAss.vst3 installed to:
echo        %DEST%
echo.
echo   [OK] Restart your DAW and find it under Gleinkaa ^> KickAss.
echo.
echo   Press any key to finish...
pause >/dev/null
endlocal
